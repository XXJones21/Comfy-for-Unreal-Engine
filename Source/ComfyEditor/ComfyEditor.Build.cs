// Copyright Comfy Org. All Rights Reserved.

using UnrealBuildTool;

public class ComfyEditor : ModuleRules
{
	public ComfyEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ComfyCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AssetTools",
			"AssetDefinition",
			"AssetRegistry",
			"BlueprintGraph",
			"ComfyBlueprint",
			"ContentBrowser",
			"DesktopPlatform",
			"EditorFramework",
			"EditorWidgets",
			"GraphEditor",
			"ImageCore",
			"InputCore",
			"Json",
			"MaterialEditor",
			"Projects",
			"PropertyEditor",
			"Settings",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"WebBrowser",
			"WorkspaceMenuStructure",
		});
	}
}
