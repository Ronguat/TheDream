// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "Engine/TimerHandle.h"
#include "TDDodgeAbility.generated.h"

class UAnimMontage;

/** Which way the dodge went. Chosen from movement input, and picks a montage section. */
UENUM(BlueprintType)
enum class ETDDodgeDirection : uint8
{
	Forward,
	Backward,
	Left,
	Right
};

/**
 *  A directional evade that costs stamina and grants invulnerability.
 *
 *  Two durations, deliberately separate. **The i-frame window is not the dodge's length.**
 *  Tying invulnerability to however long a clip happens to run makes the animation the
 *  balance authority, and it is the wrong one -- the recovery tail of a dodge is meant to
 *  be a window in which you can be punished, which is exactly what "invulnerable for the
 *  whole thing" removes. IFrameSeconds is therefore authored, and should stay shorter
 *  than DodgeSeconds.
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
	 *  Only used while there is no montage. Once DodgeMontage is set the montage's own
	 *  length ends the ability instead, so this stops being read.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="0.01"))
	float DodgeSeconds = 0.5f;

	/**
	 *  How long the character is untouchable, measured from activation.
	 *
	 *  Should stay below DodgeSeconds -- the difference is the recovery a whiffed dodge is
	 *  punished in. Setting the two equal makes dodge strictly safe, which removes the cost
	 *  the whole defensive economy is built on.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(ClampMin="0.0"))
	float IFrameSeconds = 0.3f;

	/**
	 *  Applied while the i-frames are live, and read by attackers to skip the hit.
	 *
	 *  Must match the tag the attacking ability treats as immunity
	 *  (UTDMeleeAttackAbility::TargetImmunityTags). They are two halves of one contract,
	 *  and nothing enforces that they agree.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge")
	FGameplayTag IFrameTag;

	/** Optional until the dodge animations land. Sections are named after ETDDodgeDirection. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge")
	TObjectPtr<UAnimMontage> DodgeMontage;

	/** Direction the current dodge resolved to. Exposed so the debug HUD can show it. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Dodge")
	ETDDodgeDirection DodgeDirection = ETDDodgeDirection::Backward;

private:

	/** Movement input relative to the character's facing, or Backward when stationary. */
	ETDDodgeDirection ResolveDodgeDirection() const;

	/** Removes the i-frame tag. Separate from ability end so recovery can be vulnerable. */
	void EndIFrames();

	UFUNCTION()
	void HandleDodgeFinished();

	FTimerHandle IFrameTimerHandle;
	FTimerHandle DodgeTimerHandle;
	bool bIFramesActive = false;
};
