// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "Engine/TimerHandle.h"
#include "TDDodgeAbility.generated.h"

class UAnimMontage;

/**
 *  Which way the dodge went. Chosen from movement input, and picks a montage section.
 *
 *  Eight directions rather than four because WASD produces diagonals naturally, and the
 *  source clips exist for all eight. The names match the animation pack's own direction
 *  codes deliberately -- the montage's section names are these strings, so a section maps
 *  1:1 onto the clip it was built from and a mistake is visible by reading.
 */
UENUM(BlueprintType)
enum class ETDDodgeDirection : uint8
{
	Fw,
	FR,
	R,
	BR,
	Bw,
	BL,
	L,
	FL
};

/**
 *  A directional evade that costs stamina and grants invulnerability.
 *
 *  **One duration, not two.** The i-frames last exactly as long as the dodge, so there is
 *  no second number to keep in step and no way to express invulnerability outlasting the
 *  move. An earlier version authored them separately, which made "0.3 inside 0.5" a pair
 *  of numbers whose relationship nothing explained.
 *
 *  So a whiffed dodge is never *directly* punishable. Its cost is the stamina and being
 *  unable to act, and spam is bounded by the stamina economy rather than by a vulnerable
 *  tail. If dodge later proves too safe, the honest fix is a recovery window authored in
 *  absolute time -- not a fraction, since what makes recovery punishable is how it compares
 *  to an attack's startup, and that does not scale when the dodge is retuned.
 *
 *  Direction is resolved from movement input, falling back to backward when stationary,
 *  because a neutral dodge that goes nowhere reads as a flinch rather than an evade.
 *
 *  **Displacement is authored and driven in code** (2026-08-13), on the same terms as an attack's
 *  lunge -- a root motion source carrying DodgeTargetDistanceCm over DodgeSeconds, aimed by a yaw
 *  offset taken from the direction enum. It used to come from the montage's root motion corrected
 *  by eight per-direction scales, which made this the last system in the project still reading a
 *  number off an animation. See Docs/Combat-Decisions.md.
 *
 *  **The eight source clips therefore have bEnableRootMotion switched off**, which is the library's
 *  own default and was only ever enabled by us. If a dodge ever stops moving entirely, check that
 *  first: animation root motion suppresses root motion sources outright, and scaling it to zero
 *  does not help.
 */
UCLASS(abstract)
class UTDDodgeAbility : public UTDGameplayAbility
{
	GENERATED_BODY()

public:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:

	/**
	 *  How long the dodge lasts end to end, including the part you can be punished in.
	 *
	 *  **Authoritative, and the montage is made to fit it** -- the section's play rate is
	 *  derived as SectionLength / DodgeSeconds, exactly as the attack ladder derives every
	 *  rate from authored timings. The source dashes run 0.833s, which was never a design
	 *  decision, and letting a clip's length set a defensive option's commitment would make
	 *  the animation the balance authority.
	 *
	 *  Note this changes duration, not distance: a dash played faster covers the same
	 *  ground in less time. Travel is DodgeTargetDistanceCm, directly below.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="0.01"))
	float DodgeSeconds = 0.5f;

	/**
	 *  How long after this dodge ends a parry may not be started. Applied as State.DodgeRecovery.
	 *
	 *  **Derived, and it must not be tuned freely.** The constraint is that dodge-end plus this gap
	 *  plus ParryWindowSeconds has to overshoot the charged's 750 ms arrival for the worst-timed
	 *  predictive dodge -- otherwise a dodge thrown on a guess eats the fast layer and the parry it
	 *  chains into covers the slow one at no extra read, which is the option-select the whole input
	 *  scheme was chosen to prevent.
	 *
	 *  It lives on the dodge rather than on GA_Parry because the dodge is what knows one just ended.
	 *  Re-derive it whenever DodgeSeconds, the parry window, or the charged's arrival moves.
	 *
	 *  ***It shared the whiff's tag and mechanism until 2026-08-19 -- "no second mechanism, only a
	 *  second cause" -- and now has its own.*** Both merely refused defensive activations, so one
	 *  tag said the whole sentence. The designer's ruling that a whiffed parry must prevent acting
	 *  applies to that cause and not to this one: **this gap still takes nothing but the parry**,
	 *  leaving movement, offense and block alone. Keeping them merged would have committed the
	 *  player for 0.15 s after every dodge as a side effect of a ruling about something else.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="0.0"))
	float DodgeRecoverySeconds = 0.15f;

	/**
	 *  How far a dodge should carry, in cm. **This is the distance knob.**
	 *
	 *  **Every direction travels exactly this**, as of 2026-08-13. It used to be a target that eight
	 *  per-direction scales aimed at, because the clips disagreed by 90.6 cm about how far a dodge
	 *  carries; with displacement authored there is nothing left to disagree. Defaults to 405, which
	 *  is what Dodge Distance's play pass judged good on the V1 clips (measured mean 404.9).
	 *
	 *  Retuning it is one number and always was -- what changed is that it is now also the *only*
	 *  number, and it cannot be silently biased by a stale measurement.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="1.0"))
	float DodgeTargetDistanceCm = 405.0f;

	/**
	 *  Applied for the whole dodge, and read by attackers to skip the hit.
	 *
	 *  Must match the tag the attacking ability treats as immunity
	 *  (UTDMeleeAttackAbility::TargetImmunityTags). They are two halves of one contract,
	 *  and nothing enforces that they agree.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge")
	FGameplayTag IFrameTag;

	/**
	 *  Optional. Its sections must be named exactly for ETDDodgeDirection: Fw, FR, R, BR,
	 *  Bw, BL, L, FL. A missing section warns and plays from the montage start rather than
	 *  failing silently.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge")
	TObjectPtr<UAnimMontage> DodgeMontage;

	/** Direction the current dodge resolved to. Exposed so the debug HUD can show it. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Dodge")
	ETDDodgeDirection DodgeDirection = ETDDodgeDirection::Bw;

public:

	// No constructor. It existed only to seed the eight MeasuredTravelCm calibration entries, and
	// authored displacement removed the thing they calibrated.

private:

	/** Movement input relative to the character's facing, or Backward when stationary. */
	ETDDodgeDirection ResolveDodgeDirection() const;

	/** Removes the i-frame tag. Idempotent, because EndAbility can run more than once. */
	void EndIFrames();

	UFUNCTION()
	void HandleDodgeFinished();

	FTimerHandle DodgeTimerHandle;
	bool bIFramesActive = false;

	/**
	 *  Where the dodge started, so the trace can report how far it actually travelled.
	 *
	 *  Measured rather than derived from the clip: what the player feels is the distance the
	 *  character ended up moving, which collision, slopes and the movement component all get a
	 *  say in. A number read off the asset would describe the animation's intent instead.
	 */
	FVector DodgeStartLocation = FVector::ZeroVector;
};
