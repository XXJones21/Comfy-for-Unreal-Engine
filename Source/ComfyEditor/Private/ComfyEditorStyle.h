// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/** The plugin's Slate style: the Comfy logo for the toolbar button, Tools menu, tab and workflow assets. */
class FComfyEditorStyle
{
public:
	static void Register();
	static void Unregister();

	static FName GetStyleSetName();
	static const ISlateStyle& Get();

	/** "ComfyUnreal.Icon" (40 px) with "ComfyUnreal.Icon.Small" (16 px), for toolbar and menu entries. */
	static FSlateIcon GetIcon();

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
