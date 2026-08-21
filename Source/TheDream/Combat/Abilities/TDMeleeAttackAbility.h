// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "Combat/TDAttackHitbox.h"
#include "Combat/TDKnockdownTypes.h"
#include "TDMeleeAttackAbility.generated.h"

class ATheDreamCharacter;
class UAnimMontage;
class UAbilityTask_MeleeTrace;
class UCurveFloat;
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
	 *  Every exit runs through here -- montage completed, blended out, interrupted, cancelled, and
	 *  the CancelAllAbilities that death fires -- so the restore cannot miss a path. A lock left
	 *  standing is a character who can never turn again, with nothing to announce it.
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
	 *  Stamina removed instead of health when this hit is blocked. Fallback for the branch value.
	 *
	 *  Zero is legal and means this attack can never break a guard, so blocking it is free forever.
	 *  Authorable on purpose, but not a sensible default, which is why this carries the light's
	 *  value rather than nothing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Damage", meta=(ClampMin="0.0"))
	float StaminaDamage = 5.0f;

	/**
	 *  Blockstun imposed on a defender who blocks this hit. Fallback for the branch value.
	 *
	 *  Zero means a blocked hit costs the defender nothing but stamina -- legal, not a sensible
	 *  default, so this carries the light's value as StaminaDamage does. See
	 *  FTDAttackBranch::BlockstunSeconds for what the number decides.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Damage", meta=(ClampMin="0.0"))
	float BlockstunSeconds = 0.3f;

	/**
	 *  A target carrying any of these takes no damage from this attack -- i-frames.
	 *
	 *  **Leaving this empty silently disables invulnerability**, which looks exactly like a dodge
	 *  that does not work. It is the far half of a contract whose near half is
	 *  UTDDodgeAbility::IFrameTag, and nothing enforces that the two agree, so both have to be
	 *  checked together whenever either moves. Currently State.Dodging.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Damage")
	FGameplayTagContainer TargetImmunityTags;

	/**
	 *  The volumes this attack strikes with, in the attacker's own frame. Authored, never derived
	 *  from the weapon or the animation -- see FTDAttackHitbox.
	 *
	 *  Used only when the swing being thrown does not supply its own; UTDChargedAttackAbility
	 *  authors a set per branch.
	 *
	 *  **Empty means this attack cannot hit anything.** Left legal rather than guarded, which is
	 *  also why the per-branch lookup falls back here instead of to nothing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Hitbox")
	TArray<FTDAttackHitbox> Hitboxes { FTDAttackHitbox() };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Hitbox")
	bool bDrawDebugTrace = false;

	/**
	 *  Hitstun imposed on a cleanly hit target. Fallback for the branch value.
	 *
	 *  **Zero -- the default -- means no hitstun at all**, and unlike StaminaDamage the default is
	 *  deliberately inert: hitstun refuses every ability including defense, so it must never arrive
	 *  by omission. The CDO authors the real ladder.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Damage", meta=(ClampMin="0.0"))
	float HitstunSeconds = 0.0f;

	/**
	 *  Whether a clean hit from this attack knocks its victim down, and how hard.
	 *
	 *  **None -- the default -- means the hit hitstuns instead.** A graded hit knocks down and
	 *  *never* hitstuns: the two are alternatives resolved at the hit, not layers.
	 *
	 *  The ability-level fallback; branches and string swings override it, and the charged ability
	 *  resolves which one applies.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	ETDKnockdownGrade KnockdownGrade = ETDKnockdownGrade::None;

	/**
	 *  How long this attack's owner is locked out when a parrier catches it. Authored, not derived.
	 *
	 *  The ability-level fallback; branches and string swings override it.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float ParryLockoutSeconds = 0.65f;

	/**
	 *  The spacing reset: where a clean non-final string hit parks its target, in centimetres from
	 *  the attacker along facing. **0 disables knockback entirely, and is the C++ default.**
	 *
	 *  See GetKnockbackSpacingCm for the fixed-destination model. The live value is on the CDO.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockback", meta=(ClampMin="0.0"))
	float HitSpacingCm = 0.0f;

	/**
	 *  The blocked variant of the reset: same mechanism, same full lateral centring, notably less
	 *  ground conceded. 0 disables, and is the default for the reason above.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockback", meta=(ClampMin="0.0"))
	float BlockedSpacingCm = 0.0f;

	/**
	 *  How long the knockback translation takes. Must sit inside the hitstun that accompanies it,
	 *  or the target regains control mid-slide.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockback", meta=(ClampMin="0.01"))
	float KnockbackDurationSeconds = 0.2f;

	/**
	 *  Optional pacing for the translation. **A time-mapping curve, not a strength curve**: it maps
	 *  normalised time to normalised progress and must run monotonically 0 to 1 -- a different
	 *  contract from the lunges' mean-1.0 strength curves. Null is linear.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockback")
	TObjectPtr<UCurveFloat> KnockbackTimeMappingCurve = nullptr;

	// Facing has no tuning knobs by design: an attack freezes it at commit and returns it in
	// EndAbility, both instantly.

	/**
	 *  The base lunge: how far the attack carries itself from the press, in centimetres. Authored,
	 *  not taken from the clip.
	 *
	 *  **Shared by every tier.** It runs from the press to the first branch's HoldUntilSeconds --
	 *  the span in which no branch has been chosen -- so a defender cannot tell the tiers apart by
	 *  how far they travelled. Per-tier displacement starts at the commit checkpoint; see
	 *  FTDAttackBranch::LungeDistanceCm.
	 *
	 *  **The montage must play an in-place clip or none of this does anything** -- see
	 *  StartAttackMontage's warning.
	 *
	 *  Displayed as "Base Lunge Distance Cm", since this and FTDAttackBranch::LungeDistanceCm read
	 *  identically in the details panel otherwise, as do the duration and the strength curve.
	 *
	 *  **Keep it below the capsule radius, 42 cm.** Direction is fixed at the press while facing
	 *  stays steerable, so a flick during the windup lunges one way and swings another; under the
	 *  radius that reads as a step, over it as a lunge going wrong.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Motion", meta=(ClampMin="0.0", DisplayName="Base Lunge Distance Cm"))
	float LungeDistanceCm = 30.0f;

	/**
	 *  How far *past* its maximum hit range aim assist will still select a target, in centimetres.
	 *
	 *  A margin rather than a total, and the only authored part of a wedge's reach:
	 *
	 *      LungeDistanceCm + branch LungeDistanceCm + branch damage MaxReachCm + this
	 *
	 *  Exposing the total instead would bake the other three in, so retuning the base lunge would
	 *  silently shrink the real margin while the tuned number stayed put.
	 *
	 *  Shared across the ladder, not per branch: per-tier margins differing by more than 100 would
	 *  let a later branch reach *less* than an earlier one and drop a target already locked. The CDO
	 *  is authoritative for the live value.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Aim Assist", meta=(ClampMin="0.0"))
	float AimAssistMarginCm = 200.0f;

	/**
	 *  How long the base lunge takes, in seconds. Authored, and clamped rather than replaced.
	 *
	 *  The ladder clamps it to the first branch's HoldUntilSeconds, because the base lunge must
	 *  finish before the branch lunge begins -- two Override root motion sources at equal priority
	 *  is not a design. That boundary is a ceiling, not the value.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Motion", meta=(ClampMin="0.01", DisplayName="Base Lunge Duration Seconds"))
	float LungeDurationSeconds = 0.15f;

	/**
	 *  Optional shape for the base lunge, sampled over 0..1 of its duration. Null means constant.
	 *
	 *  **A curve here must average 1.0 across its range or the authored distance is a lie.** The
	 *  root motion source multiplies the force by this value each tick, so distance travelled is
	 *  LungeDistanceCm times the curve's mean. An ease-out from 2 to 0 keeps the distance; one that
	 *  merely falls from 1 to 0 halves it silently.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Motion", meta=(DisplayName="Base Lunge Strength Curve"))
	TObjectPtr<UCurveFloat> LungeStrengthCurve = nullptr;

	/**
	 *  Target Lock, translational half: how far short of a body a lunge stops, in centimetres.
	 *
	 *  The defect it exists for is sliding, not overshooting: a lunge driving into a blocking capsule
	 *  keeps its tangential component, so travel converts almost directly into arc around the target
	 *  while the wedge, frozen in the attacker's frame, does not follow.
	 *
	 *  Gated against geometry rather than a selected target, and per movement tick rather than
	 *  subtracted up front -- see FTDRootMotionSource_FacingForce::StandoffCm, where it is used. It
	 *  only ever shortens, so a whiff still travels in full.
	 *
	 *  **Keep it below MaxReachCm or the clamp starts causing whiffs**, by stopping the attacker
	 *  short of the target's own hitbox -- silently, and looking like a hit-detection fault. Nothing
	 *  enforces the relationship, because reach is per branch and this is per ability.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Motion", meta=(ClampMin="0.0"))
	float LungeStandoffCm = 40.0f;

	/** Damage for the swing currently being thrown. Overridden when a swing has variants. */
	virtual float GetAttackDamage() const { return Damage; }

	/** Stamina damage for the swing currently being thrown, used when it is blocked. */
	virtual float GetAttackStaminaDamage() const { return StaminaDamage; }

	/** Blockstun for the swing currently being thrown, imposed when it is blocked. */
	virtual float GetAttackBlockstunSeconds() const { return BlockstunSeconds; }

	/** Hitstun for the swing currently being thrown, imposed on a clean hit. Zero means none. */
	virtual float GetAttackHitstunSeconds() const { return HitstunSeconds; }

	/** Knockdown grade for the swing being resolved. See ETDKnockdownGrade. */
	virtual ETDKnockdownGrade GetAttackKnockdownGrade() const { return KnockdownGrade; }

	/** Parry lockout for the swing being resolved. Authored; see ParryLockoutSeconds. */
	virtual float GetAttackParryLockoutSeconds() const { return ParryLockoutSeconds; }

	/**
	 *  How far from the attacker this swing parks a target it touches, or 0 for no knockback.
	 *
	 *  **Fixed destination, not an impulse**: the target is carried to `attacker + facing x spacing`
	 *  -- the same spot every time -- so the magnitude varies with where the hit caught them and the
	 *  *result* never does. A blocked contact uses the smaller spacing. The never-inward clamp lives
	 *  at the call site in ATDCombatCharacter::ReceiveKnockback.
	 *
	 *  Zero disables, per the project idiom, and is the C++ default so the mechanism ships
	 *  structurally complete and behaviourally inert until the CDO authors real spacings.
	 */
	virtual float GetKnockbackSpacingCm(bool bBlocked) const { return bBlocked ? BlockedSpacingCm : HitSpacingCm; }

	/**
	 *  The montage this activation is actually playing.
	 *
	 *  AttackMontage until a subclass says otherwise -- UTDChargedAttackAbility returns the current
	 *  string swing's montage, which is what lets every rate derivation, window guard and delegate
	 *  log in this class serve all swings without knowing strings exist.
	 */
	virtual UAnimMontage* GetActiveAttackMontage() const { return AttackMontage; }

	/** Hitboxes for the swing currently being thrown. Overridden when a swing has variants. */
	virtual const TArray<FTDAttackHitbox>& GetAttackHitboxes() const { return Hitboxes; }

	/** The avatar as a ATheDreamCharacter, or null. The only thing that owns facing. */
	ATheDreamCharacter* GetFacingCharacter() const;

	/** Starts the hitbox task. It idles until a Melee Window notify opens on the montage. */
	UAbilityTask_MeleeTrace* StartMeleeTrace(const TArray<FTDAttackHitbox>& InHitboxes);

	/**
	 *  How long the base lunge runs. Derived on anything with a branch ladder.
	 *
	 *  Virtual so UTDChargedAttackAbility can return its first branch's HoldUntilSeconds rather than
	 *  an authored copy of it: nothing enforces the link between two such numbers, so there is one.
	 */
	virtual float GetBaseLungeDurationSeconds() const { return LungeDurationSeconds; }

	/**
	 *  Traces distance and bearing to the nearest other pawn, for diagnosing the slide.
	 *
	 *  Sampled at commit and again when the release window opens, so the *change* in bearing across
	 *  the two is how far the attacker slid around the target while its wedge was frozen. One sample
	 *  cannot show that, and the melee trace silently skips every candidate its filter rejects, so a
	 *  miss otherwise leaves no record of how it missed.
	 */
	void LogTargetGeometry(const TCHAR* Phase) const;

	/**
	 *  Target Lock, rotational half: turns the avatar all the way onto the best candidate in the
	 *  wedge. Call at commit, before facing is frozen.
	 *
	 *  **It corrects where you are pointed, never whether you were close enough**, so it fires at
	 *  out-of-range targets rather than declining -- "locked on, committed, and short".
	 *
	 *  The last correction, not the only one: homing has been closing this gap at the turn rate for
	 *  the whole base lunge, so what lands here is the residual. Nothing tracks past commit, which
	 *  is where the defender's reaction window opens.
	 *
	 *  **Rotates the character, not the camera.** Facing re-settles toward the camera when the lock
	 *  releases at EndAbility.
	 *
	 *  Selection is smallest bearing, ties broken by distance -- the tiebreak for determinism, since
	 *  unstable ordering would let two machines rotate the attack two different ways.
	 */
	void ApplyAimAssist(const FTDAttackHitbox& AssistWedge);

	/**
	 *  Plays AttackMontage, optionally from a named section, and ends the ability when it finishes.
	 *
	 *  PlayRate is passed rather than authored because a derived rate has to be in force from the
	 *  montage's first frame: setting it afterwards leaves a window in which the swing runs at the
	 *  wrong speed, and the whole timing model reads the montage's measured position.
	 */
	bool StartAttackMontage(FName StartSection, float PlayRate);

	UFUNCTION()
	void HandleTraceHit(const FHitResult& Hit);

	/**
	 *  Drops this attack's commitment marker early, so defensive actions open mid-recovery.
	 *
	 *  A no-op here because a plain swing has no commit checkpoint to release;
	 *  UTDChargedAttackAbility overrides it, since the tag is that class's per-branch CommittedTag.
	 *  A hook rather than a moved property, because moving a UPROPERTY orphans every Blueprint CDO
	 *  override of it.
	 */
	virtual void ReleaseCommitmentTag() {}

	/**
	 *  This swing was parried. Set in the hit path, cleared on every activation.
	 *
	 *  What it forbids is chaining -- see UTDChargedAttackAbility::IsChainOutOpen. A parried
	 *  attacker rides their own recovery, which *is* the punish window, so chaining out of it would
	 *  hand back the reward.
	 *
	 *  Runtime only and not replicated: it is read on the server, in the same tick-ordered path that
	 *  set it.
	 */
	bool bParried = false;

	/**
	 *  Computes the fixed destination for this swing's knockback and hands it to the target.
	 *
	 *  Authority-implied: only ever called from HandleTraceHit past its authority gate. The
	 *  never-inward clamp lives here, beside the destination it guards.
	 */
	void ApplyKnockbackToTarget(class ATDCombatCharacter* Target, bool bBlocked);

	UFUNCTION()
	void HandleMontageFinished();

	UFUNCTION()
	void HandleMontageInterrupted();

	/**
	 *  Thin wrappers that exist only to name which montage delegate fired. All four funnel into two
	 *  handlers, so without them the log cannot say whether an attack ended because the montage
	 *  completed, blended out, was interrupted or was cancelled -- different causes, different fixes.
	 */
	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageBlendedOut();

	UFUNCTION()
	void HandleMontageCancelled();
};
