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

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo) || !AttackMontage)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Find the mesh that carries the trace socket.
	USkeletalMeshComponent* MeshComponent = nullptr;
	if (AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr)
	{
		MeshComponent = Avatar->IsA<ACharacter>()
			? Cast<ACharacter>(Avatar)->GetMesh()
			: Avatar->FindComponentByClass<USkeletalMeshComponent>();
	}

	if (!MeshComponent)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Tracing starts paused and is driven by the montage's Melee Window notify state.
	UAbilityTask_MeleeTrace* TraceTask = UAbilityTask_MeleeTrace::MeleeTrace(this, MeshComponent, TraceSocket, TraceRadius, bDrawDebugTrace);
	TraceTask->OnHit.AddDynamic(this, &UTDMeleeAttackAbility::HandleTraceHit);
	TraceTask->ReadyForActivation();

	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, AttackMontage, MontagePlayRate);
	MontageTask->OnCompleted.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageFinished);
	MontageTask->OnBlendOut.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageFinished);
	MontageTask->OnInterrupted.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageInterrupted);
	MontageTask->ReadyForActivation();
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

	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(DamageEffectClass, GetAbilityLevel());
	if (!SpecHandle.IsValid())
	{
		return;
	}

	// Designers tune Damage as a positive number; the effect subtracts, so send it negative.
	SpecHandle.Data->SetSetByCallerMagnitude(TDTags::Data_Damage, -Damage);

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
