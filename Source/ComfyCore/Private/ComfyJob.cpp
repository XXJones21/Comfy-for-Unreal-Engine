// Copyright Comfy Org. All Rights Reserved.

#include "ComfyJob.h"
#include "ComfyCoreModule.h"
#include "ComfyWorkflow.h"
#include "Backends/ComfyBackend.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
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
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}

namespace ComfyJobPrivate
{
	static const TCHAR* RecordFileName = TEXT("job.json");

	template <typename TEnum>
	static FString EnumName(TEnum Value)
	{
		return StaticEnum<TEnum>()->GetNameStringByValue(int64(Value));
	}

	template <typename TEnum>
	static TEnum EnumValue(const FString& Name, TEnum Fallback)
	{
		const int64 Value = StaticEnum<TEnum>()->GetValueByNameString(Name);
		return Value == INDEX_NONE ? Fallback : TEnum(Value);
	}

	/** Files the plugin derives from an output next to it (tileable copies, normal maps): not outputs themselves. */
	static bool IsDerivedFile(const FString& FileName)
	{
		const FString Base = FPaths::GetBaseFilename(FileName);
		return Base.EndsWith(TEXT("_tile")) || Base.EndsWith(TEXT("_normal"));
	}
}

void UComfyJob::SaveRecord() const
{
	using namespace ComfyJobPrivate;
	if (!IsFinished())
	{
		return;
	}
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetStringField(TEXT("job_id"), JobId);
	Root->SetStringField(TEXT("remote_id"), RemoteId);
	Root->SetStringField(TEXT("title"), Title);
	Root->SetStringField(TEXT("backend"), Backend.ToString());
	Root->SetStringField(TEXT("backend_kind"), EnumName(BackendKind));
	Root->SetStringField(TEXT("model_id"), ModelId);
	Root->SetStringField(TEXT("state"), EnumName(State));
	Root->SetStringField(TEXT("error"), ErrorMessage);
	Root->SetStringField(TEXT("created_at"), CreatedAt.ToIso8601());
	Root->SetStringField(TEXT("finished_at"), FinishedAt.ToIso8601());

	const TSharedRef<FJsonObject> WorkflowJson = MakeShared<FJsonObject>();
	if (Workflow)
	{
		WorkflowJson->SetStringField(TEXT("path"), Workflow->HasAnyFlags(RF_Transient) ? FString() : Workflow->GetPathName());
		WorkflowJson->SetStringField(TEXT("source"), Workflow->SourceFile);
		WorkflowJson->SetStringField(TEXT("name"), Workflow->GetDisplayNameString());
	}
	else
	{
		WorkflowJson->SetStringField(TEXT("path"), WorkflowPath);
		WorkflowJson->SetStringField(TEXT("source"), WorkflowSource);
		WorkflowJson->SetStringField(TEXT("name"), WorkflowName);
	}
	Root->SetObjectField(TEXT("workflow"), WorkflowJson);

	const TSharedRef<FJsonObject> InputsJson = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& Pair : Inputs)
	{
		InputsJson->SetStringField(Pair.Key, Pair.Value);
	}
	Root->SetObjectField(TEXT("inputs"), InputsJson);

	TArray<TSharedPtr<FJsonValue>> OutputValues;
	for (const FComfyOutputFile& File : Outputs)
	{
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("name"), File.Name);
		Item->SetStringField(TEXT("node_id"), File.NodeId);
		// Relative to the job folder, so the history moves with the project.
		Item->SetStringField(TEXT("file"), File.FileName);
		Item->SetStringField(TEXT("kind"), EnumName(File.Kind));
		Item->SetStringField(TEXT("import_rule"), EnumName(File.ImportRule));
		OutputValues.Add(MakeShared<FJsonValueObject>(Item));
	}
	Root->SetArrayField(TEXT("outputs"), OutputValues);

	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	FJsonSerializer::Serialize(Root, Writer);
	const FString Path = GetOutputDirectory() / RecordFileName;
	IFileManager::Get().MakeDirectory(*GetOutputDirectory(), true);
	if (!FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogComfy, Warning, TEXT("Couldn't save the run record %s."), *Path);
	}
}

UComfyJob* UComfyJob::LoadRecord(UObject* Outer, const FString& Directory)
{
	using namespace ComfyJobPrivate;
	UComfyJob* Job = NewObject<UComfyJob>(Outer);
	Job->JobId = FPaths::GetCleanFilename(Directory);
	Job->bFromHistory = true;

	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (FFileHelper::LoadFileToString(Text, *(Directory / RecordFileName)) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())
	{
		Root->TryGetStringField(TEXT("remote_id"), Job->RemoteId);
		Root->TryGetStringField(TEXT("title"), Job->Title);
		Root->TryGetStringField(TEXT("model_id"), Job->ModelId);
		Root->TryGetStringField(TEXT("error"), Job->ErrorMessage);
		FString Value;
		if (Root->TryGetStringField(TEXT("backend"), Value))
		{
			Job->Backend = FName(*Value);
		}
		if (Root->TryGetStringField(TEXT("backend_kind"), Value))
		{
			Job->BackendKind = EnumValue(Value, EComfyBackendKind::Local);
		}
		Job->State = Root->TryGetStringField(TEXT("state"), Value) ? EnumValue(Value, EComfyJobState::Succeeded) : EComfyJobState::Succeeded;
		if (Root->TryGetStringField(TEXT("created_at"), Value))
		{
			FDateTime::ParseIso8601(*Value, Job->CreatedAt);
		}
		if (Root->TryGetStringField(TEXT("finished_at"), Value))
		{
			FDateTime::ParseIso8601(*Value, Job->FinishedAt);
		}
		const TSharedPtr<FJsonObject>* WorkflowJson = nullptr;
		if (Root->TryGetObjectField(TEXT("workflow"), WorkflowJson) && WorkflowJson)
		{
			(*WorkflowJson)->TryGetStringField(TEXT("path"), Job->WorkflowPath);
			(*WorkflowJson)->TryGetStringField(TEXT("source"), Job->WorkflowSource);
			(*WorkflowJson)->TryGetStringField(TEXT("name"), Job->WorkflowName);
		}
		const TSharedPtr<FJsonObject>* InputsJson = nullptr;
		if (Root->TryGetObjectField(TEXT("inputs"), InputsJson) && InputsJson)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*InputsJson)->Values)
			{
				Job->Inputs.Add(Pair.Key, Pair.Value.IsValid() ? Pair.Value->AsString() : FString());
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* OutputValues = nullptr;
		if (Root->TryGetArrayField(TEXT("outputs"), OutputValues))
		{
			for (const TSharedPtr<FJsonValue>& Item : *OutputValues)
			{
				const TSharedPtr<FJsonObject> Object = Item.IsValid() ? Item->AsObject() : nullptr;
				if (!Object.IsValid())
				{
					continue;
				}
				FComfyOutputFile& File = Job->Outputs.AddDefaulted_GetRef();
				Object->TryGetStringField(TEXT("name"), File.Name);
				Object->TryGetStringField(TEXT("node_id"), File.NodeId);
				Object->TryGetStringField(TEXT("file"), File.FileName);
				File.Kind = Object->TryGetStringField(TEXT("kind"), Value) ? EnumValue(Value, EComfyOutputKind::File) : ComfyTypes::KindFromFileName(File.FileName);
				File.ImportRule = Object->TryGetStringField(TEXT("import_rule"), Value) ? EnumValue(Value, EComfyImportRule::Auto) : EComfyImportRule::Auto;
				File.LocalPath = Directory / File.FileName;
			}
		}
	}
	else
	{
		// A run from before records were kept: its outputs are all there is.
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Directory / TEXT("*")), true, false);
		Files.Sort();
		for (const FString& FileName : Files)
		{
			if (FileName == RecordFileName || FileName == TEXT("result.json") || IsDerivedFile(FileName))
			{
				continue;
			}
			FComfyOutputFile& File = Job->Outputs.AddDefaulted_GetRef();
			File.Name = FPaths::GetBaseFilename(FileName);
			File.FileName = FileName;
			File.Kind = ComfyTypes::KindFromFileName(FileName);
			File.LocalPath = Directory / FileName;
		}
		if (Job->Outputs.Num() == 0)
		{
			Job->MarkAsGarbage();
			return nullptr;
		}
		Job->Title = TEXT("Earlier run");
		Job->State = EComfyJobState::Succeeded;
		// Job ids start with the local start time: 20261024-224906-c28635.
		int32 Year = 0, Month = 0, Day = 0, Hour = 0, Minute = 0, Second = 0;
		if (Job->JobId.Len() >= 15 && FCString::IsNumeric(*Job->JobId.Left(8)))
		{
			Year = FCString::Atoi(*Job->JobId.Mid(0, 4));
			Month = FCString::Atoi(*Job->JobId.Mid(4, 2));
			Day = FCString::Atoi(*Job->JobId.Mid(6, 2));
			Hour = FCString::Atoi(*Job->JobId.Mid(9, 2));
			Minute = FCString::Atoi(*Job->JobId.Mid(11, 2));
			Second = FCString::Atoi(*Job->JobId.Mid(13, 2));
		}
		const FDateTime Modified = IFileManager::Get().GetTimeStamp(*Job->Outputs[0].LocalPath);
		Job->FinishedAt = Modified;
		Job->CreatedAt = FDateTime::Validate(Year, Month, Day, Hour, Minute, Second, 0)
			? FDateTime(Year, Month, Day, Hour, Minute, Second) - (FDateTime::Now() - FDateTime::UtcNow())
			: Modified;
	}
	if (Job->Title.IsEmpty())
	{
		Job->Title = Job->WorkflowName.IsEmpty() ? TEXT("Run") : Job->WorkflowName;
	}
	if (!Job->IsFinished())
	{
		// Recorded mid-run by an older build, or the editor closed first: it isn't coming back.
		Job->State = EComfyJobState::Canceled;
	}
	return Job;
}
