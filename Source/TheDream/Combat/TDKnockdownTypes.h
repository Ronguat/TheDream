// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TDKnockdownTypes.generated.h"

/**
 *  How hard a clean hit puts you on the floor. Authored per attack branch and per string swing.
 *
 *  A type, not a duration: both types spend the same 2.5 s on the ground and begin their forced
 *  rise at 2.0. What differs is how that total splits between the lockout you cannot act in and the
 *  input window you can, and which get-up options the split leaves you.
 */
UENUM(BlueprintType)
enum class ETDKnockdownType : uint8
{
	/** No knockdown, and the default. The hit imposes its authored hitstun and knockback as before. */
	None,

	/**
	 *  Lockout 1.0, input window 1.0. Every get-up option is legal: the directional dodge, block, the
	 *  get-up attack, and the free neutral stand.
	 */
	Normal,

	/**
	 *  Lockout 1.5, input window 0.5. The directional dodge is replaced by a stationary kip-up and the
	 *  free neutral stand is removed.
	 */
	Hard
};
