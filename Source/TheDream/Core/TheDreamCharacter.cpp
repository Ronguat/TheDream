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

ATheDreamCharacter::ATheDreamCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

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

	// RotationRate.Yaw is rewritten every frame from StationaryTurnRateDegrees, so editing it
	// on a Blueprint does nothing and reverts invisibly. Change that property instead. Only
	// pitch and roll, which nothing drives, are actually authored here.
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

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

	// Only the smooth branch reads RotationRate, but it is written unconditionally so the
	// value stays live-tunable in PIE rather than latching whatever it was at construction.
	Movement->RotationRate.Yaw = StationaryTurnRateDegrees;

	// Deliberately the exact source UTDDodgeAbility::ResolveDodgeDirection reads. If these
	// two ever disagree about what "no input" means, a dodge pressed on the first frame of
	// movement resolves against a facing that is still catching up. Reading one value means
	// they cannot disagree -- including about how stale that value is.
	FVector Input = Movement->GetLastInputVector();
	Input.Z = 0.0f;

	const bool bHasMoveInput = !Input.IsNearlyZero();

	// Snapped or smooth, never both: bUseControllerRotationYaw wins over the movement
	// component's desired-rotation path, so leaving both on would silently disable the turn.
	//
	// Note there is no partial state between these two. Attacks freeze facing through
	// IsFacingLocked() above and hand it straight back; nothing scales the rate. A version that
	// faded between them was built and removed the same day -- the fade suppressed the snap for
	// its whole duration, so chained attacks never caught up to the camera. See
	// bAbilityFacingLocked.
	bUseControllerRotationYaw = bHasMoveInput;
	Movement->bUseControllerDesiredRotation = !bHasMoveInput;
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
