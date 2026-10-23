// Copyright Comfy Org. All Rights Reserved.

using UnrealBuildTool;

public class ComfyAgent : ModuleRules
{
	public ComfyAgent(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ComfyCore",
			"ComfyEditor",
			"Json",
			"JsonUtilities",
			"ToolsetRegistry",
			"UnrealEd",
		});
	}
}
