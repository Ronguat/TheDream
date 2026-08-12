// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDMeleeAttackAbility.h"
#include "Combat/Tasks/AbilityTask_MeleeTrace.h"
#include "Combat/TDGameplayTags.h"
#include "Combat/TDCombatDebug.h"
#include "Core/TheDreamCharacter.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

void UTDMeleeAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	StartMeleeTrace(GetAttackHitboxes());

	// A plain swing has no derived timing, so it plays at the montage's authored speed.
	if (!StartAttackMontage(NAME_None, 1.0f))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
	}
}

void UTDMeleeAttackAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// Faded rather than restored instantly, so a cancel does not snap facing back any more
	// abruptly than the attack took it away.
	//
	// EnsureFacingRestored, not the plain setter: an attack that plays out normally has already
	// started a longer fade across its recovery, and this runs at the montage's *blend-out*,
	// part-way through it. Re-timing that fade here would snap its tail -- which is the very
	// thing the recovery fade exists to remove. This call is for the paths that never reached
	// the release window's end: cancel, interrupt, and death mid-swing.
	if (ATheDreamCharacter* Character = GetFacingCharacter())
	{
		Character->EnsureFacingRestored(FacingLockFadeSeconds);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

ATheDreamCharacter* UTDMeleeAttackAbility::GetFacingCharacter() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	return ActorInfo ? Cast<ATheDreamCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
}

void UTDMeleeAttackAbility::ApplyRootMotionScale(float Scale)
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return;
	}

	// The same gate UAbilityTask_PlayMontageAndWait uses. A simulated proxy must not scale its
	// own root motion: its movement arrives replicated, and scaling it locally would apply the
	// multiplier twice to a translation the server has already accounted for.
	const bool bCanApply = Character->GetLocalRole() == ROLE_Authority
		|| (Character->GetLocalRole() == ROLE_AutonomousProxy
			&& GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::LocalPredicted);

	if (bCanApply)
	{
		Character->SetAnimRootMotionTranslationScale(Scale);
	}
}

UAbilityTask_MeleeTrace* UTDMeleeAttackAbility::StartMeleeTrace(const TArray<FTDAttackHitbox>& InHitboxes)
{
	// AttackMontage is passed so the hitboxes only go live on *this* attack's Release Window. The
	// events reach the whole ASC and carry no ownership, so without it a second montage carrying
	// the notify would open every listening trace.
	UAbilityTask_MeleeTrace* TraceTask = UAbilityTask_MeleeTrace::MeleeTrace(
		this,
		InHitboxes,
		bDrawDebugTrace,
		AttackMontage);
	TraceTask->OnHit.AddDynamic(this, &UTDMeleeAttackAbility::HandleTraceHit);
	TraceTask->ReadyForActivation();

	return TraceTask;
}

bool UTDMeleeAttackAbility::StartAttackMontage(FName StartSection, float PlayRate)
{
	if (!AttackMontage)
	{
		return false;
	}

	// A montage's section table is invisible to the MCP reflection layer (compositeSections
	// cannot be read), so it has never been checked from outside. It is plain C++ here. A
	// section that ends before the montage does, with nothing chained after it, ends the
	// montage there -- naturally, so the task reports OnBlendOut rather than OnInterrupted,
	// which is indistinguishable from a normal finish without this.
	if (TDShouldTraceCombatTiming() && AttackMontage)
	{
		TD_TIMING_LOG(TEXT("[%.3f] MONTAGE    sections=%d  length=%.4f"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
			AttackMontage->CompositeSections.Num(),
			AttackMontage->GetPlayLength());

		for (int32 Index = 0; Index < AttackMontage->CompositeSections.Num(); ++Index)
		{
			const FCompositeSection& Composite = AttackMontage->CompositeSections[Index];
			TD_TIMING_LOG(TEXT("[%.3f] MONTAGE      [%d] '%s' start=%.4f nextSection='%s'"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
				Index,
				*Composite.SectionName.ToString(),
				Composite.GetTime(),
				*Composite.NextSectionName.ToString());
		}

		// Notify placement is not readable through the MCP layer either, so until now the only
		// way to learn where a Release Window sits was to play an attack and read the edge it
		// fired at -- a round trip per marker adjustment. It is plain C++ here. This is also the
		// authored truth that ReleaseStartSeconds duplicates by hand, so a drift between them is
		// visible on the same screen rather than needing the warning to catch it.
		for (int32 Index = 0; Index < AttackMontage->Notifies.Num(); ++Index)
		{
			const FAnimNotifyEvent& Event = AttackMontage->Notifies[Index];
			TD_TIMING_LOG(TEXT("[%.3f] MONTAGE      notify '%s' trigger=%.4f duration=%.4f"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
				*Event.NotifyName.ToString(),
				Event.GetTriggerTime(),
				Event.GetDuration());
		}
	}

	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, AttackMontage, PlayRate, StartSection,
		/*bStopWhenAbilityEnds=*/true, GetWindupRootMotionScale());
	// Bound to four separate wrappers rather than two shared handlers, so the trace can say
	// which delegate ended the attack. They have different causes and different fixes.
	MontageTask->OnCompleted.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageCompleted);
	MontageTask->OnBlendOut.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageBlendedOut);
	MontageTask->OnInterrupted.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageCancelled);
	MontageTask->ReadyForActivation();

	return true;
}

void UTDMeleeAttackAbility::HandleTraceHit(const FHitResult& Hit)
{
	// Damage is authority-only state; clients see it arrive by attribute replication.
	if (!HasAuthority(&CurrentActivationInfo) || !DamageEffectClass)
	{
		return;
	}

	AActor* HitActor = Hit.GetActor();
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor);
	if (!TargetASC)
	{
		// Hit something that cannot take damage, such as a wall.
		return;
	}

	// I-frames. Checked here rather than on the defender because this is the only place
	// that knows a hit was resolved at all -- a dodge cannot refuse damage it never sees.
	if (!TargetImmunityTags.IsEmpty() && TargetASC->HasAnyMatchingGameplayTags(TargetImmunityTags))
	{
		return;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(DamageEffectClass, GetAbilityLevel());
	if (!SpecHandle.IsValid())
	{
		return;
	}

	// Designers tune damage as a positive number; the effect subtracts, so send it negative.
	SpecHandle.Data->SetSetByCallerMagnitude(TDTags::Data_Damage, -GetAttackDamage());

	if (UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo())
	{
		SourceASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
	}
}

void UTDMeleeAttackAbility::HandleMontageCompleted()
{
	TD_TIMING_LOG(TEXT("[%.3f] MONTAGE    OnCompleted"), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
	HandleMontageFinished();
}

void UTDMeleeAttackAbility::HandleMontageBlendedOut()
{
	// Where the blend starts, and which section was playing when it did. With
	// blendOutTriggerTime at -1 the natural end-blend cannot begin before
	// (length - blendTime), so a position well short of that means something else ended the
	// section -- and the section name is the only thing that can say what.
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;

	const float Position = (AnimInstance && AttackMontage) ? AnimInstance->Montage_GetPosition(AttackMontage) : -1.0f;
	const FName Section = (AnimInstance && AttackMontage) ? AnimInstance->Montage_GetCurrentSection(AttackMontage) : NAME_None;
	const bool bPlaying = (AnimInstance && AttackMontage) ? AnimInstance->Montage_IsPlaying(AttackMontage) : false;

	TD_TIMING_LOG(TEXT("[%.3f] MONTAGE    OnBlendOut  pos=%.4f section=%s playing=%d montageLen=%.4f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		Position,
		*Section.ToString(),
		bPlaying ? 1 : 0,
		AttackMontage ? AttackMontage->GetPlayLength() : -1.0f);

	HandleMontageFinished();
}

void UTDMeleeAttackAbility::HandleMontageCancelled()
{
	TD_TIMING_LOG(TEXT("[%.3f] MONTAGE    OnCancelled"), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
	HandleMontageInterrupted();
}

void UTDMeleeAttackAbility::HandleMontageFinished()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UTDMeleeAttackAbility::HandleMontageInterrupted()
{
	TD_TIMING_LOG(TEXT("[%.3f] MONTAGE    OnInterrupted"), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
