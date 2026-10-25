// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace ComfyBuiltInAssets
{
	/**
	 * Save each Resources/Workflows file as a Comfy Workflow asset in /ComfyUnreal/Workflows, or update the asset when
	 * its file changed, so Blueprints can reference the starters and a packaged game cooks the ones it uses.
	 * Skipped in commandlets and when the plugin's Content folder is read-only; the JSON copies are used then.
	 */
	void Sync();
}
