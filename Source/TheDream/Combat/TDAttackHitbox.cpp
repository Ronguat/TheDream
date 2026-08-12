// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/TDAttackHitbox.h"

float FTDAttackHitbox::GetBroadPhaseRadiusCm() const
{
	// The wedge's furthest point is its outer radius at whichever end of the band is further from
	// the attacker's origin. A target whose *body* reaches inside MaxReachCm necessarily touches
	// this sphere, so the overlap query cannot miss anyone the filter would have accepted.
	const float VerticalExtent = FMath::Max(FMath::Abs(HeightMinCm), FMath::Abs(HeightMaxCm));
	return FMath::Sqrt(FMath::Square(FMath::Max(0.0f, MaxReachCm)) + FMath::Square(VerticalExtent));
}

bool FTDAttackHitbox::OverlapsCapsule(
	const FVector& AttackerLocation,
	float AttackerYawDegrees,
	const FVector& TargetLocation,
	float TargetRadiusCm,
	float TargetHalfHeightCm) const
{
	const FVector Delta = TargetLocation - AttackerLocation;

	// Vertical first: it is the cheapest test and the one most likely to reject.
	//
	// The capsule is treated as a cylinder, ignoring the hemispherical caps. That is generous by
	// at most the cap's bulge at the very top and bottom of a target, which is the part of a body
	// nobody aims at, and it keeps the test something a designer can predict from two numbers.
	const float TargetBottom = Delta.Z - TargetHalfHeightCm;
	const float TargetTop = Delta.Z + TargetHalfHeightCm;
	if (TargetTop < HeightMinCm || TargetBottom > HeightMaxCm)
	{
		return false;
	}

	// Horizontal distance measured to the target's body rather than its origin. A capsule
	// straddling either radius counts, so MaxReachCm means "how far from me can a body be and
	// still be struck" -- the question spacing is actually tuned against.
	const float CentreDistance = Delta.Size2D();
	if (CentreDistance + TargetRadiusCm < MinReachCm || CentreDistance - TargetRadiusCm > MaxReachCm)
	{
		return false;
	}

	// A full circle has no bearing worth testing, and short-circuiting it also avoids asking for
	// the direction to a target standing exactly on top of the attacker.
	if (ArcDegrees >= 360.0f || CentreDistance <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	const float BearingDegrees = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
	const float ArcCentreWorldDegrees = AttackerYawDegrees + ArcCentreDegrees;
	const float OffsetDegrees = FMath::Abs(FMath::FindDeltaAngleDegrees(ArcCentreWorldDegrees, BearingDegrees));

	// Widen the arc by the angle the target's own body subtends, so a capsule clipped by the
	// arc's edge is hit. Without this the arc measures a bearing to a point while the thing it is
	// being compared against is a cylinder, and edge hits would fail for no visible reason.
	// Inside the target's own radius every bearing is true, hence the 90 degree fallback.
	const float SubtendedHalfAngle = (CentreDistance > TargetRadiusCm)
		? FMath::RadiansToDegrees(FMath::Asin(TargetRadiusCm / CentreDistance))
		: 90.0f;

	return OffsetDegrees <= (ArcDegrees * 0.5f) + SubtendedHalfAngle;
}
