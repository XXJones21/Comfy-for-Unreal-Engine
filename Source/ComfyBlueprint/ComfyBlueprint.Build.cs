// Copyright Comfy Org. All Rights Reserved.

using UnrealBuildTool;

public class ComfyBlueprint : ModuleRules
{
	public ComfyBlueprint(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"BlueprintGraph",
			"ComfyCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"KismetCompiler",
			"UnrealEd",
		});
	}
}
