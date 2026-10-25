// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ComfyTypes.h"
#include "ComfyJob.generated.h"

class UComfyWorkflow;
class IComfyBackend;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FComfyJobEvent, UComfyJob*, Job);
DECLARE_MULTICAST_DELEGATE_OneParam(FComfyJobNativeEvent, UComfyJob*);

/**
 * One run of a workflow (or one Router model call). Returned immediately by every
 * Run function; bind to its events or poll State. Never blocks the game thread.
 */
UCLASS(BlueprintType)
class COMFYCORE_API UComfyJob : public UObject
{
	GENERATED_BODY()

public:
	/** Local id, stable for the session. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString JobId;

	/** The backend's own id: prompt_id on ComfyUI, job id on API v2, request_id on Router. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString RemoteId;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FName Backend;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	EComfyBackendKind BackendKind = EComfyBackendKind::Local;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	TObjectPtr<UComfyWorkflow> Workflow;

	/** Router model id, for Router jobs. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString ModelId;

	/** Display name: the workflow or model. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString Title;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	TMap<FString, FString> Inputs;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	EComfyJobState State = EComfyJobState::Pending;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FComfyProgress Progress;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	TArray<FComfyOutputFile> Outputs;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString ErrorMessage;

	/** Raw result JSON for Router jobs. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString ResultJson;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FDateTime CreatedAt;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FDateTime FinishedAt;

	/** True for a run read back from the project's history (Saved/Comfy/Jobs) rather than started this session. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	bool bFromHistory = false;

	/**
	 * Which workflow ran, for finding it again: its asset path (project workflows), source file
	 * (built-in and server workflows) and display name. Workflow itself is only set this session.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString WorkflowPath;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString WorkflowSource;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString WorkflowName;

	/** Fires on every state change. */
	UPROPERTY(BlueprintAssignable, Category = "Comfy")
	FComfyJobEvent OnStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Comfy")
	FComfyJobEvent OnProgress;

	/** Fires once, when the job succeeded and every output is downloaded. */
	UPROPERTY(BlueprintAssignable, Category = "Comfy")
	FComfyJobEvent OnCompleted;

	/** Fires once, when the job failed or was canceled. ErrorMessage says why. */
	UPROPERTY(BlueprintAssignable, Category = "Comfy")
	FComfyJobEvent OnFailed;

	/** C++ hook that fires once at the end, success or not. */
	FComfyJobNativeEvent OnFinishedNative;

	/** Ask the backend to stop this job. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Job")
	void Cancel();

	UFUNCTION(BlueprintPure, Category = "Comfy|Job")
	bool IsFinished() const { return ComfyTypes::IsTerminal(State); }

	UFUNCTION(BlueprintPure, Category = "Comfy|Job")
	bool Succeeded() const { return State == EComfyJobState::Succeeded; }

	UFUNCTION(BlueprintPure, Category = "Comfy|Job")
	FString GetStateText() const { return ComfyTypes::StateToString(State); }

	/** First output with this manifest name (or node id). */
	UFUNCTION(BlueprintPure, Category = "Comfy|Job")
	bool GetOutput(const FString& Name, FComfyOutputFile& OutFile) const;

	UFUNCTION(BlueprintPure, Category = "Comfy|Job")
	TArray<FComfyOutputFile> GetOutputsOfKind(EComfyOutputKind Kind) const;

	/** Folder the outputs are downloaded to. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Job")
	FString GetOutputDirectory() const;

	/** The job as JSON: state, progress, outputs and error. Used by the agent toolset. */
	FString ToJson() const;

	/** Writes the finished job to job.json beside its outputs, so the project's runs survive a restart. */
	void SaveRecord() const;

	/**
	 * A finished job read back from its folder under Saved/Comfy/Jobs. Folders from before job.json
	 * existed come back as "Earlier run" with whatever outputs are there. Null when there's nothing to show.
	 */
	static UComfyJob* LoadRecord(UObject* Outer, const FString& Directory);

	// Called by backends.
	void SetState(EComfyJobState NewState);
	void SetProgress(const FComfyProgress& NewProgress);
	void Fail(const FString& Message);
	void Complete();

	TSharedPtr<IComfyBackend> BackendImpl;
	/** Set by the subsystem so Cancel can reach Router jobs. */
	TFunction<void()> CancelHandler;
	double LastActivitySeconds = 0.0;
};
