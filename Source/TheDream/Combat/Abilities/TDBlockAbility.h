// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "TDBlockAbility.generated.h"

/**
 *  A held guard. Runs from the press until the button comes up, and costs stamina throughout.
 *
 *  Almost everything a block does lives elsewhere:
 *
 *  - *Being* blocking is ActivationOwnedTags carrying State.Blocking, which the character reads to
 *    drive the drain and the attacker reads to resolve a hit.
 *  - *Suppressing regen* is the same list carrying State.StaminaRegenPaused, so the tag leaves with
 *    the ability whatever ends it.
 *  - *Costing stamina per second* is ATDCombatCharacter::TickBlockDrain.
 *  - *Looking* like a guard is ABP_Combat swapping its locomotion source while State.Blocking is
 *    present. No montage.
 *  - *Breaking* is ApplyStaminaDamage on the defender, which cancels this ability by tag.
 *
 *  What is left is ending when the button comes up. Cancellability is the tag, not this class:
 *  State.Attacking.Committed in ActivationBlockedTags.
 *
 *  GA_Block also blocks on its own State.Blocking, which reads like a mistake. Three mechanisms can
 *  raise the guard -- a direct press, the buffer replaying a refused press, and
 *  bResumeWhileInputHeld -- and two succeeding in one frame activates the ability twice, leaking
 *  the spec's activeCount so the guard sticks up permanently. Blocking on the tag the ability
 *  grants makes a second activation unrepresentable; a cancelled guard drops the tag with the
 *  ability, so a resume still activates normally.
 *
 *  Movement is deliberately not locked, unlike an attack: bLocksMovement exists on the shared base
 *  and this is the first ability that could take it and does not.
 */
UCLASS(abstract)
class UTDBlockAbility : public UTDGameplayAbility
{
	GENERATED_BODY()

public:

	UTDBlockAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	/**
	 *  The guard is knockdown's protected get-up, and it is the ordinary guard throughout: full
	 *  cost, full commitment, standard drain.
	 */
	virtual const TCHAR* GetKnockdownRiseLabel(const class ATDCombatCharacter* Character) const override { return TEXT("block"); }

	/**
	 *  Never. Buffer actions, not states: a stale request to enter a state is meaningless, the
	 *  button either being down now or not, and bResumeWhileInputHeld already answers that. Attacks
	 *  deliberately keep buffering through the guard's commitment, which is why this belongs here
	 *  rather than in the shared base.
	 */
	virtual bool ShouldBufferFailedInput(const FGameplayAbilityActorInfo* ActorInfo) const override;
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:

	/** Whether releasing the button ends the guard. Debug only; leave it on. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Debug")
	bool bEndOnInputRelease = true;

public:

	/**
	 *  True once the button has come up but the guard's minimum duration has not expired. The
	 *  release is remembered, never discarded: letting go inside the commit window still means
	 *  letting go, it just takes effect when the window does.
	 */
	bool IsReleasePending() const { return bReleasePending; }

	/** Ends the guard now, as a release rather than a cancel. Called when the commit expires. */
	void FinishPendingRelease();

private:

	bool bReleasePending = false;
};
