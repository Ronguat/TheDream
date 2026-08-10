// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDDodgeAbility.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
	/** Montage section per direction. The montage must use these exact names. */
	FName SectionForDirection(ETDDodgeDirection Direction)
	{
		switch (Direction)
		{
		case ETDDodgeDirection::Forward:  return FName("Forward");
		case ETDDodgeDirection::Backward: return FName("Backward");
		case ETDDodgeDirection::Left:     return FName("Left");
		case ETDDodgeDirection::Right:    return FName("Right");
		}
		return NAME_None;
	}
}

void UTDDodgeAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// Pays the stamina cost. Failing here is the "not enough stamina to dodge" case, so it
	// must run before anything observable happens.
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	DodgeDirection = ResolveDodgeDirection();

	if (IFrameTag.IsValid() && IFrameSeconds > 0.0f)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->AddLooseGameplayTag(IFrameTag);
			bIFramesActive = true;
		}

		World->GetTimerManager().SetTimer(
			IFrameTimerHandle,
			FTimerDelegate::CreateWeakLambda(this, [this]() { EndIFrames(); }),
			IFrameSeconds,
			false);
	}

	// No montage yet, so the ability is timed rather than animation-driven. When the dodge
	// animations land, setting DodgeMontage takes over ending it and DodgeSeconds falls out
	// of use -- the montage becomes the authority on length, as it is for attacks.
	if (DodgeMontage)
	{
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, DodgeMontage, 1.0f, SectionForDirection(DodgeDirection));
		MontageTask->OnCompleted.AddDynamic(this, &UTDDodgeAbility::HandleDodgeFinished);
		MontageTask->OnBlendOut.AddDynamic(this, &UTDDodgeAbility::HandleDodgeFinished);
		MontageTask->OnInterrupted.AddDynamic(this, &UTDDodgeAbility::HandleDodgeFinished);
		MontageTask->OnCancelled.AddDynamic(this, &UTDDodgeAbility::HandleDodgeFinished);
		MontageTask->ReadyForActivation();
		return;
	}

	World->GetTimerManager().SetTimer(
		DodgeTimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this]() { HandleDodgeFinished(); }),
		DodgeSeconds,
		false);
}

ETDDodgeDirection UTDDodgeAbility::ResolveDodgeDirection() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return ETDDodgeDirection::Backward;
	}

	// The frame's input, not the current velocity: velocity still carries the previous
	// direction for a moment after the stick is released, which would send a dodge the way
	// the player has just stopped going.
	FVector Input = Movement->GetLastInputVector();
	Input.Z = 0.0f;

	if (Input.IsNearlyZero())
	{
		// Standing still dodges backward. A neutral dodge that goes nowhere reads as a
		// flinch, and backward is the direction that buys spacing.
		return ETDDodgeDirection::Backward;
	}

	Input.Normalize();

	const FRotator Facing(0.0f, Character->GetActorRotation().Yaw, 0.0f);
	const float ForwardDot = FVector::DotProduct(Input, Facing.Vector());
	const float RightDot = FVector::DotProduct(Input, FRotationMatrix(Facing).GetUnitAxis(EAxis::Y));

	if (FMath::Abs(ForwardDot) >= FMath::Abs(RightDot))
	{
		return (ForwardDot >= 0.0f) ? ETDDodgeDirection::Forward : ETDDodgeDirection::Backward;
	}

	return (RightDot >= 0.0f) ? ETDDodgeDirection::Right : ETDDodgeDirection::Left;
}

void UTDDodgeAbility::EndIFrames()
{
	if (!bIFramesActive)
	{
		return;
	}
	bIFramesActive = false;

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(IFrameTag);
	}
}

void UTDDodgeAbility::HandleDodgeFinished()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UTDDodgeAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IFrameTimerHandle);
		World->GetTimerManager().ClearTimer(DodgeTimerHandle);
	}

	// Must come off even when the dodge is cancelled, or a cancelled dodge leaves the
	// character permanently invulnerable -- the defensive equivalent of a stuck State tag.
	EndIFrames();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
