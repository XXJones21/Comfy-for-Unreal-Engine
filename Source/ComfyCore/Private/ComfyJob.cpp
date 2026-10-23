// Copyright Comfy Org. All Rights Reserved.

#include "ComfyJob.h"
#include "ComfyCoreModule.h"
#include "ComfyWorkflow.h"
#include "Backends/ComfyBackend.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"

void UComfyJob::Cancel()
{
	if (IsFinished())
	{
		return;
	}
	if (BackendImpl.IsValid())
	{
		BackendImpl->CancelJob(this);
	}
	else if (CancelHandler)
	{
		CancelHandler();
	}
	ErrorMessage = TEXT("Canceled.");
	SetState(EComfyJobState::Canceled);
	OnFailed.Broadcast(this);
	OnFinishedNative.Broadcast(this);
}

bool UComfyJob::GetOutput(const FString& Name, FComfyOutputFile& OutFile) const
{
	for (const FComfyOutputFile& File : Outputs)
	{
		if (File.Name == Name || File.NodeId == Name)
		{
			OutFile = File;
			return true;
		}
	}
	return false;
}

TArray<FComfyOutputFile> UComfyJob::GetOutputsOfKind(EComfyOutputKind Kind) const
{
	return Outputs.FilterByPredicate([Kind](const FComfyOutputFile& File) { return File.Kind == Kind; });
}

FString UComfyJob::GetOutputDirectory() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Comfy") / TEXT("Jobs") / JobId);
}

void UComfyJob::SetState(EComfyJobState NewState)
{
	LastActivitySeconds = FPlatformTime::Seconds();
	if (State == NewState || IsFinished())
	{
		return;
	}
	State = NewState;
	UE_LOG(LogComfy, Verbose, TEXT("Job %s (%s): %s"), *JobId, *Title, ComfyTypes::StateToString(State));
	OnStateChanged.Broadcast(this);
}

void UComfyJob::SetProgress(const FComfyProgress& NewProgress)
{
	LastActivitySeconds = FPlatformTime::Seconds();
	if (IsFinished())
	{
		return;
	}
	Progress = NewProgress;
	OnProgress.Broadcast(this);
}

void UComfyJob::Fail(const FString& Message)
{
	if (IsFinished())
	{
		return;
	}
	ErrorMessage = Message;
	FinishedAt = FDateTime::UtcNow();
	UE_LOG(LogComfy, Warning, TEXT("Job %s (%s) failed: %s"), *JobId, *Title, *Message);
	State = EComfyJobState::Failed;
	OnStateChanged.Broadcast(this);
	OnFailed.Broadcast(this);
	OnFinishedNative.Broadcast(this);
}

void UComfyJob::Complete()
{
	if (IsFinished())
	{
		return;
	}
	FinishedAt = FDateTime::UtcNow();
	Progress.Fraction = 1.f;
	Progress.Message = FString::Printf(TEXT("%d output(s)"), Outputs.Num());
	State = EComfyJobState::Succeeded;
	UE_LOG(LogComfy, Log, TEXT("Job %s (%s) succeeded with %d output(s) in %s"), *JobId, *Title, Outputs.Num(), *GetOutputDirectory());
	OnStateChanged.Broadcast(this);
	OnCompleted.Broadcast(this);
	OnFinishedNative.Broadcast(this);
}

FString UComfyJob::ToJson() const
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("job_id"), JobId);
	Root->SetStringField(TEXT("remote_id"), RemoteId);
	Root->SetStringField(TEXT("title"), Title);
	Root->SetStringField(TEXT("backend"), Backend.ToString());
	Root->SetStringField(TEXT("state"), ComfyTypes::StateToString(State));
	Root->SetBoolField(TEXT("finished"), IsFinished());
	Root->SetNumberField(TEXT("progress"), Progress.Fraction);
	if (!Progress.Message.IsEmpty())
	{
		Root->SetStringField(TEXT("progress_message"), Progress.Message);
	}
	if (Progress.QueuePosition >= 0)
	{
		Root->SetNumberField(TEXT("queue_position"), Progress.QueuePosition);
	}
	if (!ErrorMessage.IsEmpty())
	{
		Root->SetStringField(TEXT("error"), ErrorMessage);
	}
	TArray<TSharedPtr<FJsonValue>> OutputValues;
	for (const FComfyOutputFile& File : Outputs)
	{
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("name"), File.Name);
		Item->SetStringField(TEXT("node_id"), File.NodeId);
		Item->SetStringField(TEXT("file"), File.LocalPath);
		Item->SetStringField(TEXT("kind"), StaticEnum<EComfyOutputKind>()->GetNameStringByValue((int64)File.Kind));
		OutputValues.Add(MakeShared<FJsonValueObject>(Item));
	}
	Root->SetArrayField(TEXT("outputs"), OutputValues);

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}
