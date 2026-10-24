// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ComfyGraphBridge.generated.h"

/**
 * Bound into the Graph view's browser as window.ue.comfyunreal, so script in the ComfyUI page can
 * hand the current graph to Unreal. CEF exposes the functions in lower case: receivegraph, reporterror.
 */
UCLASS(Transient)
class UComfyGraphBridge : public UObject
{
	GENERATED_BODY()

public:
	/** The page's graph: WorkflowJson is the canvas save (with extra.linearData when App mode set one), ApiJson the API-format prompt. */
	UFUNCTION()
	void ReceiveGraph(const FString& WorkflowJson, const FString& ApiJson, const FString& Name);

	UFUNCTION()
	void ReportError(const FString& Message);

	TFunction<void(const FString& WorkflowJson, const FString& ApiJson, const FString& Name)> OnGraph;
	TFunction<void(const FString& Message)> OnError;
};
