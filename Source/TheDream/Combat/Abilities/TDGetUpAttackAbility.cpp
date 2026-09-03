// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDGetUpAttackAbility.h"
#include "Combat/TDCombatCharacter.h"
#include "Combat/TDCombatDebug.h"
#include "Combat/TDGameplayTags.h"
#include "Core/TheDreamCharacter.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"

namespace
{
	const FName TDCommittedTagName(TEXT("State.Attacking.Committed"));
}

UTDGetUpAttackAbility::UTDGetUpAttackAbility()
{
	bAllowedFromKnockdown = true;
	bLocksMovement = true;

	Damage = 10.0f;
	StaminaDamage = 10.0f;
	BlockstunSeconds = 0.65f;
	HitstunSeconds = 1.0f;
	HitSpacingCm = 250.0f;
	BlockedSpacingCm = 100.0f;
	LungeDistanceCm = 0.0f;

	// The ender's volume: a full circle at the light's reach.
	FTDAttackHitbox Volume;
	Volume.MinReachCm = 0.0f;
	Volume.MaxReachCm = 150.0f;
	Volume.ArcDegrees = 360.0f;
	Volume.ArcCentreDegrees = 0.0f;
	Volume.HeightMinCm = -70.0f;
	Volume.HeightMaxCm = 70.0f;
	Hitboxes = { Volume };
}

bool UTDGetUpAttackAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	// Only from the floor, and silently: a standing attack press belongs to GA_Attack, whose own
	// refusals are the ones the trace should carry.
	const ATDCombatCharacter* Downed = ActorInfo ? Cast<ATDCombatCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!Downed || !Downed->IsKnockedDown())
	{
		return false;
	}
	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void UTDGetUpAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// Past the melee base's activation: that one lunges and plays at rate 1.
	UTDGameplayAbility::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	const UWorld* World = GetWorld();
	ActivationWorldTime = World ? World->GetTimeSeconds() : 0.0f;
	bParried = false;

	SetCommitted(true);

	if (ATheDreamCharacter* Character = GetFacingCharacter())
	{
		Character->SetAbilityFacingLocked(true);
	}

	bReleaseWindowClosed = false;
	StartMeleeTrace(GetAttackHitboxes());

	// Registers the volume with the character: the AIM WEDGE line and the debug draw, homing off.
	if (ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
	{
		const TArray<FTDAttackHitbox>& Volumes = GetAttackHitboxes();
		CombatCharacter->SetAimAssistHoming(
			Volumes.Num() > 0 ? Volumes[0] : FTDAttackHitbox::MakeDisabled(), TargetImmunityTags, false, bDrawDebugTrace);
	}

	if (UAbilityTask_WaitGameplayEvent* BeginTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TDTags::Event_Melee_WindowBegin, nullptr, true))
	{
		BeginTask->EventReceived.AddDynamic(this, &UTDGetUpAttackAbility::HandleReleaseWindowBegan);
		BeginTask->ReadyForActivation();
	}

	if (UAbilityTask_WaitGameplayEvent* EndTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TDTags::Event_Melee_WindowEnd, nullptr, true))
	{
		EndTask->EventReceived.AddDynamic(this, &UTDGetUpAttackAbility::HandleReleaseWindowEnded);
		EndTask->ReadyForActivation();
	}

	const float WindupRate = (ReleaseStartSeconds > 0.0f && WindupSeconds > 0.0f)
		? FMath::Max(ReleaseStartSeconds / WindupSeconds, TDMinPlayRate)
		: 1.0f;
	if (!StartAttackMontage(NAME_None, WindupRate))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	TD_TIMING_LOG(TEXT("[%.3f] ACTIVATE   %s swing=0 pos=%.4f windupRate wanted=%.3f applied=%.3f"),
		ActivationWorldTime, *GetNameSafe(GetAvatarActorFromActorInfo()),
		GetMontagePosition(), WindupRate, WindupRate);
}

void UTDGetUpAttackAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	SetCommitted(false);

	if (ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
	{
		CombatCharacter->SetAimAssistHoming(FTDAttackHitbox::MakeDisabled(), FGameplayTagContainer(), false, false);
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	TD_TIMING_LOG(TEXT("[%.3f] ABILITY END  %s %s pos=%.4f elapsed=%.3f%s"),
		Now, *GetNameSafe(GetAvatarActorFromActorInfo()), *GetName(), GetMontagePosition(), Now - ActivationWorldTime,
		bWasCancelled ? TEXT(" (cancelled)") : TEXT(""));

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

float UTDGetUpAttackAbility::GetAttackParryLockoutSeconds() const
{
	const UWorld* World = GetWorld();
	const float Elapsed = World ? World->GetTimeSeconds() - ActivationWorldTime : 0.0f;
	return FMath::Max(0.0f, WindupSeconds + ReleaseSeconds + RecoverySeconds - Elapsed);
}

bool UTDGetUpAttackAbility::ShouldBufferFailedInput(const FGameplayAbilityActorInfo* ActorInfo) const
{
	const UAbilitySystemComponent* ASC = (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
		? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC || !ASC->HasMatchingGameplayTag(TDTags::State_KnockedDown))
	{
		return false;
	}
	return Super::ShouldBufferFailedInput(ActorInfo);
}

void UTDGetUpAttackAbility::ReleaseCommitmentTag()
{
	SetCommitted(false);
}

void UTDGetUpAttackAbility::HandleReleaseWindowBegan(FGameplayEventData Payload)
{
	if (!IsWindowForThisAttack(Payload))
	{
		return;
	}

	const float ActualStart = GetMontagePosition();

	if (ATheDreamCharacter* Character = GetFacingCharacter())
	{
		Character->SetAbilityFacingLocked(true);
	}

	if (ActualStart >= 0.0f && FMath::Abs(ActualStart - ReleaseStartSeconds) > TDReleaseStartTolerance)
	{
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("Release Window opened at %.4f but ReleaseStartSeconds is %.4f. Update it to match the notify."),
			ActualStart, ReleaseStartSeconds);
	}

	const float WindowEnd = Payload.EventMagnitude;
	if (WindowEnd <= 0.0f || ReleaseSeconds <= 0.0f)
	{
		return;
	}

	// Rate from the *remaining* window at the measured position, not the authored length: the
	// notify opens up to a frame late at this windup rate, and the authored-length formula -- the
	// base's and the charged ability's, whose ~10 ms loss at 1x sits inside the s1 bands -- plays
	// the release 25-45 ms short here. Falls back to the authored length when the position is
	// unreadable.
	const float Remaining = WindowEnd - ((ActualStart >= 0.0f) ? ActualStart : ReleaseStartSeconds);
	if (Remaining <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// The ladder's fit; see UTDChargedAttackAbility::HandleReleaseWindowBegan.
	const float Tick = GetWorld() ? GetWorld()->GetDeltaSeconds() : (1.0f / 60.0f);
	const float ReleaseRate = FMath::Max(Remaining / FMath::Max(ReleaseSeconds - 1.5f * Tick, Tick), TDMinPlayRate);
	SetMontagePlayRate(ReleaseRate);

	TD_TIMING_LOG(TEXT("[%.3f] RELEASE    %s pos=%.4f windowEnd=%.4f remaining=%.4f rate=%.3f (want %.3fs)"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, *GetNameSafe(GetAvatarActorFromActorInfo()), ActualStart, WindowEnd, Remaining, ReleaseRate, ReleaseSeconds);
}

void UTDGetUpAttackAbility::HandleTraceWindowClosed()
{
	CloseReleaseWindow();
}

void UTDGetUpAttackAbility::HandleReleaseWindowEnded(FGameplayEventData Payload)
{
	if (!IsWindowForThisAttack(Payload))
	{
		return;
	}

	CloseReleaseWindow();
}

void UTDGetUpAttackAbility::CloseReleaseWindow()
{
	// Both the trace task's deadline and the closing notify arrive; only the first may run.
	// Recovery derives its rate from where the montage is now, so a second pass would
	// re-derive it from a later position.
	if (bReleaseWindowClosed)
	{
		return;
	}
	bReleaseWindowClosed = true;

	const float RecoveryFrom = GetMontagePosition();
	const float RecoveryRate = ComputeRecoveryPlayRate(RecoveryFrom, RecoverySeconds);
	if (RecoveryRate > 0.0f)
	{
		SetMontagePlayRate(RecoveryRate);
	}
	else
	{
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("Recovery has no montage left: window closed at %.4f, blend-out begins at %.4f. ")
			TEXT("RecoverySeconds %.3f cannot be honoured -- the clip's tail is too short."),
			RecoveryFrom, GetBlendOutStartSeconds(1.0f), RecoverySeconds);
	}

	TD_TIMING_LOG(TEXT("[%.3f] RELEASE OFF  %s pos=%.4f rate=%.3f (want %.3fs to blendOut %.4f)"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, *GetNameSafe(GetAvatarActorFromActorInfo()), RecoveryFrom, RecoveryRate,
		RecoverySeconds, GetBlendOutStartSeconds(RecoveryRate));
}

void UTDGetUpAttackAbility::SetCommitted(bool bCommitted)
{
	if (bCommitted == bCommittedTagApplied)
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TDCommittedTagName, false);
	if (!ASC || !Tag.IsValid())
	{
		return;
	}

	if (bCommitted)
	{
		ASC->AddLooseGameplayTag(Tag);
	}
	else
	{
		ASC->RemoveLooseGameplayTag(Tag);
	}
	bCommittedTagApplied = bCommitted;
}
