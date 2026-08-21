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
 *  Third person character with the orbiting camera, and the home of camera-relative facing: the
 *  three turn rates, the facing and movement locks, and the aim frame (GetAimYawDegrees).
 *
 *  Knows nothing about combat state -- IsFacingLocked, IsIdle and GetFacingHomingYaw are hooks
 *  ATDCombatCharacter answers.
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
	 *  How fast the character turns to face the camera, in degrees/sec. One rate, moving or standing
	 *  still. Its own value rather than CharacterMovement's RotationRate, which it drives.
	 *
	 *  **An aim value, not a cosmetic one.** An attack's wedge is authored in the actor's frame and
	 *  freezes at the commit checkpoint, so whatever this rate has turned by then *is* where the
	 *  attack points. Derived, not chosen:
	 *
	 *      rate = 180 degrees / the light's HoldUntilSeconds  ->  180 / 0.15 = 1200
	 *
	 *  180 is the largest yaw error that can exist, so 1200 is the slowest rate that can *always*
	 *  close the gap before the wedge freezes, from any orientation, whenever aim was settled before
	 *  the press. **Move this if the light's HoldUntilSeconds moves** or the guarantee lapses --
	 *  nothing enforces the link, and the two live in different files.
	 *
	 *  It does not bound error from turning *after* the press: windup is meant to be steerable, and
	 *  this rate is what limits how far a committed swing can be redirected.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ClampMin="0.0"))
	float TurnRateDegrees = 1200.0f;

	/**
	 *  How fast the character turns while genuinely idle -- see IsIdle().
	 *
	 *  **Free to tune by feel, where TurnRateDegrees is not**, which is the point of splitting them.
	 *  Safe because the aim guarantee is stated against the *worst possible* 180-degree gap rather
	 *  than a typical one, and because the fast rate resumes at the **press**, not at the commit
	 *  checkpoint, so the whole 150 ms windup runs at TurnRateDegrees.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ClampMin="0.0"))
	float IdleTurnRateDegrees = 300.0f;

	/**
	 *  How fast the character turns while an attack is coiling -- see SetAbilityCoiling().
	 *
	 *  The coil is the tell and the last window in which an attack can be aimed, facing freezing at
	 *  the commit checkpoint that ends it. So this is how far a held attack may be redirected after
	 *  the defender has had time to react: a **power** value, lower being more committed.
	 *
	 *  **Free to tune by feel nonetheless**, as IdleTurnRateDegrees is: aim is already closed by the
	 *  first 150 ms, which every tier runs at TurnRateDegrees before any coil begins, so this cannot
	 *  break aim at any value including zero. The light never sees it -- it commits at the instant
	 *  the coil would start.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ClampMin="0.0"))
	float CoilTurnRateDegrees = 300.0f;

	/** Debug only: signed yaw from facing to the camera right now. Positive means camera is right. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Movement", Transient)
	float FacingErrorDegrees = 0.0f;

	/**
	 *  Debug only: the same error, sampled at the instant an ability took facing away -- the frame
	 *  the attack's wedge stops tracking the camera. Also written to the log on every lock, behind
	 *  TD.DebugCombatTiming, carrying the rate with it so a sweep is self-describing.
	 *
	 *  It does **not** record which ability took facing, so attacks and dodges are indistinguishable
	 *  in it -- add that before trying to attribute a bad reading.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Movement", Transient)
	float FacingErrorAtLockDegrees = 0.0f;

	/**
	 *  Where the camera pointed when the attack was *pressed*, and when that was.
	 *  FacingErrorAtLockDegrees answers an **angle** question -- is the body aligned with the camera
	 *  at commit -- and reads 0.0 even when the swing goes somewhere the player never aimed. What it
	 *  cannot see is that commit can happen up to ~440 ms after the press, camera moving throughout.
	 *  These two make that a *time* question the trace can answer.
	 */
	float AimPressControlYawDegrees = 0.0f;
	float AimPressWorldTime = -1.0f;

	/**
	 *  Set while an *ability* owns the character's facing. Runtime only, never authored. Named for
	 *  abilities rather than attacks because the dodge uses it too: with
	 *  bAllowPhysicsRotationDuringAnimRootMotion enabled the engine no longer stops a root-motion
	 *  montage from being steered, so anything wanting a committed direction has to say so.
	 *
	 *  **For an attack it runs from the commit checkpoint to the end of the ability, recovery
	 *  included.** The aim guarantee is indifferent, living between the press and the checkpoint,
	 *  and a chained attack redirects during the *next* windup.
	 *
	 *  A bool rather than a scale, because **any scale below 1 disables the snap branch**: a fade
	 *  would leave the character on smooth turning for its whole duration rather than softening the
	 *  handoff. Holding the lock to EndAbility covers the same case with nothing that can disable
	 *  the snap.
	 */
	bool bAbilityFacingLocked = false;

	/**
	 *  Set while an attack is coiling. Runtime only, never authored.
	 *
	 *  A bool announced by the ability rather than a rate pushed by it, following
	 *  bAbilityFacingLocked: the ability knows what phase it is in, the character owns what facing
	 *  does about it, and all three turn rates stay side by side where a designer looks for them.
	 *
	 *  Cleared in the ability's EndAbility, where every exit converges -- a cancelled coil must not
	 *  leave the character permanently slow to turn.
	 */
	bool bAbilityCoiling = false;

	/**
	 *  Set while an ability owns the character's movement. Runtime only, never authored.
	 *
	 *  Suppresses movement *input* -- WASD and jump -- rather than movement itself: a lunge, a dodge
	 *  or a knockback still moves the character while this is set. Not covered by an attack's lunge
	 *  running its root motion source in Override mode, which stops input only while it plays: the
	 *  coil and the recovery carry no lunge at all.
	 *
	 *  Named for movement rather than for attacking so block, parry or a future crouch can adopt it
	 *  without a rename.
	 */
	bool bAbilityMovementLocked = false;

	/**
	 *  World-space movement the player is *asking* for, recorded even while an ability locks it.
	 *
	 *  GetLastInputVector() cannot serve this: DoMove returns before AddMovementInput while locked,
	 *  so the movement component's vector is empty exactly when something needs to know which way
	 *  you were holding. Local-only and deliberately unreplicated -- input is knowable only on the
	 *  machine that produced it.
	 */
	FVector LastRequestedMoveInput = FVector::ZeroVector;

public:

	/** Constructor */
	ATheDreamCharacter();

	/**
	 *  What the player is asking for this frame, whether or not an ability is letting them have it.
	 *  Cleared by the release edge rather than decaying, so it is only ever this frame's answer,
	 *  which is why MoveAction binds Completed as well as Triggered. Without that binding it would
	 *  hold the last direction walked forever and a neutral dodge would inherit it.
	 */
	FVector GetLastRequestedMoveInput() const { return LastRequestedMoveInput; }

	/**
	 *  Takes facing away for the duration of an ability, or gives it straight back. Instant in both
	 *  directions -- see bAbilityFacingLocked.
	 *
	 *  **Whoever takes facing away is responsible for giving it back on every exit path**, including
	 *  cancellation and death. A stranded lock is a character who can never turn again, and nothing
	 *  announces it. Both callers clear it from EndAbility, where every exit converges.
	 */
	void SetAbilityFacingLocked(bool bLocked);

	/**
	 *  Tells the character an attack is coiling, so facing slows to CoilTurnRateDegrees. Same
	 *  contract as SetAbilityFacingLocked: whoever sets it clears it from EndAbility. Cheap to leave
	 *  set past the commit checkpoint, facing being locked from there and never consulting a rate,
	 *  but it must not survive the ability.
	 */
	void SetAbilityCoiling(bool bCoiling);

	/**
	 *  Takes movement input away for the duration of an ability, or gives it straight back. Same
	 *  contract as SetAbilityFacingLocked, and a stranded movement lock is a character who can never
	 *  walk again. Driven by UTDGameplayAbility::bLocksMovement rather than by individual abilities,
	 *  so opting in cannot be done without also opting into the clearing.
	 */
	void SetAbilityMovementLocked(bool bLocked);

	/**
	 *  Whether movement input is currently taken away. Read by DoMove and by GA_Jump's refusal.
	 *  **Virtual because an ability owning movement is only one of the reasons**: ATDCombatCharacter
	 *  adds the externally-inflicted ones -- hitstun and a broken guard -- which are states rather
	 *  than abilities and so have nothing to call SetAbilityMovementLocked.
	 */
	virtual bool IsMovementLocked() const { return bAbilityMovementLocked; }


	/**
	 *  Yaw the player is *aiming*: the camera when there is one, the body otherwise.
	 *  **Target Lock is evaluated in this frame rather than the actor's**, because the assist aids
	 *  the attacker's input and input is the camera. Damage stays in the actor frame, a defender
	 *  having to trust what the body is doing. They coincide whenever facing has caught up, and the
	 *  moment homing makes them diverge is the moment the distinction matters.
	 */
	float GetAimYawDegrees() const;

	/** Debug only: live yaw error between facing and the camera, in degrees. */
	float GetFacingErrorDegrees() const { return FacingErrorDegrees; }

	/** Debug only: yaw error sampled when facing was last taken by an ability, in degrees. */
	float GetFacingErrorAtLockDegrees() const { return FacingErrorAtLockDegrees; }

	/** Records where the camera pointed at an attack press, for the commit-time correlation. */
	void NoteAimPress();

	/**
	 *  Re-applies the camera-probe exemption to the capsule and the mesh.
	 *
	 *  **Every SetCollisionProfileName call on either component silently undoes it**, a profile
	 *  replacing the whole response table rather than merging into it, invisibly at the call site.
	 *  The symptom is a corpse the spring arm collides with -- and since the revive does not restore
	 *  it either, a revived character whose mesh blocks the camera from then on.
	 *
	 *  Anything touching a collision profile on these two components calls this afterwards.
	 */
	void ApplyCameraCollisionExemption();

protected:

	/** Drives the camera-relative facing rule. */
	virtual void Tick(float DeltaSeconds) override;

	/**
	 *  Whether facing should stop tracking the camera this frame. A hook rather than a check, this
	 *  class deliberately knowing nothing about combat state; the base returns false, a character
	 *  with no combat state always facing the camera. Disabling *movement* does not disable facing.
	 *
	 *  Attacks route through here too. This path clears *both* rotation flags and returns, which is
	 *  what a hard freeze needs, so **an override must OR with Super:: rather than replace it**, or
	 *  it silently discards the attack lock.
	 */
	virtual bool IsFacingLocked() const { return bAbilityFacingLocked; }

	/**
	 *  Whether the character is doing *nothing at all*, which selects IdleTurnRateDegrees.
	 *
	 *  **Idle means zero button presses of any kind**, not merely "not moving" and not merely "not
	 *  attacking". Stated that way deliberately: a list of exceptions would need extending by every
	 *  slice that adds an action, and would be wrong in between.
	 *
	 *  A hook rather than a check, as IsFacingLocked() is. The base answers only what it can see: no
	 *  movement input, feet on the ground; falling counts as activity. **An override must AND with
	 *  Super::**, never replace it, or it reports a sprinting character as idle.
	 */
	virtual bool IsIdle() const;

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/**
	 *  Turns facing toward the camera at TurnRateDegrees. Runs every frame.
	 *
	 *  The character is camera-relative rather than facing its own input, which is what lets it
	 *  strafe and backpedal and what lets all eight dodge directions resolve. One rate in both
	 *  states: facing does not snap when there is movement input. Direction resolves *relative to
	 *  facing* in UTDDodgeAbility::ResolveDodgeDirection and the montage travels relative to that
	 *  same facing, so turn lag cancels out and only the 45 degree quantisation survives. See
	 *  TurnRateDegrees for what smooth turning costs in aim.
	 */
	void UpdateCameraRelativeFacing(float DeltaSeconds);

	/** Yaw Target Lock wants the body turned to, if it is homing. False leaves facing to the camera. */
	virtual bool GetFacingHomingYaw(float& OutYaw) const { return false; }

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
