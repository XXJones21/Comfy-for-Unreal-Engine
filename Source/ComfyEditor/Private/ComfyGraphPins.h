// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"

/**
 * Dropdowns for Comfy pins: the Run Comfy Workflow node's Choice and Model inputs, and every Backend parameter
 * tagged ComfyBackend, which lists the backend profiles.
 */
class FComfyGraphPinFactory : public FGraphPanelPinFactory
{
public:
	virtual TSharedPtr<class SGraphPin> CreatePin(class UEdGraphPin* Pin) const override;
};
