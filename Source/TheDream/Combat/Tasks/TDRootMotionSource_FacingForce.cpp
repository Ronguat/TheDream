// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Tasks/TDRootMotionSource_FacingForce.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Character.h"

FTDRootMotionSource_FacingForce::FTDRootMotionSource_FacingForce()
	: Strength(0.0f)
	, StrengthOverTime(nullptr)
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
		&& StrengthOverTime == OtherCast->StrengthOverTime;
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
