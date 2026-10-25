// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ComfyGraphBridge.generated.h"

/**
 * Bound into the Graph view's browser as window.ue.comfyunreal, so script in the ComfyUI page can
 * hand the current graph to Unreal, report the signed-in account, and answer calls Unreal made into the page
 * (Resources/Bridge/comfy_unreal_page.js). CEF exposes the functions in lower case: receivegraph, reporterror,
 * reportaccount, reply, log.
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

	/** The page's Comfy account as JSON: signedIn, displayName, email, workspace, freeRunsRemaining, freeRunsAllowance, credits. */
	UFUNCTION()
	void ReportAccount(const FString& AccountJson);

	/** The answer to a call Unreal made with window.__comfyUnreal.call. */
	UFUNCTION()
	void Reply(const FString& CallId, bool bOk, const FString& ResultJson, const FString& Error);

	UFUNCTION()
	void Log(const FString& Message);

	TFunction<void(const FString& WorkflowJson, const FString& ApiJson, const FString& Name)> OnGraph;
	TFunction<void(const FString& Message)> OnError;
	TFunction<void(const FString& AccountJson)> OnAccount;
	TFunction<void(const FString& CallId, bool bOk, const FString& ResultJson, const FString& Error)> OnReply;
};
