// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/TDAttackHitbox.h"
#include "DrawDebugHelpers.h"

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

bool FTDAttackHitbox::GetBearingToCapsule(
	const FVector& AttackerLocation,
	float AttackerYawDegrees,
	const FVector& TargetLocation,
	float TargetRadiusCm,
	float& OutBearingDegrees,
	float& OutHalfArcDegrees) const
{
	const FVector Delta = TargetLocation - AttackerLocation;
	const float CentreDistance = Delta.Size2D();
	if (CentreDistance <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float BearingDegrees = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
	const float ArcCentreWorldDegrees = AttackerYawDegrees + ArcCentreDegrees;
	OutBearingDegrees = FMath::FindDeltaAngleDegrees(ArcCentreWorldDegrees, BearingDegrees);

	// Identical widening to OverlapsCapsule, deliberately: aim assist must ask the same question the
	// hit test will answer, or it will correct to an edge that is not where the edge actually is.
	const float SubtendedHalfAngle = (CentreDistance > TargetRadiusCm)
		? FMath::RadiansToDegrees(FMath::Asin(TargetRadiusCm / CentreDistance))
		: 90.0f;

	OutHalfArcDegrees = (ArcDegrees * 0.5f) + SubtendedHalfAngle;
	return true;
}

#if ENABLE_DRAW_DEBUG
void FTDAttackHitbox::DrawDebug(
	const UWorld* World,
	const FVector& Location,
	float YawDegrees,
	const FColor& Color,
	float DurationSeconds) const
{
	// One segment per 10 degrees keeps a narrow arc from collapsing to a straight line and a full
	// circle from costing more than it is worth to look at.
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(ArcDegrees / 10.0f), 2, 72);
	const float HalfArc = ArcDegrees * 0.5f;
	const float Heights[2] = { HeightMinCm, HeightMaxCm };
	const bool bPersistent = false;

	FVector Corners[2][2];

	for (int32 Band = 0; Band < 2; ++Band)
	{
		const FVector BandOffset(0.0f, 0.0f, Heights[Band]);
		FVector PreviousInner = FVector::ZeroVector;
		FVector PreviousOuter = FVector::ZeroVector;

		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float Alpha = static_cast<float>(Step) / static_cast<float>(Steps);
			const float AngleDegrees = YawDegrees + ArcCentreDegrees - HalfArc + ArcDegrees * Alpha;
			const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
			const FVector Direction(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.0f);

			const FVector Inner = Location + Direction * MinReachCm + BandOffset;
			const FVector Outer = Location + Direction * MaxReachCm + BandOffset;

			if (Step > 0)
			{
				DrawDebugLine(World, PreviousInner, Inner, Color, bPersistent, DurationSeconds, 0, 1.0f);
				// The outer arc is the attack's range, so it is drawn heaviest -- it is the one
				// line worth reading off the screen while tuning.
				DrawDebugLine(World, PreviousOuter, Outer, Color, bPersistent, DurationSeconds, 0, 2.0f);
			}

			// The arc's two straight edges, and the corners the vertical struts join.
			if (Step == 0 || Step == Steps)
			{
				DrawDebugLine(World, Inner, Outer, Color, bPersistent, DurationSeconds, 0, 1.0f);
				Corners[Band][Step == 0 ? 0 : 1] = Outer;
			}

			PreviousInner = Inner;
			PreviousOuter = Outer;
		}
	}

	// Struts between the bands, so the volume reads as a volume rather than two floating arcs.
	DrawDebugLine(World, Corners[0][0], Corners[1][0], Color, bPersistent, DurationSeconds, 0, 1.0f);
	DrawDebugLine(World, Corners[0][1], Corners[1][1], Color, bPersistent, DurationSeconds, 0, 1.0f);
}
#endif
