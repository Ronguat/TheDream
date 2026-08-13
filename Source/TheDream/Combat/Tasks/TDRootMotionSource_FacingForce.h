// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/RootMotionSource.h"
#include "TDRootMotionSource_FacingForce.generated.h"

class UCurveFloat;

/**
 *  A constant force along the avatar's *current* facing, re-read every movement tick.
 *
 *  **The only thing that separates this from FRootMotionSource_ConstantForce is one line**, and
 *  it is the whole point: the stock source stores a world-space Force vector chosen when the
 *  source was created, so it cannot follow a character who turns. This one stores a scalar speed
 *  and builds the direction in PrepareRootMotion.
 *
 *  **Why that matters here.** Animation root motion is applied in the actor's local frame, so an
 *  attack's travel has always turned with the player. Replacing it with a world-fixed force would
 *  have been a regression, not a missing feature -- a flick during the windup would lunge one way
 *  while the swing landed another, up to 180 degrees apart, which is exactly the case
 *  TurnRateDegrees exists to serve. Play confirmed the aimable version reads better (2026-08-12).
 *
 *  **It is not a hand-rolled movement system**, which is the objection this design had to answer.
 *  FRootMotionSource is a first-class extension point with a full replication contract, and every
 *  stock source is a sibling of this one -- so it is predicted, replayed and replicated by the same
 *  machinery that makes animation root motion safe. SetActorLocation, AddMovementInput and
 *  LaunchCharacter are the versions the netcode audit was right to fear; this is not one of them.
 *
 *  Reading facing during PrepareRootMotion is safe under prediction because rotation is part of
 *  saved-move data and is reproduced during replay. That is mechanism-level reasoning rather than
 *  a measured claim: nothing in this project has run two machines.
 *
 *  **The lunge is aimable only where facing is free.** From an attack's commit checkpoint facing is
 *  frozen until the ability ends, so during the post-commit lunge this behaves identically to a
 *  world-fixed force. Steerability is therefore a property of the phase, not a second setting --
 *  which is why one source serves both of an attack's lunges.
 */
USTRUCT()
struct FTDRootMotionSource_FacingForce : public FRootMotionSource
{
	GENERATED_USTRUCT_BODY()

	FTDRootMotionSource_FacingForce();

	virtual ~FTDRootMotionSource_FacingForce() = default;

	/**
	 *  Speed along facing, in cm/s.
	 *
	 *  Distance travelled is Strength * Duration * the mean of StrengthOverTime, so authoring a
	 *  distance means dividing it by the duration -- and a curve that does not average 1 changes
	 *  the distance. Note this is the distance travelled *along the path*: if the character turns
	 *  while it runs, the path curves and the straight-line displacement is shorter.
	 */
	UPROPERTY()
	float Strength;

	/** Optional shape over 0..1 of Duration. Must average 1.0 or the authored distance is a lie. */
	UPROPERTY()
	TObjectPtr<UCurveFloat> StrengthOverTime;

	/**
	 *  Target Lock: how close to a body this force is willing to carry the avatar, in cm. 0 disables.
	 *
	 *  **Gated per tick, not pre-computed, and the difference is the whole point.** Shortening the
	 *  authored distance up front was the first implementation and it was wrong: it bakes in a
	 *  prediction of where the target will be, taken at one instant, at a moment the target is free
	 *  to invalidate. Play found it immediately -- an opponent who backed away while an attack was
	 *  running could never be reached, because the distance had already been reduced to nothing on
	 *  their behalf. That was **worse than having no system at all**, which had at least travelled
	 *  the full distance and usually caught them.
	 *
	 *  The requirement was always "do not be inside a body", which is a *state*. Pre-shortening
	 *  expressed it as a *prediction*. This expresses it as a state again: each movement tick asks
	 *  whether a body is within StandoffCm ahead, and contributes nothing if so. Time still advances
	 *  either way, so the gate can only ever subtract travel -- the authored distance remains a hard
	 *  ceiling and the source still ends exactly on schedule.
	 *
	 *  **It is not homing**, which is the property whiff punish depends on. Homing changes a lunge's
	 *  direction or extends its distance; this changes neither. A target moving laterally still
	 *  escapes, and one backing off still escapes if it out-paces the authored travel. What it no
	 *  longer gets is the escape handed to it for free by a stale prediction.
	 */
	UPROPERTY()
	float StandoffCm;

	/**
	 *  Whether a pawn sits within StandoffCm ahead, so this tick should contribute nothing.
	 *
	 *  Swept with the avatar's own capsule on ECC_Pawn, which makes this the movement component's
	 *  own collision test asked one tick early rather than an approximation of it. Anything it
	 *  declines to gate on is something the avatar would not have collided with.
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
