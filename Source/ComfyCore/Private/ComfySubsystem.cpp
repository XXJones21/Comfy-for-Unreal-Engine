// Copyright Comfy Org. All Rights Reserved.

#include "ComfySubsystem.h"
#include "ComfyInputsLibrary.h"
#include "ComfyCoreModule.h"
#include "ComfyJob.h"
#include "ComfySettings.h"
#include "ComfyWorkflow.h"
#include "ComfyLocalDetect.h"
#include "ComfyPageChannel.h"
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
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"

UComfySubsystem* UComfySubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UComfySubsystem>() : nullptr;
}

void UComfySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadJobHistory();
}

void UComfySubsystem::LoadJobHistory()
{
	// The newest runs only: enough to browse, without loading a long project's whole history.
	static constexpr int32 MaxHistory = 200;
	const FString JobsDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Comfy") / TEXT("Jobs"));
	TArray<FString> Folders;
	IFileManager::Get().FindFiles(Folders, *(JobsDir / TEXT("*")), false, true);
	// Job ids start with their start time, so names sort by age.
	Folders.Sort();
	if (Folders.Num() > MaxHistory)
	{
		Folders.RemoveAt(0, Folders.Num() - MaxHistory);
	}
	TArray<TObjectPtr<UComfyJob>> History;
	for (const FString& Folder : Folders)
	{
		if (UComfyJob* Job = UComfyJob::LoadRecord(this, JobsDir / Folder))
		{
			History.Add(Job);
		}
	}
	History.Append(Jobs);
	Jobs = MoveTemp(History);
	UE_LOG(LogComfy, Log, TEXT("Read %d earlier run(s) from %s."), Jobs.Num(), *JobsDir);
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
		// A local ComfyUI found on another port stands in for the configured address.
		if (const FString* Resolved = ResolvedUrls.Find(Found->Name))
		{
			OutProfile.BaseUrl = *Resolved;
		}
		return true;
	}
	return false;
}

void UComfySubsystem::ResolveLocalProfile(const FComfyBackendProfile& Configured, bool bSearch, TFunction<void(const FComfyBackendProfile&, const FString&, EComfyDesktopInstall)> Done)
{
	if (Configured.Kind != EComfyBackendKind::Local || !Configured.bFindLocalComfy)
	{
		Done(Configured, FString(), EComfyDesktopInstall::Unknown);
		return;
	}
	const FString* Remembered = ResolvedUrls.Find(Configured.Name);
	if (!bSearch && Remembered)
	{
		FComfyBackendProfile Resolved = Configured;
		Resolved.BaseUrl = *Remembered;
		Done(Resolved, FString(), EComfyDesktopInstall::Unknown);
		return;
	}

	const ComfyLocalDetect::FDesktopInfo Desktop = ComfyLocalDetect::ReadDesktop();
	const TArray<FString> Urls = ComfyLocalDetect::Candidates(Remembered ? *Remembered : FString(), Configured.BaseUrl, Desktop);
	TWeakObjectPtr<UComfySubsystem> WeakThis = this;
	ComfyLocalDetect::FindFirst(Urls, [WeakThis, Configured, Desktop, Urls, Done = MoveTemp(Done)](const FString& Found)
	{
		UComfySubsystem* This = WeakThis.Get();
		FComfyBackendProfile Resolved = Configured;
		Resolved.BaseUrl.RemoveFromEnd(TEXT("/"));
		if (Found.IsEmpty())
		{
			if (This)
			{
				This->ResolvedUrls.Remove(Configured.Name);
			}
			Done(Resolved, ComfyLocalDetect::DescribeNotFound(Desktop, Urls), Desktop.Install);
			return;
		}
		if (This)
		{
			This->ResolvedUrls.Add(Configured.Name, Found);
			if (Found != Resolved.BaseUrl)
			{
				UE_LOG(LogComfy, Log, TEXT("%s: found ComfyUI at %s"), *Configured.Name.ToString(), *Found);
			}
		}
		Resolved.BaseUrl = Found;
		Done(Resolved, FString(), Desktop.Install);
	});
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

EComfyApiKeySource UComfySubsystem::GetApiKeySource(const FComfyBackendProfile& Profile, FString& OutName) const
{
	// The same order as ResolveApiKey.
	OutName.Reset();
	if (!Profile.ApiKeyEnvironmentVariable.IsEmpty() && !FPlatformMisc::GetEnvironmentVariable(*Profile.ApiKeyEnvironmentVariable).IsEmpty())
	{
		OutName = Profile.ApiKeyEnvironmentVariable;
		return EComfyApiKeySource::Environment;
	}
	if (!FPlatformMisc::GetEnvironmentVariable(TEXT("COMFY_CLOUD_API_KEY")).IsEmpty())
	{
		OutName = TEXT("COMFY_CLOUD_API_KEY");
		return EComfyApiKeySource::Environment;
	}
	if (!SessionApiKey.IsEmpty())
	{
		return EComfyApiKeySource::Session;
	}
	return GetDefault<UComfyUserSettings>()->ApiKey.IsEmpty() ? EComfyApiKeySource::None : EComfyApiKeySource::EditorPreferences;
}

bool UComfySubsystem::IsApiKeyRefused(const FComfyBackendProfile& Profile) const
{
	const FString* Refused = RefusedKeyHashes.Find(Profile.Name);
	const FString Key = Refused ? ResolveApiKey(Profile) : FString();
	return Refused && !Key.IsEmpty() && *Refused == FMD5::HashAnsiString(*Key);
}

TSharedPtr<IComfyBackend> UComfySubsystem::GetOrCreateBackend(const FComfyBackendProfile& Profile)
{
	const FString Key = ResolveApiKey(Profile);
	// Comfy Cloud without a usable key runs through the signed-in Comfy Studio page.
	const bool bViaPage = Profile.Kind == EComfyBackendKind::Cloud && (Key.IsEmpty() || IsApiKeyRefused(Profile)) && PageChannel.IsValid();
	const FString Fingerprint = FString::Printf(TEXT("%d|%s|%d|%d|%s|%d"), int32(Profile.Kind), *Profile.BaseUrl, int32(Profile.LocalProtocol), Profile.bSendPartnerNodeKey ? 1 : 0, *FMD5::HashAnsiString(*Key), bViaPage ? 1 : 0);
	if (TSharedPtr<IComfyBackend>* Existing = BackendInstances.Find(Profile.Name))
	{
		if (BackendFingerprints.FindRef(Profile.Name) == Fingerprint)
		{
			return *Existing;
		}
	}
	TSharedPtr<IComfyBackend> Backend;
	if (bViaPage)
	{
		Backend = MakeShared<FComfyPageBackend>(Profile, PageChannel.ToSharedRef());
	}
	else if (Profile.UsesApiV2())
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
	auto Finish = [WeakThis, OnStatus](const FComfyBackendStatus& Raw)
	{
		FComfyBackendStatus Status = Raw;
		if (UComfySubsystem* This = WeakThis.Get())
		{
			FComfyBackendProfile Checked;
			if (Raw.bApiKeyRefused && This->GetBackendProfile(Raw.Backend, Checked) && !This->IsApiKeyRefused(Checked))
			{
				// Remember the refusal, so this key isn't used again; a different key is.
				This->RefusedKeyHashes.Add(Raw.Backend, FMD5::HashAnsiString(*This->ResolveApiKey(Checked)));
				This->RefusedKeyDetails.Add(Raw.Backend, Raw.Summary);
				if (Checked.Kind == EComfyBackendKind::Cloud && This->PageChannel.IsValid())
				{
					// Comfy Cloud can still run with the sign-in: check it that way instead.
					This->ProbeBackend(Raw.Backend, OnStatus);
					return;
				}
			}
			This->Statuses.Add(Raw.Backend, Raw);
			Status = This->Decorate(Raw);
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

	if (Profile.Kind == EComfyBackendKind::Local)
	{
		// Probe the configured address and the usual local ports, then talk to whichever answered.
		const FComfyBackendProfile* Configured = GetDefault<UComfySettings>()->FindBackend(Profile.Name);
		ResolveLocalProfile(Configured ? *Configured : Profile, true, [WeakThis, Finish](const FComfyBackendProfile& Resolved, const FString& NotFound, EComfyDesktopInstall Desktop)
		{
			UComfySubsystem* This = WeakThis.Get();
			if (!This)
			{
				return;
			}
			if (!NotFound.IsEmpty())
			{
				FComfyBackendStatus Status;
				Status.Backend = Resolved.Name;
				Status.Url = Resolved.BaseUrl;
				Status.Summary = NotFound;
				Status.DesktopInstall = Desktop;
				Finish(Status);
				return;
			}
			This->GetOrCreateBackend(Resolved)->Probe([Finish, Resolved, Desktop](const FComfyBackendStatus& Probed)
			{
				FComfyBackendStatus Status = Probed;
				Status.Url = Resolved.BaseUrl;
				Status.DesktopInstall = Desktop;
				if (Status.bReachable)
				{
					Status.Summary = FString::Printf(TEXT("%s  ·  %s"), *ComfyLocalDetect::HostPort(Resolved.BaseUrl), *Status.Summary);
				}
				Finish(Status);
			});
		});
		return;
	}

	GetOrCreateBackend(Profile)->Probe([Finish, Url = Profile.BaseUrl](const FComfyBackendStatus& Probed)
	{
		FComfyBackendStatus Status = Probed;
		Status.Url = Url;
		Finish(Status);
	});
}

FComfyBackendStatus UComfySubsystem::Decorate(const FComfyBackendStatus& Raw) const
{
	FComfyBackendProfile Profile;
	if (!GetBackendProfile(Raw.Backend, Profile) || Profile.Kind != EComfyBackendKind::Cloud)
	{
		return Raw;
	}
	FComfyBackendStatus Status = Raw;
	Status.KeySource = GetApiKeySource(Profile, Status.KeySourceName);
	Status.bApiKeyRefused = IsApiKeyRefused(Profile);
	if (Status.bApiKeyRefused)
	{
		Status.Detail = RefusedKeyDetails.FindRef(Profile.Name);
	}
	else if (!ResolveApiKey(Profile).IsEmpty())
	{
		// The key does the work; the signed-in page, if any, still knows the free runs and credits.
		if (CloudAccount.bSignedIn)
		{
			Status.Summary += TEXT("  ·  ") + CloudAccount.Describe();
		}
		return Status;
	}
	if (!CloudAccount.bSignedIn)
	{
		Status.bReachable = false;
		Status.bNeedsSignIn = true;
		if (Status.bApiKeyRefused)
		{
			const FString From = Status.KeySource == EComfyApiKeySource::Environment ? FString::Printf(TEXT("the %s environment variable"), *Status.KeySourceName)
				: Status.KeySource == EComfyApiKeySource::Session ? FString(TEXT("this session")) : FString(TEXT("Editor Preferences"));
			Status.Summary = FString::Printf(TEXT("Comfy Cloud refused the API key from %s. Sign in to Comfy Cloud instead, or replace the key."), *From);
		}
		else
		{
			Status.Summary = TEXT("Not signed in. Sign in to Comfy Cloud, or add an API key in Editor Preferences > Plugins > Comfy.");
		}
		return Status;
	}
	Status.bNeedsSignIn = false;
	Status.Summary = CloudAccount.Describe();
	if (!Raw.bReachable)
	{
		// Signed in moments ago: the check that follows hasn't finished yet.
		Status.Summary += Raw.bNeedsSignIn ? FString(TEXT("  ·  Checking...")) : TEXT("  ·  ") + Raw.Summary;
	}
	return Status;
}

void UComfySubsystem::SetCloudAccount(const FComfyCloudAccount& Account)
{
	const bool bSignInChanged = Account.bSignedIn != CloudAccount.bSignedIn || Account.Email != CloudAccount.Email;
	const bool bChanged = Account != CloudAccount;
	CloudAccount = Account;
	if (!bChanged)
	{
		return;
	}
	OnCloudAccountChanged.Broadcast(CloudAccount);
	if (bSignInChanged)
	{
		// Signing in or out changes what a key-less Cloud backend can do: check it again, which also reads its models.
		for (const FComfyBackendProfile& Profile : GetDefault<UComfySettings>()->Backends)
		{
			if (Profile.Kind == EComfyBackendKind::Cloud && (ResolveApiKey(Profile).IsEmpty() || IsApiKeyRefused(Profile)))
			{
				ProbeBackend(Profile.Name, FComfyBackendStatusDelegate());
			}
		}
	}
	BroadcastCloudStatuses();
}

void UComfySubsystem::BroadcastCloudStatuses()
{
	for (const FComfyBackendProfile& Profile : GetDefault<UComfySettings>()->Backends)
	{
		if (Profile.Kind == EComfyBackendKind::Cloud)
		{
			if (const FComfyBackendStatus* Raw = Statuses.Find(Profile.Name))
			{
				OnBackendStatus.Broadcast(Decorate(*Raw));
			}
		}
	}
}

void UComfySubsystem::RefreshCloudAccount()
{
	OnCloudAccountRefreshRequested.Broadcast();
}

void UComfySubsystem::SetPageChannel(TSharedPtr<IComfyPageChannel> Channel)
{
	PageChannel = Channel;
	// Key-less Cloud backends run through the page, so they're rebuilt around the new one.
	for (const FComfyBackendProfile& Profile : GetDefault<UComfySettings>()->Backends)
	{
		if (Profile.Kind == EComfyBackendKind::Cloud)
		{
			BackendInstances.Remove(Profile.Name);
			BackendFingerprints.Remove(Profile.Name);
		}
	}
}

FComfyBackendStatus UComfySubsystem::GetLastStatus(FName Backend) const
{
	if (const FComfyBackendStatus* Found = Statuses.Find(Backend.IsNone() ? GetActiveBackend() : Backend))
	{
		return Decorate(*Found);
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
	const TArray<FString> Options = GetInputOptions(Backend, Workflow, InputName);
	if (Input.Prefer.Num() > 0 && Options.Num() > 0)
	{
		// A workflow that names its models shouldn't quietly run with some other file.
		return ComfyTypes::MatchModel(Options, Input.Prefer);
	}
	return ComfyTypes::PickModel(Options, Input.Prefer, Input.DefaultValue);
}

TArray<FComfyWorkflowInput> UComfySubsystem::GetMissingModels(FName Backend, const UComfyWorkflow* Workflow, const TMap<FString, FString>& Inputs) const
{
	TArray<FComfyWorkflowInput> Missing;
	if (!Workflow)
	{
		return Missing;
	}
	for (const FComfyWorkflowInput& Input : Workflow->Inputs)
	{
		if (Input.Type != EComfyInputType::Model)
		{
			continue;
		}
		const TArray<FString> Options = GetInputOptions(Backend, Workflow, Input.Name);
		if (Options.Num() == 0)
		{
			continue;
		}
		const FString* Given = Inputs.Find(Input.Name);
		const FString Value = Given && !Given->IsEmpty() ? *Given : GetDefaultInputValue(Backend, Workflow, Input.Name);
		if (Value.IsEmpty() || !Options.Contains(Value))
		{
			Missing.Add(Input);
		}
	}
	return Missing;
}

FString UComfySubsystem::DescribeMissingModels(FName Backend, const TArray<FComfyWorkflowInput>& Missing)
{
	TArray<FString> Lines;
	for (const FComfyWorkflowInput& Input : Missing)
	{
		const FString File = !Input.Download.File.IsEmpty() ? Input.Download.File : Input.Prefer.Num() > 0 ? Input.Prefer[0] : Input.DefaultValue;
		FString Line = FString::Printf(TEXT("%s (%s)"), *File, *Input.Label);
		if (!Input.Download.Folder.IsEmpty())
		{
			Line += FString::Printf(TEXT(", %sin ComfyUI/models/%s"), Input.Download.Size.IsEmpty() ? TEXT("") : *(Input.Download.Size + TEXT(", ")), *Input.Download.Folder);
		}
		if (!Input.Download.Url.IsEmpty())
		{
			Line += FString::Printf(TEXT(": %s"), *Input.Download.Url);
		}
		Lines.Add(Line);
	}
	return FString::Printf(TEXT("%s doesn't have the models this workflow needs:\n%s"), *Backend.ToString(), *FString::Join(Lines, TEXT("\n")));
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
	// Cloud runs spend free runs or credits, so the account shown in Comfy Studio is read again.
	TWeakObjectPtr<UComfySubsystem> WeakThis = this;
	Job->OnFinishedNative.AddLambda([WeakThis](UComfyJob* Finished)
	{
		if (Finished)
		{
			// Every run joins the project's history, kept beside its outputs.
			Finished->SaveRecord();
		}
		if (Finished && Finished->BackendKind == EComfyBackendKind::Cloud && WeakThis.IsValid())
		{
			WeakThis->RefreshCloudAccount();
		}
	});
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

UComfyJob* UComfySubsystem::RunWorkflowWithInputs(UComfyWorkflow* Workflow, const FComfyInputs& Inputs, FName Backend)
{
	if (!Workflow)
	{
		Workflow = Inputs.Workflow;
	}
	TMap<FString, FString> Values;
	FString Error;
	if (!UComfyInputsLibrary::ResolveComfyInputs(Workflow, Inputs, Values, Error))
	{
		UComfyJob* Job = NewJob(Backend.IsNone() ? GetActiveBackend() : Backend, Workflow ? Workflow->GetDisplayNameString() : TEXT("(no workflow)"));
		Job->Workflow = Workflow;
		Job->Fail(Error);
		return Job;
	}
	return RunWorkflow(Workflow, Values, Backend);
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
	const TArray<FComfyWorkflowInput> MissingModels = GetMissingModels(BackendName, Workflow, Job->Inputs);
	if (MissingModels.Num() > 0)
	{
		Job->Fail(DescribeMissingModels(BackendName, MissingModels));
		return Job;
	}
	for (const FComfyWorkflowInput& Input : Workflow->Inputs)
	{
		const FString* Value = Inputs.Find(Input.Name);
		if (Input.Type == EComfyInputType::File && Value && !Value->IsEmpty() && !FPaths::FileExists(*Value))
		{
			Job->Fail(FString::Printf(TEXT("Input \"%s\": file not found: %s"), *Input.Name, **Value));
			return Job;
		}
	}

	if (Profile.Kind == EComfyBackendKind::Local && !ResolvedUrls.Contains(Profile.Name))
	{
		// Nothing has found this ComfyUI yet (a script or agent ran before Comfy Studio checked), so look for it first.
		TWeakObjectPtr<UComfyJob> WeakJob = Job;
		TWeakObjectPtr<UComfySubsystem> WeakThis = this;
		ResolveLocalProfile(Profile, false, [WeakJob, WeakThis](const FComfyBackendProfile& Resolved, const FString& NotFound, EComfyDesktopInstall)
		{
			UComfyJob* Job = WeakJob.Get();
			UComfySubsystem* This = WeakThis.Get();
			if (!Job || !This || Job->IsFinished())
			{
				return;
			}
			if (!NotFound.IsEmpty())
			{
				Job->Fail(NotFound);
				return;
			}
			This->StartJob(Job, Resolved);
		});
		return Job;
	}
	StartJob(Job, Profile);
	return Job;
}

void UComfySubsystem::StartJob(UComfyJob* Job, const FComfyBackendProfile& Profile)
{
	TSharedPtr<IComfyBackend> BackendImpl = GetOrCreateBackend(Profile);
	Job->BackendImpl = BackendImpl;

	// Files to upload first: File inputs given a path on disk.
	TArray<TPair<FString, FString>> PendingFiles;
	for (const FComfyWorkflowInput& Input : Job->Workflow->Inputs)
	{
		const FString* Value = Job->Inputs.Find(Input.Name);
		if (Input.Type == EComfyInputType::File && Value && !Value->IsEmpty())
		{
			PendingFiles.Emplace(Input.Name, *Value);
		}
	}
	if (PendingFiles.Num() > 0)
	{
		Job->SetState(EComfyJobState::Uploading);
	}
	UploadInputs(Job, BackendImpl, MoveTemp(PendingFiles), {});
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

FName UComfySubsystem::GetBackendForTarget(EComfyBackendTarget Target) const
{
	EComfyBackendKind Kind;
	switch (Target)
	{
	case EComfyBackendTarget::Desktop: Kind = EComfyBackendKind::Local; break;
	case EComfyBackendTarget::Cloud: Kind = EComfyBackendKind::Cloud; break;
	case EComfyBackendTarget::Router: Kind = EComfyBackendKind::Router; break;
	default: return NAME_None;
	}

	FComfyBackendProfile Profile;
	const FName Active = GetActiveBackend();
	if (GetBackendProfile(Active, Profile) && Profile.Kind == Kind)
	{
		return Active;
	}
	const UComfySettings* Settings = GetDefault<UComfySettings>();
	if (GetBackendProfile(Settings->DefaultBackend, Profile) && Profile.Kind == Kind)
	{
		return Settings->DefaultBackend;
	}
	for (const FComfyBackendProfile& Candidate : Settings->Backends)
	{
		if (Candidate.Kind == Kind)
		{
			return Candidate.Name;
		}
	}
	return FName(*FString::Printf(TEXT("%s (none set up in Comfy settings)"), *StaticEnum<EComfyBackendTarget>()->GetDisplayNameTextByValue(static_cast<int64>(Target)).ToString()));
}

EComfyBackendTarget UComfySubsystem::GetTargetForBackend(FName Backend)
{
	const FComfyBackendProfile* Profile = Backend.IsNone() ? nullptr : GetDefault<UComfySettings>()->FindBackend(Backend);
	if (!Profile)
	{
		return EComfyBackendTarget::Active;
	}
	switch (Profile->Kind)
	{
	case EComfyBackendKind::Local: return EComfyBackendTarget::Desktop;
	case EComfyBackendKind::Cloud: return EComfyBackendTarget::Cloud;
	case EComfyBackendKind::Router: return EComfyBackendTarget::Router;
	default: return EComfyBackendTarget::Active;
	}
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
			// The editor saves each built-in as an asset, so Blueprints can reference it and a game cooks it; prefer that.
			const FString AssetName = FPaths::GetBaseFilename(File);
			const FString PackageName = FString(UComfyWorkflow::BuiltInAssetPath) / AssetName;
			if (FPackageName::DoesPackageExist(PackageName))
			{
				if (UComfyWorkflow* Asset = LoadObject<UComfyWorkflow>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet))
				{
					BuiltInWorkflows.Add(Asset);
					continue;
				}
			}

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

// ---------------------------------------------------------------------------------------------
// Comfy for Unreal extension: the server's workflow library and the editor heartbeat

namespace ComfyServerLibraryPrivate
{
	/** The extension's routes live on a ComfyUI server, so only local backends can have them. */
	static bool ServerBase(const UComfySubsystem* Comfy, FName Backend, FString& OutBase)
	{
		FComfyBackendProfile Profile;
		if (!Comfy->GetBackendProfile(Backend, Profile) || Profile.Kind != EComfyBackendKind::Local)
		{
			return false;
		}
		OutBase = Profile.BaseUrl;
		OutBase.RemoveFromEnd(TEXT("/"));
		return true;
	}
}

void UComfySubsystem::RefreshServerWorkflows(FName Backend)
{
	FString Base;
	if (!ComfyServerLibraryPrivate::ServerBase(this, Backend, Base))
	{
		if (ServerWorkflows.Num() > 0)
		{
			ServerWorkflows.Reset();
			OnServerWorkflowsChanged.Broadcast();
		}
		return;
	}
	TWeakObjectPtr<UComfySubsystem> WeakThis = this;
	ComfyHttp::Send(TEXT("GET"), Base + TEXT("/comfy_unreal/workflows"), {}, [WeakThis, Base](const ComfyHttp::FResponse& Response)
	{
		UComfySubsystem* This = WeakThis.Get();
		if (!This)
		{
			return;
		}
		TArray<TSharedPtr<FJsonValue>> Items;
		if (Response.IsOk())
		{
			TSharedPtr<FJsonValue> Parsed;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response.Body);
			if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid() && Parsed->Type == EJson::Array)
			{
				Items = Parsed->AsArray();
			}
		}
		This->ServerWorkflows.Reset();
		if (Items.Num() == 0)
		{
			This->OnServerWorkflowsChanged.Broadcast();
			return;
		}
		// Fetch each one; the list only carries summaries.
		TSharedRef<int32> Remaining = MakeShared<int32>(Items.Num());
		for (const TSharedPtr<FJsonValue>& Item : Items)
		{
			FString Id;
			const TSharedPtr<FJsonObject> Summary = Item.IsValid() ? Item->AsObject() : nullptr;
			if (!Summary.IsValid() || !Summary->TryGetStringField(TEXT("id"), Id))
			{
				--*Remaining;
				continue;
			}
			const FString Url = Base + TEXT("/comfy_unreal/workflows/") + ComfyHttp::UrlEncode(Id);
			ComfyHttp::Send(TEXT("GET"), Url, {}, [WeakThis, Url, Id, Remaining](const ComfyHttp::FResponse& File)
			{
				UComfySubsystem* This = WeakThis.Get();
				if (!This)
				{
					return;
				}
				const TSharedPtr<FJsonObject>* Prompt = nullptr;
				const TSharedPtr<FJsonObject>* Manifest = nullptr;
				if (File.IsOk() && File.Json.IsValid() && File.Json->TryGetObjectField(TEXT("prompt"), Prompt) && File.Json->TryGetObjectField(TEXT("manifest"), Manifest))
				{
					const TSharedRef<FJsonObject> Bundle = MakeShared<FJsonObject>();
					Bundle->SetObjectField(TEXT("comfy_unreal"), *Manifest);
					Bundle->SetObjectField(TEXT("prompt"), *Prompt);
					const FName ObjectName = MakeUniqueObjectName(This, UComfyWorkflow::StaticClass(), FName(*(TEXT("server_") + Id)));
					UComfyWorkflow* Workflow = NewObject<UComfyWorkflow>(This, ObjectName, RF_Transient);
					FString Error;
					if (Workflow->SetGraphFromJson(ComfyHttp::JsonToString(Bundle), true, Error))
					{
						const TSharedPtr<FJsonObject>* Canvas = nullptr;
						if (File.Json->TryGetObjectField(TEXT("workflow"), Canvas))
						{
							Workflow->UiGraphJson = ComfyHttp::JsonToString(Canvas->ToSharedRef());
						}
						Workflow->SourceFile = Url;
						This->ServerWorkflows.Add(Workflow);
					}
					else
					{
						UE_LOG(LogComfy, Warning, TEXT("Skipped server workflow %s: %s"), *Id, *Error);
					}
				}
				if (--*Remaining <= 0)
				{
					This->OnServerWorkflowsChanged.Broadcast();
				}
			}, 30.f);
		}
		if (*Remaining <= 0)
		{
			This->OnServerWorkflowsChanged.Broadcast();
		}
	}, 10.f);
}

TArray<UComfyWorkflow*> UComfySubsystem::GetServerWorkflows() const
{
	return TArray<UComfyWorkflow*>(ServerWorkflows);
}

void UComfySubsystem::SendHeartbeat(FName Backend)
{
	FString Base;
	if (!ComfyServerLibraryPrivate::ServerBase(this, Backend, Base))
	{
		return;
	}
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("client"), TEXT("unreal"));
	Body->SetStringField(TEXT("editor"), FApp::GetProjectName());
	ComfyHttp::SendJson(TEXT("POST"), Base + TEXT("/comfy_unreal/heartbeat"), {}, Body, [](const ComfyHttp::FResponse&) {}, 5.f);
}
