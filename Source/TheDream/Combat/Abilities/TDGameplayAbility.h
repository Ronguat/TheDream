// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "TDGameplayAbility.generated.h"

class UCurveFloat;

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
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	/**
	 *  Whether a press this ability just refused is worth remembering for a moment.
	 *
	 *  The input buffer exists for refusals that are *temporary* -- a committed swing, a
	 *  blocking tag -- where the player pressed slightly early and meant it. Some refusals
	 *  are not temporary in that sense but in the trivial sense that everything ends
	 *  eventually, and replaying those produces an action the player is no longer asking
	 *  for. This is where an ability says which kind its refusal was.
	 *
	 *  It is asked *instead of* enumerating reasons on the character's side, so the rule
	 *  stays next to the flag that creates it.
	 */
	virtual bool ShouldBufferFailedInput(const FGameplayAbilityActorInfo* ActorInfo) const;

protected:

	/**
	 *  Refuse activation while the avatar is falling.
	 *
	 *  Keyed to the airborne *state*, not to having jumped, so it also covers walking off a
	 *  ledge -- the question is what is physically possible, not what you chose to do. That is
	 *  deliberately the opposite of the jump's regen pause, which keys on the action precisely
	 *  because it is charging you for a choice.
	 *
	 *  A flag on the shared base rather than a check inside one ability, so block and parry can
	 *  adopt the same rule with a checkbox if they want it. Left off by default: an air attack
	 *  is a legitimate thing to want later, and this should not quietly forbid one.
	 *
	 *  **This refusal is deliberately not buffered**, decided when the buffer was built. An
	 *  earlier note here predicted the opposite -- that a dodge pressed just before landing
	 *  ought to fire on landing rather than vanish. The reason it should not: landing is not a
	 *  moment you are free to act through. A landing recovery would lock the dodge out anyway,
	 *  so deferring to the touchdown frame buys a dodge that a finished character would not
	 *  give you, and tunes the timing of a window that does not exist yet. See
	 *  ShouldBufferFailedInput below and Docs/Combat-Decisions.md.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Activation")
	bool bBlockedWhileAirborne = false;

	/**
	 *  Suppress movement input -- WASD and jump -- for as long as this ability runs.
	 *
	 *  **Input, not movement.** The ability's own displacement is unaffected: a lunge, a dash or
	 *  a knockback still moves the character, because those are the ability moving you rather
	 *  than you moving yourself. What stops is walking out of your own commitment.
	 *
	 *  A flag on the shared base, following bBlockedWhileAirborne, so opting in is a checkbox and
	 *  the clearing cannot be forgotten -- it happens in this class's EndAbility, where every exit
	 *  path converges. Off by default: an ability that leaves you mobile is a legitimate thing to
	 *  want, and this should not quietly forbid one.
	 *
	 *  **This is the seam a movement-ability category plugs into later.** When jump and crouch
	 *  become abilities, they are blocked by the same lock rather than by a second mechanism --
	 *  or by a State.MovementLocked tag in their ActivationBlockedTags, which is the same rule
	 *  expressed in GAS's own vocabulary. Neither requires undoing this.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Activation")
	bool bLocksMovement = false;

	/**
	 *  Whether *this activation* took the movement lock. Runtime only.
	 *
	 *  The release is guarded on this rather than on bLocksMovement, because EndAbility runs on
	 *  the shared base for every ability: without it, any ability ending would hand movement back
	 *  while another still owned it.
	 */
	bool bTookMovementLock = false;

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

	/**
	 *  Starts an authored displacement that follows the avatar's facing.
	 *
	 *  **Shared by attacks and the dodge as of 2026-08-13**, which is why it sits on the base
	 *  rather than on UTDMeleeAttackAbility where it was born. The dodge previously took its
	 *  displacement from the montage's root motion, corrected by eight per-direction scales
	 *  measured off the clips -- the last system in the project still reading a number off an
	 *  animation. Authored wedges took space off the art, authored durations took time, Lunge
	 *  took the attack's displacement, and this finishes the job.
	 *
	 *  Built on a root motion *source* rather than SetActorLocation, AddMovementInput or
	 *  LaunchCharacter: those are the hand-rolled displacement the netcode audit was right to
	 *  fear, while this rides the same prediction and replication machinery animation root motion
	 *  does. See FTDRootMotionSource_FacingForce.
	 *
	 *  **The montage must carry no root motion or this does nothing at all.** Animation root
	 *  motion suppresses every root motion source while it plays, and scaling it to zero does not
	 *  help -- the character simply stops moving. Attacks satisfy this by playing an in-place clip;
	 *  the dodge satisfies it by having bEnableRootMotion switched off on its eight source clips,
	 *  which is the library's own default and was only ever enabled by us.
	 *
	 *  @param DistanceCm        How far to travel. The authored ceiling; the standoff gate may
	 *                           shorten it but nothing lengthens it.
	 *  @param DurationSeconds   How long it takes. Speed is derived from these two.
	 *  @param StrengthCurve     Optional shape. **Must average 1.0** or the distance is a lie.
	 *  @param StandoffCm        Target Lock's per-tick gate. 0 disables it, which is what the
	 *                           dodge passes: an evade has to be able to travel *past* people,
	 *                           and gating it on pawns would break dodging through a crowd.
	 *  @param YawOffsetDegrees  Direction relative to facing. 0 is straight ahead, which is every
	 *                           attack; the dodge derives it from its direction enum.
	 */
	void StartLunge(
		float DistanceCm,
		float DurationSeconds,
		UCurveFloat* StrengthCurve,
		float StandoffCm = 0.0f,
		float YawOffsetDegrees = 0.0f);
};
