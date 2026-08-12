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
// Temporary, with the facing polish pass: the lock error is logged on LogTDCombatTiming.
#include "Combat/TDCombatDebug.h"

ATheDreamCharacter::ATheDreamCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// The mesh hangs from the capsule's centre, so this number must be the negative of the
	// half-height directly above it or the feet do not touch the ground. ACharacter defaults it
	// to -90 and nothing ever reconciled that with the 96 we set ourselves -- the two lived in
	// different files with no stated relationship -- so the feet floated exactly 6 cm in every
	// pose, on both characters.
	//
	// It went unnoticed for a long time because ABP_Combat's foot-IK Control Rig spent 6 cm of
	// correction every frame absorbing it. The hover was therefore only visible wherever that IK
	// does not run: inside montages, which is why attacks and dodges hovered and locomotion did
	// not, and in mid-air, where ShouldDoIKTrace is false. Three hypotheses about the animations
	// were wrong before anyone measured these two numbers.
	//
	// Change this and InitCapsuleSize together, always. SKM_Manny's reference pose puts its
	// lowest point at Z = -0.02, so the mesh origin is the feet and no further offset is owed.
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -96.0f));

	// Characters are invisible to the camera boom's collision probe, which sweeps on ECC_Camera.
	// Without this the spring arm treats another combatant as an obstruction and yanks the camera
	// forward -- and melee spends all of its time at exactly the range that triggers it, so an
	// opponent standing where they are supposed to stand is what breaks the shot.
	//
	// Deliberately narrower than switching off bDoCollisionTest: level geometry must still push
	// the camera in, or it ends up inside a wall. Only bodies are exempt. The cost is that the
	// camera may pass through an opponent at very close range, which is the conventional trade
	// and much less disruptive than the pull-in.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		
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
	// which silently kills PhysicsRotation -- and therefore bUseControllerDesiredRotation -- for
	// the whole duration of any montage with root motion. Attacks have root motion, so a player
	// standing still could not turn during a swing at all, and chaining attacks with the camera
	// turned meant each one landed nearer the camera without ever reaching it.
	//
	// **This hands rotation back to every root-motion ability, not just attacks**, which is the
	// part to remember: it removed the dodge's committed direction, because the dodge had been
	// getting that for free from a suppression it never asked for. Anything that wants a fixed
	// direction must now say so through ATheDreamCharacter::SetAbilityFacingLocked.
	GetCharacterMovement()->bAllowPhysicsRotationDuringAnimRootMotion = true;

	// RotationRate.Yaw is rewritten every frame from TurnRateDegrees, so editing it
	// on a Blueprint does nothing and reverts invisibly. Change that property instead. Only
	// pitch and roll, which nothing drives, are actually authored here.
	//
	// The yaw seeded here is cosmetic and exists so the value is never garbage on frame zero;
	// it is deliberately the same number as TurnRateDegrees' default so a reader does not find
	// two rates and have to work out which one wins. It stayed at 500 for a while after the
	// real one moved, which is exactly the confusion worth avoiding.
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

void ATheDreamCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateCameraRelativeFacing();
}

void ATheDreamCharacter::UpdateCameraRelativeFacing()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// Simulated proxies do not decide their own facing; it arrives replicated from the server
	// with the rest of the movement state. Everyone else runs this -- the owning client because
	// it predicts, the server because it decides, and the training dummy because it is an
	// unpossessed authority pawn whose death still has to stop it turning.
	//
	// This was benign before it was guarded, but only by accident: a simulated proxy has no
	// Controller, so UCharacterMovementComponent::PhysicsRotation returns before
	// bUseControllerDesiredRotation does anything. That is an engine implementation detail
	// nobody here chose, and the attack facing lock is the first state to run through this
	// function that the proxy cannot compute for itself -- it is set by the ability, which a
	// proxy never runs. Relying on a stranger's early-out to keep that harmless is the shape of
	// bug this project files traps about.
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

	// Written every frame rather than once at construction, so the rate stays live-tunable in
	// PIE. That is what let it be swept mid-session against the debug HUD's lock readout, and
	// it is how the 1200 was arrived at rather than guessed.
	//
	// The idle rate is a separate concern and cannot affect aim: the fast rate resumes on the
	// press, so the attack's whole windup runs at TurnRateDegrees regardless of how far facing
	// had drifted beforehand. See IdleTurnRateDegrees.
	Movement->RotationRate.Yaw = IsIdle() ? IdleTurnRateDegrees : TurnRateDegrees;

	// One rotation source, always. bUseControllerRotationYaw is the *snap* -- it assigns yaw
	// from the controller every frame, ignoring RotationRate entirely -- and it is deliberately
	// never enabled now. Leaving it on would silently disable the smooth turn below, since it
	// wins over the movement component's desired-rotation path.
	//
	// Nothing scales between the two. Attacks freeze facing through IsFacingLocked() above and
	// hand it straight back; a version that faded was built and removed the same day, because
	// any scale below full authority disabled the snap that then existed. That failure mode is
	// gone with the snap itself, but interpolation is still unowned work to revisit.
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

			// Kept past the pass that added it, behind the existing cvar. This number cannot be
			// read off a HUD by the same person performing the flick, and it is the only way the
			// aim consequence of TurnRateDegrees is visible at all -- which matters because that
			// rate is derived from the light's commit time, and Lunge + Recovery moves things
			// near it. Carries the rate so a sweep cannot be misattributed afterwards.
			TD_TIMING_LOG(TEXT("FACING LOCK  err=%+.1f deg  rate=%.0f"),
				FacingErrorAtLockDegrees,
				TurnRateDegrees);
		}
	}

	bAbilityFacingLocked = bLocked;
}

void ATheDreamCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ATheDreamCharacter::Move);
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
	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

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
