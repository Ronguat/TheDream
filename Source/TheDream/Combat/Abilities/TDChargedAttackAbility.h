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

	/** Montage section played when this branch is chosen. */
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
 *  Pressing starts a shared windup that is identical for every outcome, so the character
 *  commits instantly and the opponent cannot yet tell which attack is coming. Releasing
 *  during the windup gives the fastest branch; holding past it puts the character into a
 *  visible hold pose, which is the tell that makes the slower branches reactable.
 *
 *  WindupSeconds is therefore the most important number in the system: it is exactly how
 *  long an attack stays ambiguous, and it sets the whole reactability ladder.
 *
 *  Each branch section carries its own Melee Window notify, so tracing and damage work
 *  unchanged from the single-swing case -- only the numbers differ per branch.
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

	/** Section played on activation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Charge")
	FName WindupSection = FName("Windup");

	/** Looping section entered if the windup finishes while the button is still down. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Charge")
	FName HoldSection = FName("Hold");

	/** Outcomes, ordered shortest hold first. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Charge")
	TArray<FTDAttackBranch> Branches;

	virtual float GetAttackDamage() const override;
	virtual float GetAttackTraceRadius() const override;

private:

	/** Picks a branch from the hold duration, redirects the montage and starts tracing. */
	void ResolveBranch(float HeldSeconds);

	/** The longest branch whose MinHoldSeconds has been reached. */
	const FTDAttackBranch* SelectBranch(float HeldSeconds) const;

	float HoldStartTime = 0.0f;
	int32 SelectedBranchIndex = INDEX_NONE;
	bool bBranchResolved = false;

	FTimerHandle MaxHoldTimerHandle;
	FGameplayTag AppliedAttackTag;
};
