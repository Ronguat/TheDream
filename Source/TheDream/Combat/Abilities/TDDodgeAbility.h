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
 *  Displacement is deliberately absent for now: whether the dodge is moved by root motion
 *  or driven in code is unsettled, and spacing is the top feel goal, so it is not a choice
 *  to make by accident. See Docs/Combat-Decisions.md.
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

private:

	/** Movement input relative to the character's facing, or Backward when stationary. */
	ETDDodgeDirection ResolveDodgeDirection() const;

	/** Removes the i-frame tag. Idempotent, because EndAbility can run more than once. */
	void EndIFrames();

	UFUNCTION()
	void HandleDodgeFinished();

	FTimerHandle DodgeTimerHandle;
	bool bIFramesActive = false;
};
