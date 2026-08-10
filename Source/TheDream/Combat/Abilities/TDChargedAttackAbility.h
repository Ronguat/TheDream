// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDMeleeAttackAbility.h"
#include "Engine/TimerHandle.h"
#include "TDChargedAttackAbility.generated.h"

struct FGameplayEventData;

/**
 *  One outcome of a held attack, described by when it hits rather than by how it plays.
 *
 *  Every timing here is in real seconds from the press. The play rates that produce them
 *  are derived at runtime from the montage's measured position, never authored -- see
 *  UTDChargedAttackAbility.
 */
USTRUCT(BlueprintType)
struct FTDAttackBranch
{
	GENERATED_BODY()

	/** Identifies the attack, e.g. Ability.Attack.Heavy. Applied as a loose tag while it swings. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	FGameplayTag AttackTag;

	/**
	 *  Optional distinct release animation for this branch.
	 *
	 *  Leave as None and every branch shares one release. Setting it buys readability at
	 *  the direct cost of this branch's ambiguity -- a defender who can recognise the
	 *  animation no longer has to wait out the coil to know what is coming.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	FName MontageSection = NAME_None;

	/**
	 *  Hold past this and the attack escalates to the next branch instead.
	 *
	 *  This is the input boundary, not the attack's speed. Releasing any time before it
	 *  produces this branch, and produces it identically -- the windup runs its full
	 *  length either way, and that fixed cost is what stops a fractionally-held heavy
	 *  from dominating the light.
	 *
	 *  The first branch's value is also when the coil begins, because "no longer a light"
	 *  and "the defender can see what this is" are the same instant by construction.
	 *
	 *  Branches must be ordered shortest first.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float HoldUntilSeconds = 0.2f;

	/**
	 *  When this branch's hitbox goes live, measured from the press.
	 *
	 *  The headline number for an attack: it is what a defender has to react to and what
	 *  an attacker feels. Must be greater than HoldUntilSeconds -- the gap between them is
	 *  the runway the montage needs to travel from the coil into the strike.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float ReleaseAtSeconds = 0.25f;

	/**
	 *  How long the hitbox stays live.
	 *
	 *  Authored rather than inherited from whatever play rate the windup happened to end
	 *  on. Without it a branch that has to hurry into its strike gets a correspondingly
	 *  brief hitbox, and one that crawls in gets an absurdly long one, purely as a side
	 *  effect of the windup maths.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.01"))
	float ReleaseSeconds = 0.09f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float Damage = 15.0f;

	/** Heavier attacks reach further, so radius is per branch rather than per ability. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="1.0"))
	float TraceRadius = 45.0f;
};

/**
 *  An attack whose identity is decided by how long the button is held.
 *
 *  Branches are described by *when they hit*, and every play rate is derived from that at
 *  runtime. Three phases, in order:
 *
 *   - Windup, shared and identical for all branches, played fast enough that the quickest
 *     branch reaches its strike exactly on time. That rate is set by the fastest branch,
 *     which means the light needs no acceleration at commit at all -- it runs one
 *     continuous rate from press to impact.
 *   - Coil, beginning the instant the light is no longer available. This is the tell, and
 *     it is also the mechanism that makes the slower branches slower: they are not sped
 *     up later, they are held back here.
 *   - Release, stretched to the branch's authored ReleaseSeconds so the hitbox lasts as
 *     long as it should rather than as long as the windup arithmetic left over.
 *
 *  Recovery is deliberately unmanaged; whatever time is left simply plays out.
 *
 *  Two rules the implementation exists to enforce. **Every rate is computed from the
 *  montage's measured position, never from where it was assumed to be** -- the timer that
 *  starts the coil fires a frame or two late, and a rate derived from the assumed start
 *  compounds that error until the coil overruns the release window and the attack silently
 *  stops dealing damage. And **the montage is never stopped**: a stopped montage banks the
 *  time it sits still and spends it in one frame on resume, skipping the release window
 *  and firing every frame of root motion at once.
 *
 *  There is deliberately one animation rather than one per branch. The attack's identity
 *  is a consequence of how long its windup lasted, so it cannot be known in advance to
 *  pick a clip -- and sharing it means the defender cannot tell the branches apart until
 *  the coil appears. Reactability is measured from that tell, not from the press.
 *
 *  See Docs/Combat/Decisions.md for the reasoning behind all of it.
 */
UCLASS(abstract)
class UTDChargedAttackAbility : public UTDMeleeAttackAbility
{
	GENERATED_BODY()

public:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:

	/**
	 *  Montage position at which the Release Window notify opens.
	 *
	 *  Unavoidably duplicated from the notify's placement on the timeline, because a
	 *  montage's notifies cannot be read back and the windup rate has to be known at
	 *  activation, before any notify has fired. The ability checks itself against the
	 *  real thing when the window opens and warns if they have drifted apart.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Timing", meta=(ClampMin="0.0"))
	float ReleaseStartSeconds = 0.36f;

	/**
	 *  Montage position the coil creeps toward and arrives at as the deepest branch commits.
	 *
	 *  The coil's tuning knob: closer to where the coil begins reads as more of a hold,
	 *  further reads as a continuous wind-up. **Must stay below ReleaseStartSeconds.** If
	 *  the coil reaches the release window before the attack commits, the window opens
	 *  before there is a trace listening for it and the attack deals no damage at all.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Timing", meta=(ClampMin="0.0"))
	float CoilEndSeconds = 0.35f;

	/** Optional section played on activation. None starts the montage from the beginning. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Timing")
	FName WindupSection = NAME_None;

	/** Outcomes, ordered shortest first. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Timing")
	TArray<FTDAttackBranch> Branches;

	virtual float GetAttackDamage() const override;
	virtual float GetAttackTraceRadius() const override;

private:

	/** Arms the next checkpoint, or runs it immediately if it is already due. */
	void ScheduleCheckpoint(float DelaySeconds);

	/** Escalates to the next branch if the button is still down, otherwise commits. */
	void HandleCheckpoint();

	/** Starts the coil from wherever the montage actually is, aimed at CoilEndSeconds. */
	void EnterCoil();

	/** Locks in the selected branch: applies its tag, starts tracing, aims at its release. */
	void CommitAttack();

	/** Stretches the release window to the selected branch's ReleaseSeconds. */
	UFUNCTION()
	void HandleReleaseWindowBegan(FGameplayEventData Payload);

	/**
	 *  Shared windup rate: fast enough that the first branch reaches ReleaseStartSeconds
	 *  exactly on its ReleaseAtSeconds. Everything slower is produced by the coil holding
	 *  it back, never by a later branch accelerating.
	 */
	float ComputeWindupPlayRate() const;

	/** Real seconds since this activation. */
	float GetElapsedSeconds() const;

	/** Montage playhead position in seconds, or -1 if there is nothing to read. */
	float GetMontagePosition() const;

	/** Sets the montage's play rate, if one is playing. Never called with 0. */
	void SetMontagePlayRate(float PlayRate) const;

	int32 SelectedBranchIndex = 0;
	bool bAttackCommitted = false;
	bool bInputHeld = true;
	bool bCoiling = false;
	float ActivationWorldTime = 0.0f;

	FTimerHandle CheckpointTimerHandle;
	FGameplayTag AppliedAttackTag;
};
