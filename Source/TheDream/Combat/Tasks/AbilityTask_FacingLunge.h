// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask_ApplyRootMotion_Base.h"
#include "AbilityTask_FacingLunge.generated.h"

class UCurveFloat;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTDFacingLungeDelegate);

/**
 *  Carries the avatar an authored distance along its *current* facing, for an authored duration.
 *
 *  Modelled directly on UAbilityTask_ApplyRootMotionConstantForce, differing only in the source it
 *  applies -- see FTDRootMotionSource_FacingForce for why the direction cannot be baked in.
 *
 *  **Distance and duration are authored, not the force.** Every other number in this attack model
 *  is a duration a designer chose with the animation fitted to it; displacement is the last one
 *  that was still whatever the clip happened to do, and this is what finishes that job.
 */
UCLASS()
class UAbilityTask_FacingLunge : public UAbilityTask_ApplyRootMotion_Base
{
	GENERATED_UCLASS_BODY()

	/** Fires when the lunge's duration elapses. Not fired when the ability ends it early. */
	UPROPERTY(BlueprintAssignable)
	FTDFacingLungeDelegate OnFinish;

	/**
	 *  Starts a lunge of DistanceCm over DurationSeconds along the avatar's facing.
	 *
	 *  Takes a distance rather than a force so the caller authors the thing it actually cares
	 *  about; the speed is derived here. A StrengthCurve that does not average 1.0 across its
	 *  range will change the distance travelled.
	 */
	static UAbilityTask_FacingLunge* ApplyFacingLunge(
		UGameplayAbility* OwningAbility,
		FName TaskInstanceName,
		float DistanceCm,
		float DurationSeconds,
		UCurveFloat* StrengthCurve,
		float StandoffCm,
		ERootMotionFinishVelocityMode VelocityOnFinishMode,
		FVector SetVelocityOnFinish,
		float ClampVelocityOnFinish,
		bool bEnableGravity);

protected:

	virtual void SharedInitAndApply() override;

	virtual void TickTask(float DeltaTime) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void PreDestroyFromReplication() override;

	virtual void OnDestroy(bool AbilityIsEnding) override;

	/** Speed along facing, cm/s. Derived from distance and duration by the factory. */
	UPROPERTY(Replicated)
	float Strength;

	UPROPERTY(Replicated)
	float Duration;

	UPROPERTY(Replicated)
	TObjectPtr<UCurveFloat> StrengthOverTime;

	/** Target Lock's per-tick gate distance. Passed straight to the source; see StandoffCm there. */
	UPROPERTY(Replicated)
	float StandoffCm;

	UPROPERTY(Replicated)
	bool bEnableGravity;
};
