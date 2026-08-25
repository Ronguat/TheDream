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
	 *  Input this ability answers to, e.g. InputTag.Attack. A separate namespace from the ability's
	 *  own tags -- the input is not the move: one press of InputTag.Attack resolves to Light, Heavy
	 *  or Charged depending on hold length.
	 *
	 *  Routed by tag rather than integer ID, so adding an ability is a content change. The character
	 *  calls AbilitySpecInputPressed/Released, which sets Spec.InputPressed and forwards both edges
	 *  to live instances -- what WaitInputRelease observes, and what hold-to-Heavy needs.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Input")
	FGameplayTag InputTag;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	/**
	 *  Whether a press this ability just refused is worth remembering for a moment. True where the
	 *  refusal is temporary in the sense the player meant -- a committed swing, a blocking tag --
	 *  and false where replaying would produce an action nobody is still asking for.
	 *
	 *  Asked of the ability rather than enumerated on the character, so the rule stays next to the
	 *  flag that creates it.
	 */
	virtual bool ShouldBufferFailedInput(const FGameplayAbilityActorInfo* ActorInfo) const;

	/**
	 *  Whether a buffered press for this input survives while an instance is already running, and
	 *  through the string's link window after it ends.
	 *
	 *  The default buffer window is grace on taps and is shorter than a swing, so a chain tap made
	 *  early would expire before the chain could open. True means "a press made while I run is a
	 *  request to follow me", bounded by the swing plus the link window.
	 *
	 *  Per ability, not per branch, so returning true also holds a press through a heavy or charged
	 *  and lets a second commitment queue on an unresolved first -- see the trap.
	 */
	virtual bool ShouldExtendBufferWhileActive() const { return false; }

	/**
	 *  Offer a running ability the chance to end itself in favour of the buffered press. The buffer
	 *  tick calls this on the active instance answering the same input; an attack in its chain-open
	 *  span ends early through the ordinary EndAbility funnel, letting the same tick's retry
	 *  activate the next swing. Returns true if it ended.
	 */
	virtual bool TryChainOutForBufferedPress() { return false; }

	/**
	 *  Trace label for the rise this ability starts when used as a get-up. See BeginKnockdownRise.
	 *  Takes the character because the dodge answers with two labels depending on type.
	 */
	virtual const TCHAR* GetKnockdownRiseLabel(const class ATDCombatCharacter* Character) const { return TEXT("unknown"); }

	/**
	 *  Whether this get-up plays its own rise animation instead of the type's shared one. The
	 *  character plays RiseMontage (or RiseHardMontage), which is right for the auto-rise, the
	 *  neutral stand and the block get-up. The dodge animates itself: a roll on normal, a kip-up on
	 *  hard.
	 */
	virtual bool BringsOwnRiseMontage() const { return false; }

	/**
	 *  How long the rise this ability starts should last. Zero takes the character's shared
	 *  KnockdownRiseSeconds; an ability whose activation *is* the rise returns its own length
	 *  so the knockdown ends with it rather than after it.
	 */
	virtual float GetKnockdownRiseSeconds() const { return 0.0f; }

protected:

	/**
	 *  Refuse activation while the avatar is falling. Keyed to the airborne state rather than to
	 *  having jumped, so it also covers walking off a ledge. Off by default: an air attack is a
	 *  legitimate thing to want later.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Activation")
	bool bBlockedWhileAirborne = false;

	/**
	 *  Refuse activation while an ability owns movement input -- the counterpart to bLocksMovement
	 *  below, which says "I take movement while I run" where this says "I may not start while
	 *  someone else has it". Reads bAbilityMovementLocked directly rather than a mirrored tag. Off
	 *  by default.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Activation")
	bool bBlockedWhileMovementLocked = false;

	/**
	 *  Whether this ability may start from the floor, during the knockdown's input window.
	 *
	 *  Three phases, two answers: the lockout refuses everything regardless, and so does the rise,
	 *  committed once started. Only the input window consults this. Off by default, so the down
	 *  state refuses anything nobody has thought about; the four that take it are the dodge, guard,
	 *  jump and get-up attack, each pricing its own rise.
	 *
	 *  Type restrictions are not here -- hard knockdown's removals are answered by the option
	 *  itself against the type it reads off the character.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Activation")
	bool bAllowedFromKnockdown = false;

	/**
	 *  Suppress movement input -- WASD and jump -- for as long as this ability runs.
	 *
	 *  Input, not movement: a lunge, a dash or a knockback still moves the character. What stops is
	 *  walking out of your own commitment. Cleared in this class's EndAbility, where every exit path
	 *  converges. Off by default.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Activation")
	bool bLocksMovement = false;

	/**
	 *  Re-activate this ability when another ends, if its input is still held. For held states
	 *  rather than actions: a guard interrupted by a dodge or swing comes back when that finishes
	 *  and the button is still down. Opt-in, and only states should carry it.
	 *
	 *  Cannot resurrect an ability the game still forbids -- the re-attempt goes through
	 *  CanActivateAbility, so exhaustion, a broken guard or being airborne refuse it as they would a
	 *  fresh press.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Activation")
	bool bResumeWhileInputHeld = false;

public:

	/** Whether this ability wants re-activating when another ends and its input is still down. */
	bool ShouldResumeWhileInputHeld() const { return bResumeWhileInputHeld; }

protected:

	/**
	 *  Whether *this activation* took the movement lock. Runtime only. The release is guarded on
	 *  this rather than bLocksMovement, because EndAbility runs on the shared base for every
	 *  ability: without it, any ability ending would hand movement back while another still owned it.
	 */
	bool bTookMovementLock = false;

	/**
	 *  Applied to self the moment this ability activates. This is how costs are paid.
	 *
	 *  Not GAS's CostGameplayEffectClass, which is a gate checked in CanActivateAbility. Every
	 *  action is always available and stamina is the consequence of taking it, not the permission
	 *  to: dodging at 30 works, empties the bar and exhausts you.
	 *
	 *  Applied unconditionally on activation, so anything that must not be charged when activation
	 *  subsequently fails should not use this.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Effects")
	TSubclassOf<UGameplayEffect> EffectOnStart;

	/**
	 *  Applied to self as this ability ends, however it ends. Currently unset on every ability -- a
	 *  general hook. Applied on cancellation too, so being hit out of a defensive action can never
	 *  be a way to skip its cost.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Effects")
	TSubclassOf<UGameplayEffect> EffectOnEnd;

	/**
	 *  Starts an authored displacement that follows the avatar's facing. Shared by attacks and the
	 *  dodge.
	 *
	 *  Built on a root motion *source* rather than SetActorLocation, AddMovementInput or
	 *  LaunchCharacter, so it rides the same prediction and replication machinery animation root
	 *  motion does. See FTDRootMotionSource_FacingForce.
	 *
	 *  The montage must carry no root motion or this does nothing at all -- animation root motion
	 *  suppresses every root motion source, and scaling it to zero does not help. The enforcement
	 *  warning lives at StartAttackMontage.
	 *
	 *  @param DistanceCm        How far to travel. The authored ceiling; the standoff gate may
	 *                           shorten it but nothing lengthens it.
	 *  @param DurationSeconds   How long it takes. Speed is derived from these two.
	 *  @param StrengthCurve     Optional shape. Must average 1.0 or the distance is a lie.
	 *  @param StandoffCm        Target Lock's per-tick gate. 0 disables it, which the dodge passes:
	 *                           an evade must be able to travel *past* people.
	 *  @param YawOffsetDegrees  Direction relative to facing. 0 is straight ahead, which is every
	 *                           attack; the dodge derives it from its direction enum.
	 */
	void StartLunge(
		float DistanceCm,
		float DurationSeconds,
		UCurveFloat* StrengthCurve,
		float StandoffCm = 0.0f,
		float YawOffsetDegrees = 0.0f,
		float TurnBodyToTravelRate = 0.0f);

	/**
	 *  Ends the lunge started by StartLunge, now, wherever it has got to.
	 *
	 *  A stop, where the standoff gate is only a pause: the gate contributes nothing on a tick where
	 *  a body is in the way, but time keeps advancing and the source stays live, so travel resumes
	 *  when the obstruction leaves. Correct while a target may back away mid-attack, wrong once a
	 *  hit lands -- killing a target removes its capsule, the gate opens on a corpse and the
	 *  attacker slides through.
	 *
	 *  Terminal: nothing restarts a lunge within one activation, so this can only subtract from the
	 *  authored distance. Safe to call with no lunge running, or twice.
	 */
	void StopLunge();

private:

	/**
	 *  The lunge currently running for this activation, so a hit can end it. Weak because the task
	 *  is owned by the ability system and can be torn down by an ability ending, a cancellation or a
	 *  montage interrupt without passing through here.
	 */
	TWeakObjectPtr<UAbilityTask_FacingLunge> ActiveLungeTask;

	/**
	 *  Dedupe state for the refusal trace. Mutable because CanActivateAbility is const. Refusals are
	 *  polled rather than edge-triggered -- the resume retries every tick while its input is held --
	 *  so an undeduped line would emit at frame rate.
	 */
	mutable FString LastRefusalReason;
	mutable float LastRefusalLoggedAt = 0.0f;
};
