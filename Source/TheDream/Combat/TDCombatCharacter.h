// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/TheDreamCharacter.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpec.h"
#include "Engine/TimerHandle.h"
#include "Combat/TDAttackHitbox.h"
#include "Combat/TDKnockdownTypes.h"
#include "TDCombatCharacter.generated.h"

class UAbilitySystemComponent;
class UCurveFloat;
class UGameplayAbility;
class UGameplayEffect;
class UInputAction;
class UStaticMeshComponent;
class UTDAttributeSet;
struct FAbilityEndedData;

/**
 *  A press that nothing was able to answer, kept for a moment in case something can.
 *
 *  Records **both edges**, because the attack ladder's identity is a consequence of how long the
 *  button was held. A press replayed without knowing whether the button had since come up would
 *  leave the attack believing it was still held, sail past every checkpoint, and turn a tap into a
 *  charged heavy.
 *
 *  The release is recorded as a **duration**, and replayed at that same offset from activation.
 *  Storing it as a yes/no and releasing at once is not sufficient: the buffer survives indefinitely
 *  while the button is held, so the recorded hold is bounded by how long the refusal lasted rather
 *  than by the buffer window, and any hold length is reachable.
 */
struct FTDBufferedInput
{
	/** Which input was pressed. Invalid means the buffer is empty. */
	FGameplayTag InputTag;

	/** World time the button went down, for the trace's "how late did this fire". */
	float PressWorldTime = 0.0f;

	/** World time to give up at. Held buttons keep pushing this forward; see TickInputBuffer. */
	float ExpiryWorldTime = 0.0f;

	/**
	 *  How long the button was held, valid once bReleased. Replayed at this offset from activation,
	 *  which is what preserves the tier the player actually asked for.
	 */
	float HoldSeconds = 0.0f;

	/** True once the button came up, whether or not the buffer has fired. */
	bool bReleased = false;

	/**
	 *  Signed degrees from facing the player was holding when the button went down -- 0 forward,
	 *  +90 right, 180 back. Valid only while bHadMoveInput.
	 *
	 *  **A directional dodge is one composite input, not two.** The direction buffers with the
	 *  press rather than being looked up when the press surfaces, so releasing the key inside the
	 *  buffer window still gives the dodge that was asked for. Resolved at press rather than stored
	 *  raw because resolution needs the *facing* of that moment too, and the camera can turn while a
	 *  buffered input waits.
	 *
	 *  **The dodge is the only input that latches its direction this way.** An attack's direction is
	 *  read at *commit* instead, so a buffered attack goes where the camera is when it fires. Which
	 *  policy an attack should use is the buffered-aim trap's open question.
	 */
	float MoveAngleDegrees = 0.0f;

	/** False when the press was made standing still, which is what a neutral dodge reads. */
	bool bHadMoveInput = false;

	bool IsSet() const { return InputTag.IsValid(); }
	void Clear() { *this = FTDBufferedInput(); }
};

/**
 *  Why a parry window is closing. **There are exactly three, and that is the mechanic.**
 *
 *  ***Parry is sacred***: the only way out of a committed parry is success, and nothing is ever
 *  designed to beat one while it is active. That promise is deliberately **not** made for block. So
 *  this enum is exhaustive by design rather than by current need -- a fourth reason would be a
 *  design change, not an addition, which is why closing takes a reason instead of a bool.
 *
 *  Cancellation is absent on purpose. An ability cancelled mid-window leaves the window running;
 *  see UTDParryAbility::EndAbility, which warns rather than closing.
 */
UENUM(BlueprintType)
enum class ETDParryCloseReason : uint8
{
	/** Ran its authored length without catching anything. The one path that bills the whiff. */
	Expired,

	/** Negated a hit. Bills nothing and starts the Grace tail. */
	Caught,

	/**
	 *  Died. **The single carve-out to "sacred", and it is on the house** -- no recovery charged.
	 *
	 *  Unreachable today: nothing can damage you through an open parry window, so a
	 *  damage-over-time effect is the only way to die inside one, and none exists. It goes live at
	 *  exactly the moment the deferred ranged/DoT question does.
	 */
	Death,
};

/**
 *  When a debug auto-attacker turns to face what it is swinging at.
 *
 *  A dummy that never turns is a *stable* fixture and a misleading one: every defensive verdict
 *  from Block onward is measured against what it throws, and an attack that cannot follow a
 *  sidestepping player reads as more forgiving than a human opponent would. A dummy that always
 *  faces you is unpleasant to work around when measuring something else, so this is three modes
 *  rather than a bool.
 *
 *  **Facing is all this does.** The dummy does not approach: the parity rule is "accurate in the
 *  dimension being measured", and Block measures what happens when an attack arrives rather than
 *  how it closed the distance.
 */
UENUM(BlueprintType)
enum class ETDDebugFacingMode : uint8
{
	/** Never turn. Kept because it is a useful control. */
	Never,

	/**
	 *  Turn only while an attack is running, and let the position reset restore the placed yaw
	 *  between swings. The training dummy's default: it aims what it throws without following you
	 *  around the level when you are measuring something unrelated.
	 */
	WhileAttacking,

	/** Turn continuously. Closest to a real opponent, and the most intrusive to work around. */
	Always
};

/**
 *  What a debug auto-defender does with the attacks coming at it.
 *
 *  **Deliberately one behaviour at a time rather than a defensive AI.** A dummy choosing between
 *  blocking and dodging would need a policy, and a policy is a thing to debug on top of the thing
 *  being debugged. One mode does one thing forever, which is what keeps the log readable.
 */
UENUM(BlueprintType)
enum class ETDDebugDefendMode : uint8
{
	/** Defend nothing. The training dummy's default: a pure damage target. */
	Off,

	/**
	 *  Raise the guard once and never let go.
	 *
	 *  One press is the whole implementation. GA_Block opts into bResumeWhileInputHeld, and the
	 *  press path marks the spec InputPressed whether or not the activation succeeds -- so the
	 *  resume tick puts the guard back after every break, exhaustion and airborne cancel.
	 */
	HoldBlock,

	/**
	 *  Tap the dodge input on DebugDodgeIntervalSeconds.
	 *
	 *  With no movement input a dodge always resolves backward, so this both spends stamina on a
	 *  fixed schedule and travels a known distance in a known direction.
	 */
	PeriodicDodge,

	/**
	 *  Tap the parry input on DebugParryIntervalSeconds.
	 *
	 *  **The phase sweep is the whole design.** A parry is a 300 ms window against attacks arriving
	 *  on their own schedule, so a period co-prime with the attacker's walks the window across
	 *  windup, release and recovery -- yielding successes *and* whiffs from one unattended run. A
	 *  period that divided the attacker's would answer one question forever while looking like a
	 *  working fixture. See DebugParryIntervalSeconds.
	 */
	PeriodicParry
};

/**
 *  Base class for anything that can fight: the player and the training dummy alike.
 *
 *  Inherits locomotion and the third person camera from ATheDreamCharacter and adds the Ability
 *  System Component plus the core combat attributes on top. Abilities are granted from
 *  DefaultAbilities, so a Blueprint subclass decides what it can do without any graph wiring.
 *  Leaving that list empty produces a valid damage target that cannot act.
 */
UCLASS(abstract)
class ATDCombatCharacter : public ATheDreamCharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:

	ATDCombatCharacter();

	//~ Begin IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetHealth() const;

	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetStamina() const;

	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetMaxStamina() const;

	/** Health as a 0-1 fraction, for health bars and debug readouts. */
	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetHealthPercent() const;

	/** Stamina as a 0-1 fraction, for stamina bars and debug readouts. */
	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetStaminaPercent() const;

	/** True while stamina regen is suppressed, whether by an action or the tail after one. */
	UFUNCTION(BlueprintPure, Category="Combat|Stamina")
	bool IsStaminaRegenPaused() const;

	UFUNCTION(BlueprintPure, Category="Combat|Stamina")
	bool IsExhausted() const { return bExhausted; }

	/** True from health reaching 0 until revived. Every ability is refused throughout. */
	UFUNCTION(BlueprintPure, Category="Combat|Health")
	bool IsDead() const { return bDead; }

	/** Fixture-only: this character starts no lunges at all. See bDebugSuppressLunge. */
	UFUNCTION(BlueprintPure, Category="Combat|Debug")
	bool IsDebugLungeSuppressed() const { return bDebugSuppressLunge; }

	/** True while BlockingTag is present, i.e. a guard is up. */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	bool IsBlocking() const;

	/** True from a guard breaking until its stun expires. Every ability is refused throughout. */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	bool IsGuardBroken() const { return bGuardBroken; }

	/**
	 *  True while a *successful* block's lockout is running. Refuses offense only.
	 *
	 *  Not the same state as IsGuardBroken(), which is the penalty for a guard that failed and
	 *  refuses everything. A block that worked costs the defender initiative, not their guard.
	 */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	bool IsInBlockstun() const { return bInBlockstun; }

	/**
	 *  The heading the player was holding when the press that activated this ability landed, in
	 *  signed degrees from facing. Returns false when that press was neutral.
	 *
	 *  **Read this rather than the movement component.** GetLastInputVector() is empty for the whole
	 *  of any ability that locks movement, so an ability asking it which way the player is holding
	 *  always hears "nowhere".
	 */
	bool GetPressMoveDirection(float& OutAngleDegrees) const
	{
		OutAngleDegrees = PressMoveAngleDegrees;
		return bPressHadMoveInput;
	}

	/**
	 *  Starts the blockstun lockout, or extends one already running to the later end time.
	 *
	 *  Called by the attacking ability when its hit is blocked, so the duration travels with the
	 *  attack rather than being a property of the defender -- a heavy should pin a guard for longer
	 *  than a light, and only the attack knows which it was.
	 */
	void EnterBlockstun(float DurationSeconds);

	/** True while cleanly-hit stun is running. Refuses everything -- see State_Hitstun. */
	UFUNCTION(BlueprintPure, Category="Combat|Stun")
	bool IsInHitstun() const { return bInHitstun; }

	/**
	 *  Starts hitstun, or extends one already running to the later end time -- blockstun's shape,
	 *  with one addition: **entry cancels everything the victim was doing, committed or not.** A hit
	 *  through your swing beats your swing; commitment governs what you may cancel voluntarily, not
	 *  what being hit does to you. It also resets the victim's own string. Server decides; the state
	 *  pair applies the tag everywhere. 0 no-ops.
	 */
	void EnterHitstun(float DurationSeconds);

	/** Whether this body is on the floor -- jail, choice window or rise. */
	bool IsKnockedDown() const { return bKnockedDown; }

	/** Which grade put it there. None whenever IsKnockedDown() is false. */
	ETDKnockdownGrade GetKnockdownGrade() const { return KnockdownGrade; }

	/**
	 *  Whether hits should pass straight through this body.
	 *
	 *  True from the moment of the knockdown until **any** rise begins, auto or chosen. Each get-up
	 *  option then prices its own rise: the dodge brings i-frames, block brings a guard, the get-up
	 *  attack is naked but threatening, and doing nothing is plainly hittable.
	 *
	 *  **The rise-begin frame resolves to the defender.** Ties at a protective boundary go to the
	 *  protected, so the one place engine tick order could coin-flip an outcome is ruled instead.
	 */
	bool IsKnockdownInvulnerable() const { return bKnockedDown && !bKnockdownRising; }

	/** Whether get-up options are currently legal -- past the jail, before any rise. */
	bool IsInKnockdownChoiceWindow() const;

	/**
	 *  Put this character on the floor. **Supersedes hitstun rather than joining it.**
	 *
	 *  Replaces EnterHitstun for any hit whose swing authored a grade: cancels through the same
	 *  funnel death uses, resets the victim's string, overrides a whiffed parry's recovery, starts
	 *  the radial carry and begins forced facing. Server-only.
	 *
	 *  @param Grade     Which split to run. None returns without doing anything.
	 *  @param Attacker  Who did it -- the carry's radial origin and the facing target.
	 */
	void EnterKnockdown(ETDKnockdownGrade Grade, AActor* Attacker);

	/** Whether a parried attacker is serving their lockout. */
	bool IsInParryLockout() const { return bInParryLockout; }

	/**
	 *  Lock this attacker out for the duration the swing a parrier just caught authored.
	 *
	 *  **Externally inflicted, so a lockout rather than a recovery**, and it composes by the
	 *  standing schema: a lockout overrides a recovery. Takes the full movement lock, refuses every
	 *  ability from the shared base, and ends the string explicitly.
	 *
	 *  @param LockoutSeconds  Authored on the swing that was caught -- see
	 *                         UTDMeleeAttackAbility::ParryLockoutSeconds. Zero is honoured, not
	 *                         floored.
	 */
	void EnterParryLockout(float LockoutSeconds);

	/**
	 *  Start the rise. **Ends invincibility on the frame it is called**, and commits: from here
	 *  there are no options, no movement and no way back to the floor.
	 *
	 *  Called by the auto-rise at the choice window's close and by every get-up option, which is
	 *  what makes "the action is the exit" true -- there is no shared pre-rise to wait through.
	 *
	 *  @param By  Trace label: auto, dodge, block, attack, kipup or stand.
	 */
	void BeginKnockdownRise(const TCHAR* By, bool bPlayRiseMontage = true);

	/**
	 *  Turn this body toward an attacker at the derived rate. Applies to every clean hit, not only
	 *  knockdowns. **The camera never moves** -- this is the body, and taking someone's aim away is
	 *  a different and much larger thing than turning their model.
	 */
	void BeginForcedFacing(AActor* Toward);

	/**
	 *  True while a parry window is open and has not yet caught anything.
	 *
	 *  **Asked by the *attacker's* hit path**, which is why this is mechanical state on the
	 *  character rather than anything owned by GA_Parry. A defender cannot refuse a hit it never
	 *  sees, exactly as with i-frames and the guard, so the negation is resolved where the hit is
	 *  detected -- and that code has a character pointer, not an ability one.
	 */
	UFUNCTION(BlueprintPure, Category="Combat|Parry")
	bool IsParryWindowOpen() const { return bParryWindowOpen; }

	/**
	 *  Opens the negation window for DurationSeconds. Called by GA_Parry on activation.
	 *
	 *  The whiff recovery travels in with it rather than being read back later, so the price of a
	 *  window is fixed the moment it opens.
	 */
	void OpenParryWindow(float DurationSeconds, float WhiffRecoverySeconds);

	/**
	 *  Closes the window, charging the whiff recovery unless it caught something. Idempotent.
	 *
	 *  **One exit for every way a window can end** -- expiry in Tick, the ability being cancelled,
	 *  death -- so the recovery cannot be skipped by ending the parry through an unusual path.
	 */
	void CloseParryWindow(ETDParryCloseReason Reason);

	/**
	 *  True while a successful parry's Grace tail is still protecting this character.
	 *
	 *  **Deliberately carries no gameplay tag**, unlike every other state here. Tags exist to refuse
	 *  things, and Grace refuses nothing -- it is read by the *attacker's* hit path and by nothing
	 *  else. A tag would invite something to start blocking on it.
	 */
	UFUNCTION(BlueprintPure, Category="Combat|Parry")
	bool IsInParryGrace() const { return bInParryGrace; }

	/**
	 *  The montage rate the parry clip's recovery segment should switch to, or -1 if unset.
	 *
	 *  Parked here rather than on GA_Parry because **the ability is gone by the time it is needed on
	 *  a successful parry** -- a catch ends it at once, and the authored recovery rate must be used
	 *  regardless. Computed once at activation; see UTDParryAbility::PlayParryMontage.
	 */
	float GetPendingParryMontageRecoveryRate() const { return PendingParryMontageRecoveryRate; }
	void SetPendingParryMontageRecoveryRate(float Rate) { PendingParryMontageRecoveryRate = Rate; }

	/**
	 *  A parry landed: pay the reward and close the window free of charge.
	 *
	 *  Called from the attacker's hit path on the server, the only place that knows a hit was
	 *  resolved at all. Clears the regen pause outright rather than letting it tail off: a *whiff*
	 *  pays the pause, a *success* discharges it.
	 */
	void NotifyParrySuccess(AActor* Attacker);

	/**
	 *  Refuses **every** ability for DurationSeconds after a parry whiffed, or extends a running
	 *  recovery to the later end time.
	 *
	 *  Max-extended rather than reassigned, following blockstun: two overlapping causes must never
	 *  produce a shorter total than either alone.
	 */
	void ApplyParryRecovery(float DurationSeconds);

	/** True while every ability is refused by a whiffed parry. See State_ParryRecovery. */
	UFUNCTION(BlueprintPure, Category="Combat|Parry")
	bool IsInParryRecovery() const { return bInParryRecovery; }

	/**
	 *  Refuses **parry only** for DurationSeconds after a dodge ended, or extends a running gap.
	 *
	 *  Separate from ApplyParryRecovery: a whiffed parry refuses everything and commits the
	 *  character, while this takes nothing but the parry.
	 */
	void ApplyDodgeRecovery(float DurationSeconds);

	/** True while a parry is refused by a just-ended dodge. See State_DodgeRecovery. */
	UFUNCTION(BlueprintPure, Category="Combat|Dodge")
	bool IsInDodgeRecovery() const { return bInDodgeRecovery; }

	/**
	 *  Ends whatever GA_Parry instance is running. Shared by the catch path, which ends it at once,
	 *  and by the whiff's recovery expiry, which ends it later.
	 */
	void CancelParryAbility();

	/**
	 *  Hands movement back DelaySeconds from now, part-way through an attack that connected.
	 *
	 *  The on-hit waiver's movement half; the defensive half is instant and happens in the ability,
	 *  by dropping the commitment tag.
	 *
	 *  **A release, never a lock** -- it can only return control early, so a waiver firing against
	 *  an ability that has already ended is harmless rather than a way to strand movement.
	 */
	void BeginOnHitMovementWaiver(float DelaySeconds);

	/**
	 *  The knockback's receiving half: carry this character to a fixed world destination over a
	 *  duration, on the root-motion-source channel. The attacking ability computed the destination
	 *  and owns the never-inward clamp; this only runs the translation. A re-hit mid-slide replaces
	 *  the running translation, last hit wins. Server only; no-ops on the dead.
	 */
	void ReceiveKnockback(const FVector& DestinationWorld, float DurationSeconds, UCurveFloat* TimeMappingCurve);

	/**
	 *  The string's per-activation gate: which swing an attack activating *now* should be.
	 *
	 *  Advances the index while the link window is open and a successor exists; resets to the first
	 *  swing otherwise. Consumes the window either way -- it reopens when the new swing ends, if
	 *  that swing is itself chainable. Lives here rather than on the ability because the string
	 *  outlives any one activation.
	 */
	int32 ResolveStringSwingIndexForActivation(int32 SwingCount);

	/** Opens the link window: a fresh attack press within it continues the string. */
	void OpenStringLinkWindow(float WindowSeconds);

	/** Kills the string and its window. The reason is for the trace alone. */
	void ResetString(const TCHAR* Reason);

	/** True while a chain press should outlive its ordinary buffer window; see TickInputBuffer. */
	bool HasStringLinkWindowOpen() const;

	/**
	 *  Starts the guard's minimum-duration commitment. Called by the block ability on activation.
	 *
	 *  Pushed from the ability rather than detected here as a rising edge on IsBlocking(), because
	 *  an edge is what goes wrong when a guard is cancelled and resumed inside a frame.
	 */
	void BeginBlockCommitment();

	/**
	 *  Charges BlockInitialStaminaCost. Called by the block ability once it has actually activated.
	 *
	 *  Deliberately *not* EffectOnStart, which is how the dodge pays. That route needs a
	 *  GameplayEffect asset, and a GameplayEffect's modifier attribute cannot be reliably configured
	 *  through the toolset -- it accepts the write and leaves the FProperty null, so the effect
	 *  modifies nothing and says so nowhere.
	 */
	void PayBlockInitialCost();

	/** True while a guard is inside its minimum duration and cannot be acted out of. */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	bool IsBlockCommitted() const;

	/**
	 *  True if this attack's origin lies inside the guard's forward arc.
	 *
	 *  The spec's "180 degree forward coverage" as a single question, asked in the *defender's*
	 *  frame at the moment the hit resolves. A hit from behind is not blocked however long the
	 *  button has been held.
	 *
	 *  Measured against facing rather than the camera: the defender's body is what an attacker can
	 *  see, and defence has to be legible from the outside. Same reason the damage wedge is
	 *  actor-framed while aim assist is camera-framed.
	 */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	bool IsGuardFacing(const FVector& AttackOriginWorld) const;

	/**
	 *  Spends stamina from a blocked hit and breaks the guard if that empties the bar. Returns true
	 *  if the guard broke.
	 *
	 *  **This is the only thing that can break a guard**: drain runs you to zero and leaves you
	 *  there, damage is what punishes you for being there.
	 *
	 *  The break condition is *post-damage stamina is zero*, one rule covering both damage exceeding
	 *  what is left and damage landing on an already-empty bar. The second is why this cannot be
	 *  driven from the stamina-changed delegate: that fires only on a *change*, so a hit taken at
	 *  exactly zero would move nothing and be silently ignored.
	 *
	 *  Server only. The bar replicates, and the tag is applied on both sides by the state pair.
	 */
	void ApplyStaminaDamage(float Amount);

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 *  Drops the guard and defers to the base. **Every permission check lives in UTDJumpAbility.**
	 *
	 *  Kept as an override only for the guard drop, which is presentation rather than permission.
	 *  Reached solely through GA_Jump now, so anything asking "may I jump" belongs in that class or
	 *  in the shared ability base, never here.
	 */
	virtual void Jump() override;

	/**
	 *  The dead do not turn to face the camera -- **and neither does a character mid-swing, nor one
	 *  lying on the floor.**
	 *
	 *  ORed with Super rather than replacing it. The base holds the attack lock, so returning only
	 *  bDead here would discard it silently: attacks would keep tracking the camera through their
	 *  own release window and drag an actor-frame hitbox around with them.
	 *
	 *  Knockdown is here because *camera* rotation while down is fine and wanted -- the free camera
	 *  is the aiming instrument the block get-up's latched call reads from -- while the *body*
	 *  spinning on its back to match it is not. This gate separates exactly those two.
	 *
	 *  **Forced facing is unaffected and must be.** TickForcedFacing writes rotation directly rather
	 *  than through the movement component's desired-rotation path, so the rate-limited turn toward
	 *  the attacker still runs while this refuses camera tracking.
	 *
	 *  Released at the stand boundary with the tag, symmetric with the movement lock.
	 */
	virtual bool IsFacingLocked() const override { return bDead || bKnockedDown || Super::IsFacingLocked(); }
	/**
	 *  **Lockouts take movement, not just abilities.**
	 *
	 *  ORed with Super rather than replacing it, following IsFacingLocked directly above: the base
	 *  holds the ability-side lock, so returning only these three would let an attack's own
	 *  commitment be walked out of.
	 *
	 *  **Hitstun, the guard break and knockdown.** Deliberately not blockstun: a guard that *fails*
	 *  costs you everything for a fixed stun, a guard that *works* costs you initiative only, so
	 *  blockstun refuses offense while leaving the defender free to walk.
	 *
	 *  Knockdown covers the whole down state -- jail, choice window and rise alike. Movement returns
	 *  at the stand boundary and nowhere earlier: the choice window buys *options*, not steps, and a
	 *  rise is committed once started.
	 */
	virtual bool IsMovementLocked() const override { return bInHitstun || bGuardBroken || bKnockedDown || Super::IsMovementLocked(); }

	/**
	 *  Idle additionally means no ability running and no press waiting to be answered.
	 *
	 *  ANDed with Super rather than replacing it, so movement input and being airborne still count
	 *  as activity. Keyed on *any* active ability rather than a list of state tags, so every future
	 *  block, parry and stun is covered without this function being revisited.
	 *
	 *  The buffered press counts too: a press that was refused and stored is still a press.
	 */
	virtual bool IsIdle() const override;

	/** The actual launch, as opposed to Jump() which only records the press. Starts the regen pause. */
	virtual void OnJumped_Implementation() override;

	/** Ends the jump's regen pause, leaving JumpRegenPauseSeconds of tail behind it. */
	virtual void Landed(const FHitResult& Hit) override;
	virtual void PossessedBy(AController* NewController) override;

	/**
	 *  A client's PlayerState arrives *after* its pawn, so the ASC has to be resolved again here.
	 *
	 *  Without this a client initialises against a null PlayerState, silently falls back to the
	 *  owned ASC, and has no abilities -- while the server, which possesses before replicating,
	 *  works perfectly.
	 */
	virtual void OnRep_PlayerState() override;

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Granted on spawn. Empty means this character cannot act (training dummy). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Abilities")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	/**
	 *  Applied to self on spawn and never removed -- the always-on effects.
	 *
	 *  Stamina regen is deliberately *not* here; it is orchestrated in C++ below, because the
	 *  economy is a small state machine -- regen, a pause that outlives the action causing it, and a
	 *  timed exhaustion lockout -- and expressing that across effects needs tag components that
	 *  cannot be scripted in UE 5.8. Currently empty.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Abilities")
	TArray<TSubclassOf<UGameplayEffect>> DefaultEffects;

	/**
	 *  Which input action drives which input tag, e.g. IA_LightAttack -> InputTag.Attack.
	 *
	 *  Abilities are matched by tag rather than by an integer ID, so granting a new ability to a
	 *  button is a content change rather than a C++ enum edit and rebuild.
	 *
	 *  **Mapped actions must use a Down trigger** (or no trigger at all), never Pressed -- a Pressed
	 *  trigger completes on the frame after the press, while the button is still held, so the
	 *  release edge would be lost.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Input")
	TMap<TObjectPtr<UInputAction>, FGameplayTag> AbilityInputActions;

	/**
	 *  How long a press nothing could answer keeps trying, once the button is back up.
	 *
	 *  **It is a window on taps, not on intent.** A button still held is not a stale input, so the
	 *  buffer does not expire while it is down; this measures from the release. That is what lets a
	 *  heavy be buffered at all -- its boundary is beyond any window this size, so a tier above
	 *  light can only come from a hold that outlives the window.
	 *
	 *  Zero disables buffering entirely, which is the honest way to A/B whether it is helping.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Input", meta=(ClampMin="0.0"))
	float InputBufferSeconds = 0.1f;

	/**
	 *  Stamina regained per second while *not* exhausted, and while regen is running at all.
	 *
	 *  Driven here rather than by a periodic GameplayEffect because the economy is a small state
	 *  machine and expressing it across effects means tag components that cannot be scripted.
	 *  Attributes and tags are still GAS; only the orchestration is here.
	 *
	 *  This is also how fast a *dodge* becomes affordable again, so it sets the cadence of repeated
	 *  evasion as directly as DodgeSeconds does.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.0"))
	float StaminaRegenPerSecond = 40.0f;

	/**
	 *  Stamina regained per second *while exhausted*. Separate from StaminaRegenPerSecond so that
	 *  being run out of stamina costs more than the bar it emptied.
	 *
	 *  A *rate*, not a duration: exhaustion ends when stamina reaches Max and at no other moment, so
	 *  the bar remains the single source of truth for how long it lasts.
	 *
	 *  **Zero is forbidden.** Regen is the only thing that can end exhaustion, so a rate of zero is
	 *  not "no recovery" -- it is a character exhausted permanently, with every defensive action
	 *  locked out for the rest of the match. The clamp is load-bearing. See TickStaminaRegen.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.01"))
	float ExhaustedStaminaRegenPerSecond = 25.0f;

	/**
	 *  How long regen stays suppressed *after* the last action carrying StaminaRegenPausedTag ends.
	 *
	 *  Measured from the end, not the start, so it is a genuine tax on acting rather than something
	 *  a long action absorbs for free. Shared by every such ability, attacks included, so an attack
	 *  is taxed for its whole windup, release and recovery, plus this.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.0"))
	float StaminaRegenPauseSeconds = 1.0f;

	/**
	 *  How long regen stays suppressed after *landing* from a jump.
	 *
	 *  Jumping costs no stamina but is not free: regen is suppressed from the jump until this long
	 *  after landing, so height and airtime are paid for in recovery rather than in bar. Separate
	 *  from StaminaRegenPauseSeconds because a jump is a weaker commitment.
	 *
	 *  Keyed to the jump *action*, never to being airborne -- walking off a ledge is not something
	 *  you did, and costs nothing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.0"))
	float JumpRegenPauseSeconds = 0.5f;

	// ---- Knockdown ------------------------------------------------------------------------
	//
	// **Both grades total 2.5 s and begin their forced rise at 2.0.** The split between jail and
	// choice is what the grade decides; the total is deliberately invariant, because everything
	// derived from it would otherwise need re-deriving per grade. Each grade's split is its own dial
	// if either misreads in play; the total is not.

	/**
	 *  Normal grade: seconds of jail, in which every action is refused and presses buffer.
	 *
	 *  Held to the minimum a knockdown can be and still read as one.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.0"))
	float KnockdownJailSecondsNormal = 1.0f;

	/**
	 *  Normal grade: seconds of choice window, in which get-up options are legal.
	 *
	 *  Doubled against the lockout deliberately -- the fast layer's knockdowns are escape-rich, and
	 *  boundary oki against them is light-only.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.0"))
	float KnockdownChoiceSecondsNormal = 1.0f;

	/**
	 *  Hard grade: seconds of jail. **The meaner half of the same total.**
	 *
	 *  1.5 against normal's 1.0 holds every exit back far enough that a committed follow-up's
	 *  arrival window overlaps the forced rise. That overlap is the whole of hard oki.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.0"))
	float KnockdownJailSecondsHard = 1.5f;

	/** Hard grade: seconds of choice window. Narrow by design; see KnockdownJailSecondsHard. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.0"))
	float KnockdownChoiceSecondsHard = 0.5f;

	/**
	 *  Seconds a rise takes, shared by both grades and by every way of starting one.
	 *
	 *  **Shared deliberately**: the clips are rate-fitted to this, and keeping it grade-invariant is
	 *  what keeps both grades standing at 2.5. A rise is fully committed once started, so this is
	 *  also the width of the meaty window a chosen stand is choosing *when* to open.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.01"))
	float KnockdownRiseSeconds = 0.5f;

	/**
	 *  How far from the attacker a knocked-down body is carried, along the attacker->victim bearing.
	 *
	 *  **Radial, not along the attacker's facing, and the two axes are never to be unified.** The
	 *  string's forward knockback centres on the facing axis because the next hit needs its target
	 *  in front; a knockdown radiates, so a side target flies to its own side and a crowd scatters.
	 *
	 *  Farther than the knockback by design -- a full light's coverage of separation, so re-engaging
	 *  the riser costs real travel. **Coupled to MaxReachCm**: pushing it below a branch's reach
	 *  would let a standing attacker touch the floor.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.0"))
	float KnockdownSpacingCm = 450.0f;

	/** Seconds the carry takes. Sits inside the fall; knockback's contract, same root motion source. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.01"))
	float KnockdownCarrySeconds = 0.35f;

	/** Optional shape for the carry, exactly as the knockback's curve. **Must average 1.0.** */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown")
	TObjectPtr<UCurveFloat> KnockdownCarryTimeMappingCurve;

	/**
	 *  Degrees per second the body turns to face its attacker after **any** clean hit.
	 *
	 *  **Derived, not free.** 180 degrees must complete well inside the shortest hitstun anyone can
	 *  actually feel, or a victim is still turning when they regain control -- which reads as the hit
	 *  having spun them rather than faced them. The floor is 180 / that hitstun, and the
	 *  `HitstunSeconds` to derive against is the shortest *felt* one, not the ladder's minimum:
	 *  knockdown repurposed the heavy's and charged's into attacker-side oki knobs nobody feels.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="1.0"))
	float ForcedFacingTurnRateDegrees = 720.0f;

	/**
	 *  Played on entering the down state. Rate derived as `length / KnockdownCarrySeconds`, so the
	 *  fall covers exactly the carry.
	 *
	 *  **Authored with `bEnableAutoBlendOut` false**, which is what lets the last frame hold as the
	 *  ground pose for the jail and choice window. A montage that blends itself out here leaves the
	 *  body standing in idle while the state machine still has it on the floor.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown")
	TObjectPtr<UAnimMontage> KnockdownMontage;

	/**
	 *  Played when a normal-grade rise begins. Rate derived as `length / KnockdownRiseSeconds`.
	 *
	 *  Covers the auto-rise, the neutral stand and the block get-up -- every exit that does not bring
	 *  its own animation. The dodge does; see UTDGameplayAbility::BringsOwnRiseMontage.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown")
	TObjectPtr<UAnimMontage> RiseMontage;

	/** The hard grade's rise. Same derivation, same span -- a different clip, not a different rule. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown")
	TObjectPtr<UAnimMontage> RiseHardMontage;
	/**
	 *  Present while an action that suppresses regen is running, via that ability's owned tags.
	 *
	 *  Regen watches for this rather than each ability telling it to stop, so an ability that is
	 *  cancelled or interrupted cannot leave regen suppressed forever. The assets are authoritative
	 *  for who suppresses regen; nothing in C++ names them, which is what makes adding a fourth a
	 *  content change.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina")
	FGameplayTag StaminaRegenPausedTag;

	/**
	 *  Applied when stamina reaches zero, and what defensive abilities block on.
	 *
	 *  **Cleared when stamina reaches Max again, not on a timer.** Recovery is the thing that ends
	 *  exhaustion, so the punishment scales with how empty you ran yourself, and there is no second
	 *  number that can disagree with the bar.
	 *
	 *  Regen deliberately continues while exhausted -- exhaustion is a lockout on acting, not on
	 *  recovering, and regen is the only thing that can end it.
	 *
	 *  **The regen pause is the one thing that still suppresses it.** Holding an action at zero can
	 *  therefore suppress recovery for as long as it is held, which is a choice with an obvious exit
	 *  rather than a trap: releasing is always available, and a guard held at zero accomplishes
	 *  nothing anyway since anything it blocks breaks it.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina")
	FGameplayTag ExhaustedTag;

	/**
	 *  Present while a guard is held, via GA_Block's owned tags. Drives the drain below.
	 *
	 *  Read from the character rather than the ability draining itself, because every other stamina
	 *  rule already lives here precisely so they cannot disagree, and a second spender running on an
	 *  ability's own clock would be the first thing able to.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block")
	FGameplayTag BlockingTag;

	/**
	 *  Stamina spent per second for as long as a guard is held. **Drain, not damage.**
	 *
	 *  Self-inflicted: it runs the bar down and parks it at zero, harmlessly, for as long as the
	 *  player cares to hold. What it buys the attacker is that the defender stops being able to
	 *  *absorb* anything -- a guard at zero breaks to the very next blocked hit.
	 *
	 *  So this is not a countdown on how long you may block; it is how fast holding a guard converts
	 *  into risk, which is why raising it makes blocking more committal without taking the option
	 *  away.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.0"))
	float BlockDrainPerSecond = 10.0f;

	/**
	 *  How long a broken guard is stunned for, and how long regen stays suppressed across it.
	 *
	 *  **One number for both deliberately.** The stun and the suppression are the same event seen
	 *  from two sides, and authoring them apart would allow the pair that makes no sense -- regen
	 *  resuming while you are still stunned for it.
	 *
	 *  StaminaRegenPauseSeconds runs *after* this rather than instead of it, because the regen tick
	 *  takes the max of every live suppressor rather than assigning.
	 *
	 *  Bounded, which is what keeps it a cost rather than a deadlock.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.0"))
	float GuardBreakStunSeconds = 1.0f;

	/**
	 *  Ground speed cap while a guard is up. Authored, not derived -- a braced shuffle rather than a
	 *  walk.
	 *
	 *  **Recorded as a relationship to MaxWalkSpeed, not implemented as one**: it stays a number to
	 *  tune, so changing MaxWalkSpeed will not move it and should prompt a second look here.
	 *
	 *  Restored from the value captured at BeginPlay rather than to a constant, so a Blueprint that
	 *  authors a different MaxWalkSpeed is not silently overwritten.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.0"))
	float BlockingMaxWalkSpeed = 125.0f;

	/**
	 *  Ground speed cap while exhausted. Authored, not derived, exactly as the guard's cap is, and
	 *  recorded as a relationship to MaxWalkSpeed rather than implemented as one.
	 *
	 *  Being run dry should read in the body before the player checks a bar.
	 *
	 *  **Combines with the guard's cap by taking the slower**, which is reachable rather than
	 *  theoretical: raising a guard you cannot afford exhausts you with the guard still up. Both are
	 *  penalties, so the minimum is the only combination that cannot be gamed by entering states in
	 *  the right order.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.0"))
	float ExhaustedMaxWalkSpeed = 400.0f;

	/**
	 *  How long a raised guard is committed for. **Locking: nothing but movement is allowed.**
	 *
	 *  Without a floor the guard can be feathered at input speed, and `attack > block > attack >
	 *  block` reads as unfinished. **It has to gate the attack to do anything at all** -- a floor
	 *  that only holds the guard up while attacking still cancels out of it changes nothing. So this
	 *  is a deliberate narrowing of "whichever comes last wins": that still holds between block,
	 *  dodge and attack, *except* inside this window, where the guard has already won.
	 *
	 *  Releasing inside the window does not lower the guard early; it schedules the drop for the
	 *  moment the window ends, and the guard defends for the whole of it.
	 *
	 *  Responsiveness comes from the input buffer rather than from shortening this: an attack
	 *  pressed inside the window is refused, buffered, and fires the instant it expires.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.0"))
	float MinimumBlockSeconds = 0.25f;

	/**
	 *  Stamina taken once, the moment a guard goes up. Zero by default, so it costs nothing to raise.
	 *
	 *  **Paid, never required**, like every cost in this project: raising a guard at 4 stamina with a
	 *  cost of 5 works, empties the bar and exhausts you. The guard still does everything it would
	 *  have done, including cancelling an attack -- GAS applies an ability's cancel tags during
	 *  activation, before this is charged, so the attack is already gone by the time the exhaustion
	 *  lands.
	 *
	 *  **It is not a guard break.** Nothing is broken and there is no stun. Breaking remains the sole
	 *  preserve of stamina *damage*.
	 *
	 *  Charged on every activation, including a resume: **a resume is an intended block, and all
	 *  blocks are created equal.** With a non-zero value, holding a guard through a swing therefore
	 *  pays twice, because it is two guards.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.0"))
	float BlockInitialStaminaCost = 0.0f;

	/**
	 *  Stamina paid to the parrier when a parry lands. The one *authored* half of the reward.
	 *
	 *  Everything else a parry pays out is derived, so this is the deliberate exception: a flat
	 *  refund that makes a correct read *pay* rather than merely cost nothing, paid per parry rather
	 *  than per tier.
	 *
	 *  Lives here rather than on GA_Parry because the stamina economy is orchestrated in one place.
	 *  Clamped by the attribute set like every other write, so a reward at full stamina is silently
	 *  free rather than an overflow.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Parry", meta=(ClampMin="0.0"))
	float ParryStaminaReward = 25.0f;

	/**
	 *  **Parry Grace** -- how long a successful parry keeps protecting you after it lands.
	 *
	 *  **The problem it solves is 1vX.** A catch closes the window, so one press answers exactly one
	 *  attack -- unanswerable when two arrive together, because no human presses twice that fast.
	 *  Grace waives the second press for *simultaneous* hits only, simultaneous being this value:
	 *  **one parry per incoming attack, unless the attacks are simultaneous.**
	 *
	 *  **Derived, not chosen:** 150 ms is the interval most humans cannot beat. Re-derive it against
	 *  that, never against feel.
	 *
	 *  Three properties it deliberately does **not** have:
	 *  - **It does not re-arm.** Only a catch by an open *window* starts a tail; a catch made by
	 *    Grace itself pays the full reward and starts nothing.
	 *  - **It gates no input whatsoever, including a fresh parry.** It aids, it never restricts.
	 *  - **It jails nothing.** A successful parry frees you instantly and Grace does not take that
	 *    back, so you are mechanically free *and* protected for its duration.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Parry", meta=(ClampMin="0.0"))
	float ParryGraceSeconds = 0.15f;

	/**
	 *  Collapse into a ragdoll on death.
	 *
	 *  The cheapest unambiguous read that death has happened, and it needs no animation content.
	 *
	 *  **Requires a physics asset on the mesh or physics silently does nothing.** Ours has one
	 *  (`PA_Mannequin`, on the bundle's `SKM_Manny`); a mesh without one dies standing up.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Death")
	bool bRagdollOnDeath = true;

	/**
	 *  Debug only: seconds after death before reviving at full. 0 disables the auto-revive.
	 *
	 *  Death is deliberately the *minimum*: a state that stops the character acting, so the health
	 *  bar means something and damage observations after a kill are not garbage.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.0"))
	float DebugAutoReviveSeconds = 3.0f;

	/** Starting and maximum health. Characters spawn at full. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Attributes", meta=(ClampMin="1.0"))
	float StartingMaxHealth = 100.0f;

	/** Starting and maximum stamina. Per the design this is 100 for everyone, for now. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Attributes", meta=(ClampMin="1.0"))
	float StartingMaxStamina = 100.0f;

	/**
	 *  Debug only: throw DebugAttackInputTag on a loop, so the dummy can be defended against.
	 *
	 *  Defensive work is unjudgeable against a target that never attacks. Off by default.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugAutoAttack = false;

	/** Input the auto-attack presses, normally InputTag.Attack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	FGameplayTag DebugAutoAttackInputTag;

	/** Seconds between one auto-attack starting and the next. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.1"))
	float DebugAutoAttackInterval = 3.0f;

	/**
	 *  How long the auto-attack holds the button, which selects the tier it throws.
	 *
	 *  The hold thresholds live on GA_Attack's Branches, so this is how you aim the dummy at a
	 *  light, a heavy or a charged: 0.1 for light, 0.3 for heavy, 0.8 for charged.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.0"))
	float DebugAutoAttackHoldSeconds = 0.1f;

	/**
	 *  Snap the auto-attacker back to its spawn transform. Debug only.
	 *
	 *  Attack montages carry root motion, so an attacker on a loop walks itself across the level.
	 *  Preferred over zeroing AnimRootMotionTranslationScale: suppressing the lunge would shorten
	 *  the dummy's effective reach, and reach is what spacing tests measure.
	 *
	 *  Fires when the swing *ends*, so the attacker spends the gap between attacks where it was
	 *  placed. It also fires before each swing, normally a no-op but covering an ability that was
	 *  cancelled and so never ended cleanly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugAutoAttackResetPosition = true;

	/**
	 *  Extra delay after the attack ability ends before the reset fires. Debug only.
	 *
	 *  The ability ends when its montage blends out, which is noticeably earlier than the swing
	 *  looks finished, so resetting on that edge alone snaps the attacker home mid-follow-through.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.0"))
	float DebugAutoAttackResetDelaySeconds = 0.35f;

	/**
	 *  How many attack taps one auto-attack cycle throws, at string cadence. Debug only.
	 *
	 *  1 -- the default -- is the pre-string behaviour exactly, which is what keeps every existing
	 *  scenario's fixture untouched. Above 1, each subsequent tap lands during the previous swing,
	 *  so the buffer extension and the chain-out are exercised the way a mashing human exercises
	 *  them. The home-position reset waits for the burst to finish; a teleport mid-string would
	 *  sever the very spacing chain s4 measures.
	 *
	 *  **The whole burst must fit inside DebugAutoAttackInterval**, exactly as the single attack must.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="1"))
	int32 DebugAutoAttackStringTaps = 1;

	/** Seconds between the burst's taps. 0.25 lands each press mid-previous-swing. Debug only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.05"))
	float DebugAutoAttackStringTapIntervalSeconds = 0.25f;

	/**
	 *  Whether this auto-attacker turns to face its target, and when. Debug only.
	 *
	 *  **The turn itself needs nothing but a focus.** A possessed pawn with no focus has
	 *  AAIController copying its control rotation *from the pawn* every tick
	 *  (bSetControlRotationFromPawnOrientation, on by default), so the rotation error is permanently
	 *  zero and every turn rate multiplies nothing. A focus points the control rotation at the
	 *  target and CharacterMovementComponent closes the gap at RotationRate.Yaw.
	 *
	 *  Parity is then inherited rather than configured: IsIdle() returns false while any ability is
	 *  active, so a swinging dummy turns at TurnRateDegrees exactly as a swinging player does.
	 *  CoilTurnRateDegrees is the one rate it cannot reach while the hold selects the light.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	ETDDebugFacingMode DebugAutoAttackFacingMode = ETDDebugFacingMode::Never;

	/**
	 *  Take the *next* target each swing instead of always the nearest.
	 *
	 *  Default false, so every existing scenario is untouched. Nearest-target selection makes the
	 *  attacker **chase**: it lunges after the victim its own knockback just pushed away, so the
	 *  pair travel together and any third body is left behind -- harmless in a single-defender
	 *  fixture and fatal in a multi-target one.
	 *
	 *  Rotation advances **per attack, not per burst.** A chained string is three attacks, not one
	 *  attack in three parts, so a fixture re-targeting only when a burst begins still chases one
	 *  victim through all three.
	 *
	 *  Candidates are ordered by name, so the cycle is identical across runs. Ordering by distance
	 *  would reshuffle as knockback moves bodies about, which is the thing being escaped.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugAutoAttackRotateTargets = false;

	/**
	 *  Teleport home after **every** attack rather than only at a burst's end.
	 *
	 *  Default false, and the default is load-bearing: `s4-string` measures the spacing chain a
	 *  connecting string produces, and a teleport between attacks would sever it.
	 *
	 *  What it buys is a **stationary attacker**, the whole requirement for testing a 360-degree
	 *  volume. An attacker whiffing into empty space has an open standoff gate and travels its full
	 *  authored lunge, so without this it leaves both targets behind after one attack.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugAutoAttackHomeBetweenAttacks = false;

	/**
	 *  On landing a clean hit, immediately press dodge. Debug only, off by default.
	 *
	 *  **The on-hit waiver's only unattended witness.** The waiver frees defensive actions the
	 *  instant an attack connects, and nothing else in the fixture set ever asks an *attacker* to
	 *  defend. What it produces is a DODGE line between the attacker's own DAMAGED and the end of
	 *  its recovery; before the waiver, that press appeared as REFUSED instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugDodgeAfterHit = false;

	/**
	 *  Press block a fixed delay after each auto-attack press, cancelling the swing. Debug only.
	 *
	 *  **The unattended witness for the pre-commit cancel**, which nothing else in the fixture set
	 *  can produce: the auto-attacker only attacks and the auto-defender never attacks. It matters
	 *  more since the on-hit waiver, which loosens when defensive actions are permitted -- so the
	 *  *other* boundary, that a committed swing still cannot be cancelled, wants a standing
	 *  assertion rather than trust.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugCancelAttackIntoBlock = false;

	/**
	 *  Delay from the auto-attack press to the cancelling block press, in seconds.
	 *
	 *  Default 0.10, inside the light's 0.15 commit boundary with 50 ms to spare -- the cancel must
	 *  land *before* State.Attacking.Committed or the scenario measures a refusal rather than a
	 *  cancel. Raise it above the boundary deliberately to assert the opposite.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.0"))
	float DebugCancelAfterPressSeconds = 0.10f;

	/**
	 *  Suppress **every** lunge this character would start -- base, per-attack, and the dodge's.
	 *  Fixture-only, defaulted off, and checked in UTDGameplayAbility::StartLunge because that is
	 *  the single function they all route through. Logs `LUNGE SKIP` so it is never silently on.
	 *
	 *  This is what a *stationary* attacker actually requires, and bDebugAutoAttackHomeBetweenAttacks
	 *  is not a substitute: the lunge and the release window both happen **inside one attack**, so
	 *  re-homing afterwards leaves the hitbox having already gone live wherever the travel ended.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugSuppressLunge = false;

	/**
	 *  Debug only: defend on a loop, so the defensive economy can be watched without a human.
	 *
	 *  The mirror of bDebugAutoAttack, and pairing it with one is deliberately *two actors' job*. An
	 *  attacker that also defends is two fixtures interfering -- a guard cancels an attack's startup
	 *  and the attack's own tags refuse the guard -- so one of each is what produces a readable
	 *  exchange.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	ETDDebugDefendMode DebugAutoDefendMode = ETDDebugDefendMode::Off;

	/** Input the HoldBlock mode presses, normally InputTag.Block. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	FGameplayTag DebugDefendBlockInputTag;

	/** Input the PeriodicDodge mode taps, normally InputTag.Dodge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	FGameplayTag DebugDefendDodgeInputTag;

	/** Input the PeriodicParry mode taps, normally InputTag.Parry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	FGameplayTag DebugDefendParryInputTag;

	/**
	 *  Tap the jump input on DebugJumpIntervalSeconds. Debug only.
	 *
	 *  **Orthogonal to DebugAutoDefendMode rather than a member of it.** The rule it was built to
	 *  observe -- a broken guard can no longer jump -- needs a defender that is *blocking* (to get
	 *  broken) and *jumping* (to be refused) at the same time, which a one-mode-at-a-time enum
	 *  cannot express.
	 *
	 *  Reused by knockdown's neutral stand, which is also the jump input.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugPeriodicJump = false;

	/**
	 *  Seconds between one auto-jump and the next. Debug only.
	 *
	 *  **Co-prime with the attacker's cycle for the reason the parry's interval is**: a period that
	 *  divided the attacker's would sample one phase of the exchange forever, and the question here
	 *  is whether presses land inside a lockout -- which only some of them will.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.1"))
	float DebugJumpIntervalSeconds = 1.3f;

	/** Input the periodic jump taps, normally InputTag.Jump. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	FGameplayTag DebugJumpInputTag;

	/**
	 *  Seconds between one auto-parry and the next. Debug only.
	 *
	 *  **Must not alias against the attacker's DebugAutoAttackInterval**, or the window meets every
	 *  swing at the same phase and the run answers one question repeatedly while looking thorough.
	 *  A co-prime period sweeps, which is what makes a single unattended session produce both
	 *  successes and whiffs.
	 *
	 *  **It also has to clear the recovery**, which the dodger's interval does not: a whiffed parry
	 *  refuses defensive activations for ParryWhiffRecoverySeconds, so an interval shorter than
	 *  window + recovery would spend most of the run measuring the fixture rather than the mechanic.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.1"))
	float DebugParryIntervalSeconds = 1.7f;

	/**
	 *  Hold a guard this long before each auto-parry, to spend stamina first. 0 disables it.
	 *
	 *  **This exists to make the parry's reward observable at all.** A parry costs nothing, so an
	 *  unattended parrier never spends, its bar never leaves 100, and the attribute set's clamp eats
	 *  the entire reward -- every credited sample reads `gained=0.0`. Blocking spends stamina and,
	 *  unlike the dodge, authors **no displacement**, so it drains the parrier without moving it out
	 *  of the exchange.
	 *
	 *  **Do not raise it so far that the guard breaks**: a break refuses every ability, the parry
	 *  included, and the fixture would then measure its own guard.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.0"))
	float DebugParryPreBlockSeconds = 0.0f;

	/**
	 *  Seconds between one auto-dodge and the next. Debug only.
	 *
	 *  **Co-prime with the attacker's interval rather than a round number.** A dodger whose period
	 *  divides or multiplies DebugAutoAttackInterval meets every swing at the same phase and so
	 *  answers one question forever; co-prime periods sweep the phase across the attack instead, so
	 *  an unattended run covers dodging into windup, release and recovery without anyone scripting
	 *  it. See Docs/Working-In-Unreal.md on periodic samplers.
	 *
	 *  Clamped at 0.1 for the reason the attacker's interval is: the tap must fit inside the
	 *  interval, or the release edge lands in the next cycle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.1"))
	float DebugDodgeIntervalSeconds = 1.9f;

public:

	/**
	 *  Free-form line drawn under this character's bars by ATDDebugHUD. Debug only.
	 *
	 *  For per-activation detail that is not worth a gameplay tag -- which way a dodge resolved,
	 *  which parry window is open. Set it when an ability starts and clear it when the ability ends,
	 *  or it will outlive what it describes.
	 */
	UPROPERTY(Transient, BlueprintReadWrite, Category="Combat|Debug")
	FString DebugStatusLine;

	FTDAttackHitbox AimAssistWedge;
	FGameplayTagContainer AimAssistImmunityTags;
	bool bAimAssistHoming = false;
	bool bAimAssistDrawDebug = false;

public:

	/**
	 *  Target Lock: turn the body toward whatever the camera has tagged, for the base lunge's span.
	 *
	 *  **Homing runs before the commit checkpoint and stops at it**, which is what stops it being
	 *  the homing this design rejected. Tracking *after* commit would mean a defender's movement
	 *  could no longer make an attack whiff; tracking before it costs nothing, because commit is
	 *  where the defender's reaction window opens in the first place.
	 *
	 *  What it buys is that the correction arrives *gradually*. Without it the whole turn lands in
	 *  one frame at commit, which reads as a pop once the wedge is wide enough to matter.
	 *
	 *  Set at activation with the first branch's wedge, since every tier shares the windup. Cleared
	 *  at commit **and again in EndAbility**, the one place every exit converges -- a stranded homing
	 *  state is a character that turns toward strangers forever.
	 */
	void SetAimAssistHoming(const FTDAttackHitbox& InWedge, const FGameplayTagContainer& InImmunityTags, bool bActive, bool bInDrawDebug);

	/**
	 *  The best Target Lock candidate for a wedge, or null. Shared by homing and the commit snap.
	 *
	 *  One implementation deliberately, because two would drift -- and the one that drifted would be
	 *  the one deciding where attacks point. Selection is smallest bearing with distance breaking
	 *  ties, the tiebreak existing so two machines cannot order equal candidates differently.
	 *
	 *  @param AimYawDegrees  The frame to measure in -- the *camera's*, not the body's. See
	 *                        ATheDreamCharacter::GetAimYawDegrees.
	 */
	static AActor* FindAimAssistTarget(
		const AActor* Attacker,
		float AimYawDegrees,
		const FTDAttackHitbox& Wedge,
		const FGameplayTagContainer& ImmunityTags,
		float& OutBearingDegrees);

protected:

	/** Homing's answer for the facing system: the bearing to the tagged target, in world yaw. */
	virtual bool GetFacingHomingYaw(float& OutYaw) const override;

protected:

	/**
	 *  Held props, attached to the SwordShield pack's own `Sword` / `Shield` sockets.
	 *
	 *  Those sockets carry the grip rotation plus a non-uniform scale correcting for meshes authored
	 *  several times too large, so **both props are correct at identity and no offset should be
	 *  authored here.** They exist only on the bundle's `SKM_Manny`, not Epic's `SKM_Manny_Simple`.
	 *
	 *  **Not the `weapon_r` / `weapon_l` bones**, which fail twice: absent from Epic's mesh, and
	 *  animated only by GDH clips, so under any Epic animation a prop parented there freezes at
	 *  reference pose.
	 *
	 *  Cosmetic: collision is off and the melee trace is a separate ability task, so the sword's mesh
	 *  never decides what the sword hits. Left empty on the CDO, which keeps the dummy unarmed.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Equipment")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Equipment")
	TObjectPtr<UStaticMeshComponent> ShieldMesh;

	/**
	 *  The ASC this character is actually using: its PlayerState's, or OwnedAbilitySystemComponent.
	 *
	 *  Resolved rather than assumed, because only *players* have a PlayerState. Null until
	 *  InitialiseAbilitySystem has run, and on a client that is later than you expect, so every use
	 *  is null-checked.
	 *
	 *  **Never reach past this to OwnedAbilitySystemComponent.** For a player that subobject exists,
	 *  is registered, and holds nothing -- so using it compiles, runs, and silently reads an empty
	 *  ASC with no attributes and no abilities.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UAbilitySystemComponent> AbilitySystem;

	/** Attributes on whichever ASC was resolved. Null until InitialiseAbilitySystem has run. */
	UPROPERTY(Transient)
	TObjectPtr<UTDAttributeSet> AttributeSet;

	/**
	 *  The fallback ASC, for characters that have no PlayerState -- i.e. the training dummy.
	 *
	 *  A player carries this subobject too and never uses it: the accepted cost of one character
	 *  class serving both cases. It seeds no attributes and grants no abilities, so it costs
	 *  registration rather than bandwidth.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UAbilitySystemComponent> OwnedAbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UTDAttributeSet> OwnedAttributeSet;

private:

	/**
	 *  Points AbilitySystem at the right ASC, binds actor info, and seeds defaults once.
	 *
	 *  Safe -- and expected -- to call repeatedly: from BeginPlay, from every possession, and from
	 *  OnRep_PlayerState. Actor info is rebound every time because possession changes who owns the
	 *  ASC; seeding is guarded per-ASC, not per-call and not per-character.
	 */
	void InitialiseAbilitySystem();

	/**
	 *  Which ASC this character should use, and which actor owns it.
	 *
	 *  The PlayerState's when there is one, the owned component when there is not. OutOwner is what
	 *  InitAbilityActorInfo is given as the *owner*, which is not the avatar: GAS wants the
	 *  PlayerState as owner and the pawn as avatar, so ability state survives a pawn while traces,
	 *  sockets and montages still resolve against the body.
	 *
	 *  **Never reach past `AbilitySystem` to the owned fallback.** Which one is correct is a question
	 *  only this function may answer.
	 */
	UAbilitySystemComponent* ResolveAbilitySystem(AActor*& OutOwner) const;

	/** Attributes, abilities and effects, applied once per ASC. Authority only. */
	void SeedAbilitySystemDefaults();

	void OnAbilityInputPressed(FGameplayTag InputTag);
	void OnAbilityInputReleased(FGameplayTag InputTag);

	/** Handles of granted abilities whose InputTag matches, in activation order. */
	void GatherAbilitiesForInput(const FGameplayTag& InputTag, TArray<FGameplayAbilitySpecHandle>& OutHandles) const;

	/**
	 *  Presses the input at every matching spec and starts whatever is not already running.
	 *
	 *  Returns whether a *new* activation happened -- deliberately not whether anything is live. A
	 *  press arriving at an ability that is already running is the single most important thing to
	 *  buffer, since a committed swing is what refuses most inputs.
	 *
	 *  bForwardToActive is false on a buffered retry: the button was pressed once, and a retry is
	 *  this system trying again rather than the player pressing again. Telling a running ability it
	 *  was re-pressed every frame is a lie that WaitInputRelease would read.
	 */
	bool TryActivateAbilitiesForInput(const FGameplayTag& InputTag, bool bForwardToActive);

	/** Forwards the release edge to every matching spec. */
	void ReleaseAbilitiesForInput(const FGameplayTag& InputTag);

	/** True if any ability answering this input wants its refusal remembered. */
	bool ShouldBufferInput(const FGameplayTag& InputTag) const;

	/** Retries the buffered press, or drops it once its window has passed. */
	void TickInputBuffer();

	/**
	 *  Whether the buffered press should outlive its ordinary window: an ability answering it that
	 *  opted into extension is either running now, or its string link window is still open. The
	 *  policy is the ability's -- see UTDGameplayAbility::ShouldExtendBufferWhileActive.
	 */
	bool ShouldExtendBufferedPress(const FGameplayTag& InputTag) const;

	/** Offers the active ability answering this input the chain-out; true if it ended for it. */
	bool TryChainOutActiveAbility(const FGameplayTag& InputTag);

	/**
	 *  Replays a buffered release, HoldSeconds after the buffered press finally activated.
	 *
	 *  Cancelled the moment real input for that tag arrives, because a live edge always beats a
	 *  recorded one -- otherwise a replay scheduled for a hold the player has since abandoned would
	 *  land on whatever ability is running by then.
	 */
	void ReplayBufferedRelease(FGameplayTag InputTag);

	/** Presses the debug attack input, then releases it DebugAutoAttackHoldSeconds later. */
	void DebugAutoAttackPress();
	void DebugAutoAttackRelease();

	/**
	 *  Presses the debug dodge input, then releases it a tap later. Debug only.
	 *
	 *  HoldBlock has no equivalent pair on purpose: it presses once in BeginPlay and never releases,
	 *  and the resume tick does the rest.
	 */
	void DebugAutoDodgePress();
	void DebugAutoDodgeRelease();
	/** Taps the jump input, then releases it a frame later. See bDebugPeriodicJump. */
	void DebugAutoJumpPress();
	void DebugAutoJumpRelease();

	/**
	 *  One PeriodicParry cycle. Raises a guard first when DebugParryPreBlockSeconds is set,
	 *  otherwise goes straight to the tap.
	 *
	 *  Three steps rather than two because the guard must be *down* before the parry is pressed --
	 *  GA_Parry refuses activation while State.Blocking is present, so releasing and pressing in the
	 *  same frame would produce a refusal on every cycle and an assertion that never fires.
	 */
	void DebugAutoParryCycle();
	void DebugAutoParryDropGuard();

	/**
	 *  PeriodicParry's tap. Unlike the dodge's, it does **not** re-home the pawn first.
	 *
	 *  A parrier is supposed to stand its ground, so it never leaves the exchange the way a
	 *  backward-dodging dummy does, and a teleport between attempts would sever the very spacing the
	 *  parry is meant to be resolved at.
	 */
	void DebugAutoParryPress();
	void DebugAutoParryRelease();

	/**
	 *  Warn when this placed actor's values have drifted from the class they came from.
	 *
	 *  **Exists because the failure it catches is completely silent.** A placed actor serialises the
	 *  values its Blueprint had at the moment it was placed and keeps them as per-instance overrides
	 *  forever -- so a later-authored ability, input tag or knob never reaches it, and nothing in the
	 *  editor, the log or the game says so.
	 *
	 *  Deliberately a **warning at BeginPlay rather than a fix**: the values cannot be repaired from
	 *  here, since EditDefaultsOnly properties reject writes on an instance, and a silent repair
	 *  would hide the divergence from the person who needs to act on it. Ungated on
	 *  LogTDCombatTiming.
	 */
	void WarnOnStaleInstanceOverrides() const;

	/**
	 *  The cancelling block press, scheduled by bDebugCancelAttackIntoBlock.
	 *
	 *  Released again rather than held, unlike the HoldBlock mode: a guard left up would make BLOCK
	 *  cost fire once for the whole session instead of once per cancelled swing, and the per-swing
	 *  count is the assertion. The release is deliberately later than MinimumBlockSeconds so the
	 *  guard clears its own commitment and ends as a release rather than fighting the floor.
	 */
	void DebugCancelIntoBlockPress();
	void DebugCancelIntoBlockRelease();

	/** Teleports back to DebugAutoAttackHomeTransform and kills leftover velocity. No-op if disabled. */
	void ReturnToDebugAutoAttackHome();

	/**
	 *  Points the AI controller's focus at the nearest other pawn, or clears it. Debug only.
	 *
	 *  Re-resolved on every call rather than cached in BeginPlay, because a placed dummy is possessed
	 *  before the player pawn exists -- a focus taken at BeginPlay would be null forever.
	 */
	void UpdateDebugFacingFocus(bool bAttacking);

	/** Bound to the ASC so the attacker returns home the moment a swing finishes. */
	void HandleDebugAutoAttackEnded(const FAbilityEndedData& EndedData);

	/** Adds StaminaRegenPerSecond * delta, unless suppressed or already full. */
	void TickStaminaRegen(float DeltaSeconds);

	/** Spends BlockDrainPerSecond * delta while BlockingTag is present. Never breaks a guard. */
	void TickBlockDrain(float DeltaSeconds);

	/**
	 *  Ends any running block. Used by the guard break and by becoming airborne.
	 *
	 *  One helper because a guard has grown three ways to end that are not the player letting go, and
	 *  each must also stop the drain -- which happens for free, since the drain reads BlockingTag and
	 *  the tag leaves with the ability.
	 */
	void CancelBlockAbility();

	/**
	 *  Applies whichever authored speed caps are live, and the captured default when none are.
	 *
	 *  Takes the *slowest* applicable cap rather than the last one checked, because blocking and
	 *  exhaustion can overlap.
	 */
	void TickMoveSpeedClamps();

	/**
	 *  Maintains State.Blocking.Committed, and finishes a release that was held back by it.
	 *
	 *  Written as "make the tag match the current state" rather than as edges, for the reason the
	 *  speed cap is: an edge-driven version has to be right on every entry and exit, and a stranded
	 *  commit tag would refuse every action forever with nothing on screen to explain it.
	 */
	void TickBlockCommitment(float Now);

	/** Requests a resume pass. Never performs one -- OnAbilityEnded is re-entrant. */
	void HandleAbilityEndedForResume(const FAbilityEndedData& EndedData);

	/** Re-activates abilities that opted into resuming and whose input is still held. */
	void TickResumeHeldAbilities();

	/** Applies ExhaustedTag. Removed only once stamina is back to Max -- there is no timer. */
	void EnterExhaustion();
	void ExitExhaustion();

	/**
	 *  The locally-visible half of exhaustion and death, run on every machine.
	 *
	 *  **The split is the whole point.** Enter/Exit decide *whether* the state changed and run on the
	 *  server alone, because the stamina and health delegates that drive them are bound behind the
	 *  authority gate. Apply/Clear make the state *true on this machine* -- the tag, the ragdoll, the
	 *  movement stop -- and run on the server directly and on clients from OnRep.
	 *
	 *  A loose gameplay tag does not replicate, so without this split a client's ASC never has the
	 *  tag: its own CanActivateAbility would pass, it would predict an action the server had already
	 *  refused, and only a correction would tell it otherwise.
	 */
	void ApplyExhaustionState();
	void ClearExhaustionState();
	void ApplyDeathState();
	void ClearDeathState();

	/**
	 *  Starts and ends the guard-break stun. Server decides; the state pair runs everywhere.
	 *
	 *  EndGuardBreak is driven from Tick against GuardBreakEndsAt rather than from a timer, so the
	 *  stun cannot outlive a pause, a seek or a slow frame in a way nothing observes.
	 */
	void EnterGuardBreak();
	void EndGuardBreak();
	void ApplyGuardBreakState();
	void ClearGuardBreakState();

	/**
	 *  Ends the blockstun lockout. Driven from Tick against BlockstunEndsAt, as the guard break is.
	 *
	 *  There is no ApplyBlockstunState pairing beyond the tag, because blockstun cancels nothing --
	 *  it refuses activations through ActivationBlockedTags and lets whatever is running finish.
	 *  That is the deliberate difference from a break, which takes the guard down with it.
	 */
	void EndBlockstun();
	void ApplyBlockstunState();
	void ClearBlockstunState();

	/** Ends the parry lockout. Driven from Tick against ParryLockoutEndsAt, as its siblings are. */
	void EndParryLockout();
	void ApplyParryLockoutState();
	void ClearParryLockoutState();

	void EndKnockdown();
	void ApplyKnockdownState();
	void ClearKnockdownState();

	/** Advances the jail -> choice -> rise -> stand boundaries. Authority only, called from Tick. */
	void TickKnockdown();

	/** Turns the body toward ForcedFacingTarget at the derived rate. Called from Tick. */
	void TickForcedFacing(float DeltaSeconds);

	/** The radial carry: attacker + (attacker->victim bearing) * KnockdownSpacingCm, Z natural. */
	void ApplyKnockdownCarry(AActor* Attacker);

	/** Plays a knockdown montage at a rate derived to fit TargetSeconds. Shared by the fall and both rises. */
	void PlayKnockdownMontage(UAnimMontage* Montage, float TargetSeconds, const TCHAR* Label);

	/** Jail seconds for the grade currently held. */
	float GetKnockdownJailSeconds() const;

	/** Choice-window seconds for the grade currently held. */
	float GetKnockdownChoiceSeconds() const;

	/**
	 *  Ends hitstun. Driven from Tick against HitstunEndsAt, as its two siblings are. The Apply half
	 *  carries no cancel -- EnterHitstun cancels on the server once; a client's OnRep must not cancel
	 *  predicted copies out from under a correction, the death path's exact reasoning.
	 */
	void EndHitstun();
	void ApplyHitstunState();
	void ClearHitstunState();

	/**
	 *  Ends the parry recovery. Driven from Tick against ParryRecoveryEndsAt, as the stuns are.
	 *
	 *  There is no window equivalent here: the window's expiry is CloseParryWindow, which is shared
	 *  with the cancellation path so that a recovery cannot be dodged by ending the parry unusually.
	 *
	 *  **This one also ends GA_Parry**, which a whiff deliberately leaves running so its movement
	 *  lock spans the recovery.
	 */
	void EndParryRecovery();
	void ApplyParryRecoveryState();
	void ClearParryRecoveryState();

	/** The dodge's parry gap, same shape. Ends no ability -- GA_Dodge is long over by then. */
	void EndDodgeRecovery();
	void ApplyDodgeRecoveryState();
	void ClearDodgeRecoveryState();

	/**
	 *  State.Parrying, applied and cleared against the window rather than against the ability.
	 *
	 *  There is no Begin/End pair here because the window already has one -- OpenParryWindow and
	 *  CloseParryWindow -- and adding a second would give the tag a lifetime that could drift from
	 *  the thing it names.
	 */
	void ApplyParryWindowState();
	void ClearParryWindowState();

	/**
	 *  Ends a running parry recovery because something was inflicted on this character.
	 *
	 *  **The schema doing the work rather than a special case**: a lockout is externally inflicted
	 *  and a recovery is self-inflicted, so being punished supersedes the price you were paying
	 *  yourself. **Anything that inflicts a lockout should call this** -- that generality is
	 *  deliberate and is not to be narrowed to hitstun.
	 */
	void OverrideParryRecovery(const TCHAR* Cause);

	/** Starts the Grace tail. Called only from a window catch, never from a Grace catch. */
	void BeginParryGrace();

	/** Grace expiring. Driven from Tick against ParryGraceEndsAt, as the recoveries are. */
	void EndParryGrace();

	UFUNCTION()
	void OnRep_Dead();

	UFUNCTION()
	void OnRep_Exhausted();

	UFUNCTION()
	void OnRep_GuardBroken();

	UFUNCTION()
	void OnRep_Blockstun();

	UFUNCTION()
	void OnRep_Hitstun();

	UFUNCTION()
	void OnRep_KnockedDown();

	UFUNCTION()
	void OnRep_ParryLockout();

	UFUNCTION()
	void OnRep_ParryRecovery();

	UFUNCTION()
	void OnRep_DodgeRecovery();

	UFUNCTION()
	void OnRep_ParryWindow();

	/**
	 *  Applies State.Dead, cancels everything running, and stops the character moving.
	 *
	 *  Cancelling is the deliberate difference from exhaustion, which gates activation and lets a
	 *  running ability finish. Without it a killing blow landing mid-swing leaves the dead character
	 *  completing the attack, hitbox and all.
	 */
	void EnterDeath();

	/** Debug only: clears State.Dead, restores movement, refills health and stamina. */
	void ReviveFromDebug();

	/** Hands the mesh to physics. No-op without a physics asset -- and silent, so verify in play. */
	void StartRagdoll();

	/** Returns the mesh to the capsule at its authored offset. Exact, or the character stays skewed. */
	void StopRagdoll();

	void HandleStaminaChanged(const struct FOnAttributeChangeData& Data);
	void HandleHealthChanged(const struct FOnAttributeChangeData& Data);

	/** The one press waiting for something to answer it. Single slot: last press wins. */
	FTDBufferedInput BufferedInput;

	/**
	 *  The same direction pair for the press an ability is activating from *right now*.
	 *
	 *  Written for every ability press rather than only for the dodge, because the character has no
	 *  business knowing which tag means dodge. A buffered press restores these from the buffer
	 *  immediately before it retries activation, so an ability reads one field whether it fired live
	 *  or late.
	 */
	float PressMoveAngleDegrees = 0.0f;
	bool bPressHadMoveInput = false;

	/** Fills the pair above from LastRequestedMoveInput and the current facing. */
	void CaptureMoveDirectionForPress();

	/** Pending replayed release. Live input for the same tag cancels it. */
	FTimerHandle BufferedReleaseTimerHandle;

	/** World time before which regen stays suppressed. Pushed out while a suppressor is active. */
	float RegenSuppressedUntil = 0.0f;

	/** True between an actual jump launch and landing. Never set by merely falling. */
	bool bJumpRegenPauseActive = false;

	/**
	 *  Replicated, because the tags they drive are *loose* tags and loose tags do not replicate.
	 *
	 *  These bools are the wire format and the tag is the local consequence: the server decides, the
	 *  bool replicates, and OnRep applies the same tag on each client that the server applied on
	 *  itself. A GameplayEffect carrying the tag would replicate on its own and is the more usual
	 *  answer, but UE 5.8 expresses effect-granted tags through gEComponents, which cannot be
	 *  scripted -- see Docs/Working-In-Unreal.md.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Exhausted)
	bool bExhausted = false;

	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;

	/** Follows the same server-decides/OnRep-applies contract as the two above. */
	UPROPERTY(ReplicatedUsing = OnRep_GuardBroken)
	bool bGuardBroken = false;

	/**
	 *  When the guard-break stun expires, in world seconds. Server-authoritative.
	 *
	 *  A timestamp checked in Tick rather than a SetTimer, deliberately: the two network-unaware
	 *  SetTimer sites this project already has are filed as a multiplayer trap. A timestamp is also
	 *  what the regen suppressor beside it uses.
	 */
	float GuardBreakEndsAt = 0.0f;

	/** Follows the same server-decides/OnRep-applies contract as the three above. */
	UPROPERTY(ReplicatedUsing = OnRep_Blockstun)
	bool bInBlockstun = false;

	/** The fifth of the family, same contract. Blockstun's sibling for hits that were not blocked. */
	UPROPERTY(ReplicatedUsing = OnRep_Hitstun)
	bool bInHitstun = false;

	/** When hitstun expires, in world seconds. A Tick-checked timestamp like its two siblings. */
	float HitstunEndsAt = 0.0f;

	/**
	 *  On the floor. **Ninth member of the replicated state family**, same contract as the eight
	 *  before it: the server decides, this replicates, OnRep applies the tag locally.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_KnockedDown)
	bool bKnockedDown = false;

	/**
	 *  Which grade is running. Replicated beside the bool rather than derived, because the split it
	 *  selects decides which get-up options a *client* may predict, and a client that guessed would
	 *  mispredict a refusal.
	 */
	UPROPERTY(Replicated)
	ETDKnockdownGrade KnockdownGrade = ETDKnockdownGrade::None;

	/** Authority-side boundary: when everything stops being refused. */
	float KnockdownJailEndsAt = 0.0f;

	/** Authority-side boundary: when the auto-rise takes the choice away. */
	float KnockdownChoiceEndsAt = 0.0f;

	/** Authority-side boundary: the stand. Set when a rise begins, not at entry. */
	float KnockdownRiseEndsAt = 0.0f;

	/**
	 *  Whether a rise has begun. **The invincibility switch**, and the reason it is separate from the
	 *  tag: the tag spans the rise too, because the body is still on the floor for every purpose
	 *  except being hittable.
	 */
	bool bKnockdownRising = false;

	/** Whether a forced turn is running, and what it is turning toward. */
	bool bForcedFacingActive = false;

	/** Weak, because the attacker can die, despawn or be destroyed mid-turn. */
	TWeakObjectPtr<AActor> ForcedFacingTarget;

	/** Yaw the turn started from, so FACING FORCED can report the span it actually covered. */
	float ForcedFacingStartYaw = 0.0f;

	/**
	 *  The last carry's bearing: the radial axis's angle off the attacker's facing, in degrees.
	 *
	 *  Trace-only, and server-only. It exists so an assertion can tell the two carry axes apart --
	 *  roughly zero in a 1v1, roughly plus or minus ninety for the ender's two victims.
	 */
	float LastKnockdownBearingDegrees = 0.0f;

	/**
	 *  Serving a parry lockout. **Tenth member of the replicated state family**, same contract.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ParryLockout)
	bool bInParryLockout = false;

	/** Authority-side deadline. Authored on the caught swing; see EnterParryLockout. */
	float ParryLockoutEndsAt = 0.0f;

	/**
	 *  A parry window is open. The sixth of the replicated-state family, same contract.
	 *
	 *  **Replicated rather than left as the ability's business**, under the project's own rule that
	 *  new state is a replicated property and never a loose tag. This bool is the mechanism the
	 *  attacker's hit path reads; State.Parrying is the tag marking the same span for anything that
	 *  gates on it. Keeping them apart is what lets the animation and the negation be retimed
	 *  independently.
	 *
	 *  **The tag is applied and cleared against *this* bool**, not against GA_Parry's lifetime: a
	 *  whiffed parry keeps the ability alive across its recovery to hold the movement lock, so a tag
	 *  riding the ability would stay up for the whole jail. State.Parrying means the window is open
	 *  and State.ParryRecovery means the recovery is running -- each says what it is named.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ParryWindow)
	bool bParryWindowOpen = false;

	/** When the parry window expires, in world seconds. A Tick-checked timestamp like the stuns. */
	float ParryWindowEndsAt = 0.0f;

	/**
	 *  What the currently-open window will charge if it closes without catching anything.
	 *
	 *  Captured when the window opens rather than read back off GA_Parry at close time, so the price
	 *  of a window is fixed the moment it is bought.
	 */
	float PendingParryWhiffRecoverySeconds = 0.0f;

	/** Whether the open window has already negated a hit, which is what makes its close free. */
	bool bParryCaughtThisWindow = false;

	/** Every ability is refused by a whiffed parry. The seventh of the family, same contract. */
	UPROPERTY(ReplicatedUsing = OnRep_ParryRecovery)
	bool bInParryRecovery = false;

	/** When the parry recovery expires, in world seconds. Max-extended, never reassigned. */
	float ParryRecoveryEndsAt = 0.0f;

	/** A parry is refused by a just-ended dodge. The eighth of the family, same contract. */
	UPROPERTY(ReplicatedUsing = OnRep_DodgeRecovery)
	bool bInDodgeRecovery = false;

	/** When the dodge's parry gap expires, in world seconds. Max-extended, never reassigned. */
	float DodgeRecoveryEndsAt = 0.0f;

	/**
	 *  A successful parry is still protecting this character. **Replicated but tagless**, which is
	 *  the one place this breaks the family's pattern: the others carry a gameplay tag because
	 *  something refuses on them, and nothing refuses on Grace.
	 */
	UPROPERTY(Replicated)
	bool bInParryGrace = false;

	/**
	 *  When Grace expires, in world seconds. **Assigned, never max-extended** -- the deliberate
	 *  opposite of every other timestamp here, and it is the no-re-arm rule expressed in one line.
	 *  Only BeginParryGrace writes it, and only a window catch calls that.
	 */
	float ParryGraceEndsAt = 0.0f;

	/** See GetPendingParryMontageRecoveryRate. -1 means the parry montage has no marker. */
	float PendingParryMontageRecoveryRate = -1.0f;

	/** A connected attack owes this character its movement back. See BeginOnHitMovementWaiver. */
	bool bOnHitMovementWaiverPending = false;

	/** When the on-hit waiver hands movement back, in world seconds: contact + that swing's hitstun. */
	float OnHitMovementWaiverAt = 0.0f;

	/** Dedup clock for bDebugDodgeAfterHit, so a swing hitting two bodies still dodges once. */
	float DebugLastDodgeAfterHitAt = -1.0f;

	/**
	 *  Which hit of the light string the *next* chained attack continues from. 0 is a fresh string's
	 *  first swing. Replicated so remote machines can agree which swing an activation means. See
	 *  ResolveStringSwingIndexForActivation.
	 */
	UPROPERTY(Replicated)
	uint8 StringIndex = 0;

	/**
	 *  When the string's link window closes, in world seconds. Local like BlockstunEndsAt -- the
	 *  replicated index is the wire truth, this is only its deadline.
	 */
	float StringWindowEndsAt = 0.0f;

	/** The running knockback translation's root motion source ID, so a re-hit can replace it. */
	uint16 KnockbackRootMotionSourceID = 0;

	/** Taps left in the current auto-attack burst. Guards the home reset; see the taps knob. */
	int32 DebugStringTapsRemaining = 0;

	/** The body the last attack aimed at, excluded from the next selection while rotating. A weak
	 *  pointer because a target can die and be destroyed between attacks, and a stale raw pointer
	 *  would then exclude an address rather than a pawn. */
	TWeakObjectPtr<APawn> DebugLastFocusTarget;

	/**
	 *  When the blockstun lockout expires, in world seconds. Server-authoritative.
	 *
	 *  A timestamp checked in Tick rather than a SetTimer, for the reason GuardBreakEndsAt gives.
	 *  Extended by taking the max, never reassigned, so a second blocked hit landing inside an
	 *  existing lockout can only lengthen it -- blocking two attacks must not be a way to serve a
	 *  shorter sentence than blocking the slower one alone.
	 */
	float BlockstunEndsAt = 0.0f;

	/**
	 *  True only while TickBlockDrain is writing. **This is what stops drain exhausting you.**
	 *
	 *  Exhaustion is decided by the stamina-changed delegate, which sees "the bar reached zero" and
	 *  cannot see what emptied it. That is correct for every other spender -- a dodge that empties
	 *  you should exhaust you -- and wrong for the guard's drain, the only *continuous* spend in the
	 *  game, which is meant to park the bar at zero harmlessly.
	 *
	 *  The rule it encodes: **exhaustion is a consequence of an action or an attack, never of a state
	 *  you are holding.**
	 */
	bool bApplyingBlockDrain = false;

	/** MaxWalkSpeed as authored, captured before any block modifies it. */
	float DefaultMaxWalkSpeed = 0.0f;

	/**
	 *  A resume pass is owed on the next tick. Set by an ability ending and by landing.
	 *
	 *  The deferral is the fix, not an optimisation: performing the resume inside OnAbilityEnded
	 *  re-entered ability activation and double-activated the guard, leaking its spec's activeCount
	 *  and sticking it up permanently. A flag drained once per tick cannot re-enter anything.
	 */
	bool bResumePending = false;

	/** When the guard's minimum duration expires, in world seconds. */
	float BlockCommitEndsAt = 0.0f;


	/**
	 *  The mesh's authored offset from the capsule, captured once before physics ever moves it.
	 *
	 *  Ragdolling drives the mesh in world space, so the relative transform is meaningless by the
	 *  time it stops. Restoring from a value read *after* death would bake the ragdoll's final pose
	 *  in as the new rest offset, and the character would stand up crooked and stay that way.
	 *  Captured in BeginPlay rather than the constructor so it reflects whatever a Blueprint authored.
	 */
	FTransform MeshRestRelativeTransform;

	bool bRagdollActive = false;

	FTimerHandle DebugReviveTimerHandle;

	/**
	 *  Seeded-once flag for the *owned* ASC only -- i.e. the training dummy's.
	 *
	 *  A player's flag lives on ATDPlayerState instead, because that is where its ASC lives.
	 *  Splitting them is not tidiness: a player pawn's BeginPlay runs before possession, so a single
	 *  flag on the character would be spent seeding the fallback ASC and the real one would never be
	 *  seeded at all. See ATDPlayerState::HasSeededDefaults.
	 */
	bool bOwnedDefaultsApplied = false;

	FTimerHandle DebugAutoAttackTimerHandle;
	FTimerHandle DebugAutoAttackReleaseTimerHandle;
	FTimerHandle DebugAutoAttackResetTimerHandle;
	FTimerHandle DebugAutoAttackStringTimerHandle;

	FTimerHandle DebugAutoDodgeTimerHandle;
	FTimerHandle DebugAutoParryTimerHandle;
	FTimerHandle DebugAutoParryReleaseTimerHandle;
	FTimerHandle DebugAutoParryPreBlockTimerHandle;
	FTimerHandle DebugCancelIntoBlockTimerHandle;
	FTimerHandle DebugAutoDodgeReleaseTimerHandle;

	FTimerHandle DebugAutoJumpTimerHandle;
	FTimerHandle DebugAutoJumpReleaseTimerHandle;

	/**
	 *  Where a debug fixture started, captured once, so each swing or dodge begins from the same
	 *  spot. Named for the auto-attacker because it shipped with it; the auto-defender shares it
	 *  rather than carrying a second copy of the same transform.
	 */
	FTransform DebugAutoAttackHomeTransform;
};
