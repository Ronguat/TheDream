// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Tasks/TDRootMotionSource_FacingForce.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

FTDRootMotionSource_FacingForce::FTDRootMotionSource_FacingForce()
	: Strength(0.0f)
	, StrengthOverTime(nullptr)
	, StandoffCm(0.0f)
	, YawOffsetDegrees(0.0f)
{
	// Copied from FRootMotionSource_ConstantForce, and for its stated reason: a partial tick at
	// the end produces a very inconsistent velocity on the final frame.
	Settings.SetFlag(ERootMotionSourceSettingsFlags::DisablePartialEndTick);
}

FRootMotionSource* FTDRootMotionSource_FacingForce::Clone() const
{
	return new FTDRootMotionSource_FacingForce(*this);
}

bool FTDRootMotionSource_FacingForce::Matches(const FRootMotionSource* Other) const
{
	if (!FRootMotionSource::Matches(Other))
	{
		return false;
	}

	// Safe cast: FRootMotionSource::Matches has already established ScriptStruct equality.
	const FTDRootMotionSource_FacingForce* OtherCast = static_cast<const FTDRootMotionSource_FacingForce*>(Other);

	return FMath::IsNearlyEqual(Strength, OtherCast->Strength, 0.1f)
		&& StrengthOverTime == OtherCast->StrengthOverTime
		&& FMath::IsNearlyEqual(StandoffCm, OtherCast->StandoffCm, 0.1f)
		&& FMath::IsNearlyEqual(YawOffsetDegrees, OtherCast->YawOffsetDegrees, 0.1f);
}

bool FTDRootMotionSource_FacingForce::MatchesAndHasSameState(const FRootMotionSource* Other) const
{
	// No unique state beyond Time, which the base class owns -- the direction is not stored, it
	// is read from the character, so there is nothing here that can drift between machines.
	return FRootMotionSource::MatchesAndHasSameState(Other);
}

bool FTDRootMotionSource_FacingForce::UpdateStateFrom(const FRootMotionSource* SourceToTakeStateFrom, bool bMarkForSimulatedCatchup)
{
	return FRootMotionSource::UpdateStateFrom(SourceToTakeStateFrom, bMarkForSimulatedCatchup);
}

void FTDRootMotionSource_FacingForce::PrepareRootMotion(
	float SimulationTime,
	float MovementTickTime,
	const ACharacter& Character,
	const UCharacterMovementComponent& MoveComponent)
{
	RootMotionParams.Clear();

	// The line this whole struct exists for. Facing is sampled now rather than baked in at
	// creation, so the lunge curves with the player instead of sliding past them.
	//
	// Flattened because a lunge is a ground move: the actor is yaw-only today, so Z is already
	// zero, and flattening means a future pitched avatar cannot drive itself into the floor.
	// GetSafeNormal returns zero for a degenerate vector, which produces no motion rather than
	// a NaN.
	FVector Direction = Character.GetActorForwardVector();
	Direction.Z = 0.0f;
	Direction = Direction.GetSafeNormal();

	// Rotated after normalising and after flattening, so the offset is a pure yaw about world up
	// regardless of what the actor is doing. Every attack passes 0 and pays nothing for this.
	if (!FMath::IsNearlyZero(YawOffsetDegrees))
	{
		Direction = Direction.RotateAngleAxis(YawOffsetDegrees, FVector::UpVector);
	}

	// Target Lock's gate. Asked every tick against where the target actually is, rather than once
	// against where it was predicted to be -- see StandoffCm. Time still advances below, so a
	// gated tick spends duration without spending distance: the gate subtracts travel and can
	// never add any.
	if (IsWithinStandoff(Character, Direction))
	{
		RootMotionParams.Set(FTransform::Identity);
		SetTime(GetTime() + SimulationTime);
		return;
	}

	FTransform NewTransform(Direction * Strength);

	// Scale strength of force over time.
	if (StrengthOverTime)
	{
		const float TimeValue = Duration > 0.0f ? FMath::Clamp(GetTime() / Duration, 0.0f, 1.0f) : GetTime();
		NewTransform.ScaleTranslation(StrengthOverTime->GetFloatValue(TimeValue));
	}

	// Scale for Simulation/MovementTime differences, so a client catching up applies the right
	// total rather than the right per-frame amount. Same as every stock source.
	const float Multiplier = (MovementTickTime > UE_SMALL_NUMBER) ? (SimulationTime / MovementTickTime) : 1.0f;
	NewTransform.ScaleTranslation(Multiplier);

	RootMotionParams.Set(NewTransform);

	SetTime(GetTime() + SimulationTime);
}

bool FTDRootMotionSource_FacingForce::IsWithinStandoff(const ACharacter& Character, const FVector& Direction) const
{
	if (StandoffCm <= 0.0f || Direction.IsNearlyZero())
	{
		return false;
	}

	const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	const UWorld* World = Character.GetWorld();
	if (!Capsule || !World)
	{
		return false;
	}

	const FVector Start = Character.GetActorLocation();

	// SweepMulti rather than SweepSingle because the first blocking hit may be world geometry, and
	// a wall is not what this gates on: sliding along a flat surface is fine and a lunge into one
	// is already handled by the movement component. Only a pawn closes the gate.
	TArray<FHitResult> Hits;
	World->SweepMultiByChannel(
		Hits,
		Start,
		Start + Direction * StandoffCm,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()),
		FCollisionQueryParams(SCENE_QUERY_STAT(TDLungeStandoff), /*bTraceComplex=*/false, &Character));

	for (const FHitResult& Hit : Hits)
	{
		if (Hit.GetActor() && Hit.GetActor()->IsA<APawn>())
		{
			return true;
		}
	}

	return false;
}

bool FTDRootMotionSource_FacingForce::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	if (!FRootMotionSource::NetSerialize(Ar, Map, bOutSuccess))
	{
		return false;
	}

	// Only the speed and the curve travel. The direction deliberately does not -- each machine
	// reads it from a character whose rotation is already replicated, which is what keeps this
	// cheaper on the wire than the stock source rather than more expensive.
	Ar << Strength;
	Ar << StrengthOverTime;
	Ar << StandoffCm;
	Ar << YawOffsetDegrees;

	bOutSuccess = true;
	return true;
}

UScriptStruct* FTDRootMotionSource_FacingForce::GetScriptStruct() const
{
	return FTDRootMotionSource_FacingForce::StaticStruct();
}

FString FTDRootMotionSource_FacingForce::ToSimpleString() const
{
	return FString::Printf(TEXT("[ID:%u]FTDRootMotionSource_FacingForce %s"), LocalID, *InstanceName.GetPlainNameString());
}

void FTDRootMotionSource_FacingForce::AddReferencedObjects(class FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(StrengthOverTime);

	FRootMotionSource::AddReferencedObjects(Collector);
}
