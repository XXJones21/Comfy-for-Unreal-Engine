// Copyright Comfy Org. All Rights Reserved.

#include "ComfyEditorModule.h"
#include "SComfyStudio.h"
#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "ComfyEditor"

const FName FComfyEditorModule::StudioTabName(TEXT("ComfyStudio"));

void FComfyEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(StudioTabName, FOnSpawnTab::CreateRaw(this, &FComfyEditorModule::SpawnStudioTab))
		.SetDisplayName(LOCTEXT("StudioTabTitle", "Comfy Studio"))
		.SetTooltipText(LOCTEXT("StudioTabTooltip", "Run ComfyUI workflows and import the results."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Texture2D"));

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FComfyEditorModule::RegisterMenus));
}

void FComfyEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(StudioTabName);
	}
}

void FComfyEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// Toolbar button next to the other level editor tools.
	UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
	FToolMenuSection& Section = Toolbar->FindOrAddSection("Comfy");
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"OpenComfyStudio",
		FUIAction(FExecuteAction::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(FComfyEditorModule::StudioTabName); })),
		LOCTEXT("ToolbarLabel", "Comfy"),
		LOCTEXT("ToolbarTooltip", "Open Comfy Studio"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Texture2D")));
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
