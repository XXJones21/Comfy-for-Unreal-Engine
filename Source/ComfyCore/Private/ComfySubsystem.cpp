// Copyright Comfy Org. All Rights Reserved.

#include "ComfySubsystem.h"
#include "ComfyCoreModule.h"
#include "ComfyJob.h"
#include "ComfySettings.h"
#include "ComfyWorkflow.h"
#include "Backends/ComfyBackend.h"
#include "Backends/ComfyHttp.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Engine.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"

UComfySubsystem* UComfySubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UComfySubsystem>() : nullptr;
}

void UComfySubsystem::Deinitialize()
{
	for (UComfyJob* Job : Jobs)
	{
		if (Job && !Job->IsFinished())
		{
			Job->Cancel();
		}
	}
	BackendInstances.Reset();
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------
// Backends

TArray<FName> UComfySubsystem::GetBackendNames() const
{
	TArray<FName> Names;
	for (const FComfyBackendProfile& Profile : GetDefault<UComfySettings>()->Backends)
	{
		Names.Add(Profile.Name);
	}
	return Names;
}

bool UComfySubsystem::GetBackendProfile(FName Backend, FComfyBackendProfile& OutProfile) const
{
	if (const FComfyBackendProfile* Found = GetDefault<UComfySettings>()->FindBackend(Backend.IsNone() ? GetActiveBackend() : Backend))
	{
		OutProfile = *Found;
		return true;
	}
	return false;
}

FName UComfySubsystem::GetActiveBackend() const
{
	if (!SessionActiveBackend.IsNone())
	{
		return SessionActiveBackend;
	}
	const UComfyUserSettings* User = GetDefault<UComfyUserSettings>();
	if (!User->ActiveBackend.IsNone() && GetDefault<UComfySettings>()->FindBackend(User->ActiveBackend))
	{
		return User->ActiveBackend;
	}
	return GetDefault<UComfySettings>()->DefaultBackend;
}

void UComfySubsystem::SetActiveBackend(FName Backend, bool bRemember)
{
	SessionActiveBackend = Backend;
	if (bRemember && GIsEditor)
	{
		UComfyUserSettings* User = GetMutableDefault<UComfyUserSettings>();
		User->ActiveBackend = Backend;
		User->SaveConfig();
	}
	OnActiveBackendChanged.Broadcast();
}

void UComfySubsystem::SetSessionApiKey(const FString& ApiKey)
{
	SessionApiKey = ApiKey;
	BackendInstances.Reset();
}

FString UComfySubsystem::ResolveApiKey(const FComfyBackendProfile& Profile) const
{
	if (!Profile.ApiKeyEnvironmentVariable.IsEmpty())
	{
		const FString FromEnv = FPlatformMisc::GetEnvironmentVariable(*Profile.ApiKeyEnvironmentVariable);
		if (!FromEnv.IsEmpty())
		{
			return FromEnv;
		}
	}
	// comfy-cli's workspace key, so a machine set up for `comfy build` / `comfy deploy` works as is.
	const FString FromCli = FPlatformMisc::GetEnvironmentVariable(TEXT("COMFY_CLOUD_API_KEY"));
	if (!FromCli.IsEmpty())
	{
		return FromCli;
	}
	if (!SessionApiKey.IsEmpty())
	{
		return SessionApiKey;
	}
	return GetDefault<UComfyUserSettings>()->ApiKey;
}

TSharedPtr<IComfyBackend> UComfySubsystem::GetOrCreateBackend(const FComfyBackendProfile& Profile)
{
	const FString Key = ResolveApiKey(Profile);
	const FString Fingerprint = FString::Printf(TEXT("%d|%s|%d|%d|%s"), int32(Profile.Kind), *Profile.BaseUrl, int32(Profile.LocalProtocol), Profile.bSendPartnerNodeKey ? 1 : 0, *FMD5::HashAnsiString(*Key));
	if (TSharedPtr<IComfyBackend>* Existing = BackendInstances.Find(Profile.Name))
	{
		if (BackendFingerprints.FindRef(Profile.Name) == Fingerprint)
		{
			return *Existing;
		}
	}
	TSharedPtr<IComfyBackend> Backend;
	if (Profile.UsesApiV2())
	{
		Backend = MakeShared<FComfyApiV2Backend>(Profile, Key);
	}
	else
	{
		Backend = MakeShared<FComfyNativeBackend>(Profile, Key);
	}
	BackendInstances.Add(Profile.Name, Backend);
	BackendFingerprints.Add(Profile.Name, Fingerprint);
	return Backend;
}

void UComfySubsystem::ProbeBackend(FName Backend, FComfyBackendStatusDelegate OnStatus)
{
	FComfyBackendProfile Profile;
	if (!GetBackendProfile(Backend, Profile))
	{
		FComfyBackendStatus Status;
		Status.Backend = Backend;
		Status.Summary = FString::Printf(TEXT("No backend named \"%s\" in Project Settings > Plugins > Comfy."), *Backend.ToString());
		OnStatus.ExecuteIfBound(Status);
		return;
	}

	TWeakObjectPtr<UComfySubsystem> WeakThis = this;
	auto Finish = [WeakThis, OnStatus](const FComfyBackendStatus& Status)
	{
		if (UComfySubsystem* This = WeakThis.Get())
		{
			This->Statuses.Add(Status.Backend, Status);
			This->OnBackendStatus.Broadcast(Status);
		}
		OnStatus.ExecuteIfBound(Status);
	};

	if (Profile.Kind == EComfyBackendKind::Router)
	{
		const FString Key = ResolveApiKey(Profile);
		FComfyBackendStatus Status;
		Status.Backend = Profile.Name;
		if (Key.IsEmpty())
		{
			Status.Summary = TEXT("No API key. Add one in Editor Preferences > Plugins > Comfy, or set COMFY_API_KEY.");
			Finish(Status);
			return;
		}
		ComfyHttp::Send(TEXT("GET"), Profile.BaseUrl + TEXT("/v2/models?limit=1"), { { TEXT("Authorization"), TEXT("Bearer ") + Key } }, [Status, Profile, Finish](const ComfyHttp::FResponse& Response) mutable
		{
			Status.bReachable = Response.IsOk();
			Status.Summary = Response.IsOk() ? FString::Printf(TEXT("Comfy Router at %s, key accepted"), *Profile.BaseUrl) : Response.Describe(TEXT("Comfy Router"));
			Finish(Status);
		}, 15.f);
		return;
	}

	GetOrCreateBackend(Profile)->Probe(Finish);
}

FComfyBackendStatus UComfySubsystem::GetLastStatus(FName Backend) const
{
	if (const FComfyBackendStatus* Found = Statuses.Find(Backend.IsNone() ? GetActiveBackend() : Backend))
	{
		return *Found;
	}
	FComfyBackendStatus Unknown;
	Unknown.Backend = Backend;
	Unknown.Summary = TEXT("Not checked yet");
	return Unknown;
}

bool UComfySubsystem::SpendsCredits(FName Backend, const UComfyWorkflow* Workflow) const
{
	FComfyBackendProfile Profile;
	if (!GetBackendProfile(Backend, Profile))
	{
		return false;
	}
	return Profile.Kind != EComfyBackendKind::Local || (Workflow && Workflow->bUsesPartnerNodes);
}

TArray<FString> UComfySubsystem::GetMissingNodeClasses(FName Backend, const UComfyWorkflow* Workflow) const
{
	TArray<FString> Missing;
	const FComfyBackendStatus* Status = Statuses.Find(Backend.IsNone() ? GetActiveBackend() : Backend);
	if (!Workflow || !Status || Status->NodeClasses.Num() == 0)
	{
		return Missing;
	}
	for (const FString& Class : Workflow->RequiredNodeClasses)
	{
		if (!Status->NodeClasses.Contains(Class))
		{
			Missing.Add(Class);
		}
	}
	return Missing;
}

TArray<FString> UComfySubsystem::GetInputOptions(FName Backend, const UComfyWorkflow* Workflow, const FString& InputName) const
{
	FComfyWorkflowInput Input;
	const FComfyBackendStatus* Status = Statuses.Find(Backend.IsNone() ? GetActiveBackend() : Backend);
	if (!Workflow || !Status || !Workflow->FindInput(InputName, Input))
	{
		return {};
	}
	const TArray<FString>* Options = Status->InputOptions.Find(Workflow->GetNodeClass(Input.NodeId) + TEXT(".") + Input.InputName);
	return Options ? *Options : TArray<FString>();
}

FString UComfySubsystem::GetDefaultInputValue(FName Backend, const UComfyWorkflow* Workflow, const FString& InputName) const
{
	FComfyWorkflowInput Input;
	if (!Workflow || !Workflow->FindInput(InputName, Input))
	{
		return FString();
	}
	if (Input.Type != EComfyInputType::Model)
	{
		return Input.DefaultValue;
	}
	return ComfyTypes::PickModel(GetInputOptions(Backend, Workflow, InputName), Input.Prefer, Input.DefaultValue);
}

// ---------------------------------------------------------------------------------------------
// Jobs

TArray<UComfyJob*> UComfySubsystem::GetJobs() const
{
	TArray<UComfyJob*> Result;
	for (UComfyJob* Job : Jobs)
	{
		Result.Add(Job);
	}
	return Result;
}

UComfyJob* UComfySubsystem::FindJob(const FString& JobId) const
{
	for (UComfyJob* Job : Jobs)
	{
		if (Job && (Job->JobId == JobId || Job->RemoteId == JobId))
		{
			return Job;
		}
	}
	return nullptr;
}

void UComfySubsystem::ClearFinishedJobs()
{
	Jobs.RemoveAll([](const TObjectPtr<UComfyJob>& Job) { return !Job || Job->IsFinished(); });
}

UComfyJob* UComfySubsystem::NewJob(FName Backend, const FString& Title)
{
	UComfyJob* Job = NewObject<UComfyJob>(this);
	Job->JobId = FString::Printf(TEXT("%s-%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6).ToLower());
	Job->Backend = Backend;
	Job->Title = Title;
	Job->CreatedAt = FDateTime::UtcNow();
	Jobs.Add(Job);
	OnJobAdded.Broadcast(Job);
	return Job;
}

UComfyJob* UComfySubsystem::RunWorkflowJson(const FString& ApiGraphJson, const TMap<FString, FString>& Inputs, FName Backend)
{
	FString Error;
	UComfyWorkflow* Workflow = UComfyWorkflow::CreateTransientFromJson(ApiGraphJson, Error);
	if (!Workflow)
	{
		UComfyJob* Job = NewJob(Backend.IsNone() ? GetActiveBackend() : Backend, TEXT("Inline workflow"));
		Job->Fail(Error);
		return Job;
	}
	return RunWorkflow(Workflow, Inputs, Backend);
}

UComfyJob* UComfySubsystem::RunWorkflow(UComfyWorkflow* Workflow, const TMap<FString, FString>& Inputs, FName Backend)
{
	const FName BackendName = Backend.IsNone() ? GetActiveBackend() : Backend;
	UComfyJob* Job = NewJob(BackendName, Workflow ? Workflow->GetDisplayNameString() : TEXT("(no workflow)"));
	Job->Workflow = Workflow;
	Job->Inputs = Inputs;
	if (Workflow)
	{
		// Model files differ between machines and Cloud, so an unset model input takes this backend's best match.
		for (const FComfyWorkflowInput& Input : Workflow->Inputs)
		{
			const FString* Value = Inputs.Find(Input.Name);
			if (Input.Type == EComfyInputType::Model && (!Value || Value->IsEmpty()))
			{
				Job->Inputs.Add(Input.Name, GetDefaultInputValue(BackendName, Workflow, Input.Name));
			}
		}
	}

	if (!Workflow)
	{
		Job->Fail(TEXT("No workflow given."));
		return Job;
	}
	FComfyBackendProfile Profile;
	if (!GetBackendProfile(BackendName, Profile))
	{
		Job->Fail(FString::Printf(TEXT("No backend named \"%s\". Backends: %s"), *BackendName.ToString(), *FString::JoinBy(GetBackendNames(), TEXT(", "), [](FName N) { return N.ToString(); })));
		return Job;
	}
	Job->BackendKind = Profile.Kind;
	if (Profile.Kind == EComfyBackendKind::Router)
	{
		Job->Fail(TEXT("Comfy Router runs single models, not workflows. Pick a Desktop, Cloud or deployment backend, or call Run Router Model."));
		return Job;
	}
	const TArray<FString> Missing = GetMissingNodeClasses(BackendName, Workflow);
	if (Missing.Num() > 0)
	{
		Job->Fail(FString::Printf(TEXT("%s doesn't have these nodes: %s. Install them (ComfyUI Manager) or run on another backend."), *BackendName.ToString(), *FString::Join(Missing, TEXT(", "))));
		return Job;
	}

	TSharedPtr<IComfyBackend> BackendImpl = GetOrCreateBackend(Profile);
	Job->BackendImpl = BackendImpl;

	// Files to upload first: File inputs given a path on disk.
	TArray<TPair<FString, FString>> PendingFiles;
	for (const FComfyWorkflowInput& Input : Workflow->Inputs)
	{
		const FString* Value = Inputs.Find(Input.Name);
		if (Input.Type == EComfyInputType::File && Value && !Value->IsEmpty())
		{
			if (!FPaths::FileExists(*Value))
			{
				Job->Fail(FString::Printf(TEXT("Input \"%s\": file not found: %s"), *Input.Name, **Value));
				return Job;
			}
			PendingFiles.Emplace(Input.Name, *Value);
		}
	}
	if (PendingFiles.Num() > 0)
	{
		Job->SetState(EComfyJobState::Uploading);
	}
	UploadInputs(Job, BackendImpl, MoveTemp(PendingFiles), {});
	return Job;
}

void UComfySubsystem::UploadInputs(UComfyJob* Job, TSharedPtr<IComfyBackend> Backend, TArray<TPair<FString, FString>> PendingFiles, TMap<FString, TSharedPtr<FJsonValue>> References)
{
	if (Job->IsFinished())
	{
		return;
	}
	if (PendingFiles.Num() == 0)
	{
		SubmitGraph(Job, Backend, References);
		return;
	}
	const TPair<FString, FString> Next = PendingFiles.Pop();
	TWeakObjectPtr<UComfyJob> WeakJob = Job;
	TWeakObjectPtr<UComfySubsystem> WeakThis = this;
	Backend->UploadFile(Next.Value, [WeakJob, WeakThis, Backend, PendingFiles, References, Name = Next.Key](bool bOk, TSharedPtr<FJsonValue> Value, const FString& Error) mutable
	{
		UComfyJob* Job = WeakJob.Get();
		UComfySubsystem* This = WeakThis.Get();
		if (!Job || !This)
		{
			return;
		}
		if (!bOk)
		{
			Job->Fail(Error);
			return;
		}
		References.Add(Name, Value);
		This->UploadInputs(Job, Backend, MoveTemp(PendingFiles), MoveTemp(References));
	});
}

void UComfySubsystem::SubmitGraph(UComfyJob* Job, TSharedPtr<IComfyBackend> Backend, const TMap<FString, TSharedPtr<FJsonValue>>& References)
{
	FString Error;
	const TSharedPtr<FJsonObject> Graph = Job->Workflow->BuildGraph(Job->Inputs, References, Error);
	if (!Graph.IsValid())
	{
		Job->Fail(Error);
		return;
	}
	Job->SetState(EComfyJobState::Queued);
	TWeakObjectPtr<UComfyJob> WeakJob = Job;
	TWeakObjectPtr<UComfySubsystem> WeakThis = this;
	Backend->Submit(Job, Graph.ToSharedRef(), [WeakJob, WeakThis, Backend](bool bOk, const FString& SubmitError)
	{
		UComfyJob* Job = WeakJob.Get();
		UComfySubsystem* This = WeakThis.Get();
		if (!Job || !This || Job->IsFinished())
		{
			return;
		}
		if (!bOk)
		{
			Job->Fail(SubmitError);
			return;
		}
		UE_LOG(LogComfy, Log, TEXT("Job %s submitted to %s as %s"), *Job->JobId, *Job->Backend.ToString(), *Job->RemoteId);
		This->PollJob(Job, Backend, FPlatformTime::Seconds());
	});
}

void UComfySubsystem::PollJob(UComfyJob* Job, TSharedPtr<IComfyBackend> Backend, double StartSeconds)
{
	if (Job->IsFinished())
	{
		return;
	}
	const UComfySettings* Settings = GetDefault<UComfySettings>();
	if (FPlatformTime::Seconds() - StartSeconds > Settings->JobTimeoutMinutes * 60.0)
	{
		Backend->CancelJob(Job);
		Job->Fail(FString::Printf(TEXT("Timed out after %d minutes. Raise the limit in Project Settings > Plugins > Comfy."), Settings->JobTimeoutMinutes));
		return;
	}
	TWeakObjectPtr<UComfyJob> WeakJob = Job;
	TWeakObjectPtr<UComfySubsystem> WeakThis = this;
	const float Base = Settings->PollIntervalSeconds;
	Backend->Poll(Job, [WeakJob, WeakThis, Backend, StartSeconds, Base](EComfyPollResult Result, const FString& Error, float NextPoll)
	{
		UComfyJob* Job = WeakJob.Get();
		UComfySubsystem* This = WeakThis.Get();
		if (!Job || !This || Job->IsFinished())
		{
			return;
		}
		switch (Result)
		{
		case EComfyPollResult::Pending:
			ComfyHttp::Delay(FMath::Max(Base, NextPoll), [WeakJob, WeakThis, Backend, StartSeconds]()
			{
				if (WeakJob.IsValid() && WeakThis.IsValid())
				{
					WeakThis->PollJob(WeakJob.Get(), Backend, StartSeconds);
				}
			});
			break;
		case EComfyPollResult::Failed:
			Job->Fail(Error);
			break;
		case EComfyPollResult::Canceled:
			Job->ErrorMessage = Error;
			Job->SetState(EComfyJobState::Canceled);
			Job->OnFailed.Broadcast(Job);
			Job->OnFinishedNative.Broadcast(Job);
			break;
		case EComfyPollResult::Done:
			if (Job->Outputs.Num() == 0)
			{
				Job->Fail(TEXT("The workflow finished but produced no files. Check that it ends in a Save node."));
				return;
			}
			Job->SetState(EComfyJobState::Downloading);
			This->DownloadOutputs(Job, Backend, 0);
			break;
		}
	});
}

void UComfySubsystem::DownloadOutputs(UComfyJob* Job, TSharedPtr<IComfyBackend> Backend, int32 Index)
{
	if (Job->IsFinished())
	{
		return;
	}
	if (!Job->Outputs.IsValidIndex(Index))
	{
		Job->Complete();
		return;
	}
	FComfyProgress Progress = Job->Progress;
	Progress.Message = FString::Printf(TEXT("Downloading %d/%d"), Index + 1, Job->Outputs.Num());
	Job->SetProgress(Progress);

	TWeakObjectPtr<UComfyJob> WeakJob = Job;
	TWeakObjectPtr<UComfySubsystem> WeakThis = this;
	Backend->Download(Job->Outputs[Index], [WeakJob, WeakThis, Backend, Index](bool bOk, const FString& Error)
	{
		UComfyJob* Job = WeakJob.Get();
		UComfySubsystem* This = WeakThis.Get();
		if (!Job || !This)
		{
			return;
		}
		if (!bOk)
		{
			Job->Fail(Error);
			return;
		}
		This->DownloadOutputs(Job, Backend, Index + 1);
	});
}

// ---------------------------------------------------------------------------------------------
// Comfy Router

namespace ComfyRouterPrivate
{
	static FString ModelPath(const FComfyBackendProfile& Profile, const FString& ModelId)
	{
		return Profile.BaseUrl + TEXT("/v2/models/") + ModelId;
	}

	static FString ExtensionFromContentType(const FString& ContentType)
	{
		if (ContentType.Contains(TEXT("png"))) return TEXT("png");
		if (ContentType.Contains(TEXT("jpeg"))) return TEXT("jpg");
		if (ContentType.Contains(TEXT("webp"))) return TEXT("webp");
		if (ContentType.Contains(TEXT("wav"))) return TEXT("wav");
		if (ContentType.Contains(TEXT("mpeg"))) return TEXT("mp3");
		if (ContentType.Contains(TEXT("mp4"))) return TEXT("mp4");
		if (ContentType.Contains(TEXT("gltf-binary"))) return TEXT("glb");
		return TEXT("bin");
	}

	static bool LooksLikeMediaUrl(const FString& Value)
	{
		if (Value.StartsWith(TEXT("data:")))
		{
			return true;
		}
		if (!Value.StartsWith(TEXT("http://")) && !Value.StartsWith(TEXT("https://")))
		{
			return false;
		}
		FString Path = Value;
		int32 Query = INDEX_NONE;
		if (Path.FindChar(TEXT('?'), Query))
		{
			Path.LeftInline(Query);
		}
		return ComfyTypes::KindFromFileName(Path) != EComfyOutputKind::File || Path.EndsWith(TEXT(".bin"));
	}

	/** Collect every media URL in a Router result, wherever the model put it. */
	static void CollectUrls(const TSharedPtr<FJsonValue>& Value, const FString& Path, TArray<TPair<FString, FString>>& Out)
	{
		if (!Value.IsValid())
		{
			return;
		}
		switch (Value->Type)
		{
		case EJson::String:
			if (LooksLikeMediaUrl(Value->AsString()))
			{
				Out.Emplace(Path, Value->AsString());
			}
			break;
		case EJson::Array:
		{
			const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
			for (int32 i = 0; i < Items.Num(); ++i)
			{
				CollectUrls(Items[i], FString::Printf(TEXT("%s[%d]"), *Path, i), Out);
			}
			break;
		}
		case EJson::Object:
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Value->AsObject()->Values)
			{
				CollectUrls(Pair.Value, Path.IsEmpty() ? Pair.Key : Path + TEXT(".") + Pair.Key, Out);
			}
			break;
		default:
			break;
		}
	}
}

UComfyJob* UComfySubsystem::RunRouterModel(const FString& ModelId, const FString& RequestJson, FName Backend)
{
	using namespace ComfyRouterPrivate;

	FComfyBackendProfile Profile;
	FName BackendName = Backend;
	if (BackendName.IsNone())
	{
		// Router calls go to the first Router profile unless one is named.
		for (const FComfyBackendProfile& Candidate : GetDefault<UComfySettings>()->Backends)
		{
			if (Candidate.Kind == EComfyBackendKind::Router)
			{
				BackendName = Candidate.Name;
				break;
			}
		}
	}
	UComfyJob* Job = NewJob(BackendName, ModelId);
	Job->ModelId = ModelId;
	Job->BackendKind = EComfyBackendKind::Router;
	if (!GetBackendProfile(BackendName, Profile) || Profile.Kind != EComfyBackendKind::Router)
	{
		Job->Fail(TEXT("No Comfy Router backend. Add one in Project Settings > Plugins > Comfy with Kind = Router and URL https://api.comfy.org."));
		return Job;
	}
	const FString Key = ResolveApiKey(Profile);
	if (Key.IsEmpty())
	{
		Job->Fail(TEXT("Comfy Router needs an API key. Add one in Editor Preferences > Plugins > Comfy, or set COMFY_API_KEY."));
		return Job;
	}
	TSharedPtr<FJsonObject> Body;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
	if (!FJsonSerializer::Deserialize(Reader, Body) || !Body.IsValid())
	{
		Job->Fail(TEXT("The request body isn't a JSON object."));
		return Job;
	}

	const ComfyHttp::FHeaders Headers = { { TEXT("Authorization"), TEXT("Bearer ") + Key }, { TEXT("Idempotency-Key"), ComfyHttp::NewIdempotencyKey() } };
	TWeakObjectPtr<UComfyJob> WeakJob = Job;
	TWeakObjectPtr<UComfySubsystem> WeakThis = this;
	Job->SetState(EComfyJobState::Queued);
	ComfyHttp::SendJson(TEXT("POST"), ModelPath(Profile, ModelId) + TEXT("/requests"), Headers, Body.ToSharedRef(), [WeakJob, WeakThis, Profile, Key, ModelId](const ComfyHttp::FResponse& Response)
	{
		UComfyJob* Job = WeakJob.Get();
		UComfySubsystem* This = WeakThis.Get();
		if (!Job || !This || Job->IsFinished())
		{
			return;
		}
		if (!Response.IsOk() || !Response.Json.IsValid())
		{
			Job->Fail(Response.Describe(FString::Printf(TEXT("Submitting to %s"), *ModelId)));
			return;
		}
		Job->RemoteId = Response.Json->GetStringField(TEXT("request_id"));
		FString StatusUrl = ModelPath(Profile, ModelId) + TEXT("/requests/") + Job->RemoteId + TEXT("/status");
		FString ResponseUrl = ModelPath(Profile, ModelId) + TEXT("/requests/") + Job->RemoteId;
		FString CancelUrl = ResponseUrl + TEXT("/cancel");
		FString Link;
		if (Response.Json->TryGetStringField(TEXT("status_url"), Link)) StatusUrl = ComfyHttp::ResolveLink(Profile.BaseUrl, Link);
		if (Response.Json->TryGetStringField(TEXT("response_url"), Link)) ResponseUrl = ComfyHttp::ResolveLink(Profile.BaseUrl, Link);
		if (Response.Json->TryGetStringField(TEXT("cancel_url"), Link)) CancelUrl = ComfyHttp::ResolveLink(Profile.BaseUrl, Link);
		Job->CancelHandler = [CancelUrl, Key]()
		{
			ComfyHttp::Send(TEXT("PUT"), CancelUrl, { { TEXT("Authorization"), TEXT("Bearer ") + Key } }, [](const ComfyHttp::FResponse&) {});
		};
		This->RouterPoll(Job, Profile, Key, StatusUrl, ResponseUrl, FPlatformTime::Seconds());
	});
	return Job;
}

void UComfySubsystem::RouterPoll(UComfyJob* Job, FComfyBackendProfile Profile, FString ApiKey, FString StatusUrl, FString ResponseUrl, double StartSeconds)
{
	if (Job->IsFinished())
	{
		return;
	}
	if (FPlatformTime::Seconds() - StartSeconds > GetDefault<UComfySettings>()->JobTimeoutMinutes * 60.0)
	{
		Job->Fail(TEXT("Timed out waiting for Comfy Router."));
		return;
	}
	TWeakObjectPtr<UComfyJob> WeakJob = Job;
	TWeakObjectPtr<UComfySubsystem> WeakThis = this;
	ComfyHttp::Send(TEXT("GET"), StatusUrl, { { TEXT("Authorization"), TEXT("Bearer ") + ApiKey } }, [WeakJob, WeakThis, Profile, ApiKey, StatusUrl, ResponseUrl, StartSeconds](const ComfyHttp::FResponse& Response)
	{
		UComfyJob* Job = WeakJob.Get();
		UComfySubsystem* This = WeakThis.Get();
		if (!Job || !This || Job->IsFinished())
		{
			return;
		}
		const float Wait = Response.RetryAfter(2.f);
		if (Response.IsOk() && Response.Json.IsValid())
		{
			const FString Status = Response.Json->GetStringField(TEXT("status"));
			FComfyProgress Progress = Job->Progress;
			double Position = -1;
			if (Response.Json->TryGetNumberField(TEXT("queue_position"), Position))
			{
				Progress.QueuePosition = int32(Position);
			}
			Progress.Message = Status == TEXT("IN_QUEUE") ? FString::Printf(TEXT("In queue (%d)"), Progress.QueuePosition) : Status == TEXT("IN_PROGRESS") ? FString(TEXT("Generating")) : Status;
			Job->SetProgress(Progress);
			if (Status == TEXT("COMPLETED"))
			{
				This->RouterCollect(Job, Profile, ApiKey, ResponseUrl);
				return;
			}
			Job->SetState(Status == TEXT("IN_PROGRESS") ? EComfyJobState::Running : EComfyJobState::Queued);
		}
		else if (Response.bConnected && Response.Code >= 400 && Response.Code < 500 && Response.Code != 429)
		{
			Job->Fail(Response.Describe(TEXT("Checking the Router request")));
			return;
		}
		ComfyHttp::Delay(Wait, [WeakJob, WeakThis, Profile, ApiKey, StatusUrl, ResponseUrl, StartSeconds]()
		{
			if (WeakJob.IsValid() && WeakThis.IsValid())
			{
				WeakThis->RouterPoll(WeakJob.Get(), Profile, ApiKey, StatusUrl, ResponseUrl, StartSeconds);
			}
		});
	});
}

void UComfySubsystem::RouterCollect(UComfyJob* Job, FComfyBackendProfile Profile, FString ApiKey, FString ResponseUrl)
{
	using namespace ComfyRouterPrivate;

	TWeakObjectPtr<UComfyJob> WeakJob = Job;
	ComfyHttp::Send(TEXT("GET"), ResponseUrl, { { TEXT("Authorization"), TEXT("Bearer ") + ApiKey } }, [WeakJob, Profile, ApiKey](const ComfyHttp::FResponse& Response)
	{
		UComfyJob* Job = WeakJob.Get();
		if (!Job || Job->IsFinished())
		{
			return;
		}
		if (!Response.IsOk())
		{
			Job->Fail(Response.Describe(TEXT("Collecting the Router result")));
			return;
		}
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*Job->GetOutputDirectory());

		// Some models answer raw bytes instead of JSON.
		if (!Response.Json.IsValid())
		{
			FComfyOutputFile File;
			File.Name = TEXT("result");
			File.FileName = FString::Printf(TEXT("result.%s"), *ExtensionFromContentType(Response.ContentType));
			File.Kind = ComfyTypes::KindFromFileName(File.FileName);
			File.LocalPath = Job->GetOutputDirectory() / File.FileName;
			FFileHelper::SaveArrayToFile(Response.Bytes, *File.LocalPath);
			Job->Outputs.Add(File);
			Job->Complete();
			return;
		}
		FString ErrorType;
		if (Response.Json->TryGetStringField(TEXT("error_type"), ErrorType))
		{
			Job->Fail(Response.Describe(TEXT("The Router request")));
			return;
		}
		Job->ResultJson = Response.Body;
		FFileHelper::SaveStringToFile(Response.Body, *(Job->GetOutputDirectory() / TEXT("result.json")));

		TArray<TPair<FString, FString>> Urls;
		CollectUrls(MakeShared<FJsonValueObject>(Response.Json), FString(), Urls);
		if (Urls.Num() == 0)
		{
			Job->Complete();
			return;
		}
		Job->SetState(EComfyJobState::Downloading);

		struct FPending
		{
			int32 Remaining = 0;
			bool bFailed = false;
		};
		TSharedRef<FPending> Pending = MakeShared<FPending>();
		Pending->Remaining = Urls.Num();
		for (int32 i = 0; i < Urls.Num(); ++i)
		{
			const FString& Url = Urls[i].Value;
			FComfyOutputFile File;
			File.Name = Urls[i].Key;
			File.RemoteRef = Url;
			if (Url.StartsWith(TEXT("data:")))
			{
				int32 Comma = INDEX_NONE;
				Url.FindChar(TEXT(','), Comma);
				const FString Header = Url.Mid(5, Comma - 5);
				File.FileName = FString::Printf(TEXT("output_%d.%s"), i, *ExtensionFromContentType(Header));
				File.Kind = ComfyTypes::KindFromFileName(File.FileName);
				File.LocalPath = Job->GetOutputDirectory() / File.FileName;
				TArray<uint8> Bytes;
				FBase64::Decode(Url.Mid(Comma + 1), Bytes);
				FFileHelper::SaveArrayToFile(Bytes, *File.LocalPath);
				Job->Outputs.Add(File);
				if (--Pending->Remaining == 0)
				{
					Job->Complete();
				}
				continue;
			}
			FString PathPart = Url;
			int32 Query = INDEX_NONE;
			if (PathPart.FindChar(TEXT('?'), Query))
			{
				PathPart.LeftInline(Query);
			}
			File.FileName = FString::Printf(TEXT("output_%d_%s"), i, *FPaths::GetCleanFilename(PathPart));
			File.Kind = ComfyTypes::KindFromFileName(File.FileName);
			File.LocalPath = Job->GetOutputDirectory() / File.FileName;
			Job->Outputs.Add(File);
			const bool bSameOrigin = ComfyHttp::Origin(Url) == ComfyHttp::Origin(Profile.BaseUrl);
			ComfyHttp::Download(Url, bSameOrigin ? ComfyHttp::FHeaders{ { TEXT("Authorization"), TEXT("Bearer ") + ApiKey } } : ComfyHttp::FHeaders(), File.LocalPath, [WeakJob, Pending](bool bOk, const FString& Error)
			{
				UComfyJob* Job = WeakJob.Get();
				if (!Job || Pending->bFailed)
				{
					return;
				}
				if (!bOk)
				{
					Pending->bFailed = true;
					Job->Fail(Error);
					return;
				}
				if (--Pending->Remaining == 0)
				{
					Job->Complete();
				}
			});
		}
	}, 120.f);
}

bool UComfySubsystem::FindRouterProfile(FName Backend, FComfyBackendProfile& OutProfile) const
{
	FName Name = Backend;
	if (Name.IsNone())
	{
		for (const FComfyBackendProfile& Candidate : GetDefault<UComfySettings>()->Backends)
		{
			if (Candidate.Kind == EComfyBackendKind::Router)
			{
				Name = Candidate.Name;
				break;
			}
		}
	}
	return GetBackendProfile(Name, OutProfile) && OutProfile.Kind == EComfyBackendKind::Router;
}

void UComfySubsystem::GetRouterModelSchema(const FString& ModelId, FComfyTextResultDelegate OnResult, FName Backend)
{
	GetRouterModelSchemaNative(ModelId, [OnResult](bool bOk, const FString& Text) { OnResult.ExecuteIfBound(bOk, Text); }, Backend);
}

void UComfySubsystem::GetRouterModelSchemaNative(const FString& ModelId, TFunction<void(bool, const FString&)> OnResult, FName Backend)
{
	FComfyBackendProfile Profile;
	if (!FindRouterProfile(Backend, Profile))
	{
		OnResult(false, TEXT("No Comfy Router backend configured."));
		return;
	}
	ComfyHttp::Send(TEXT("GET"), ComfyRouterPrivate::ModelPath(Profile, ModelId) + TEXT("/openapi.json"), { { TEXT("Authorization"), TEXT("Bearer ") + ResolveApiKey(Profile) } }, [OnResult, ModelId](const ComfyHttp::FResponse& Response)
	{
		OnResult(Response.IsOk(), Response.IsOk() ? Response.Body : Response.Describe(FString::Printf(TEXT("Schema for %s"), *ModelId)));
	});
}

void UComfySubsystem::ListRouterModels(const FString& Cursor, FComfyTextResultDelegate OnResult, FName Backend)
{
	ListRouterModelsNative(Cursor, [OnResult](bool bOk, const FString& Text) { OnResult.ExecuteIfBound(bOk, Text); }, Backend);
}

void UComfySubsystem::ListRouterModelsNative(const FString& Cursor, TFunction<void(bool, const FString&)> OnResult, FName Backend)
{
	FComfyBackendProfile Profile;
	if (!FindRouterProfile(Backend, Profile))
	{
		OnResult(false, TEXT("No Comfy Router backend configured."));
		return;
	}
	FString Url = Profile.BaseUrl + TEXT("/v2/models?limit=100");
	if (!Cursor.IsEmpty())
	{
		Url += TEXT("&cursor=") + ComfyHttp::UrlEncode(Cursor);
	}
	ComfyHttp::Send(TEXT("GET"), Url, { { TEXT("Authorization"), TEXT("Bearer ") + ResolveApiKey(Profile) } }, [OnResult](const ComfyHttp::FResponse& Response)
	{
		OnResult(Response.IsOk(), Response.IsOk() ? Response.Body : Response.Describe(TEXT("Listing Router models")));
	});
}

TArray<UComfyWorkflow*> UComfySubsystem::GetBuiltInWorkflows(bool bReload)
{
	if (!bBuiltInsLoaded || bReload)
	{
		bBuiltInsLoaded = true;
		BuiltInWorkflows.Reset();

		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ComfyUnreal"));
		if (!Plugin.IsValid())
		{
			return {};
		}
		const FString Folder = Plugin->GetBaseDir() / TEXT("Resources") / TEXT("Workflows");
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.json")), true, false);
		Files.Sort();

		for (const FString& File : Files)
		{
			FString Json;
			FString Error;
			if (!FFileHelper::LoadFileToString(Json, *(Folder / File)))
			{
				continue;
			}
			// Keep the file's name so agents can find a built-in by it; only a reload needs a unique one.
			const FName BaseName(*FPaths::GetBaseFilename(File));
			const FName ObjectName = StaticFindObjectFast(nullptr, this, BaseName) ? MakeUniqueObjectName(this, UComfyWorkflow::StaticClass(), BaseName) : BaseName;
			UComfyWorkflow* Workflow = NewObject<UComfyWorkflow>(this, ObjectName, RF_Transient);
			if (!Workflow->SetGraphFromJson(Json, true, Error))
			{
				UE_LOG(LogComfy, Warning, TEXT("Skipped built-in workflow %s: %s"), *File, *Error);
				continue;
			}
			Workflow->SourceFile = Folder / File;
			if (Workflow->DisplayName.IsEmpty())
			{
				Workflow->DisplayName = FText::FromString(FName::NameToDisplayString(FPaths::GetBaseFilename(File), false));
			}
			BuiltInWorkflows.Add(Workflow);
		}
	}
	return TArray<UComfyWorkflow*>(BuiltInWorkflows);
}
