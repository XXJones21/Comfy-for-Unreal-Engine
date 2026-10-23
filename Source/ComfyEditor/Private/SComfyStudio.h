// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "ComfyTypes.h"

class UComfyJob;
class UComfyWorkflow;
class SVerticalBox;
template <typename OptionType> class SComboBox;

/**
 * The Comfy Studio tab: pick a backend and a workflow, fill in its inputs, run it, and
 * watch jobs land in the project as textures, materials, sounds and meshes.
 */
class SComfyStudio : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SComfyStudio) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SComfyStudio() override;

private:
	struct FWorkflowItem
	{
		TWeakObjectPtr<UComfyWorkflow> Workflow;
	};
	struct FJobItem
	{
		TWeakObjectPtr<UComfyJob> Job;
	};

	// Backend bar
	TSharedRef<SWidget> BuildBackendBar();
	void RefreshBackends();
	void CheckActiveBackend();
	FText GetStatusText() const;
	FSlateColor GetStatusColor() const;

	// Workflow library
	TSharedRef<SWidget> BuildLibrary();
	void RefreshWorkflows();
	TSharedRef<ITableRow> MakeWorkflowRow(TSharedPtr<FWorkflowItem> Item, const TSharedRef<STableViewBase>& Owner);
	void OnWorkflowSelected(TSharedPtr<FWorkflowItem> Item, ESelectInfo::Type SelectInfo);
	FReply OnImportWorkflowClicked();

	// Inputs form
	TSharedRef<SWidget> BuildRunPanel();
	void RebuildForm();
	TSharedRef<SWidget> MakeInputWidget(const FComfyWorkflowInput& Input);
	FText GetWarningText() const;
	bool CanRun() const;
	FReply OnRunClicked();

	// Jobs
	TSharedRef<SWidget> BuildJobList();
	void RefreshJobs();
	TSharedRef<ITableRow> MakeJobRow(TSharedPtr<FJobItem> Item, const TSharedRef<STableViewBase>& Owner);
	void HandleJobFinished(UComfyJob* Job, bool bImport, bool bMaterial);

	UComfyWorkflow* GetSelectedWorkflow() const { return SelectedWorkflow.Get(); }
	bool SelectedMakesImages() const;

	TArray<TSharedPtr<FName>> BackendOptions;
	TSharedPtr<SComboBox<TSharedPtr<FName>>> BackendCombo;

	TArray<TSharedPtr<FWorkflowItem>> WorkflowItems;
	TSharedPtr<SListView<TSharedPtr<FWorkflowItem>>> WorkflowList;
	TWeakObjectPtr<UComfyWorkflow> SelectedWorkflow;

	TSharedPtr<SVerticalBox> FormBox;
	TMap<FString, FString> InputValues;
	/** Option lists for enum inputs; held here because combo boxes keep a pointer to them. */
	TArray<TSharedPtr<TArray<TSharedPtr<FString>>>> EnumOptionLists;

	FString DestinationFolder;
	bool bImportResults = true;
	bool bCreateMaterial = true;
	bool bMakeTileable = true;
	bool bAssignToSelection = false;

	TArray<TSharedPtr<FJobItem>> JobItems;
	TSharedPtr<SListView<TSharedPtr<FJobItem>>> JobList;

	FDelegateHandle JobAddedHandle;
	FDelegateHandle StatusHandle;
	FDelegateHandle ActiveBackendHandle;
};
