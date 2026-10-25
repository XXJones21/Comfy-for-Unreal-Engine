// Copyright Comfy Org. All Rights Reserved.

#include "ComfyEditorModule.h"
#include "ComfyBuiltInAssets.h"
#include "ComfyEditorLibrary.h"
#include "ComfyEditorStyle.h"
#include "ComfyGraphPins.h"
#include "ComfyInputsLibrary.h"
#include "EdGraphUtilities.h"
#include "SComfyStudio.h"
#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "ComfyEditor"

const FName FComfyEditorModule::StudioTabName(TEXT("ComfyStudio"));

void FComfyEditorModule::SetStudioView(const FString& View)
{
	SComfyStudio::SetActiveView(View);
}

void FComfyEditorModule::StartupModule()
{
	FComfyEditorStyle::Register();

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(StudioTabName, FOnSpawnTab::CreateRaw(this, &FComfyEditorModule::SpawnStudioTab))
		.SetDisplayName(LOCTEXT("StudioTabTitle", "Comfy Studio"))
		.SetTooltipText(LOCTEXT("StudioTabTooltip", "Run ComfyUI workflows and import the results."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FComfyEditorStyle::GetIcon());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FComfyEditorModule::RegisterMenus));

	// Dropdowns for the Run Comfy Workflow node's choice and model pins.
	PinFactory = MakeShared<FComfyGraphPinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(PinFactory);

	// Comfy Inputs can carry meshes, sounds and textures with source art; only the editor can write those to files.
	UComfyInputsLibrary::EditorAssetExporter = [](UObject* Asset, FString& OutError)
	{
		return UComfyEditorLibrary::ExportAssetForInput(Asset, OutError);
	};

	ComfyBuiltInAssets::Sync();
}

void FComfyEditorModule::ShutdownModule()
{
	UComfyInputsLibrary::EditorAssetExporter = nullptr;
	if (PinFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualPinFactory(PinFactory);
		PinFactory.Reset();
	}
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(StudioTabName);
	}
	FComfyEditorStyle::Unregister();
}

void FComfyEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// Toolbar button next to the other level editor tools.
	UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
	FToolMenuSection& Section = Toolbar->FindOrAddSection("Comfy");
	FToolMenuEntry Entry = FToolMenuEntry::InitToolBarButton(
		"OpenComfyStudio",
		FUIAction(FExecuteAction::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(FComfyEditorModule::StudioTabName); })),
		LOCTEXT("ToolbarLabel", "Comfy"),
		LOCTEXT("ToolbarTooltip", "Open Comfy Studio"),
		FComfyEditorStyle::GetIcon());
	// The style the level toolbar's own Settings button uses, so "Comfy" shows next to the logo in compact mode too.
	Entry.StyleNameOverride = "CalloutToolbar";
	Section.AddEntry(Entry);
}

TSharedRef<SDockTab> FComfyEditorModule::SpawnStudioTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SComfyStudio)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FComfyEditorModule, ComfyEditor)
