// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "TDGameplayAbility.generated.h"

class UCurveFloat;
class UAbilityTask_FacingLunge;

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

	/**
	 *  Whether a buffered press for this ability's input should survive while an instance of it
	 *  is *already running* -- and for the string's link window after it ends.
	 *
	 *  The default buffer window is grace on taps, sized against short lockouts; an attack's own
	 *  duration is longer than any such window, so a chain tap made early in a swing would expire
	 *  before the chain could open. An ability returning true here says "a press made while I run
	 *  is a request to follow me, hold it until I can answer" -- which is bounded by the swing plus
	 *  the link window, well under the four-second lockouts that killed widening the window itself.
	 *
	 *  Asked of the ability rather than keyed on a tag name, because the character deliberately
	 *  does not know which tag means attack.
	 *
	 *  **What it actually rescues is narrower than it looks, and it is on probation** (2026-08-16).
	 *  Chain-out fires when *recovery* opens, not when the press arrived, so pressing early buys no
	 *  cadence at all -- the only input this saves is a tap *completed* inside the swing's first
	 *  ~165 ms, i.e. mashing at 2-3x the rate the chain accepts, paid for in aim staleness. It is
	 *  insurance, not technique.
	 *
	 *  **It is per ability, not per branch**, so returning true here holds a press through a heavy
	 *  or charged too, letting a second commitment be queued while the first is unresolved. That is
	 *  emergent rather than chosen -- see the trap. Interplay's input-forgiveness subslice owns all
	 *  three options: keep, return false, or narrow it to chain-eligible attacks.
	 */
	virtual bool ShouldExtendBufferWhileActive() const { return false; }

	/**
	 *  Offer a running ability the chance to end itself in favour of the buffered press.
	 *
	 *  The chain-out seam: the buffer tick calls this on the active instance answering the same
	 *  input, and an attack in its chain-open span ends early through the ordinary EndAbility
	 *  funnel -- facing, tags, homing and the lunge all clean up exactly as on a natural end --
	 *  letting the same tick's retry activate the next swing. Returns true if it ended.
	 */
	virtual bool TryChainOutForBufferedPress() { return false; }

	/**
	 *  Trace label for the rise this ability starts when used as a get-up. See BeginKnockdownRise.
	 *
	 *  Asked of the ability rather than switched on in the character, because the character
	 *  deliberately does not know which ability means what -- the same reasoning that keeps it from
	 *  knowing which tag means attack. The dodge answers with two labels depending on grade, which
	 *  is exactly why this takes the character rather than being a constant.
	 */
	virtual const TCHAR* GetKnockdownRiseLabel(const class ATDCombatCharacter* Character) const { return TEXT("unknown"); }

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
	 *  Refuse activation while an ability owns movement input.
	 *
	 *  **The counterpart to bLocksMovement below, and the pair is deliberately asymmetric**: that
	 *  flag says "I take movement while I run", this one says "I may not start while someone else
	 *  has it". Most abilities want neither, an attack and a dodge want the first, and jump is the
	 *  first thing in the project to want the second.
	 *
	 *  It reads ATheDreamCharacter's bAbilityMovementLocked directly rather than going through a
	 *  State.MovementLocked tag, which this header used to offer as the alternative. **The bool is
	 *  the better half of that choice for a reason that outlives the convenience**: CLAUDE.md's
	 *  network rule is that new state is a replicated property or an attribute and never a loose
	 *  tag, and the lock is already a bool -- minting a tag to mirror it would create a second
	 *  source of truth for one fact, which is the thing that goes stale.
	 *
	 *  Off by default, following bBlockedWhileAirborne above: an ability that can be started out of
	 *  someone else's commitment is a legitimate thing to want, and this should not quietly forbid
	 *  one.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Activation")
	bool bBlockedWhileMovementLocked = false;

	/**
	 *  Whether this ability may be started from the floor, during the knockdown's choice window.
	 *
	 *  **Three phases, two answers.** The jail refuses everything regardless of this flag; the rise
	 *  refuses everything too, because a rise is committed once started. Only the choice window
	 *  between them consults it, which is what makes "the action is the exit" true -- a get-up
	 *  option is the ordinary ability, activated from an unusual place, with no shared pre-rise for
	 *  it to wait through.
	 *
	 *  Off by default, so the down state refuses anything nobody has thought about. The four that
	 *  take it are the dodge, the guard, the jump (the neutral stand) and the get-up attack; each
	 *  then prices its own rise, which is the whole design of the choice.
	 *
	 *  **Grade restrictions are not here.** Hard knockdown removes the directional dodge and the
	 *  free stand, but that is a question about *which* option, answered by the option itself
	 *  against the grade it can read off the character. A second flag here would spread one ruling
	 *  across two files.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Activation")
	bool bAllowedFromKnockdown = false;

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
	 *  **This was the seam a movement-ability category plugs into, and jump took it 2026-08-20.**
	 *  The prediction offered two forms -- the same lock, or a State.MovementLocked tag in
	 *  ActivationBlockedTags -- and the lock won: see bBlockedWhileMovementLocked above for why a
	 *  mirrored tag would have been a second source of truth for one fact. Crouch, if it ever
	 *  arrives, has the route already built.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Activation")
	bool bLocksMovement = false;

	/**
	 *  Re-activate this ability when another ends, if its input is still held.
	 *
	 *  For *held states* rather than actions. A guard interrupted by a dodge or a swing should come
	 *  back when that finishes and the player is still holding the button, because the button being
	 *  down is a continuous statement of intent rather than a one-off press.
	 *
	 *  **Opt-in, and that is the whole safety argument.** The general form -- re-attempting anything
	 *  whose input is held -- was the first design and is dangerous: holding attack through the end
	 *  of a swing would silently become auto-repeat, turning a held button into a fire rate nobody
	 *  authored. Only abilities that are *states* want this, and only they should carry it.
	 *
	 *  Off by default. It cannot resurrect an ability the game still forbids -- the re-attempt goes
	 *  through CanActivateAbility like any other, so exhaustion, a broken guard or being airborne
	 *  refuse it exactly as they refuse a fresh press.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Activation")
	bool bResumeWhileInputHeld = false;

public:

	/** Whether this ability wants re-activating when another ends and its input is still down. */
	bool ShouldResumeWhileInputHeld() const { return bResumeWhileInputHeld; }

protected:

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
	 *  time keeps being pushed forward, so the tail falls out of the tag coming off and needs
	 *  nothing applied at the end. That is the more robust half of the reason
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
	 *  **The montage must carry no root motion or this does nothing at all** -- animation root
	 *  motion suppresses every root motion source, and scaling it to zero does not help. Attacks
	 *  play an in-place clip and the dodge's source clips have bEnableRootMotion off; the full
	 *  account and the enforcement warning live at StartAttackMontage.
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

	/**
	 *  Ends the lunge started by StartLunge, now, wherever it has got to.
	 *
	 *  **This is a stop, and the standoff gate is a pause -- they are different mechanisms and the
	 *  distinction is the whole reason this exists.** The gate contributes nothing on a tick where a
	 *  body is in the way, but time keeps advancing and the source stays live, so travel resumes the
	 *  instant the obstruction leaves. That is correct for its own job -- a target backing away mid
	 *  attack has to stay reachable -- and wrong once a hit has landed: killing a target removes its
	 *  capsule, the gate opens on a corpse, and the attacker slides forward through where it was.
	 *
	 *  Terminal by design. Nothing restarts a lunge within one activation, so the ceiling on travel
	 *  is still the authored distance and this can only ever subtract, exactly like the gate.
	 *
	 *  Safe to call when no lunge is running, or twice.
	 */
	void StopLunge();

private:

	/**
	 *  The lunge currently running for this activation, so a hit can end it.
	 *
	 *  Weak because the task is owned by the ability system and can be torn down by an ability
	 *  ending, a cancellation or a montage interrupt without passing through here -- a raw pointer
	 *  would be the dangling one exactly on the paths that are hardest to test.
	 */
	TWeakObjectPtr<UAbilityTask_FacingLunge> ActiveLungeTask;

	/**
	 *  Dedupe state for the refusal trace. Mutable because CanActivateAbility is const.
	 *
	 *  Refusals are polled rather than edge-triggered -- the resume retries every tick while its
	 *  input is held -- so an undeduped line would emit at frame rate and bury everything else in
	 *  any capped log window.
	 */
	mutable FString LastRefusalReason;
	mutable float LastRefusalLoggedAt = 0.0f;
};
