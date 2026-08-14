// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "TDBlockAbility.generated.h"

/**
 *  A held guard. Runs from the press until the button comes up, and costs stamina throughout.
 *
 *  **Deliberately almost empty, and that is the design rather than an unfinished state.** Nearly
 *  everything a block does is expressed somewhere it already belongs:
 *
 *  - *Being* blocking is ActivationOwnedTags carrying State.Blocking, which the character reads
 *    to drive the drain and the attacker reads to resolve a hit.
 *  - *Suppressing regen* is the same list carrying State.StaminaRegenPaused, exactly as the dodge
 *    does -- so an interrupted guard cannot strand the suppression, because the tag leaves with
 *    the ability whatever ends it.
 *  - *Costing stamina per second* is ATDCombatCharacter::TickBlockDrain, because the whole stamina
 *    economy is orchestrated in one place on purpose and a second spender on an ability's own
 *    clock would be the first thing able to disagree with it.
 *  - *Looking* like a guard is ABP_Combat swapping its locomotion source while State.Blocking is
 *    present. No montage: the guard is a stance you move in, not an action you play.
 *  - *Breaking* is ApplyStaminaDamage on the defender, which cancels this ability by tag.
 *
 *  What is left is the one thing GAS will not do by itself: end when the button comes up.
 *
 *  **Cancellability is the tag, not this class.** Defensive actions cancel an attack before its
 *  commit checkpoint and never after, expressed once as State.Attacking.Committed in
 *  ActivationBlockedTags -- so block inherits the boundary the dodge and parry share rather than
 *  restating it.
 *
 *  **Movement is deliberately not locked**, unlike an attack. bLocksMovement exists on the shared
 *  base and this is the first ability that could have taken it and does not: a guard you cannot
 *  move behind is a corner to be trapped in, and the spec's block is a stance you carry around.
 */
UCLASS(abstract)
class UTDBlockAbility : public UTDGameplayAbility
{
	GENERATED_BODY()

public:

	UTDBlockAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:

	/**
	 *  Whether releasing the button ends the guard. Debug only; leave it on.
	 *
	 *  Exists because a guard that never ends is the fastest way to measure the drain, the break
	 *  and the stun without a second pair of hands on the keyboard -- and because switching it off
	 *  is a cheaper experiment than editing the release path when something about the exit is
	 *  under suspicion.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Debug")
	bool bEndOnInputRelease = true;
};
