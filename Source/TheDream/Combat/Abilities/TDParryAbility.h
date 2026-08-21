// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "TDParryAbility.generated.h"

class UAnimMontage;

/**
 *  A read. Opens a short window in which any incoming melee hit is negated outright.
 *
 *  A standalone input, not a modifier on block, and never buffered.
 *
 *  Success is derived rather than authored: a parried attacker is planted at zero distance by the
 *  lunge stop and rides their own attack into recovery, so the reward scales with the victim's
 *  commitment without anything per-tier being written. Activation costs no stamina -- the parry is
 *  time-priced, and ParryWhiffRecoverySeconds is the price.
 *
 *  The window is mechanical, never a notify: a timestamp on the character checked in Tick, so the
 *  montage is purely visual and can be swapped, retimed or removed without changing behaviour.
 *
 *  No facing test -- 360 degrees.
 *
 *  Not refused by State.Blockstun; blockstun and parry never know about each other. It *is* refused
 *  while blocking, which is a property of the guard rather than of blockstun.
 *
 *  Throwing one jails you until it resolves. State.Parrying spans the window and State.ParryRecovery
 *  the whiff tail, and the shared base refuses every ability on both; movement is locked across the
 *  same span by bLocksMovement, separately, because WASD is not an ability. The three exits are the
 *  recovery expiring, a catch (NotifyParrySuccess ends the ability at once), and an attacker's
 *  punishment cancelling it.
 */
UCLASS(abstract)
class UTDParryAbility : public UTDGameplayAbility
{
	GENERATED_BODY()

public:

	UTDParryAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/** Never. A replayed parry is a mistimed parry: its entire value is when it lands. */
	virtual bool ShouldBufferFailedInput(const FGameplayAbilityActorInfo* ActorInfo) const override;

protected:

	/**
	 *  How long the negation window stays open, from activation.
	 *
	 *  Fenced at both ends and not freely tunable; both bounds are tuning-map invariants. Covering
	 *  [t, t+300] catches both members of the fast layer, 200 and 350, under a single read.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Parry", meta=(ClampMin="0.0"))
	float ParryWindowSeconds = 0.30f;

	/**
	 *  How long every ability is refused after a window closes without catching anything, applied
	 *  as State.ParryRecovery. This is the whole price of the input, since activation costs no
	 *  stamina. Floored by a tuning-map invariant rather than chosen for feel.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Parry", meta=(ClampMin="0.0"))
	float ParryWhiffRecoverySeconds = 0.60f;

	/**
	 *  The clip. Purely cosmetic -- the parry works correctly with this unset.
	 *
	 *  Fitted to the authored values in two segments: a Parry Gesture notify marks where the gesture
	 *  reads, the segment before it plays across ParryWindowSeconds and the segment after across
	 *  ParryWhiffRecoverySeconds, each at its own derived rate. With no marker the whole clip plays
	 *  across the total at one rate and warns.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Parry")
	TObjectPtr<UAnimMontage> ParryMontage;

private:

	/**
	 *  Starts the clip at the window segment's rate and parks the recovery segment's on the
	 *  character. Silent and harmless with no montage.
	 *
	 *  Both rates are computed here rather than when the marker fires, so the recovery rate survives
	 *  a catch ending this ability before the marker arrives.
	 */
	void PlayParryMontage();

	/** Trigger time of the Parry Gesture marker on ParryMontage, or -1 if there is none. */
	float FindGestureTime() const;
};
