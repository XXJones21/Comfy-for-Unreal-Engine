// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
class IPropertyUtilities;
class FAssetThumbnailPool;
class UComfyWorkflow;
struct FComfyInputs;
struct FComfyWorkflowInput;

/**
 * Details panel for Comfy Inputs: pick a workflow and get one row per input, as in the Run view. A row left alone
 * isn't sent, so the workflow's value applies; the reset arrow clears a row. Names the workflow doesn't have are
 * listed so they can be removed. Without a workflow, the raw Values and Assets maps show instead.
 */
class FComfyInputsCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	/** The first edited struct, for showing values. Null when there's none. */
	const FComfyInputs* GetInputs() const;

	/** The workflow the edited inputs are for, or null when there's none or several differ. */
	UComfyWorkflow* GetWorkflow() const;

	/** Change every edited struct, with undo. */
	void Edit(const FText& Description, TFunctionRef<void(FComfyInputs&)> Change);

	void SetValue(const FString& Name, const FString& Value);
	void SetAsset(const FString& Name, UObject* Asset);
	void Clear(const FString& Name);
	bool IsSet(const FString& Name) const;
	FString GetValue(const FString& Name) const;

	TSharedRef<SWidget> MakeValueWidget(const UComfyWorkflow* Workflow, const FComfyWorkflowInput& Input);
	TSharedRef<SWidget> MakeChoiceWidget(const FComfyWorkflowInput& Input, const TArray<FString>& Choices);
	TSharedRef<SWidget> MakeResetButton(const FString& Name);

	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyUtilities> Utilities;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;

	/** Option lists the dropdowns point at; they must outlive the widgets. */
	TArray<TSharedPtr<TArray<TSharedPtr<FString>>>> OptionLists;
};
