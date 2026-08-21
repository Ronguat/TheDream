// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDMeleeAttackAbility.h"
#include "Engine/TimerHandle.h"
#include "TDChargedAttackAbility.generated.h"

struct FGameplayEventData;

/**
 *  One outcome of a held attack, described by when it hits rather than how it plays. Every timing
 *  is real seconds from the press; the play rates producing them are derived at runtime from the
 *  montage's measured position, never authored.
 */
USTRUCT(BlueprintType)
struct FTDAttackBranch
{
	GENERATED_BODY()

	/** Identifies the attack, e.g. Ability.Attack.Heavy. Applied as a loose tag while it swings. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	FGameplayTag AttackTag;

	/**
	 *  Optional distinct release animation for this branch. None means every branch shares one
	 *  release; setting it buys readability at the cost of this branch's ambiguity, since a defender
	 *  who recognises the animation need not wait out the coil.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	FName MontageSection = NAME_None;

	/**
	 *  Hold past this and the attack escalates to the next branch. The input boundary, not the
	 *  attack's speed: releasing any time before it produces this branch identically, because the
	 *  windup runs its full length either way -- the fixed cost that stops a fractionally-held heavy
	 *  dominating the light. The first branch's value is also when the coil begins.
	 *
	 *  Branches must be ordered shortest first.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float HoldUntilSeconds = 0.2f;

	/**
	 *  When this branch's hitbox goes live, measured from the press -- the headline number, what a
	 *  defender reacts to. Must exceed HoldUntilSeconds: the gap is the runway the montage needs to
	 *  travel from the coil into the strike.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float ReleaseAtSeconds = 0.25f;

	/**
	 *  How long the hitbox stays live. Authored rather than inherited from whatever play rate the
	 *  windup ended on -- without it, a branch hurrying into its strike gets a brief hitbox and one
	 *  that crawls in gets an absurdly long one, purely as a side effect of the windup maths.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.01"))
	float ReleaseSeconds = 0.09f;

	/**
	 *  How long this branch is helpless after its hitbox closes -- the punish window, and a balance
	 *  number rather than anything else.
	 *
	 *  Authored in absolute seconds, and the montage warps to fit. Measured to the blend-out,
	 *  because that is where the ability ends and the attacker can act again; the clip keeps playing
	 *  past it as follow-through.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.01"))
	float RecoverySeconds = 0.2667f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float Damage = 15.0f;

	/**
	 *  Stamina taken from a target who blocks this branch. Zero health damage is dealt instead.
	 *
	 *  Stamina damage is not stamina drain: drain is self-inflicted by holding a guard and runs the
	 *  bar down harmlessly, while damage is what an attacker inflicts and the only thing that can
	 *  break a guard -- which happens exactly when a blocked hit leaves the defender at zero.
	 *
	 *  The values are set against the bar's maximum, which is what makes "charged heavy breaks
	 *  block" true without a special case. Change the maximum and it silently stops being true.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float StaminaDamage = 5.0f;

	/**
	 *  How long a defender who successfully blocks this branch is locked out of offense -- the
	 *  attacker's reward for being blocked, and what decides whether a blocked attack is safe. The
	 *  defender keeps guard and movement and loses initiative. Above the attacker's own
	 *  RecoverySeconds the attack is safe on block; below, the defender can punish.
	 *
	 *  Zero disables blockstun for the branch: blocking costs the defender nothing but stamina.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float BlockstunSeconds = 0.3f;

	/**
	 *  The volumes this branch strikes with. Empty falls back to the ability's own set, so a branch
	 *  nobody authored is not silently damage-less. Per branch because the spec gives heavy a higher
	 *  range than light and charged the highest; MaxReachCm is the authored answer.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	TArray<FTDAttackHitbox> Hitboxes;

	/**
	 *  How far this branch carries itself from the commit checkpoint, in centimetres. Where an
	 *  attack's reach is actually differentiated, and the majority of the travel -- the base lunge
	 *  stays small enough that a flick cannot make it look wrong.
	 *
	 *  It cannot apply earlier: all three tiers share one windup, so the light carries no tell, and
	 *  a charged lunging further from the press would announce itself from frame one. Starting at
	 *  commit also means facing is frozen and the coil -- the phase whose duration differs between
	 *  tiers -- carries no lunge.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float LungeDistanceCm = 75.0f;

	/**
	 *  How long this branch's lunge takes. Authored, and independent of ReleaseSeconds: how long the
	 *  volume persists and how long the character is carried are different questions.
	 *
	 *  A burst finishing early in the release window is the point -- equal to it, the volume is
	 *  dragged through space for its whole existence with no moment of planting and striking. Keep
	 *  it below ReleaseAtSeconds + ReleaseSeconds - HoldUntilSeconds. Longer is legal and unclamped.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.01"))
	float LungeDurationSeconds = 0.12f;

	/**
	 *  Optional shape for this branch's lunge, over 0..1 of its duration. Null is constant. Must
	 *  average 1.0 or the authored distance is a lie -- the force is multiplied by it each tick.
	 *  See UTDMeleeAttackAbility::LungeStrengthCurve.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	TObjectPtr<UCurveFloat> LungeStrengthCurve = nullptr;

	/**
	 *  Hitstun imposed on a target this branch cleanly hits. 0 -- the default -- means none. For the
	 *  light it is the string guarantee's whole mechanism: it must cover the gap to the next chained
	 *  contact, or "any hit guarantees the rest" is a lie.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float HitstunSeconds = 0.0f;

	/**
	 *  Whether this branch's clean hit knocks down, and how hard. None means hitstun instead.
	 *
	 *  Light None, heavy Hard, charged Hard: a committed hit floors you hard, because the commitment
	 *  bought the oki. The light stays a hitstun so the string it opens can chain -- a knockdown
	 *  mid-string would end the string it belongs to.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	ETDKnockdownGrade KnockdownGrade = ETDKnockdownGrade::None;

	/** Seconds locked out when this branch is parried. Authored; see UTDMeleeAttackAbility. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float ParryLockoutSeconds = 0.65f;

	/**
	 *  Whether committing this branch keeps the string alive. True on the light alone: lights chain,
	 *  and a heavy or charged commit ends the string. Setting it on a heavy is a details-panel
	 *  change, never structure. False by default so deserialised branches are inert.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	bool bChainsIntoString = false;

	/**
	 *  Target Lock, rotational half: who this branch may be steered onto. Shape only -- reach is
	 *  derived from this branch's own travel and damage reach; see FTDAimAssistWedge and
	 *  UTDMeleeAttackAbility::AimAssistMarginCm.
	 *
	 *  Its half-arc is the maximum correction, by construction: a candidate outside the wedge is not
	 *  eligible. Live from the moment its branch is escalated to, not only at commit.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	FTDAimAssistWedge AimAssistWedge;
};

/**
 *  One hit of the light string beyond the first: its clip, that clip's authored positions, and the
 *  branch-0 values that vary by position.
 *
 *  Hit 1 is deliberately absent -- it stays on the ability's original surface (AttackMontage, the
 *  ability-level ReleaseStartSeconds and CoilEndSeconds, Branches[0]), because moving those
 *  UPROPERTYs would orphan every play-verified CDO override. So StringSwings[k] describes hit k+2
 *  and the accessors resolve index 0 to the legacy fields. The asymmetry is load-bearing.
 *
 *  Heavy and charged are reachable from any swing and keep their Branches values; only the
 *  montage-position numbers here apply to them, being properties of the clip rather than the tier.
 */
USTRUCT(BlueprintType)
struct FTDStringSwing
{
	GENERATED_BODY()

	/** This swing's montage. Must play an in-place clip, like AM_Attack, or the lunge dies. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	/**
	 *  Where this montage's Release Window notify opens, hand-copied from its placement and checked
	 *  at runtime by the drift warning. Read the real value off the MONTAGE trace's `notify
	 *  trigger=` line.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float ReleaseStartSeconds = 0.3f;

	/** Where this montage's coil creeps to. Must stay below its ReleaseStartSeconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float CoilEndSeconds = 0.28f;

	/** Branch-0 values for this position -- damage on hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float Damage = 15.0f;

	/** Stamina damage this position deals to a guard. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float StaminaDamage = 5.0f;

	/** Blockstun this position imposes when blocked. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float BlockstunSeconds = 0.4f;

	/** Hitstun this position imposes on a clean hit. 0 means none; see FTDAttackBranch's field. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float HitstunSeconds = 0.0f;

	/**
	 *  Whether this position's clean hit knocks down. The ender authors Normal; the rest None.
	 *
	 *  The string's volume finisher knocks down on the gentle grade, and it is the kit's only
	 *  360-degree knockdown -- that pairing is what stops a crowd being hard-floored. Authored here
	 *  rather than structural; a future weapon may differ.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing")
	ETDKnockdownGrade KnockdownGrade = ETDKnockdownGrade::None;

	/** Seconds locked out when this position is parried. Authored; see UTDMeleeAttackAbility. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float ParryLockoutSeconds = 0.65f;

	/** This position's recovery -- commitment and punish window. The ender authors the heavy endlag. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.01"))
	float RecoverySeconds = 0.4f;

	/**
	 *  This position's damaging volumes. Empty inherits the branch's, the same fallback a branch's
	 *  own empty array has: a swing that silently deals no damage is a filed failure mode.
	 *
	 *  Per-swing because what an attack hits is a different question from how much aim error is
	 *  forgiven -- the aim wedge stays a learnable constant across the ladder and the string.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing")
	TArray<FTDAttackHitbox> Hitboxes;

	/**
	 *  How long this position's damaging phase lasts. 0 inherits the branch's.
	 *
	 *  It sets the release play rate as well as the window, the rate being the notify's authored
	 *  width divided by this -- so lengthening it widens the window in wall clock and slows the
	 *  contact motion toward true speed. Equal to the notify's width plays the release at 1.0.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float ReleaseSeconds = 0.0f;

	/**
	 *  This position's lunge distance. 0 inherits the branch's.
	 *
	 *  Mostly a whiff value at the string's own spacing: a connecting chain parks the target at
	 *  HitSpacingCm and the standoff gate clamps travel to what is left, so an increase is invisible
	 *  on connects until knockback grows. Also feeds the aim wedge's derived reach.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float LungeDistanceCm = 0.0f;

	/** This position's lunge duration. **0 inherits the branch's.** Separate from distance because
	 *  the two are tuned against different things: reach, and how long you are committed to it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float LungeDurationSeconds = 0.0f;
};

/**
 *  An attack whose identity is decided by how long the button is held.
 *
 *  Branches are described by when they hit, and every play rate is derived from that at runtime.
 *  Four phases:
 *
 *   - Windup, shared and identical for all branches, played fast enough that the quickest reaches
 *     its strike on time. That rate is set by the fastest branch, so the light needs no
 *     acceleration at commit -- one continuous rate from press to impact.
 *   - Coil, from the instant the light is no longer available. The tell, and the mechanism that
 *     makes slower branches slower: they are held back here, not sped up later.
 *   - Release, stretched to the branch's authored ReleaseSeconds.
 *   - Recovery, stretched to RecoverySeconds, measured to the montage's blend-out because that is
 *     where the ability ends.
 *
 *  So an attack is three authored durations with the animation fitted to all three.
 *
 *  Two rules the implementation enforces. Every rate is computed from the montage's measured
 *  position, never from where it was assumed to be -- the coil timer fires a frame or two late, and
 *  a rate derived from the assumed start compounds until the coil overruns the release window and
 *  the attack silently stops dealing damage. And the montage is never stopped: a stopped montage
 *  banks the time it sits still and spends it in one frame on resume, skipping the release window
 *  and firing every frame of root motion at once.
 *
 *  One animation rather than one per branch: the identity is a consequence of how long the windup
 *  lasted, so it cannot be known in advance to pick a clip -- and sharing it means the defender
 *  cannot tell branches apart until the coil appears. Reactability is measured from that tell.
 */
UCLASS(abstract)
class UTDChargedAttackAbility : public UTDMeleeAttackAbility
{
	GENERATED_BODY()

public:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:

	/**
	 *  Montage position at which the Release Window notify opens. Unavoidably duplicated from the
	 *  notify's placement, because the windup rate must be known at activation before any notify has
	 *  fired. The ability checks itself against the real thing when the window opens and warns on
	 *  drift.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Animation", meta=(ClampMin="0.0"))
	float ReleaseStartSeconds = 0.36f;

	/**
	 *  Montage position the coil creeps toward, arriving as the deepest branch commits. The coil's
	 *  tuning knob: closer to the coil's start reads as a hold, further as a continuous wind-up.
	 *
	 *  Must stay below ReleaseStartSeconds. If the coil reaches the release window before the attack
	 *  commits, the window opens with no trace listening and the attack deals no damage at all.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Timing", meta=(ClampMin="0.0"))
	float CoilEndSeconds = 0.35f;

	/** Optional section played on activation. None starts the montage from the beginning. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Animation")
	FName WindupSection = NAME_None;

	/**
	 *  Applied the instant the attack commits, removed when it ends -- the boundary every defensive
	 *  action cancels before and none cancels after.
	 *
	 *  A defensive ability blocks on this rather than State.Attacking, which is present from the
	 *  press: blocking on State.Attacking would forbid cancelling a windup, and blocking on nothing
	 *  would let a committed swing be erased.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Timing")
	FGameplayTag CommittedTag;

	/** Outcomes, ordered shortest first. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Timing")
	TArray<FTDAttackBranch> Branches;

	/**
	 *  The light string's hits beyond the first, in order. Empty means no string, and is the C++
	 *  default, so the machinery ships inert.
	 *
	 *  String length is this array's size plus one, which makes 2-, 3- and 4-hit strings
	 *  details-panel variants. Hit 1 lives in the legacy fields rather than element 0; see
	 *  FTDStringSwing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|String")
	TArray<FTDStringSwing> StringSwings;

	/**
	 *  How long after a chainable swing ends a fresh press continues the string, fighting-game link
	 *  style, before it resets to hit 1. Also how long a buffered chain press outlives the swing that
	 *  refused it -- one window on purpose. The delay-and-bait game's ceiling.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|String", meta=(ClampMin="0.0"))
	float StringLinkWindowSeconds = 0.4f;

	/**
	 *  Extra delay after recovery starts before a chain may leave the swing. 0 chains at recovery
	 *  start, the release window's close. The cadence knob beyond hitstun: raising it slows every
	 *  string without touching a clip.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|String", meta=(ClampMin="0.0"))
	float ChainOpenAfterRecoverySeconds = 0.0f;

	virtual float GetAttackDamage() const override;
	virtual float GetAttackStaminaDamage() const override;
	virtual float GetAttackBlockstunSeconds() const override;
	virtual float GetAttackHitstunSeconds() const override;
	virtual ETDKnockdownGrade GetAttackKnockdownGrade() const override;
	virtual float GetAttackParryLockoutSeconds() const override;
	virtual float GetKnockbackSpacingCm(bool bBlocked) const override;

	/**
	 *  The on-hit waiver's offensive half: drop CommittedTag now rather than at EndAbility. Only the
	 *  tag goes -- the facing and movement locks have their own lifetimes, movement returning on its
	 *  derived delay and facing staying frozen because a connected swing has no business re-aiming.
	 */
	virtual void ReleaseCommitmentTag() override;

	virtual const TArray<FTDAttackHitbox>& GetAttackHitboxes() const override;
	virtual UAnimMontage* GetActiveAttackMontage() const override;

	/** Chain presses survive a running attack; see the base's contract. */
	virtual bool ShouldExtendBufferWhileActive() const override { return true; }

	/** Ends this swing early for a waiting chain press, if the chain-out span is open. */
	virtual bool TryChainOutForBufferedPress() override;

	/**
	 *  The aim assist wedge actually tested for a branch: its authored shape at its derived reach.
	 *  Built on demand rather than cached, because every input can change in the details panel
	 *  between activations and a cache is one more thing that can disagree with what a designer sees.
	 *
	 *  Monotonicity is structural rather than a rule to follow: reach is a constant plus the branch's
	 *  own lunge, and lunges increase up the ladder, so a later branch reaching less than an earlier
	 *  one cannot be expressed -- which is what ladder-following homing needs to avoid dropping a
	 *  target it had locked.
	 *
	 *  Returns a disabled wedge for an out-of-range index or a branch with bEnabled false.
	 */
	FTDAttackHitbox BuildAimAssistWedge(int32 BranchIndex) const;

private:

	/** Arms the next checkpoint, or runs it immediately if it is already due. */
	void ScheduleCheckpoint(float DelaySeconds);

	/** Escalates to the next branch if the button is still down, otherwise commits. */
	void HandleCheckpoint();

	/** Starts the coil from wherever the montage actually is, aimed at CoilEndSeconds. */
	void EnterCoil();

	/** Locks in the selected branch: applies its tag, starts tracing, aims at its release. */
	void CommitAttack();

	/** Stretches the release window to the selected branch's ReleaseSeconds. */
	UFUNCTION()
	void HandleReleaseWindowBegan(FGameplayEventData Payload);

	/**
	 *  Takes the release rate back off when the damaging window closes. Without it the rate derived
	 *  for the release stays applied for the rest of the montage, so follow-through and all of
	 *  recovery inherit a speed computed from how wide the notify happens to be authored.
	 */
	UFUNCTION()
	void HandleReleaseWindowEnded(FGameplayEventData Payload);

	/** Whether a window event came from the montage this attack is playing. */
	bool IsWindowForThisAttack(const FGameplayEventData& Payload) const;

	/**
	 *  Shared windup rate: fast enough that the first branch reaches ReleaseStartSeconds exactly on
	 *  its ReleaseAtSeconds. Everything slower comes from the coil holding it back, never from a
	 *  later branch accelerating.
	 */
	float ComputeWindupPlayRate() const;

	/** The first branch's HoldUntilSeconds: where the tiers stop being indistinguishable. */
	virtual float GetBaseLungeDurationSeconds() const override;

	/**
	 *  Recovery rate: carries the montage from where it actually is to the blend-out boundary in the
	 *  branch's authored RecoverySeconds. Takes the measured position rather than assuming the
	 *  release ended where the notify says -- the window closes a frame or two late, and a rate from
	 *  the assumed end compounds that error across the longest phase.
	 *
	 *  Returns negative when the montage has no room left, a real authoring outcome rather than an
	 *  error: a clip whose tail is shorter than the blend cannot host any recovery. The caller warns
	 *  and leaves the rate alone.
	 */
	float ComputeRecoveryPlayRate(float FromPosition, float TargetSeconds) const;

	/**
	 *  Montage position at which blend-out begins, which is where the ability ends.
	 *
	 *  Takes a play rate because the boundary is not fixed: unless the montage authors a
	 *  BlendOutTriggerTime, the engine blends when the remaining time at the current rate equals the
	 *  blend's duration, so a slower recovery pushes the boundary later. The wrong rate here
	 *  silently misplaces the end of the ability.
	 */
	float GetBlendOutStartSeconds(float PlayRate) const;

	/** Real seconds since this activation. */
	float GetElapsedSeconds() const;

	/** Montage playhead position in seconds, or -1 if there is nothing to read. */
	float GetMontagePosition() const;

	/** Sets the montage's play rate, if one is playing. Never called with 0. */
	void SetMontagePlayRate(float PlayRate) const;

	/** Total swings: the legacy first hit plus the StringSwings array. Never below 1. */
	int32 GetSwingCount() const { return 1 + StringSwings.Num(); }

	/** Whether a swing at this index has a string successor to chain into. */
	bool HasSuccessorSwing(int32 SwingIndex) const { return SwingIndex + 1 < GetSwingCount(); }

	/** This swing's ReleaseStartSeconds: the legacy field for index 0, the swing's otherwise. */
	float GetSwingReleaseStartSeconds(int32 SwingIndex) const;

	/** This swing's CoilEndSeconds, resolved the same way. */
	float GetSwingCoilEndSeconds(int32 SwingIndex) const;

	/**
	 *  Per-swing overrides, each resolving swing -> branch -> ability. They take the branch index
	 *  explicitly rather than reading SelectedBranchIndex, because BuildAimAssistWedge asks about
	 *  branches other than the selected one.
	 */
	const TArray<FTDAttackHitbox>& GetSwingHitboxes(int32 SwingIndex, int32 BranchIndex) const;

	/** This swing's release duration, or the branch's when it authors none. */
	float GetSwingReleaseSeconds(int32 SwingIndex, int32 BranchIndex) const;

	/** This swing's lunge distance, or the branch's when it authors none. */
	float GetSwingLungeDistanceCm(int32 SwingIndex, int32 BranchIndex) const;

	/** This swing's lunge duration, or the branch's when it authors none. */
	float GetSwingLungeDurationSeconds(int32 SwingIndex, int32 BranchIndex) const;

	/**
	 *  Whether the selected branch at the current swing is a non-final string light -- the gate for
	 *  both knockback and the chain-out. A heavy, a charged, the ender and a non-chaining branch 0
	 *  all fail it, which is exactly the set that must not reset spacing.
	 */
	bool IsNonFinalStringLight() const;

	/** True inside the chain-out span: committed, chainable, recovery running past the delay. */
	bool IsChainOutOpen() const;

	int32 SelectedBranchIndex = 0;
	bool bAttackCommitted = false;
	bool bInputHeld = true;
	bool bCoiling = false;
	float ActivationWorldTime = 0.0f;

	/** Which swing this activation is, fixed at activation. 0 is the legacy first hit. */
	int32 CurrentSwingIndex = 0;

	/** True from RELEASE OFF to the ability's end -- the span the chain-out may open inside. */
	bool bInRecovery = false;

	/** World time recovery began, for ChainOpenAfterRecoverySeconds. */
	float RecoveryStartedAt = 0.0f;

	FTimerHandle CheckpointTimerHandle;
	FGameplayTag AppliedAttackTag;
};
