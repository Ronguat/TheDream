// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/TDCombatDebug.h"

DEFINE_LOG_CATEGORY(LogTDCombatTiming);

TAutoConsoleVariable<int32> CVarTDDebugCombatTiming(
	TEXT("TD.DebugCombatTiming"),
	0,
	TEXT("Trace attack phase timings: windup rate, coil, commit, and release window edges. 1 to enable."),
	ECVF_Cheat);
