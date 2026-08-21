// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/RootMotionSource.h"
#include "TDRootMotionSource_FacingForce.generated.h"

class UCurveFloat;

/**
 *  A constant force along the avatar's *current* facing, re-read every movement tick.
 *
 *  **One line separates this from FRootMotionSource_ConstantForce**, and it is the whole point: the
 *  stock source stores a world-space Force vector chosen when the source was created, so it cannot
 *  follow a character who turns. This one stores a scalar speed and builds the direction in
 *  PrepareRootMotion. Reading facing there replays correctly under prediction, because rotation is
 *  part of saved-move data.
 *
 *  **The lunge is aimable only where facing is free.** From an attack's commit checkpoint facing is
 *  frozen until the ability ends, so during the post-commit lunge this behaves identically to a
 *  world-fixed force. Steerability is a property of the phase, not a second setting, which is why
 *  one source serves both of an attack's lunges.
 */
USTRUCT()
struct FTDRootMotionSource_FacingForce : public FRootMotionSource
{
	GENERATED_USTRUCT_BODY()

	FTDRootMotionSource_FacingForce();

	virtual ~FTDRootMotionSource_FacingForce() = default;

	/**
	 *  Speed along facing, in cm/s. Distance travelled is Strength * Duration * the mean of
	 *  StrengthOverTime, so authoring a distance means dividing it by the duration. This is distance
	 *  along the *path*: if the character turns while it runs, the path curves and straight-line
	 *  displacement is shorter.
	 */
	UPROPERTY()
	float Strength;

	/** Optional shape over 0..1 of Duration. Must average 1.0 or the authored distance is a lie. */
	UPROPERTY()
	TObjectPtr<UCurveFloat> StrengthOverTime;

	/**
	 *  Target Lock: how close to a body this force is willing to carry the avatar, in cm. 0 disables.
	 *
	 *  **Gated per tick, not pre-computed.** Each movement tick asks whether a body is within
	 *  StandoffCm ahead and contributes nothing if so. Time still advances either way, so the gate
	 *  can only ever subtract travel -- the authored distance remains a hard ceiling and the source
	 *  still ends exactly on schedule. Shortening the distance up front instead would bake in a
	 *  prediction of where the target will be, taken at an instant the target is free to invalidate.
	 *
	 *  **It is not homing**, which is the property whiff punish depends on. This changes neither a
	 *  lunge's direction nor its distance, so a target moving laterally still escapes, and one
	 *  backing off still escapes if it out-paces the authored travel.
	 */
	UPROPERTY()
	float StandoffCm;

	/**
	 *  Direction relative to facing, in degrees clockwise. 0 is straight ahead. Every attack passes
	 *  0. The dodge passes its direction, the eight values coming out of ETDDodgeDirection's own
	 *  order at 45 degrees a step rather than from a table.
	 *
	 *  Applied to a direction still read from facing every tick, which keeps this cheap on the wire:
	 *  an offset is one float, where a world-space direction would be a vector that has to travel
	 *  and then disagree with a rotation which already replicates.
	 */
	UPROPERTY()
	float YawOffsetDegrees;

	/**
	 *  Whether a pawn sits within StandoffCm ahead, so this tick should contribute nothing. Swept
	 *  with the avatar's own capsule on ECC_Pawn, making this the movement component's own collision
	 *  test asked one tick early rather than an approximation of it.
	 */
	bool IsWithinStandoff(const ACharacter& Character, const FVector& Direction) const;

	virtual FRootMotionSource* Clone() const override;

	virtual bool Matches(const FRootMotionSource* Other) const override;

	virtual bool MatchesAndHasSameState(const FRootMotionSource* Other) const override;

	virtual bool UpdateStateFrom(const FRootMotionSource* SourceToTakeStateFrom, bool bMarkForSimulatedCatchup = false) override;

	virtual void PrepareRootMotion(
		float SimulationTime,
		float MovementTickTime,
		const ACharacter& Character,
		const UCharacterMovementComponent& MoveComponent
		) override;

	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;

	virtual UScriptStruct* GetScriptStruct() const override;

	virtual FString ToSimpleString() const override;

	virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};

template<>
struct TStructOpsTypeTraits<FTDRootMotionSource_FacingForce> : public TStructOpsTypeTraitsBase2<FTDRootMotionSource_FacingForce>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};
