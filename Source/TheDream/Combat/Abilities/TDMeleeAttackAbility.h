// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "Combat/TDAttackHitbox.h"
#include "TDMeleeAttackAbility.generated.h"

class ATheDreamCharacter;
class UAnimMontage;
class UAbilityTask_MeleeTrace;
class UGameplayEffect;

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

	/**
	 *  Gives facing back, then ends as usual.
	 *
	 *  **The single funnel is the point.** Every exit runs through here -- montage completed,
	 *  blended out, interrupted, cancelled, and the CancelAllAbilities that death fires -- so
	 *  restoring here cannot miss a path the way patching the four montage delegates could. A
	 *  lock left standing is a character who can never turn again, with nothing to announce it,
	 *  and the montage's blend-out already ends attacks earlier than they look finished.
	 */
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

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
	 *  The volumes this attack strikes with, in the attacker's own frame.
	 *
	 *  Authored, never derived from the weapon or the animation -- see FTDAttackHitbox for why.
	 *  This replaced a swept capsule chain running blade-base to blade-tip off the `Sword`
	 *  socket, which measured whatever the vendor's animator drew and could not describe a shield
	 *  bash or a spin at all.
	 *
	 *  Used only when the swing being thrown does not supply its own; UTDChargedAttackAbility
	 *  authors a set per branch so heavy and charged can genuinely out-range the light rather
	 *  than out-ranging it by accident of clip choice.
	 *
	 *  **Empty means this attack cannot hit anything.** That is left legal rather than guarded,
	 *  because a swing with no damaging volume is a coherent thing to author; it is also why the
	 *  per-branch lookup falls back here instead of to nothing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Hitbox")
	TArray<FTDAttackHitbox> Hitboxes { FTDAttackHitbox() };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Hitbox")
	bool bDrawDebugTrace = false;

	// Facing has no tuning knobs by design. An attack freezes it at commit and returns it at the
	// release window's end, both instantly. FacingLockFadeSeconds and
	// FacingUnlockRecoveryFraction existed here for one day, 2026-08-12, and were removed by
	// play: every value below full authority disables the camera snap, so the fades left chained
	// attacks perpetually behind the camera. Smoothing is item 14's, and it will need a
	// mechanism that does not gate the snap on the same number.

	/**
	 *  Multiplier on the montage's authored root-motion translation, from the press onward.
	 *
	 *  **Displacement is authored per attack rather than taken from the clip** (2026-08-11): the
	 *  vendor's attacks travel a fraction of what this design wants, and foot sliding is accepted
	 *  as the price. Scaling root motion is deliberately preferred over driving movement in code,
	 *  because a scaled root motion is still root motion and CMC replicates it for free.
	 *
	 *  **Scaling changes how much motion happens, never when.** A clip that stands still through
	 *  the phase that needs travel cannot be fixed here at any value, and that is the one case
	 *  that genuinely forces code. Check before reaching for a larger number.
	 *
	 *  On UTDChargedAttackAbility this governs the **shared windup**, and every tier gets it --
	 *  see FTDAttackBranch::RootMotionScale for why it cannot be otherwise.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Motion", meta=(ClampMin="0.0"))
	float RootMotionScale = 1.0f;

	/** Damage for the swing currently being thrown. Overridden when a swing has variants. */
	virtual float GetAttackDamage() const { return Damage; }

	/**
	 *  Applies a root-motion scale to the avatar, on the machines entitled to compute movement.
	 *
	 *  Mirrors the role check UAbilityTask_PlayMontageAndWait makes when it applies its own
	 *  scale -- authority, plus the autonomous proxy when the ability is LocalPredicted, which
	 *  ours are. The task also resets the scale to 1 when it is destroyed, so anything set here
	 *  is cleaned up on every exit path including a cancel.
	 */
	void ApplyRootMotionScale(float Scale);

	/** Hitboxes for the swing currently being thrown. Overridden when a swing has variants. */
	virtual const TArray<FTDAttackHitbox>& GetAttackHitboxes() const { return Hitboxes; }

	/** The avatar as a ATheDreamCharacter, or null. The only thing that owns facing. */
	ATheDreamCharacter* GetFacingCharacter() const;

	/** Starts the hitbox task. It idles until a Melee Window notify opens on the montage. */
	UAbilityTask_MeleeTrace* StartMeleeTrace(const TArray<FTDAttackHitbox>& InHitboxes);

	/** Root-motion scale the montage starts under. Branch-specific travel is applied at commit. */
	virtual float GetWindupRootMotionScale() const { return RootMotionScale; }

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
