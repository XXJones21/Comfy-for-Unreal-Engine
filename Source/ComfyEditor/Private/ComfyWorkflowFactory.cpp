// Copyright Comfy Org. All Rights Reserved.

#include "ComfyWorkflowFactory.h"
#include "ComfyWorkflow.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UComfyWorkflowFactory::UComfyWorkflowFactory()
{
	SupportedClass = UComfyWorkflow::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
	bText = true;
	Formats.Add(TEXT("json;ComfyUI workflow (API format)"));
	ImportPriority = DefaultImportPriority + 10;
}

bool UComfyWorkflowFactory::FactoryCanImport(const FString& Filename)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Filename))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	// Canvas saves are claimed too, so the import fails with advice instead of going to another importer.
	if (Root->HasField(TEXT("nodes")) && Root->HasField(TEXT("links")))
	{
		return true;
	}
	const TSharedPtr<FJsonObject>* Wrapped = nullptr;
	if (Root->TryGetObjectField(TEXT("prompt"), Wrapped) && Wrapped)
	{
		Root = *Wrapped;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Root->Values)
	{
		const TSharedPtr<FJsonObject>* Node = nullptr;
		if (Pair.Value.IsValid() && Pair.Value->TryGetObject(Node) && Node && (*Node)->HasField(TEXT("class_type")))
		{
			return true;
		}
	}
	return false;
}

UObject* UComfyWorkflowFactory::FactoryCreateText(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const TCHAR*& Buffer, const TCHAR* BufferEnd, FFeedbackContext* Warn)
{
	const FString Json(UE_PTRDIFF_TO_INT32(BufferEnd - Buffer), Buffer);

	UComfyWorkflow* Workflow = FindObject<UComfyWorkflow>(InParent, *InName.ToString());
	const bool bExisting = Workflow != nullptr;
	if (!Workflow)
	{
		Workflow = NewObject<UComfyWorkflow>(InParent, InClass, InName, Flags | RF_Transactional);
	}

	FString Error;
	// Re-importing keeps a hand-edited manifest unless the graph's nodes changed.
	const FString OldHash = Workflow->GraphHash;
	if (!Workflow->SetGraphFromJson(Json, !bExisting, Error))
	{
		Warn->Logf(ELogVerbosity::Error, TEXT("%s"), *Error);
		return bExisting ? Workflow : nullptr;
	}
	if (bExisting && OldHash != Workflow->GraphHash && Workflow->Inputs.Num() == 0)
	{
		Workflow->DetectInterface();
	}
	Workflow->SourceFile = UFactory::GetCurrentFilename();
	if (Workflow->DisplayName.IsEmpty())
	{
		Workflow->DisplayName = FText::FromString(FName::NameToDisplayString(InName.ToString(), false));
	}
	return Workflow;
}

bool UComfyWorkflowFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	const UComfyWorkflow* Workflow = Cast<UComfyWorkflow>(Obj);
	if (Workflow && !Workflow->SourceFile.IsEmpty())
	{
		OutFilenames.Add(Workflow->SourceFile);
		return true;
	}
	return false;
}

void UComfyWorkflowFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	if (UComfyWorkflow* Workflow = Cast<UComfyWorkflow>(Obj); Workflow && NewReimportPaths.Num() == 1)
	{
		Workflow->Modify();
		Workflow->SourceFile = NewReimportPaths[0];
	}
}

EReimportResult::Type UComfyWorkflowFactory::Reimport(UObject* Obj)
{
	UComfyWorkflow* Workflow = Cast<UComfyWorkflow>(Obj);
	FString Json;
	if (!Workflow || !FFileHelper::LoadFileToString(Json, *Workflow->SourceFile))
	{
		return EReimportResult::Failed;
	}
	FString Error;
	if (!Workflow->SetGraphFromJson(Json, false, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("Reimport of %s failed: %s"), *Workflow->GetName(), *Error);
		return EReimportResult::Failed;
	}
	Workflow->MarkPackageDirty();
	return EReimportResult::Succeeded;
}
