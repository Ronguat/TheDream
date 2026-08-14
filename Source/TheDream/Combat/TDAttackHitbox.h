// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TDAttackHitbox.generated.h"

/**
 *  An attack's damaging volume, authored rather than derived from the animation.
 *
 *  A horizontal wedge -- an annular sector with a vertical band -- anchored to the attacker and
 *  live for exactly the release window. Six numbers, all in the attacker's own frame.
 *
 *  **Why this is authored at all.** The timing half of the attack model already works this way:
 *  windup, release and recovery are authored durations and every play rate is derived, so the
 *  animation warps to fit the mechanic. Space was the last dimension still taken from the clip,
 *  which meant reach was whatever the vendor's animator happened to draw and three shortlisted
 *  attacks -- two shield bashes and a 360 spin -- could not be expressed at all, because a
 *  blade-socket trace follows the wrong object for them. See Docs/Combat-Decisions.md.
 *
 *  **Why a wedge and not a box.** Reach is radially constant, so MaxReachCm is the answer to
 *  "what is my range?" in every direction the arc covers. A box's reach varies with angle --
 *  negligibly for a narrow attack, by 41% at the corners of a wide one -- and cannot express a
 *  full circle. It also matches the vocabulary the spec already uses for defense (block is "180
 *  forward", parry is "360 coverage"), so offense and defense stop describing space in two
 *  different languages.
 *
 *  This is **not** an FCollisionShape; the engine offers only sphere, capsule and box. It is one
 *  broad-phase sphere overlap plus an exact filter, which is both cheaper than the four capsule
 *  sweeps it replaced and more precise.
 */
USTRUCT(BlueprintType)
struct FTDAttackHitbox
{
	GENERATED_BODY()

	/**
	 *  Inner radius in cm, measured horizontally from the attacker's origin.
	 *
	 *  0 covers point blank. Raise it only to author an attack that genuinely misses someone
	 *  stood inside it -- it is a hole in the hitbox, not a minimum range for the animation.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hitbox", meta=(ClampMin="0.0"))
	float MinReachCm = 0.0f;

	/**
	 *  Outer radius in cm. **This is the attack's range**, and it is the number spacing is
	 *  tuned with.
	 *
	 *  Measured to the target's *body*, not to its origin -- a capsule whose near edge falls
	 *  inside this distance is hit. That is the difference between "reach is 165 cm to your
	 *  centre" and "165 cm to you", and only the second is a number a player can learn.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hitbox", meta=(ClampMin="0.0"))
	float MaxReachCm = 165.0f;

	/**
	 *  Total horizontal arc in degrees, centred on ArcCentreDegrees. 360 covers every direction.
	 *
	 *  Widened at test time by the angle the target's own capsule subtends, so a body clipped by
	 *  the arc's edge counts. Without that the arc would measure a point against a cylinder.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hitbox", meta=(ClampMin="0.0", ClampMax="360.0"))
	float ArcDegrees = 100.0f;

	/**
	 *  Where the arc's centre sits, in degrees clockwise from the attacker's facing.
	 *
	 *  0 is straight ahead and is right for most swings. It exists because a diagonal slash does
	 *  not travel symmetrically about the character's forward axis, so the coverage can be skewed
	 *  to the side the blade actually crosses rather than widening the arc to reach it.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hitbox")
	float ArcCentreDegrees = 0.0f;

	/**
	 *  Vertical band, in cm relative to the attacker's origin -- the capsule's centre, roughly
	 *  90 cm off the ground, not the feet.
	 *
	 *  Nearly inert today: everyone is the same standing capsule, so the band only discriminates
	 *  on a slope or against a jumping target. Author it generously and revisit when there are
	 *  states worth cutting under or over.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hitbox")
	float HeightMinCm = -70.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hitbox")
	float HeightMaxCm = 70.0f;

	/**
	 *  A wedge that contains nothing, for properties where "off" is the right default.
	 *
	 *  Zero reach rather than zero arc, because an arc of 0 still passes the subtended-angle
	 *  widening and would quietly hit anything close enough. Reach is the one field that cannot be
	 *  widened by anything.
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
	 *  contain. Deliberately generous: over-querying a handful of pawns costs nothing, and the
	 *  exact filter is what decides.
	 */
	float GetBroadPhaseRadiusCm() const;

	/**
	 *  Whether an upright capsule intersects this wedge.
	 *
	 *  Takes the target's dimensions rather than an actor so the geometry stays free of engine
	 *  types and can be reasoned about on its own.
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
	 *  Split out so aim assist can ask "how far outside am I, and by how much" using exactly the
	 *  geometry OverlapsCapsule decides with -- including the widening by the target's own subtended
	 *  angle. Two implementations of that test would drift, and the one that drifted would be the
	 *  one deciding where attacks point.
	 *
	 *  @param OutBearingDegrees   Signed angle from the arc's centre to the target, positive clockwise.
	 *  @param OutHalfArcDegrees   Half the arc, widened by the angle the target's body subtends.
	 *  @return false if the target is degenerate (on top of the attacker), where bearing is meaningless.
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
	 *  Draws the wedge as two banded arcs joined by struts.
	 *
	 *  Lives here rather than on the trace task because it is the wedge's own geometry, and because
	 *  aim assist needs to draw a *different* wedge than the one being traced -- a second copy of
	 *  this would be a second thing to keep in step with the shape it claims to show.
	 *
	 *  DurationSeconds of -1 draws for a single frame, which suits anything redrawn every tick --
	 *  including the aim assist wedge, which is redrawn every tick while homing runs.
	 *
	 *  *This previously said the aim assist wedge "is a one-instant decision and needs a real duration
	 *  to be visible at all", describing a design that was replaced during the same slice and never
	 *  shipped. Corrected 2026-08-14, after the stale note sent a debugging session looking for a draw
	 *  that git shows never existed.* A comment describing a sibling's behaviour goes stale silently,
	 *  because nothing about editing the sibling brings you here.
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
 *  **Reach is absent rather than defaulted, and that absence is the design** (2026-08-14). It is
 *  derived at use time as
 *
 *      base lunge + this branch's lunge + this branch's damage reach + AimAssistMarginCm
 *
 *  which is "the furthest a body can be and still be struck by this attack, plus a margin". The
 *  margin is the point: if the wedge matched hit range exactly, then locking on would *encode*
 *  whether the target is reachable, and aim assist would be answering the one question Target Lock
 *  forbids it to answer -- it may correct where you are pointed, never whether you were in range.
 *  Extending past hit range keeps assist strictly about direction, and stops a player using the
 *  snap as a free rangefinder. See Docs/Combat-Decisions.md.
 *
 *  This is a separate struct from FTDAttackHitbox purely so that reach *cannot be authored*. The
 *  earlier design reused the hitbox and derived nothing; three wedges were authored by hand, and two
 *  of them turned out never to have done anything. A field that is silently ignored is worse than no
 *  field, so there is no field.
 *
 *  Everything else stays a dial, deliberately -- the user's call when this was designed: arc, its
 *  centre, and the vertical band remain per branch even though nothing differentiates them today.
 */
USTRUCT(BlueprintType)
struct FTDAimAssistWedge
{
	GENERATED_BODY()

	/**
	 *  Whether this branch gets aim assist at all.
	 *
	 *  **An explicit flag because arc cannot express "off".** An ArcDegrees of 0 still passes the
	 *  subtended-angle widening in OverlapsCapsule and would quietly select anything close enough --
	 *  the same reason FTDAttackHitbox::MakeDisabled zeroes reach rather than arc. Reach is derived
	 *  here and is never 0, so that route is unavailable and this takes its place.
	 *
	 *  Defaults to *on*, which is the deliberate opposite of the old per-branch default. A branch
	 *  that nobody has authored should aim like the rest of the ladder rather than silently drop
	 *  homing partway through a hold, which is what an unauthored wedge used to do.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aim Assist")
	bool bEnabled = true;

	/**
	 *  Total horizontal arc in degrees. **This is the contract: how wrong your aim may be.**
	 *
	 *  Widened at test time by the angle the target's own body subtends, exactly as the damage wedge
	 *  is, so a narrow authored arc self-widens as a target gets closer. Its half-arc is also the
	 *  maximum correction, by construction -- a candidate outside the wedge is not eligible, so
	 *  nothing can be rotated onto from further away than the wedge is wide.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aim Assist", meta=(ClampMin="0.0", ClampMax="360.0"))
	float ArcDegrees = 40.0f;

	/** Where the arc's centre sits, in degrees clockwise from the aim direction. 0 is straight ahead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aim Assist")
	float ArcCentreDegrees = 0.0f;

	/**
	 *  Vertical band in cm relative to the attacker's origin -- the capsule's centre, not the feet.
	 *
	 *  Kept authorable at the user's request even though nothing differentiates it today: everyone is
	 *  the same standing capsule, so this only discriminates on a slope or against a jumping target.
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
