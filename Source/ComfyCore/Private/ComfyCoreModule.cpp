// Copyright Comfy Org. All Rights Reserved.

#include "ComfyCoreModule.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogComfy);

void FComfyCoreModule::StartupModule()
{
	FModuleManager::Get().LoadModuleChecked(TEXT("HTTP"));
	FModuleManager::Get().LoadModuleChecked(TEXT("WebSockets"));
}

void FComfyCoreModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FComfyCoreModule, ComfyCore)
