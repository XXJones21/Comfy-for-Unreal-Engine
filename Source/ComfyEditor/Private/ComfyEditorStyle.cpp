// Copyright Comfy Org. All Rights Reserved.

#include "ComfyEditorStyle.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"
#include "Textures/SlateIcon.h"

TSharedPtr<FSlateStyleSet> FComfyEditorStyle::StyleSet;

void FComfyEditorStyle::Register()
{
	if (StyleSet.IsValid())
	{
		return;
	}
	StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ComfyUnreal"));
	StyleSet->SetContentRoot((Plugin.IsValid() ? Plugin->GetBaseDir() : FString()) / TEXT("Resources") / TEXT("Icons"));

	StyleSet->Set("ComfyUnreal.Icon", new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("ComfyIcon40"), TEXT(".png")), FVector2D(40.f, 40.f)));
	StyleSet->Set("ComfyUnreal.Icon.Small", new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("ComfyIcon16"), TEXT(".png")), FVector2D(16.f, 16.f)));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

void FComfyEditorStyle::Unregister()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
		StyleSet.Reset();
	}
}

FName FComfyEditorStyle::GetStyleSetName()
{
	static const FName Name(TEXT("ComfyUnrealStyle"));
	return Name;
}

const ISlateStyle& FComfyEditorStyle::Get()
{
	check(StyleSet.IsValid());
	return *StyleSet;
}

FSlateIcon FComfyEditorStyle::GetIcon()
{
	return FSlateIcon(GetStyleSetName(), "ComfyUnreal.Icon", "ComfyUnreal.Icon.Small");
}
