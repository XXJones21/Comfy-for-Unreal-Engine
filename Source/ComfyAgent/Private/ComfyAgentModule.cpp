// Copyright Comfy Org. All Rights Reserved.

#include "ComfyToolset.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

/** Registers the Comfy toolset with the editor's toolset registry, which Unreal MCP serves to agents. */
class FComfyAgentModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (UToolsetRegistry::IsAvailable())
		{
			UToolsetRegistry::RegisterToolsetClass(UComfyToolset::StaticClass());
		}
	}

	virtual void ShutdownModule() override
	{
		if (UObjectInitialized() && UToolsetRegistry::IsAvailable())
		{
			UToolsetRegistry::UnregisterToolsetClass(UComfyToolset::StaticClass());
		}
	}
};

IMPLEMENT_MODULE(FComfyAgentModule, ComfyAgent)
