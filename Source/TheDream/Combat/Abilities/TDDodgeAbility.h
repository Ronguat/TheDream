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
 *  Eight directions because WASD produces diagonals and the source clips exist for all eight. The
 *  names are the montage's section names, matching the animation pack's own direction codes.
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
 *  A directional evade. Costs stamina; i-frames last exactly as long as the dodge, so there is no
 *  punishable tail and spam is bounded by stamina alone.
 *
 *  Direction comes from movement input, backward when stationary. Displacement is authored -- a
 *  root motion source carrying DodgeTargetDistanceCm over DodgeSeconds at a yaw offset from the
 *  direction enum.
 *
 *  The eight source clips have bEnableRootMotion off. If a dodge stops moving, check that first:
 *  animation root motion suppresses root motion sources outright, and zeroing it does not help.
 */
UCLASS(abstract)
class UTDDodgeAbility : public UTDGameplayAbility
{
	GENERATED_BODY()

public:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	/**
	 *  Knockdown's directional get-up, and its kip-up. Decided at activation from the type: normal
	 *  gives the directional dodge, hard a stationary kip-up. Both cost full; the exhausted are
	 *  refused either way.
	 */
	virtual const TCHAR* GetKnockdownRiseLabel(const class ATDCombatCharacter* Character) const override;

	/** The dodge animates its own rise -- a roll, or a kip-up on hard. */
	virtual bool BringsOwnRiseMontage() const override { return true; }

	/** The rise is the dodge, so it ends with it -- i-frames and knockdown expire together. */
	virtual float GetKnockdownRiseSeconds() const override { return DodgeSeconds; }
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:

	/** Whether this activation is a hard knockdown's kip-up rather than a directional dodge. */
	bool bIsKnockdownKipUp = false;

	/** Whether this activation is a get-up off the floor at all (either type). */
	bool bIsKnockdownGetUp = false;

	/**
	 *  How long the dodge lasts end to end, including the punishable part.
	 *
	 *  Authoritative: the section's play rate is derived as SectionLength / DodgeSeconds. Changes
	 *  duration, not distance -- travel is DodgeTargetDistanceCm below.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="0.01"))
	float DodgeSeconds = 0.5f;

	/**
	 *  How much of KnockdownRollMontage is the roll itself, in its own unscaled seconds. Only
	 *  this portion is fitted to DodgeSeconds; the tail past it plays out afterwards at the
	 *  same rate, outside the i-frames.
	 *
	 *  Fitting the whole clip instead runs the recovery-to-stance while the body is still
	 *  travelling. Clamped to the montage length, so a shorter clip degrades to fitting all
	 *  of it rather than overrunning. Zero fits the whole clip.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="0.0"))
	float KnockdownRollSeconds = 0.6f;

	/**
	 *  How fast the get-up roll turns to face where it travels, in degrees per second. The
	 *  travel direction is taken at activation, so this only moves the body.
	 *
	 *  **Authored, unlike ATheDreamCharacter::TurnRateDegrees**, which is derived from the
	 *  light's commit and must not be copied here: nothing about this turn is an aim guarantee.
	 *  Low enough to read as a turn, high enough to finish inside the roll -- a 180 costs
	 *  180 / this seconds, against DodgeSeconds.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="0.0"))
	float RollTurnRateDegrees = 1200.0f;

	/**
	 *  How long after this dodge ends a parry may not be started. Applied as State.DodgeRecovery,
	 *  and lives on the dodge because the dodge is what knows one just ended.
	 *
	 *  Zero disables it: ApplyDodgeRecovery returns early, so no tag is applied and no
	 *  DODGE RECOVERY line is traced. Takes nothing but the parry -- movement, offense, block
	 *  and a further dodge are untouched.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="0.0"))
	float DodgeRecoverySeconds = 0.0f;

	/** How far a dodge carries, in cm. The distance knob; every direction travels it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="1.0"))
	float DodgeTargetDistanceCm = 405.0f;

	/**
	 *  Applied for the whole dodge, and read by attackers to skip the hit. Must match
	 *  UTDMeleeAttackAbility::TargetImmunityTags -- two halves of one contract, unenforced.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge")
	FGameplayTag IFrameTag;

	/**
	 *  Optional. Sections must be named exactly for ETDDodgeDirection: Fw, FR, R, BR, Bw, BL, L, FL.
	 *  A missing section warns and plays from the start rather than failing silently.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge")
	TObjectPtr<UAnimMontage> DodgeMontage;

	/**
	 *  The roll played when dodging off the floor on a normal knockdown. Single-segment, so it plays
	 *  from the start with no section. Root motion off; displacement is the authored distance.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge")
	TObjectPtr<UAnimMontage> KnockdownRollMontage;

	/**
	 *  The kip-up played when dodging off the floor on a hard knockdown. The one clip whose own root
	 *  motion is the travel, so the distance passed to StartLunge is zero. Stationary by design.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge")
	TObjectPtr<UAnimMontage> KipUpMontage;

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

	/**
	 *  Where the dodge started, so the trace can report actual travel. Measured, not read off the
	 *  clip: collision, slopes and the movement component all get a say.
	 */
	FVector DodgeStartLocation = FVector::ZeroVector;
};
