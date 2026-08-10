// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDMeleeAttackAbility.h"
#include "Combat/Tasks/AbilityTask_MeleeTrace.h"
#include "Combat/TDGameplayTags.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"

void UTDMeleeAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	StartMeleeTrace(GetAttackTraceRadius());

	// A plain swing has no derived timing, so it plays at the montage's authored speed.
	if (!StartAttackMontage(NAME_None, 1.0f))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
	}
}

USkeletalMeshComponent* UTDMeleeAttackAbility::FindAvatarMesh() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!Avatar)
	{
		return nullptr;
	}

	if (ACharacter* Character = Cast<ACharacter>(Avatar))
	{
		return Character->GetMesh();
	}

	return Avatar->FindComponentByClass<USkeletalMeshComponent>();
}

UAbilityTask_MeleeTrace* UTDMeleeAttackAbility::StartMeleeTrace(float Radius)
{
	USkeletalMeshComponent* MeshComponent = FindAvatarMesh();
	if (!MeshComponent)
	{
		return nullptr;
	}

	UAbilityTask_MeleeTrace* TraceTask = UAbilityTask_MeleeTrace::MeleeTrace(this, MeshComponent, TraceSocket, Radius, bDrawDebugTrace);
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

	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, AttackMontage, PlayRate, StartSection);
	MontageTask->OnCompleted.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageFinished);
	MontageTask->OnBlendOut.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageFinished);
	MontageTask->OnInterrupted.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageInterrupted);
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

void UTDMeleeAttackAbility::HandleMontageFinished()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UTDMeleeAttackAbility::HandleMontageInterrupted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
