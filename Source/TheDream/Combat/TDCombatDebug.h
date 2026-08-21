// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

/**
 *  Timing trace for the attack phase model.
 *
 *  **Per-attack output defaults to ON** while combat is under active development -- see the cvar's
 *  own comment in the .cpp. Switch it off with `TD.DebugCombatTiming 0` once combat stops being the
 *  thing under test.
 *
 *  Warnings on this category are deliberately **not** gated. They report configuration that
 *  silently breaks an attack rather than crashing it, so they need to be visible without anyone
 *  having thought to opt in.
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
 *  Switch on with `TD.DebugMeleeTrace 1`, at runtime, on either combatant. A cvar rather than only
 *  the per-ability `bDrawDebugTrace`, which is EditDefaultsOnly on a Blueprint CDO and so costs a
 *  details-panel edit and a PIE restart at best.
 *
 *  **This is the only way an FTDAttackHitbox can be judged at all**: the editor has no hitbox
 *  preview, and a volume that is merely *wrong* resolves hits perfectly happily. Drawn during the
 *  windup as well as the release, dimmed, so a volume can be looked at before the instant it is
 *  already deciding a hit. The per-ability property still works and is OR'd with this.
 */
extern TAutoConsoleVariable<int32> CVarTDDebugMeleeTrace;

FORCEINLINE bool TDShouldDrawMeleeTrace()
{
	return CVarTDDebugMeleeTrace.GetValueOnGameThread() != 0;
}

/**
 *  One line of per-attack trace. Skipped entirely -- arguments included -- unless the cvar is set,
 *  so gathering montage positions costs nothing when it is off. Requires at least one format
 *  argument.
 */
#define TD_TIMING_LOG(Format, ...) \
	do { if (TDShouldTraceCombatTiming()) { UE_LOG(LogTDCombatTiming, Log, Format, __VA_ARGS__); } } while (0)
