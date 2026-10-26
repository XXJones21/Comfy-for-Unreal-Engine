// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "ComfyTypes.h"
#include "ComfyRunWorkflowAsyncAction.generated.h"

class UComfyJob;
class UComfySubsystem;
class UComfyWorkflow;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FComfyAsyncJobPin, UComfyJob*, Job);

/**
 * Blueprint node "Run Comfy Workflow (String Map)": starts a job and fires On Progress while it runs,
 * then On Completed or On Failed once. Outputs are downloaded before On Completed fires.
 * The typed "Run Comfy Workflow" node, with a pin per input, uses UComfyRunWorkflowWithInputsAction.
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

	UFUNCTION(BlueprintCallable, Category = "Comfy", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "Inputs", DisplayName = "Run Comfy Workflow (String Map)"))
	static UComfyRunWorkflowAsyncAction* RunComfyWorkflow(UObject* WorldContextObject, UComfyWorkflow* Workflow, const TMap<FString, FString>& Inputs, EComfyBackendTarget Backend = EComfyBackendTarget::Active);

	UFUNCTION(BlueprintCallable, Category = "Comfy|Router", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Run Comfy Router Model"))
	static UComfyRunWorkflowAsyncAction* RunComfyRouterModel(UObject* WorldContextObject, const FString& ModelId, const FString& RequestJson, EComfyBackendTarget Backend = EComfyBackendTarget::Active);

	virtual void Activate() override;

protected:
	/** Start the job this action runs. */
	virtual UComfyJob* StartJob(UComfySubsystem* Comfy);

	UFUNCTION()
	void HandleProgress(UComfyJob* Job);

	void HandleFinished(UComfyJob* Job);

	UPROPERTY()
	TObjectPtr<UComfyWorkflow> WorkflowToRun;

	TMap<FString, FString> Inputs;
	FString ModelId;
	FString RequestJson;
	EComfyBackendTarget Backend = EComfyBackendTarget::Active;

	UPROPERTY()
	TObjectPtr<UComfyJob> Job;
};

/**
 * The action behind the "Run Comfy Workflow" node (UK2Node_RunComfyWorkflow in ComfyBlueprint), which shows one pin
 * per workflow input and passes them here as Comfy Inputs. It has its own node, so it isn't listed as a plain async action.
 */
UCLASS(meta = (HasDedicatedAsyncNode))
class COMFYCORE_API UComfyRunWorkflowWithInputsAction : public UComfyRunWorkflowAsyncAction
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Comfy", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "Inputs", DisplayName = "Run Comfy Workflow"))
	static UComfyRunWorkflowWithInputsAction* RunComfyWorkflowWithInputs(UObject* WorldContextObject, UComfyWorkflow* Workflow, const FComfyInputs& Inputs, EComfyBackendTarget Backend = EComfyBackendTarget::Active);

protected:
	virtual UComfyJob* StartJob(UComfySubsystem* Comfy) override;

	UPROPERTY()
	FComfyInputs TypedInputs;
};
