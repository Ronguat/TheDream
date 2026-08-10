// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "TDGameplayAbility.generated.h"

/**
 *  Shared base for every combat ability in the project.
 */
UCLASS(abstract)
class UTDGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:

	UTDGameplayAbility();

	/**
	 *  Input this ability answers to, e.g. InputTag.Attack.
	 *
	 *  Deliberately a separate namespace from the ability's own tags: the input is not
	 *  the move. One press of InputTag.Attack resolves to Light, Heavy or Charged
	 *  depending on how long it is held.
	 *
	 *  Block and Parry deliberately do *not* share a button, unlike the attack ladder:
	 *  both have to be active on the frame they are pressed, so neither can afford to
	 *  wait and see which one was meant. See Docs/Combat-Decisions.md.
	 *
	 *  Input is routed by tag rather than by an integer ID so that adding an ability is
	 *  a content change. The character calls AbilitySpecInputPressed/Released, which sets
	 *  Spec.InputPressed and forwards both edges to live instances -- that is what
	 *  WaitInputRelease observes, and what the hold-to-Heavy conversion needs.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Input")
	FGameplayTag InputTag;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:

	/**
	 *  Applied to self the moment this ability activates. **This is how costs are paid.**
	 *
	 *  Deliberately not GAS's CostGameplayEffectClass, which is a *gate* -- it is checked in
	 *  CanActivateAbility and refuses the activation if the cost cannot be met. This design
	 *  wants the opposite: every action is always available, and stamina is the consequence
	 *  of taking it rather than the permission to. Dodging at 30 stamina should work, empty
	 *  the bar and exhaust you; it should not silently do nothing.
	 *
	 *  Applied unconditionally on activation, so anything that must not be charged when an
	 *  activation subsequently fails should not use this.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Effects")
	TSubclassOf<UGameplayEffect> EffectOnStart;

	/**
	 *  Applied to self as this ability ends, however it ends. **Currently unset on every
	 *  ability** -- a general hook, not a mechanism anything depends on.
	 *
	 *  It was added for the stamina regen pause and no longer carries it. The pause is now
	 *  entirely on ATDCombatCharacter: while State.StaminaRegenPaused is present the resume
	 *  time keeps being pushed forward, so the one-second tail falls out of the tag coming
	 *  off and needs nothing applied at the end. That is the more robust half of the reason
	 *  -- an ability that is cancelled or interrupted cannot fail to apply something it no
	 *  longer applies.
	 *
	 *  If something does use this later: it is applied on cancellation too, deliberately, so
	 *  that being hit out of a defensive action can never be a way to skip its cost.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Effects")
	TSubclassOf<UGameplayEffect> EffectOnEnd;
};
