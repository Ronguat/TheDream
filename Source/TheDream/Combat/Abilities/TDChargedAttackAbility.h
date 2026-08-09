// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDMeleeAttackAbility.h"
#include "Engine/TimerHandle.h"
#include "TDChargedAttackAbility.generated.h"

/**
 *  One outcome of a held attack: which section plays, how long it must be held, and how hard it hits.
 */
USTRUCT(BlueprintType)
struct FTDAttackBranch
{
	GENERATED_BODY()

	/** Identifies the attack, e.g. Ability.Attack.Heavy. Applied as a loose tag while it swings. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	FGameplayTag AttackTag;

	/**
	 *  Optional distinct strike section for this branch.
	 *
	 *  Leave as None and the montage simply resumes from the windup, so every branch
	 *  shares one strike and one set of impact frames. Set it once a branch earns its
	 *  own animation -- a data change, no code change.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	FName MontageSection = NAME_None;

	/** Shortest hold that selects this branch. The longest eligible branch wins. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float MinHoldSeconds = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float Damage = 15.0f;

	/** Heavier attacks reach further, so radius is per branch rather than per ability. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="1.0"))
	float TraceRadius = 45.0f;
};

/**
 *  An attack whose identity is decided by how long the button is held.
 *
 *  Pressing starts a windup that is identical for every outcome, so the character commits
 *  instantly and the opponent cannot yet tell which attack is coming. Releasing during the
 *  windup gives the fastest branch; holding past it slows the montage to HoldPlayRate, and
 *  that visible coil is the tell that makes the slower branches reactable.
 *
 *  There is deliberately one animation rather than one per branch. The attack's identity is
 *  a *consequence* of how long the windup lasted, so it cannot be known in advance to pick
 *  a clip; and sharing the strike means all three attacks land on the same impact frames,
 *  which keeps hit timing predictable for the defender regardless of what was thrown.
 *
 *  WindupSeconds is the most important number in the system: it is exactly how long an
 *  attack stays ambiguous, and it sets the whole reactability ladder.
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

	/** Ambiguous startup, shared by every branch. Also the window in which a release picks the fastest branch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Charge", meta=(ClampMin="0.0"))
	float WindupSeconds = 0.25f;

	/**
	 *  Held this long without releasing and the attack commits on its own, at the deepest branch.
	 *  Defaults to the Charged threshold, so crossing into Charged fires it immediately.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Charge", meta=(ClampMin="0.0"))
	float MaxHoldSeconds = 0.5f;

	/**
	 *  Point in the montage where the swing is fully coiled and holding begins.
	 *
	 *  Distinct from WindupSeconds on purpose: this is an animation landmark, that is a
	 *  design threshold. They need not match, and releasing between them is harmless.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Charge", meta=(ClampMin="0.0"))
	float WindupAnimEndSeconds = 0.3f;

	/**
	 *  Play rate while the button is held past the coil point.
	 *
	 *  0 freezes on the coiled pose; a small value keeps it slowly winding, which reads as
	 *  tension rather than as a hitch. Worth trying both -- it is the difference between
	 *  looking stalled and looking loaded.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Charge", meta=(ClampMin="0.0", ClampMax="1.0"))
	float HoldPlayRate = 0.1f;

	/** Optional section played on activation. None starts the montage from the beginning. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Charge")
	FName WindupSection = NAME_None;

	/** Outcomes, ordered shortest hold first. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Charge")
	TArray<FTDAttackBranch> Branches;

	virtual float GetAttackDamage() const override;
	virtual float GetAttackTraceRadius() const override;

private:

	/** Picks a branch from the hold duration, restores play rate and starts tracing. */
	void ResolveBranch(float HeldSeconds);

	/** The longest branch whose MinHoldSeconds has been reached. */
	const FTDAttackBranch* SelectBranch(float HeldSeconds) const;

	/** Slows the montage to HoldPlayRate if the button is still down at the coil point. */
	void EnterHold();

	/** Sets the montage's play rate, if one is playing. */
	void SetMontagePlayRate(float PlayRate) const;

	float HoldStartTime = 0.0f;
	int32 SelectedBranchIndex = INDEX_NONE;
	bool bBranchResolved = false;

	FTimerHandle HoldTimerHandle;
	FTimerHandle MaxHoldTimerHandle;
	FGameplayTag AppliedAttackTag;
};
