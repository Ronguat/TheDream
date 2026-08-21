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

	/**
	 *  How long this branch is helpless after its hitbox closes. **This is the punish window.**
	 *
	 *  Authored in absolute seconds and the montage warps to fit, exactly as windup and release
	 *  already do -- so an attack is three durations and the animation is fitted to them rather
	 *  than consulted about them.
	 *
	 *  **It is measured to the montage's blend-out, because that is where the ability actually
	 *  ends** and therefore where the attacker can act again. The clip keeps playing for the
	 *  blend's duration afterwards; that tail is follow-through a spectator sees and is
	 *  mechanically over. Settled deliberately on 2026-08-12 rather than discovered by tuning:
	 *  the alternative was waiting for OnCompleted and making the blend real committed time,
	 *  which would have lengthened every punish window by the blend. Recovery *is* the punish
	 *  window, and what gates the next action is the ability ending, so the authored number is
	 *  the one that cannot disagree with the mechanic.
	 *
	 *  Raising it makes this branch more punishable. It is a balance number and nothing else.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.01"))
	float RecoverySeconds = 0.2667f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float Damage = 15.0f;

	/**
	 *  Stamina taken from a target who *blocks* this branch. Zero health damage is dealt instead.
	 *
	 *  **Stamina damage is not stamina drain, and the distinction is the whole design.** Drain is
	 *  self-inflicted by holding a guard and can run the bar to zero harmlessly. Damage is what an
	 *  attacker inflicts on that guard, and it is the *only* thing that can break one -- a guard
	 *  breaks exactly when a blocked hit leaves the defender at zero. So this number is not "how
	 *  fast blocking costs you", it is "how much of a defender's remaining safety this attack
	 *  removes at a stroke".
	 *
	 *  Authored per branch beside Damage, so "heavies drain more" lives where the rest of the
	 *  ladder is authored rather than in a table somewhere else. The shipped values are the user's:
	 *  5 / 50 / 100 against a 100 bar, which makes the charged break a *full* guard exactly and the
	 *  heavy break one that has already taken anything at all. That relationship is deliberate and
	 *  is what makes the spec's "charged heavy breaks block" true without a special case in code --
	 *  change the bar's maximum and it silently stops being true.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float StaminaDamage = 5.0f;

	/**
	 *  How long a defender who *successfully* blocks this branch is locked out of offense and parry.
	 *
	 *  **This is the attacker's reward for being blocked**, and it is the number that decides whether
	 *  a blocked attack is safe. The defender keeps their guard and their movement; what they lose is
	 *  the initiative, for as long as what they blocked deserves. Set it above the attacker's own
	 *  RecoverySeconds and the attack is safe on block; set it below and the defender can punish.
	 *
	 *  Authored per branch beside StaminaDamage for the same reason: the ladder's asymmetries belong
	 *  where the ladder is authored. The spec's shape is "light's first hit safe on block, heavy safe
	 *  on block, charged breaks it anyway", so the values climb with the tier.
	 *
	 *  **Placeholders as of 2026-08-14 -- none of these has been felt.** They are scaled to the tier
	 *  rather than tuned against recovery, and the first play pass should expect to move them.
	 *  Zero disables blockstun for the branch entirely, which is legal and means "blocking this
	 *  costs the defender nothing but stamina".
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float BlockstunSeconds = 0.3f;

	/**
	 *  The volumes this branch strikes with. Empty falls back to the ability's own set.
	 *
	 *  Per branch rather than per ability because the spec gives heavy a higher range than light
	 *  and charged the highest, and until this existed those differences were whatever the clips
	 *  happened to do. MaxReachCm is now the authored answer.
	 *
	 *  The fallback is not merely convenience: removing the old per-branch TraceRadius left every
	 *  authored branch with an empty array, and without falling back an attack would have gone
	 *  silently damage-less until the content pass caught up.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	TArray<FTDAttackHitbox> Hitboxes;

	/**
	 *  How far this branch carries itself from the commit checkpoint, in centimetres.
	 *
	 *  **This is where an attack's reach is actually differentiated**, and it is the majority of
	 *  the travel: the base lunge is deliberately kept small enough that a flick cannot make it
	 *  look wrong, so the drama lives here instead. Runs from commit to the end of the release
	 *  window -- 200 ms on every tier at present, though the duration is derived rather than
	 *  assumed, so retuning ReleaseSeconds carries it.
	 *
	 *  **It cannot apply any earlier, and that is the reactability model rather than a
	 *  limitation.** All three tiers share one windup precisely so the light carries no tell that
	 *  distinguishes it from a heavy. A charged that lunged further from the press would announce
	 *  itself from frame one -- exactly the failure that moving the coil earlier would cause,
	 *  arriving through the movement system instead.
	 *
	 *  Two things it gets free from starting at commit: facing is already frozen, so a world-space
	 *  force cannot diverge from the body; and the coil sits *before* it, so the phase whose
	 *  duration differs between tiers carries no lunge at all. That is what makes an authored,
	 *  wall-clock displacement safe here when it would break parity in the windup.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float LungeDistanceCm = 75.0f;

	/**
	 *  How long this branch's lunge takes, in seconds. **Authored, and it did not used to be.**
	 *
	 *  It ran commit -> the end of the release window, derived rather than chosen, and the original
	 *  entry recorded that as a virtue: Lunge shipped adding two distances and zero timing values
	 *  because the boundaries it needed already existed. Play falsified it on 2026-08-13. The lunge
	 *  felt simultaneously *too slow* and *too far*, which sounds contradictory and is one fact --
	 *  speed is distance over duration, and the duration was set by ReleaseSeconds, which answers a
	 *  completely different question: **how long the hitbox stays live.**
	 *
	 *  Nothing relates "how long the damaging volume persists" to "how long the character is carried
	 *  forward". Welding them meant the lunge could not be made snappier without shortening the
	 *  hitbox, nor the hitbox lengthened without making the lunge floatier -- two independent design
	 *  questions sharing one number. For scale, the derived value put the light's lunge at 1000 cm/s
	 *  against a 500 cm/s walk and a 1012 cm/s dodge: an attack that lunged at exactly dodge speed,
	 *  which is why it did not read as a burst.
	 *
	 *  **A burst that finishes early in the release window is the point.** While the two were equal
	 *  the avatar was moving for the entire time its hitbox was live, so the damaging volume was
	 *  dragged through space for its whole existence and there was never a moment of planting and
	 *  striking. Keep this comfortably below ReleaseAtSeconds + ReleaseSeconds - HoldUntilSeconds.
	 *
	 *  Longer than that span is legal and merely means the lunge is still running during recovery.
	 *  It is not clamped, because "the attack drags you on past its own swing" is a coherent thing
	 *  to author even though it is not what any current branch wants.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.01"))
	float LungeDurationSeconds = 0.12f;

	/**
	 *  Optional shape for this branch's lunge, sampled over 0..1 of its duration. Null is constant.
	 *
	 *  **Must average 1.0 across its range or the authored distance is a lie** -- the force is
	 *  multiplied by this each tick. See UTDMeleeAttackAbility::LungeStrengthCurve.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	TObjectPtr<UCurveFloat> LungeStrengthCurve = nullptr;

	/**
	 *  Hitstun imposed on a target this branch cleanly hits. **0 -- the default -- means none.**
	 *
	 *  For the light it is the string guarantee's whole mechanism: it must cover the gap to the
	 *  next chained contact (350 ms at the shipped cadence) or "any hit guarantees the rest" is a
	 *  lie -- 0.40 carries the same 50 ms margin blockstun does. Authored per branch beside
	 *  BlockstunSeconds because the ladder's asymmetries belong where the ladder is authored.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float HitstunSeconds = 0.0f;

	/**
	 *  Whether this branch's clean hit knocks down, and how hard. **None means hitstun instead.**
	 *
	 *  The shipped pairing is light None, heavy Hard, charged Hard: a committed hit floors you
	 *  hard, because the commitment is what bought the oki. The light stays a hitstun so the string
	 *  it opens can still chain -- a knockdown mid-string would end the string it belongs to.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	ETDKnockdownGrade KnockdownGrade = ETDKnockdownGrade::None;

	/**
	 *  Seconds this branch's owner is locked out when it is parried. **Authored**; see
	 *  UTDMeleeAttackAbility::ParryLockoutSeconds for why this stopped being derived.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack", meta=(ClampMin="0.0"))
	float ParryLockoutSeconds = 0.65f;

	/**
	 *  Whether committing this branch keeps the string alive. **The DKO/New World flip in data.**
	 *
	 *  True on the light alone expresses the current model: lights chain and a heavy or charged
	 *  commit ends the string (heavy never chains into light). Setting it on a heavy is how the
	 *  New World-shaped alternative arrives -- a details-panel change, deliberately never
	 *  structure, per the 2026-08-16 plan-session entry. False by default so deserialised branches
	 *  are inert until the CDO chooses.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	bool bChainsIntoString = false;

	/**
	 *  Target Lock, rotational half: who this branch is willing to be steered onto.
	 *
	 *  **Shape only -- reach is derived**, see FTDAimAssistWedge and
	 *  UTDMeleeAttackAbility::AimAssistMarginCm. Arc, its centre and the vertical band stay per
	 *  branch; how far it reaches is computed from this branch's own travel and damage reach so it
	 *  cannot drift out of step with either.
	 *
	 *  **Its half-arc is the maximum correction**, by construction rather than by a second number: a
	 *  candidate outside the wedge is not eligible, so nothing can be rotated onto from further away
	 *  than the wedge is wide. The arc *is* the definition of "aimed well enough", and the subtended
	 *  widening means a narrow authored arc self-widens as a target closes -- tight where intent is
	 *  being declared, forgiving where the player obviously meant the person they are standing on.
	 *
	 *  **Live from the moment its branch is escalated to, not only at commit** (2026-08-14). Before
	 *  that, homing ran the entire windup on branch 0's wedge, so every tier homed at the *light's*
	 *  reach and only the light's value did anything observable -- two of three authored numbers had
	 *  never been seen when they were committed. See Docs/Combat-Decisions.md.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack")
	FTDAimAssistWedge AimAssistWedge;
};

/**
 *  One hit of the light string beyond the first: its clip, that clip's authored positions, and
 *  the branch-0 values that vary by position.
 *
 *  **Hit 1 is deliberately absent from this struct's world.** The first swing stays the ability's
 *  original authored surface -- AttackMontage, the ability-level ReleaseStartSeconds and
 *  CoilEndSeconds, and Branches[0]'s values -- because moving those UPROPERTYs would orphan every
 *  play-verified CDO override. StringSwings[k] therefore describes hit k+2, and the accessors
 *  resolve index 0 to the legacy fields. The asymmetry is load-bearing, not an accident.
 *
 *  Heavy and charged are reachable from any swing (hold-to-convert is per swing) and keep their
 *  Branches values; only the montage-position numbers here apply to them, since those are
 *  properties of the clip rather than of the tier.
 */
USTRUCT(BlueprintType)
struct FTDStringSwing
{
	GENERATED_BODY()

	/** This swing's montage. Must play an in-place clip, like AM_Attack, or the lunge dies. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	/**
	 *  Where this montage's Release Window notify opens, hand-copied from its placement exactly as
	 *  the ability-level field is for hit 1 -- and checked against the notify at runtime by the
	 *  same drift warning. Read the real value off the MONTAGE trace's `notify trigger=` line.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float ReleaseStartSeconds = 0.3f;

	/** Where this montage's coil creeps to. Must stay below its ReleaseStartSeconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float CoilEndSeconds = 0.28f;

	/** Branch-0 values for this position -- damage on hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float Damage = 15.0f;

	/** Stamina damage this position deals to a guard. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float StaminaDamage = 5.0f;

	/** Blockstun this position imposes when blocked. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float BlockstunSeconds = 0.4f;

	/** Hitstun this position imposes on a clean hit. 0 means none; see FTDAttackBranch's field. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float HitstunSeconds = 0.0f;

	/**
	 *  Whether this position's clean hit knocks down. **The ender authors Normal; the rest None.**
	 *
	 *  The string's volume finisher knocks down on the gentle grade -- generous escape is what the
	 *  string trades for having already extracted its damage. It is also the kit's only 360-degree
	 *  knockdown, so pairing it with the gentle grade is what stops a crowd ever being hard-floored;
	 *  that pairing is authored here rather than structural, and a future weapon may differ.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing")
	ETDKnockdownGrade KnockdownGrade = ETDKnockdownGrade::None;

	/**
	 *  Seconds this position's owner is locked out when it is parried. **Authored**; see
	 *  UTDMeleeAttackAbility::ParryLockoutSeconds.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float ParryLockoutSeconds = 0.65f;

	/**
	 *  This position's recovery -- commitment and punish window both, exactly as on the branch.
	 *  The string's ender authors the spec's "heavy endlag" here.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.01"))
	float RecoverySeconds = 0.4f;

	/**
	 *  This position's damaging volumes. **Empty inherits the branch's**, which is what every
	 *  swing did before per-swing volumes existed -- the same fallback shape as a branch's own
	 *  empty array falling back to the ability's set, and for the same reason: a swing that
	 *  silently deals no damage is the failure this project keeps a trap list for.
	 *
	 *  Per-swing because what an attack *hits* is a different question from how much aim error is
	 *  forgiven. The aim wedge stays a learnable constant across the ladder and the string, by
	 *  decision; a 360-degree finisher is a statement about geometry, not about forgiveness.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing")
	TArray<FTDAttackHitbox> Hitboxes;

	/**
	 *  How long this position's damaging phase lasts. **0 inherits the branch's.**
	 *
	 *  It sets the release *play rate* as well as the window, since the rate is the notify's
	 *  authored width divided by this. So lengthening it both widens the damaging window in wall
	 *  clock and slows the contact motion toward true speed -- authoring it to equal the notify's
	 *  width is what makes a swing's release play at exactly 1.0.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float ReleaseSeconds = 0.0f;

	/**
	 *  This position's lunge distance. **0 inherits the branch's.**
	 *
	 *  Mostly a *whiff* value at the string's own spacing: a connecting chain parks the target at
	 *  HitSpacingCm and the standoff gate then clamps travel to whatever is left -- about 26 cm at
	 *  a 150 cm reset -- so an increase here is invisible on connects until knockback grows.
	 *  It also feeds the aim wedge's derived reach, exactly as the branch's does.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float LungeDistanceCm = 0.0f;

	/** This position's lunge duration. **0 inherits the branch's.** Separate from distance because
	 *  the two are tuned against different things: reach, and how long you are committed to it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Swing", meta=(ClampMin="0.0"))
	float LungeDurationSeconds = 0.0f;
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
 *   - Recovery, stretched to the branch's authored RecoverySeconds, measured to the montage's
 *     blend-out because that is where the ability ends and the attacker can act again.
 *
 *  So an attack is three authored durations and the animation is fitted to all three. Recovery
 *  was the last one to stop being "whatever is left of the montage" (2026-08-12); until then a
 *  clip picked for its swing was silently setting the punish window.
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
 *  See Docs/Combat-Decisions.md for the reasoning behind all of it.
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Animation", meta=(ClampMin="0.0"))
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

	// RecoveryPlayRate lived here until 2026-08-12 and is gone -- recovery is authored as
	// FTDAttackBranch::RecoverySeconds and its rate derived; see the retired names table.

	/** Optional section played on activation. None starts the montage from the beginning. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Animation")
	FName WindupSection = NAME_None;

	/**
	 *  Applied the instant the attack commits, and removed when it ends.
	 *
	 *  This is the boundary every defensive action cancels *before* and none cancels after.
	 *  A defensive ability blocks on this rather than on State.Attacking, which is present
	 *  from the press: blocking on State.Attacking would forbid cancelling a windup, and
	 *  blocking on nothing would let a committed swing be erased. Stating it once here beats
	 *  each defensive ability knowing when an attack became irrevocable.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Timing")
	FGameplayTag CommittedTag;

	/** Outcomes, ordered shortest first. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Timing")
	TArray<FTDAttackBranch> Branches;

	/**
	 *  The light string's hits beyond the first, in order. **Empty means no string** -- the
	 *  pre-2026-08-16 single-swing behaviour, and the C++ default, so the machinery ships inert.
	 *
	 *  String length is this array's size plus one, which is what makes 2-, 3- and 4-hit strings
	 *  details-panel variants and the clip roster an authored trial. Hit 1 deliberately lives in
	 *  the legacy fields instead of element 0; see FTDStringSwing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|String")
	TArray<FTDStringSwing> StringSwings;

	/**
	 *  How long after a chainable swing ends a fresh press still continues the string, fighting-
	 *  game link style, before the string resets to hit 1. Also how long a buffered chain press
	 *  outlives the swing that refused it -- the two are one window on purpose.
	 *
	 *  0.40 is the proposed first probe, deliberately generous in the MinimumBlockSeconds
	 *  tradition. The delay-and-bait game's ceiling: chain any time from recovery start to this
	 *  window's close.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|String", meta=(ClampMin="0.0"))
	float StringLinkWindowSeconds = 0.4f;

	/**
	 *  Extra delay after recovery starts before a chain may leave the swing. 0 -- the default and
	 *  the proposal -- chains at recovery start, which is the release window's close. The cadence
	 *  knob beyond hitstun: raising it slows every string without touching any clip.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|String", meta=(ClampMin="0.0"))
	float ChainOpenAfterRecoverySeconds = 0.0f;

	virtual float GetAttackDamage() const override;
	virtual float GetAttackStaminaDamage() const override;
	virtual float GetAttackBlockstunSeconds() const override;
	virtual float GetAttackHitstunSeconds() const override;
	virtual ETDKnockdownGrade GetAttackKnockdownGrade() const override;
	virtual float GetAttackParryLockoutSeconds() const override;
	virtual float GetKnockbackSpacingCm(bool bBlocked) const override;

	/**
	 *  The on-hit waiver's offensive half: drop CommittedTag now rather than at EndAbility.
	 *
	 *  Only the tag goes. The facing lock and the movement lock are separate flags with their own
	 *  lifetimes, and neither is touched here -- movement comes back on its own derived delay, and
	 *  facing stays frozen because a connected swing has no business re-aiming itself.
	 */
	virtual void ReleaseCommitmentTag() override;

	virtual const TArray<FTDAttackHitbox>& GetAttackHitboxes() const override;
	virtual UAnimMontage* GetActiveAttackMontage() const override;

	/** Chain presses survive a running attack; see the base's contract. */
	virtual bool ShouldExtendBufferWhileActive() const override { return true; }

	/** Ends this swing early for a waiting chain press, if the chain-out span is open. */
	virtual bool TryChainOutForBufferedPress() override;

	/**
	 *  The aim assist wedge actually tested for a branch: its authored shape at its derived reach.
	 *
	 *  Reach is travel plus damage reach plus AimAssistMarginCm -- see that property for why the
	 *  margin is the authored part and the rest is arithmetic. Built on demand rather than cached
	 *  because every input can be changed in the details panel between activations, and a cache is
	 *  one more thing that can disagree with the numbers a designer is looking at.
	 *
	 *  **Monotonicity is now structural rather than a rule anyone has to follow.** Reach is a
	 *  constant plus the branch's own lunge, and lunges increase up the ladder, so a later branch
	 *  reaching *less* than an earlier one cannot be expressed -- which is what the ladder-following
	 *  homing needs in order not to drop a target it had already locked.
	 *
	 *  Returns a disabled wedge for an out-of-range index or a branch with bEnabled false.
	 */
	FTDAttackHitbox BuildAimAssistWedge(int32 BranchIndex) const;

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
	 *  Takes the release rate back off when the damaging window closes.
	 *
	 *  Without this the rate derived for the *release* stays applied for the whole remainder
	 *  of the montage, so the follow-through and all of recovery inherit a speed that was
	 *  computed from how wide the notify happens to be authored. It went unnoticed for weeks
	 *  because the punch's notify was 0.0842 against a ReleaseSeconds of 0.09 -- a rate of
	 *  0.935, near enough to 1 to look like nothing. The first clip whose notify did not
	 *  happen to match sent recovery through at 3.28x.
	 */
	UFUNCTION()
	void HandleReleaseWindowEnded(FGameplayEventData Payload);

	/** Whether a window event came from the montage this attack is playing. */
	bool IsWindowForThisAttack(const FGameplayEventData& Payload) const;

	/**
	 *  Shared windup rate: fast enough that the first branch reaches ReleaseStartSeconds
	 *  exactly on its ReleaseAtSeconds. Everything slower is produced by the coil holding
	 *  it back, never by a later branch accelerating.
	 */
	float ComputeWindupPlayRate() const;

	/** The first branch's HoldUntilSeconds: where the tiers stop being indistinguishable. */
	virtual float GetBaseLungeDurationSeconds() const override;

	/**
	 *  Recovery rate: carries the montage from where it actually is to the blend-out boundary
	 *  in the branch's authored RecoverySeconds.
	 *
	 *  Takes the measured position rather than assuming the release ended where the notify says,
	 *  for the same reason every other rate here does -- the window closes a frame or two late
	 *  and a rate derived from the assumed end compounds that error across the longest phase.
	 *
	 *  Returns a negative value when the montage has no room left, which is a real authoring
	 *  outcome rather than an error: a clip whose tail is shorter than the blend cannot host any
	 *  recovery at all. The caller warns and leaves the rate alone.
	 */
	float ComputeRecoveryPlayRate(float FromPosition, float TargetSeconds) const;

	/**
	 *  Montage position at which blend-out begins, which is where the ability ends.
	 *
	 *  **Takes a play rate because the boundary is not fixed.** Unless the montage authors a
	 *  BlendOutTriggerTime, the engine starts blending when the *remaining time* at the current
	 *  rate equals the blend's duration -- so a slower recovery pushes the boundary later in the
	 *  clip. Passing the wrong rate here silently misplaces the end of the ability.
	 */
	float GetBlendOutStartSeconds(float PlayRate) const;

	/** Real seconds since this activation. */
	float GetElapsedSeconds() const;

	/** Montage playhead position in seconds, or -1 if there is nothing to read. */
	float GetMontagePosition() const;

	/** Sets the montage's play rate, if one is playing. Never called with 0. */
	void SetMontagePlayRate(float PlayRate) const;

	/** Total swings: the legacy first hit plus the StringSwings array. Never below 1. */
	int32 GetSwingCount() const { return 1 + StringSwings.Num(); }

	/** Whether a swing at this index has a string successor to chain into. */
	bool HasSuccessorSwing(int32 SwingIndex) const { return SwingIndex + 1 < GetSwingCount(); }

	/** This swing's ReleaseStartSeconds: the legacy field for index 0, the swing's otherwise. */
	float GetSwingReleaseStartSeconds(int32 SwingIndex) const;

	/** This swing's CoilEndSeconds, resolved the same way. */
	float GetSwingCoilEndSeconds(int32 SwingIndex) const;

	/**
	 *  Per-swing overrides, each resolving swing -> branch -> ability. They take the branch index
	 *  explicitly rather than reading SelectedBranchIndex, because BuildAimAssistWedge asks about
	 *  branches other than the one currently selected.
	 */
	const TArray<FTDAttackHitbox>& GetSwingHitboxes(int32 SwingIndex, int32 BranchIndex) const;

	/** This swing's release duration, or the branch's when it authors none. */
	float GetSwingReleaseSeconds(int32 SwingIndex, int32 BranchIndex) const;

	/** This swing's lunge distance, or the branch's when it authors none. */
	float GetSwingLungeDistanceCm(int32 SwingIndex, int32 BranchIndex) const;

	/** This swing's lunge duration, or the branch's when it authors none. */
	float GetSwingLungeDurationSeconds(int32 SwingIndex, int32 BranchIndex) const;

	/**
	 *  Whether the selected branch at the current swing is a non-final string light -- the gate
	 *  for both knockback and the chain-out. A heavy, a charged, the string's ender and a
	 *  non-chaining branch 0 all fail it, which is exactly the set that must not reset spacing.
	 */
	bool IsNonFinalStringLight() const;

	/** True inside the chain-out span: committed, chainable, recovery running past the delay. */
	bool IsChainOutOpen() const;

	int32 SelectedBranchIndex = 0;
	bool bAttackCommitted = false;
	bool bInputHeld = true;
	bool bCoiling = false;
	float ActivationWorldTime = 0.0f;

	/**
	 *  Which swing of the string this activation is, resolved from the character at activation
	 *  and stable for the activation's whole life. 0 is the legacy first hit.
	 */
	int32 CurrentSwingIndex = 0;

	/** True from RELEASE OFF to the ability's end -- the span the chain-out may open inside. */
	bool bInRecovery = false;

	/** World time recovery began, for ChainOpenAfterRecoverySeconds. */
	float RecoveryStartedAt = 0.0f;

	FTimerHandle CheckpointTimerHandle;
	FGameplayTag AppliedAttackTag;
};
