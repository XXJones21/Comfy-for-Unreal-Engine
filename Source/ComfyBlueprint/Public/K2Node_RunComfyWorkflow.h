// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node_BaseAsyncTask.h"
#include "ComfyTypes.h"
#include "K2Node_RunComfyWorkflow.generated.h"

class UComfyWorkflow;
class FBlueprintActionDatabaseRegistrar;

/**
 * "Run Comfy Workflow": an async node that grows one pin per workflow input once its Workflow pin is set to an asset,
 * the same inputs Comfy Studio's Run view shows. Pins left at their defaults aren't sent, so the workflow's own values
 * and each backend's model choice still apply. The palette also lists one entry per workflow asset.
 *
 * It compiles to Set (Comfy Inputs) calls feeding UComfyRunWorkflowWithInputsAction, so a packaged game only
 * needs ComfyCore. With the Workflow pin wired instead of set, the inputs come from the Inputs pin alone.
 */
UCLASS()
class COMFYBLUEPRINT_API UK2Node_RunComfyWorkflow : public UK2Node_BaseAsyncTask
{
	GENERATED_BODY()

public:
	UK2Node_RunComfyWorkflow(const FObjectInitializer& ObjectInitializer);

	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void PostReconstructNode() override;
	virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;

	// UK2Node
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;

	/** The workflow the input pins were made for, or null when the Workflow pin is wired or empty. */
	UComfyWorkflow* GetPinsWorkflow() const { return PinsWorkflow; }

	/** The workflow input a pin sets, or null for the node's fixed pins. */
	const FComfyWorkflowInput* FindInputForPin(const UEdGraphPin* Pin) const;

	/**
	 * Choices for an Enum or Model input pin, for its dropdown. Model inputs list the active backend's files when it's
	 * a local ComfyUI that Studio has checked, and the workflow's preferred files otherwise. Empty for other pins.
	 */
	TArray<FString> GetPinChoices(const UEdGraphPin* Pin) const;

	/** Name of the Workflow, Inputs and Backend pins (the factory function's parameters). */
	static const FName WorkflowPinName;
	static const FName InputsPinName;

private:
	/** Make the input pins for PinsWorkflow. */
	void CreateInputPins();

	/** Replace the input pins after the Workflow pin changed. */
	void RebuildInputPins();

	/**
	 * Compile checks: a workflow to run, inputs that still match it, valid choices. Run from ExpandNode, because the
	 * compiler validates nodes only after expansion, when the base class has already broken the node's links.
	 */
	bool CheckBeforeExpansion(FCompilerResultsLog& MessageLog) const;

	/** True while the node is the compiler's copy, being expanded. */
	bool IsInCompilerGraph() const;

	/** True for a pin made from a workflow input. */
	static bool IsInputPin(const UEdGraphPin* Pin);

	/** Inputs and their pin types, to tell when the asset changed under the node. */
	static FString MakeSignature(const UComfyWorkflow* Workflow);

	/** The workflow the input pins were made for. */
	UPROPERTY()
	TObjectPtr<UComfyWorkflow> PinsWorkflow;

	/** MakeSignature(PinsWorkflow) when the pins were made. */
	UPROPERTY()
	FString PinsSignature;
};
