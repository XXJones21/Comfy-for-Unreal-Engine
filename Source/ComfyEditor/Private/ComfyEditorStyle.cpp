// Copyright Comfy Org. All Rights Reserved.

#include "ComfyEditorStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Textures/SlateIcon.h"

TSharedPtr<FSlateStyleSet> FComfyEditorStyle::StyleSet;

const FLinearColor FComfyEditorStyle::FPalette::Background = FLinearColor(FColor(0x17, 0x17, 0x18));
const FLinearColor FComfyEditorStyle::FPalette::Surface = FLinearColor(FColor(0x20, 0x21, 0x21));
const FLinearColor FComfyEditorStyle::FPalette::Card = FLinearColor(FColor(0x26, 0x27, 0x29));
const FLinearColor FComfyEditorStyle::FPalette::CardHover = FLinearColor(FColor(0x31, 0x32, 0x35));
const FLinearColor FComfyEditorStyle::FPalette::Stroke = FLinearColor(FColor(0x31, 0x32, 0x35));
const FLinearColor FComfyEditorStyle::FPalette::Border = FLinearColor(FColor(0x49, 0x4a, 0x50));
const FLinearColor FComfyEditorStyle::FPalette::Accent = FLinearColor(FColor(0x0b, 0x8c, 0xe9));
const FLinearColor FComfyEditorStyle::FPalette::AccentHover = FLinearColor(FColor(0x31, 0xb9, 0xf4));
const FLinearColor FComfyEditorStyle::FPalette::Text = FLinearColor::White;
const FLinearColor FComfyEditorStyle::FPalette::TextMuted = FLinearColor(FColor(0xa0, 0xa0, 0xa0));
const FLinearColor FComfyEditorStyle::FPalette::Success = FLinearColor(FColor(0x33, 0xc4, 0x6b));
const FLinearColor FComfyEditorStyle::FPalette::Danger = FLinearColor(FColor(0xe5, 0x48, 0x4d));

namespace ComfyEditorStylePrivate
{
	// The frontend's radius-lg (0.5rem) and radius-xl (0.75rem).
	static constexpr float RadiusLg = 8.f;
	static constexpr float RadiusXl = 12.f;

	static FButtonStyle MakeButton(const FLinearColor& Normal, const FLinearColor& Hovered, const FLinearColor& Outline, float Radius)
	{
		const FSlateRoundedBoxBrush NormalBrush(Normal, Radius, Outline, Outline.A > 0.f ? 1.f : 0.f);
		const FSlateRoundedBoxBrush HoverBrush(Hovered, Radius, Outline, Outline.A > 0.f ? 1.f : 0.f);
		// Disabled buttons all look alike: a flat card with muted text, never a faded accent.
		const FSlateRoundedBoxBrush DisabledBrush(Normal.A > 0.f ? FComfyEditorStyle::FPalette::Card : FLinearColor::Transparent, Radius, FComfyEditorStyle::FPalette::Stroke, Normal.A > 0.f ? 1.f : 0.f);
		return FButtonStyle()
			.SetNormal(NormalBrush)
			.SetHovered(HoverBrush)
			.SetPressed(HoverBrush)
			.SetDisabled(DisabledBrush)
			.SetNormalForeground(FSlateColor(FComfyEditorStyle::FPalette::Text))
			.SetHoveredForeground(FSlateColor(FComfyEditorStyle::FPalette::Text))
			.SetPressedForeground(FSlateColor(FComfyEditorStyle::FPalette::Text))
			.SetDisabledForeground(FSlateColor(FComfyEditorStyle::FPalette::TextMuted))
			.SetNormalPadding(FMargin(12, 6))
			.SetPressedPadding(FMargin(12, 6));
	}
}

void FComfyEditorStyle::Register()
{
	using namespace ComfyEditorStylePrivate;

	if (StyleSet.IsValid())
	{
		return;
	}
	StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ComfyUnreal"));
	StyleSet->SetContentRoot((Plugin.IsValid() ? Plugin->GetBaseDir() : FString()) / TEXT("Resources") / TEXT("Icons"));

	StyleSet->Set("ComfyUnreal.Icon", new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("ComfyIcon40"), TEXT(".png")), FVector2D(40.f, 40.f)));
	StyleSet->Set("ComfyUnreal.Icon.Small", new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("ComfyIcon16"), TEXT(".png")), FVector2D(16.f, 16.f)));

	// Surfaces.
	StyleSet->Set("Comfy.Panel", new FSlateColorBrush(FPalette::Background));
	StyleSet->Set("Comfy.Card", new FSlateRoundedBoxBrush(FPalette::Card, RadiusLg));
	StyleSet->Set("Comfy.Card.Hover", new FSlateRoundedBoxBrush(FPalette::CardHover, RadiusLg));
	StyleSet->Set("Comfy.Card.Selected", new FSlateRoundedBoxBrush(FPalette::Card, RadiusLg, FPalette::Accent, 1.5f));
	StyleSet->Set("Comfy.Field", new FSlateRoundedBoxBrush(FPalette::Surface, RadiusLg, FPalette::Stroke, 1.f));
	StyleSet->Set("Comfy.DropZone", new FSlateRoundedBoxBrush(FPalette::Surface, RadiusXl, FPalette::Border, 1.f));
	StyleSet->Set("Comfy.Preview", new FSlateRoundedBoxBrush(FPalette::Surface, RadiusXl));
	StyleSet->Set("Comfy.Chip", new FSlateRoundedBoxBrush(FPalette::CardHover, 99.f));
	StyleSet->Set("Comfy.Divider", new FSlateColorBrush(FPalette::Stroke));

	// Buttons: primary (Run), secondary (card), ghost (toolbar text).
	StyleSet->Set("Comfy.Button.Primary", MakeButton(FPalette::Accent, FPalette::AccentHover, FLinearColor::Transparent, RadiusLg));
	StyleSet->Set("Comfy.Button", MakeButton(FPalette::Card, FPalette::CardHover, FLinearColor::Transparent, RadiusLg));
	StyleSet->Set("Comfy.Button.Ghost", MakeButton(FLinearColor::Transparent, FPalette::CardHover, FLinearColor::Transparent, RadiusLg));

	// Text: the frontend uses Inter; Roboto, the editor's font, is the closest bundled face. The editor's
	// heading fonts are Roboto Bold Condensed, so headings use plain Roboto Bold to read like the frontend.
	const FTextBlockStyle Normal = FTextBlockStyle(FAppStyle::GetWidgetStyle<FTextBlockStyle>("NormalText")).SetColorAndOpacity(FPalette::Text);
	StyleSet->Set("Comfy.Text", FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 10)));
	StyleSet->Set("Comfy.Text.Muted", FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 9)).SetColorAndOpacity(FPalette::TextMuted));
	StyleSet->Set("Comfy.Text.Label", FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 8)).SetColorAndOpacity(FPalette::TextMuted));
	StyleSet->Set("Comfy.Text.Title", FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 11)));
	StyleSet->Set("Comfy.Text.Large", FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 15)));
	StyleSet->Set("Comfy.Text.Error", FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 9)).SetColorAndOpacity(FPalette::Danger));

	// A one-line notice above the library: a tinted strip with a colored edge.
	StyleSet->Set("Comfy.Banner", new FSlateRoundedBoxBrush(FPalette::Card, RadiusLg, FPalette::Border, 1.f));
	StyleSet->Set("Comfy.Banner.Error", new FSlateRoundedBoxBrush(FLinearColor::LerpUsingHSV(FPalette::Card, FPalette::Danger, 0.12f), RadiusLg, FPalette::Danger.CopyWithNewOpacity(0.6f), 1.f));

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
