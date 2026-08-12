// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "TDMeleeAttackAbility.generated.h"

class UAnimMontage;
class UAbilityTask_MeleeTrace;
class UGameplayEffect;
class USkeletalMeshComponent;

/**
 *  A single melee swing: play a montage, trace during its active frames, apply damage.
 *
 *  The damage and trace values are virtual so a subclass can vary them per swing --
 *  UTDChargedAttackAbility picks them from whichever branch the player's hold selected.
 */
UCLASS(abstract)
class UTDMeleeAttackAbility : public UTDGameplayAbility
{
	GENERATED_BODY()

public:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:

	/** Montage to play. Its Melee Window notify states define the active frames. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Animation")
	TObjectPtr<UAnimMontage> AttackMontage;

	/** Applied to each actor hit. Expects a Data.Damage SetByCaller magnitude on Health. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Damage")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	/** Health removed per hit. Positive here; applied as a negative magnitude. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Damage", meta=(ClampMin="0.0"))
	float Damage = 15.0f;

	/**
	 *  A target carrying any of these takes no damage from this attack -- i-frames.
	 *
	 *  **Leaving this empty silently disables invulnerability**, which looks exactly like
	 *  a dodge that does not work. It is the far half of a contract whose near half is
	 *  UTDDodgeAbility::IFrameTag, and nothing enforces that the two agree, so both have
	 *  to be checked together whenever either moves. Currently State.Dodging.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Damage")
	FGameplayTagContainer TargetImmunityTags;

	/**
	 *  Socket the blade hangs off. The pack's `Sword` socket, on hand_r.
	 *
	 *  Was `hand_r` until item 6, which was legacy from unarmed prototyping and already wrong:
	 *  the character has held a sword since item 3b and the blade contributed nothing to what an
	 *  attack hit.
	 *
	 *  **Moving it changed reach, which is the point and is not a bug.** Hits land from a
	 *  different distance now, so a target standing where it used to be struck may sit outside
	 *  the new hitbox. Verified in play 2026-08-11: both characters damage and kill each other
	 *  once spacing is adjusted, while still playing the unarmed punch montage.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Trace")
	FName TraceSocket = FName("Sword");

	/**
	 *  Socket-space direction the blade points, from grip toward tip. **+Z for this pack.**
	 *
	 *  Read off SM_Sword's own bounds rather than guessed: the mesh spans 0..100 on Z, ~25 cm on
	 *  Y (the crossguard) and ~3.7 cm on X, and it is correct at identity on the `Sword` socket,
	 *  so the socket's +Z *is* the blade. This shipped briefly as ForwardVector -- +X, the blade's
	 *  *thickness* axis -- which projected the hitbox sideways out of the grip and connected with
	 *  nothing. The failure is silent, so verify with bDrawDebugTrace, which draws the blade in
	 *  yellow, rather than by whether hits seem to land.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Trace")
	FVector BladeAxisLocal = FVector::UpVector;

	/**
	 *  Distance from the socket to the blade's base, along BladeAxisLocal.
	 *
	 *  0 traces from the pommel, which is deliberate for now: it is generous rather than wrong,
	 *  and where the grip ends is not readable from the mesh's bounds. Raise it during item 6's
	 *  content pass if hits land off the handle in a way play notices.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Trace")
	float BladeStartCm = 0.0f;

	/**
	 *  Blade base-to-tip length in cm. **This is the attack's reach.**
	 *
	 *  Authored, and deliberately **never** measured from the attached weapon mesh. The `Sword`
	 *  socket lives on the skeleton and exists whether or not a prop hangs off it, so a
	 *  mesh-derived length would hand an unarmed character a well-formed *zero-length* trace --
	 *  a hitbox that misses everything, with nothing null and nothing logged to say so.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Trace", meta=(ClampMin="0.0"))
	float BladeLengthCm = 100.0f;

	/**
	 *  Sample points along the blade, each swept from its own previous position.
	 *
	 *  Closes the gap *along* the blade the way the previous-to-current sweep closes the gap
	 *  through time. Cost is linear: this many sweeps per tick per attacker. 1 collapses to the
	 *  old single-point trace.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Trace", meta=(ClampMin="1"))
	int32 BladeTraceSegments = 5;

	/**
	 *  Sphere radius in cm swept at each blade sample -- the blade's **thickness**, not its reach.
	 *
	 *  Reach is BladeLengthCm. The 45 / 55 / 65 values these carried were tuned against a fist
	 *  standing in for the whole hitbox, so they are not merely rescaled by the move to a blade;
	 *  they measure a different thing now and need re-judging in play.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Trace", meta=(ClampMin="1.0"))
	float TraceRadius = 45.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Trace")
	bool bDrawDebugTrace = false;

	/** Damage for the swing currently being thrown. Overridden when a swing has variants. */
	virtual float GetAttackDamage() const { return Damage; }

	/** Trace radius for the swing currently being thrown. Longer attacks reach further. */
	virtual float GetAttackTraceRadius() const { return TraceRadius; }

	/** The mesh carrying TraceSocket, or null if the avatar has none. */
	USkeletalMeshComponent* FindAvatarMesh() const;

	/** Starts tracing. The task idles until a Melee Window notify opens on the montage. */
	UAbilityTask_MeleeTrace* StartMeleeTrace(float Radius);

	/**
	 *  Plays AttackMontage, optionally from a named section, and ends the ability when it finishes.
	 *
	 *  PlayRate is passed rather than authored because a derived rate has to be in force from
	 *  the montage's first frame. Setting it after the montage starts leaves a window in which
	 *  the swing runs at the wrong speed, and the whole timing model is built on the montage's
	 *  measured position.
	 */
	bool StartAttackMontage(FName StartSection, float PlayRate);

	UFUNCTION()
	void HandleTraceHit(const FHitResult& Hit);

	UFUNCTION()
	void HandleMontageFinished();

	UFUNCTION()
	void HandleMontageInterrupted();

	/**
	 *  Thin wrappers that exist only to name which montage delegate fired.
	 *
	 *  All four funnel into two handlers, so the log could never say whether an attack ended
	 *  because the montage completed, blended out, was interrupted or was cancelled -- and
	 *  those have different causes and different fixes. Diagnosing an attack that ended 8 ms
	 *  after its release window opened cost three wrong guesses for want of this distinction.
	 */
	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageBlendedOut();

	UFUNCTION()
	void HandleMontageCancelled();
};
