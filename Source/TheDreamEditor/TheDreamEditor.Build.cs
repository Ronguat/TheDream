using UnrealBuildTool;

public class TheDreamEditor : ModuleRules
{
	public TheDreamEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"AnimGraph",
			"BlueprintGraph",
			"Slate",
			"SlateCore"
		});

		PublicIncludePaths.AddRange(new string[] {
			"TheDreamEditor"
		});
	}
}
