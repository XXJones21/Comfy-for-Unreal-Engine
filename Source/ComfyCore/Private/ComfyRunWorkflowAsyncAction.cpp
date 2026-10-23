// Copyright Comfy Org. All Rights Reserved.

#include "ComfyRunWorkflowAsyncAction.h"
#include "ComfyJob.h"
#include "ComfySubsystem.h"
#include "ComfyCoreModule.h"

UComfyRunWorkflowAsyncAction* UComfyRunWorkflowAsyncAction::RunComfyWorkflow(UObject* WorldContextObject, UComfyWorkflow* InWorkflow, const TMap<FString, FString>& InInputs, FName InBackend)
{
	UComfyRunWorkflowAsyncAction* Action = NewObject<UComfyRunWorkflowAsyncAction>();
	Action->Workflow = InWorkflow;
	Action->Inputs = InInputs;
	Action->Backend = InBackend;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

UComfyRunWorkflowAsyncAction* UComfyRunWorkflowAsyncAction::RunComfyRouterModel(UObject* WorldContextObject, const FString& InModelId, const FString& InRequestJson, FName InBackend)
{
	UComfyRunWorkflowAsyncAction* Action = NewObject<UComfyRunWorkflowAsyncAction>();
	Action->ModelId = InModelId;
	Action->RequestJson = InRequestJson;
	Action->Backend = InBackend;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UComfyRunWorkflowAsyncAction::Activate()
{
	UComfySubsystem* Comfy = UComfySubsystem::Get();
	if (!Comfy)
	{
		UE_LOG(LogComfy, Error, TEXT("Run Comfy Workflow: the Comfy subsystem isn't available."));
		OnFailed.Broadcast(nullptr);
		SetReadyToDestroy();
		return;
	}

	Job = ModelId.IsEmpty()
		? Comfy->RunWorkflow(Workflow, Inputs, Backend)
		: Comfy->RunRouterModel(ModelId, RequestJson, Backend);

	if (!Job)
	{
		OnFailed.Broadcast(nullptr);
		SetReadyToDestroy();
		return;
	}

	// The job may already have failed during validation.
	if (Job->IsFinished())
	{
		HandleFinished(Job);
		return;
	}

	Job->OnProgress.AddDynamic(this, &UComfyRunWorkflowAsyncAction::HandleProgress);
	Job->OnFinishedNative.AddUObject(this, &UComfyRunWorkflowAsyncAction::HandleFinished);
}

void UComfyRunWorkflowAsyncAction::HandleProgress(UComfyJob* InJob)
{
	OnProgress.Broadcast(InJob);
}

void UComfyRunWorkflowAsyncAction::HandleFinished(UComfyJob* InJob)
{
	if (InJob)
	{
		InJob->OnProgress.RemoveDynamic(this, &UComfyRunWorkflowAsyncAction::HandleProgress);
		InJob->OnFinishedNative.RemoveAll(this);
	}

	if (InJob && InJob->Succeeded())
	{
		OnCompleted.Broadcast(InJob);
	}
	else
	{
		OnFailed.Broadcast(InJob);
	}
	SetReadyToDestroy();
}
