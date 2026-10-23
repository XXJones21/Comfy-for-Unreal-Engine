// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "EditorReimportHandler.h"
#include "ComfyWorkflowFactory.generated.h"

/**
 * Imports a ComfyUI API-format workflow (Export (API) in ComfyUI) as a Comfy Workflow asset.
 * Only claims .json files that look like API-format graphs, so other JSON importers still work.
 */
UCLASS()
class UComfyWorkflowFactory : public UFactory, public FReimportHandler
{
	GENERATED_BODY()

public:
	UComfyWorkflowFactory();

	virtual bool FactoryCanImport(const FString& Filename) override;
	virtual UObject* FactoryCreateText(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const TCHAR*& Buffer, const TCHAR* BufferEnd, FFeedbackContext* Warn) override;

	virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames) override;
	virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths) override;
	virtual EReimportResult::Type Reimport(UObject* Obj) override;
	virtual int32 GetPriority() const override { return ImportPriority; }
};
