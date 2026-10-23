// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "ComfyRunWorkflowAsyncAction.generated.h"

class UComfyJob;
class UComfyWorkflow;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FComfyAsyncJobPin, UComfyJob*, Job);

/**
 * Blueprint node "Run Comfy Workflow": starts a job and fires On Progress while it runs,
 * then On Completed or On Failed once. Outputs are downloaded before On Completed fires.
 */
UCLASS()
class COMFYCORE_API UComfyRunWorkflowAsyncAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FComfyAsyncJobPin OnProgress;

	UPROPERTY(BlueprintAssignable)
	FComfyAsyncJobPin OnCompleted;

	UPROPERTY(BlueprintAssignable)
	FComfyAsyncJobPin OnFailed;

	UFUNCTION(BlueprintCallable, Category = "Comfy", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "Inputs", DisplayName = "Run Comfy Workflow"))
	static UComfyRunWorkflowAsyncAction* RunComfyWorkflow(UObject* WorldContextObject, UComfyWorkflow* Workflow, const TMap<FString, FString>& Inputs, FName Backend = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Comfy|Router", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Run Comfy Router Model"))
	static UComfyRunWorkflowAsyncAction* RunComfyRouterModel(UObject* WorldContextObject, const FString& ModelId, const FString& RequestJson, FName Backend = NAME_None);

	virtual void Activate() override;

private:
	UFUNCTION()
	void HandleProgress(UComfyJob* Job);

	void HandleFinished(UComfyJob* Job);

	UPROPERTY()
	TObjectPtr<UComfyWorkflow> Workflow;

	TMap<FString, FString> Inputs;
	FString ModelId;
	FString RequestJson;
	FName Backend;

	UPROPERTY()
	TObjectPtr<UComfyJob> Job;
};
