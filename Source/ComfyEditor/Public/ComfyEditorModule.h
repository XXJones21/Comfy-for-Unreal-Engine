// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FComfyEditorModule : public IModuleInterface
{
public:
	static const FName StudioTabName;

	/** Switch an open Comfy Studio to "Run" or "Graph". Does nothing when the tab isn't open. */
	static COMFYEDITOR_API void SetStudioView(const FString& View);

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	TSharedRef<class SDockTab> SpawnStudioTab(const class FSpawnTabArgs& Args);

	TSharedPtr<struct FGraphPanelPinFactory> PinFactory;
};
