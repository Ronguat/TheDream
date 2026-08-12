// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/TDCombatDebug.h"

DEFINE_LOG_CATEGORY(LogTDCombatTiming);

// Defaults ON while combat is under active development: every real bug in the timing model
// was found by measuring, and reasoning on paper mis-diagnosed several of them confidently.
// Turn it back to 0 once combat changes stop being the thing under test.
TAutoConsoleVariable<int32> CVarTDDebugCombatTiming(
	TEXT("TD.DebugCombatTiming"),
	1,
	TEXT("Trace attack phase timings: windup rate, coil, commit, and release window edges. 1 to enable."),
	ECVF_Cheat);

// Off by default: it draws every frame of every active window on every combatant, and unlike
// the timing trace it is only wanted while a specific geometry question is open.
TAutoConsoleVariable<int32> CVarTDDebugMeleeTrace(
	TEXT("TD.DebugMeleeTrace"),
	0,
	TEXT("Draw each authored melee hitbox: arcs at both radii across its vertical band. 1 to enable."),
	ECVF_Cheat);
