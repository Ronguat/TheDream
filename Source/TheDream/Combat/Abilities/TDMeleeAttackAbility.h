// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "Combat/TDAttackHitbox.h"
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
	 *  Stamina removed instead of health when this hit is blocked. Fallback for the branch value.
	 *
	 *  Zero is meaningful and dangerous here: an attack that deals no stamina damage can never
	 *  break a guard, so blocking it is free forever. That is authorable on purpose -- a chip
	 *  attack is a legitimate thing to want -- but it is not a sensible default, which is why
	 *  this carries the light's value rather than nothing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Damage", meta=(ClampMin="0.0"))
	float StaminaDamage = 5.0f;

	/**
	 *  Blockstun imposed on a defender who blocks this hit. Fallback for the branch value.
	 *
	 *  Zero means a blocked hit costs the defender nothing but stamina, which is authorable on
	 *  purpose and is not a sensible default -- so this carries the light's value, as StaminaDamage
	 *  above does. See FTDAttackBranch::BlockstunSeconds for what the number actually decides.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Damage", meta=(ClampMin="0.0"))
	float BlockstunSeconds = 0.3f;

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

	/**
	 *  Hitstun imposed on a cleanly hit target. Fallback for the branch value, like the two above.
	 *
	 *  **Zero -- the default -- means no hitstun at all**, and unlike StaminaDamage the default is
	 *  deliberately inert rather than carrying the light's value: hitstun refuses every ability
	 *  including defense, so it must never arrive by omission. The CDO authors the real ladder.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Damage", meta=(ClampMin="0.0"))
	float HitstunSeconds = 0.0f;

	/**
	 *  The spacing reset: where a clean non-final string hit parks its target, in centimetres from
	 *  the attacker along facing. **0 disables knockback entirely, and is the C++ default.**
	 *
	 *  See GetKnockbackSpacingCm for the fixed-destination model. The proposed authored value is
	 *  150 -- `MaxReachCm`, the sword's edge -- and it lives on the CDO, not here.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockback", meta=(ClampMin="0.0"))
	float HitSpacingCm = 0.0f;

	/**
	 *  The blocked variant of the reset: same mechanism, same full lateral centring, notably less
	 *  ground conceded ("moves them laterally but pushback is reduced notably", the designer,
	 *  2026-08-16). 0 disables, and is the default for the reason above.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockback", meta=(ClampMin="0.0"))
	float BlockedSpacingCm = 0.0f;

	/**
	 *  How long the knockback translation takes. Must sit inside the hitstun that accompanies it,
	 *  or the target regains control mid-slide -- 0.20 against hitstun's 0.40 is the proposed pair.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockback", meta=(ClampMin="0.01"))
	float KnockbackDurationSeconds = 0.2f;

	/**
	 *  Optional pacing for the translation. **A time-mapping curve, not a strength curve**: it maps
	 *  normalised time to normalised progress and must run monotonically 0 to 1 -- a different
	 *  contract from the lunges' mean-1.0 strength curves, worth stating because they sit one
	 *  category apart in the details panel. Null is linear.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockback")
	TObjectPtr<UCurveFloat> KnockbackTimeMappingCurve = nullptr;

	// Facing has no tuning knobs by design: an attack freezes it at commit and returns it in
	// EndAbility, both instantly. The fades that briefly lived here (2026-08-12) are in the
	// decision log's retired names -- any value below full authority disabled the snap outright.

	/**
	 *  The base lunge: how far the attack carries itself from the press, in centimetres.
	 *
	 *  **Displacement is authored, not taken from the clip** (2026-08-12, replacing a
	 *  RootMotionScale multiplier). Scaling was tried first and rejected by play as "still a bit
	 *  too animation-driven": a multiplier cannot decouple you from an authored curve, it can only
	 *  make the animator's acceleration, pauses and stop louder. Measured before it was replaced,
	 *  the clip put 65.7 cm of its 75.1 into the windup and then effectively stopped -- a surge
	 *  into a coast, which is what that verdict was describing.
	 *
	 *  **Shared by every tier, and that is the reactability model rather than a limitation.** This
	 *  runs from the press to the first branch's HoldUntilSeconds, which is the whole span in
	 *  which no branch has been chosen yet, so a defender must not be able to tell the tiers apart
	 *  by how far they travelled. Per-tier displacement starts at the commit checkpoint -- see
	 *  FTDAttackBranch::LungeDistanceCm.
	 *
	 *  **The montage must play an in-place clip or none of this does anything** -- the rule, its
	 *  mechanism and its enforcement all live at StartAttackMontage's warning.
	 *
	 *  **Displayed as "Base Lunge Distance Cm", and the rename is load-bearing.** This and
	 *  FTDAttackBranch::LungeDistanceCm are different properties at different nesting levels that read
	 *  identically in the details panel -- as do the duration and the strength curve. A designer
	 *  expanded inside a Branches element sees the same three labels and no indication which lunge
	 *  they are editing, which cost real time on 2026-08-14. The prefix is only on this trio because
	 *  the shared one is the one worth marking; inside Branches the context is already the branch.
	 *
	 *  **Keep it modest, and the bound is the capsule radius (42 cm).** The direction is fixed at
	 *  the press while facing stays steerable, so a flick during the windup lunges one way and
	 *  swings another. Under about 42 cm even a full 180 degree divergence displaces the character
	 *  less than its own width, which reads as a step rather than as a lunge going wrong. Above
	 *  it the artifact becomes visible, and the knob that fixes it is this number.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Motion", meta=(ClampMin="0.0", DisplayName="Base Lunge Distance Cm"))
	float LungeDistanceCm = 30.0f;

	/**
	 *  How far *past* its maximum hit range aim assist will still select a target, in centimetres.
	 *
	 *  **The one authored number in aim assist's reach, and the reason it is a margin rather than a
	 *  total.** A wedge's reach is derived as
	 *
	 *      LungeDistanceCm + branch LungeDistanceCm + branch damage MaxReachCm + this
	 *
	 *  -- travel plus reach is exactly the furthest a body can be and still be struck, so this is the
	 *  only part of the sum that is a judgement rather than arithmetic. Exposing the *total* instead
	 *  would bake the other three in: retune the base lunge and the real margin would silently shrink
	 *  while the number you tuned stayed put. That is the failure shape TurnRateDegrees already has a
	 *  trap for, and it is avoidable here for free.
	 *
	 *  **Why a margin exists at all, which is the design rather than a safety factor.** If the wedge
	 *  matched hit range exactly, locking on would *encode* whether the target is reachable -- a free
	 *  rangefinder, and aim assist answering the one question Target Lock forbids it: it may correct
	 *  where you are pointed, never whether you were in range. Overshooting keeps assist strictly
	 *  about direction, and lets a defender read an attacker turning onto them and still whiffing.
	 *
	 *  Larger is also the right direction to err. A wedge *shorter* than hit range would drop targets
	 *  at maximum range -- exactly where travel is longest and aiming matters most, which is the
	 *  "backwards" failure an earlier deadzone design was rejected for.
	 *
	 *  **Played and settled at 100 on 2026-08-14**, down from the 200 it was signed off at before
	 *  anyone had felt it. The CDO is authoritative for the live value; both figures here are dated
	 *  measurements. At 100 the margin equals the 100 cm separating the tiers' lunges, so each tier's
	 *  assist range sits exactly one tier's travel past its own hit range -- the tiers stay as far
	 *  apart in assist as they are in reach, where the larger value flattened that difference.
	 *
	 *  **Shared across the ladder rather than authored per branch**, considered and rejected the same
	 *  day. It is the same class of quantity as the arc -- how wrong the *player* may be -- and a
	 *  per-branch margin would put back the monotonicity trap that deriving reach had just made
	 *  unrepresentable: reach is 450/550/650 plus the margin, so per-tier margins differing by more
	 *  than 100 let a later branch reach *less* than an earlier one and drop a target already locked.
	 *  Cheap to add later if a tier is ever felt to want its own; expensive to remove once three
	 *  numbers have been tuned against it.
	 *
	 *  **Deliberately in its own category rather than beside the lunge it is derived from.** Sitting
	 *  under Combat|Motion made it unfindable: a designer looks for it near aim assist, not near the
	 *  arithmetic that produces it. Categories follow the person, not the formula.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Aim Assist", meta=(ClampMin="0.0"))
	float AimAssistMarginCm = 200.0f;

	/**
	 *  How long the base lunge takes, in seconds. **Authored, and clamped rather than replaced.**
	 *
	 *  This was ignored on anything with a branch ladder until 2026-08-13: UTDChargedAttackAbility
	 *  returned Branches[0].HoldUntilSeconds outright, so the light's *input boundary* decided how
	 *  long the approach burst lasted. Those are different questions, and the same conflation
	 *  existed on the branch lunge -- see FTDAttackBranch::LungeDurationSeconds for the full
	 *  account, since it is one mistake made twice rather than two.
	 *
	 *  The ladder still clamps it to that boundary, because the base lunge genuinely must finish
	 *  before the branch lunge begins -- two Override root motion sources at equal priority is not
	 *  a design. So the boundary remains a ceiling and stopped being the value.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Motion", meta=(ClampMin="0.01", DisplayName="Base Lunge Duration Seconds"))
	float LungeDurationSeconds = 0.15f;

	/**
	 *  Optional shape for the base lunge, sampled over 0..1 of its duration. Null means constant.
	 *
	 *  **A curve here must average 1.0 across its range or the authored distance is a lie.** The
	 *  root motion source multiplies the force by this value each tick, so the distance actually
	 *  travelled is LungeDistanceCm times the curve's mean. An ease-out that starts at 2 and
	 *  falls to 0 keeps the distance; one that merely falls from 1 to 0 halves it silently.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Motion", meta=(DisplayName="Base Lunge Strength Curve"))
	TObjectPtr<UCurveFloat> LungeStrengthCurve = nullptr;

	/**
	 *  Target Lock, translational half: how far short of a body a lunge stops, in centimetres.
	 *
	 *  **The bug this exists for is sliding, not overshooting.** A lunge that drives into a
	 *  blocking capsule keeps its tangential component, and the tangential fraction grows as you
	 *  slide toward the target's side -- so travel converts almost directly into arc around them.
	 *  The contact circle is 528 cm around, so the 100 cm base lunge alone can carry you 68 degrees
	 *  round a target *before commit*, and a light's full 300 cm is 205 degrees. Facing freezes at
	 *  commit and the wedge is defined in the attacker's frame, so the wedge does not follow you
	 *  round. That is why attacks at anything but maximum range felt awkward.
	 *
	 *  **Gated against geometry, never against a selected target**, and that is deliberate. A
	 *  selection test has a boundary, and a boundary is a cliff: a candidate at 29 degrees would
	 *  stop dead while one at 31 travelled the full authored distance, from the same input. The
	 *  sweep has no cliff because it *is* the collision test -- anything it declines to gate on is
	 *  something the character would not have collided with.
	 *
	 *  **Gated per movement tick rather than subtracted from the distance up front**, which is where
	 *  the first implementation went wrong -- see FTDRootMotionSource_FacingForce::StandoffCm, which
	 *  is where the value is actually used and where that account lives.
	 *
	 *  **It only ever shortens.** The authored distance stays the ceiling, so a whiff still travels
	 *  in full and the punish window is untouched; at maximum range, with nothing in the path, or
	 *  into empty air the gate never closes and the attack behaves exactly as it did before Target
	 *  Lock existed. Those are consequences of the arithmetic rather than cases handled by name.
	 *
	 *  **Keep it below MaxReachCm or the clamp starts causing whiffs**, by stopping the attacker
	 *  short of the target's own hitbox -- silently, and looking like a hit-detection fault. Nothing
	 *  enforces the relationship because reach is authored per branch and this is per ability, so
	 *  the realistic way to break it is to tune reach *down* and not think of this. See the trap in
	 *  Docs/Combat-Decisions.md.
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

	/**
	 *  How far from the attacker this swing parks a target it touches, or 0 for no knockback.
	 *
	 *  **Fixed destination, not an impulse** (2026-08-16): the target is carried to
	 *  `attacker + facing x spacing` -- the same spot every time, every hit -- so the magnitude
	 *  varies with where the hit caught them and the *result* never does. A blocked contact uses
	 *  the smaller spacing: same full lateral centring, notably less ground conceded. The
	 *  never-inward clamp lives at the call site in ATDCombatCharacter::ReceiveKnockback.
	 *
	 *  Zero disables, per the project idiom -- and the C++ default is zero so the mechanism ships
	 *  structurally complete and behaviourally inert until the CDO authors real spacings.
	 */
	virtual float GetKnockbackSpacingCm(bool bBlocked) const { return bBlocked ? BlockedSpacingCm : HitSpacingCm; }

	/**
	 *  The montage this activation is actually playing.
	 *
	 *  The authored AttackMontage until a subclass says otherwise -- UTDChargedAttackAbility
	 *  returns the current string swing's montage, which is what lets every rate derivation,
	 *  window guard and delegate log in this class serve all swings without knowing strings exist.
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
	 *  Virtual so UTDChargedAttackAbility can return its first branch's HoldUntilSeconds rather
	 *  than an authored copy of it. Nothing enforces the link between the two numbers, so the
	 *  cheapest way to keep them married is not to have two numbers.
	 */
	virtual float GetBaseLungeDurationSeconds() const { return LungeDurationSeconds; }

	/**
	 *  Traces distance and bearing to the nearest other pawn, for diagnosing the slide.
	 *
	 *  Sampled at commit and again when the release window opens, so the *change* in bearing across
	 *  those two points is how far the attacker slid around the target while its wedge was frozen.
	 *  One sample cannot show that, which is why nothing could previously measure the defect that
	 *  Target Lock exists to fix -- the melee trace silently skips every candidate its filter
	 *  rejects, so a miss leaves no record of how it missed.
	 */
	void LogTargetGeometry(const TCHAR* Phase) const;

	/**
	 *  Target Lock, rotational half: turns the avatar all the way onto the best candidate in the
	 *  wedge. Call at commit, before facing is frozen.
	 *
	 *  **It corrects where you are pointed, never whether you were close enough.** The rotation
	 *  cannot rescue a spacing miss, which is what keeps whiff punish intact -- and it is why this
	 *  deliberately fires at targets that are *out of range*, producing the "locked on, committed,
	 *  and short" outcome rather than silently declining to help.
	 *
	 *  **The last correction, not the only one.** Homing has been closing this gap at the turn
	 *  rate for the whole base lunge, so what lands here is the residual -- usually nothing.
	 *  Firing at commit is what ends assist exactly where the defender's reaction window opens:
	 *  past this instant, tracking would be homing, and homing means a defender's movement can no
	 *  longer make a committed attack whiff.
	 *
	 *  **All the way onto the target, not to the edge of a tolerance** (2026-08-13, superseding a
	 *  minimum-sufficient design: measured against the damage wedge, that correction is always
	 *  zero, and the deadzone that replaced it protected leading, which this game does not have).
	 *  The wedge is the margin of error, and it is aimed rather than corrected into.
	 *
	 *  **Rotates the character, not the camera**, which is what keeps it invisible: the player is
	 *  steering the camera, and the body following it is not something they are watching. Facing
	 *  re-settles toward the camera when the lock releases at EndAbility, over a few tens of
	 *  milliseconds.
	 *
	 *  Selection is smallest bearing, ties broken by distance. Angle is the player's expressed
	 *  intent and distance is their own responsibility, which is also what lets them deliberately
	 *  take the *further* of two targets. The distance tiebreak exists for determinism rather than
	 *  for occlusion -- unstable ordering would let two machines pick different targets and rotate
	 *  the attack two different ways.
	 */
	void ApplyAimAssist(const FTDAttackHitbox& AssistWedge);

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
