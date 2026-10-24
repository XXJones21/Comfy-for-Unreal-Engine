// Copyright Comfy Org. All Rights Reserved.

#include "SComfyStudio.h"
#include "ComfyEditorLibrary.h"
#include "ComfyJob.h"
#include "ComfySettings.h"
#include "ComfySubsystem.h"
#include "ComfyWorkflow.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "ISettingsModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ComfyStudio"

namespace ComfyStudioPrivate
{
	static void Notify(const FText& Text, bool bSuccess)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = bSuccess ? 4.f : 8.f;
		Info.bUseSuccessFailIcons = true;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	static TSharedRef<SWidget> SectionHeader(const FText& Text)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(FAppStyle::GetFontStyle("BoldFont"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground());
	}
}

TWeakPtr<SComfyStudio> SComfyStudio::Instance;

void SComfyStudio::SetActiveView(const FString& View)
{
	if (const TSharedPtr<SComfyStudio> Studio = Instance.Pin())
	{
		Studio->ShowView(View);
	}
}

void SComfyStudio::ShowView(const FString& View)
{
}

void SComfyStudio::Construct(const FArguments& InArgs)
{
	DestinationFolder = GetDefault<UComfySettings>()->DefaultImportFolder;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(6.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				BuildBackendBar()
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Vertical)
				+ SSplitter::Slot().Value(0.68f)
				[
					SNew(SSplitter)
					+ SSplitter::Slot().Value(0.32f)
					[
						BuildLibrary()
					]
					+ SSplitter::Slot().Value(0.68f)
					[
						BuildRunPanel()
					]
				]
				+ SSplitter::Slot().Value(0.32f)
				[
					BuildJobList()
				]
			]
		]
	];

	if (UComfySubsystem* Comfy = UComfySubsystem::Get())
	{
		JobAddedHandle = Comfy->OnJobAdded.AddSPLambda(this, [this](UComfyJob*) { RefreshJobs(); });
		StatusHandle = Comfy->OnBackendStatus.AddSPLambda(this, [this](const FComfyBackendStatus& Status) { HandleBackendStatus(Status); });
		ActiveBackendHandle = Comfy->OnActiveBackendChanged.AddSP(this, &SComfyStudio::RefreshBackends);
	}

	RefreshBackends();
	RefreshWorkflows();
	RefreshJobs();
	CheckActiveBackend();
	Instance = SharedThis(this);
}

SComfyStudio::~SComfyStudio()
{
	if (UComfySubsystem* Comfy = UComfySubsystem::Get())
	{
		Comfy->OnJobAdded.Remove(JobAddedHandle);
		Comfy->OnBackendStatus.Remove(StatusHandle);
		Comfy->OnActiveBackendChanged.Remove(ActiveBackendHandle);
	}
}

// Backend bar ------------------------------------------------------------------------------

TSharedRef<SWidget> SComfyStudio::BuildBackendBar()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
		[
			SNew(STextBlock).Text(LOCTEXT("Backend", "Backend"))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).MinDesiredWidth(180.f)
			[
				SAssignNew(BackendCombo, SComboBox<TSharedPtr<FName>>)
				.OptionsSource(&BackendOptions)
				.OnGenerateWidget_Lambda([](TSharedPtr<FName> Item)
				{
					return SNew(STextBlock).Text(FText::FromName(Item.IsValid() ? *Item : NAME_None));
				})
				.OnSelectionChanged_Lambda([this](TSharedPtr<FName> Item, ESelectInfo::Type Type)
				{
					if (Item.IsValid() && Type != ESelectInfo::Direct)
					{
						UComfySubsystem::Get()->SetActiveBackend(*Item, true);
						CheckActiveBackend();
					}
				})
				[
					SNew(STextBlock).Text_Lambda([]()
					{
						const UComfySubsystem* Comfy = UComfySubsystem::Get();
						return Comfy ? FText::FromName(Comfy->GetActiveBackend()) : FText::GetEmpty();
					})
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 4, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("●")))
			.ColorAndOpacity(this, &SComfyStudio::GetStatusColor)
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(this, &SComfyStudio::GetStatusText)
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("Check", "Check"))
			.ToolTipText(LOCTEXT("CheckTooltip", "Check that the backend answers and accepts your key."))
			.OnClicked_Lambda([this]() { CheckActiveBackend(); return FReply::Handled(); })
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("Settings", "Settings"))
			.ToolTipText(LOCTEXT("SettingsTooltip", "Backends live in Project Settings > Plugins > Comfy. Your API key lives in Editor Preferences > Plugins > Comfy."))
			.OnClicked_Lambda([]()
			{
				FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Plugins", "ComfySettings");
				return FReply::Handled();
			})
		];
}

void SComfyStudio::RefreshBackends()
{
	BackendOptions.Reset();
	const UComfySubsystem* Comfy = UComfySubsystem::Get();
	if (!Comfy)
	{
		return;
	}
	TSharedPtr<FName> Active;
	for (const FName Name : Comfy->GetBackendNames())
	{
		TSharedPtr<FName> Option = MakeShared<FName>(Name);
		if (Name == Comfy->GetActiveBackend())
		{
			Active = Option;
		}
		BackendOptions.Add(Option);
	}
	if (BackendCombo.IsValid())
	{
		BackendCombo->RefreshOptions();
		BackendCombo->SetSelectedItem(Active);
	}
}

void SComfyStudio::HandleBackendStatus(const FComfyBackendStatus& Status)
{
	const UComfySubsystem* Comfy = UComfySubsystem::Get();
	const UComfyWorkflow* Workflow = GetSelectedWorkflow();
	if (!Comfy || !Workflow || Status.Backend != Comfy->GetActiveBackend())
	{
		return;
	}
	// New model lists: keep a choice the backend has, otherwise take its best match.
	for (const FComfyWorkflowInput& Input : Workflow->Inputs)
	{
		if (Input.Type != EComfyInputType::Model)
		{
			continue;
		}
		const TArray<FString> Options = Comfy->GetInputOptions(NAME_None, Workflow, Input.Name);
		const FString* Current = InputValues.Find(Input.Name);
		if (!Current || !Options.Contains(*Current))
		{
			InputValues.Add(Input.Name, Comfy->GetDefaultInputValue(NAME_None, Workflow, Input.Name));
		}
	}
	RebuildForm();
}

void SComfyStudio::CheckActiveBackend()
{
	if (UComfySubsystem* Comfy = UComfySubsystem::Get())
	{
		Comfy->ProbeBackend(Comfy->GetActiveBackend(), FComfyBackendStatusDelegate());
	}
}

FText SComfyStudio::GetStatusText() const
{
	const UComfySubsystem* Comfy = UComfySubsystem::Get();
	if (!Comfy)
	{
		return FText::GetEmpty();
	}
	const FComfyBackendStatus Status = Comfy->GetLastStatus(Comfy->GetActiveBackend());
	return Status.Summary.IsEmpty() ? LOCTEXT("Checking", "Checking...") : FText::FromString(Status.Summary);
}

FSlateColor SComfyStudio::GetStatusColor() const
{
	const UComfySubsystem* Comfy = UComfySubsystem::Get();
	if (!Comfy)
	{
		return FSlateColor::UseSubduedForeground();
	}
	const FComfyBackendStatus Status = Comfy->GetLastStatus(Comfy->GetActiveBackend());
	if (Status.Summary.IsEmpty())
	{
		return FSlateColor::UseSubduedForeground();
	}
	return Status.bReachable ? FLinearColor(0.2f, 0.8f, 0.3f) : FLinearColor(0.9f, 0.25f, 0.2f);
}

// Workflow library -------------------------------------------------------------------------

TSharedRef<SWidget> SComfyStudio::BuildLibrary()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				ComfyStudioPrivate::SectionHeader(LOCTEXT("Workflows", "WORKFLOWS"))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("ImportJson", "Import JSON..."))
				.ToolTipText(LOCTEXT("ImportJsonTooltip", "Import a workflow saved with Export (API) in ComfyUI."))
				.OnClicked(this, &SComfyStudio::OnImportWorkflowClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "Refresh"))
				.OnClicked_Lambda([this]() { RefreshWorkflows(); return FReply::Handled(); })
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SAssignNew(WorkflowList, SListView<TSharedPtr<FWorkflowItem>>)
			.ListItemsSource(&WorkflowItems)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SComfyStudio::MakeWorkflowRow)
			.OnSelectionChanged(this, &SComfyStudio::OnWorkflowSelected)
		];
}

void SComfyStudio::RefreshWorkflows()
{
	WorkflowItems.Reset();
	TArray<UComfyWorkflow*> Workflows = UComfyEditorLibrary::GetAllWorkflows();
	Workflows.StableSort([](const UComfyWorkflow& A, const UComfyWorkflow& B)
	{
		return A.Category == B.Category ? A.GetDisplayNameString() < B.GetDisplayNameString() : A.Category < B.Category;
	});
	TSharedPtr<FWorkflowItem> Previous;
	for (UComfyWorkflow* Workflow : Workflows)
	{
		TSharedPtr<FWorkflowItem> Item = MakeShared<FWorkflowItem>();
		Item->Workflow = Workflow;
		WorkflowItems.Add(Item);
		if (Workflow == SelectedWorkflow.Get())
		{
			Previous = Item;
		}
	}
	if (WorkflowList.IsValid())
	{
		WorkflowList->RequestListRefresh();
		if (Previous.IsValid())
		{
			WorkflowList->SetSelection(Previous);
		}
	}
}

TSharedRef<ITableRow> SComfyStudio::MakeWorkflowRow(TSharedPtr<FWorkflowItem> Item, const TSharedRef<STableViewBase>& Owner)
{
	const UComfyWorkflow* Workflow = Item->Workflow.Get();
	const bool bBuiltIn = Workflow && Workflow->HasAnyFlags(RF_Transient);
	FString Meta = Workflow ? Workflow->Category : FString();
	if (bBuiltIn)
	{
		Meta += Meta.IsEmpty() ? TEXT("Built-in") : TEXT(" · Built-in");
	}
	if (Workflow && Workflow->bUsesPartnerNodes)
	{
		Meta += TEXT(" · Uses credits");
	}

	return SNew(STableRow<TSharedPtr<FWorkflowItem>>, Owner)
		.Padding(FMargin(4, 3))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(Workflow ? Workflow->GetDisplayNameString() : TEXT("(unloaded)")))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Meta))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Font(FAppStyle::GetFontStyle("SmallFont"))
			]
		];
}

void SComfyStudio::OnWorkflowSelected(TSharedPtr<FWorkflowItem> Item, ESelectInfo::Type SelectInfo)
{
	UComfyWorkflow* Workflow = Item.IsValid() ? Item->Workflow.Get() : nullptr;
	if (Workflow == SelectedWorkflow.Get())
	{
		return;
	}
	SelectedWorkflow = Workflow;
	InputValues.Reset();
	if (Workflow)
	{
		for (const FComfyWorkflowInput& Input : Workflow->Inputs)
		{
			InputValues.Add(Input.Name, Input.Type == EComfyInputType::Seed ? FString(TEXT("random")) : UComfySubsystem::Get()->GetDefaultInputValue(NAME_None, Workflow, Input.Name));
		}
	}
	RebuildForm();
}

FReply SComfyStudio::OnImportWorkflowClicked()
{
	IDesktopPlatform* Platform = FDesktopPlatformModule::Get();
	if (!Platform)
	{
		return FReply::Handled();
	}
	TArray<FString> Files;
	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
	if (!Platform->OpenFileDialog(ParentWindow, TEXT("Import ComfyUI workflow (API format)"), FPaths::ProjectDir(), TEXT(""), TEXT("ComfyUI API workflow (*.json)|*.json"), EFileDialogFlags::Multiple, Files))
	{
		return FReply::Handled();
	}
	for (const FString& File : Files)
	{
		FString Error;
		if (UComfyWorkflow* Workflow = UComfyEditorLibrary::ImportWorkflowFromFile(File, TEXT("/Game/Comfy/Workflows"), Error))
		{
			SelectedWorkflow = nullptr;
			ComfyStudioPrivate::Notify(FText::Format(LOCTEXT("Imported", "Imported {0}"), FText::FromString(Workflow->GetDisplayNameString())), true);
		}
		else
		{
			ComfyStudioPrivate::Notify(FText::Format(LOCTEXT("ImportFailed", "Couldn't import {0}: {1}"), FText::FromString(FPaths::GetCleanFilename(File)), FText::FromString(Error)), false);
		}
	}
	RefreshWorkflows();
	return FReply::Handled();
}

// Inputs and run ---------------------------------------------------------------------------

TSharedRef<SWidget> SComfyStudio::BuildRunPanel()
{
	auto Check = [](const FText& Label, bool* Value, TAttribute<bool> Enabled = true)
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([Value]() { return *Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Value](ECheckBoxState State) { *Value = State == ECheckBoxState::Checked; })
			.IsEnabled(Enabled)
			[
				SNew(STextBlock).Text(Label)
			];
	};
	const TAttribute<bool> ImagesOnly = TAttribute<bool>::CreateLambda([this]() { return bImportResults && SelectedMakesImages(); });
	const TAttribute<bool> MaterialOnly = TAttribute<bool>::CreateLambda([this]() { return bImportResults && bCreateMaterial && SelectedMakesImages(); });

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
		.Padding(8.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
				.Text_Lambda([this]()
				{
					const UComfyWorkflow* Workflow = GetSelectedWorkflow();
					return Workflow ? FText::FromString(Workflow->GetDisplayNameString()) : LOCTEXT("PickWorkflow", "Pick a workflow on the left, or import one.");
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 4)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text_Lambda([this]()
				{
					const UComfyWorkflow* Workflow = GetSelectedWorkflow();
					return Workflow ? Workflow->Description : FText::GetEmpty();
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FLinearColor(1.f, 0.7f, 0.2f))
				.Text(this, &SComfyStudio::GetWarningText)
				.Visibility_Lambda([this]() { return GetWarningText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(FormBox, SVerticalBox)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(SSeparator)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("SaveTo", "Save to"))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]() { return FText::FromString(DestinationFolder); })
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { DestinationFolder = Text.ToString(); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
			[
				SNew(SWrapBox)
				.UseAllottedSize(true)
				.InnerSlotPadding(FVector2D(12.f, 4.f))
				+ SWrapBox::Slot()[ Check(LOCTEXT("ImportResults", "Import results"), &bImportResults) ]
				+ SWrapBox::Slot()[ Check(LOCTEXT("CreateMaterial", "Create material"), &bCreateMaterial, ImagesOnly) ]
				+ SWrapBox::Slot()[ Check(LOCTEXT("MakeTileable", "Make tileable + normal map"), &bMakeTileable, MaterialOnly) ]
				+ SWrapBox::Slot()[ Check(LOCTEXT("AssignSelection", "Assign to selected actors"), &bAssignToSelection, MaterialOnly) ]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, 8, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.IsEnabled(this, &SComfyStudio::CanRun)
				.OnClicked(this, &SComfyStudio::OnRunClicked)
				.ContentPadding(FMargin(16, 4))
				[
					SNew(STextBlock).Text(LOCTEXT("Run", "Run"))
				]
			]
		];
}

void SComfyStudio::RebuildForm()
{
	FormBox->ClearChildren();
	EnumOptionLists.Reset();

	const UComfyWorkflow* Workflow = GetSelectedWorkflow();
	if (!Workflow)
	{
		return;
	}
	if (Workflow->Inputs.Num() == 0)
	{
		FormBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT("NoInputs", "This workflow has no exposed inputs. It runs exactly as exported. Open the asset to add inputs to its manifest."))
		];
		return;
	}
	for (const FComfyWorkflowInput& Input : Workflow->Inputs)
	{
		FormBox->AddSlot().AutoHeight().Padding(0, 3)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 3, 8, 0)
			[
				SNew(SBox).WidthOverride(120.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Input.Label.IsEmpty() ? Input.Name : Input.Label))
					.ToolTipText(FText::FromString(Input.Tooltip.IsEmpty() ? FString::Printf(TEXT("%s on node %s"), *Input.InputName, *Input.NodeId) : Input.Tooltip))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				MakeInputWidget(Input)
			]
		];
	}
}

TSharedRef<SWidget> SComfyStudio::MakeInputWidget(const FComfyWorkflowInput& Input)
{
	const FString Name = Input.Name;
	auto Getter = [this, Name]() { const FString* Value = InputValues.Find(Name); return FText::FromString(Value ? *Value : FString()); };
	auto Setter = [this, Name](const FText& Text) { InputValues.Add(Name, Text.ToString()); };

	switch (Input.Type)
	{
	case EComfyInputType::Text:
		return SNew(SBox).MinDesiredHeight(64.f).MaxDesiredHeight(160.f)
			[
				SNew(SMultiLineEditableTextBox)
				.AutoWrapText(true)
				.Text_Lambda(Getter)
				.OnTextChanged_Lambda(Setter)
			];

	case EComfyInputType::Bool:
		return SNew(SCheckBox)
			.IsChecked_Lambda([this, Name]()
			{
				const FString* Value = InputValues.Find(Name);
				return Value && Value->ToBool() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, Name](ECheckBoxState State) { InputValues.Add(Name, State == ECheckBoxState::Checked ? TEXT("true") : TEXT("false")); });

	case EComfyInputType::Seed:
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda(Getter)
				.HintText(LOCTEXT("SeedHint", "random"))
				.OnTextChanged_Lambda(Setter)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Random", "Random"))
				.ToolTipText(LOCTEXT("RandomTooltip", "Use a new seed on every run."))
				.OnClicked_Lambda([this, Name]() { InputValues.Add(Name, TEXT("random")); return FReply::Handled(); })
			];

	case EComfyInputType::File:
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda(Getter)
				.HintText(LOCTEXT("FileHint", "Path to a file on disk"))
				.OnTextChanged_Lambda(Setter)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("...")))
				.OnClicked_Lambda([this, Name]()
				{
					TArray<FString> Files;
					IDesktopPlatform* Platform = FDesktopPlatformModule::Get();
					const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
					if (Platform && Platform->OpenFileDialog(ParentWindow, TEXT("Choose input file"), FPaths::ProjectDir(), TEXT(""), TEXT("All files (*.*)|*.*"), EFileDialogFlags::None, Files) && Files.Num() > 0)
					{
						InputValues.Add(Name, FPaths::ConvertRelativePathToFull(Files[0]));
					}
					return FReply::Handled();
				})
			];

	case EComfyInputType::Model:
	case EComfyInputType::Enum:
	{
		// Model choices come from the backend; enum choices from the manifest.
		const TArray<FString> Choices = Input.Type == EComfyInputType::Model
			? UComfySubsystem::Get()->GetInputOptions(NAME_None, GetSelectedWorkflow(), Input.Name)
			: Input.Options;
		if (Choices.Num() > 0)
		{
			TSharedPtr<TArray<TSharedPtr<FString>>> Options = MakeShared<TArray<TSharedPtr<FString>>>();
			for (const FString& Option : Choices)
			{
				Options->Add(MakeShared<FString>(Option));
			}
			EnumOptionLists.Add(Options);
			return SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(Options.Get())
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) { return SNew(STextBlock).Text(FText::FromString(*Item)); })
				.OnSelectionChanged_Lambda([this, Name](TSharedPtr<FString> Item, ESelectInfo::Type) { if (Item.IsValid()) { InputValues.Add(Name, *Item); } })
				[
					SNew(STextBlock).Text_Lambda(Getter)
				];
		}
	}
		[[fallthrough]];

	default:
		return SNew(SEditableTextBox)
			.Text_Lambda(Getter)
			.OnTextChanged_Lambda(Setter);
	}
}

bool SComfyStudio::SelectedMakesImages() const
{
	const UComfyWorkflow* Workflow = GetSelectedWorkflow();
	if (!Workflow)
	{
		return false;
	}
	for (const FComfyWorkflowOutput& Output : Workflow->Outputs)
	{
		if (Output.Kind == EComfyOutputKind::Image)
		{
			return true;
		}
	}
	return false;
}

FText SComfyStudio::GetWarningText() const
{
	const UComfyWorkflow* Workflow = GetSelectedWorkflow();
	const UComfySubsystem* Comfy = UComfySubsystem::Get();
	if (!Workflow || !Comfy)
	{
		return FText::GetEmpty();
	}
	const FName Backend = Comfy->GetActiveBackend();
	TArray<FString> Lines;
	const TArray<FString> Missing = Comfy->GetMissingNodeClasses(Backend, Workflow);
	if (Missing.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("%s doesn't have these nodes: %s. Install them with ComfyUI Manager, or pick another backend."), *Backend.ToString(), *FString::Join(Missing, TEXT(", "))));
	}
	if (Comfy->SpendsCredits(Backend, Workflow))
	{
		Lines.Add(TEXT("Running this spends Comfy credits."));
	}
	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

bool SComfyStudio::CanRun() const
{
	return GetSelectedWorkflow() != nullptr;
}

FReply SComfyStudio::OnRunClicked()
{
	UComfySubsystem* Comfy = UComfySubsystem::Get();
	UComfyWorkflow* Workflow = GetSelectedWorkflow();
	if (!Comfy || !Workflow)
	{
		return FReply::Handled();
	}

	UComfyJob* Job = Comfy->RunWorkflow(Workflow, InputValues, NAME_None);
	if (!Job)
	{
		return FReply::Handled();
	}
	const bool bImport = bImportResults;
	const bool bMaterial = bImportResults && bCreateMaterial && SelectedMakesImages();
	if (Job->IsFinished())
	{
		HandleJobFinished(Job, bImport, bMaterial);
	}
	else
	{
		TWeakPtr<SComfyStudio> WeakThis = SharedThis(this);
		Job->OnFinishedNative.AddLambda([WeakThis, bImport, bMaterial](UComfyJob* Finished)
		{
			if (TSharedPtr<SComfyStudio> This = WeakThis.Pin())
			{
				This->HandleJobFinished(Finished, bImport, bMaterial);
			}
		});
	}
	return FReply::Handled();
}

void SComfyStudio::HandleJobFinished(UComfyJob* Job, bool bImport, bool bMaterial)
{
	if (!Job->Succeeded())
	{
		ComfyStudioPrivate::Notify(FText::Format(LOCTEXT("JobFailed", "{0} failed: {1}"), FText::FromString(Job->Title), FText::FromString(Job->ErrorMessage)), false);
		return;
	}
	if (!bImport)
	{
		ComfyStudioPrivate::Notify(FText::Format(LOCTEXT("JobDone", "{0} finished"), FText::FromString(Job->Title)), true);
		return;
	}
	if (bMaterial)
	{
		FComfyMaterialOptions Options;
		Options.bMakeTileable = bMakeTileable;
		Options.bDeriveNormalMap = bMakeTileable;
		Options.bAssignToSelection = bAssignToSelection;
		if (UComfyEditorLibrary::CreateMaterialFromJob(Job, DestinationFolder, FString(), Options))
		{
			ComfyStudioPrivate::Notify(FText::Format(LOCTEXT("MaterialMade", "{0}: material created in {1}"), FText::FromString(Job->Title), FText::FromString(DestinationFolder)), true);
			return;
		}
	}
	const TArray<UObject*> Assets = UComfyEditorLibrary::ImportJobOutputs(Job, DestinationFolder, true);
	ComfyStudioPrivate::Notify(FText::Format(LOCTEXT("ImportedJob", "{0}: imported {1} asset(s) to {2}"), FText::FromString(Job->Title), FText::AsNumber(Assets.Num()), FText::FromString(DestinationFolder)), Assets.Num() > 0);
}

// Jobs -------------------------------------------------------------------------------------

TSharedRef<SWidget> SComfyStudio::BuildJobList()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				ComfyStudioPrivate::SectionHeader(LOCTEXT("Jobs", "JOBS"))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("ClearFinished", "Clear finished"))
				.OnClicked_Lambda([this]()
				{
					UComfySubsystem::Get()->ClearFinishedJobs();
					RefreshJobs();
					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SAssignNew(JobList, SListView<TSharedPtr<FJobItem>>)
			.ListItemsSource(&JobItems)
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow(this, &SComfyStudio::MakeJobRow)
		];
}

void SComfyStudio::RefreshJobs()
{
	JobItems.Reset();
	if (const UComfySubsystem* Comfy = UComfySubsystem::Get())
	{
		TArray<UComfyJob*> Jobs = Comfy->GetJobs();
		for (int32 Index = Jobs.Num() - 1; Index >= 0; --Index)
		{
			TSharedPtr<FJobItem> Item = MakeShared<FJobItem>();
			Item->Job = Jobs[Index];
			JobItems.Add(Item);
		}
	}
	if (JobList.IsValid())
	{
		JobList->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SComfyStudio::MakeJobRow(TSharedPtr<FJobItem> Item, const TSharedRef<STableViewBase>& Owner)
{
	const TWeakObjectPtr<UComfyJob> WeakJob = Item->Job;

	return SNew(STableRow<TSharedPtr<FJobItem>>, Owner)
		.Padding(FMargin(4, 3))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.3f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([WeakJob]() { return WeakJob.IsValid() ? FText::FromString(WeakJob->Title) : FText::GetEmpty(); })
				.ToolTipText_Lambda([WeakJob]() { return WeakJob.IsValid() ? FText::FromString(FString::Printf(TEXT("%s on %s"), *WeakJob->JobId, *WeakJob->Backend.ToString())) : FText::GetEmpty(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)
			[
				SNew(SBox).WidthOverride(80.f)
				[
					SNew(STextBlock).Text_Lambda([WeakJob]() { return WeakJob.IsValid() ? FText::FromString(WeakJob->GetStateText()) : FText::GetEmpty(); })
				]
			]
			+ SHorizontalBox::Slot().FillWidth(0.3f).VAlign(VAlign_Center)
			[
				SNew(SProgressBar)
				.Percent_Lambda([WeakJob]() -> TOptional<float>
				{
					if (!WeakJob.IsValid())
					{
						return 0.f;
					}
					if (WeakJob->IsFinished())
					{
						return 1.f;
					}
					return WeakJob->Progress.Fraction >= 0.f ? TOptional<float>(WeakJob->Progress.Fraction) : TOptional<float>();
				})
			]
			+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center).Padding(8, 0)
			[
				SNew(STextBlock)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.Text_Lambda([WeakJob]()
				{
					if (!WeakJob.IsValid())
					{
						return FText::GetEmpty();
					}
					if (!WeakJob->ErrorMessage.IsEmpty())
					{
						return FText::FromString(WeakJob->ErrorMessage);
					}
					if (WeakJob->Succeeded())
					{
						return FText::Format(LOCTEXT("OutputCount", "{0} output(s)"), FText::AsNumber(WeakJob->Outputs.Num()));
					}
					if (WeakJob->Progress.QueuePosition > 0)
					{
						return FText::Format(LOCTEXT("QueuePosition", "{0} ahead in queue"), FText::AsNumber(WeakJob->Progress.QueuePosition));
					}
					return FText::FromString(WeakJob->Progress.Message.IsEmpty() ? WeakJob->Progress.CurrentNode : WeakJob->Progress.Message);
				})
				.ToolTipText_Lambda([WeakJob]() { return WeakJob.IsValid() ? FText::FromString(WeakJob->ErrorMessage) : FText::GetEmpty(); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Cancel", "Cancel"))
				.Visibility_Lambda([WeakJob]() { return WeakJob.IsValid() && !WeakJob->IsFinished() ? EVisibility::Visible : EVisibility::Collapsed; })
				.OnClicked_Lambda([WeakJob]() { if (WeakJob.IsValid()) { WeakJob->Cancel(); } return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Import", "Import"))
				.ToolTipText(LOCTEXT("ImportTooltip", "Import this job's outputs into the Save to folder."))
				.Visibility_Lambda([WeakJob]() { return WeakJob.IsValid() && WeakJob->Succeeded() ? EVisibility::Visible : EVisibility::Collapsed; })
				.OnClicked_Lambda([this, WeakJob]()
				{
					if (WeakJob.IsValid())
					{
						const TArray<UObject*> Assets = UComfyEditorLibrary::ImportJobOutputs(WeakJob.Get(), DestinationFolder, true);
						ComfyStudioPrivate::Notify(FText::Format(LOCTEXT("ImportedCount", "Imported {0} asset(s)"), FText::AsNumber(Assets.Num())), Assets.Num() > 0);
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Folder", "Folder"))
				.ToolTipText(LOCTEXT("FolderTooltip", "Show the downloaded files."))
				.Visibility_Lambda([WeakJob]() { return WeakJob.IsValid() && WeakJob->Outputs.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
				.OnClicked_Lambda([WeakJob]()
				{
					if (WeakJob.IsValid())
					{
						FPlatformProcess::ExploreFolder(*WeakJob->GetOutputDirectory());
					}
					return FReply::Handled();
				})
			]
		];
}

#undef LOCTEXT_NAMESPACE
