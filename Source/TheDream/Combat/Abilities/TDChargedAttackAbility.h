// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDMeleeAttackAbility.h"
#include "Engine/TimerHandle.h"
#include "TDChargedAttackAbility.generated.h"

/**
 *  One outcome of a held attack: how long its windup runs, and how hard it hits.
 */
USTRUCT(BlueprintType)
struct FTDAttackBranch
{
	GENERATED_BODY()

	/** Identifies the attack, e.g. Ability.Attack.Heavy. Applied as a loose tag while it swings. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	FGameplayTag AttackTag;

	/**
	 *  Optional distinct release animation for this branch.
	 *
	 *  Leave as None and the montage simply plays on from the windup, so every branch
	 *  shares one release. Setting it buys readability at the direct cost of this
	 *  branch's ambiguity -- a defender who can recognise the animation no longer has
	 *  to wait out the coil to know what is coming.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	FName MontageSection = NAME_None;

	/**
	 *  How long this branch's windup lasts, measured from the press.
	 *
	 *  Doubles as the checkpoint this branch is escalated away from: still holding when
	 *  it elapses and the attack becomes the next branch instead, taking on that
	 *  branch's longer windup.
	 *
	 *  Releasing early does not shorten it. The windup always runs its full length, and
	 *  that fixed cost is the whole point -- resolving at the moment of release let a
	 *  251 ms hold produce a heavy that came out at 251 ms, which left the light with no
	 *  reason to exist.
	 *
	 *  Branches must be ordered shortest windup first.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float WindupSeconds = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float Damage = 15.0f;

	/** Heavier attacks reach further, so radius is per branch rather than per ability. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="1.0"))
	float TraceRadius = 45.0f;
};

/**
 *  An attack whose identity is decided by how long the button is held.
 *
 *  Each branch's WindupSeconds is a checkpoint. Still holding when one elapses and the
 *  attack escalates to the next branch and its longer windup; already released and the
 *  attack commits there, at whatever branch it had reached. The last branch commits
 *  whether or not the button is still down. Releasing anywhere inside a band changes
 *  nothing, which is what makes the windups preset rather than dynamic.
 *
 *  There is deliberately one animation rather than one per branch. The attack's identity
 *  is a *consequence* of how long its windup lasted, so it cannot be known in advance to
 *  pick a clip -- and more importantly, sharing it means the defender cannot tell the
 *  branches apart until the coil appears. Reactability is measured from that tell, not
 *  from the press, so the shared windup is what keeps the reaction window short.
 *
 *  See Docs/Combat/Decisions.md for the reasoning behind both.
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
	 *  Point in the montage where the swing is fully coiled and the coil begins.
	 *
	 *  An animation landmark, deliberately distinct from any branch's WindupSeconds,
	 *  which are design thresholds. They need not line up, and a branch that commits
	 *  before this point simply never coils -- which is exactly why the light has no
	 *  tell.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Coil", meta=(ClampMin="0.0"))
	float CoilStartSeconds = 0.3f;

	/**
	 *  Montage position the coil creeps toward and will not pass.
	 *
	 *  Uncapped, a long hold walks the montage into its own release window and the attack
	 *  fires with part of its active frames already spent. Keep this just short of the
	 *  first frame of the Release Window notify.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Coil", meta=(ClampMin="0.0"))
	float CoilCeilingSeconds = 0.35f;

	/**
	 *  Play rate while coiling.
	 *
	 *  0 freezes on the coiled pose; a small value keeps it slowly winding, which reads
	 *  as tension rather than as a hitch. The difference between looking stalled and
	 *  looking loaded.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Coil", meta=(ClampMin="0.0", ClampMax="1.0"))
	float CoilPlayRate = 0.1f;

	/** Optional section played on activation. None starts the montage from the beginning. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Windup")
	FName WindupSection = NAME_None;

	/** Outcomes, ordered shortest windup first. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Windup")
	TArray<FTDAttackBranch> Branches;

	virtual float GetAttackDamage() const override;
	virtual float GetAttackTraceRadius() const override;

private:

	/** Arms the next checkpoint, or runs it immediately if it is already due. */
	void ScheduleCheckpoint(float DelaySeconds);

	/** Escalates to the next branch if the button is still down, otherwise commits. */
	void HandleCheckpoint();

	/** Locks in the selected branch: applies its tag, starts tracing, leaves the coil. */
	void CommitAttack();

	/** Slows the montage once the swing is fully coiled, and caps how far it may creep. */
	void EnterCoil();

	void ClearAllTimers();

	/** Sets the montage's play rate, if one is playing. */
	void SetMontagePlayRate(float PlayRate) const;

	int32 SelectedBranchIndex = 0;
	bool bAttackCommitted = false;
	bool bInputHeld = true;

	FTimerHandle CheckpointTimerHandle;
	FTimerHandle CoilTimerHandle;
	FTimerHandle CoilCeilingTimerHandle;
	FGameplayTag AppliedAttackTag;
};
