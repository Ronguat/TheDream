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
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"TheDream",
			"TheDream/Variant_Platforming",
			"TheDream/Variant_Platforming/Animation",
			"TheDream/Variant_Combat",
			"TheDream/Variant_Combat/AI",
			"TheDream/Variant_Combat/Animation",
			"TheDream/Variant_Combat/Gameplay",
			"TheDream/Variant_Combat/Interfaces",
			"TheDream/Variant_Combat/UI",
			"TheDream/Variant_SideScrolling",
			"TheDream/Variant_SideScrolling/AI",
			"TheDream/Variant_SideScrolling/Gameplay",
			"TheDream/Variant_SideScrolling/Interfaces",
			"TheDream/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
