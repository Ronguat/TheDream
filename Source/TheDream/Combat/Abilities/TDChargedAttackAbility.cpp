// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDChargedAttackAbility.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UTDChargedAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// Deliberately not Super: the base class starts tracing and plays the montage
	// immediately, whereas the branch -- and therefore the trace radius -- is not known
	// until the player releases.
	UTDGameplayAbility::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	SelectedBranchIndex = INDEX_NONE;
	bBranchResolved = false;
	AppliedAttackTag = FGameplayTag();

	UWorld* World = GetWorld();
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo) || !World || Branches.Num() == 0)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	HoldStartTime = World->GetTimeSeconds();

	// The montage runs Windup -> Hold (looping); resolving redirects it to a branch.
	if (!StartAttackMontage(WindupSection))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Holding forever would make the deepest attack free of risk, so it commits itself.
	if (MaxHoldSeconds > 0.0f)
	{
		World->GetTimerManager().SetTimer(
			MaxHoldTimerHandle,
			FTimerDelegate::CreateWeakLambda(this, [this]() { ResolveBranch(MaxHoldSeconds); }),
			MaxHoldSeconds,
			false);
	}
}

void UTDChargedAttackAbility::InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputReleased(Handle, ActorInfo, ActivationInfo);

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ResolveBranch(World->GetTimeSeconds() - HoldStartTime);
}

const FTDAttackBranch* UTDChargedAttackAbility::SelectBranch(float HeldSeconds) const
{
	const FTDAttackBranch* Best = nullptr;

	for (const FTDAttackBranch& Branch : Branches)
	{
		if (HeldSeconds >= Branch.MinHoldSeconds && (!Best || Branch.MinHoldSeconds >= Best->MinHoldSeconds))
		{
			Best = &Branch;
		}
	}

	// A release faster than the shortest branch still has to produce a swing.
	return Best ? Best : &Branches[0];
}

void UTDChargedAttackAbility::ResolveBranch(float HeldSeconds)
{
	if (bBranchResolved)
	{
		return;
	}
	bBranchResolved = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MaxHoldTimerHandle);
	}

	const FTDAttackBranch* Branch = SelectBranch(HeldSeconds);
	SelectedBranchIndex = Branches.IndexOfByPredicate([Branch](const FTDAttackBranch& Candidate)
	{
		return &Candidate == Branch;
	});

	// Surfaces the chosen attack on the debug HUD and lets other systems react to it.
	if (Branch->AttackTag.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			AppliedAttackTag = Branch->AttackTag;
			ASC->AddLooseGameplayTag(AppliedAttackTag);
		}
	}

	// Radius is per branch, so tracing can only start once the branch is known. The task
	// still idles until the branch section's Melee Window notify opens.
	StartMeleeTrace(GetAttackTraceRadius());

	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	if (!AnimInstance || !AttackMontage)
	{
		return;
	}

	// Mid-windup, queue the branch as the next section so it flows at the section boundary
	// with no visible pop. Already holding, jump straight there.
	const FName CurrentSection = AnimInstance->Montage_GetCurrentSection(AttackMontage);
	if (CurrentSection == WindupSection)
	{
		MontageSetNextSectionName(WindupSection, Branch->MontageSection);
	}
	else
	{
		MontageJumpToSection(Branch->MontageSection);
	}
}

float UTDChargedAttackAbility::GetAttackDamage() const
{
	return Branches.IsValidIndex(SelectedBranchIndex) ? Branches[SelectedBranchIndex].Damage : Damage;
}

float UTDChargedAttackAbility::GetAttackTraceRadius() const
{
	return Branches.IsValidIndex(SelectedBranchIndex) ? Branches[SelectedBranchIndex].TraceRadius : TraceRadius;
}

void UTDChargedAttackAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MaxHoldTimerHandle);
	}

	// Must come off even on cancellation, or the character reads as mid-attack forever.
	if (AppliedAttackTag.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->RemoveLooseGameplayTag(AppliedAttackTag);
		}
		AppliedAttackTag = FGameplayTag();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
