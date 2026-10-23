// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

COMFYCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogComfy, Log, All);

class FComfyCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
