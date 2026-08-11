// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDGameplayAbility.h"
#include "Combat/TDCombatDebug.h"
#include "Combat/TDGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"

UTDGameplayAbility::UTDGameplayAbility()
{
	// One instance per actor: abilities keep state across activations (combo index,
	// input holds) and per-execution instancing would throw that away every swing.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

bool UTDGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	// Checked here rather than by adding State.Dead to every ability's ActivationBlockedTags,
	// so a future ability cannot be granted without it. The dead are not a special case any
	// one ability should have to remember. Unconditional rather than a flag like
	// bBlockedWhileAirborne -- there is no ability a corpse should be able to use.
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid()
		&& ActorInfo->AbilitySystemComponent->HasMatchingGameplayTag(TDTags::State_Dead))
	{
		TD_TIMING_LOG(TEXT("[%.3f] REFUSED    %s: dead"),
			ActorInfo->AvatarActor.IsValid() && ActorInfo->AvatarActor->GetWorld()
				? ActorInfo->AvatarActor->GetWorld()->GetTimeSeconds() : 0.0f,
			*GetName());
		return false;
	}

	if (bBlockedWhileAirborne)
	{
		const ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
		const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
		if (Movement && Movement->IsFalling())
		{
			// Traced, because a refused activation is otherwise indistinguishable from a
			// dropped input -- the exact failure this project treats as the worst feedback
			// available. Only on the refusal, so it reports an event rather than every press.
			TD_TIMING_LOG(TEXT("[%.3f] REFUSED    %s: airborne (mode=%d)"),
				Character->GetWorld() ? Character->GetWorld()->GetTimeSeconds() : 0.0f,
				*GetName(),
				static_cast<int32>(Movement->MovementMode.GetValue()));
			return false;
		}
	}

	return true;
}

bool UTDGameplayAbility::ShouldBufferFailedInput(const FGameplayAbilityActorInfo* ActorInfo) const
{
	// Death is the clearest case of a refusal that must not be remembered. It lasts whole
	// seconds, so a press buffered across it would fire on revive -- an action the player
	// asked for in a situation that no longer exists, which is precisely the false positive
	// the window length exists to prevent.
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid()
		&& ActorInfo->AbilitySystemComponent->HasMatchingGameplayTag(TDTags::State_Dead))
	{
		return false;
	}

	// The airborne refusal is the one kind that must not be remembered. Everything else this
	// ability can be refused by -- a committed swing, an exhaustion lockout, a live instance --
	// clears while the player is still standing there meaning it. Being in the air does not:
	// the thing it clears into is a landing, which is itself not a moment you can act through.
	// Buffering it would hand back a dodge on the touchdown frame that a landing recovery will
	// later have to take away again.
	if (bBlockedWhileAirborne)
	{
		const ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
		const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
		if (Movement && Movement->IsFalling())
		{
			return false;
		}
	}

	return true;
}

void UTDGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (EffectOnStart && HasAuthority(&ActivationInfo))
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(EffectOnStart, GetAbilityLevel());
			if (SpecHandle.IsValid())
			{
				ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			}
		}
	}
}

void UTDGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// Before Super, which tears down the actor info this needs. Authority-only, like every
	// other effect application in this project -- clients see the result by replication.
	if (EffectOnEnd && IsValid(this) && HasAuthority(&ActivationInfo))
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(EffectOnEnd, GetAbilityLevel());
			if (SpecHandle.IsValid())
			{
				ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			}
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
