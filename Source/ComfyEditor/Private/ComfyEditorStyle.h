// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/**
 * The plugin's Slate style: the Comfy logo, and a palette and brushes taken from the ComfyUI frontend's
 * dark theme (frontend 1.53 tokens), so Comfy Studio looks like ComfyUI's App mode rather than an Unreal panel.
 */
class FComfyEditorStyle
{
public:
	static void Register();
	static void Unregister();

	static FName GetStyleSetName();
	static const ISlateStyle& Get();

	/** "ComfyUnreal.Icon" (40 px) with "ComfyUnreal.Icon.Small" (16 px), for toolbar and menu entries. */
	static FSlateIcon GetIcon();

	/** ComfyUI dark theme colors. */
	struct FPalette
	{
		static const FLinearColor Background;	// charcoal-800 #171718, panels
		static const FLinearColor Surface;		// charcoal-700 #202121, input fields
		static const FLinearColor Card;			// charcoal-600 #262729, cards and buttons
		static const FLinearColor CardHover;	// charcoal-400 #313235
		static const FLinearColor Stroke;		// charcoal-400 #313235, hairlines
		static const FLinearColor Border;		// charcoal-200 #494a50, selected outlines
		static const FLinearColor Accent;		// azure-600 #0b8ce9, primary action
		static const FLinearColor AccentHover;	// azure-400 #31b9f4
		static const FLinearColor Text;			// white
		static const FLinearColor TextMuted;	// smoke-700 #a0a0a0
		static const FLinearColor Success;
		static const FLinearColor Danger;
	};

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
