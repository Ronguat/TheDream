// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "TDJumpAbility.generated.h"

/**
 *  Jump, as a GameplayAbility rather than a hand-gated call on the character.
 *
 *  **It exists to delete five copies of the same rule, not to add a feature.** Until 2026-08-20
 *  ATDCombatCharacter::Jump() restated exhaustion, death, hitstun, the movement lock and the
 *  guard's minimum duration by hand, and its own comment named itself "the only place that can be
 *  forgotten". That prediction came true exactly once and was filed rather than fixed: a broken
 *  guard refuses every ability from the shared base, and jump -- not being an ability -- walked
 *  straight through it. See the "Before Stun" trap in Docs/Combat-Decisions.md.
 *
 *  So every refusal here is inherited. Death, the guard break, the parry window, parry recovery and
 *  hitstun come from UTDGameplayAbility::CanActivateAbility; exhaustion and the guard's commitment
 *  come from ActivationBlockedTags on the CDO; the movement lock comes from
 *  bBlockedWhileMovementLocked. **Nothing about jumping is restated in this class**, which is the
 *  entire point of it -- a future lockout added to the base covers jump without anyone remembering
 *  that jump exists.
 *
 *  **Not a movement ability in GAS's sense.** It does not take bLocksMovement (jumping while
 *  walking is the whole of jumping) and it authors no displacement of its own -- the launch is the
 *  character movement component's, driven by JumpZVelocity exactly as before. This class owns
 *  *permission* and the button's lifecycle, nothing else.
 *
 *  **Costs no stamina, and still is not free**: the regen pause stays on the character, keyed to
 *  OnJumped rather than to this activation, because it is charging for a launch that happened
 *  rather than for a press that was made. A press held against a ceiling must not pause regen.
 *  See ATDCombatCharacter::OnJumped_Implementation.
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
	 *  **The ability outlives the launch deliberately, and only for this.** ACharacter::Jump()
	 *  records bPressedJump rather than launching, and nothing clears it but StopJumping() -- so an
	 *  ability that ended at activation would strand a pressed flag that re-fires the moment the
	 *  character next touches down. Holding the button through a landing re-jumping is the
	 *  behaviour this project already had; what would be new, and wrong, is a *released* button
	 *  still doing it.
	 *
	 *  No WaitInputRelease task, following UTDBlockAbility: the character forwards both input edges
	 *  to live instances already, so this is reached without an ability task -- one fewer object per
	 *  jump and one fewer thing that can outlive the ability that made it.
	 */
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/**
	 *  Never buffered, whatever refused it. **A stale jump is a jump nobody asked for.**
	 *
	 *  The base class asks this per refusal so an ability can distinguish a temporary "not yet"
	 *  from a lasting "no"; jump answers the same way for all of them, because every refusal it can
	 *  meet is one where replaying the press lands you in the air at a moment you did not choose.
	 *  Death and the guard break are already exempted upstream; hitstun and the movement lock are
	 *  the ones this adds, and both are exactly the case -- a jump queued during someone else's
	 *  combo firing on the frame it ends is a movement option the player never requested.
	 */
	virtual bool ShouldBufferFailedInput(const FGameplayAbilityActorInfo* ActorInfo) const override { return false; }

	/**
	 *  **The jump input is also the neutral stand**, so this ability is legal from the floor.
	 *
	 *  Held movement input was rejected as the trigger: incidental WASD would manufacture rises
	 *  nobody called. A jump press is deliberate, and it is already the button that means "get me
	 *  off the ground".
	 */
	virtual const TCHAR* GetKnockdownRiseLabel(const class ATDCombatCharacter* Character) const override { return TEXT("stand"); }

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
};
