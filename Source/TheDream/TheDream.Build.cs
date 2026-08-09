// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TheDream : ModuleRules
{
	public TheDream(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		// Project code includes relative to the module root, e.g.
		// #include "Combat/Attributes/TDAttributeSet.h"
		PublicIncludePaths.AddRange(new string[] {
			"TheDream"
		});

		// Epic's stock Variant_Combat sample uses bare cross-folder includes and
		// needs these. Remove this block when Variant_Combat is deleted.
		PublicIncludePaths.AddRange(new string[] {
			"TheDream/Variant_Combat",
			"TheDream/Variant_Combat/AI",
			"TheDream/Variant_Combat/Animation",
			"TheDream/Variant_Combat/Gameplay",
			"TheDream/Variant_Combat/Interfaces",
			"TheDream/Variant_Combat/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
