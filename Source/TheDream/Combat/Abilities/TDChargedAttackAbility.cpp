// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDChargedAttackAbility.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UTDChargedAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// Deliberately not Super: the base class starts tracing immediately, whereas the
	// branch -- and therefore the trace radius -- is unknown until the player releases.
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

	if (!StartAttackMontage(WindupSection))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FTimerManager& Timers = World->GetTimerManager();

	// Reaching the coil point with the button still down slows the swing rather than
	// letting it run on into the strike.
	if (WindupAnimEndSeconds > 0.0f)
	{
		Timers.SetTimer(
			HoldTimerHandle,
			FTimerDelegate::CreateWeakLambda(this, [this]() { EnterHold(); }),
			WindupAnimEndSeconds,
			false);
	}

	// Holding forever would make the deepest attack free of risk, so it commits itself.
	if (MaxHoldSeconds > 0.0f)
	{
		Timers.SetTimer(
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

void UTDChargedAttackAbility::EnterHold()
{
	if (bBranchResolved)
	{
		return;
	}

	SetMontagePlayRate(HoldPlayRate);
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
		World->GetTimerManager().ClearTimer(HoldTimerHandle);
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
	// still idles until the montage's Melee Window notify opens.
	StartMeleeTrace(GetAttackTraceRadius());

	// Back to full speed: the strike and its impact frames play identically for every
	// branch, so hit timing reads the same to the defender no matter what was thrown.
	SetMontagePlayRate(1.0f);

	// Only used once a branch earns a distinct strike animation.
	if (Branch->MontageSection != NAME_None)
	{
		MontageJumpToSection(Branch->MontageSection);
	}
}

void UTDChargedAttackAbility::SetMontagePlayRate(float PlayRate) const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	if (AnimInstance && AttackMontage && AnimInstance->Montage_IsPlaying(AttackMontage))
	{
		AnimInstance->Montage_SetPlayRate(AttackMontage, PlayRate);
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
		World->GetTimerManager().ClearTimer(HoldTimerHandle);
		World->GetTimerManager().ClearTimer(MaxHoldTimerHandle);
	}

	// A cancelled charge would otherwise leave the montage crawling at HoldPlayRate.
	SetMontagePlayRate(1.0f);

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
