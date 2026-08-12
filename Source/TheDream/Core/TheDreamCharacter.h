// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "TheDreamCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A simple player-controllable third person character
 *  Implements a controllable orbiting camera
 */
UCLASS(abstract)
class ATheDreamCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
	
protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

	/**
	 *  How fast the character turns to face the camera *while standing still*, in degrees/sec.
	 *
	 *  Deliberately its own value rather than CharacterMovement's RotationRate, which it
	 *  drives: the two were one concern while facing followed movement and are two now.
	 *  Facing snaps instantly the moment there is movement input, so this rate only ever
	 *  applies at rest -- see UpdateCameraRelativeFacing().
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ClampMin="0.0"))
	float StationaryTurnRateDegrees = 500.0f;

	/**
	 *  Set while an *ability* owns the character's facing. Runtime only, never authored.
	 *
	 *  Named for abilities rather than attacks because the dodge uses it too, and it is the
	 *  dodge that makes it load-bearing: with bAllowPhysicsRotationDuringAnimRootMotion enabled
	 *  the engine no longer stops a root-motion montage from being steered, so anything that
	 *  wants a committed direction has to say so. Previously the dodge got that for free from a
	 *  suppression it did not ask for.
	 *
	 *  A plain bool, and the fact that it is not a scale is a decision rather than an omission.
	 *  It shipped on 2026-08-12 as a float faded in and out over time, so a swing eased into and
	 *  out of its freeze, and play killed it the same day: **any scale below 1 disables the snap
	 *  branch**, so the fade did not merely soften the handoff, it left the character on smooth
	 *  turning for its whole duration. Chaining lights with the camera turned meant each swing
	 *  landed a little nearer to where you were looking and never at it. Precision beats polish
	 *  here; the smoothing is deferred to the polish audit, item 14.
	 */
	bool bAbilityFacingLocked = false;

public:

	/** Constructor */
	ATheDreamCharacter();

	/**
	 *  Takes facing away for the duration of an ability, or gives it straight back.
	 *
	 *  Instant in both directions, deliberately -- see bAbilityFacingLocked.
	 *
	 *  **Whoever takes facing away is responsible for giving it back on every exit path**,
	 *  including cancellation and death -- a stranded lock is a character who can never turn
	 *  again, and nothing about it announces itself. Both callers clear it from `EndAbility`
	 *  for exactly that reason, which is the one place every exit converges.
	 */
	void SetAbilityFacingLocked(bool bLocked) { bAbilityFacingLocked = bLocked; }

protected:

	/** Drives the camera-relative facing rule. */
	virtual void Tick(float DeltaSeconds) override;

	/**
	 *  Whether facing should stop tracking the camera this frame.
	 *
	 *  A hook rather than a check, because this class deliberately knows nothing about combat
	 *  state -- death lives on ATDCombatCharacter. Base returns false: a character with no
	 *  combat state always faces the camera.
	 *
	 *  Note that disabling *movement* does not disable facing. The two are separate systems
	 *  and a dead character was still turning in place until this existed.
	 *
	 *  Attacks route through here too, rather than through a second mechanism. This path already
	 *  clears *both* rotation flags and returns, which is precisely what a hard freeze needs, and
	 *  it was already the tested one. An override must therefore OR with `Super::` rather than
	 *  replace it, or it silently discards the attack lock.
	 */
	virtual bool IsFacingLocked() const { return bAbilityFacingLocked; }

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/**
	 *  Snaps facing to the camera while there is movement input; turns smoothly while there
	 *  is not. Runs every frame.
	 *
	 *  The character is camera-relative rather than facing its own input, which is what lets
	 *  it strafe and backpedal and what lets all eight dodge directions resolve. Snapping is
	 *  right while moving and wrong at rest, where a static idle has nothing to hide an
	 *  instant rotation pop and the library has no turn-in-place clip to cover it.
	 *
	 *  **The condition is movement input, never velocity, and that is load-bearing.** Keyed
	 *  on movement this would break the dodge exactly where dodges are pressed: standing
	 *  still mid-turn, pressing forward and dodge on one frame would resolve the dodge
	 *  against a stale facing and send it sideways. UTDDodgeAbility::ResolveDodgeDirection
	 *  already returns Bw on near-zero input and never reads facing at rest, so keying both
	 *  off the same value means facing may only lag while nothing is looking at it.
	 *
	 */
	void UpdateCameraRelativeFacing();

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

public:

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};

