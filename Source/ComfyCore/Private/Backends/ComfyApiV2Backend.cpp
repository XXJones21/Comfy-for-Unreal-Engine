// Copyright Comfy Org. All Rights Reserved.

#include "ComfyBackend.h"
#include "ComfyCoreModule.h"
#include "ComfyJob.h"
#include "ComfyWorkflow.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

ComfyHttp::FHeaders FComfyApiV2Backend::AuthHeaders() const
{
	ComfyHttp::FHeaders Headers;
	if (!ApiKey.IsEmpty())
	{
		Headers.Add(TEXT("Authorization"), TEXT("Bearer ") + ApiKey);
	}
	return Headers;
}

void FComfyApiV2Backend::Probe(TFunction<void(const FComfyBackendStatus&)> Callback)
{
	FComfyBackendStatus Status;
	Status.Backend = Profile.Name;
	if (ApiKey.IsEmpty() && Profile.Kind != EComfyBackendKind::Local)
	{
		Status.Summary = TEXT("No API key. Add one in Editor Preferences > Plugins > Comfy, or set COMFY_API_KEY.");
		Callback(Status);
		return;
	}
	// Asking for an asset that can't exist checks reachability and the key without side effects:
	// 404 means the key was accepted, 401 means it wasn't.
	const FString Url = Profile.BaseUrl + TEXT("/api/v2/assets/00000000-0000-0000-0000-000000000000");
	const FString Base = Profile.BaseUrl;
	const ComfyHttp::FHeaders Auth = AuthHeaders();
	ComfyHttp::Send(TEXT("GET"), Url, Auth, [Status, Base, Auth, Callback](const ComfyHttp::FResponse& Response) mutable
	{
		if (!Response.bConnected || (Response.Code != 404 && !Response.IsOk()))
		{
			Status.bApiKeyRefused = Response.bConnected && (Response.Code == 401 || Response.Code == 403);
			Status.Summary = Response.Describe(FString::Printf(TEXT("Comfy API v2 at %s"), *Base));
			Callback(Status);
			return;
		}
		Status.bReachable = true;
		Status.Summary = FString::Printf(TEXT("Comfy API v2 at %s, key accepted"), *Base);
		// Comfy Cloud also serves ComfyUI's node catalog here, which gives model choices. Optional: skip if absent.
		ComfyHttp::Send(TEXT("GET"), Base + TEXT("/api/object_info"), Auth, [Status, Callback](const ComfyHttp::FResponse& Info) mutable
		{
			if (Info.IsOk() && Info.Json.IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Info.Json->Values)
				{
					Status.NodeClasses.Add(Pair.Key);
				}
				ComfyTypes::ReadInputOptions(Info.Json, Status.InputOptions);
				Status.Summary += FString::Printf(TEXT(", %d node types"), Status.NodeClasses.Num());
			}
			Callback(Status);
		}, 120.f);
	}, 15.f);
}

void FComfyApiV2Backend::UploadFile(const FString& LocalPath, FUploadCallback Callback)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *LocalPath))
	{
		Callback(false, nullptr, FString::Printf(TEXT("Couldn't read %s."), *LocalPath));
		return;
	}
	const FString FileName = FPaths::GetCleanFilename(LocalPath);
	const FString ContentType = ComfyTypes::ContentTypeFromFileName(FileName);
	ComfyHttp::FHeaders Headers = AuthHeaders();
	Headers.Add(TEXT("Idempotency-Key"), ComfyHttp::NewIdempotencyKey());
	TArray<ComfyHttp::FMultipartField> Fields = { { TEXT("content_type"), ContentType }, { TEXT("file_path"), FileName }, { TEXT("tags"), TEXT("input") } };
	ComfyHttp::SendMultipart(Profile.BaseUrl + TEXT("/api/v2/assets"), Headers, Fields, TEXT("file"), FileName, ContentType, Bytes,
		[FileName, Callback](const ComfyHttp::FResponse& Response)
	{
		if (!Response.IsOk() || !Response.Json.IsValid())
		{
			Callback(false, nullptr, Response.Describe(FString::Printf(TEXT("Uploading %s"), *FileName)));
			return;
		}
		// The typed reference API v2 expects wherever a graph names a file.
		const TSharedRef<FJsonObject> Info = MakeShared<FJsonObject>();
		Info->SetStringField(TEXT("id"), Response.Json->GetStringField(TEXT("id")));
		Info->SetStringField(TEXT("file_path"), FileName);
		const TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
		Reference->SetStringField(TEXT("__type"), TEXT("core/ASSET"));
		Reference->SetObjectField(TEXT("info"), Info);
		Callback(true, MakeShared<FJsonValueObject>(Reference), FString());
	});
}

void FComfyApiV2Backend::Submit(UComfyJob* Job, const TSharedRef<FJsonObject>& Graph, FResultCallback Callback)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("workflow"), Graph);
	if (Profile.bSendPartnerNodeKey && !ApiKey.IsEmpty())
	{
		const TSharedRef<FJsonObject> Extra = MakeShared<FJsonObject>();
		Extra->SetStringField(TEXT("api_key_comfy_org"), ApiKey);
		Body->SetObjectField(TEXT("extra_data"), Extra);
	}
	ComfyHttp::FHeaders Headers = AuthHeaders();
	Headers.Add(TEXT("Idempotency-Key"), ComfyHttp::NewIdempotencyKey());

	TWeakObjectPtr<UComfyJob> WeakJob = Job;
	TWeakPtr<IComfyBackend> WeakThis = AsShared();
	const FString Base = Profile.BaseUrl;
	ComfyHttp::SendJson(TEXT("POST"), Base + TEXT("/api/v2/jobs"), Headers, Body, [WeakJob, WeakThis, Base, Callback](const ComfyHttp::FResponse& Response)
	{
		UComfyJob* Job = WeakJob.Get();
		TSharedPtr<IComfyBackend> Pinned = WeakThis.Pin();
		if (!Job || !Pinned.IsValid())
		{
			return;
		}
		if (!Response.IsOk() || !Response.Json.IsValid())
		{
			// An unknown outcome (timeout, 5xx) may still have created a job; the key is single-use,
			// so we report it rather than resubmitting.
			Callback(false, Response.Describe(TEXT("Submitting the workflow")));
			return;
		}
		FComfyApiV2Backend* This = static_cast<FComfyApiV2Backend*>(Pinned.Get());
		Job->RemoteId = Response.Json->GetStringField(TEXT("id"));
		const TSharedPtr<FJsonObject>* Urls = nullptr;
		FString Self = Base + TEXT("/api/v2/jobs/") + Job->RemoteId;
		FString Cancel = Self + TEXT("/cancel");
		if (Response.Json->TryGetObjectField(TEXT("urls"), Urls) && Urls)
		{
			FString Link;
			if ((*Urls)->TryGetStringField(TEXT("self"), Link)) Self = ComfyHttp::ResolveLink(Base, Link);
			if ((*Urls)->TryGetStringField(TEXT("cancel"), Link)) Cancel = ComfyHttp::ResolveLink(Base, Link);
		}
		This->SelfUrls.Add(Job->JobId, Self);
		This->CancelUrls.Add(Job->JobId, Cancel);
		Job->SetState(EComfyJobState::Queued);
		Callback(true, FString());
	});
}

void FComfyApiV2Backend::Poll(UComfyJob* Job, FPollCallback Callback)
{
	const FString* Self = SelfUrls.Find(Job->JobId);
	if (!Self)
	{
		Callback(EComfyPollResult::Failed, TEXT("Lost track of the job's URL."), 0.f);
		return;
	}
	TWeakObjectPtr<UComfyJob> WeakJob = Job;
	ComfyHttp::Send(TEXT("GET"), *Self, AuthHeaders(), [WeakJob, Callback](const ComfyHttp::FResponse& Response)
	{
		UComfyJob* Job = WeakJob.Get();
		if (!Job)
		{
			return;
		}
		if (Response.Code == 429 || !Response.bConnected || Response.Code >= 500)
		{
			Callback(EComfyPollResult::Pending, FString(), Response.RetryAfter(3.f));
			return;
		}
		if (!Response.IsOk() || !Response.Json.IsValid())
		{
			Callback(EComfyPollResult::Failed, Response.Describe(TEXT("Checking the job")), 0.f);
			return;
		}
		const TSharedPtr<FJsonObject>& J = Response.Json;
		const FString Status = J->GetStringField(TEXT("status"));

		FComfyProgress Progress = Job->Progress;
		double QueuePosition = -1;
		if (J->TryGetNumberField(TEXT("queue_position"), QueuePosition))
		{
			Progress.QueuePosition = int32(QueuePosition);
		}
		const TSharedPtr<FJsonObject>* ProgressObject = nullptr;
		if (J->TryGetObjectField(TEXT("progress"), ProgressObject) && ProgressObject)
		{
			double Value = 0;
			if ((*ProgressObject)->TryGetNumberField(TEXT("value"), Value))
			{
				Progress.Fraction = float(Value);
			}
			(*ProgressObject)->TryGetStringField(TEXT("message"), Progress.Message);
			(*ProgressObject)->TryGetStringField(TEXT("current_node"), Progress.CurrentNode);
		}
		else if (Status == TEXT("queued") && Progress.QueuePosition >= 0)
		{
			Progress.Message = FString::Printf(TEXT("Queued, position %d"), Progress.QueuePosition);
		}
		Job->SetProgress(Progress);

		if (Status == TEXT("queued"))
		{
			Job->SetState(EComfyJobState::Queued);
			Callback(EComfyPollResult::Pending, FString(), Response.RetryAfter(1.5f));
			return;
		}
		if (Status == TEXT("running") || Status == TEXT("canceling"))
		{
			Job->SetState(EComfyJobState::Running);
			Callback(EComfyPollResult::Pending, FString(), Response.RetryAfter(1.0f));
			return;
		}
		if (Status == TEXT("canceled"))
		{
			Callback(EComfyPollResult::Canceled, TEXT("Canceled."), 0.f);
			return;
		}
		if (Status == TEXT("failed") || Status == TEXT("expired"))
		{
			FString Message = FString::Printf(TEXT("The job %s."), *Status);
			const TSharedPtr<FJsonObject>* Error = nullptr;
			if (J->TryGetObjectField(TEXT("error"), Error) && Error)
			{
				FString Code, Text, NodeId, ClassType;
				(*Error)->TryGetStringField(TEXT("code"), Code);
				(*Error)->TryGetStringField(TEXT("message"), Text);
				(*Error)->TryGetStringField(TEXT("node_id"), NodeId);
				(*Error)->TryGetStringField(TEXT("class_type"), ClassType);
				Message = NodeId.IsEmpty() ? FString::Printf(TEXT("%s (%s)"), *Text, *Code) : FString::Printf(TEXT("Node %s (%s): %s (%s)"), *NodeId, *ClassType, *Text, *Code);
			}
			Callback(EComfyPollResult::Failed, Message, 0.f);
			return;
		}

		// succeeded
		Job->Outputs.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
		if (J->TryGetArrayField(TEXT("outputs"), Outputs) && Outputs)
		{
			for (const TSharedPtr<FJsonValue>& Item : *Outputs)
			{
				const TSharedPtr<FJsonObject> O = Item->AsObject();
				if (!O.IsValid() || O->GetStringField(TEXT("type")) == TEXT("latent"))
				{
					continue;
				}
				FComfyOutputFile File;
				File.NodeId = O->GetStringField(TEXT("node_id"));
				File.FileName = FPaths::GetCleanFilename(O->GetStringField(TEXT("name")));
				File.RemoteRef = O->GetStringField(TEXT("id"));
				File.Kind = ComfyTypes::KindFromFileName(File.FileName);
				const FComfyWorkflowOutput* Manifest = Job->Workflow ? Job->Workflow->FindOutputForNode(File.NodeId) : nullptr;
				File.Name = Manifest ? Manifest->Name : (File.NodeId.IsEmpty() ? File.FileName : File.NodeId);
				File.ImportRule = Manifest ? Manifest->ImportRule : EComfyImportRule::Auto;
				File.LocalPath = Job->GetOutputDirectory() / File.FileName;
				Job->Outputs.Add(File);
			}
		}
		Callback(EComfyPollResult::Done, FString(), 0.f);
	}, 30.f);
}

void FComfyApiV2Backend::Download(const FComfyOutputFile& OutputFile, FResultCallback Callback)
{
	// Resolve the asset to its short-lived download URL, then fetch that. Signed storage URLs
	// carry their own authorization, so the API key is only sent back to our own origin.
	const FString Base = Profile.BaseUrl;
	const ComfyHttp::FHeaders Auth = AuthHeaders();
	const FString LocalPath = OutputFile.LocalPath;
	ComfyHttp::Send(TEXT("GET"), Base + TEXT("/api/v2/assets/") + OutputFile.RemoteRef, Auth, [Base, Auth, LocalPath, Callback](const ComfyHttp::FResponse& Response)
	{
		if (!Response.IsOk() || !Response.Json.IsValid())
		{
			Callback(false, Response.Describe(TEXT("Looking up an output")));
			return;
		}
		const FString Url = ComfyHttp::ResolveLink(Base, Response.Json->GetStringField(TEXT("url")));
		const bool bSameOrigin = ComfyHttp::Origin(Url) == ComfyHttp::Origin(Base);
		ComfyHttp::Download(Url, bSameOrigin ? Auth : ComfyHttp::FHeaders(), LocalPath, Callback);
	});
}

void FComfyApiV2Backend::CancelJob(UComfyJob* Job)
{
	if (const FString* Cancel = CancelUrls.Find(Job->JobId))
	{
		ComfyHttp::Send(TEXT("POST"), *Cancel, AuthHeaders(), [](const ComfyHttp::FResponse&) {});
	}
}
