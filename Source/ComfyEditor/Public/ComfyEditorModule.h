// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FComfyEditorModule : public IModuleInterface
{
public:
	static const FName StudioTabName;

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	TSharedRef<class SDockTab> SpawnStudioTab(const class FSpawnTabArgs& Args);
};
