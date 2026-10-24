// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "ComfyTypes.h"
#include "ComfySubsystem.generated.h"

class UComfyJob;
class UComfyWorkflow;
class IComfyBackend;

DECLARE_DYNAMIC_DELEGATE_OneParam(FComfyBackendStatusDelegate, const FComfyBackendStatus&, Status);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FComfyTextResultDelegate, bool, bSuccess, const FString&, Text);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnComfyJobAdded, UComfyJob*);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnComfyBackendStatus, const FComfyBackendStatus&);
DECLARE_MULTICAST_DELEGATE(FOnComfyActiveBackendChanged);
DECLARE_MULTICAST_DELEGATE(FOnComfyServerWorkflowsChanged);

/**
 * Entry point for running Comfy workflows from C++, Blueprints and Python, in the editor
 * and at runtime. Every Run function returns a UComfyJob at once; the work happens
 * asynchronously and the job's events report progress, completion and failure.
 *
 * Python: unreal.get_engine_subsystem(unreal.ComfySubsystem)
 */
UCLASS()
class COMFYCORE_API UComfySubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	static UComfySubsystem* Get();

	virtual void Deinitialize() override;

	/**
	 * Run a workflow asset. Inputs are keyed by the names in the workflow's manifest; anything
	 * left out keeps the value from the exported graph. File inputs take an absolute path on disk.
	 * Backend picks a profile by name; None uses the active backend.
	 */
	UFUNCTION(BlueprintCallable, Category = "Comfy", meta = (AutoCreateRefTerm = "Inputs"))
	UComfyJob* RunWorkflow(UComfyWorkflow* Workflow, const TMap<FString, FString>& Inputs, FName Backend = NAME_None);

	/** Run an API-format graph given as JSON, without an asset. */
	UFUNCTION(BlueprintCallable, Category = "Comfy", meta = (AutoCreateRefTerm = "Inputs"))
	UComfyJob* RunWorkflowJson(const FString& ApiGraphJson, const TMap<FString, FString>& Inputs, FName Backend = NAME_None);

	/**
	 * Call one Comfy Router model (for example "bfl/flux-2-pro") with its native request body.
	 * Media URLs in the result are downloaded as the job's outputs. Spends Comfy credits.
	 */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Router")
	UComfyJob* RunRouterModel(const FString& ModelId, const FString& RequestJson, FName Backend = NAME_None);

	/** Fetch a Router model's OpenAPI document (its input and output schema). */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Router")
	void GetRouterModelSchema(const FString& ModelId, FComfyTextResultDelegate OnResult, FName Backend = NAME_None);

	/** List Router models, one page at a time. Pass the previous page's next_cursor, or empty. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Router")
	void ListRouterModels(const FString& Cursor, FComfyTextResultDelegate OnResult, FName Backend = NAME_None);

	/** Check that a backend answers and accepts our key. Native backends also report their node types. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Backends")
	void ProbeBackend(FName Backend, FComfyBackendStatusDelegate OnStatus);

	/** The last probe result for a backend. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Backends")
	FComfyBackendStatus GetLastStatus(FName Backend) const;

	UFUNCTION(BlueprintPure, Category = "Comfy|Backends")
	TArray<FName> GetBackendNames() const;

	UFUNCTION(BlueprintPure, Category = "Comfy|Backends")
	bool GetBackendProfile(FName Backend, FComfyBackendProfile& OutProfile) const;

	/** The backend used when a run doesn't name one. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Backends")
	FName GetActiveBackend() const;

	/** Switch the active backend. In the editor, bRemember saves it to your user settings. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Backends")
	void SetActiveBackend(FName Backend, bool bRemember = true);

	/**
	 * Use this API key for the rest of the session, without saving it. For shipped games,
	 * prefer calling your own server, which holds the key, over putting a key in a client.
	 */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Backends")
	void SetSessionApiKey(const FString& ApiKey);

	/** True when running this workflow on this backend can spend Comfy credits. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Backends")
	bool SpendsCredits(FName Backend, const UComfyWorkflow* Workflow) const;

	/** Choices the backend reports for a workflow input (model files, enums). Empty when unknown: probe the backend first. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Backends")
	TArray<FString> GetInputOptions(FName Backend, const UComfyWorkflow* Workflow, const FString& InputName) const;

	/** The value a Model input gets when nobody sets it: the first backend file matching its Prefer list, else its default. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Backends")
	FString GetDefaultInputValue(FName Backend, const UComfyWorkflow* Workflow, const FString& InputName) const;

	/** Node classes the workflow needs that the backend doesn't have. Empty when all are there, or when the backend can't say. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Backends")
	TArray<FString> GetMissingNodeClasses(FName Backend, const UComfyWorkflow* Workflow) const;

	/**
	 * The default workflows that ship with the plugin (Resources/Workflows), loaded as
	 * transient assets. Duplicate one into your project to customize it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Workflow")
	TArray<UComfyWorkflow*> GetBuiltInWorkflows(bool bReload = false);

	/**
	 * Fetch the workflows the Comfy for Unreal ComfyUI extension saved on a local backend
	 * (GET /comfy_unreal/workflows). Backends without the extension, and Comfy Cloud, simply have none.
	 */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Workflow")
	void RefreshServerWorkflows(FName Backend = NAME_None);

	/** The last fetched server workflows, as transient assets. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Workflow")
	TArray<UComfyWorkflow*> GetServerWorkflows() const;

	/** Tell the extension an Unreal editor is connected (POST /comfy_unreal/heartbeat). Comfy Studio sends one while it's open. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Workflow")
	void SendHeartbeat(FName Backend = NAME_None);

	UFUNCTION(BlueprintPure, Category = "Comfy|Jobs")
	TArray<UComfyJob*> GetJobs() const;

	UFUNCTION(BlueprintPure, Category = "Comfy|Jobs")
	UComfyJob* FindJob(const FString& JobId) const;

	UFUNCTION(BlueprintCallable, Category = "Comfy|Jobs")
	void ClearFinishedJobs();

	FString ResolveApiKey(const FComfyBackendProfile& Profile) const;

	// C++ forms of the Router queries, which report errors as well as results.
	void GetRouterModelSchemaNative(const FString& ModelId, TFunction<void(bool, const FString&)> OnResult, FName Backend = NAME_None);
	void ListRouterModelsNative(const FString& Cursor, TFunction<void(bool, const FString&)> OnResult, FName Backend = NAME_None);

	FOnComfyJobAdded OnJobAdded;
	FOnComfyBackendStatus OnBackendStatus;
	FOnComfyActiveBackendChanged OnActiveBackendChanged;
	FOnComfyServerWorkflowsChanged OnServerWorkflowsChanged;

private:
	UComfyJob* NewJob(FName Backend, const FString& Title);
	bool FindRouterProfile(FName Backend, FComfyBackendProfile& OutProfile) const;
	TSharedPtr<IComfyBackend> GetOrCreateBackend(const FComfyBackendProfile& Profile);
	void UploadInputs(UComfyJob* Job, TSharedPtr<IComfyBackend> Backend, TArray<TPair<FString, FString>> PendingFiles, TMap<FString, TSharedPtr<class FJsonValue>> References);
	void SubmitGraph(UComfyJob* Job, TSharedPtr<IComfyBackend> Backend, const TMap<FString, TSharedPtr<class FJsonValue>>& References);
	void PollJob(UComfyJob* Job, TSharedPtr<IComfyBackend> Backend, double StartSeconds);
	void DownloadOutputs(UComfyJob* Job, TSharedPtr<IComfyBackend> Backend, int32 Index);

	void RouterPoll(UComfyJob* Job, FComfyBackendProfile Profile, FString ApiKey, FString StatusUrl, FString ResponseUrl, double StartSeconds);
	void RouterCollect(UComfyJob* Job, FComfyBackendProfile Profile, FString ApiKey, FString ResponseUrl);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UComfyJob>> Jobs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UComfyWorkflow>> BuiltInWorkflows;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UComfyWorkflow>> ServerWorkflows;
	bool bBuiltInsLoaded = false;

	TMap<FName, TSharedPtr<IComfyBackend>> BackendInstances;
	TMap<FName, FString> BackendFingerprints;
	TMap<FName, FComfyBackendStatus> Statuses;
	FString SessionApiKey;
	FName SessionActiveBackend;
};
