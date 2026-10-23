// Copyright Comfy Org. All Rights Reserved.

using UnrealBuildTool;

public class ComfyCore : ModuleRules
{
	public ComfyCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"HTTP",
			"Json",
			"JsonUtilities",
			"WebSockets",
			"ImageCore",
			"ImageWrapper",
			"AudioExtensions",
			"Projects",
		});
	}
}
