// Copyright Comfy Org. All Rights Reserved.

#include "ComfyBackend.h"
#include "ComfyCoreModule.h"
#include "ComfyJob.h"
#include "ComfyWorkflow.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

FComfyNativeBackend::FComfyNativeBackend(const FComfyBackendProfile& InProfile, const FString& InApiKey)
	: IComfyBackend(InProfile, InApiKey)
	, ClientId(FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower))
{
}

FComfyNativeBackend::~FComfyNativeBackend()
{
	if (Socket.IsValid())
	{
		Socket->OnMessage().Clear();
		Socket->Close();
	}
}

void FComfyNativeBackend::Probe(TFunction<void(const FComfyBackendStatus&)> Callback)
{
	const FName Name = Profile.Name;
	const FString Base = Profile.BaseUrl;
	ComfyHttp::Send(TEXT("GET"), Base + TEXT("/system_stats"), {}, [Name, Base, Callback](const ComfyHttp::FResponse& Stats)
	{
		FComfyBackendStatus Status;
		Status.Backend = Name;
		if (!Stats.IsOk() || !Stats.Json.IsValid())
		{
			Status.Summary = Stats.Describe(FString::Printf(TEXT("ComfyUI at %s"), *Base));
			Callback(Status);
			return;
		}
		Status.bReachable = true;
		FString Version = TEXT("ComfyUI");
		const TSharedPtr<FJsonObject>* System = nullptr;
		if (Stats.Json->TryGetObjectField(TEXT("system"), System) && System)
		{
			FString V;
			if ((*System)->TryGetStringField(TEXT("comfyui_version"), V))
			{
				Version = FString::Printf(TEXT("ComfyUI %s"), *V);
			}
		}
		FString Device;
		const TArray<TSharedPtr<FJsonValue>>* Devices = nullptr;
		if (Stats.Json->TryGetArrayField(TEXT("devices"), Devices) && Devices && Devices->Num() > 0)
		{
			const TSharedPtr<FJsonObject> First = (*Devices)[0]->AsObject();
			if (First.IsValid())
			{
				FString DeviceName = First->GetStringField(TEXT("name"));
				int32 Colon = INDEX_NONE;
				if (DeviceName.FindChar(TEXT(':'), Colon))
				{
					DeviceName = DeviceName.Mid(Colon + 1).TrimStartAndEnd();
				}
				const double FreeGB = First->GetNumberField(TEXT("vram_free")) / (1024.0 * 1024.0 * 1024.0);
				Device = FString::Printf(TEXT(", %s, %.1f GB VRAM free"), *DeviceName, FreeGB);
			}
		}
		Status.Summary = Version + Device;

		// Node classes, for checking a workflow before submitting it.
		ComfyHttp::Send(TEXT("GET"), Base + TEXT("/object_info"), {}, [Status, Callback](const ComfyHttp::FResponse& Info) mutable
		{
			if (Info.IsOk() && Info.Json.IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Info.Json->Values)
				{
					Status.NodeClasses.Add(Pair.Key);
				}
				Status.Summary += FString::Printf(TEXT(", %d node types"), Status.NodeClasses.Num());
			}
			Callback(Status);
		}, 120.f);
	}, 10.f);
}

void FComfyNativeBackend::UploadFile(const FString& LocalPath, FUploadCallback Callback)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *LocalPath))
	{
		Callback(false, nullptr, FString::Printf(TEXT("Couldn't read %s."), *LocalPath));
		return;
	}
	const FString FileName = FPaths::GetCleanFilename(LocalPath);
	// /upload/image stores any file in ComfyUI's input folder; the frontend uses it for audio too.
	TArray<ComfyHttp::FMultipartField> Fields = { { TEXT("type"), TEXT("input") }, { TEXT("overwrite"), TEXT("true") }, { TEXT("subfolder"), TEXT("unreal") } };
	ComfyHttp::SendMultipart(Profile.BaseUrl + TEXT("/upload/image"), {}, Fields, TEXT("image"), FileName, ComfyTypes::ContentTypeFromFileName(FileName), Bytes,
		[FileName, Callback](const ComfyHttp::FResponse& Response)
	{
		if (!Response.IsOk() || !Response.Json.IsValid())
		{
			Callback(false, nullptr, Response.Describe(FString::Printf(TEXT("Uploading %s"), *FileName)));
			return;
		}
		const FString Name = Response.Json->GetStringField(TEXT("name"));
		const FString Subfolder = Response.Json->GetStringField(TEXT("subfolder"));
		const FString GraphName = Subfolder.IsEmpty() ? Name : Subfolder + TEXT("/") + Name;
		Callback(true, MakeShared<FJsonValueString>(GraphName), FString());
	});
}

void FComfyNativeBackend::EnsureSocket()
{
	if (Socket.IsValid() && Socket->IsConnected())
	{
		return;
	}
	FString WsUrl = Profile.BaseUrl;
	WsUrl.ReplaceInline(TEXT("https://"), TEXT("wss://"));
	WsUrl.ReplaceInline(TEXT("http://"), TEXT("ws://"));
	WsUrl += FString::Printf(TEXT("/ws?clientId=%s"), *ClientId);

	Socket = FWebSocketsModule::Get().CreateWebSocket(WsUrl, TEXT(""));
	TWeakPtr<IComfyBackend> WeakThis = AsShared();
	Socket->OnMessage().AddLambda([WeakThis](const FString& Message)
	{
		if (TSharedPtr<IComfyBackend> Pinned = WeakThis.Pin())
		{
			StaticCastSharedPtr<FComfyNativeBackend>(Pinned)->HandleSocketMessage(Message);
		}
	});
	Socket->OnConnectionError().AddLambda([WsUrl](const FString& Error)
	{
		// Progress falls back to polling /history; nothing else depends on the socket.
		UE_LOG(LogComfy, Verbose, TEXT("ComfyUI websocket %s unavailable: %s"), *WsUrl, *Error);
	});
	Socket->Connect();
}

void FComfyNativeBackend::HandleSocketMessage(const FString& Message)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}
	const FString Type = Root->GetStringField(TEXT("type"));
	const TSharedPtr<FJsonObject>* Data = nullptr;
	if (!Root->TryGetObjectField(TEXT("data"), Data) || !Data)
	{
		return;
	}
	FString PromptId;
	(*Data)->TryGetStringField(TEXT("prompt_id"), PromptId);
	const TWeakObjectPtr<UComfyJob>* WeakJob = JobsByPromptId.Find(PromptId);
	UComfyJob* Job = WeakJob ? WeakJob->Get() : nullptr;
	if (!Job || Job->IsFinished())
	{
		return;
	}

	FComfyProgress Progress = Job->Progress;
	if (Type == TEXT("execution_start"))
	{
		Job->SetState(EComfyJobState::Running);
		Progress.QueuePosition = 0;
		Progress.Message = TEXT("Started");
	}
	else if (Type == TEXT("executing"))
	{
		FString Node;
		if ((*Data)->TryGetStringField(TEXT("node"), Node))
		{
			Job->SetState(EComfyJobState::Running);
			Progress.CurrentNode = Node;
			Progress.Message = FString::Printf(TEXT("Node %s"), *Node);
		}
	}
	else if (Type == TEXT("progress"))
	{
		const double Value = (*Data)->GetNumberField(TEXT("value"));
		const double Max = (*Data)->GetNumberField(TEXT("max"));
		FString Node;
		(*Data)->TryGetStringField(TEXT("node"), Node);
		Progress.Fraction = Max > 0 ? float(Value / Max) : -1.f;
		Progress.CurrentNode = Node;
		Progress.Message = FString::Printf(TEXT("Node %s: %d/%d"), *Node, int32(Value), int32(Max));
		Job->SetState(EComfyJobState::Running);
	}
	else
	{
		return;
	}
	Job->SetProgress(Progress);
}

void FComfyNativeBackend::Submit(UComfyJob* Job, const TSharedRef<FJsonObject>& Graph, FResultCallback Callback)
{
	EnsureSocket();

	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("prompt"), Graph);
	Body->SetStringField(TEXT("client_id"), ClientId);
	if (Profile.bSendPartnerNodeKey && !ApiKey.IsEmpty())
	{
		const TSharedRef<FJsonObject> Extra = MakeShared<FJsonObject>();
		Extra->SetStringField(TEXT("api_key_comfy_org"), ApiKey);
		Body->SetObjectField(TEXT("extra_data"), Extra);
	}

	TWeakObjectPtr<UComfyJob> WeakJob = Job;
	TWeakPtr<IComfyBackend> WeakThis = AsShared();
	ComfyHttp::SendJson(TEXT("POST"), Profile.BaseUrl + TEXT("/prompt"), {}, Body, [WeakJob, WeakThis, Callback](const ComfyHttp::FResponse& Response)
	{
		UComfyJob* Job = WeakJob.Get();
		TSharedPtr<IComfyBackend> Pinned = WeakThis.Pin();
		if (!Job || !Pinned.IsValid())
		{
			return;
		}
		if (!Response.IsOk() || !Response.Json.IsValid())
		{
			FString Message = Response.Describe(TEXT("Submitting the workflow"));
			// ComfyUI lists per-node validation errors separately.
			const TSharedPtr<FJsonObject>* NodeErrors = nullptr;
			if (Response.Json.IsValid() && Response.Json->TryGetObjectField(TEXT("node_errors"), NodeErrors) && NodeErrors)
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*NodeErrors)->Values)
				{
					const TSharedPtr<FJsonObject> NodeError = Pair.Value->AsObject();
					if (!NodeError.IsValid())
					{
						continue;
					}
					const TArray<TSharedPtr<FJsonValue>>* Errors = nullptr;
					if (NodeError->TryGetArrayField(TEXT("errors"), Errors) && Errors)
					{
						for (const TSharedPtr<FJsonValue>& Error : *Errors)
						{
							const TSharedPtr<FJsonObject> ErrorObject = Error->AsObject();
							if (ErrorObject.IsValid())
							{
								Message += FString::Printf(TEXT("\n  Node %s (%s): %s %s"), *Pair.Key, *NodeError->GetStringField(TEXT("class_type")),
									*ErrorObject->GetStringField(TEXT("message")), *ErrorObject->GetStringField(TEXT("details")));
							}
						}
					}
				}
			}
			Callback(false, Message);
			return;
		}
		Job->RemoteId = Response.Json->GetStringField(TEXT("prompt_id"));
		StaticCastSharedPtr<FComfyNativeBackend>(Pinned)->JobsByPromptId.Add(Job->RemoteId, Job);
		Callback(true, FString());
	});
}

void FComfyNativeBackend::Poll(UComfyJob* Job, FPollCallback Callback)
{
	const float Interval = 1.0f;
	TWeakObjectPtr<UComfyJob> WeakJob = Job;
	ComfyHttp::Send(TEXT("GET"), Profile.BaseUrl + TEXT("/history/") + Job->RemoteId, {}, [WeakJob, Interval, Callback](const ComfyHttp::FResponse& Response)
	{
		UComfyJob* Job = WeakJob.Get();
		if (!Job)
		{
			return;
		}
		if (!Response.IsOk() || !Response.Json.IsValid())
		{
			// Transient: keep trying until the job timeout.
			Callback(EComfyPollResult::Pending, Response.Describe(TEXT("Checking the job")), Interval * 2.f);
			return;
		}
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (!Response.Json->TryGetObjectField(Job->RemoteId, Entry) || !Entry)
		{
			if (Job->State == EComfyJobState::Pending)
			{
				Job->SetState(EComfyJobState::Queued);
			}
			Callback(EComfyPollResult::Pending, FString(), Interval);
			return;
		}

		const TSharedPtr<FJsonObject>* StatusObject = nullptr;
		if ((*Entry)->TryGetObjectField(TEXT("status"), StatusObject) && StatusObject)
		{
			FString StatusStr;
			(*StatusObject)->TryGetStringField(TEXT("status_str"), StatusStr);
			if (StatusStr == TEXT("error"))
			{
				FString Message = TEXT("The workflow failed in ComfyUI.");
				const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
				if ((*StatusObject)->TryGetArrayField(TEXT("messages"), Messages) && Messages)
				{
					for (const TSharedPtr<FJsonValue>& Item : *Messages)
					{
						const TArray<TSharedPtr<FJsonValue>>& Pair = Item->AsArray();
						if (Pair.Num() == 2 && Pair[0]->AsString() == TEXT("execution_error"))
						{
							const TSharedPtr<FJsonObject> Detail = Pair[1]->AsObject();
							if (Detail.IsValid())
							{
								Message = FString::Printf(TEXT("Node %s (%s) raised: %s"), *Detail->GetStringField(TEXT("node_id")), *Detail->GetStringField(TEXT("node_type")), *Detail->GetStringField(TEXT("exception_message")));
							}
						}
						else if (Pair.Num() == 2 && Pair[0]->AsString() == TEXT("execution_interrupted"))
						{
							Callback(EComfyPollResult::Canceled, TEXT("Interrupted in ComfyUI."), 0.f);
							return;
						}
					}
				}
				Callback(EComfyPollResult::Failed, Message, 0.f);
				return;
			}
		}

		const TSharedPtr<FJsonObject>* NodeOutputs = nullptr;
		if (!(*Entry)->TryGetObjectField(TEXT("outputs"), NodeOutputs) || !NodeOutputs)
		{
			Callback(EComfyPollResult::Pending, FString(), Interval);
			return;
		}

		TArray<FComfyOutputFile> Saved;
		TArray<FComfyOutputFile> Previews;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& NodePair : (*NodeOutputs)->Values)
		{
			const TSharedPtr<FJsonObject> NodeObject = NodePair.Value->AsObject();
			if (!NodeObject.IsValid())
			{
				continue;
			}
			const FComfyWorkflowOutput* Manifest = Job->Workflow ? Job->Workflow->FindOutputForNode(NodePair.Key) : nullptr;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : NodeObject->Values)
			{
				if (Field.Value->Type != EJson::Array)
				{
					continue;
				}
				for (const TSharedPtr<FJsonValue>& Item : Field.Value->AsArray())
				{
					const TSharedPtr<FJsonObject> FileObject = Item->AsObject();
					FString FileName;
					if (!FileObject.IsValid() || !FileObject->TryGetStringField(TEXT("filename"), FileName))
					{
						continue;
					}
					FComfyOutputFile File;
					File.NodeId = NodePair.Key;
					File.FileName = FileName;
					File.RemoteRef = FileName;
					FileObject->TryGetStringField(TEXT("subfolder"), File.RemoteSubfolder);
					FileObject->TryGetStringField(TEXT("type"), File.RemoteType);
					File.Kind = ComfyTypes::KindFromFileName(FileName);
					File.Name = Manifest ? Manifest->Name : NodePair.Key;
					File.ImportRule = Manifest ? Manifest->ImportRule : EComfyImportRule::Auto;
					File.LocalPath = Job->GetOutputDirectory() / FileName;
					(File.RemoteType == TEXT("output") || Manifest ? Saved : Previews).Add(File);
				}
			}
		}
		Job->Outputs = Saved.Num() > 0 ? Saved : Previews;
		Callback(EComfyPollResult::Done, FString(), 0.f);
	}, 30.f);
}

void FComfyNativeBackend::Download(const FComfyOutputFile& OutputFile, FResultCallback Callback)
{
	const FString Url = FString::Printf(TEXT("%s/view?filename=%s&subfolder=%s&type=%s"), *Profile.BaseUrl,
		*ComfyHttp::UrlEncode(OutputFile.RemoteRef), *ComfyHttp::UrlEncode(OutputFile.RemoteSubfolder),
		*ComfyHttp::UrlEncode(OutputFile.RemoteType.IsEmpty() ? FString(TEXT("output")) : OutputFile.RemoteType));
	ComfyHttp::Download(Url, {}, OutputFile.LocalPath, MoveTemp(Callback));
}

void FComfyNativeBackend::CancelJob(UComfyJob* Job)
{
	if (Job->RemoteId.IsEmpty())
	{
		return;
	}
	// Remove it from the queue if it hasn't started, and interrupt it if it has.
	const TSharedRef<FJsonObject> Delete = MakeShared<FJsonObject>();
	Delete->SetArrayField(TEXT("delete"), { MakeShared<FJsonValueString>(Job->RemoteId) });
	ComfyHttp::SendJson(TEXT("POST"), Profile.BaseUrl + TEXT("/queue"), {}, Delete, [](const ComfyHttp::FResponse&) {});
	const TSharedRef<FJsonObject> Interrupt = MakeShared<FJsonObject>();
	Interrupt->SetStringField(TEXT("prompt_id"), Job->RemoteId);
	ComfyHttp::SendJson(TEXT("POST"), Profile.BaseUrl + TEXT("/interrupt"), {}, Interrupt, [](const ComfyHttp::FResponse&) {});
	JobsByPromptId.Remove(Job->RemoteId);
}
