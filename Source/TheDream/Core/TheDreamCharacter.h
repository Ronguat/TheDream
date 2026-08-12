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
	 *  How much of StationaryTurnRateDegrees the character currently gets. 1 is free, 0 is frozen.
	 *
	 *  Runtime only, driven by SetFacingAuthority. It exists so an attack can take facing away
	 *  *gradually* rather than at a stroke -- an instantaneous freeze mid-swing reads as a hitch,
	 *  and there is no turn-in-place content to hide one.
	 */
	float FacingTurnScale = 1.0f;

	float FacingTurnScaleTarget = 1.0f;

	/** Units of scale per second. Non-positive snaps, which is what a zero-length fade means. */
	float FacingTurnScaleRate = 0.0f;

public:

	/** Constructor */
	ATheDreamCharacter();

	/**
	 *  Sets how freely the character may turn, optionally easing there over OverSeconds.
	 *
	 *  0 with a duration is how an attack commits: the swing's direction stops being negotiable
	 *  shortly before its hitbox appears, rather than the instant it does. 1 gives facing back.
	 *  Passing 0 seconds applies immediately.
	 *
	 *  **Whoever takes facing away is responsible for giving it back on every exit path**,
	 *  including cancellation and death -- a stranded lock is a character who can never turn
	 *  again, and nothing about it announces itself. UTDMeleeAttackAbility restores from
	 *  EndAbility for exactly that reason.
	 */
	void SetFacingAuthority(float Target, float OverSeconds);

	/**
	 *  Gives facing back only if something has taken it away.
	 *
	 *  The difference from SetFacingAuthority(1, x) is that this leaves an *in-flight* restore
	 *  alone. An attack's unlock is deliberately spread across its recovery, and the ability ends
	 *  at the montage's blend-out -- part-way through that fade. Calling the plain setter there
	 *  would re-time a fade that is already going the right way and snap the tail of it.
	 */
	void EnsureFacingRestored(float OverSeconds);

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
	 */
	virtual bool IsFacingLocked() const { return false; }

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
	 *  **Below full authority the snap branch is forced off**, because it does not read
	 *  RotationRate at all and therefore cannot be faded -- left on, it would jump the character
	 *  to the camera on any frame with movement input, however far into a lock the attack was.
	 */
	void UpdateCameraRelativeFacing();

	/** Eases FacingTurnScale toward its target. Called from Tick, deliberately not from a timer. */
	void AdvanceFacingFade(float DeltaSeconds);

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

