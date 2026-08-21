// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TDAttackHitbox.generated.h"

/**
 *  An attack's damaging volume, authored rather than derived from the animation: a horizontal wedge
 *  -- an annular sector with a vertical band -- anchored to the attacker and live for exactly the
 *  release window. Six numbers, all in the attacker's own frame.
 *
 *  Not an FCollisionShape; the engine offers only sphere, capsule and box. One broad-phase sphere
 *  overlap plus an exact filter.
 */
USTRUCT(BlueprintType)
struct FTDAttackHitbox
{
	GENERATED_BODY()

	/**
	 *  Inner radius in cm, measured horizontally from the attacker's origin. 0 covers point blank.
	 *  Raise it only to author an attack that genuinely misses someone stood inside it -- a hole in
	 *  the hitbox, not a minimum range for the animation.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hitbox", meta=(ClampMin="0.0"))
	float MinReachCm = 0.0f;

	/**
	 *  Outer radius in cm. The attack's range, and the number spacing is tuned with. Measured to the
	 *  target's *body*, not its origin -- a capsule whose near edge falls inside this is hit.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hitbox", meta=(ClampMin="0.0"))
	float MaxReachCm = 165.0f;

	/**
	 *  Total horizontal arc in degrees, centred on ArcCentreDegrees. 360 covers every direction.
	 *  Widened at test time by the angle the target's capsule subtends, so a body clipped by the
	 *  arc's edge counts.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hitbox", meta=(ClampMin="0.0", ClampMax="360.0"))
	float ArcDegrees = 100.0f;

	/**
	 *  Where the arc's centre sits, in degrees clockwise from facing. 0 is straight ahead and right
	 *  for most swings. It exists because a diagonal slash does not travel symmetrically about the
	 *  forward axis, so coverage can be skewed to the side the blade crosses rather than widened.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hitbox")
	float ArcCentreDegrees = 0.0f;

	/**
	 *  Vertical band, in cm relative to the attacker's origin -- the capsule's centre, roughly 90 cm
	 *  up, not the feet. Nearly inert today: everyone is the same standing capsule, so it only
	 *  discriminates on a slope or against a jumping target. Author it generously.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hitbox")
	float HeightMinCm = -70.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hitbox")
	float HeightMaxCm = 70.0f;

	/**
	 *  A wedge that contains nothing, for properties where "off" is the right default. Zero reach
	 *  rather than zero arc, because an arc of 0 still passes the subtended-angle widening and would
	 *  quietly hit anything close enough. Reach is the one field nothing widens.
	 */
	static FTDAttackHitbox MakeDisabled()
	{
		FTDAttackHitbox Disabled;
		Disabled.MaxReachCm = 0.0f;
		return Disabled;
	}

	/** Whether this wedge can contain anything at all. */
	bool IsEnabled() const { return MaxReachCm > 0.0f; }

	/**
	 *  Radius of a sphere about the attacker's origin that cannot miss anything this wedge could
	 *  contain. Deliberately generous: over-querying a handful of pawns costs nothing, and the exact
	 *  filter decides.
	 */
	float GetBroadPhaseRadiusCm() const;

	/**
	 *  Whether an upright capsule intersects this wedge. Takes the target's dimensions rather than
	 *  an actor, so the geometry stays free of engine types.
	 *
	 *  @param AttackerLocation    World location of the attacker's origin.
	 *  @param AttackerYawDegrees  Attacker's world yaw. The wedge is defined in this frame.
	 *  @param TargetLocation      World location of the target capsule's centre.
	 *  @param TargetRadiusCm      Target capsule radius.
	 *  @param TargetHalfHeightCm  Target capsule half height.
	 */
	bool OverlapsCapsule(
		const FVector& AttackerLocation,
		float AttackerYawDegrees,
		const FVector& TargetLocation,
		float TargetRadiusCm,
		float TargetHalfHeightCm) const;

	/**
	 *  Signed bearing to a target in this wedge's frame, and the half-arc that would contain it.
	 *
	 *  Split out so aim assist asks "how far outside am I" using exactly the geometry
	 *  OverlapsCapsule decides with, widening included. Two implementations would drift, and the one
	 *  that drifted would decide where attacks point.
	 *
	 *  @param OutBearingDegrees   Signed angle from the arc's centre, positive clockwise.
	 *  @param OutHalfArcDegrees   Half the arc, widened by the angle the target's body subtends.
	 *  @return false if the target is on top of the attacker, where bearing is meaningless.
	 */
	bool GetBearingToCapsule(
		const FVector& AttackerLocation,
		float AttackerYawDegrees,
		const FVector& TargetLocation,
		float TargetRadiusCm,
		float& OutBearingDegrees,
		float& OutHalfArcDegrees) const;

#if ENABLE_DRAW_DEBUG
	/**
	 *  Draws the wedge as two banded arcs joined by struts. Lives here because it is the wedge's own
	 *  geometry, and because aim assist draws a *different* wedge than the one being traced.
	 *
	 *  DurationSeconds of -1 draws for a single frame, suiting anything redrawn every tick --
	 *  including the aim assist wedge while homing runs.
	 */
	void DrawDebug(
		const UWorld* World,
		const FVector& Location,
		float YawDegrees,
		const FColor& Color,
		float DurationSeconds = -1.0f) const;
#endif
};

/**
 *  Aim assist's targeting wedge: every part of its shape *except* how far it reaches.
 *
 *  Reach is absent rather than defaulted, derived at use time as
 *
 *      base lunge + this branch's lunge + this branch's damage reach + AimAssistMarginCm
 *
 *  A separate struct from FTDAttackHitbox purely so reach cannot be authored, since a field that is
 *  silently ignored is worse than no field. Everything else stays a dial: arc, its centre and the
 *  vertical band remain per branch even though nothing differentiates them today.
 */
USTRUCT(BlueprintType)
struct FTDAimAssistWedge
{
	GENERATED_BODY()

	/**
	 *  Whether this branch gets aim assist at all. An explicit flag because arc cannot express
	 *  "off": an ArcDegrees of 0 still passes the subtended-angle widening in OverlapsCapsule and
	 *  would quietly select anything close enough -- the same reason MakeDisabled zeroes reach
	 *  rather than arc. Reach is derived here and never 0, so that route is unavailable.
	 *
	 *  Defaults to on: a branch nobody has authored should aim like the rest of the ladder rather
	 *  than silently drop homing partway through a hold.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aim Assist")
	bool bEnabled = true;

	/**
	 *  Total horizontal arc in degrees. The contract: how wrong your aim may be. Widened at test
	 *  time by the angle the target's body subtends, exactly as the damage wedge is, so a narrow
	 *  authored arc self-widens as a target closes. Its half-arc is also the maximum correction, by
	 *  construction -- a candidate outside the wedge is not eligible.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aim Assist", meta=(ClampMin="0.0", ClampMax="360.0"))
	float ArcDegrees = 40.0f;

	/** Where the arc's centre sits, in degrees clockwise from the aim direction. 0 is straight ahead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aim Assist")
	float ArcCentreDegrees = 0.0f;

	/**
	 *  Vertical band in cm relative to the attacker's origin -- the capsule's centre, not the feet.
	 *  Only discriminates on a slope or against a jumping target, since everyone is the same
	 *  standing capsule.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aim Assist")
	float HeightMinCm = -70.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aim Assist")
	float HeightMaxCm = 70.0f;

	/** Builds the wedge that is actually tested, given the reach derived for its branch. */
	FTDAttackHitbox ToHitbox(float ReachCm) const
	{
		FTDAttackHitbox Wedge;
		Wedge.MinReachCm = 0.0f;
		Wedge.MaxReachCm = bEnabled ? FMath::Max(0.0f, ReachCm) : 0.0f;
		Wedge.ArcDegrees = ArcDegrees;
		Wedge.ArcCentreDegrees = ArcCentreDegrees;
		Wedge.HeightMinCm = HeightMinCm;
		Wedge.HeightMaxCm = HeightMaxCm;
		return Wedge;
	}
};
