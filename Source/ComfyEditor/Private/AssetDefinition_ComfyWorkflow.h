// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "ComfyWorkflow.h"
#include "AssetDefinition_ComfyWorkflow.generated.h"

UCLASS()
class UAssetDefinition_ComfyWorkflow : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override { return NSLOCTEXT("ComfyEditor", "WorkflowAsset", "Comfy Workflow"); }
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(242, 196, 66)); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UComfyWorkflow::StaticClass(); }
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override
	{
		static const FAssetCategoryPath Categories[] = { FAssetCategoryPath(NSLOCTEXT("ComfyEditor", "Category", "Comfy")) };
		return Categories;
	}
};
