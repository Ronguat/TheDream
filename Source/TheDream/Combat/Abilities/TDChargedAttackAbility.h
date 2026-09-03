// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDMeleeAttackAbility.h"
#include "Engine/TimerHandle.h"
#include "TDChargedAttackAbility.generated.h"

struct FGameplayEventData;

/**
 *  One rung of the hold ladder: the tier's identity and its checkpoints, shared by every string
 *  position. What a position throws at this tier is its FTDAttackCell. Every timing is real
 *  seconds from the press; the play rates producing them are derived at runtime from the
 *  montage's measured position, never authored.
 */
USTRUCT(BlueprintType)
struct FTDAttackBranch
{
	GENERATED_BODY()

	/** Identifies the attack, e.g. Ability.Attack.Heavy. Applied as a loose tag while it swings. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	FGameplayTag AttackTag;

	/** Optional section jumped to at commit. None plays the cell's montage straight through. */
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
	 *  Whether committing this branch keeps the string alive. True on the light alone: lights chain,
	 *  and a heavy or charged commit ends the string. Setting it on a heavy is a details-panel
	 *  change, never structure. False by default so deserialised branches are inert.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	bool bChainsIntoString = false;

	/**
	 *  Target Lock, rotational half: who this branch may be steered onto. Shape only -- reach is
	 *  derived from the cell's own travel and damage reach; see FTDAimAssistWedge and
	 *  UTDMeleeAttackAbility::AimAssistMarginCm.
	 *
	 *  Its half-arc is the maximum correction, by construction: a candidate outside the wedge is not
	 *  eligible. Live from the moment its branch is escalated to, not only at commit.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	FTDAimAssistWedge AimAssistWedge;
};

/**
 *  One attack: what one string position throws at one tier. Every value is the cell's own, and
 *  nothing is read from another cell.
 *
 *  The montage is the position's light clip when this is the branch-0 cell, entered at 0, and an
 *  escalated tier's clip otherwise, entered at EntrySeconds when the escalation swaps it in.
 *  An unset montage on an escalated tier leaves that tier on the light's clip, rate-warped.
 */
USTRUCT(BlueprintType)
struct FTDAttackCell
{
	GENERATED_BODY()

	/** The clip. Must play in place, like AM_Attack, or the lunge dies. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	/** Where an escalation's blend enters the clip. The dial that fits one clip to more than one source. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation", meta=(ClampMin="0.0"))
	float EntrySeconds = 0.0f;

	/**
	 *  Where the montage's Release Window notify opens, hand-copied from its placement and checked
	 *  at runtime by the drift warning. Read the real value off the MONTAGE trace's `notify
	 *  trigger=` line, and re-copy it whenever the notify moves.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation", meta=(ClampMin="0.0"))
	float ReleaseStartSeconds = 0.3f;

	/**
	 *  How long the hitbox stays live. It sets the release play rate as well as the window, the
	 *  rate being the notify's authored width divided by this -- equal to the notify's width plays
	 *  the release at 1.0.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0.01"))
	float ReleaseSeconds = 0.15f;

	/**
	 *  How long the attacker is helpless after the hitbox closes -- the punish window, in absolute
	 *  seconds. Measured to the montage's blend-out, because that is where the ability ends; the
	 *  montage warps to fit.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0.01"))
	float RecoverySeconds = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage", meta=(ClampMin="0.0"))
	float Damage = 15.0f;

	/**
	 *  Stamina taken from a target who blocks this cell. Zero health damage is dealt instead.
	 *  Damage, not drain: drain is self-inflicted by holding a guard, damage is what breaks one,
	 *  exactly when a blocked hit leaves the defender at zero. Set against the bar's maximum.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage", meta=(ClampMin="0.0"))
	float StaminaDamage = 5.0f;

	/**
	 *  How long a defender who blocks this cell is locked out of offense. Above this cell's
	 *  RecoverySeconds the attack is safe on block; below, the defender can punish. Zero disables.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage", meta=(ClampMin="0.0"))
	float BlockstunSeconds = 0.3f;

	/**
	 *  Hitstun imposed on a target this cell cleanly hits. 0 means none. For a chaining light it is
	 *  the string guarantee's whole mechanism and must cover the gap to the next contact.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage", meta=(ClampMin="0.0"))
	float HitstunSeconds = 0.0f;

	/** Whether this cell's clean hit knocks down, and how hard. None means hitstun instead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage")
	ETDKnockdownType KnockdownType = ETDKnockdownType::None;

	/** Seconds locked out when this cell is parried. Authored; see UTDMeleeAttackAbility. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage", meta=(ClampMin="0.0"))
	float ParryLockoutSeconds = 0.65f;

	/**
	 *  The volumes this cell strikes with, in the attacker's frame; see FTDAttackHitbox. Empty falls
	 *  back to the ability's own set and warns, a swing that silently deals no damage being a filed
	 *  failure mode.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hitbox")
	TArray<FTDAttackHitbox> Hitboxes;

	/**
	 *  How far this cell carries itself from the commit checkpoint, in centimetres. Where an
	 *  attack's reach is differentiated; the base lunge before commit is shared by every tier.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Motion", meta=(ClampMin="0.0"))
	float LungeDistanceCm = 75.0f;

	/**
	 *  How long this cell's lunge takes. Independent of ReleaseSeconds: how long the volume persists
	 *  and how long the character is carried are different questions. Keep it below
	 *  ReleaseAtSeconds + ReleaseSeconds - HoldUntilSeconds; longer is legal and unclamped.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Motion", meta=(ClampMin="0.01"))
	float LungeDurationSeconds = 0.12f;

	/**
	 *  Optional shape for this cell's lunge, over 0..1 of its duration. Null is constant. Must
	 *  average 1.0 or the authored distance is a lie; see UTDMeleeAttackAbility::LungeStrengthCurve.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Motion")
	TObjectPtr<UCurveFloat> LungeStrengthCurve = nullptr;
};

/**
 *  One string position: its cell per tier, indexed by branch, and the creep target an unsocketed
 *  tier's hold aims the light's clip at.
 */
USTRUCT(BlueprintType)
struct FTDAttackPosition
{
	GENERATED_BODY()

	/** One cell per branch, in the ladder's order. Element 0 is the light. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Position")
	TArray<FTDAttackCell> Cells;

	/**
	 *  Where an escalated tier with no montage of its own creeps the light's clip to. Must stay
	 *  below the light cell's ReleaseStartSeconds, or the window opens with no trace listening and
	 *  the attack deals no damage.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Position", meta=(ClampMin="0.0"))
	float CoilEndSeconds = 0.28f;
};

/**
 *  An attack whose identity is decided by how long the button is held.
 *
 *  Branches describe the ladder, positions describe the string, and each (position, branch) cell
 *  authors the attack thrown there in full. Four phases:
 *
 *   - Windup, played fast enough that the first branch reaches its strike on time; an escalation
 *     swaps the tier's own clip in at its entry point and blends into it.
 *   - Coil, the un-socketed fallback: with no clip of its own a tier holds the light's clip back.
 *   - Release, stretched to the cell's authored ReleaseSeconds.
 *   - Recovery, stretched to the cell's RecoverySeconds, measured to the montage's blend-out because
 *     that is where the ability ends.
 *
 *  Every rate is computed from the montage's measured position, never from where it was assumed
 *  to be, and the montage is never stopped: a stopped montage banks the time it sits still and
 *  spends it in one frame on resume.
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

	/** The hold ladder, ordered shortest first. Shared by every position. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Timing")
	TArray<FTDAttackBranch> Branches;

	/**
	 *  The string, hit 1 first, each position carrying one cell per branch. Three positions and
	 *  three branches make nine authored attacks. String length is this array's size; a position
	 *  or cell that is not authored falls back to the ability's own fields and warns.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|String")
	TArray<FTDAttackPosition> Positions;

	/**
	 *  Extra delay after recovery starts before a chain may leave the swing. 0 chains at recovery
	 *  start, the release window's close. The cadence knob beyond hitstun: raising it slows every
	 *  string without touching a clip.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|String", meta=(ClampMin="0.0"))
	float ChainOpenAfterRecoverySeconds = 0.0f;

	/**
	 *  How long chain-out stays open once ChainOpenAfterRecoverySeconds has elapsed. The span closes
	 *  inside recovery rather than at its end, so a swing left unchained past it runs the remainder
	 *  with no exit. InputBufferSeconds covers the same length before the span opens, making the
	 *  press-to-press window this value either side of the cadence.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|String", meta=(ClampMin="0.0"))
	float ChainOpenDurationSeconds = 0.2f;

	virtual float GetAttackDamage() const override;
	virtual float GetAttackStaminaDamage() const override;
	virtual float GetAttackBlockstunSeconds() const override;
	virtual float GetAttackHitstunSeconds() const override;
	virtual ETDKnockdownType GetAttackKnockdownType() const override;
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

	/** Inherits the base's refusal: a press expires at InputBufferSeconds whatever is running. */

	/** Ends this swing early for a waiting chain press, if the chain-out span is open. */
	virtual bool TryChainOutForBufferedPress() override;

	/**
	 *  The aim assist wedge actually tested for a branch: its authored shape at its derived reach.
	 *  Built on demand rather than cached, because every input can change in the details panel
	 *  between activations and a cache is one more thing that can disagree with what a designer sees.
	 *
	 *  Reach is a constant plus the cell's own lunge and damage reach, so a later branch reaching
	 *  less than an earlier one is expressible only by authoring it so; ladder-following homing
	 *  relies on reach not decreasing.
	 *
	 *  Returns a disabled wedge for an out-of-range index or a branch with bEnabled false.
	 */
	FTDAttackHitbox BuildAimAssistWedge(int32 BranchIndex) const;

private:

	/** Arms the next checkpoint, or runs it immediately if it is already due. */
	void ScheduleCheckpoint(float DelaySeconds);

	/** Escalates to the next branch if the button is still down, otherwise commits. */
	void HandleCheckpoint();

	/** Starts the coil from wherever the montage actually is, aimed at the position's CoilEndSeconds. */
	void EnterCoil();

	/** Locks in the selected branch: applies its tag, starts tracing, aims at its release. */
	void CommitAttack();

	/** Real seconds since this activation. */
	float GetElapsedSeconds() const;

	/** Stretches the release window to the cell's ReleaseSeconds. */
	UFUNCTION()
	void HandleReleaseWindowBegan(FGameplayEventData Payload);

	/**
	 *  Takes the release rate back off when the damaging window closes. Without it the rate derived
	 *  for the release stays applied for the rest of the montage, so follow-through and all of
	 *  recovery inherit a speed computed from how wide the notify happens to be authored.
	 */
	UFUNCTION()
	void HandleReleaseWindowEnded(FGameplayEventData Payload);

	/** The cell's authored ReleaseSeconds, so the trace task can close on time. */
	virtual float GetTraceWindowSeconds() const override;

	/** The trace task's closing edge. Routes to the same place the closing notify does. */
	virtual void HandleTraceWindowClosed() override;

	/** Takes the release rate off and starts recovery. Runs once per activation. */
	void CloseReleaseWindow();

	/**
	 *  Shared windup rate: fast enough that the first branch reaches the light cell's
	 *  ReleaseStartSeconds exactly on its ReleaseAtSeconds. Everything slower comes from a swap or
	 *  the coil holding it back, never from a later branch accelerating.
	 */
	float ComputeWindupPlayRate() const;

	/** The first branch's HoldUntilSeconds: where the tiers stop being indistinguishable. */
	virtual float GetBaseLungeDurationSeconds() const override;

	/** Total swings: the number of authored positions. */
	int32 GetSwingCount() const { return Positions.Num(); }

	/** Whether a swing at this index has a string successor to chain into. */
	bool HasSuccessorSwing(int32 SwingIndex) const { return SwingIndex + 1 < GetSwingCount(); }

	/** This position's cell for a branch, or null when either is not authored. */
	const FTDAttackCell* FindCell(int32 SwingIndex, int32 BranchIndex) const;

	/**
	 *  Where the montage now playing opens its Release Window: the swapped-in tier's own position
	 *  once one holds the slot, the light cell's otherwise.
	 */
	float GetSwingReleaseStartSeconds(int32 SwingIndex) const;

	/** This position's CoilEndSeconds. */
	float GetSwingCoilEndSeconds(int32 SwingIndex) const;

	/**
	 *  The socket an escalation to this branch swaps in: the cell, when it authors a montage and
	 *  the branch is not 0. Null means the tier stays on the light's clip.
	 */
	const FTDAttackCell* FindTierAnimation(int32 SwingIndex, int32 BranchIndex) const;

	/**
	 *  Swaps the escalated tier's montage in, blending from wherever the windup has reached. The
	 *  blend replaces the coil's rate freeze as the tell; the clip's own length is what gives the
	 *  hold somewhere to live.
	 */
	void StartTierMontage(const FTDAttackCell& Tier);

	/**
	 *  Per-cell readers, each falling back to the ability's own field when the cell is not authored.
	 *  They take the branch index explicitly rather than reading SelectedBranchIndex, because
	 *  BuildAimAssistWedge asks about branches other than the selected one.
	 */
	const TArray<FTDAttackHitbox>& GetSwingHitboxes(int32 SwingIndex, int32 BranchIndex) const;
	float GetSwingReleaseSeconds(int32 SwingIndex, int32 BranchIndex) const;
	float GetSwingLungeDistanceCm(int32 SwingIndex, int32 BranchIndex) const;
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

	/** True once this activation's release window has closed, so the second edge is inert. */
	bool bReleaseWindowClosed = false;
	float ActivationWorldTime = 0.0f;

	/** Which swing this activation is, fixed at activation. 0 is the first hit. */
	int32 CurrentSwingIndex = 0;

	/**
	 *  The tier montage now holding the slot, once one has been swapped in. Null means the light's
	 *  clip is still playing, which is every light and every unpopulated socket.
	 *
	 *  It is what GetActiveAttackMontage returns, so position, play rate, the Release Window filter
	 *  and the blend-out all follow it without each having to know a swap happened.
	 */
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveTierMontage = nullptr;

	/** The swapped-in montage's authored Release Window position. Only read while it is set. */
	float ActiveTierReleaseStart = 0.0f;

	/** True from RELEASE OFF to the ability's end -- the span the chain-out may open inside. */
	bool bInRecovery = false;

	/** The authored recovery start in world time -- activation plus ReleaseAtSeconds plus
	 *  ReleaseSeconds -- which ChainOpenAfterRecoverySeconds counts from. */
	float RecoveryStartedAt = 0.0f;

	/**
	 *  Set by TryChainOutForBufferedPress immediately before it ends the ability, so EndAbility can
	 *  tell a chain-out from a natural end -- both arrive through the same funnel, and only the
	 *  chain-out advances the string.
	 */
	bool bEndingViaChainOut = false;

	/**
	 *  How long this activation's press was already held before it activated, taken from the
	 *  character at activation. Shifts every ladder checkpoint earlier by that much, so the tier is
	 *  decided by the whole hold rather than the part after activation.
	 */
	float PriorHoldSeconds = 0.0f;

	FTimerHandle CheckpointTimerHandle;
	FGameplayTag AppliedAttackTag;
};
