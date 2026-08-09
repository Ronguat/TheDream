// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDGameplayAbility.h"

UTDGameplayAbility::UTDGameplayAbility()
{
	// One instance per actor: abilities keep state across activations (combo index,
	// input holds) and per-execution instancing would throw that away every swing.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}
