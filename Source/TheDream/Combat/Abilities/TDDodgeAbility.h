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
 *  Displacement comes from the montage's root motion, scaled by DodgeRootMotionScale. Driving
 *  it in code was the alternative and is still available without new assets, but the clips
 *  ship with authored displacement and using it costs nothing. See Docs/Combat-Decisions.md.
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
	 *  rate from authored timings. The source rolls run 0.9s, which was never a design
	 *  decision, and letting a clip's length set a defensive option's commitment would make
	 *  the animation the balance authority.
	 *
	 *  Note this changes duration, not distance: a roll played faster covers the same
	 *  ground in less time. Travel is AnimRootMotionTranslationScale, not this.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="0.01"))
	float DodgeSeconds = 0.5f;

	/**
	 *  Multiplies the montage's authored root motion translation. 1.0 is the clip as authored.
	 *
	 *  **This is the travel knob, and the play rate is not.** Rate changes how long the dodge
	 *  takes, never how far it goes -- a dash played twice as fast covers the same ground in
	 *  half the time. Reaching for DodgeSeconds to fix distance is the standing mistake this
	 *  property exists to prevent.
	 *
	 *  **A global multiplier, applied on top of the per-direction correction below.** Leave it at
	 *  1.0 and tune DodgeTargetDistanceCm; this exists for wholesale scaling (a slowed effect, a
	 *  future weight class) rather than for setting the distance.
	 *
	 *  It used to be the only knob and was deliberately uniform, on the reasoning that if the
	 *  source clips disagreed about distance a single scale would multiply that disagreement
	 *  rather than correct it -- and that the fix would then be per-direction data. That case
	 *  arrived with the V3 clips: measured travel ran 371.9 uu left against 462.6 uu right,
	 *  reproducible to within 0.1 uu and plainly visible when dodging up a ramp.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="0.0"))
	float DodgeRootMotionScale = 1.0f;

	/**
	 *  How far a dodge should carry, in cm. **This is the distance knob.**
	 *
	 *  Per-direction scales are derived from this and MeasuredTravelCm, so retuning distance is
	 *  one number rather than eight -- the same reason recovery is authored in absolute time
	 *  rather than as a fraction. Defaults to 405, which is what Dodge Distance's play pass judged good
	 *  on the V1 clips (measured mean 404.9); V3's untouched mean is 413, within 2%.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="1.0"))
	float DodgeTargetDistanceCm = 405.0f;

	/**
	 *  Travel each direction produces at scale 1.0, on **flat ground**. Calibration, not taste.
	 *
	 *  These are measurements of the clips, so they change only when the clips do -- and they
	 *  must be re-measured whenever the dodge montage is rebuilt from a different pack. Measured
	 *  at the *actor* rather than read off the clip, deliberately: what the player feels is where
	 *  the character ended up, which collision and the movement component get a say in.
	 *
	 *  A direction missing from this map falls back to DodgeRootMotionScale alone, which is the
	 *  old uniform behaviour rather than a division by zero.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge")
	TMap<ETDDodgeDirection, float> MeasuredTravelCm;

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

	UTDDodgeAbility();

private:

	/** Movement input relative to the character's facing, or Backward when stationary. */
	ETDDodgeDirection ResolveDodgeDirection() const;

	/**
	 *  Root motion scale for one direction: the global multiplier times the correction that
	 *  normalises this direction's clip to DodgeTargetDistanceCm.
	 *
	 *  Returns DodgeRootMotionScale unchanged when the direction has no measurement, so an
	 *  unpopulated map degrades to the old uniform behaviour rather than dividing by zero.
	 */
	float ComputeRootMotionScale(ETDDodgeDirection Direction) const;

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
