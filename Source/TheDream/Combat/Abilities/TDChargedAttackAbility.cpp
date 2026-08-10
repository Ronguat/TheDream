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
	// branch -- and therefore the trace radius -- is unknown until the attack commits.
	UTDGameplayAbility::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	SelectedBranchIndex = 0;
	bAttackCommitted = false;
	bInputHeld = true;
	AppliedAttackTag = FGameplayTag();

	UWorld* World = GetWorld();
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo) || !World || Branches.Num() == 0)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!StartAttackMontage(WindupSection))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// The coil is an animation landmark, so it is scheduled off the montage rather than
	// off any branch. A branch that commits before it never coils, and therefore never
	// gives the defender a tell.
	if (CoilStartSeconds > 0.0f)
	{
		World->GetTimerManager().SetTimer(
			CoilTimerHandle,
			FTimerDelegate::CreateWeakLambda(this, [this]() { EnterCoil(); }),
			CoilStartSeconds,
			false);
	}
	else
	{
		EnterCoil();
	}

	ScheduleCheckpoint(Branches[0].WindupSeconds);
}

void UTDChargedAttackAbility::InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputReleased(Handle, ActorInfo, ActivationInfo);

	// Deliberately does not commit anything. The windup runs its preset length either
	// way; the pending checkpoint reads this and decides whether to escalate or commit.
	// Letting go early is what selects the branch, not what fires it.
	bInputHeld = false;
}

void UTDChargedAttackAbility::ScheduleCheckpoint(float DelaySeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// A branch with a zero-length windup is due the moment it is selected.
	if (DelaySeconds <= 0.0f)
	{
		HandleCheckpoint();
		return;
	}

	World->GetTimerManager().SetTimer(
		CheckpointTimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this]() { HandleCheckpoint(); }),
		DelaySeconds,
		false);
}

void UTDChargedAttackAbility::HandleCheckpoint()
{
	if (bAttackCommitted)
	{
		return;
	}

	const int32 NextIndex = SelectedBranchIndex + 1;

	// Still holding escalates to the next branch and its longer windup. The deepest
	// branch has nothing to escalate to, so holding forever commits it anyway -- an
	// attack that could be held indefinitely would be free of risk.
	if (bInputHeld && Branches.IsValidIndex(NextIndex))
	{
		const float RemainingWindup = Branches[NextIndex].WindupSeconds - Branches[SelectedBranchIndex].WindupSeconds;
		SelectedBranchIndex = NextIndex;
		ScheduleCheckpoint(FMath::Max(RemainingWindup, 0.0f));
		return;
	}

	CommitAttack();
}

void UTDChargedAttackAbility::EnterCoil()
{
	if (bAttackCommitted)
	{
		return;
	}

	SetMontagePlayRate(CoilPlayRate);

	// A frozen coil cannot creep anywhere, so it needs no ceiling.
	UWorld* World = GetWorld();
	if (!World || CoilPlayRate <= 0.0f)
	{
		return;
	}

	// Creeping without a ceiling walks the montage into its own release window, and the
	// attack then fires with part of its active frames already spent. Long holds are the
	// only ones that ever reach the cap.
	const float CreepSeconds = CoilCeilingSeconds - CoilStartSeconds;
	if (CreepSeconds <= 0.0f)
	{
		SetMontagePlayRate(0.0f);
		return;
	}

	World->GetTimerManager().SetTimer(
		CoilCeilingTimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!bAttackCommitted)
			{
				SetMontagePlayRate(0.0f);
			}
		}),
		CreepSeconds / CoilPlayRate,
		false);
}

void UTDChargedAttackAbility::CommitAttack()
{
	if (bAttackCommitted || !Branches.IsValidIndex(SelectedBranchIndex))
	{
		return;
	}
	bAttackCommitted = true;

	ClearAllTimers();

	const FTDAttackBranch& Branch = Branches[SelectedBranchIndex];

	// Surfaces the chosen attack on the debug HUD and lets other systems react to it.
	// This tag is the intended way to confirm which branch was thrown -- reading it back
	// beats inferring the answer from the animation.
	if (Branch.AttackTag.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			AppliedAttackTag = Branch.AttackTag;
			ASC->AddLooseGameplayTag(AppliedAttackTag);
		}
	}

	// Radius is per branch, so tracing can only start once the branch is known. The task
	// still idles until the montage's Release Window notify opens.
	StartMeleeTrace(GetAttackTraceRadius());

	// Out of the coil at full speed.
	SetMontagePlayRate(1.0f);

	// Only used once a branch earns a distinct release animation.
	if (Branch.MontageSection != NAME_None)
	{
		MontageJumpToSection(Branch.MontageSection);
	}
}

void UTDChargedAttackAbility::ClearAllTimers()
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& Timers = World->GetTimerManager();
		Timers.ClearTimer(CheckpointTimerHandle);
		Timers.ClearTimer(CoilTimerHandle);
		Timers.ClearTimer(CoilCeilingTimerHandle);
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
	ClearAllTimers();

	// A cancelled attack would otherwise leave the montage crawling at CoilPlayRate.
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
