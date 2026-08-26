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
			"UMG",
			"Slate",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"AnimGraphRuntime"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		// Project code includes relative to the module root, e.g.
		// #include "Combat/Attributes/TDAttributeSet.h"
		PublicIncludePaths.AddRange(new string[] {
			"TheDream"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
