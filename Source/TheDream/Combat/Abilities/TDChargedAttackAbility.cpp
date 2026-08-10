// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDChargedAttackAbility.h"
#include "Combat/TDCombatDebug.h"
#include "Combat/TDGameplayTags.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
	/** Play rate floor. Zero would stop the montage, which banks time and then spends it in one frame. */
	constexpr float TDMinPlayRate = 0.01f;

	/** How far the notify may sit from ReleaseStartSeconds before it is worth complaining about. */
	constexpr float TDReleaseStartTolerance = 0.03f;

	/** The rate actually in force on the montage, so a rate that failed to apply is visible. */
	float ActualMontageRate(const FGameplayAbilityActorInfo* ActorInfo, UAnimMontage* Montage)
	{
		UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
		const FAnimMontageInstance* Instance = (AnimInstance && Montage) ? AnimInstance->GetActiveInstanceForMontage(Montage) : nullptr;
		return Instance ? Instance->GetPlayRate() : -1.0f;
	}
}

void UTDChargedAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// Deliberately not Super: the base class starts tracing immediately, whereas the
	// branch -- and therefore the trace radius -- is unknown until the attack commits.
	UTDGameplayAbility::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	SelectedBranchIndex = 0;
	bAttackCommitted = false;
	bInputHeld = true;
	bCoiling = false;
	AppliedAttackTag = FGameplayTag();

	UWorld* World = GetWorld();
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo) || !World || Branches.Num() == 0)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActivationWorldTime = World->GetTimeSeconds();

	if (!StartAttackMontage(WindupSection))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// The shared windup runs at whatever rate the *fastest* branch needs. Slower branches
	// are made slower by the coil holding them back, not by this being slow.
	const float WindupRate = ComputeWindupPlayRate();
	SetMontagePlayRate(WindupRate);

	// applied should match wanted. If it reads 1.000 the montage was not yet playing when
	// the rate was set, and the whole windup is running at the wrong speed.
	TD_TIMING_LOG(TEXT("[%.3f] ACTIVATE   pos=%.4f windupRate wanted=%.3f applied=%.3f"),
		World->GetTimeSeconds(), GetMontagePosition(), WindupRate,
		ActualMontageRate(GetCurrentActorInfo(), AttackMontage));

	ScheduleCheckpoint(Branches[0].HoldUntilSeconds);
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

	// Measured against activation rather than chained, so a late timer cannot push every
	// subsequent checkpoint further out.
	const float Remaining = DelaySeconds - GetElapsedSeconds();
	if (Remaining <= 0.0f)
	{
		HandleCheckpoint();
		return;
	}

	World->GetTimerManager().SetTimer(
		CheckpointTimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this]() { HandleCheckpoint(); }),
		Remaining,
		false);
}

void UTDChargedAttackAbility::HandleCheckpoint()
{
	if (bAttackCommitted)
	{
		return;
	}

	const int32 NextIndex = SelectedBranchIndex + 1;

	// Still holding escalates to the next branch. The deepest branch has nothing to
	// escalate to, so holding forever commits it anyway -- an attack that could be held
	// indefinitely would be free of risk.
	if (bInputHeld && Branches.IsValidIndex(NextIndex))
	{
		SelectedBranchIndex = NextIndex;

		// Leaving the first branch behind is exactly the moment the attack stops being a
		// light, which is the moment it earns a tell.
		if (!bCoiling)
		{
			EnterCoil();
		}

		TD_TIMING_LOG(TEXT("[%.3f] ESCALATE   -> branch %d  pos=%.4f"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, NextIndex, GetMontagePosition());

		ScheduleCheckpoint(Branches[NextIndex].HoldUntilSeconds);
		return;
	}

	CommitAttack();
}

void UTDChargedAttackAbility::EnterCoil()
{
	bCoiling = true;

	// Measured, not assumed. The checkpoint timer fires a frame or two late, so the
	// montage is always a little past where the maths would have put it; deriving the
	// rate from the assumed position compounds that error across the whole coil until it
	// overruns the release window.
	const float CurrentPosition = GetMontagePosition();
	const float CoilDistance = CoilEndSeconds - CurrentPosition;
	const float CoilDuration = Branches.Last().HoldUntilSeconds - GetElapsedSeconds();

	if (CurrentPosition < 0.0f || CoilDistance <= 0.0f || CoilDuration <= 0.0f)
	{
		// Nowhere to creep to, or no time to do it in. Carrying on at the windup rate is
		// a poor swing; stopping would be far worse, so there is no zero-rate branch here.
		// Ungated: a coil that never runs means a held attack races into its own release
		// window, which reads as an attack that simply does nothing.
		UE_LOG(LogTDCombatTiming, Warning, TEXT("Coil skipped: pos=%.4f distance=%.4f duration=%.4f"),
			CurrentPosition, CoilDistance, CoilDuration);
		return;
	}

	const float CoilRate = FMath::Max(CoilDistance / CoilDuration, TDMinPlayRate);
	SetMontagePlayRate(CoilRate);

	TD_TIMING_LOG(TEXT("[%.3f] COIL START pos=%.4f rate=%.3f (derived)"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, CurrentPosition, CoilRate);
}

void UTDChargedAttackAbility::CommitAttack()
{
	if (bAttackCommitted || !Branches.IsValidIndex(SelectedBranchIndex))
	{
		return;
	}
	bAttackCommitted = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CheckpointTimerHandle);
	}

	const FTDAttackBranch& Branch = Branches[SelectedBranchIndex];

	// Surfaces the chosen attack on the debug HUD and lets other systems react to it.
	// Reading this tag back is the intended way to confirm which branch was thrown.
	if (Branch.AttackTag.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			AppliedAttackTag = Branch.AttackTag;
			ASC->AddLooseGameplayTag(AppliedAttackTag);
		}
	}

	// Radius is per branch, so tracing can only start once the branch is known. Starting
	// it here is also what guarantees a listener exists before the window opens -- which
	// is why CoilEndSeconds must stay below ReleaseStartSeconds.
	StartMeleeTrace(GetAttackTraceRadius());

	// The window's own length is only knowable once the notify fires, so the ability waits
	// for it rather than duplicating the timeline.
	if (UAbilityTask_WaitGameplayEvent* WaitTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TDTags::Event_Melee_WindowBegin, nullptr, true, true))
	{
		WaitTask->EventReceived.AddDynamic(this, &UTDChargedAttackAbility::HandleReleaseWindowBegan);
		WaitTask->ReadyForActivation();
	}

	// Carry the montage from wherever it actually is into the strike, arriving exactly on
	// this branch's ReleaseAtSeconds. For the fastest branch this works out to the windup
	// rate it was already running, so the light never changes pace at all.
	const float CurrentPosition = GetMontagePosition();
	const float Distance = ReleaseStartSeconds - CurrentPosition;
	const float Remaining = Branch.ReleaseAtSeconds - GetElapsedSeconds();
	const float CommitRate = (Distance > 0.0f && Remaining > 0.0f)
		? FMath::Max(Distance / Remaining, TDMinPlayRate)
		: 1.0f;

	SetMontagePlayRate(CommitRate);

	TD_TIMING_LOG(TEXT("[%.3f] COMMIT     branch %d (%s) pos=%.4f rate=%.3f targetRelease=%.3f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, SelectedBranchIndex,
		*Branch.AttackTag.ToString(), CurrentPosition, CommitRate, Branch.ReleaseAtSeconds);

	// Only used once a branch earns a distinct release animation.
	if (Branch.MontageSection != NAME_None)
	{
		MontageJumpToSection(Branch.MontageSection);
	}
}

void UTDChargedAttackAbility::HandleReleaseWindowBegan(FGameplayEventData Payload)
{
	if (!Branches.IsValidIndex(SelectedBranchIndex))
	{
		return;
	}

	const FTDAttackBranch& Branch = Branches[SelectedBranchIndex];
	const float ActualStart = GetMontagePosition();

	// ReleaseStartSeconds is hand-copied from the notify's placement, so it can silently
	// drift if the montage is re-authored. This is the only moment the truth is available.
	if (ActualStart >= 0.0f && FMath::Abs(ActualStart - ReleaseStartSeconds) > TDReleaseStartTolerance)
	{
		// Ungated: this is hand-copied data having silently drifted, and every rate the
		// ability derives is computed from it.
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("Release Window opened at %.4f but ReleaseStartSeconds is %.4f. Update it to match the notify."),
			ActualStart, ReleaseStartSeconds);
	}

	// The notify reports its own length, so the window can be stretched to the authored
	// duration without anyone maintaining a copy of the timeline.
	const float WindowLength = Payload.EventMagnitude;
	if (WindowLength <= 0.0f || Branch.ReleaseSeconds <= 0.0f)
	{
		return;
	}

	const float ReleaseRate = FMath::Max(WindowLength / Branch.ReleaseSeconds, TDMinPlayRate);
	SetMontagePlayRate(ReleaseRate);

	TD_TIMING_LOG(TEXT("[%.3f] RELEASE    pos=%.4f windowLen=%.4f rate=%.3f (want %.3fs)"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, ActualStart, WindowLength, ReleaseRate, Branch.ReleaseSeconds);
}

float UTDChargedAttackAbility::ComputeWindupPlayRate() const
{
	if (Branches.Num() == 0 || Branches[0].ReleaseAtSeconds <= 0.0f || ReleaseStartSeconds <= 0.0f)
	{
		return 1.0f;
	}

	return FMath::Max(ReleaseStartSeconds / Branches[0].ReleaseAtSeconds, TDMinPlayRate);
}

float UTDChargedAttackAbility::GetElapsedSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() - ActivationWorldTime : 0.0f;
}

float UTDChargedAttackAbility::GetMontagePosition() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	return (AnimInstance && AttackMontage) ? AnimInstance->Montage_GetPosition(AttackMontage) : -1.0f;
}

void UTDChargedAttackAbility::SetMontagePlayRate(float PlayRate) const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	if (AnimInstance && AttackMontage && AnimInstance->Montage_IsPlaying(AttackMontage))
	{
		AnimInstance->Montage_SetPlayRate(AttackMontage, FMath::Max(PlayRate, TDMinPlayRate));
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
		World->GetTimerManager().ClearTimer(CheckpointTimerHandle);
	}

	// A cancelled attack would otherwise leave the montage crawling at the coil rate.
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
