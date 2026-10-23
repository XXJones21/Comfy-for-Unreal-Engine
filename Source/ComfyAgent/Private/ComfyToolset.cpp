// Copyright Comfy Org. All Rights Reserved.

#include "ComfyToolset.h"
#include "ComfyEditorLibrary.h"
#include "ComfyJob.h"
#include "ComfySettings.h"
#include "ComfySubsystem.h"
#include "ComfyWorkflow.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/StrongObjectPtr.h"

namespace ComfyToolsetPrivate
{
	static FString Write(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	static FString Write(const TArray<TSharedPtr<FJsonValue>>& Array)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Array, Writer);
		return Out;
	}

	static TSharedPtr<FJsonValue> Parse(const FString& Json)
	{
		TSharedPtr<FJsonValue> Value;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		FJsonSerializer::Deserialize(Reader, Value);
		return Value.IsValid() ? Value : MakeShared<FJsonValueString>(Json);
	}

	/** Report a tool error the way the toolset registry expects, and return an empty result. */
	static FString Error(const FString& Message)
	{
		UKismetSystemLibrary::RaiseScriptError(Message);
		return FString();
	}

	static UToolCallAsyncResultString* NewResult()
	{
		return NewObject<UToolCallAsyncResultString>();
	}

	static UToolCallAsyncResultString* ErrorResult(const FString& Message)
	{
		UToolCallAsyncResultString* Result = NewResult();
		Result->SetError(Message);
		return Result;
	}

	static FName BackendName(const FString& Backend)
	{
		return Backend.IsEmpty() ? NAME_None : FName(*Backend);
	}

	static UComfySubsystem* Comfy()
	{
		return UComfySubsystem::Get();
	}

	static UComfyJob* FindJob(const FString& JobId, FString& OutError)
	{
		UComfyJob* Job = Comfy() ? Comfy()->FindJob(JobId) : nullptr;
		if (!Job)
		{
			OutError = FString::Printf(TEXT("No job with id \"%s\" in this editor session. list_jobs shows the current ones."), *JobId);
		}
		return Job;
	}

	static bool CreditsAllowed(FName Backend, const UComfyWorkflow* Workflow, bool bAllow, bool bAlwaysSpends, FString& OutError)
	{
		const bool bSpends = bAlwaysSpends || Comfy()->SpendsCredits(Backend, Workflow);
		if (bSpends && !bAllow && GetDefault<UComfySettings>()->bAgentsMustConfirmCreditSpend)
		{
			OutError = TEXT("This run spends Comfy credits. Ask the user first, then call again with bAllowCreditSpend set to true.");
			return false;
		}
		return true;
	}

	static TSharedRef<FJsonObject> WorkflowJson(const UComfyWorkflow* Workflow, bool bDetailed)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Workflow->GetName());
		Object->SetStringField(TEXT("path"), Workflow->HasAnyFlags(RF_Transient) ? FString(TEXT("(built-in)")) : Workflow->GetPathName());
		Object->SetStringField(TEXT("display_name"), Workflow->GetDisplayNameString());
		Object->SetStringField(TEXT("category"), Workflow->Category);
		Object->SetStringField(TEXT("description"), Workflow->Description.ToString());
		Object->SetBoolField(TEXT("uses_credits"), Workflow->bUsesPartnerNodes);

		TArray<TSharedPtr<FJsonValue>> Outputs;
		for (const FComfyWorkflowOutput& Output : Workflow->Outputs)
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Output.Name);
			Entry->SetStringField(TEXT("kind"), StaticEnum<EComfyOutputKind>()->GetNameStringByValue(int64(Output.Kind)));
			Outputs.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Object->SetArrayField(TEXT("outputs"), Outputs);

		if (bDetailed)
		{
			Object->SetField(TEXT("input_schema"), Parse(Workflow->GetInputSchemaJson()));
			TArray<TSharedPtr<FJsonValue>> Missing;
			for (const FString& Node : Comfy()->GetMissingNodeClasses(NAME_None, Workflow))
			{
				Missing.Add(MakeShared<FJsonValueString>(Node));
			}
			Object->SetArrayField(TEXT("missing_nodes"), Missing);
			Object->SetBoolField(TEXT("uses_credits"), Comfy()->SpendsCredits(NAME_None, Workflow));
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> Inputs;
			for (const FString& Name : Workflow->GetInputNames())
			{
				Inputs.Add(MakeShared<FJsonValueString>(Name));
			}
			Object->SetArrayField(TEXT("inputs"), Inputs);
		}
		return Object;
	}

	/** Agents rarely pass a seed; give each run a fresh one unless they did. */
	static TMap<FString, FString> WithRandomSeed(const UComfyWorkflow* Workflow, TMap<FString, FString> Inputs)
	{
		for (const FComfyWorkflowInput& Input : Workflow->Inputs)
		{
			if (Input.Type == EComfyInputType::Seed && !Inputs.Contains(Input.Name))
			{
				Inputs.Add(Input.Name, TEXT("random"));
			}
		}
		return Inputs;
	}

	static TSharedRef<FJsonObject> MaterialJson(UMaterialInstanceConstant* Material, int32 Assigned)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("material"), Material ? Material->GetPathName() : FString());
		UTexture* BaseColor = nullptr;
		UTexture* Normal = nullptr;
		if (Material)
		{
			Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("BaseColor")), BaseColor);
			Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Normal")), Normal);
		}
		Object->SetStringField(TEXT("base_color"), BaseColor ? BaseColor->GetPathName() : FString());
		Object->SetStringField(TEXT("normal"), Normal ? Normal->GetPathName() : FString());
		Object->SetNumberField(TEXT("assigned_components"), Assigned);
		return Object;
	}

	/** Run Callback once when the job finishes or the timeout passes, whichever is first. */
	static void OnJobDone(UComfyJob* Job, float TimeoutSeconds, TFunction<void(UComfyJob*, bool /*bTimedOut*/)> Callback)
	{
		struct FState
		{
			bool bDone = false;
			FDelegateHandle FinishHandle;
			FTSTicker::FDelegateHandle TickerHandle;
		};
		TSharedRef<FState> State = MakeShared<FState>();
		TWeakObjectPtr<UComfyJob> WeakJob = Job;

		State->FinishHandle = Job->OnFinishedNative.AddLambda([State, Callback](UComfyJob* Finished)
		{
			if (!State->bDone)
			{
				State->bDone = true;
				FTSTicker::GetCoreTicker().RemoveTicker(State->TickerHandle);
				Callback(Finished, false);
			}
		});
		State->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State, WeakJob, Callback](float)
		{
			if (!State->bDone)
			{
				State->bDone = true;
				if (WeakJob.IsValid())
				{
					WeakJob->OnFinishedNative.Remove(State->FinishHandle);
				}
				Callback(WeakJob.Get(), true);
			}
			return false;
		}), TimeoutSeconds);
	}
}

using namespace ComfyToolsetPrivate;

FString UComfyToolset::ListBackends()
{
	UComfySubsystem* Subsystem = Comfy();
	if (!Subsystem)
	{
		return Error(TEXT("The Comfy subsystem isn't running."));
	}
	TArray<TSharedPtr<FJsonValue>> Backends;
	for (const FName Name : Subsystem->GetBackendNames())
	{
		FComfyBackendProfile Profile;
		Subsystem->GetBackendProfile(Name, Profile);
		const FComfyBackendStatus Status = Subsystem->GetLastStatus(Name);

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Name.ToString());
		Entry->SetStringField(TEXT("kind"), StaticEnum<EComfyBackendKind>()->GetNameStringByValue(int64(Profile.Kind)));
		Entry->SetStringField(TEXT("url"), Profile.BaseUrl);
		Entry->SetBoolField(TEXT("reachable"), Status.bReachable);
		Entry->SetStringField(TEXT("summary"), Status.Summary.IsEmpty() ? FString(TEXT("not checked yet")) : Status.Summary);
		Entry->SetBoolField(TEXT("spends_credits"), Profile.Kind != EComfyBackendKind::Local);
		Backends.Add(MakeShared<FJsonValueObject>(Entry));
	}
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("active"), Subsystem->GetActiveBackend().ToString());
	Result->SetArrayField(TEXT("backends"), Backends);
	return Write(Result);
}

UToolCallAsyncResultString* UComfyToolset::CheckBackend(const FString& Backend)
{
	UComfySubsystem* Subsystem = Comfy();
	if (!Subsystem)
	{
		return ErrorResult(TEXT("The Comfy subsystem isn't running."));
	}
	const FName Name = Backend.IsEmpty() ? Subsystem->GetActiveBackend() : FName(*Backend);
	FComfyBackendProfile Profile;
	if (!Subsystem->GetBackendProfile(Name, Profile))
	{
		return ErrorResult(FString::Printf(TEXT("No backend named \"%s\". list_backends shows the configured ones."), *Backend));
	}

	TStrongObjectPtr<UToolCallAsyncResultString> Result(NewResult());
	TSharedRef<FDelegateHandle> Handle = MakeShared<FDelegateHandle>();
	*Handle = Subsystem->OnBackendStatus.AddLambda([Result, Name, Handle](const FComfyBackendStatus& Status)
	{
		if (Status.Backend != Name)
		{
			return;
		}
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Name.ToString());
		Object->SetBoolField(TEXT("reachable"), Status.bReachable);
		Object->SetStringField(TEXT("summary"), Status.Summary);
		Result->SetValue(Write(Object));
		if (UComfySubsystem* Current = UComfySubsystem::Get())
		{
			Current->OnBackendStatus.Remove(*Handle);
		}
	});
	Subsystem->ProbeBackend(Name, FComfyBackendStatusDelegate());
	return Result.Get();
}

FString UComfyToolset::SetActiveBackend(const FString& Backend)
{
	FComfyBackendProfile Profile;
	if (!Comfy() || !Comfy()->GetBackendProfile(FName(*Backend), Profile))
	{
		return Error(FString::Printf(TEXT("No backend named \"%s\"."), *Backend));
	}
	Comfy()->SetActiveBackend(FName(*Backend), true);
	return ListBackends();
}

FString UComfyToolset::ListWorkflows(const FString& Category)
{
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const UComfyWorkflow* Workflow : UComfyEditorLibrary::GetAllWorkflows())
	{
		if (Category.IsEmpty() || Workflow->Category.Equals(Category, ESearchCase::IgnoreCase))
		{
			Items.Add(MakeShared<FJsonValueObject>(WorkflowJson(Workflow, false)));
		}
	}
	return Write(Items);
}

FString UComfyToolset::DescribeWorkflow(const FString& Workflow)
{
	const UComfyWorkflow* Found = UComfyEditorLibrary::FindWorkflow(Workflow);
	if (!Found)
	{
		return Error(FString::Printf(TEXT("No workflow named \"%s\". list_workflows shows what's available."), *Workflow));
	}
	return Write(WorkflowJson(Found, true));
}

FString UComfyToolset::RunWorkflow(const FString& Workflow, const TMap<FString, FString>& Inputs, const FString& Backend, bool bAllowCreditSpend)
{
	UComfyWorkflow* Found = UComfyEditorLibrary::FindWorkflow(Workflow);
	if (!Found)
	{
		return Error(FString::Printf(TEXT("No workflow named \"%s\". list_workflows shows what's available."), *Workflow));
	}
	for (const TPair<FString, FString>& Pair : Inputs)
	{
		FComfyWorkflowInput Input;
		if (!Found->FindInput(Pair.Key, Input))
		{
			return Error(FString::Printf(TEXT("Workflow %s has no input \"%s\". Its inputs are: %s."), *Found->GetName(), *Pair.Key, *FString::Join(Found->GetInputNames(), TEXT(", "))));
		}
	}
	FString CreditError;
	if (!CreditsAllowed(BackendName(Backend), Found, bAllowCreditSpend, false, CreditError))
	{
		return Error(CreditError);
	}
	UComfyJob* Job = Comfy()->RunWorkflow(Found, WithRandomSeed(Found, Inputs), BackendName(Backend));
	return Job ? Job->ToJson() : Error(TEXT("The job couldn't start. See the Output Log."));
}

UToolCallAsyncResultString* UComfyToolset::WaitForJob(const FString& JobId, int32 TimeoutSeconds)
{
	FString Message;
	UComfyJob* Job = FindJob(JobId, Message);
	if (!Job)
	{
		return ErrorResult(Message);
	}
	UToolCallAsyncResultString* Result = NewResult();
	if (Job->IsFinished())
	{
		Result->SetValue(Job->ToJson());
		return Result;
	}
	TStrongObjectPtr<UToolCallAsyncResultString> Held(Result);
	OnJobDone(Job, float(FMath::Clamp(TimeoutSeconds, 1, 600)), [Held](UComfyJob* Done, bool)
	{
		Held->SetValue(Done ? Done->ToJson() : FString(TEXT("{\"error\":\"The job is gone.\"}")));
	});
	return Result;
}

FString UComfyToolset::GetJob(const FString& JobId)
{
	FString Message;
	const UComfyJob* Job = FindJob(JobId, Message);
	return Job ? Job->ToJson() : Error(Message);
}

FString UComfyToolset::ListJobs()
{
	TArray<TSharedPtr<FJsonValue>> Items;
	if (Comfy())
	{
		TArray<UComfyJob*> Jobs = Comfy()->GetJobs();
		for (int32 Index = Jobs.Num() - 1; Index >= 0; --Index)
		{
			Items.Add(Parse(Jobs[Index]->ToJson()));
		}
	}
	return Write(Items);
}

FString UComfyToolset::CancelJob(const FString& JobId)
{
	FString Message;
	UComfyJob* Job = FindJob(JobId, Message);
	if (!Job)
	{
		return Error(Message);
	}
	Job->Cancel();
	return Job->ToJson();
}

FString UComfyToolset::ImportJobOutputs(const FString& JobId, const FString& DestinationPath)
{
	FString Message;
	UComfyJob* Job = FindJob(JobId, Message);
	if (!Job)
	{
		return Error(Message);
	}
	if (!Job->Succeeded())
	{
		return Error(FString::Printf(TEXT("Job %s is %s, not succeeded."), *JobId, *Job->GetStateText()));
	}
	TArray<TSharedPtr<FJsonValue>> Assets;
	for (const UObject* Asset : UComfyEditorLibrary::ImportJobOutputs(Job, DestinationPath, true))
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Asset->GetPathName());
		Entry->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
		Assets.Add(MakeShared<FJsonValueObject>(Entry));
	}
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("assets"), Assets);
	return Write(Result);
}

FString UComfyToolset::CreateMaterialFromJob(const FString& JobId, const FString& DestinationPath, const FString& MaterialName, bool bAssignToSelection)
{
	FString Message;
	UComfyJob* Job = FindJob(JobId, Message);
	if (!Job)
	{
		return Error(Message);
	}
	FComfyMaterialOptions Options;
	UMaterialInstanceConstant* Material = UComfyEditorLibrary::CreateMaterialFromJob(Job, DestinationPath, MaterialName, Options);
	if (!Material)
	{
		return Error(FString::Printf(TEXT("Couldn't build a material from job %s. It needs to have succeeded with an image output."), *JobId));
	}
	const int32 Assigned = bAssignToSelection ? UComfyEditorLibrary::AssignMaterialToSelectedActors(Material) : 0;
	return Write(MaterialJson(Material, Assigned));
}

UToolCallAsyncResultString* UComfyToolset::GenerateMaterial(const FString& Prompt, const FString& Workflow, bool bAssignToSelection, bool bAllowCreditSpend)
{
	UComfyWorkflow* Found = nullptr;
	if (Workflow.IsEmpty())
	{
		for (UComfyWorkflow* Candidate : UComfyEditorLibrary::GetAllWorkflows())
		{
			if (Candidate->Category.Equals(TEXT("Textures"), ESearchCase::IgnoreCase))
			{
				Found = Candidate;
				break;
			}
		}
	}
	else
	{
		Found = UComfyEditorLibrary::FindWorkflow(Workflow);
	}
	if (!Found)
	{
		return ErrorResult(TEXT("No texture workflow found. Pass one from list_workflows."));
	}
	FComfyWorkflowInput PromptInput;
	if (!Found->FindInput(TEXT("prompt"), PromptInput))
	{
		return ErrorResult(FString::Printf(TEXT("Workflow %s has no \"prompt\" input."), *Found->GetName()));
	}
	FString CreditError;
	if (!CreditsAllowed(NAME_None, Found, bAllowCreditSpend, false, CreditError))
	{
		return ErrorResult(CreditError);
	}

	UComfyJob* Job = Comfy()->RunWorkflow(Found, WithRandomSeed(Found, { { TEXT("prompt"), Prompt } }), NAME_None);
	if (!Job)
	{
		return ErrorResult(TEXT("The job couldn't start. See the Output Log."));
	}

	UToolCallAsyncResultString* Result = NewResult();
	TStrongObjectPtr<UToolCallAsyncResultString> Held(Result);
	const float Timeout = GetDefault<UComfySettings>()->JobTimeoutMinutes * 60.f;
	auto Finish = [Held, Prompt, bAssignToSelection](UComfyJob* Done, bool bTimedOut)
	{
		if (!Done || bTimedOut || !Done->Succeeded())
		{
			Held->SetError(Done ? FString::Printf(TEXT("The texture job %s %s: %s"), *Done->JobId, bTimedOut ? TEXT("timed out") : TEXT("failed"), *Done->ErrorMessage) : FString(TEXT("The job is gone.")));
			return;
		}
		FComfyMaterialOptions Options;
		UMaterialInstanceConstant* Material = UComfyEditorLibrary::CreateMaterialFromJob(Done, FString(), Prompt, Options);
		if (!Material)
		{
			Held->SetError(TEXT("The job finished but building the material failed. See the Output Log."));
			return;
		}
		const int32 Assigned = bAssignToSelection ? UComfyEditorLibrary::AssignMaterialToSelectedActors(Material) : 0;
		TSharedRef<FJsonObject> Object = MaterialJson(Material, Assigned);
		Object->SetStringField(TEXT("job"), Done->JobId);
		Held->SetValue(Write(Object));
	};
	if (Job->IsFinished())
	{
		Finish(Job, false);
	}
	else
	{
		OnJobDone(Job, Timeout, Finish);
	}
	return Result;
}

FString UComfyToolset::ImportWorkflow(const FString& FilePath, const FString& DestinationPath)
{
	FString Message;
	const UComfyWorkflow* Workflow = UComfyEditorLibrary::ImportWorkflowFromFile(FilePath, DestinationPath, Message);
	return Workflow ? Write(WorkflowJson(Workflow, true)) : Error(Message);
}

UToolCallAsyncResultString* UComfyToolset::ListRouterModels(const FString& Cursor)
{
	UToolCallAsyncResultString* Result = NewResult();
	TStrongObjectPtr<UToolCallAsyncResultString> Held(Result);
	Comfy()->ListRouterModelsNative(Cursor, [Held](bool bOk, const FString& Text) { bOk ? Held->SetValue(Text) : Held->SetError(Text); });
	return Result;
}

UToolCallAsyncResultString* UComfyToolset::DescribeRouterModel(const FString& ModelId)
{
	UToolCallAsyncResultString* Result = NewResult();
	TStrongObjectPtr<UToolCallAsyncResultString> Held(Result);
	Comfy()->GetRouterModelSchemaNative(ModelId, [Held](bool bOk, const FString& Text) { bOk ? Held->SetValue(Text) : Held->SetError(Text); });
	return Result;
}

FString UComfyToolset::RunRouterModel(const FString& ModelId, const FString& RequestJson, bool bAllowCreditSpend)
{
	FString CreditError;
	if (!CreditsAllowed(NAME_None, nullptr, bAllowCreditSpend, true, CreditError))
	{
		return Error(CreditError);
	}
	UComfyJob* Job = Comfy()->RunRouterModel(ModelId, RequestJson, NAME_None);
	return Job ? Job->ToJson() : Error(TEXT("The Router call couldn't start. See the Output Log."));
}
