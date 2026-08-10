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
