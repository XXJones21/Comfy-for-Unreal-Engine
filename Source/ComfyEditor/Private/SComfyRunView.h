// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComfyTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class SHorizontalBox;
class SVerticalBox;
class SWidgetSwitcher;
class UComfyJob;
class UComfyWorkflow;
class UMaterialInterface;
class UMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;
struct FSlateBrush;

DECLARE_DELEGATE_OneParam(FOnComfyOpenInGraph, const UComfyWorkflow*);

/**
 * Comfy Studio's Run view, laid out like ComfyUI's App mode: a workflow picker, input cards and a Run
 * button down the left, and an output gallery on the right with per-result actions for the project.
 */
class SComfyRunView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SComfyRunView) {}
		SLATE_EVENT(FOnComfyOpenInGraph, OnOpenInGraph)
		/** Show the Graph view, where Comfy Cloud's sign-in lives. */
		SLATE_EVENT(FSimpleDelegate, OnShowGraph)
		/** Check the active backend again. */
		SLATE_EVENT(FSimpleDelegate, OnCheckBackend)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SComfyRunView() override;

	void RefreshWorkflows();
	void HandleBackendStatus(const FComfyBackendStatus& Status);
	UComfyWorkflow* GetSelectedWorkflow() const { return SelectedWorkflow.Get(); }

private:
	// Left column
	TSharedRef<SWidget> BuildPicker();
	TSharedRef<SWidget> BuildPickerMenu();
	TSharedRef<SWidget> BuildOptionsCard();
	TSharedRef<SWidget> BuildRunBar();
	void SelectWorkflow(UComfyWorkflow* Workflow);
	void RebuildForm();
	TSharedRef<SWidget> MakeInputCard(const FComfyWorkflowInput& Input);
	TSharedRef<SWidget> MakeInputWidget(const FComfyWorkflowInput& Input);
	TSharedRef<SWidget> MakeFileInput(const FComfyWorkflowInput& Input);
	void SetFileFromAsset(const FString& Name, UObject* Asset);
	bool CanRun() const;
	FReply OnRun();

	// Main area
	void RebuildEmptyState();
	/** Shown above the library while the active backend isn't connected: what's wrong and what to do. */
	TSharedPtr<SWidget> MakeWelcomeCard() const;
	TSharedRef<SWidget> BuildGallery();
	void RebuildStrip();
	TSharedRef<SWidget> MakeJobTile(UComfyJob* Job);
	void SelectJob(UComfyJob* Job);
	const FSlateBrush* GetJobBrush(const UComfyJob* Job);
	FText GetPreviewText() const;

	// Result actions
	FReply OnImport();
	FReply OnReplace();
	FReply OnRevert();
	FReply OnShowInContentBrowser();
	FReply OnOpenInGraphClicked();
	TArray<UObject*> ImportJob(UComfyJob* Job);
	void HandleJobFinished(UComfyJob* Job);
	void SetStatus(const FText& Text, bool bError);

	static FString TypeOf(const UComfyWorkflow* Workflow);
	static FString OriginOf(const UComfyWorkflow* Workflow);

	FOnComfyOpenInGraph OnOpenInGraph;
	FSimpleDelegate OnShowGraph;
	FSimpleDelegate OnCheckBackend;

	/** The active backend's last status, for the welcome card. */
	TOptional<FComfyBackendStatus> ActiveStatus;
	/** What the welcome card last showed, so it's only rebuilt when that changes. */
	FString WelcomeKey;
	/** The option lists the form was last built with. */
	FString FormOptionsSignature;

	TArray<TWeakObjectPtr<UComfyWorkflow>> Workflows;
	TWeakObjectPtr<UComfyWorkflow> SelectedWorkflow;
	TMap<FString, FString> InputValues;
	/** Seed inputs that get a new seed on every run. */
	TSet<FString> RandomSeeds;
	/** Option lists for combo boxes, which keep a pointer to them. */
	TArray<TSharedPtr<TArray<TSharedPtr<FString>>>> OptionLists;

	int32 BatchCount = 1;
	FString DestinationFolder;
	bool bAutoImport = true;
	bool bCreateMaterial = true;
	bool bMakeTileable = true;

	TSharedPtr<SVerticalBox> FormBox;
	TSharedPtr<SWidgetSwitcher> MainSwitcher;
	TSharedPtr<SVerticalBox> EmptyStateBox;
	TSharedPtr<SHorizontalBox> Strip;

	TWeakObjectPtr<UComfyJob> SelectedJob;
	FText Status;
	bool bStatusIsError = false;

	/** What each job put in the project, for Show in Content Browser and Replace. Keyed by job id. */
	TMap<FString, TArray<TWeakObjectPtr<UObject>>> ImportedAssets;

	struct FPreviousMaterial
	{
		TWeakObjectPtr<UMeshComponent> Component;
		int32 Slot = 0;
		TWeakObjectPtr<UMaterialInterface> Material;
	};
	struct FPreviousMesh
	{
		TWeakObjectPtr<UStaticMeshComponent> Component;
		TWeakObjectPtr<UStaticMesh> Mesh;
	};
	struct FReplacement
	{
		TArray<FPreviousMaterial> Materials;
		TArray<FPreviousMesh> Meshes;
	};
	/** What Replace on selection changed, so Revert can put it back. Keyed by job id. */
	TMap<FString, FReplacement> Replacements;

	struct FPreview
	{
		TStrongObjectPtr<UTexture2D> Texture;
		TSharedPtr<FSlateBrush> Brush;
	};
	/** Result images loaded for the gallery, keyed by file path. */
	TMap<FString, FPreview> Previews;

	FDelegateHandle JobAddedHandle;
};
