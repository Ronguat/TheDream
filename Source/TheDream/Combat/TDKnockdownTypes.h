// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TDKnockdownTypes.generated.h"

/**
 *  How hard a clean hit puts you on the floor. Authored per attack branch and per string swing.
 *
 *  **A grade, not a duration.** Both grades spend the same 2.5 s on the ground and begin their
 *  forced rise at 2.0; what differs is how that total is split between the jail you cannot act in
 *  and the choice window you can, and which get-up options the split leaves you. Keeping the total
 *  invariant is what lets every derivation keyed to it -- the exhausted player's ~62 stamina, the
 *  netcode line -- stay grade-blind.
 *
 *  **None is the default and means the hit hitstuns instead**, which is the pre-knockdown
 *  behaviour of every attack in the game. A branch that authors nothing here is unchanged.
 *
 *  The pairing shipped 2026-08-20 -- light None, heavy Hard, charged Hard, string ender Normal --
 *  is authored per swing rather than structural. The kit's one 360-degree knockdown carrying the
 *  gentle grade, so a crowd can never be hard-floored, is a property of *this* weapon; a future
 *  one may pair them differently. See Docs/Combat-Decisions.md.
 */
UENUM(BlueprintType)
enum class ETDKnockdownGrade : uint8
{
	/** No knockdown. The hit imposes its authored hitstun and knockback as before. */
	None,

	/**
	 *  Jail 1.0, choice 1.0. **The escape-rich grade**, carried by the light string's ender.
	 *
	 *  The lockout is held to its minimum and the agency doubled, because the string already
	 *  extracted its damage on the way here -- generous escape is the volume trade. Every get-up
	 *  option is legal: the directional dodge, block, the get-up attack, and the free neutral stand.
	 */
	Normal,

	/**
	 *  Jail 1.5, choice 0.5. **The committed-hit grade**, carried by the heavy and the charged.
	 *
	 *  Meaner on three axes at the same total: the directional dodge is replaced by a stationary
	 *  kip-up, the free stand is removed, and the narrower split holds every exit back far enough
	 *  that a committed follow-up's arrival window overlaps the forced rise. Hard oki needs the
	 *  setup time, and the hit that bought it earned it.
	 */
	Hard
};
