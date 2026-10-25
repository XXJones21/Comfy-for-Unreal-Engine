// Copyright Comfy Org. All Rights Reserved.

#include "ComfyGraphBridge.h"
#include "ComfyCoreModule.h"

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

void UComfyGraphBridge::ReportAccount(const FString& AccountJson)
{
	if (OnAccount)
	{
		OnAccount(AccountJson);
	}
}

void UComfyGraphBridge::Reply(const FString& CallId, bool bOk, const FString& ResultJson, const FString& Error)
{
	if (OnReply)
	{
		OnReply(CallId, bOk, ResultJson, Error);
	}
}

void UComfyGraphBridge::Log(const FString& Message)
{
	UE_LOG(LogComfy, Log, TEXT("Comfy page: %s"), *Message);
}
