// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "TDJumpAbility.generated.h"

/**
 *  Jump, as a GameplayAbility rather than a hand-gated call on the character.
 *
 *  Every refusal is inherited and nothing about jumping is restated here. Death, the guard break,
 *  the parry window, parry recovery and hitstun come from UTDGameplayAbility::CanActivateAbility;
 *  exhaustion and the guard's commitment from ActivationBlockedTags on the CDO; the movement lock
 *  from bBlockedWhileMovementLocked.
 *
 *  Not a movement ability in GAS's sense: it does not take bLocksMovement and authors no
 *  displacement of its own -- the launch is the character movement component's, driven by
 *  JumpZVelocity. This class owns permission and the button's lifecycle, nothing else.
 *
 *  Costs no stamina. The regen pause stays on the character, keyed to OnJumped rather than to this
 *  activation, so a press held against a ceiling does not pause regen.
 */
UCLASS()
class UTDJumpAbility : public UTDGameplayAbility
{
	GENERATED_BODY()

public:

	UTDJumpAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	/**
	 *  The button came up. Ends the ability, which is what releases the engine's pressed flag.
	 *
	 *  The ability must outlive the launch: ACharacter::Jump() records bPressedJump rather than
	 *  launching and only StopJumping() clears it, so an ability ending at activation would strand
	 *  a flag that re-fires on the next landing.
	 *
	 *  No WaitInputRelease task -- the character forwards both input edges to live instances.
	 */
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/** Never buffered, whatever refused it: a replayed press lands you in the air unasked. */
	virtual bool ShouldBufferFailedInput(const FGameplayAbilityActorInfo* ActorInfo) const override { return false; }

	/** The jump input is also the neutral stand, so this ability is legal from the floor. */
	virtual const TCHAR* GetKnockdownRiseLabel(const class ATDCombatCharacter* Character) const override { return TEXT("stand"); }

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
};
