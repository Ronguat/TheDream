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
class UCurveVector;
class UGameplayAbility;
class UGameplayEffect;
class UInputAction;
class UStaticMeshComponent;
class UTDAttributeSet;
struct FAbilityEndedData;

/**
 *  A press nothing could answer, kept for a moment in case something can.
 *
 *  Records both edges: the ladder's identity depends on hold length, so a press replayed without
 *  knowing the button came up would sail past every checkpoint and turn a tap into a charged heavy.
 *
 *  The release is a duration, replayed at that offset from activation. A yes/no flag will not do --
 *  the buffer survives indefinitely while the button is held, so any hold length is reachable.
 */
struct FTDBufferedInput
{
	/** Which input was pressed. Invalid means the buffer is empty. */
	FGameplayTag InputTag;

	/** World time the button went down, for the trace's "how late did this fire". */
	float PressWorldTime = 0.0f;

	/** World time to give up at. Held buttons keep pushing this forward; see TickInputBuffer. */
	float ExpiryWorldTime = 0.0f;

	/** How long the button was held, valid once bReleased. Replayed at this offset from activation. */
	float HoldSeconds = 0.0f;

	/** True once the button came up, whether or not the buffer has fired. */
	bool bReleased = false;

	/**
	 *  Signed degrees from facing at press -- 0 forward, +90 right, 180 back. Valid only while
	 *  bHadMoveInput.
	 *
	 *  A directional dodge is one composite input, so direction buffers with the press: releasing
	 *  the key inside the window still gives the dodge asked for. Resolved at press rather than
	 *  stored raw, because resolution needs that moment's facing and the camera can turn since.
	 *
	 *  The dodge is the only input that latches direction. An attack's is read at commit, so a
	 *  buffered attack goes where the camera is when it fires -- the buffered-aim trap's question.
	 */
	float MoveAngleDegrees = 0.0f;

	/** False when the press was made standing still, which is what a neutral dodge reads. */
	bool bHadMoveInput = false;

	bool IsSet() const { return InputTag.IsValid(); }
	void Clear() { *this = FTDBufferedInput(); }
};

/**
 *  Why a parry window is closing. Exactly three, and that is the mechanic.
 *
 *  Parry is sacred: the only way out of a committed parry is success, and nothing is designed to
 *  beat one while active -- a promise not made for block. The enum is therefore exhaustive by
 *  design, a fourth reason would be a design change, and closing takes a reason rather than a bool.
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
	 *  Died. The single exception to "sacred", on the house -- no recovery charged.
	 *
	 *  Unreachable today: nothing damages you through an open window, so only a damage-over-time
	 *  effect could kill you inside one, and none exists. Goes live when the ranged/DoT question does.
	 */
	Death,
};

/**
 *  When a debug auto-attacker turns to face what it is swinging at.
 *
 *  A dummy that never turns is a stable fixture and a misleading one -- an attack that cannot
 *  follow a sidestepping player reads as more forgiving than a human would. One that always faces
 *  you is intrusive when measuring something else, hence three modes rather than a bool.
 *
 *  Facing is all it does; the dummy never approaches. The parity rule is "accurate in the dimension
 *  being measured", and Block measures what an arriving attack does, not how it closed distance.
 */
UENUM(BlueprintType)
enum class ETDDebugFacingMode : uint8
{
	/** Never turn. Kept as a control. */
	Never,

	/**
	 *  Turn only while an attack is running; the position reset restores the placed yaw between
	 *  swings. The dummy's default -- it aims what it throws without following you around the level.
	 */
	WhileAttacking,

	/** Turn continuously. Closest to a real opponent, and the most intrusive to work around. */
	Always
};

/**
 *  What a debug auto-defender does with the attacks coming at it.
 *
 *  One behaviour at a time rather than a defensive AI: a dummy choosing between blocking and
 *  dodging needs a policy, and a policy is one more thing to debug. One mode, one behaviour, so
 *  the log stays readable.
 */
UENUM(BlueprintType)
enum class ETDDebugDefendMode : uint8
{
	/** Defend nothing. The dummy's default: a pure damage target. */
	Off,

	/**
	 *  Raise the guard once and never let go. One press is the whole implementation: GA_Block opts
	 *  into bResumeWhileInputHeld and the press path marks the spec InputPressed even when
	 *  activation fails, so the resume tick puts the guard back after every break and cancel.
	 */
	HoldBlock,

	/**
	 *  Tap the dodge input on DebugDodgeIntervalSeconds. With no movement input a dodge resolves
	 *  backward, so this spends stamina on a fixed schedule and travels a known distance.
	 */
	PeriodicDodge,

	/**
	 *  Tap the parry input on DebugParryIntervalSeconds.
	 *
	 *  The phase sweep is the design. A 300 ms window against attacks on their own schedule, at a
	 *  period co-prime with the attacker's, walks across windup, release and recovery -- yielding
	 *  successes and whiffs from one run. A dividing period would answer one question forever while
	 *  looking like a working fixture. See DebugParryIntervalSeconds.
	 */
	PeriodicParry
};

/**
 *  Which get-up option a debug defender takes, pressed once per knockdown at the input window's
 *  start plus DebugGetUpDelaySeconds. One mode, one press, so a knockdown log reads as one choice.
 */
UENUM(BlueprintType)
enum class ETDDebugGetUpMode : uint8
{
	/** Press nothing; the auto-rise takes it. */
	Wait,

	/** Tap the dodge input: the directional get-up, or the kip-up under the hard type. */
	DodgeGetUp,

	/** Press and hold the block input through the rise. */
	BlockGetUp,

	/** Tap the attack input: the get-up attack. */
	AttackGetUp,

	/** Tap the jump input: the neutral stand. */
	StandGetUp
};

/**
 *  Base class for anything that can fight: the player and the training dummy alike.
 *
 *  Locomotion and the third person camera come from ATheDreamCharacter; this adds the Ability
 *  System Component and the core combat attributes. Abilities are granted from DefaultAbilities, so
 *  a Blueprint subclass decides what it can do without graph wiring -- an empty list is a valid
 *  damage target that cannot act.
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

	/** Health as a 0-1 fraction, for bars and debug readouts. */
	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetHealthPercent() const;

	/** Stamina as a 0-1 fraction, for bars and debug readouts. */
	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetStaminaPercent() const;

	/** True while stamina regen is suppressed, by an action or the tail after one. */
	UFUNCTION(BlueprintPure, Category="Combat|Stamina")
	bool IsStaminaRegenPaused() const;

	UFUNCTION(BlueprintPure, Category="Combat|Stamina")
	bool IsExhausted() const { return bExhausted; }

	/** True from health reaching 0 until revived. Every ability is refused throughout. */
	UFUNCTION(BlueprintPure, Category="Combat|Health")
	bool IsDead() const { return bDead; }

	/** Fixture-only: this character starts no lunges. See bDebugSuppressLunge. */
	UFUNCTION(BlueprintPure, Category="Combat|Debug")
	bool IsDebugLungeSuppressed() const { return bDebugSuppressLunge; }

	/** True while BlockingTag is present, i.e. a guard is up. */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	bool IsBlocking() const;

	/** True from a guard breaking until its stun expires. Every ability is refused throughout. */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	bool IsGuardBroken() const { return bGuardBroken; }

	/**
	 *  True while a successful block's lockout is running. Refuses offense only.
	 *
	 *  Not IsGuardBroken(): that is the failed-guard penalty and refuses everything. A block that
	 *  worked costs initiative, not the guard.
	 */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	bool IsInBlockstun() const { return bInBlockstun; }

	/**
	 *  Heading held when the press that activated this ability landed, signed degrees from facing.
	 *  False when that press was neutral.
	 *
	 *  Read this rather than the movement component: GetLastInputVector() is empty for the whole of
	 *  any ability that locks movement, so it always answers "nowhere".
	 */
	bool GetPressMoveDirection(float& OutAngleDegrees) const
	{
		OutAngleDegrees = PressMoveAngleDegrees;
		return bPressHadMoveInput;
	}

	/**
	 *  Starts blockstun, or extends a running one to the later end time.
	 *
	 *  Called by the attacking ability, so the duration travels with the attack rather than being a
	 *  property of the defender -- a heavy pins a guard longer than a light, and only the attack
	 *  knows which it was.
	 */
	void EnterBlockstun(float DurationSeconds);

	/** True while cleanly-hit stun is running. Refuses everything -- see State_Hitstun. */
	UFUNCTION(BlueprintPure, Category="Combat|Stun")
	bool IsInHitstun() const { return bInHitstun; }

	/**
	 *  Starts hitstun, or extends a running one -- blockstun's shape, plus one addition: entry
	 *  cancels everything the victim was doing, committed or not. Commitment governs what you may
	 *  cancel voluntarily, not what being hit does to you. Also resets the victim's string. Server
	 *  decides; the state pair applies the tag everywhere. 0 no-ops.
	 */
	void EnterHitstun(float DurationSeconds);

	/**
	 *  Where the hitstun tell's playhead belongs this frame, in seconds into its clip.
	 *
	 *  Progress through the current stun scaled onto HitstunTellPortionSeconds, so the tell fills a
	 *  stun of any length and restarts on each hit. Read from the anim graph by
	 *  UTDAnimTellTools::DriveHitstunTell, which holds the player at rate zero and sets this.
	 *  Zero until the first hit lands.
	 */
	UFUNCTION(BlueprintPure, Category="Combat|Stun")
	float GetHitstunTellTime() const;

	/** Blockstun's half of the same pair, against BlockstunTellPortionSeconds. */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	float GetBlockstunTellTime() const;

	/** The third of the family, against ParryLockoutTellPortionSeconds. */
	UFUNCTION(BlueprintPure, Category="Combat|Parry")
	float GetParryLockoutTellTime() const;

	/** Whether this body is on the floor -- lockout, input window or rise. */
	bool IsKnockedDown() const { return bKnockedDown; }

	/** Which type put it there. None whenever IsKnockedDown() is false. */
	ETDKnockdownType GetKnockdownType() const { return KnockdownType; }

	/**
	 *  Whether hits pass straight through this body. True from the knockdown until any rise begins,
	 *  auto or chosen; each get-up option then prices its own rise.
	 *
	 *  The rise-begin frame resolves to the defender -- ties at a protective boundary go to the
	 *  protected, so tick order cannot coin-flip an outcome.
	 */
	bool IsKnockdownInvulnerable() const { return bKnockedDown && !bKnockdownRising; }

	/** Whether get-up options are currently legal -- past the lockout, before any rise. */
	bool IsInKnockdownInputWindow() const;

	/** Asks the resume tick to retry held inputs on its next pass. */
	void RequestResumePass() { bResumePending = true; }

	/**
	 *  Put this character on the floor. Supersedes hitstun rather than joining it.
	 *
	 *  Replaces EnterHitstun for any hit whose swing authored a type: cancels through the same
	 *  funnel death uses, resets the string, overrides a whiffed parry's recovery, starts the radial
	 *  carry and begins forced facing. Server-only.
	 *
	 *  @param Type     Which split to run. None returns without doing anything.
	 *  @param Attacker  The carry's radial origin and the facing target.
	 */
	void EnterKnockdown(ETDKnockdownType Type, AActor* Attacker);

	/** Whether a parried attacker is serving their lockout. */
	UFUNCTION(BlueprintPure, Category="Combat|Parry")
	bool IsInParryLockout() const { return bInParryLockout; }

	/**
	 *  Lock this attacker out for the duration the caught swing authored.
	 *
	 *  Externally inflicted, so a lockout rather than a recovery, and a lockout overrides one. Takes
	 *  the full movement lock, refuses every ability from the shared base, ends the string.
	 *
	 *  @param LockoutSeconds  Authored on the caught swing -- see
	 *                         UTDMeleeAttackAbility::ParryLockoutSeconds. Zero is honoured.
	 */
	void EnterParryLockout(float LockoutSeconds);

	/**
	 *  Start the rise. Ends invincibility on the frame it is called, and commits: no options, no
	 *  movement, no way back to the floor.
	 *
	 *  Called by the auto-rise at the input window's close and by every get-up option, which is
	 *  what makes "the action is the exit" true -- there is no shared pre-rise to wait through.
	 *
	 *  @param By  Trace label: auto, dodge, block, attack, kipup or stand.
	 */
	void BeginKnockdownRise(const TCHAR* By, bool bPlayRiseMontage = true, float RiseSecondsOverride = 0.0f);

	/**
	 *  Turn this body toward an attacker at the derived rate. Every clean hit, not only knockdowns.
	 *  The camera never moves -- this is the body.
	 */
	void BeginForcedFacing(AActor* Toward);

	/**
	 *  True while a parry window is open and has not caught anything.
	 *
	 *  Asked by the attacker's hit path, which is why this is state on the character rather than on
	 *  GA_Parry: a defender cannot refuse a hit it never sees, so negation resolves where the hit is
	 *  detected -- and that code has a character pointer, not an ability one.
	 */
	UFUNCTION(BlueprintPure, Category="Combat|Parry")
	bool IsParryWindowOpen() const { return bParryWindowOpen; }

	/**
	 *  Opens the negation window for DurationSeconds. Called by GA_Parry on activation. The whiff
	 *  recovery travels in with it, so a window's price is fixed the moment it opens.
	 */
	void OpenParryWindow(float DurationSeconds, float WhiffRecoverySeconds);

	/**
	 *  Closes the window, charging the whiff recovery unless it caught something. Idempotent.
	 *
	 *  One exit for every way a window can end -- expiry in Tick, cancellation, death -- so the
	 *  recovery cannot be skipped by ending the parry through an unusual path.
	 */
	void CloseParryWindow(ETDParryCloseReason Reason);

	/**
	 *  True while a successful parry's Grace tail is still protecting this character.
	 *
	 *  Carries no gameplay tag, unlike every other state here: tags exist to refuse things, and
	 *  Grace refuses nothing. A tag would invite something to start blocking on it.
	 */
	UFUNCTION(BlueprintPure, Category="Combat|Parry")
	bool IsInParryGrace() const { return bInParryGrace; }

	/**
	 *  The rate the parry clip's recovery segment switches to, or -1 if unset.
	 *
	 *  Parked here because the ability is gone by the time it is needed on a success -- a catch ends
	 *  it at once, and the authored recovery rate must be used regardless. Computed once at
	 *  activation; see UTDParryAbility::PlayParryMontage.
	 */
	float GetPendingParryMontageRecoveryRate() const { return PendingParryMontageRecoveryRate; }
	void SetPendingParryMontageRecoveryRate(float Rate) { PendingParryMontageRecoveryRate = Rate; }

	/**
	 *  A parry landed: pay the reward and close the window free of charge. Called from the attacker's
	 *  hit path on the server, the only place that knows a hit resolved. Clears the regen pause
	 *  outright -- a whiff pays the pause, a success discharges it.
	 */
	void NotifyParrySuccess(AActor* Attacker);

	/**
	 *  Refuses every ability for DurationSeconds after a whiffed parry, or extends a running
	 *  recovery. Max-extended, following blockstun: two overlapping causes must never produce a
	 *  shorter total than either alone.
	 */
	void ApplyParryRecovery(float DurationSeconds);

	/** True while every ability is refused by a whiffed parry. See State_ParryRecovery. */
	UFUNCTION(BlueprintPure, Category="Combat|Parry")
	bool IsInParryRecovery() const { return bInParryRecovery; }

	/**
	 *  Refuses parry only for DurationSeconds after a dodge ended, or extends a running gap.
	 *  Separate from ApplyParryRecovery, which refuses everything and commits the character.
	 */
	void ApplyDodgeRecovery(float DurationSeconds);

	/** True while a parry is refused by a just-ended dodge. See State_DodgeRecovery. */
	UFUNCTION(BlueprintPure, Category="Combat|Dodge")
	bool IsInDodgeRecovery() const { return bInDodgeRecovery; }

	/** Ends the running GA_Parry. Shared by the catch path and by the whiff's recovery expiry. */
	void CancelParryAbility();

	/**
	 *  Hands movement back DelaySeconds from now, part-way through an attack that connected. The
	 *  on-hit waiver's movement half; the defensive half is instant, in the ability.
	 *
	 *  A release, never a lock -- it can only return control early, so a waiver firing against an
	 *  ability that has already ended is harmless.
	 */
	void BeginOnHitMovementWaiver(float DelaySeconds);

	/**
	 *  The knockback's receiving half: carry this character to a fixed world destination over a
	 *  duration, on the root-motion-source channel. The attacking ability computed the destination
	 *  and owns the never-inward clamp. A re-hit mid-slide replaces the running translation, last
	 *  hit wins. Server only; no-ops on the dead.
	 */
	void ReceiveKnockback(const FVector& DestinationWorld, float DurationSeconds, UCurveFloat* TimeMappingCurve,
		class UCurveVector* PathOffsetCurve = nullptr);

	/**
	 *  Which swing an attack activating now should be. Advances while the link window is open and a
	 *  successor exists, resets otherwise, and consumes the window either way -- it reopens when the
	 *  new swing ends, if that swing is chainable. Lives here because the string outlives any one
	 *  activation.
	 */
	int32 ResolveStringSwingIndexForActivation(int32 SwingCount);

	/** Opens the link window: a fresh attack press within it continues the string. */
	void OpenStringLinkWindow(float WindowSeconds);

	/** Kills the string and its window. The reason is for the trace alone. */
	void ResetString(const TCHAR* Reason);

	/** True while a chain press should outlive its ordinary buffer window; see TickInputBuffer. */
	bool HasStringLinkWindowOpen() const;

	/**
	 *  Starts the guard's minimum-duration commitment. Pushed from the ability rather than detected
	 *  as a rising edge on IsBlocking(), because a guard cancelled and resumed inside a frame has no
	 *  observable edge.
	 */
	void BeginBlockCommitment();

	/**
	 *  Charges BlockInitialStaminaCost, once the block ability has activated.
	 *
	 *  Not EffectOnStart, which is how the dodge pays: that route needs a GameplayEffect asset, and
	 *  a GameplayEffect's modifier attribute cannot be reliably configured through the toolset -- it
	 *  accepts the write, leaves the FProperty null, and the effect modifies nothing silently.
	 */
	void PayBlockInitialCost();

	/** True while a guard is inside its minimum duration and cannot be acted out of. */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	bool IsBlockCommitted() const;

	/**
	 *  True if this attack's origin lies inside the guard's forward arc -- the spec's 180-degree
	 *  coverage, asked in the defender's frame when the hit resolves. A hit from behind is not
	 *  blocked however long the button has been held.
	 *
	 *  Measured against facing rather than the camera: the body is what an attacker can see, and
	 *  defence has to be legible from outside. Same reason the damage wedge is actor-framed and aim
	 *  assist camera-framed.
	 */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	bool IsGuardFacing(const FVector& AttackOriginWorld) const;

	/**
	 *  Spends stamina from a blocked hit and breaks the guard if that empties the bar. Returns true
	 *  if it broke. This is the only thing that can break a guard: drain runs you to zero and leaves
	 *  you there, damage punishes you for being there.
	 *
	 *  The condition is post-damage stamina at zero, one rule covering both damage exceeding what is
	 *  left and damage landing on an already-empty bar. The second is why this cannot be driven from
	 *  the stamina-changed delegate: that fires only on a change, so a hit at exactly zero moves
	 *  nothing and is silently ignored.
	 *
	 *  Server only. The bar replicates; the tag is applied on both sides by the state pair.
	 */
	void ApplyStaminaDamage(float Amount);

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 *  Drops the guard and defers to the base. Every permission check lives in UTDJumpAbility; this
	 *  override exists only for the guard drop, which is presentation rather than permission.
	 */
	virtual void Jump() override;

	/**
	 *  The dead do not turn to face the camera, nor does a character mid-swing or on the floor.
	 *
	 *  ORed with Super, not replacing it: the base holds the attack lock, so returning only bDead
	 *  would silently discard it and attacks would drag an actor-frame hitbox around with them.
	 *
	 *  Knockdown is here because camera rotation while down is wanted -- the free camera is the
	 *  aiming instrument the block get-up reads from -- while the body spinning on its back is not.
	 *
	 *  Forced facing is unaffected and must be: TickForcedFacing writes rotation directly rather
	 *  than through the desired-rotation path, so the turn toward the attacker still runs.
	 *
	 *  Released at the stand boundary with the tag, symmetric with the movement lock.
	 */
	virtual bool IsFacingLocked() const override { return bDead || bKnockedDown || Super::IsFacingLocked(); }
	/**
	 *  Lockouts take movement, not just abilities.
	 *
	 *  ORed with Super, following IsFacingLocked above: the base holds the ability-side lock, so
	 *  returning only these four would let an attack's own commitment be walked out of.
	 *
	 *  Hitstun, the guard break, knockdown and the parry lockout -- not blockstun, which costs
	 *  initiative only and leaves the defender free to walk.
	 *
	 *  Knockdown covers the whole down state. Movement returns at the stand boundary and nowhere
	 *  earlier: the input window buys options, not steps, and a rise is committed once started.
	 *
	 *  Each of the four is read here rather than pushed into the ability-side lock, so a state and
	 *  an ability can hold movement at the same time without either clearing the other.
	 */
	virtual bool IsMovementLocked() const override { return bInHitstun || bGuardBroken || bKnockedDown || bInParryLockout || Super::IsMovementLocked(); }

	/**
	 *  Idle additionally means no ability running and no press waiting.
	 *
	 *  ANDed with Super, so movement input and being airborne still count as activity. Keyed on any
	 *  active ability rather than a tag list, so future states are covered without revisiting this.
	 *  A buffered press counts too.
	 */
	virtual bool IsIdle() const override;

	/** The actual launch, as opposed to Jump() which records the press. Starts the regen pause. */
	virtual void OnJumped_Implementation() override;

	/** Ends the jump's regen pause, leaving JumpRegenPauseSeconds of tail behind it. */
	virtual void Landed(const FHitResult& Hit) override;
	virtual void PossessedBy(AController* NewController) override;

	/**
	 *  A client's PlayerState arrives after its pawn, so the ASC is resolved again here. Without it
	 *  a client initialises against a null PlayerState, falls back to the owned ASC and has no
	 *  abilities -- while the server, which possesses before replicating, works perfectly.
	 */
	virtual void OnRep_PlayerState() override;

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Granted on spawn. Empty means this character cannot act (training dummy). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Abilities")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	/**
	 *  Applied on spawn and never removed. Stamina regen is not here: it is orchestrated in C++
	 *  below, because the economy is a small state machine and expressing it across effects needs
	 *  tag components that cannot be scripted in UE 5.8. Currently empty.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Abilities")
	TArray<TSubclassOf<UGameplayEffect>> DefaultEffects;

	/**
	 *  Which input action drives which input tag, e.g. IA_LightAttack -> InputTag.Attack. Matched by
	 *  tag rather than integer ID, so granting a new ability to a button is a content change.
	 *
	 *  Mapped actions must use a Down trigger, or no trigger -- never Pressed, which completes on
	 *  the frame after the press while the button is still held, losing the release edge.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Input")
	TMap<TObjectPtr<UInputAction>, FGameplayTag> AbilityInputActions;

	/**
	 *  How long a press nothing could answer keeps trying, once the button is back up.
	 *
	 *  A window on taps, not on intent: a held button is not stale, so this measures from the
	 *  release. That is what lets a heavy be buffered at all -- its boundary is beyond any window
	 *  this size, so a tier above light can only come from a hold that outlives the window.
	 *
	 *  Zero disables buffering, which is the honest way to A/B whether it helps.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Input", meta=(ClampMin="0.0"))
	float InputBufferSeconds = 0.1f;

	/**
	 *  Stamina regained per second while not exhausted, and while regen is running.
	 *
	 *  Driven here rather than by a periodic GameplayEffect because the economy is a small state
	 *  machine needing tag components that cannot be scripted. Attributes and tags are still GAS.
	 *
	 *  Also how fast a dodge becomes affordable again, so it sets the cadence of repeated evasion.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.0"))
	float StaminaRegenPerSecond = 40.0f;

	/**
	 *  Stamina regained per second while exhausted, so being run dry costs more than the bar it
	 *  emptied. A rate, not a duration: exhaustion ends when stamina reaches Max and nowhere else,
	 *  so the bar stays the single source of truth.
	 *
	 *  Zero is forbidden. Regen is the only thing that ends exhaustion, so a rate of zero is not "no
	 *  recovery" but permanent exhaustion. The clamp is load-bearing. See TickStaminaRegen.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.01"))
	float ExhaustedStaminaRegenPerSecond = 25.0f;

	/**
	 *  How long regen stays suppressed after the last action carrying StaminaRegenPausedTag ends.
	 *  Measured from the end, so it is a tax on acting rather than something a long action absorbs.
	 *  Shared by every such ability, attacks included.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.0"))
	float StaminaRegenPauseSeconds = 1.0f;

	/**
	 *  How long regen stays suppressed after landing from a jump. Jumping costs no stamina but is
	 *  not free -- height and airtime are paid in recovery. Shorter than
	 *  StaminaRegenPauseSeconds because a jump is a weaker commitment.
	 *
	 *  Keyed to the jump action, never to being airborne: walking off a ledge costs nothing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.0"))
	float JumpRegenPauseSeconds = 0.5f;

	// ---- Knockdown ------------------------------------------------------------------------
	//
	// Both types total 2.5 s and begin their forced rise at 2.0. The type decides the lockout/choice
	// split; the total is invariant, because everything derived from it would otherwise need
	// re-deriving per type. Each split is its own dial; the total is not.

	/**
	 *  Normal type: seconds of lockout, in which every action is refused and presses buffer. Held to
	 *  the minimum a knockdown can be and still read as one.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.0"))
	float KnockdownLockoutSecondsNormal = 1.0f;

	/**
	 *  Normal type: seconds of input window. Doubled against the lockout -- the fast layer's
	 *  knockdowns are escape-rich, and boundary oki against them is light-only.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.0"))
	float KnockdownInputWindowSecondsNormal = 1.0f;

	/**
	 *  Hard type: seconds of lockout. The meaner half of the same total -- 1.5 against normal's 1.0
	 *  holds every exit back far enough that a committed follow-up's arrival overlaps the forced
	 *  rise, which is the whole of hard oki.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.0"))
	float KnockdownLockoutSecondsHard = 1.5f;

	/** Hard type: seconds of input window. Narrow by design; see KnockdownLockoutSecondsHard. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.0"))
	float KnockdownInputWindowSecondsHard = 0.5f;

	/**
	 *  Seconds a rise takes, shared by both types and every way of starting one. The clips are
	 *  rate-fitted to it, and keeping it type-invariant is what keeps both types standing at 2.5.
	 *  A rise is committed once started, so this is also the width of the meaty window a chosen
	 *  stand is choosing when to open.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.01"))
	float KnockdownRiseSeconds = 0.5f;

	/**
	 *  How far a knocked-down body is carried, along the attacker->victim bearing.
	 *
	 *  Radial, not along the attacker's facing, and the two axes are never unified: the string's
	 *  knockback centres on facing because the next hit needs its target in front, while a knockdown
	 *  radiates so a side target flies to its own side and a crowd scatters.
	 *
	 *  Farther than the knockback, so re-engaging the riser costs real travel. Coupled to
	 *  MaxReachCm: below a branch's reach, a standing attacker could touch the floor.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.0"))
	float KnockdownSpacingCm = 450.0f;

	/**
	 *  Seconds the fall takes -- clip and radial carry alike, both fitted to this, the carry
	 *  through knockback's own root motion source.
	 *
	 *  **Bounded above by KnockdownLockoutSecondsNormal**, or a get-up begins while the body is
	 *  still sliding. The value is shared across both types, so the hard type's longer lockout
	 *  does not raise the ceiling -- the shorter one binds. What must fit is the *montage*,
	 *  which outlasts this whenever KnockdownFallClipSeconds is shorter than the clip.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.01"))
	float KnockdownFallSeconds = 0.5f;

	/**
	 *  Where the fall clip has landed, in seconds from its start. The span fitted to
	 *  KnockdownFallSeconds; the remainder plays on afterwards at the same rate.
	 *
	 *  **Measured, not eyeballed** -- the pelvis stops descending at 0.80 of AM_Knockdown's
	 *  0.900, the last 0.10 giving up 1.6 cm. Fitting the whole clip instead slides the body
	 *  for that tail after it has visibly landed. Zero uses the whole clip.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.0"))
	float KnockdownFallClipSeconds = 0.8f;

	/**
	 *  Where the fall clip's reaction commits, in seconds from its start. Everything before is
	 *  skipped: the montage begins here and the window fitted is this to KnockdownFallClipSeconds.
	 *
	 *  **Measured, not eyeballed.** The clip is a death animation authored from a neutral stance,
	 *  so it gathers before it falls -- the pelvis dips, rises back 4 cm across four frames, and
	 *  only then commits at 0.35. Played from zero that reads as reacting late to the hit.
	 *
	 *  **Zero, which starts at the beginning, and that is deliberate.** Skipping the gather
	 *  shortens the fitted window, which lowers the rate, which stretches the clip's flat tail --
	 *  one artifact traded for another, because a constant rate makes them the same knob. The
	 *  measured 0.35 is kept here for whoever builds the time curve; see the Polish brief.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.0"))
	float KnockdownFallClipStartSeconds = 0.0f;

	/**
	 *  Optional pacing for the carry. **A time-mapping curve, not a strength curve**: it maps
	 *  normalised time to normalised progress and must run monotonically 0 to 1, the endpoint
	 *  staying pinned whatever it does. Null is linear.
	 *
	 *  Paces the capsule alone. AM_Knockdown's own TimeStretchCurve paces the animation, and the
	 *  two are separate assets under separate contracts.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown")
	TObjectPtr<UCurveFloat> KnockdownFallTimeMappingCurve;

	/**
	 *  Optional arc for the carry, in cm, offsetting the straight path. Z is world up; X and Y
	 *  offset along and across the travel. Sampled at the same fraction as the time mapping curve,
	 *  after it, so the two share one time base and an arc cannot drift from the slide.
	 *
	 *  **Setting one hands the carry's Z to the root motion source**: IgnoreZAccumulate is dropped
	 *  for this displacement, so the body follows the authored arc rather than gravity, and a
	 *  victim floored while already airborne is carried from where they are. Null keeps gravity.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown")
	TObjectPtr<UCurveVector> KnockdownFallPathOffsetCurve;

	/**
	 *  How long the carry runs past the fall, in seconds. The body is still laying itself down
	 *  after the fitted window ends, and the carry used to stop dead at that boundary; this is the
	 *  span the skid decays across.
	 *
	 *  Purely cosmetic: the victim is invincible throughout and the destination is unchanged, so
	 *  this moves only when the authored spacing is *reached*, never where. Zero restores the
	 *  carry ending with the fall. **The pacing and arc curves are normalised over fall plus this**,
	 *  so changing it without re-deriving them re-times both.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="0.0"))
	float KnockdownCarrySettleSeconds = 0.16f;

	/**
	 *  Degrees per second the body turns to face its attacker after any clean hit.
	 *
	 *  Derived, not free: 180 degrees must complete well inside the shortest hitstun anyone can
	 *  feel, or a victim is still turning when they regain control. The floor is 180 / that hitstun,
	 *  and the `HitstunSeconds` to derive against is the shortest *felt* one -- knockdown repurposed
	 *  the heavy's and charged's into attacker-side oki knobs nobody feels.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown", meta=(ClampMin="1.0"))
	float ForcedFacingTurnRateDegrees = 720.0f;

	/**
	 *  How much of the flinch clip the hitstun tell uses, in seconds from its start.
	 *
	 *  The clip runs 1.333 s and does not settle -- it staggers throughout and plants at 0.718 --
	 *  so the tell takes the span before that plant and the rest goes unplayed. Fitting the whole
	 *  clip instead would run the common 0.55 s hitstun at 2.4x.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stun", meta=(ClampMin="0.01"))
	float HitstunTellPortionSeconds = 0.684f;

	/**
	 *  Blockstun's equivalent. Its clip settles at 0.485 s before rising back into the guard, so
	 *  the tell ends on that trough and the return-to-guard tail goes unplayed.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.01"))
	float BlockstunTellPortionSeconds = 0.485f;

	/** The parry lockout's equivalent -- hitstun's clip, so hitstun's seam and hitstun's span. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Parry", meta=(ClampMin="0.01"))
	float ParryLockoutTellPortionSeconds = 0.684f;

	/**
	 *  Played on entering the down state. Rate derived as `length / KnockdownFallSeconds`.
	 *
	 *  Authored with `bEnableAutoBlendOut` false, which lets the last frame hold as the ground pose
	 *  for the lockout and input window. A montage that blends itself out leaves the body standing in
	 *  idle while the state machine still has it on the floor.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown")
	TObjectPtr<UAnimMontage> KnockdownMontage;

	/**
	 *  Played when a normal-type rise begins. Rate derived as `length / KnockdownRiseSeconds`.
	 *  Covers the auto-rise, the neutral stand and the block get-up; the dodge brings its own, see
	 *  UTDGameplayAbility::BringsOwnRiseMontage.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown")
	TObjectPtr<UAnimMontage> RiseMontage;

	/** The hard type's rise. Same derivation, same span -- a different clip, not a different rule. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockdown")
	TObjectPtr<UAnimMontage> RiseHardMontage;
	/**
	 *  Present while an action that suppresses regen is running, via that ability's owned tags.
	 *  Regen watches for the tag rather than each ability telling it to stop, so a cancelled ability
	 *  cannot leave regen suppressed forever. The assets are authoritative for who suppresses; no
	 *  C++ names them, which makes adding a fourth a content change.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina")
	FGameplayTag StaminaRegenPausedTag;

	/**
	 *  Applied when stamina reaches zero, and what defensive abilities block on.
	 *
	 *  Cleared when stamina reaches Max, not on a timer: the punishment scales with how empty you
	 *  ran yourself, and no second number can disagree with the bar. Regen continues while exhausted
	 *  -- it is a lockout on acting, not on recovering, and regen is the only thing that ends it.
	 *
	 *  The regen pause still suppresses it, so holding an action at zero suppresses recovery for as
	 *  long as it is held. That is a choice with an obvious exit rather than a trap: releasing is
	 *  always available, and a guard held at zero accomplishes nothing anyway.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina")
	FGameplayTag ExhaustedTag;

	/**
	 *  Present while a guard is held, via GA_Block's owned tags. Drives the drain below. Read from
	 *  the character rather than the ability draining itself, because every other stamina rule lives
	 *  here so they cannot disagree.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block")
	FGameplayTag BlockingTag;

	/**
	 *  Stamina spent per second while a guard is held. Drain, not damage.
	 *
	 *  Self-inflicted: it runs the bar to zero and parks there harmlessly. What it buys the attacker
	 *  is that the defender stops being able to absorb anything -- a guard at zero breaks to the
	 *  next blocked hit. So it is not a countdown on how long you may block; it is how fast holding
	 *  a guard converts into risk.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.0"))
	float BlockDrainPerSecond = 10.0f;

	/**
	 *  How long a broken guard is stunned, and how long regen stays suppressed across it.
	 *
	 *  One number for both: the stun and the suppression are the same event from two sides, and
	 *  authoring them apart allows regen resuming while you are still stunned for it.
	 *
	 *  StaminaRegenPauseSeconds runs after this rather than instead, because the regen tick takes
	 *  the max of every live suppressor. Bounded, which keeps it a cost rather than a deadlock.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.0"))
	float GuardBreakStunSeconds = 1.0f;

	/**
	 *  Ground speed cap while a guard is up. Authored, not derived -- a braced shuffle.
	 *
	 *  Recorded as a relationship to MaxWalkSpeed but not implemented as one: changing MaxWalkSpeed
	 *  will not move it and should prompt a second look here. Restored from the value captured at
	 *  BeginPlay, so a Blueprint authoring a different MaxWalkSpeed is not overwritten.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.0"))
	float BlockingMaxWalkSpeed = 125.0f;

	/**
	 *  Ground speed cap while exhausted. Authored, and recorded as a relationship to MaxWalkSpeed
	 *  rather than implemented as one, exactly as the guard's cap is. Being run dry should read in
	 *  the body before the player checks a bar.
	 *
	 *  Combines with the guard's cap by taking the slower, which is reachable: raising a guard you
	 *  cannot afford exhausts you with the guard still up. Both are penalties, so the minimum is the
	 *  only combination that cannot be gamed by entering states in the right order.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.0"))
	float ExhaustedMaxWalkSpeed = 400.0f;

	/**
	 *  How long a raised guard is committed for. Locking: nothing but movement is allowed.
	 *
	 *  Without a floor the guard is featherable at input speed. It must gate the attack to do
	 *  anything -- narrowing "whichever comes last wins" for this window only, where the guard has
	 *  already won.
	 *
	 *  Releasing inside the window schedules the drop for its end rather than lowering the guard
	 *  early. Responsiveness comes from the input buffer: an attack pressed inside is refused,
	 *  buffered, and fires the instant it expires.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.0"))
	float MinimumBlockSeconds = 0.25f;

	/**
	 *  Stamina taken once as a guard goes up. Zero by default, so raising costs nothing.
	 *
	 *  Paid, never required, like every cost here: raising a guard at 4 stamina with a cost of 5
	 *  works, empties the bar and exhausts you. The guard still does everything it would have,
	 *  including cancelling an attack -- GAS applies cancel tags during activation, before this is
	 *  charged.
	 *
	 *  Not a guard break: nothing is broken and there is no stun. Breaking is the sole preserve of
	 *  stamina damage.
	 *
	 *  Charged on every activation including a resume -- a resume is an intended block, and all
	 *  blocks are created equal. With a non-zero value, holding a guard through a swing pays twice,
	 *  because it is two guards.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.0"))
	float BlockInitialStaminaCost = 0.0f;

	/**
	 *  Stamina paid to the parrier when a parry lands -- the one authored half of the reward, and a
	 *  flat refund that makes a correct read pay rather than merely cost nothing. Per parry, not per
	 *  tier.
	 *
	 *  Lives here because the stamina economy is orchestrated in one place. Clamped by the attribute
	 *  set, so a reward at full stamina is silently free rather than an overflow.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Parry", meta=(ClampMin="0.0"))
	float ParryStaminaReward = 25.0f;

	/**
	 *  Parry Grace: how long a successful parry keeps protecting you after it lands.
	 *
	 *  Solves 1vX. A catch closes the window, so one press answers exactly one attack -- unanswerable
	 *  when two arrive together, because no human presses twice that fast. Grace waives the second
	 *  press for simultaneous hits only, simultaneous being this value: one parry per incoming
	 *  attack, unless the attacks are simultaneous.
	 *
	 *  Derived, not chosen: 150 ms is the interval most humans cannot beat. Re-derive against that,
	 *  never against feel.
	 *
	 *  Three properties it deliberately lacks:
	 *  - It does not re-arm. Only a catch by an open window starts a tail; a catch by Grace itself
	 *    pays the full reward and starts nothing.
	 *  - It gates no input, including a fresh parry. It aids, never restricts.
	 *  - It refuses nothing. A successful parry frees you instantly and Grace does not take that back.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Parry", meta=(ClampMin="0.0"))
	float ParryGraceSeconds = 0.15f;

	/**
	 *  Collapse into a ragdoll on death -- the cheapest unambiguous read, needing no animation
	 *  content.
	 *
	 *  Requires a physics asset on the mesh or physics silently does nothing. Ours has one
	 *  (`PA_Mannequin`, on the bundle's `SKM_Manny`); a mesh without one dies standing up.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Death")
	bool bRagdollOnDeath = true;

	/**
	 *  Impulse magnitude given to the ragdoll at death, along the bearing from killer to victim.
	 *  A true impulse rather than a velocity change, so it divides by the body mass. Measured on
	 *  Manny, as the distance the corpse settles from where it fell: 12000 about 84 cm, 24000 about
	 *  271, 30000 about 397, 36000 about 480. Passing it as a velocity change instead reads as cm/s
	 *  directly and fires the corpse 180 m out of the level.
	 *  A killing blow otherwise imparts nothing at all -- knockback sits on the hitstun branch and
	 *  EnterKnockdown returns early once bDead is set, so the corpse drops straight down where it
	 *  stood.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Death", meta=(ClampMin="0.0"))
	float DeathImpulseStrength = 30000.0f;

	/** Fraction of the death impulse aimed upward, which is what stops the body sliding flat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Death", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DeathImpulseLift = 0.35f;

	/**
	 *  Written by the server before bDead replicates, and applied by every machine to its own
	 *  ragdoll. Replicated rather than recomputed because the killer may already be gone by the
	 *  time a late joiner resolves the corpse.
	 */
	UPROPERTY(Replicated)
	FVector_NetQuantize10 DeathImpulse = FVector::ZeroVector;

	/**
	 *  Debug only: seconds after death before reviving at full. 0 disables it. Death is deliberately
	 *  the minimum -- a state that stops the character acting, so the health bar means something and
	 *  damage observations after a kill are not garbage.
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
	 *  How long the auto-attack holds the button, which selects the tier. The thresholds live on
	 *  GA_Attack's Branches: 0.1 for light, 0.3 for heavy, 0.8 for charged.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.0"))
	float DebugAutoAttackHoldSeconds = 0.1f;

	/**
	 *  Snap the auto-attacker back to its spawn transform. Debug only.
	 *
	 *  Attack montages carry root motion, so an attacker on a loop walks across the level. Preferred
	 *  over zeroing AnimRootMotionTranslationScale, which would shorten the dummy's reach -- and
	 *  reach is what spacing tests measure.
	 *
	 *  Fires when the swing ends, and again before each swing: normally a no-op, but covering an
	 *  ability that was cancelled and never ended cleanly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugAutoAttackResetPosition = true;

	/**
	 *  Extra delay after the attack ends before the reset fires. The ability ends when its montage
	 *  blends out, noticeably earlier than the swing looks finished, so resetting on that edge alone
	 *  snaps the attacker home mid-follow-through.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.0"))
	float DebugAutoAttackResetDelaySeconds = 0.35f;

	/**
	 *  How many attack taps one cycle throws, at string cadence. Debug only.
	 *
	 *  1 is the pre-string behaviour exactly, which keeps every existing scenario's fixture
	 *  untouched. Above 1, each tap lands during the previous swing, exercising the buffer extension
	 *  and the chain-out the way a mashing human does. The home reset waits for the burst; a
	 *  teleport mid-string would sever the spacing chain s4 measures.
	 *
	 *  The whole burst must fit inside DebugAutoAttackInterval, as the single attack must.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="1"))
	int32 DebugAutoAttackStringTaps = 1;

	/** Seconds between the burst's taps. 0.25 lands each press mid-previous-swing. Debug only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.05"))
	float DebugAutoAttackStringTapIntervalSeconds = 0.25f;

	/**
	 *  Whether this auto-attacker turns to face its target, and when. Debug only.
	 *
	 *  The turn needs nothing but a focus. A possessed pawn with no focus has AAIController copying
	 *  its control rotation from the pawn every tick (bSetControlRotationFromPawnOrientation, on by
	 *  default), so the rotation error is permanently zero and every turn rate multiplies nothing. A
	 *  focus points the control rotation at the target and CharacterMovementComponent closes the gap
	 *  at RotationRate.Yaw.
	 *
	 *  Parity is then inherited: IsIdle() is false while any ability is active, so a swinging dummy
	 *  turns at TurnRateDegrees exactly as a swinging player does. CoilTurnRateDegrees is the one
	 *  rate it cannot reach while the hold selects the light.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	ETDDebugFacingMode DebugAutoAttackFacingMode = ETDDebugFacingMode::Never;

	/**
	 *  Take the next target each swing instead of always the nearest. Default false, so existing
	 *  scenarios are untouched.
	 *
	 *  Nearest-target selection makes the attacker chase: it lunges after the victim its own
	 *  knockback pushed away, so the pair travel together and a third body is left behind --
	 *  harmless in a single-defender fixture, fatal in a multi-target one.
	 *
	 *  Rotation advances per attack, not per burst: a chained string is three attacks, so a fixture
	 *  re-targeting only at burst start still chases one victim through all three.
	 *
	 *  Candidates are ordered by name, so the cycle is identical across runs. Ordering by distance
	 *  would reshuffle as knockback moves bodies about.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugAutoAttackRotateTargets = false;

	/**
	 *  Teleport home after every attack rather than only at a burst's end. Default false, and
	 *  load-bearing: `s4-string` measures the spacing chain a connecting string produces, and a
	 *  teleport between attacks would sever it.
	 *
	 *  What it buys is a stationary attacker, the whole requirement for testing a 360-degree volume.
	 *  An attacker whiffing into empty space has an open standoff gate and travels its full lunge,
	 *  so without this it leaves both targets behind after one attack.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugAutoAttackHomeBetweenAttacks = false;

	/**
	 *  On landing a clean hit, immediately press dodge. Debug only, off by default.
	 *
	 *  The on-hit waiver's only unattended witness: the waiver frees defensive actions the instant
	 *  an attack connects, and nothing else in the fixture set asks an attacker to defend. Produces
	 *  a DODGE line between the attacker's own DAMAGED and the end of its recovery.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugDodgeAfterHit = false;

	/**
	 *  Press block a fixed delay after each auto-attack press, cancelling the swing. Debug only.
	 *
	 *  The unattended witness for the pre-commit cancel, which nothing else can produce: the
	 *  auto-attacker only attacks and the auto-defender never attacks. It matters more since the
	 *  on-hit waiver loosened when defensive actions are permitted, so the other boundary -- a
	 *  committed swing still cannot be cancelled -- wants a standing assertion.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugCancelAttackIntoBlock = false;

	/**
	 *  Delay from the auto-attack press to the cancelling block press. Default 0.10, inside the
	 *  light's 0.15 commit boundary with 50 ms to spare -- the cancel must land before
	 *  State.Attacking.Committed or the scenario measures a refusal. Raise it above the boundary
	 *  deliberately to assert the opposite.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.0"))
	float DebugCancelAfterPressSeconds = 0.10f;

	/**
	 *  Suppress every lunge this character would start -- base, per-attack and the dodge's.
	 *  Fixture-only, off by default, checked in UTDGameplayAbility::StartLunge because that is the
	 *  single function they route through. Logs `LUNGE SKIP` so it is never silently on.
	 *
	 *  This is what a stationary attacker requires, and bDebugAutoAttackHomeBetweenAttacks is not a
	 *  substitute: the lunge and the release window both happen inside one attack, so re-homing
	 *  afterwards leaves the hitbox having gone live wherever the travel ended.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugSuppressLunge = false;

	/**
	 *  Debug only: defend on a loop, so the defensive economy can be watched without a human.
	 *
	 *  The mirror of bDebugAutoAttack, and pairing them is two actors' job: an attacker that also
	 *  defends is two fixtures interfering -- a guard cancels an attack's startup and the attack's
	 *  tags refuse the guard.
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
	 *  Orthogonal to DebugAutoDefendMode rather than a member of it: the rule it observes -- a
	 *  broken guard can no longer jump -- needs a defender that is blocking (to get broken) and
	 *  jumping (to be refused) at once, which a one-mode enum cannot express.
	 *
	 *  Reused by knockdown's neutral stand, which is also the jump input.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugPeriodicJump = false;

	/**
	 *  Seconds between one auto-jump and the next. Co-prime with the attacker's cycle, for the
	 *  reason the parry's interval is: a dividing period samples one phase forever, and the question
	 *  is whether presses land inside a lockout -- which only some will.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.1"))
	float DebugJumpIntervalSeconds = 1.3f;

	/** Input the periodic jump taps, normally InputTag.Jump. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	FGameplayTag DebugJumpInputTag;

	/** The get-up option this pawn presses when knocked down. Debug only; see ETDDebugGetUpMode. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	ETDDebugGetUpMode DebugGetUpMode = ETDDebugGetUpMode::Wait;

	/** Seconds after the lockout ends before DebugGetUpMode presses. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.0"))
	float DebugGetUpDelaySeconds = 0.05f;

	/** Home to the placed transform at the stand boundary even with no other debug fixture armed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugHomeAtStand = true;

	/**
	 *  Seconds between one auto-parry and the next. Debug only.
	 *
	 *  Must not alias against DebugAutoAttackInterval, or the window meets every swing at the same
	 *  phase and the run answers one question repeatedly while looking thorough. A co-prime period
	 *  sweeps, producing both successes and whiffs from one session.
	 *
	 *  It also has to clear the recovery, which the dodger's interval does not: a whiffed parry
	 *  refuses defensive activations for ParryWhiffRecoverySeconds, so anything shorter than window
	 *  + recovery spends the run measuring the fixture rather than the mechanic.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.1"))
	float DebugParryIntervalSeconds = 1.7f;

	/**
	 *  Hold a guard this long before each auto-parry, to spend stamina first. 0 disables it.
	 *
	 *  Makes the parry's reward observable at all: a parry costs nothing, so an unattended parrier
	 *  never spends, its bar never leaves 100, and the clamp eats the whole reward -- every credited
	 *  sample reads `gained=0.0`. Blocking spends stamina and, unlike the dodge, authors no
	 *  displacement, so it drains the parrier without moving it out of the exchange.
	 *
	 *  Do not raise it so far that the guard breaks: a break refuses every ability, the parry
	 *  included, and the fixture would measure its own guard.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.0"))
	float DebugParryPreBlockSeconds = 0.0f;

	/**
	 *  Seconds between one auto-dodge and the next. Co-prime with the attacker's interval rather
	 *  than a round number: a period that divides or multiplies it meets every swing at the same
	 *  phase and answers one question forever, while a co-prime one sweeps across windup, release
	 *  and recovery without anyone scripting it. See Docs/Working-In-Unreal.md on periodic samplers.
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
	 *  For per-activation detail not worth a gameplay tag -- which way a dodge resolved, which parry
	 *  window is open. Set it when an ability starts and clear it when the ability ends, or it will
	 *  outlive what it describes.
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
	 *  Homing runs before the commit checkpoint and stops at it, which is what stops it being the
	 *  homing this design rejected: tracking after commit would mean a defender's movement could no
	 *  longer make an attack whiff, while tracking before it costs nothing.
	 *
	 *  What it buys is that the correction arrives gradually. Without it the whole turn lands in one
	 *  frame at commit, which reads as a pop once the wedge is wide enough to matter.
	 *
	 *  Set at activation with the first branch's wedge, since every tier shares the windup. Cleared
	 *  at commit and again in EndAbility, the one place every exit converges -- a stranded homing
	 *  state is a character that turns toward strangers forever.
	 */
	void SetAimAssistHoming(const FTDAttackHitbox& InWedge, const FGameplayTagContainer& InImmunityTags, bool bActive, bool bInDrawDebug);

	/**
	 *  The best Target Lock candidate for a wedge, or null. Shared by homing and the commit snap --
	 *  two implementations would drift, and the one that drifted would decide where attacks point.
	 *
	 *  Selection is smallest bearing, ties broken by distance so two machines cannot order equal
	 *  candidates differently.
	 *
	 *  @param AimYawDegrees  The frame to measure in -- the camera's, not the body's. See
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
	 *  Held props, on the SwordShield pack's own `Sword` / `Shield` sockets.
	 *
	 *  Those sockets carry the grip rotation plus a non-uniform scale correcting for meshes authored
	 *  several times too large, so both props are correct at identity and no offset should be
	 *  authored here. They exist only on the bundle's `SKM_Manny`, not Epic's `SKM_Manny_Simple`.
	 *
	 *  Not the `weapon_r` / `weapon_l` bones, which fail twice: absent from Epic's mesh, and animated
	 *  only by GDH clips, so under any Epic animation a prop parented there freezes at reference pose.
	 *
	 *  Cosmetic: collision is off and the melee trace is a separate ability task, so the sword's mesh
	 *  never decides what the sword hits. Left empty on the CDO, which keeps the dummy unarmed.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Equipment")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Equipment")
	TObjectPtr<UStaticMeshComponent> ShieldMesh;

	/**
	 *  The ASC actually in use: the PlayerState's, or OwnedAbilitySystemComponent. Resolved rather
	 *  than assumed, because only players have a PlayerState. Null until InitialiseAbilitySystem has
	 *  run -- later than you expect on a client -- so every use is null-checked.
	 *
	 *  Never reach past this to OwnedAbilitySystemComponent. For a player that subobject exists, is
	 *  registered, and holds nothing, so using it compiles, runs, and silently reads an empty ASC.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UAbilitySystemComponent> AbilitySystem;

	/** Attributes on whichever ASC was resolved. Null until InitialiseAbilitySystem has run. */
	UPROPERTY(Transient)
	TObjectPtr<UTDAttributeSet> AttributeSet;

	/**
	 *  The fallback ASC, for characters with no PlayerState -- the training dummy. A player carries
	 *  it too and never uses it: the accepted cost of one class serving both cases. It seeds nothing
	 *  and grants nothing, so it costs registration rather than bandwidth.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UAbilitySystemComponent> OwnedAbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UTDAttributeSet> OwnedAttributeSet;

private:

	/**
	 *  Points AbilitySystem at the right ASC, binds actor info, seeds defaults once.
	 *
	 *  Safe and expected to call repeatedly: from BeginPlay, every possession, and OnRep_PlayerState.
	 *  Actor info is rebound every time because possession changes who owns the ASC; seeding is
	 *  guarded per-ASC, not per-call and not per-character.
	 */
	void InitialiseAbilitySystem();

	/**
	 *  Which ASC this character should use, and which actor owns it -- the PlayerState's when there
	 *  is one, the owned component otherwise.
	 *
	 *  OutOwner is what InitAbilityActorInfo takes as owner, which is not the avatar: GAS wants the
	 *  PlayerState as owner and the pawn as avatar, so ability state survives a pawn while traces,
	 *  sockets and montages resolve against the body.
	 *
	 *  Never reach past `AbilitySystem` to the owned fallback. Which one is correct is a question
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
	 *  Presses the input at every matching spec and starts whatever is not already running. Returns
	 *  whether a *new* activation happened, not whether anything is live -- a press arriving at an
	 *  already-running ability is the most important thing to buffer.
	 *
	 *  bForwardToActive is false on a buffered retry: the button was pressed once, and telling a
	 *  running ability it was re-pressed every frame is a lie WaitInputRelease would read.
	 */
	bool TryActivateAbilitiesForInput(const FGameplayTag& InputTag, bool bForwardToActive);

	/** Forwards the release edge to every matching spec. */
	void ReleaseAbilitiesForInput(const FGameplayTag& InputTag);

	/** True if any ability answering this input wants its refusal remembered. */
	bool ShouldBufferInput(const FGameplayTag& InputTag) const;

	/** Retries the buffered press, or drops it once its window has passed. */
	void TickInputBuffer();

	/**
	 *  Whether the buffered press outlives its ordinary window: an ability answering it that opted
	 *  into extension is running now, or its string link window is open. The policy is the
	 *  ability's -- see UTDGameplayAbility::ShouldExtendBufferWhileActive.
	 */
	bool ShouldExtendBufferedPress(const FGameplayTag& InputTag) const;

	/** Offers the active ability answering this input the chain-out; true if it ended for it. */
	bool TryChainOutActiveAbility(const FGameplayTag& InputTag);

	/**
	 *  Replays a buffered release, HoldSeconds after the buffered press activated. Cancelled the
	 *  moment real input for that tag arrives, because a live edge beats a recorded one -- otherwise
	 *  a replay for a hold since abandoned lands on whatever is running by then.
	 */
	void ReplayBufferedRelease(FGameplayTag InputTag);

	/** Presses the debug attack input, then releases it DebugAutoAttackHoldSeconds later. */
	void DebugAutoAttackPress();
	void DebugAutoAttackRelease();

	/**
	 *  Presses the debug dodge input, then releases it a tap later. HoldBlock has no equivalent pair:
	 *  it presses once in BeginPlay and never releases, and the resume tick does the rest.
	 */
	void DebugAutoDodgePress();
	void DebugAutoDodgeRelease();
	/** Taps the jump input, then releases it a frame later. See bDebugPeriodicJump. */
	void DebugAutoJumpPress();
	void DebugAutoJumpRelease();

	/** Presses the input DebugGetUpMode names, then releases it on DebugGetUpReleaseTimerHandle. */
	void DebugGetUpPress();
	void DebugGetUpRelease();

	/**
	 *  One PeriodicParry cycle. Raises a guard first when DebugParryPreBlockSeconds is set.
	 *
	 *  Three steps rather than two because the guard must be down before the parry is pressed --
	 *  GA_Parry refuses activation while State.Blocking is present, so releasing and pressing in one
	 *  frame produces a refusal every cycle and an assertion that never fires.
	 */
	void DebugAutoParryCycle();
	void DebugAutoParryDropGuard();

	/**
	 *  PeriodicParry's tap. Unlike the dodge's, it does not re-home the pawn first: a parrier stands
	 *  its ground, and a teleport between attempts would sever the spacing the parry resolves at.
	 */
	void DebugAutoParryPress();
	void DebugAutoParryRelease();

	/**
	 *  Warn when this placed actor's values have drifted from the class they came from.
	 *
	 *  The failure it catches is completely silent: a placed actor serialises the values its
	 *  Blueprint had when placed and keeps them as per-instance overrides forever, so a
	 *  later-authored ability, input tag or knob never reaches it and nothing says so.
	 *
	 *  A warning at BeginPlay rather than a fix -- the values cannot be repaired from here, since
	 *  EditDefaultsOnly properties reject writes on an instance, and a silent repair would hide the
	 *  divergence. Ungated on LogTDCombatTiming.
	 */
	void WarnOnStaleInstanceOverrides() const;

	/**
	 *  The cancelling block press, scheduled by bDebugCancelAttackIntoBlock. Released rather than
	 *  held, unlike HoldBlock: a guard left up fires BLOCK cost once for the session instead of once
	 *  per cancelled swing, and the per-swing count is the assertion. Released later than
	 *  MinimumBlockSeconds so the guard clears its own commitment rather than fighting the floor.
	 */
	void DebugCancelIntoBlockPress();
	void DebugCancelIntoBlockRelease();

	/** Teleports back to DebugAutoAttackHomeTransform and kills leftover velocity. No-op if disabled. */
	void ReturnToDebugAutoAttackHome();

	/**
	 *  Points the AI controller's focus at the nearest other pawn, or clears it. Re-resolved on every
	 *  call rather than cached in BeginPlay, because a placed dummy is possessed before the player
	 *  pawn exists -- a focus taken at BeginPlay would be null forever.
	 */
	void UpdateDebugFacingFocus(bool bAttacking);

	/** Bound to the ASC so the attacker returns home the moment a swing finishes. */
	void HandleDebugAutoAttackEnded(const FAbilityEndedData& EndedData);

	/** Adds StaminaRegenPerSecond * delta, unless suppressed or already full. */
	void TickStaminaRegen(float DeltaSeconds);

	/** Spends BlockDrainPerSecond * delta while BlockingTag is present. Never breaks a guard. */
	void TickBlockDrain(float DeltaSeconds);

	/**
	 *  Ends any running block. Used by the guard break and by becoming airborne. One helper because
	 *  a guard has three ways to end that are not the player letting go, and each must stop the
	 *  drain -- which is free, since the drain reads BlockingTag and the tag leaves with the ability.
	 */
	void CancelBlockAbility();

	/**
	 *  Applies whichever authored speed caps are live, and the captured default when none are. Takes
	 *  the slowest applicable cap rather than the last checked, because blocking and exhaustion can
	 *  overlap.
	 */
	void TickMoveSpeedClamps();

	/**
	 *  Maintains State.Blocking.Committed, and finishes a release held back by it.
	 *
	 *  Written as "make the tag match the current state" rather than as edges, for the reason the
	 *  speed cap is: an edge-driven version must be right on every entry and exit, and a stranded
	 *  commit tag refuses every action forever with nothing on screen to explain it.
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
	 *  Enter/Exit decide whether the state changed and run on the server alone, because the delegates
	 *  driving them are bound behind the authority gate. Apply/Clear make the state true on this
	 *  machine -- tag, ragdoll, movement stop -- on the server directly and on clients from OnRep.
	 *
	 *  A loose gameplay tag does not replicate, so without the split a client's ASC never has the
	 *  tag: its CanActivateAbility would pass, it would predict an action the server already
	 *  refused, and only a correction would tell it otherwise.
	 */
	void ApplyExhaustionState();
	void ClearExhaustionState();
	void ApplyDeathState();
	void ClearDeathState();

	/**
	 *  Starts and ends the guard-break stun. Server decides; the state pair runs everywhere.
	 *  EndGuardBreak is driven from Tick against GuardBreakEndsAt rather than a timer, so the stun
	 *  cannot outlive a pause, a seek or a slow frame unobserved.
	 */
	void EnterGuardBreak();
	void EndGuardBreak();
	void ApplyGuardBreakState();
	void ClearGuardBreakState();

	/**
	 *  Ends the blockstun lockout. Driven from Tick against BlockstunEndsAt, as the guard break is.
	 *  No Apply pairing beyond the tag, because blockstun cancels nothing -- it refuses activations
	 *  through ActivationBlockedTags and lets whatever is running finish. That is the difference from
	 *  a break, which takes the guard down with it.
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

	/** Advances the lockout -> choice -> rise -> stand boundaries. Authority only, called from Tick. */
	void TickKnockdown();

	/** Turns the body toward ForcedFacingTarget at the derived rate. Called from Tick. */
	void TickForcedFacing(float DeltaSeconds);

	/** The radial carry: attacker + (attacker->victim bearing) * KnockdownSpacingCm, Z natural. */
	void ApplyKnockdownFall(AActor* Attacker);

	/** Plays a knockdown montage at a rate derived to fit TargetSeconds. The fall and both rises. */
	void PlayKnockdownMontage(UAnimMontage* Montage, float TargetSeconds, const TCHAR* Label,
		float ClipPortionSeconds = 0.0f, float ClipStartSeconds = 0.0f);

	/** Lockout seconds for the type currently held. */
	float GetKnockdownLockoutSeconds() const;

	/** Choice-window seconds for the type currently held. */
	float GetKnockdownInputWindowSeconds() const;

	/**
	 *  Ends hitstun. Driven from Tick against HitstunEndsAt, as its siblings are. The Apply half
	 *  carries no cancel -- EnterHitstun cancels on the server once, and a client's OnRep must not
	 *  cancel predicted copies out from under a correction. The death path's exact reasoning.
	 */
	void EndHitstun();

	/** Shared body of the two tell getters: clamped progress across the span, scaled onto the portion. */
	float ComputeTellTime(float StartTime, float SpanSeconds, float PortionSeconds) const;
	void ApplyHitstunState();
	void ClearHitstunState();

	/**
	 *  Ends the parry recovery. Driven from Tick against ParryRecoveryEndsAt, as the stuns are. No
	 *  window equivalent: the window's expiry is CloseParryWindow, shared with the cancellation path
	 *  so a recovery cannot be dodged by ending the parry unusually.
	 *
	 *  This one also ends GA_Parry, which a whiff leaves running so its movement lock spans the
	 *  recovery.
	 */
	void EndParryRecovery();
	void ApplyParryRecoveryState();
	void ClearParryRecoveryState();

	/** The dodge's parry gap, same shape. Ends no ability -- GA_Dodge is long over by then. */
	void EndDodgeRecovery();
	void ApplyDodgeRecoveryState();
	void ClearDodgeRecoveryState();

	/**
	 *  State.Parrying, applied and cleared against the window rather than the ability. No Begin/End
	 *  pair, because the window already has one -- OpenParryWindow and CloseParryWindow -- and a
	 *  second would give the tag a lifetime that could drift from what it names.
	 */
	void ApplyParryWindowState();
	void ClearParryWindowState();

	/**
	 *  Ends a running parry recovery because something was inflicted on this character.
	 *
	 *  The schema does the work: a lockout is externally inflicted and a recovery self-inflicted, so
	 *  being punished supersedes the price you were paying yourself. Anything that inflicts a lockout
	 *  should call this -- deliberately not narrowed to hitstun.
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
	void OnRep_HitstunTell();

	UFUNCTION()
	void OnRep_BlockstunTell();

	UFUNCTION()
	void OnRep_ParryLockoutTell();

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
	 *  Applies State.Dead, cancels everything running, stops the character moving.
	 *
	 *  Cancelling is the difference from exhaustion, which gates activation and lets a running
	 *  ability finish. Without it a killing blow mid-swing leaves the dead character completing the
	 *  attack, hitbox and all.
	 */
	void EnterDeath(AActor* Killer);

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
	 *  The same direction pair for the press an ability is activating from right now. Written for
	 *  every ability press rather than only the dodge, because the character has no business knowing
	 *  which tag means dodge. A buffered press restores these before retrying, so an ability reads
	 *  one field whether it fired live or late.
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
	 *  Replicated, because the tags they drive are loose tags and loose tags do not replicate. The
	 *  bool is the wire format and the tag is the local consequence: the server decides, the bool
	 *  replicates, OnRep applies the tag on each client. A GameplayEffect carrying the tag would
	 *  replicate on its own, but UE 5.8 expresses effect-granted tags through gEComponents, which
	 *  cannot be scripted -- see Docs/Working-In-Unreal.md.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Exhausted)
	bool bExhausted = false;

	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;

	/** Same server-decides/OnRep-applies contract as the two above. */
	UPROPERTY(ReplicatedUsing = OnRep_GuardBroken)
	bool bGuardBroken = false;

	/**
	 *  When the guard-break stun expires, in world seconds. Server-authoritative.
	 *
	 *  A Tick-checked timestamp rather than a SetTimer: the two network-unaware SetTimer sites this
	 *  project has are filed as a multiplayer trap, and a timestamp is what the regen suppressor
	 *  beside it uses.
	 */
	float GuardBreakEndsAt = 0.0f;

	/** Same server-decides/OnRep-applies contract as the three above. */
	UPROPERTY(ReplicatedUsing = OnRep_Blockstun)
	bool bInBlockstun = false;

	/** The fifth of the family, same contract. Blockstun's sibling for hits that were not blocked. */
	UPROPERTY(ReplicatedUsing = OnRep_Hitstun)
	bool bInHitstun = false;

	/** When hitstun expires, in world seconds. A Tick-checked timestamp like its two siblings. */
	float HitstunEndsAt = 0.0f;

	/**
	 *  Bumped on every EnterHitstun, and the only thing that marks a *fresh* hit.
	 *
	 *  bInHitstun is already true for a second hit inside a running stun, so a tell keyed on it
	 *  re-enters nothing. Replicated because the tell is drawn on every machine; a uint8 counter
	 *  and a span travel where a world-time deadline cannot, world clocks being per-machine.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_HitstunTell)
	uint8 HitstunTellSerial = 0;

	/** Seconds the current hitstun tell must fill: HitstunEndsAt minus the moment the hit landed. */
	UPROPERTY(Replicated)
	float HitstunTellSpanSeconds = 0.0f;

	/** Local clock the span runs against -- set on the server at entry, on a client by the OnRep. */
	float HitstunTellStartTime = 0.0f;

	/** Blockstun's three, same contract. */
	UPROPERTY(ReplicatedUsing = OnRep_BlockstunTell)
	uint8 BlockstunTellSerial = 0;

	UPROPERTY(Replicated)
	float BlockstunTellSpanSeconds = 0.0f;

	float BlockstunTellStartTime = 0.0f;

	/** On the floor. Ninth of the replicated state family, same contract. */
	UPROPERTY(ReplicatedUsing = OnRep_KnockedDown)
	bool bKnockedDown = false;

	/**
	 *  Which type is running. Replicated beside the bool rather than derived, because the split it
	 *  selects decides which get-up options a client may predict, and a client that guessed would
	 *  mispredict a refusal.
	 */
	UPROPERTY(Replicated)
	ETDKnockdownType KnockdownType = ETDKnockdownType::None;

	/** Authority-side boundary: when everything stops being refused. */
	float KnockdownLockoutEndsAt = 0.0f;

	/** Authority-side boundary: when the auto-rise takes the choice away. */
	float KnockdownInputWindowEndsAt = 0.0f;

	/** Authority-side boundary: the stand. Set when a rise begins, not at entry. */
	float KnockdownRiseEndsAt = 0.0f;

	/**
	 *  When the fixture's home reset is due, always the shared KnockdownRiseSeconds from the
	 *  rise. Kept separately from KnockdownRiseEndsAt so an option that shortens the rise moves
	 *  the knockdown's end without moving the teleport, which would otherwise land inside the
	 *  travel the option just made.
	 */
	float KnockdownHomeResetAt = 0.0f;

	/** Its own handle, so a pending attack reset and a pending stand reset cannot clobber. */
	FTimerHandle KnockdownHomeResetTimerHandle;

	/**
	 *  Whether a rise has begun -- the invincibility switch, separate from the tag because the tag
	 *  spans the rise too: the body is still on the floor for every purpose except being hittable.
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
	 *  Trace-only and server-only, so an assertion can tell the two carry axes apart -- roughly zero
	 *  in a 1v1, roughly plus or minus ninety for the ender's two victims.
	 */
	float LastKnockdownBearingDegrees = 0.0f;

	/** Serving a parry lockout. Tenth of the replicated state family, same contract. */
	UPROPERTY(ReplicatedUsing = OnRep_ParryLockout)
	bool bInParryLockout = false;

	/** Authority-side deadline. Authored on the caught swing; see EnterParryLockout. */
	float ParryLockoutEndsAt = 0.0f;

	/** The stuns' three, same contract. Spans the extended end, so max-extension needs no handling. */
	UPROPERTY(ReplicatedUsing = OnRep_ParryLockoutTell)
	uint8 ParryLockoutTellSerial = 0;

	UPROPERTY(Replicated)
	float ParryLockoutTellSpanSeconds = 0.0f;

	float ParryLockoutTellStartTime = 0.0f;

	/**
	 *  A parry window is open. Sixth of the replicated-state family, same contract.
	 *
	 *  Replicated rather than left as the ability's business, under the rule that new state is a
	 *  replicated property and never a loose tag. This bool is what the attacker's hit path reads;
	 *  State.Parrying is the tag marking the same span for anything gating on it, and keeping them
	 *  apart lets the animation and the negation be retimed independently.
	 *
	 *  The tag is applied and cleared against this bool, not GA_Parry's lifetime: a whiffed parry
	 *  keeps the ability alive across its recovery to hold the movement lock, so a tag riding the
	 *  ability would stay up for the whole lockout.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ParryWindow)
	bool bParryWindowOpen = false;

	/** When the parry window expires, in world seconds. A Tick-checked timestamp like the stuns. */
	float ParryWindowEndsAt = 0.0f;

	/**
	 *  What the open window will charge if it closes without catching anything. Captured when the
	 *  window opens rather than read off GA_Parry at close time, so its price is fixed when bought.
	 */
	float PendingParryWhiffRecoverySeconds = 0.0f;

	/** Whether the open window has already negated a hit, which is what makes its close free. */
	bool bParryCaughtThisWindow = false;

	/** Every ability is refused by a whiffed parry. Seventh of the family, same contract. */
	UPROPERTY(ReplicatedUsing = OnRep_ParryRecovery)
	bool bInParryRecovery = false;

	/** When the parry recovery expires, in world seconds. Max-extended, never reassigned. */
	float ParryRecoveryEndsAt = 0.0f;

	/** A parry is refused by a just-ended dodge. Eighth of the family, same contract. */
	UPROPERTY(ReplicatedUsing = OnRep_DodgeRecovery)
	bool bInDodgeRecovery = false;

	/** When the dodge's parry gap expires, in world seconds. Max-extended, never reassigned. */
	float DodgeRecoveryEndsAt = 0.0f;

	/**
	 *  A successful parry is still protecting this character. Replicated but tagless -- the one place
	 *  this breaks the family's pattern, because the others carry a tag for something to refuse on
	 *  and nothing refuses on Grace.
	 */
	UPROPERTY(Replicated)
	bool bInParryGrace = false;

	/**
	 *  When Grace expires, in world seconds. Assigned, never max-extended -- the opposite of every
	 *  other timestamp here, and the no-re-arm rule in one line. Only BeginParryGrace writes it, and
	 *  only a window catch calls that.
	 */
	float ParryGraceEndsAt = 0.0f;

	/** See GetPendingParryMontageRecoveryRate. -1 means the parry montage has no marker. */
	float PendingParryMontageRecoveryRate = -1.0f;

	/** A connected attack owes this character its movement back. See BeginOnHitMovementWaiver. */
	bool bOnHitMovementWaiverPending = false;

	/** When the on-hit waiver hands movement back: contact + that swing's hitstun. */
	float OnHitMovementWaiverAt = 0.0f;

	/** Dedup clock for bDebugDodgeAfterHit, so a swing hitting two bodies still dodges once. */
	float DebugLastDodgeAfterHitAt = -1.0f;

	/**
	 *  Which hit of the light string the next chained attack continues from. 0 is a fresh string's
	 *  first swing. Replicated so remote machines agree which swing an activation means. See
	 *  ResolveStringSwingIndexForActivation.
	 */
	UPROPERTY(Replicated)
	uint8 StringIndex = 0;

	/**
	 *  When the string's link window closes. Local like BlockstunEndsAt -- the replicated index is
	 *  the wire truth, this is only its deadline.
	 */
	float StringWindowEndsAt = 0.0f;

	/** The running knockback translation's root motion source ID, so a re-hit can replace it. */
	uint16 KnockbackRootMotionSourceID = 0;

	/** Taps left in the current auto-attack burst. Guards the home reset; see the taps knob. */
	int32 DebugStringTapsRemaining = 0;

	/** The body the last attack aimed at, excluded from the next selection while rotating. Weak,
	 *  because a target can die and be destroyed between attacks, and a stale raw pointer would
	 *  exclude an address rather than a pawn. */
	TWeakObjectPtr<APawn> DebugLastFocusTarget;

	/**
	 *  When the blockstun lockout expires, in world seconds. Server-authoritative, a Tick-checked
	 *  timestamp for the reason GuardBreakEndsAt gives. Extended by taking the max, never
	 *  reassigned: blocking two attacks must not serve a shorter sentence than the slower alone.
	 */
	float BlockstunEndsAt = 0.0f;

	/**
	 *  True only while TickBlockDrain is writing. This is what stops drain exhausting you.
	 *
	 *  Exhaustion is decided by the stamina-changed delegate, which sees the bar reach zero and
	 *  cannot see what emptied it. Correct for every other spender, wrong for the guard's drain --
	 *  the only continuous spend, meant to park the bar at zero harmlessly.
	 *
	 *  The rule: exhaustion is a consequence of an action or an attack, never of a state you hold.
	 */
	bool bApplyingBlockDrain = false;

	/** MaxWalkSpeed as authored, captured before any block modifies it. */
	float DefaultMaxWalkSpeed = 0.0f;

	/**
	 *  A resume pass is owed next tick. Set by an ability ending and by landing. The deferral is the
	 *  fix, not an optimisation: resuming inside OnAbilityEnded re-entered activation and
	 *  double-activated the guard, leaking activeCount and sticking it up permanently. A flag
	 *  drained once per tick cannot re-enter anything.
	 */
	bool bResumePending = false;

	/** When the guard's minimum duration expires, in world seconds. */
	float BlockCommitEndsAt = 0.0f;


	/**
	 *  The mesh's authored offset from the capsule, captured before physics ever moves it.
	 *
	 *  Ragdolling drives the mesh in world space, so the relative transform is meaningless by the
	 *  time it stops. Restoring from a value read after death would bake the ragdoll's final pose in
	 *  as the new rest offset and the character would stand up crooked. Captured in BeginPlay rather
	 *  than the constructor, so it reflects whatever a Blueprint authored.
	 */
	FTransform MeshRestRelativeTransform;

	bool bRagdollActive = false;

	FTimerHandle DebugReviveTimerHandle;

	/**
	 *  Seeded-once flag for the owned ASC only -- the training dummy's. A player's lives on
	 *  ATDPlayerState, where its ASC lives. Splitting them is not tidiness: a player pawn's BeginPlay
	 *  runs before possession, so a single flag would be spent seeding the fallback ASC and the real
	 *  one would never be seeded. See ATDPlayerState::HasSeededDefaults.
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
	FTimerHandle DebugGetUpReleaseTimerHandle;

	/** Input DebugGetUpPress is holding, for its release. */
	FGameplayTag DebugGetUpHeldTag;

	/** True once DebugGetUpMode has pressed for the current knockdown. */
	bool bDebugGetUpPressed = false;

	/**
	 *  Where a debug fixture started, captured once, so each swing or dodge begins from the same
	 *  spot. Named for the auto-attacker because it shipped with it; the auto-defender shares it
	 *  rather than carrying a second copy.
	 */
	FTransform DebugAutoAttackHomeTransform;
};
