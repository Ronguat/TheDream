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
 *  Third person character with the orbiting camera, and the home of camera-relative facing:
 *  the three turn rates, the facing and movement locks, and the aim frame (GetAimYawDegrees).
 *
 *  Deliberately knows nothing about combat state -- IsFacingLocked, IsIdle and
 *  GetFacingHomingYaw are hooks ATDCombatCharacter answers.
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
	 *  How fast the character turns to face the camera, in degrees/sec. One rate, whether
	 *  moving or standing still.
	 *
	 *  Deliberately its own value rather than CharacterMovement's RotationRate, which it
	 *  drives: the two were one concern while facing followed movement and are two now.
	 *
	 *  **This is an aim value, not a cosmetic one.** An attack's wedge is authored in the
	 *  actor's frame and freezes at the commit checkpoint, so whatever this rate has turned by
	 *  then *is* where the attack points. It is therefore derived rather than chosen:
	 *
	 *      rate = 180 degrees / the light's HoldUntilSeconds  ->  180 / 0.15 = 1200
	 *
	 *  180 is the largest yaw error that can exist, since the delta is normalised to +/-180.
	 *  So 1200 is the slowest rate that can *always* close the gap before the wedge freezes,
	 *  from any starting orientation, for any input where aim was settled before the press.
	 *  Below it there are flicks the character cannot finish in time: at 500 it covered only
	 *  75 degrees, and 71% of measured flick-attacks committed with the target outside their
	 *  own 60 degree wedge.
	 *
	 *  **Move this if the light's HoldUntilSeconds moves**, or the guarantee quietly lapses.
	 *  Nothing enforces the link -- the two live in different files, like MaxWalkSpeed and the
	 *  locomotion blendspace.
	 *
	 *  It does not bound error from turning *after* the press; that is by design, since windup
	 *  is meant to be steerable and the rate is what limits how far a committed swing can be
	 *  redirected. Read the debug HUD's "lock" figure to see where any given attack froze.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ClampMin="0.0"))
	float TurnRateDegrees = 1200.0f;

	/**
	 *  How fast the character turns while genuinely idle -- see IsIdle().
	 *
	 *  Exists because the derived rate above is *right* and still reads badly in one place: a
	 *  character standing in a looping idle, spun on the spot by a camera, with no turn-in-place
	 *  clip in the library to cover it. Everywhere else 1200 looks like intent; there it looks
	 *  like a prop being rotated.
	 *
	 *  **This one is free to tune by feel, and TurnRateDegrees is not.** That asymmetry is the
	 *  point of splitting them, and it is safe for a specific reason rather than by luck: the
	 *  aim guarantee is stated against the *worst possible* gap of 180 degrees, not a typical
	 *  one, so however far facing drifts while idling, the windup still closes it. What makes
	 *  that hold is that the fast rate resumes at the **press**, not at the commit checkpoint --
	 *  the whole 150 ms windup runs at TurnRateDegrees. Had the rate been fitted to observed
	 *  flick sizes instead of derived from the ceiling, this split would have broken it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ClampMin="0.0"))
	float IdleTurnRateDegrees = 300.0f;

	/**
	 *  How fast the character turns while an attack is coiling -- see SetAbilityCoiling().
	 *
	 *  The coil is the tell, and it is also the last window in which an attack can be aimed:
	 *  facing freezes at the commit checkpoint that ends it. So this rate is exactly how far a
	 *  held attack may be redirected after the defender has had time to react to it, which makes
	 *  it a **power** value rather than a cosmetic one. Lower means more committed.
	 *
	 *  **It is nonetheless free to tune by feel, and for the same reason IdleTurnRateDegrees is.**
	 *  The aim guarantee is stated against the worst possible gap of 180 degrees and is discharged
	 *  by the *first* 150 ms, which every tier runs at TurnRateDegrees before any coil begins --
	 *  so by the time this rate applies, aim is already closed and only tracking is left. It
	 *  cannot break aim at any value, including zero.
	 *
	 *  The light never sees this: it commits at the instant the coil would start, so only the
	 *  heavy and the charged have a coil at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement", meta=(ClampMin="0.0"))
	float CoilTurnRateDegrees = 300.0f;

	/** Debug only: signed yaw from facing to the camera right now. Positive means camera is right. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Movement", Transient)
	float FacingErrorDegrees = 0.0f;

	/**
	 *  Debug only: the same error, sampled at the instant an ability took facing away.
	 *
	 *  This is the number that matters, because it is the frame the attack's wedge stops
	 *  tracking the camera. A live error is only ever a hint about it.
	 *
	 *  Also written to the log on every lock, behind TD.DebugCombatTiming, because reading a
	 *  figure off a HUD in the same moment you are flicking the camera and pressing attack turned
	 *  out not to be a thing a person can do. The line carries the rate with it so a sweep is
	 *  self-describing. It does **not** record which ability took facing, so attacks and dodges
	 *  are indistinguishable in it -- add that before trying to attribute a bad reading.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Movement", Transient)
	float FacingErrorAtLockDegrees = 0.0f;

	/**
	 *  Set while an *ability* owns the character's facing. Runtime only, never authored.
	 *
	 *  Named for abilities rather than attacks because the dodge uses it too, and it is the
	 *  dodge that makes it load-bearing: with bAllowPhysicsRotationDuringAnimRootMotion enabled
	 *  the engine no longer stops a root-motion montage from being steered, so anything that
	 *  wants a committed direction has to say so. Previously the dodge got that for free from a
	 *  suppression it did not ask for.
	 *
	 *  **For an attack it runs from the commit checkpoint to the end of the ability, recovery
	 *  included** (2026-08-12). It used to end when the release window closed, on the argument
	 *  that recovery is not part of the commitment; play found the handback abrupt, and being
	 *  committed to a direction through the punish window is the same commitment the recovery
	 *  already imposes. The aim guarantee is indifferent to this -- it lives between the press and
	 *  the commit checkpoint -- and a chained attack redirects during the *next* windup, which is
	 *  where a spectator can actually read it.
	 *
	 *  A plain bool, and the fact that it is not a scale is a decision rather than an omission.
	 *  It shipped on 2026-08-12 as a float faded in and out over time, so a swing eased into and
	 *  out of its freeze, and play killed it the same day: **any scale below 1 disables the snap
	 *  branch**, so the fade did not merely soften the handoff, it left the character on smooth
	 *  turning for its whole duration. Chaining lights with the camera turned meant each swing
	 *  landed a little nearer to where you were looking and never at it. Precision beats polish
	 *  here, and the smoothing turned out not to need a scale at all. Holding the lock to
	 *  EndAbility and letting IdleTurnRateDegrees close the gap covers the case a fade would have
	 *  softened, without any value that can disable the snap.
	 */
	bool bAbilityFacingLocked = false;

	/**
	 *  Set while an attack is coiling. Runtime only, never authored.
	 *
	 *  A bool announced by the ability rather than a rate pushed by it, following
	 *  bAbilityFacingLocked: the ability knows *what phase it is in*, the character owns what
	 *  facing does about it. That keeps all three turn rates side by side where a designer
	 *  looks for them, instead of one of them living in an ability.
	 *
	 *  Cleared in the ability's EndAbility, the one place every exit converges -- a coil that
	 *  was cancelled must not leave the character permanently slow to turn.
	 */
	bool bAbilityCoiling = false;

	/**
	 *  Set while an ability owns the character's movement. Runtime only, never authored.
	 *
	 *  Suppresses movement *input* -- WASD and jump -- rather than movement itself, which is the
	 *  distinction that matters: a lunge, a dodge or a knockback still moves the character while
	 *  this is set, because those are the ability moving you rather than you moving yourself.
	 *
	 *  **It exists because "the attack already overrides movement" was only true in part.** An
	 *  attack's lunge runs its root motion source in Override mode, so input genuinely does
	 *  nothing while it plays -- but the coil and the recovery carry no lunge at all, and WASD
	 *  worked normally through both. Found in play 2026-08-12.
	 *
	 *  Named for movement rather than for attacking, deliberately, so block, parry or a future
	 *  crouch can adopt it without the flag having to be renamed or re-explained.
	 */
	bool bAbilityMovementLocked = false;

	/**
	 *  World-space movement the player is *asking* for, recorded even while an ability locks it.
	 *
	 *  `GetLastInputVector()` cannot serve this. `DoMove` returns before `AddMovementInput` while
	 *  locked, so the movement component's vector is empty exactly when something needs to know
	 *  which way you were holding -- which is how a dodge cancelling an attack resolved backward
	 *  every time between 2026-08-12 and 2026-08-16, the lock and the dodge having been written
	 *  against different assumptions about the same vector.
	 *
	 *  Local-only and deliberately unreplicated: input is one of the three things the networking
	 *  model names as knowable only on the machine that produced it.
	 */
	FVector LastRequestedMoveInput = FVector::ZeroVector;

public:

	/** Constructor */
	ATheDreamCharacter();

	/**
	 *  What the player is asking for this frame, whether or not an ability is letting them have it.
	 *
	 *  Cleared by the release edge rather than decaying, so it is only ever this frame's answer --
	 *  which is why `MoveAction` binds `Completed` as well as `Triggered`. Without that binding
	 *  this would hold the last direction walked forever and a neutral dodge would inherit it.
	 */
	FVector GetLastRequestedMoveInput() const { return LastRequestedMoveInput; }

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
	void SetAbilityFacingLocked(bool bLocked);

	/**
	 *  Tells the character an attack is coiling, so facing slows to CoilTurnRateDegrees.
	 *
	 *  Same contract as SetAbilityFacingLocked: whoever sets it clears it from `EndAbility`,
	 *  which is the one place every exit path converges. Cheap to leave set past the commit
	 *  checkpoint -- facing is locked from there and never consults a rate at all -- but it must
	 *  not survive the ability.
	 */
	void SetAbilityCoiling(bool bCoiling);

	/**
	 *  Takes movement input away for the duration of an ability, or gives it straight back.
	 *
	 *  Same contract as SetAbilityFacingLocked: whoever takes it is responsible for returning it
	 *  on **every** exit path. Both are cleared from `EndAbility`, the one place every exit
	 *  converges, and a stranded movement lock is a character who can never walk again.
	 *
	 *  Driven by `UTDGameplayAbility::bLocksMovement` rather than by individual abilities calling
	 *  it, so opting in is a checkbox and opting in cannot be done without also opting into the
	 *  clearing.
	 */
	void SetAbilityMovementLocked(bool bLocked);

	/** Whether an ability currently owns movement input. Read by DoMove and by the jump gate. */
	bool IsMovementLocked() const { return bAbilityMovementLocked; }


	/**
	 *  Yaw the player is *aiming*, which is the camera when there is one and the body otherwise.
	 *
	 *  **Target Lock is evaluated in this frame rather than the actor.s**, because the assist aids
	 *  the attacker.s input and input is the camera. Damage stays in the actor frame, because a
	 *  defender has to be able to trust what the body is doing. They coincide whenever facing has
	 *  caught up, which is most of the time -- and the moment homing makes them diverge is exactly
	 *  the moment the distinction starts mattering.
	 */
	float GetAimYawDegrees() const;

	/** Debug only: live yaw error between facing and the camera, in degrees. */
	float GetFacingErrorDegrees() const { return FacingErrorDegrees; }

	/** Debug only: yaw error sampled when facing was last taken by an ability, in degrees. */
	float GetFacingErrorAtLockDegrees() const { return FacingErrorAtLockDegrees; }

	/**
	 *  Re-applies the camera-probe exemption to the capsule and the mesh.
	 *
	 *  **Every SetCollisionProfileName call on either component silently undoes it**, because a
	 *  profile replaces the whole response table rather than merging into it. That is invisible at
	 *  the call site: ragdolling sets the mesh to `Ragdoll` and the revive sets it back to
	 *  `CharacterMesh`, and both read as restoring a known-good state while actually dropping a
	 *  per-channel override the constructor set. The symptom is a corpse the spring arm collides
	 *  with -- and because the revive does not restore it either, a revived character whose mesh
	 *  blocks the camera permanently from then on.
	 *
	 *  So the exemption lives here rather than inline in the constructor, and anything touching a
	 *  collision profile on these two components calls it afterwards.
	 */
	void ApplyCameraCollisionExemption();

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

	/**
	 *  Whether the character is doing *nothing at all*, which selects IdleTurnRateDegrees.
	 *
	 *  **Idle means zero button presses of any kind**, not merely "not moving" and not merely
	 *  "not attacking". Stated that way deliberately: a list of exceptions would need extending
	 *  by every slice that adds an action, and would be wrong in between. Block and parry will
	 *  cost nothing here.
	 *
	 *  A hook rather than a check, for the same reason IsFacingLocked() is one -- this class
	 *  knows nothing about combat. The base answers only what it can see: no movement input and
	 *  feet on the ground. Falling counts as activity because you either jumped, which is a
	 *  press, or walked off something, which was one a moment ago.
	 *
	 *  An override must AND with `Super::`, never replace it, or it reports a sprinting
	 *  character as idle.
	 */
	virtual bool IsIdle() const;

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/**
	 *  Turns facing toward the camera at TurnRateDegrees. Runs every frame.
	 *
	 *  The character is camera-relative rather than facing its own input, which is what lets
	 *  it strafe and backpedal and what lets all eight dodge directions resolve.
	 *
	 *  **One rate in both states, since 2026-08-12.** Facing used to *snap* whenever there was
	 *  movement input and turn smoothly only at rest, on the reasoning that a static idle has
	 *  nothing to hide a rotation pop while a moving character does. Two things retired that:
	 *  the snap read as a bug rather than a feature in play, and the snap's supposed safety
	 *  argument -- that smooth turning would send dodges sideways -- turned out to be false.
	 *  UTDDodgeAbility::ResolveDodgeDirection resolves the direction *relative to facing* and
	 *  the montage then travels relative to that same facing, so lag cancels out of the result
	 *  entirely; only the 45 degree quantisation survives. Confirmed in play.
	 *
	 *  What the snap really bought was an aim error of identically zero, because facing was
	 *  assigned to the camera every frame. Giving it up costs a measured 5.7 degree mean
	 *  against a 60 degree wedge, with 86% of attacks still exact. See TurnRateDegrees.
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

