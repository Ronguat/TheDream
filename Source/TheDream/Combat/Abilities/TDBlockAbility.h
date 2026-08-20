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
 *  **GA_Block also blocks on its own State.Blocking, which reads like a mistake and is the fix for
 *  a whole class of bug.** Three separate mechanisms can raise this guard: a direct press, the
 *  input buffer replaying a press that was refused mid-attack, and bResumeWhileInputHeld bringing
 *  it back after a cancel. They are all correct and they all fire in the same frame sometimes. Two
 *  of them succeeding activates the ability twice, which leaks the spec's activeCount -- one
 *  release then only decrements it to one, and the guard is stuck up permanently with State.Blocking
 *  applied and no input able to clear it.
 *
 *  Guarding each caller was tried and is the wrong shape: every new way to raise a guard would have
 *  to remember, and the races are frame-order dependent so a missed one is invisible until someone
 *  plays it. Blocking on the tag the ability itself grants makes a second activation
 *  *unrepresentable* rather than merely unlikely, because the tag is applied by the first one. It
 *  costs nothing legitimate: a cancelled guard drops the tag with the ability, so a resume still
 *  activates normally.
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

	/**
	 *  **The guard is knockdown's protected get-up**, and it is the ordinary guard throughout: full
	 *  cost, full commitment, standard drain. All blocks are created equal.
	 *
	 *  The trade it buys, priced: half a second guarded-and-committed instead of naked, for about
	 *  15 stamina plus the guard's regen-pause tail -- front arc only, and a meaty heavy against a
	 *  sub-50 bar breaks the guard on the way up.
	 */
	virtual const TCHAR* GetKnockdownRiseLabel(const class ATDCombatCharacter* Character) const override { return TEXT("block"); }

	/**
	 *  Never. **Buffer actions, not states** -- and this is the rule the whole input model was
	 *  missing.
	 *
	 *  The buffer exists so a deliberate *tap* is not lost to a brief lockout: you meant to swing,
	 *  you were half a frame early, the swing still comes out. That reasoning is about an action,
	 *  which is a thing you asked for once and which is still worth doing a moment later. A guard is
	 *  a *state*, and a stale request to enter one is meaningless -- the button either is or is not
	 *  down now, and bResumeWhileInputHeld already answers "still down".
	 *
	 *  Found from play. A 42 ms tap on RMB was refused because a previous guard was still inside its
	 *  minimum, buffered, replayed when that expired, and became a *fresh* 250 ms guard whose own
	 *  commitment then held back the replayed release. So a tap turned into a quarter-second guard
	 *  long after the button came up -- and because each new guard commits, the next tap was
	 *  buffered too, chaining indefinitely. Three individually-correct mechanisms with no single
	 *  culprit between them.
	 *
	 *  Attacks deliberately keep buffering through the guard's commitment: that is what makes a
	 *  swing thrown during a block feel responsive rather than dropped. The asymmetry is the point,
	 *  and it is why this belongs here rather than in the shared base.
	 */
	virtual bool ShouldBufferFailedInput(const FGameplayAbilityActorInfo* ActorInfo) const override;
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

public:

	/**
	 *  True once the button has come up but the guard's minimum duration has not expired.
	 *
	 *  The release is *remembered*, never discarded: letting go inside the commit window still
	 *  means letting go, it just takes effect when the window does. Discarding it would make the
	 *  player hold the button through the whole minimum to get a guard that ends when they wanted,
	 *  which is the opposite of a floor.
	 */
	bool IsReleasePending() const { return bReleasePending; }

	/** Ends the guard now, as a release rather than a cancel. Called when the commit expires. */
	void FinishPendingRelease();

private:

	bool bReleasePending = false;
};
