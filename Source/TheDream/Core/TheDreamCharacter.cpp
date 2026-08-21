// Copyright Epic Games, Inc. All Rights Reserved.

#include "TheDreamCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "TheDream.h"
// The facing lock error is logged on LogTDCombatTiming; kept past the facing pass deliberately.
#include "Combat/TDCombatDebug.h"

ATheDreamCharacter::ATheDreamCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// **Change this and InitCapsuleSize together, always.** The mesh hangs from the capsule's
	// centre, so this must be the negative of the half-height directly above it or the feet do not
	// touch the ground. A mismatch hovers the character, and ABP_Combat's foot-IK Control Rig
	// absorbs it silently -- so the hover shows only where that IK does not run: inside montages,
	// and in mid-air where ShouldDoIKTrace is false. SKM_Manny's reference pose puts its lowest
	// point at Z = -0.02, so the mesh origin is the feet and no further offset is owed.
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -96.0f));

	// Characters are invisible to the camera boom's collision probe, which sweeps on ECC_Camera.
	// Without this the spring arm treats another combatant as an obstruction and yanks the camera
	// forward -- and melee spends all of its time at exactly the range that triggers it.
	//
	// Narrower than switching off bDoCollisionTest: level geometry must still push the camera in,
	// so only bodies are exempt. The cost is that the camera may pass through an opponent at very
	// close range, much less disruptive than the pull-in. A call rather than two inline lines
	// because setting it once is not enough -- see ApplyCameraCollisionExemption.
	ApplyCameraCollisionExemption();

	// Facing is camera-relative, not movement-relative: the character faces where the camera
	// looks so it can strafe and backpedal. These are only the at-rest defaults -- from the
	// first frame onward UpdateCameraRelativeFacing() owns both yaw flags.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->bUseControllerDesiredRotation = true;

	// Let the smooth turn keep running while a root-motion montage plays. UE defaults this off,
	// which silently kills PhysicsRotation -- and therefore bUseControllerDesiredRotation -- for the
	// whole duration of any montage with root motion. **This hands rotation back to every
	// root-motion ability, not just attacks**, so anything wanting a fixed direction must say so
	// through SetAbilityFacingLocked rather than inheriting one from a suppression it never asked
	// for.
	GetCharacterMovement()->bAllowPhysicsRotationDuringAnimRootMotion = true;

	// **RotationRate.Yaw is rewritten every frame from TurnRateDegrees**, so editing it on a
	// Blueprint does nothing and reverts invisibly. Change that property instead. Only pitch and
	// roll, which nothing drives, are authored here. The yaw seeded here is cosmetic, so the value
	// is never garbage on frame zero -- keep it equal to TurnRateDegrees' default, or a reader
	// finds two rates and has to work out which wins.
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 1200.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from
	// Character) are set in BP_PlayerCharacter, to avoid direct content references in C++. It uses
	// GDHBundle's SKM_Manny rather than Epic's SKM_Manny_Simple, because the Sword and Shield
	// sockets the props attach to exist only on that mesh -- see Docs/Animation-Library.md.
}

void ATheDreamCharacter::ApplyCameraCollisionExemption()
{
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}

	if (USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		SkeletalMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}
}

void ATheDreamCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateCameraRelativeFacing(DeltaSeconds);
}

void ATheDreamCharacter::UpdateCameraRelativeFacing(float DeltaSeconds)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// Simulated proxies do not decide their own facing; it arrives replicated with the rest of the
	// movement state. Everyone else runs this -- the owning client because it predicts, the server
	// because it decides, and the AI-possessed dummy because it is an authority pawn whose death
	// still has to stop it turning. Guarded explicitly rather than left to
	// UCharacterMovementComponent::PhysicsRotation's own early-out on a null Controller: that is an
	// engine detail nobody here chose, and the facing lock is set by an ability a proxy never runs.
	if (!IsLocallyControlled() && !HasAuthority())
	{
		return;
	}

	// Debug only, and sampled before the lock check so it keeps reporting while an ability holds
	// facing -- watching the error grow during a swing is how you see what the freeze cost.
	if (const AController* FacingController = GetController())
	{
		FacingErrorDegrees = FMath::FindDeltaAngleDegrees(
			GetActorRotation().Yaw, FacingController->GetControlRotation().Yaw);
	}

	// Both rotation sources are switched off rather than merely left alone. Returning early
	// without this would freeze them at whatever they were on the last live frame -- and if
	// that frame had movement input, bUseControllerRotationYaw stays true and the character
	// keeps turning with the camera, which is exactly the bug this fixes.
	if (IsFacingLocked())
	{
		bUseControllerRotationYaw = false;
		Movement->bUseControllerDesiredRotation = false;
		return;
	}

	// **Target Lock's homing, and it takes facing outright rather than competing for it.**
	//
	// Two rules writing one yaw is last-writer-wins, and made to compete at equal rates it would
	// deadlock: turning away raises the bearing and homing pulls back exactly as hard, so a tagged
	// target could never be left. **The player's authority moves up a level instead** -- the wedge
	// is evaluated from the *camera*, so aiming at a different target selects it and the body
	// follows. Steering is expressed as choosing, not as fighting. Runs at TurnRateDegrees, so no
	// new number exists to keep in step with the aim guarantee.
	if (float HomingYaw = 0.0f; GetFacingHomingYaw(HomingYaw))
	{
		bUseControllerRotationYaw = false;
		Movement->bUseControllerDesiredRotation = false;

		FRotator Rotation = GetActorRotation();
		Rotation.Yaw = FMath::FixedTurn(Rotation.Yaw, HomingYaw, TurnRateDegrees * DeltaSeconds);
		SetActorRotation(Rotation);
		return;
	}

	// Written every frame rather than once at construction, so the rate stays live-tunable in PIE.
	// **Three rates, and only one of them is an aim value.** The fast rate resumes on the press and
	// the whole first 150 ms of every attack runs at it, which is where the guarantee lives -- so
	// the other two apply only outside that window and cannot affect aim at any value. The coil
	// rate wins over idle because holding an attack is not idle; IsIdle() is already false then,
	// and the order is stated rather than relied upon.
	Movement->RotationRate.Yaw = bAbilityCoiling
		? CoilTurnRateDegrees
		: (IsIdle() ? IdleTurnRateDegrees : TurnRateDegrees);

	// **One rotation source, always.** bUseControllerRotationYaw is the *snap* -- it assigns yaw
	// from the controller every frame, ignoring RotationRate entirely -- and is never enabled now.
	// Leaving it on would silently disable the smooth turn below, since it wins over the movement
	// component's desired-rotation path. Nothing scales between the two: attacks freeze facing
	// through IsFacingLocked() above and hand it straight back, and IdleTurnRateDegrees covers the
	// catch-up an interpolation would have smoothed.
	bUseControllerRotationYaw = false;
	Movement->bUseControllerDesiredRotation = true;
}

bool ATheDreamCharacter::IsIdle() const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return false;
	}

	// Airborne is never idle. Either a jump put us here, which was a press, or we walked off
	// something, which was one moments ago -- and in both cases a slow turn on a character with
	// no ground contact is the opposite of what the idle rate exists for.
	if (Movement->IsFalling())
	{
		return false;
	}

	FVector Input = Movement->GetLastInputVector();
	Input.Z = 0.0f;

	return Input.IsNearlyZero();
}

void ATheDreamCharacter::SetAbilityFacingLocked(bool bLocked)
{
	// Sample on the rising edge only: the error at the moment facing was taken is the number
	// that describes where the attack's wedge ended up pointing. Computed here rather than read
	// from the cached value, because an ability can call this either side of the character's
	// tick and a frame of skew is most of a 150 ms commit window.
	if (bLocked && !bAbilityFacingLocked)
	{
		if (const AController* FacingController = GetController())
		{
			FacingErrorAtLockDegrees = FMath::FindDeltaAngleDegrees(
				GetActorRotation().Yaw, FacingController->GetControlRotation().Yaw);

			// This number cannot be read off a HUD by the same person performing the flick, and it
			// is the only way the aim consequence of TurnRateDegrees is visible at all. Carries the
			// rate so a sweep cannot be misattributed afterwards.
			//
			// **A clean err= is not a clean bill of health.** err answers "is the body aligned with
			// the camera *now*"; camDelta answers "did the camera move since the player asked for
			// this swing". A flick made during a buffered press reads err=+0.0, camDelta=-170.
			const float CameraDeltaDegrees = (AimPressWorldTime >= 0.0f)
				? FMath::FindDeltaAngleDegrees(AimPressControlYawDegrees, FacingController->GetControlRotation().Yaw)
				: 0.0f;
			const float SincePressMs = (AimPressWorldTime >= 0.0f && GetWorld())
				? (GetWorld()->GetTimeSeconds() - AimPressWorldTime) * 1000.0f
				: -1.0f;

			TD_TIMING_LOG(TEXT("FACING LOCK  err=%+.1f deg  rate=%.0f  camDelta=%+.1f since press %.0fms"),
				FacingErrorAtLockDegrees,
				TurnRateDegrees,
				CameraDeltaDegrees,
				SincePressMs);
		}
	}

	bAbilityFacingLocked = bLocked;
}

void ATheDreamCharacter::SetAbilityCoiling(bool bCoiling)
{
	bAbilityCoiling = bCoiling;
}

void ATheDreamCharacter::SetAbilityMovementLocked(bool bLocked)
{
	bAbilityMovementLocked = bLocked;
}

void ATheDreamCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// **Jump is deliberately not bound here.** It is a GameplayAbility, so its press and release
		// both arrive through ATDCombatCharacter's AbilityInputActions map like every other combat
		// input, and binding it twice would launch a jump that GA_Jump had just refused. JumpAction
		// itself is kept -- removing a UPROPERTY orphans every CDO override -- but nothing reads it.

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ATheDreamCharacter::Move);

		// **Completed matters, and only because of LastRequestedMoveInput.** Triggered stops firing
		// the instant the keys come up, so without this edge nothing would ever write a zero and the
		// recorded direction would outlive the press forever -- a dodge from standing still would
		// inherit whichever way you last walked. Harmless for movement itself: it arrives as
		// AddMovementInput with a zero magnitude.
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &ATheDreamCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ATheDreamCharacter::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ATheDreamCharacter::Look);
	}
	else
	{
		UE_LOG(LogTheDream, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void ATheDreamCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void ATheDreamCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void ATheDreamCharacter::DoMove(float Right, float Forward)
{
	// An ability owning movement means the player cannot walk out of it. Gated on the *input* rather
	// than by disabling the movement component, because the ability itself still has to move: an
	// attack's lunge, a dodge's dash and any knockback all run through CMC.
	//
	// **The input is recorded before the gate, and only applying it is gated.** Returning before
	// AddMovementInput leaves GetLastInputVector() empty, so anything asking "which way is the
	// player holding" reads LastRequestedMoveInput -- see UTDDodgeAbility::ResolveDodgeDirection,
	// which would otherwise resolve every dodge cancelling an attack to the standing-still default.
	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// Recorded whether or not an ability is letting it through. Not normalised: magnitude is
		// the analogue stick's, and callers that only want a heading normalise it themselves.
		LastRequestedMoveInput = ForwardDirection * Forward + RightDirection * Right;

		if (IsMovementLocked())
		{
			return;
		}

		// add movement
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void ATheDreamCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void ATheDreamCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void ATheDreamCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}

float ATheDreamCharacter::GetAimYawDegrees() const
{
	// A player's aim is the camera, read off the controller. The AI-possessed dummy answers here
	// too: a stock AAIController copies its control rotation from the pawn, or points it at a
	// focus while the debug attacker swings -- an AI's aim *is* its facing or its target. The
	// body below is the fallback only for a pawn with no controller at all.
	if (const AController* AimController = GetController())
	{
		return AimController->GetControlRotation().Yaw;
	}

	return GetActorRotation().Yaw;
}

void ATheDreamCharacter::NoteAimPress()
{
	// Captured on the *press* rather than looked up when an ability activates, for the same reason
	// the buffer stores its move heading: by activation the camera has moved, and reading it then
	// measures the wrong instant. A press that never becomes an attack simply leaves a stale value
	// that the next one overwrites, which costs nothing.
	if (const AController* AimController = GetController())
	{
		AimPressControlYawDegrees = AimController->GetControlRotation().Yaw;
		AimPressWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f;
	}
}
