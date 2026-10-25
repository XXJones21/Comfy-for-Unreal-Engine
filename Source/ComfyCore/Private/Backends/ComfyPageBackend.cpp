// Copyright Comfy Org. All Rights Reserved.

#include "ComfyBackend.h"
#include "ComfyCoreModule.h"
#include "ComfyJob.h"
#include "ComfyPageChannel.h"
#include "ComfyWorkflow.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace ComfyPagePrivate
{
	static const TCHAR* NotSignedIn = TEXT("Comfy Cloud needs you to sign in. Open Comfy Studio's Graph view and sign in, or add an API key in Editor Preferences > Plugins > Comfy.");

	static TSharedPtr<FJsonObject> Parse(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object);
		return Object;
	}

	static FString Write(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out));
		return Out;
	}
}

using namespace ComfyPagePrivate;

FComfyPageBackend::FComfyPageBackend(const FComfyBackendProfile& InProfile, const TSharedRef<IComfyPageChannel>& InChannel)
	: IComfyBackend(InProfile, FString())
	, Channel(InChannel)
{
}

void FComfyPageBackend::Probe(TFunction<void(const FComfyBackendStatus&)> Callback)
{
	FComfyBackendStatus Status;
	Status.Backend = Profile.Name;
	const TSharedPtr<IComfyPageChannel> Page = Channel.Pin();
	if (!Page.IsValid() || !Page->IsReady())
	{
		Status.bNeedsSignIn = true;
		Status.Summary = NotSignedIn;
		Callback(Status);
		return;
	}
	// The node catalog gives model choices; Comfy Cloud's is large, so it gets time.
	Page->Call(TEXT("objectInfo"), TEXT("{}"), [Status, Callback](bool bOk, const FString& Result, const FString& Error) mutable
	{
		Status.bReachable = true;
		if (bOk)
		{
			const TSharedPtr<FJsonObject> Info = Parse(Result);
			if (Info.IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Info->Values)
				{
					Status.NodeClasses.Add(Pair.Key);
				}
				ComfyTypes::ReadInputOptions(Info, Status.InputOptions);
			}
			Status.Summary = FString::Printf(TEXT("Comfy Cloud, %d node types"), Status.NodeClasses.Num());
		}
		else
		{
			UE_LOG(LogComfy, Warning, TEXT("Comfy Cloud node catalog unavailable: %s"), *Error);
			Status.Summary = TEXT("Comfy Cloud");
		}
		Callback(Status);
	}, 120.f);
}

void FComfyPageBackend::UploadFile(const FString& LocalPath, FUploadCallback Callback)
{
	const TSharedPtr<IComfyPageChannel> Page = Channel.Pin();
	if (!Page.IsValid() || !Page->IsReady())
	{
		Callback(false, nullptr, NotSignedIn);
		return;
	}
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *LocalPath))
	{
		Callback(false, nullptr, FString::Printf(TEXT("Couldn't read %s."), *LocalPath));
		return;
	}
	const FString FileName = FPaths::GetCleanFilename(LocalPath);
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("name"), FileName);
	Args->SetStringField(TEXT("mime"), ComfyTypes::ContentTypeFromFileName(FileName));
	Args->SetStringField(TEXT("data"), FBase64::Encode(Bytes));
	Page->Call(TEXT("upload"), Write(Args), [FileName, Callback](bool bOk, const FString& Result, const FString& Error)
	{
		const TSharedPtr<FJsonObject> Uploaded = bOk ? Parse(Result) : nullptr;
		if (!Uploaded.IsValid())
		{
			Callback(false, nullptr, FString::Printf(TEXT("Uploading %s to Comfy Cloud failed: %s"), *FileName, *Error));
			return;
		}
		const FString Name = Uploaded->GetStringField(TEXT("name"));
		const FString Subfolder = Uploaded->GetStringField(TEXT("subfolder"));
		Callback(true, MakeShared<FJsonValueString>(Subfolder.IsEmpty() ? Name : Subfolder + TEXT("/") + Name), FString());
	}, 300.f);
}

void FComfyPageBackend::Submit(UComfyJob* Job, const TSharedRef<FJsonObject>& Graph, FResultCallback Callback)
{
	const TSharedPtr<IComfyPageChannel> Page = Channel.Pin();
	if (!Page.IsValid() || !Page->IsReady())
	{
		Callback(false, NotSignedIn);
		return;
	}
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetObjectField(TEXT("prompt"), Graph);
	// The canvas save, when there is one, so the run shows its graph in Comfy Cloud's history.
	if (Job->Workflow && !Job->Workflow->UiGraphJson.IsEmpty())
	{
		if (const TSharedPtr<FJsonObject> Canvas = Parse(Job->Workflow->UiGraphJson))
		{
			Args->SetObjectField(TEXT("workflow"), Canvas);
		}
	}
	TWeakObjectPtr<UComfyJob> WeakJob = Job;
	Page->Call(TEXT("queue"), Write(Args), [WeakJob, Callback](bool bOk, const FString& Result, const FString& Error)
	{
		UComfyJob* Job = WeakJob.Get();
		if (!Job)
		{
			return;
		}
		const TSharedPtr<FJsonObject> Queued = bOk ? Parse(Result) : nullptr;
		FString PromptId;
		if (!Queued.IsValid() || !Queued->TryGetStringField(TEXT("prompt_id"), PromptId))
		{
			Callback(false, FString::Printf(TEXT("Comfy Cloud didn't accept the workflow: %s"), *Error));
			return;
		}
		Job->RemoteId = PromptId;
		Callback(true, FString());
	}, 60.f);
}

void FComfyPageBackend::Poll(UComfyJob* Job, FPollCallback Callback)
{
	const TSharedPtr<IComfyPageChannel> Page = Channel.Pin();
	if (!Page.IsValid())
	{
		Callback(EComfyPollResult::Failed, TEXT("Comfy Studio was closed while the job ran on Comfy Cloud. The job keeps running there; see its history in the Graph view."), 0.f);
		return;
	}
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("prompt_id"), Job->RemoteId);
	TWeakObjectPtr<UComfyJob> WeakJob = Job;
	Page->Call(TEXT("status"), Write(Args), [WeakJob, Callback](bool bOk, const FString& Result, const FString& Error)
	{
		UComfyJob* Job = WeakJob.Get();
		if (!Job)
		{
			return;
		}
		const TSharedPtr<FJsonObject> Detail = bOk ? Parse(Result) : nullptr;
		if (!Detail.IsValid())
		{
			// The page may be reloading; keep trying until the job timeout.
			Callback(EComfyPollResult::Pending, Error, 3.f);
			return;
		}

		// Progress from the page's live events.
		const TSharedPtr<FJsonObject>* Live = nullptr;
		if (Detail->TryGetObjectField(TEXT("progress"), Live) && Live)
		{
			FComfyProgress Progress = Job->Progress;
			const double Value = (*Live)->GetNumberField(TEXT("value"));
			const double Max = (*Live)->GetNumberField(TEXT("max"));
			(*Live)->TryGetStringField(TEXT("node"), Progress.CurrentNode);
			if (Max > 0)
			{
				Progress.Fraction = float(Value / Max);
				Progress.Message = FString::Printf(TEXT("Node %s: %d/%d"), *Progress.CurrentNode, int32(Value), int32(Max));
			}
			Job->SetProgress(Progress);
		}

		// Comfy Cloud's jobs API: pending, in_progress, completed, failed, cancelled.
		const FString State = Detail->GetStringField(TEXT("status"));
		if (State == TEXT("pending") || State.IsEmpty())
		{
			if (Job->State == EComfyJobState::Pending)
			{
				Job->SetState(EComfyJobState::Queued);
			}
			Callback(EComfyPollResult::Pending, FString(), 1.f);
			return;
		}
		if (State == TEXT("in_progress"))
		{
			Job->SetState(EComfyJobState::Running);
			Callback(EComfyPollResult::Pending, FString(), 1.f);
			return;
		}
		if (State == TEXT("cancelled"))
		{
			Callback(EComfyPollResult::Canceled, TEXT("Canceled on Comfy Cloud."), 0.f);
			return;
		}
		if (State == TEXT("failed"))
		{
			FString Message = TEXT("The workflow failed on Comfy Cloud.");
			const TSharedPtr<FJsonObject>* ExecutionError = nullptr;
			if (Detail->TryGetObjectField(TEXT("execution_error"), ExecutionError) && ExecutionError)
			{
				const FString Type = (*ExecutionError)->GetStringField(TEXT("exception_type"));
				if (Type == TEXT("InterruptProcessingException"))
				{
					Callback(EComfyPollResult::Canceled, TEXT("Interrupted on Comfy Cloud."), 0.f);
					return;
				}
				Message = FString::Printf(TEXT("Node %s (%s) raised: %s"), *(*ExecutionError)->GetStringField(TEXT("node_id")),
					*(*ExecutionError)->GetStringField(TEXT("node_type")), *(*ExecutionError)->GetStringField(TEXT("exception_message")));
			}
			Callback(EComfyPollResult::Failed, Message, 0.f);
			return;
		}

		const TSharedPtr<FJsonObject>* NodeOutputs = nullptr;
		if (!Detail->TryGetObjectField(TEXT("outputs"), NodeOutputs) || !NodeOutputs)
		{
			// Completed, but the outputs aren't listed yet.
			Callback(EComfyPollResult::Pending, FString(), 1.f);
			return;
		}
		Job->Outputs = ComfyBackendShared::CollectOutputs(Job, *NodeOutputs);
		Callback(EComfyPollResult::Done, FString(), 0.f);
	}, 30.f);
}

void FComfyPageBackend::Download(const FComfyOutputFile& OutputFile, FResultCallback Callback)
{
	const TSharedPtr<IComfyPageChannel> Page = Channel.Pin();
	if (!Page.IsValid() || !Page->IsReady())
	{
		Callback(false, NotSignedIn);
		return;
	}
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("filename"), OutputFile.RemoteRef);
	Args->SetStringField(TEXT("subfolder"), OutputFile.RemoteSubfolder);
	Args->SetStringField(TEXT("type"), OutputFile.RemoteType.IsEmpty() ? FString(TEXT("output")) : OutputFile.RemoteType);
	const FString LocalPath = OutputFile.LocalPath;
	const FString FileName = OutputFile.FileName;
	Page->Call(TEXT("fetch"), Write(Args), [LocalPath, FileName, Callback](bool bOk, const FString& Result, const FString& Error)
	{
		const TSharedPtr<FJsonObject> Fetched = bOk ? Parse(Result) : nullptr;
		TArray<uint8> Bytes;
		if (!Fetched.IsValid() || !FBase64::Decode(Fetched->GetStringField(TEXT("data")), Bytes))
		{
			Callback(false, FString::Printf(TEXT("Downloading %s from Comfy Cloud failed: %s"), *FileName, *Error));
			return;
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(LocalPath), true);
		if (!FFileHelper::SaveArrayToFile(Bytes, *LocalPath))
		{
			Callback(false, FString::Printf(TEXT("Couldn't write %s."), *LocalPath));
			return;
		}
		Callback(true, FString());
	}, 300.f);
}

void FComfyPageBackend::CancelJob(UComfyJob* Job)
{
	const TSharedPtr<IComfyPageChannel> Page = Channel.Pin();
	if (Job->RemoteId.IsEmpty() || !Page.IsValid())
	{
		return;
	}
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("prompt_id"), Job->RemoteId);
	Page->Call(TEXT("cancel"), Write(Args), [](bool, const FString&, const FString&) {}, 30.f);
}
