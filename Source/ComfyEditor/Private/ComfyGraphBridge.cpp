// Copyright Comfy Org. All Rights Reserved.

#include "ComfyGraphBridge.h"

void UComfyGraphBridge::ReceiveGraph(const FString& WorkflowJson, const FString& ApiJson, const FString& Name)
{
	if (OnGraph)
	{
		OnGraph(WorkflowJson, ApiJson, Name);
	}
}

void UComfyGraphBridge::ReportError(const FString& Message)
{
	if (OnError)
	{
		OnError(Message);
	}
}
