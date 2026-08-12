// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

/**
 *  Timing trace for the attack phase model.
 *
 *  Kept rather than deleted because it is what found every real bug in that system: an
 *  attack that applied its tag, played its animation and dealt no damage; a montage that
 *  banked the time it sat still and spent it in a single frame, launching the character
 *  across the map; a coil that overran its own release window. Reasoning about play rates
 *  on paper mis-diagnosed two of those before the log settled them.
 *
 *  Per-attack output is off by default -- switch it on with `TD.DebugCombatTiming 1`.
 *
 *  Warnings on this category are deliberately **not** gated. They report configuration
 *  that silently breaks an attack rather than crashing it, so they need to be visible
 *  without anyone having thought to opt in.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogTDCombatTiming, Log, All);

extern TAutoConsoleVariable<int32> CVarTDDebugCombatTiming;

FORCEINLINE bool TDShouldTraceCombatTiming()
{
	return CVarTDDebugCombatTiming.GetValueOnGameThread() != 0;
}

/**
 *  Outlines each authored melee hitbox: arcs at both radii, across both ends of its band.
 *
 *  Switch on with `TD.DebugMeleeTrace 1`, at runtime, on either combatant. It exists as a
 *  cvar rather than only as the per-ability `bDrawDebugTrace` because that property is
 *  EditDefaultsOnly on a Blueprint CDO -- changing it means the details panel and a PIE
 *  restart at best, and an editor restart when set programmatically. A question like "how far
 *  does this actually reach" should cost a keypress, not a restart.
 *
 *  **This is the only way an FTDAttackHitbox can be judged at all.** Six numbers in a details
 *  panel say nothing about whether the arc covers the swing or the reach matches the sword, the
 *  editor has no hitbox preview, and a volume that is merely *wrong* resolves hits perfectly
 *  happily. The draw is deliberately live during the windup as well as the release, dimmed, so
 *  a volume can be looked at before the instant it is already deciding a hit.
 *
 *  The per-ability property still works and is OR'd with this, so an attack can be left
 *  drawing without touching the console.
 */
extern TAutoConsoleVariable<int32> CVarTDDebugMeleeTrace;

FORCEINLINE bool TDShouldDrawMeleeTrace()
{
	return CVarTDDebugMeleeTrace.GetValueOnGameThread() != 0;
}

/**
 *  One line of per-attack trace. Skipped entirely -- arguments included -- unless the
 *  cvar is set, so gathering montage positions costs nothing when it is off.
 *
 *  Requires at least one format argument.
 */
#define TD_TIMING_LOG(Format, ...) \
	do { if (TDShouldTraceCombatTiming()) { UE_LOG(LogTDCombatTiming, Log, Format, __VA_ARGS__); } } while (0)
