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
 *  Eight directions because WASD produces diagonals naturally and the source clips exist for all
 *  eight. **The names are the montage's section names**, and they match the animation pack's own
 *  direction codes, so a section maps 1:1 onto the clip it was built from.
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
 *  **One duration, not two.** The i-frames last exactly as long as the dodge, so there is no second
 *  number to keep in step and no way to express invulnerability outlasting the move. A whiffed
 *  dodge is therefore never *directly* punishable: its cost is the stamina and being unable to act,
 *  and spam is bounded by the stamina economy rather than by a vulnerable tail.
 *
 *  Direction resolves from movement input, falling back to backward when stationary, because a
 *  neutral dodge that goes nowhere reads as a flinch rather than an evade.
 *
 *  Displacement is authored and driven in code, on the same terms as an attack's lunge -- a root
 *  motion source carrying DodgeTargetDistanceCm over DodgeSeconds, aimed by a yaw offset taken from
 *  the direction enum.
 *
 *  **The eight source clips therefore have bEnableRootMotion switched off.** If a dodge ever stops
 *  moving entirely, check that first: animation root motion suppresses root motion sources
 *  outright, and scaling it to zero does not help.
 */
UCLASS(abstract)
class UTDDodgeAbility : public UTDGameplayAbility
{
	GENERATED_BODY()

public:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	/**
	 *  **The dodge is knockdown's directional get-up, and its kip-up.** Which one is decided at
	 *  activation from the character's grade: normal gives the ordinary directional dodge, hard a
	 *  stationary i-framed kip-up. Both cost the full 50, and the exhausted are refused either way.
	 */
	virtual const TCHAR* GetKnockdownRiseLabel(const class ATDCombatCharacter* Character) const override;

	/** The dodge animates its own rise -- a roll, or a kip-up on hard. */
	virtual bool BringsOwnRiseMontage() const override { return true; }
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:

	/** Whether this activation is a hard knockdown's kip-up rather than a directional dodge. */
	bool bIsKnockdownKipUp = false;

	/** Whether this activation is a get-up off the floor at all (either grade). */
	bool bIsKnockdownGetUp = false;

	/**
	 *  How long the dodge lasts end to end, including the part you can be punished in.
	 *
	 *  **Authoritative, and the montage is made to fit it**: the section's play rate is derived as
	 *  SectionLength / DodgeSeconds, exactly as the attack ladder derives every rate from authored
	 *  timings.
	 *
	 *  This changes duration, not distance -- a dash played faster covers the same ground in less
	 *  time. Travel is DodgeTargetDistanceCm, directly below.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="0.01"))
	float DodgeSeconds = 0.5f;

	/**
	 *  How long after this dodge ends a parry may not be started. Applied as State.DodgeRecovery.
	 *
	 *  **Derived, and it must not be tuned freely.** Dodge-end plus this gap plus ParryWindowSeconds
	 *  has to overshoot the charged's 750 ms arrival for the worst-timed predictive dodge, or a
	 *  dodge thrown on a guess eats the fast layer and the parry it chains into covers the slow one
	 *  at no extra read. Re-derive it whenever DodgeSeconds, the parry window, or the charged's
	 *  arrival moves.
	 *
	 *  It lives on the dodge rather than on GA_Parry because the dodge is what knows one just ended.
	 *  **It takes nothing but the parry** -- movement, offense and block are untouched.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="0.0"))
	float DodgeRecoverySeconds = 0.15f;

	/** How far a dodge should carry, in cm. **The distance knob**, and every direction travels it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="1.0"))
	float DodgeTargetDistanceCm = 405.0f;

	/**
	 *  Applied for the whole dodge, and read by attackers to skip the hit.
	 *
	 *  Must match the tag the attacking ability treats as immunity
	 *  (UTDMeleeAttackAbility::TargetImmunityTags). They are two halves of one contract, and nothing
	 *  enforces that they agree.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge")
	FGameplayTag IFrameTag;

	/**
	 *  Optional. Its sections must be named exactly for ETDDodgeDirection: Fw, FR, R, BR, Bw, BL, L,
	 *  FL. A missing section warns and plays from the montage start rather than failing silently.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge")
	TObjectPtr<UAnimMontage> DodgeMontage;

	/**
	 *  The roll played when dodging off the floor on a **normal** knockdown.
	 *
	 *  A single-segment montage, so it plays from the start with no section, unlike DodgeMontage.
	 *  Displacement is still the authored DodgeTargetDistanceCm: this clip's root motion is switched
	 *  off, like the eight standing rolls.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge")
	TObjectPtr<UAnimMontage> KnockdownRollMontage;

	/**
	 *  The kip-up played when dodging off the floor on a **hard** knockdown.
	 *
	 *  **The one clip in the project whose own root motion is the travel**, so the distance passed to
	 *  StartLunge is zero and would be suppressed by the animation anyway. Stationary by design.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge")
	TObjectPtr<UAnimMontage> KipUpMontage;

	/** Direction the current dodge resolved to. Exposed so the debug HUD can show it. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Dodge")
	ETDDodgeDirection DodgeDirection = ETDDodgeDirection::Bw;

public:

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
	 *  character ended up moving, which collision, slopes and the movement component all get a say
	 *  in.
	 */
	FVector DodgeStartLocation = FVector::ZeroVector;
};
