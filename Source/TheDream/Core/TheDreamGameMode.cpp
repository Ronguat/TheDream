// Copyright Epic Games, Inc. All Rights Reserved.

#include "TheDreamGameMode.h"
#include "Core/TDPlayerState.h"

ATheDreamGameMode::ATheDreamGameMode()
{
	// The ASC lives on the PlayerState for players, so the game mode has to actually spawn ours.
	// Without this it spawns a plain APlayerState, ATDCombatCharacter's resolution finds no
	// ATDPlayerState, and every player silently falls back to its owned ASC -- which *works*,
	// and is precisely the wrong thing to have working: the fallback is unseeded, so it would
	// present as a player with no abilities rather than as a missing setting.
	PlayerStateClass = ATDPlayerState::StaticClass();
}
