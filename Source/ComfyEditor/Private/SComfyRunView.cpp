// Copyright Comfy Org. All Rights Reserved.

#include "SComfyRunView.h"
#include "ComfyEditorLibrary.h"
#include "ComfyEditorStyle.h"
#include "ComfyJob.h"
#include "ComfyRuntimeLibrary.h"
#include "ComfySettings.h"
#include "ComfySubsystem.h"
#include "ComfyWorkflow.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Input/DragAndDrop.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IContentBrowserSingleton.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/Paths.h"
#include "SDropTarget.h"
#include "ScopedTransaction.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ComfyRunView"

namespace ComfyRunViewPrivate
{
	/** Workflow types in the order the library shows them (docs/MANIFEST.md). */
	static const TCHAR* TypeOrder[] = { TEXT("Material"), TEXT("Texture"), TEXT("Sound"), TEXT("Music"), TEXT("Mesh"), TEXT("Upscale"), TEXT("Other") };

	static const ISlateStyle& Style()
	{
		return FComfyEditorStyle::Get();
	}

	static TSharedRef<SWidget> Chip(const FText& Text)
	{
		return SNew(SBorder)
			.BorderImage(Style().GetBrush("Comfy.Chip"))
			.Padding(FMargin(8, 2))
			[
				SNew(STextBlock).TextStyle(&Style(), "Comfy.Text.Muted").Text(Text)
			];
	}

	static TSharedRef<SWidget> Card(const TSharedRef<SWidget>& Content, float Padding = 12.f)
	{
		return SNew(SBorder)
			.BorderImage(Style().GetBrush("Comfy.Card"))
			.Padding(Padding)
			[
				Content
			];
	}

	static TSharedRef<SButton> Button(const FText& Label, FOnClicked OnClicked, const FName StyleName = "Comfy.Button")
	{
		return SNew(SButton)
			.ButtonStyle(&Style(), StyleName)
			.OnClicked(OnClicked)
			[
				SNew(STextBlock).TextStyle(&Style(), "Comfy.Text").Text(Label)
			];
	}

	static bool IsImageJob(const UComfyJob* Job)
	{
		return Job && Job->GetOutputsOfKind(EComfyOutputKind::Image).Num() > 0;
	}
}

using namespace ComfyRunViewPrivate;

void SComfyRunView::Construct(const FArguments& InArgs)
{
	OnOpenInGraph = InArgs._OnOpenInGraph;
	DestinationFolder = GetDefault<UComfySettings>()->DefaultImportFolder;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(Style().GetBrush("Comfy.Panel"))
		.Padding(0)
		[
			SNew(SHorizontalBox)
			// Left column: what to run.
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(360.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(12, 12, 12, 8)
					[
						BuildPicker()
					]
					+ SVerticalBox::Slot().FillHeight(1.f).Padding(12, 0, 4, 0)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot().Padding(0, 0, 8, 0)
						[
							SAssignNew(FormBox, SVerticalBox)
						]
						+ SScrollBox::Slot().Padding(0, 0, 8, 8)
						[
							BuildOptionsCard()
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(12, 8, 12, 12)
					[
						BuildRunBar()
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(1.f)[SNew(SImage).Image(Style().GetBrush("Comfy.Divider"))]
			]
			// Main area: what came back.
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(12)
			[
				SAssignNew(MainSwitcher, SWidgetSwitcher)
				+ SWidgetSwitcher::Slot()
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(EmptyStateBox, SVerticalBox)
					]
				]
				+ SWidgetSwitcher::Slot()
				[
					BuildGallery()
				]
			]
		]
	];

	if (UComfySubsystem* Comfy = UComfySubsystem::Get())
	{
		JobAddedHandle = Comfy->OnJobAdded.AddSPLambda(this, [this](UComfyJob* Job)
		{
			RebuildStrip();
			SelectJob(Job);
		});
	}
	RefreshWorkflows();
	RebuildStrip();
}

SComfyRunView::~SComfyRunView()
{
	if (UComfySubsystem* Comfy = UComfySubsystem::Get())
	{
		Comfy->OnJobAdded.Remove(JobAddedHandle);
	}
}

FString SComfyRunView::TypeOf(const UComfyWorkflow* Workflow)
{
	const FString Type = Workflow ? ComfyTypes::NormalizeCategory(Workflow->Category) : FString();
	for (const TCHAR* Known : TypeOrder)
	{
		if (Type.Equals(Known, ESearchCase::IgnoreCase))
		{
			return Known;
		}
	}
	return TEXT("Other");
}

FString SComfyRunView::OriginOf(const UComfyWorkflow* Workflow)
{
	if (Workflow && Workflow->SourceFile.Contains(TEXT("/comfy_unreal/workflows/")))
	{
		return TEXT("Server");
	}
	return Workflow && Workflow->HasAnyFlags(RF_Transient) ? TEXT("Built-in") : TEXT("Project");
}

void SComfyRunView::SetStatus(const FText& Text, bool bError)
{
	Status = Text;
	bStatusIsError = bError;
}

// Workflow picker and library ----------------------------------------------------------------

void SComfyRunView::RefreshWorkflows()
{
	Workflows.Reset();
	for (UComfyWorkflow* Workflow : UComfyEditorLibrary::GetAllWorkflows())
	{
		Workflows.Add(Workflow);
	}
	if (SelectedWorkflow.IsValid() && !Workflows.Contains(SelectedWorkflow))
	{
		SelectWorkflow(nullptr);
	}
	RebuildEmptyState();
	MainSwitcher->SetActiveWidgetIndex(SelectedWorkflow.IsValid() ? 1 : 0);
}

TSharedRef<SWidget> SComfyRunView::BuildPicker()
{
	return SNew(SComboButton)
		.ButtonStyle(&Style(), "Comfy.Button")
		.HasDownArrow(true)
		.OnGetMenuContent(this, &SComfyRunView::BuildPickerMenu)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
			[
				SNew(SImage).Image(Style().GetBrush("ComfyUnreal.Icon.Small"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.TextStyle(&Style(), "Comfy.Text.Title")
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Text_Lambda([this]()
					{
						const UComfyWorkflow* Workflow = SelectedWorkflow.Get();
						return Workflow ? FText::FromString(Workflow->GetDisplayNameString()) : LOCTEXT("PickWorkflow", "Choose a workflow");
					})
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.TextStyle(&Style(), "Comfy.Text.Muted")
					.Text_Lambda([this]()
					{
						const UComfyWorkflow* Workflow = SelectedWorkflow.Get();
						return Workflow ? FText::FromString(FString::Printf(TEXT("%s  ·  %s"), *TypeOf(Workflow), *OriginOf(Workflow))) : FText::Format(LOCTEXT("Available", "{0} available"), FText::AsNumber(Workflows.Num()));
					})
				]
			]
		];
}

TSharedRef<SWidget> SComfyRunView::BuildPickerMenu()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const TCHAR* Origin : { TEXT("Project"), TEXT("Server"), TEXT("Built-in") })
	{
		bool bHeader = false;
		for (const TCHAR* Type : TypeOrder)
		{
			for (const TWeakObjectPtr<UComfyWorkflow>& Weak : Workflows)
			{
				UComfyWorkflow* Workflow = Weak.Get();
				if (!Workflow || OriginOf(Workflow) != Origin || TypeOf(Workflow) != Type)
				{
					continue;
				}
				if (!bHeader)
				{
					bHeader = true;
					List->AddSlot().AutoHeight().Padding(10, 10, 10, 4)
					[
						SNew(STextBlock).TextStyle(&Style(), "Comfy.Text.Label").Text(FText::FromString(FString(Origin).ToUpper()))
					];
				}
				TWeakObjectPtr<UComfyWorkflow> Captured = Workflow;
				List->AddSlot().AutoHeight().Padding(4, 1)
				[
					SNew(SButton)
					.ButtonStyle(&Style(), "Comfy.Button.Ghost")
					.OnClicked_Lambda([this, Captured]()
					{
						SelectWorkflow(Captured.Get());
						FSlateApplication::Get().DismissAllMenus();
						return FReply::Handled();
					})
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).TextStyle(&Style(), "Comfy.Text").Text(FText::FromString(Workflow->GetDisplayNameString()))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
						[
							Chip(FText::FromString(Type))
						]
					]
				];
			}
		}
	}
	return SNew(SBorder)
		.BorderImage(Style().GetBrush("Comfy.Panel"))
		.Padding(4)
		[
			SNew(SBox).WidthOverride(340.f).MaxDesiredHeight(520.f)
			[
				SNew(SScrollBox) + SScrollBox::Slot()[List]
			]
		];
}

void SComfyRunView::RebuildEmptyState()
{
	EmptyStateBox->ClearChildren();
	EmptyStateBox->AddSlot().AutoHeight().Padding(4, 4, 4, 2)
	[
		SNew(STextBlock).TextStyle(&Style(), "Comfy.Text.Large").Text(LOCTEXT("EmptyTitle", "What do you want to make?"))
	];
	EmptyStateBox->AddSlot().AutoHeight().Padding(4, 0, 4, 12)
	[
		SNew(STextBlock).TextStyle(&Style(), "Comfy.Text.Muted").AutoWrapText(true)
		.Text(LOCTEXT("EmptyHint", "Pick a workflow. Send your own from the Graph view, or from ComfyUI with the Comfy for Unreal extension."))
	];
	for (const TCHAR* Type : TypeOrder)
	{
		TSharedRef<SWrapBox> Cards = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(10.f, 10.f));
		int32 Count = 0;
		for (const TWeakObjectPtr<UComfyWorkflow>& Weak : Workflows)
		{
			UComfyWorkflow* Workflow = Weak.Get();
			if (!Workflow || TypeOf(Workflow) != Type)
			{
				continue;
			}
			++Count;
			TWeakObjectPtr<UComfyWorkflow> Captured = Workflow;
			Cards->AddSlot()
			[
				SNew(SBox).WidthOverride(240.f).HeightOverride(120.f)
				[
					SNew(SButton)
					.ButtonStyle(&Style(), "Comfy.Button")
					.OnClicked_Lambda([this, Captured]() { SelectWorkflow(Captured.Get()); return FReply::Handled(); })
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 6)
						[
							SNew(STextBlock).TextStyle(&Style(), "Comfy.Text.Title").AutoWrapText(true).Text(FText::FromString(Workflow->GetDisplayNameString()))
						]
						+ SVerticalBox::Slot().FillHeight(1.f)
						[
							SNew(STextBlock).TextStyle(&Style(), "Comfy.Text.Muted").AutoWrapText(true).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							.Text(Workflow->Description)
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
						[
							Chip(FText::FromString(OriginOf(Workflow)))
						]
					]
				]
			];
		}
		if (Count > 0)
		{
			EmptyStateBox->AddSlot().AutoHeight().Padding(4, 12, 4, 8)
			[
				SNew(STextBlock).TextStyle(&Style(), "Comfy.Text.Label").Text(FText::FromString(FString(Type).ToUpper()))
			];
			EmptyStateBox->AddSlot().AutoHeight().Padding(4, 0)[Cards];
		}
	}
}

void SComfyRunView::SelectWorkflow(UComfyWorkflow* Workflow)
{
	SelectedWorkflow = Workflow;
	InputValues.Reset();
	RandomSeeds.Reset();
	if (Workflow)
	{
		const UComfySubsystem* Comfy = UComfySubsystem::Get();
		for (const FComfyWorkflowInput& Input : Workflow->Inputs)
		{
			if (Input.Type == EComfyInputType::Seed)
			{
				RandomSeeds.Add(Input.Name);
			}
			InputValues.Add(Input.Name, Comfy ? Comfy->GetDefaultInputValue(NAME_None, Workflow, Input.Name) : Input.DefaultValue);
		}
	}
	RebuildForm();
	MainSwitcher->SetActiveWidgetIndex(Workflow ? 1 : 0);
}

void SComfyRunView::HandleBackendStatus(const FComfyBackendStatus& BackendStatus)
{
	const UComfySubsystem* Comfy = UComfySubsystem::Get();
	const UComfyWorkflow* Workflow = SelectedWorkflow.Get();
	if (!Comfy || !Workflow || BackendStatus.Backend != Comfy->GetActiveBackend())
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

// Input cards --------------------------------------------------------------------------------

void SComfyRunView::RebuildForm()
{
	FormBox->ClearChildren();
	OptionLists.Reset();
	const UComfyWorkflow* Workflow = SelectedWorkflow.Get();
	if (!Workflow)
	{
		return;
	}
	if (!Workflow->Description.IsEmpty())
	{
		FormBox->AddSlot().AutoHeight().Padding(0, 0, 0, 10)
		[
			SNew(STextBlock).TextStyle(&Style(), "Comfy.Text.Muted").AutoWrapText(true).Text(Workflow->Description)
		];
	}
	if (Workflow->Inputs.Num() == 0)
	{
		FormBox->AddSlot().AutoHeight()
		[
			Card(SNew(STextBlock).TextStyle(&Style(), "Comfy.Text.Muted").AutoWrapText(true)
				.Text(LOCTEXT("NoInputs", "This workflow has no exposed inputs; it runs exactly as saved. Expose inputs with the Comfy for Unreal extension in the Graph view.")))
		];
		return;
	}
	for (const FComfyWorkflowInput& Input : Workflow->Inputs)
	{
		FormBox->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
		[
			MakeInputCard(Input)
		];
	}
}

TSharedRef<SWidget> SComfyRunView::MakeInputCard(const FComfyWorkflowInput& Input)
{
	const FString Label = Input.Label.IsEmpty() ? FName::NameToDisplayString(Input.Name, false) : Input.Label;
	const FString Tooltip = Input.Tooltip.IsEmpty() ? FString::Printf(TEXT("%s on node %s"), *Input.InputName, *Input.NodeId) : Input.Tooltip;
	return Card(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock).TextStyle(&Style(), "Comfy.Text.Label").Text(FText::FromString(Label.ToUpper())).ToolTipText(FText::FromString(Tooltip))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeInputWidget(Input)
		]);
}

TSharedRef<SWidget> SComfyRunView::MakeInputWidget(const FComfyWorkflowInput& Input)
{
	const FString Name = Input.Name;
	auto Getter = [this, Name]() { const FString* Value = InputValues.Find(Name); return FText::FromString(Value ? *Value : FString()); };
	auto Setter = [this, Name](const FText& Text) { InputValues.Add(Name, Text.ToString()); };

	switch (Input.Type)
	{
	case EComfyInputType::Text:
	{
		// The main prompt gets room, like App mode's prompt box.
		const bool bPrompt = Input.Source.Equals(TEXT("Prompt"), ESearchCase::IgnoreCase) || Input.Name.Contains(TEXT("prompt"));
		return SNew(SBox).MinDesiredHeight(bPrompt ? 120.f : 56.f).MaxDesiredHeight(240.f)
			[
				SNew(SMultiLineEditableTextBox)
				.AutoWrapText(true)
				.HintText(LOCTEXT("PromptHint", "Describe what you want"))
				.Text_Lambda(Getter)
				.OnTextChanged_Lambda(Setter)
			];
	}

	case EComfyInputType::Seed:
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SEditableTextBox)
				.IsEnabled_Lambda([this, Name]() { return !RandomSeeds.Contains(Name); })
				.Text_Lambda([this, Name, Getter]() { return RandomSeeds.Contains(Name) ? LOCTEXT("RandomSeed", "New seed every run") : Getter(); })
				.OnTextChanged_Lambda(Setter)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, Name]() { return RandomSeeds.Contains(Name) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, Name](ECheckBoxState State)
				{
					if (State == ECheckBoxState::Checked)
					{
						RandomSeeds.Add(Name);
					}
					else
					{
						RandomSeeds.Remove(Name);
						const FString* Value = InputValues.Find(Name);
						if (!Value || !Value->IsNumeric())
						{
							InputValues.Add(Name, LexToString(UComfyRuntimeLibrary::NewSeed()));
						}
					}
				})
				[
					SNew(STextBlock).TextStyle(&Style(), "Comfy.Text").Text(LOCTEXT("Randomize", "Randomize"))
				]
			];

	case EComfyInputType::Int:
		return SNew(SSpinBox<int32>)
			.Value_Lambda([this, Name]() { const FString* Value = InputValues.Find(Name); return Value ? FCString::Atoi(**Value) : 0; })
			.OnValueChanged_Lambda([this, Name](int32 Value) { InputValues.Add(Name, LexToString(Value)); })
			.MinValue(TOptional<int32>())
			.MaxValue(TOptional<int32>())
			.Delta(1);

	case EComfyInputType::Float:
		return SNew(SSpinBox<float>)
			.Value_Lambda([this, Name]() { const FString* Value = InputValues.Find(Name); return Value ? FCString::Atof(**Value) : 0.f; })
			.OnValueChanged_Lambda([this, Name](float Value) { InputValues.Add(Name, FString::SanitizeFloat(Value)); })
			.MinValue(TOptional<float>())
			.MaxValue(TOptional<float>())
			.Delta(0.05f);

	case EComfyInputType::Bool:
		return SNew(SCheckBox)
			.IsChecked_Lambda([this, Name]() { const FString* Value = InputValues.Find(Name); return Value && Value->ToBool() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Name](ECheckBoxState State) { InputValues.Add(Name, State == ECheckBoxState::Checked ? TEXT("true") : TEXT("false")); });

	case EComfyInputType::File:
		return MakeFileInput(Input);

	case EComfyInputType::Model:
	case EComfyInputType::Enum:
	{
		const TArray<FString> Choices = Input.Type == EComfyInputType::Model
			? UComfySubsystem::Get()->GetInputOptions(NAME_None, SelectedWorkflow.Get(), Input.Name)
			: Input.Options;
		if (Choices.Num() > 0)
		{
			TSharedPtr<TArray<TSharedPtr<FString>>> Options = MakeShared<TArray<TSharedPtr<FString>>>();
			for (const FString& Choice : Choices)
			{
				Options->Add(MakeShared<FString>(Choice));
			}
			OptionLists.Add(Options);
			return SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(Options.Get())
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) { return SNew(STextBlock).Text(FText::FromString(*Item)); })
				.OnSelectionChanged_Lambda([this, Name](TSharedPtr<FString> Item, ESelectInfo::Type) { if (Item.IsValid()) { InputValues.Add(Name, *Item); } })
				[
					SNew(STextBlock).Text_Lambda(Getter)
				];
		}
		break;
	}

	default:
		break;
	}
	return SNew(SEditableTextBox).Text_Lambda(Getter).OnTextChanged_Lambda(Setter);
}

TSharedRef<SWidget> SComfyRunView::MakeFileInput(const FComfyWorkflowInput& Input)
{
	const FString Name = Input.Name;
	const bool bViewport = Input.Source.Equals(TEXT("ViewportCapture"), ESearchCase::IgnoreCase);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SDropTarget)
			.OnAllowDrop_Lambda([](TSharedPtr<FDragDropOperation> Operation)
			{
				return Operation.IsValid() && (Operation->IsOfType<FAssetDragDropOp>() || Operation->IsOfType<FExternalDragOperation>());
			})
			.OnIsRecognized_Lambda([](TSharedPtr<FDragDropOperation> Operation)
			{
				return Operation.IsValid() && (Operation->IsOfType<FAssetDragDropOp>() || Operation->IsOfType<FExternalDragOperation>());
			})
			.OnDropped_Lambda([this, Name](const FGeometry&, const FDragDropEvent& Event)
			{
				const TSharedPtr<FDragDropOperation> Operation = Event.GetOperation();
				if (Operation.IsValid() && Operation->IsOfType<FExternalDragOperation>())
				{
					const TArray<FString>& Files = StaticCastSharedPtr<FExternalDragOperation>(Operation)->GetFiles();
					if (Files.Num() > 0)
					{
						InputValues.Add(Name, Files[0]);
					}
				}
				else if (Operation.IsValid() && Operation->IsOfType<FAssetDragDropOp>())
				{
					const TArray<FAssetData>& Assets = StaticCastSharedPtr<FAssetDragDropOp>(Operation)->GetAssets();
					if (Assets.Num() > 0)
					{
						SetFileFromAsset(Name, Assets[0].GetAsset());
					}
				}
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(Style().GetBrush("Comfy.DropZone"))
				.Padding(FMargin(12, 18))
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(&Style(), "Comfy.Text.Muted")
					.Justification(ETextJustify::Center)
					.AutoWrapText(true)
					.Text_Lambda([this, Name]()
					{
						const FString* Value = InputValues.Find(Name);
						return Value && !Value->IsEmpty() ? FText::FromString(FPaths::GetCleanFilename(*Value)) : LOCTEXT("DropHere", "Drop a texture, mesh or file here");
					})
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				Button(LOCTEXT("UseSelection", "Use selection"), FOnClicked::CreateLambda([this, Name]()
				{
					TArray<FAssetData> Selected;
					FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().GetSelectedAssets(Selected);
					if (Selected.Num() > 0)
					{
						SetFileFromAsset(Name, Selected[0].GetAsset());
					}
					else
					{
						SetStatus(LOCTEXT("NoSelection", "Select a texture or mesh in the Content Browser first."), true);
					}
					return FReply::Handled();
				}))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				Button(LOCTEXT("Viewport", "Viewport"), FOnClicked::CreateLambda([this, Name]()
				{
					FString Error;
					const FString Path = UComfyEditorLibrary::CaptureActiveViewport(Error);
					if (Path.IsEmpty())
					{
						SetStatus(FText::FromString(Error), true);
					}
					else
					{
						InputValues.Add(Name, Path);
					}
					return FReply::Handled();
				}), bViewport ? FName("Comfy.Button.Primary") : FName("Comfy.Button"))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Button(LOCTEXT("Browse", "Browse..."), FOnClicked::CreateLambda([this, Name]()
				{
					TArray<FString> Files;
					IDesktopPlatform* Platform = FDesktopPlatformModule::Get();
					const void* Parent = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
					if (Platform && Platform->OpenFileDialog(Parent, TEXT("Choose input file"), FPaths::ProjectDir(), TEXT(""), TEXT("All files (*.*)|*.*"), EFileDialogFlags::None, Files) && Files.Num() > 0)
					{
						InputValues.Add(Name, FPaths::ConvertRelativePathToFull(Files[0]));
					}
					return FReply::Handled();
				}))
			]
		];
}

void SComfyRunView::SetFileFromAsset(const FString& Name, UObject* Asset)
{
	FString Error;
	const FString Path = UComfyEditorLibrary::ExportAssetForInput(Asset, Error);
	if (Path.IsEmpty())
	{
		SetStatus(FText::FromString(Error), true);
		return;
	}
	InputValues.Add(Name, Path);
}

TSharedRef<SWidget> SComfyRunView::BuildOptionsCard()
{
	auto Check = [this](const FText& Label, bool* Value)
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([Value]() { return *Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Value](ECheckBoxState State) { *Value = State == ECheckBoxState::Checked; })
			[
				SNew(STextBlock).TextStyle(&Style(), "Comfy.Text").Text(Label)
			];
	};
	return Card(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock).TextStyle(&Style(), "Comfy.Text.Label").Text(LOCTEXT("UnrealOptions", "IN UNREAL"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[Check(LOCTEXT("AutoImport", "Import results when they finish"), &bAutoImport)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[Check(LOCTEXT("MakeMaterial", "Make a material from images"), &bCreateMaterial)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[Check(LOCTEXT("Tileable", "Tileable, with a derived normal map"), &bMakeTileable)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SEditableTextBox)
			.ToolTipText(LOCTEXT("SaveToTooltip", "Content folder for imported assets"))
			.Text_Lambda([this]() { return FText::FromString(DestinationFolder); })
			.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { DestinationFolder = Text.ToString(); })
		]);
}

TSharedRef<SWidget> SComfyRunView::BuildRunBar()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text_Lambda([this]() { return Status; })
			.ColorAndOpacity_Lambda([this]() { return FSlateColor(bStatusIsError ? FComfyEditorStyle::FPalette::Danger : FComfyEditorStyle::FPalette::TextMuted); })
			.Visibility_Lambda([this]() { return Status.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SBox).WidthOverride(72.f).HeightOverride(40.f).ToolTipText(LOCTEXT("BatchTooltip", "How many runs to queue"))
				[
					SNew(SSpinBox<int32>)
					.MinValue(1).MaxValue(16).MinSliderValue(1).MaxSliderValue(16)
					.Value_Lambda([this]() { return BatchCount; })
					.OnValueChanged_Lambda([this](int32 Value) { BatchCount = FMath::Clamp(Value, 1, 16); })
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SBox).HeightOverride(40.f)
				[
					SNew(SButton)
					.ButtonStyle(&Style(), "Comfy.Button.Primary")
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					.IsEnabled(this, &SComfyRunView::CanRun)
					.OnClicked(this, &SComfyRunView::OnRun)
					[
						SNew(STextBlock).TextStyle(&Style(), "Comfy.Text.Title").Text(LOCTEXT("Run", "Run"))
					]
				]
			]
		];
}

bool SComfyRunView::CanRun() const
{
	return SelectedWorkflow.IsValid();
}

FReply SComfyRunView::OnRun()
{
	UComfySubsystem* Comfy = UComfySubsystem::Get();
	UComfyWorkflow* Workflow = SelectedWorkflow.Get();
	if (!Comfy || !Workflow)
	{
		return FReply::Handled();
	}
	SetStatus(FText::GetEmpty(), false);
	for (int32 Index = 0; Index < BatchCount; ++Index)
	{
		TMap<FString, FString> Values = InputValues;
		for (const FString& Seed : RandomSeeds)
		{
			Values.Add(Seed, TEXT("random"));
		}
		UComfyJob* Job = Comfy->RunWorkflow(Workflow, Values, NAME_None);
		if (!Job)
		{
			continue;
		}
		if (Job->IsFinished())
		{
			HandleJobFinished(Job);
			continue;
		}
		TWeakPtr<SComfyRunView> WeakThis = SharedThis(this);
		Job->OnFinishedNative.AddLambda([WeakThis](UComfyJob* Finished)
		{
			if (const TSharedPtr<SComfyRunView> This = WeakThis.Pin())
			{
				This->HandleJobFinished(Finished);
			}
		});
	}
	return FReply::Handled();
}

// Gallery ------------------------------------------------------------------------------------

TSharedRef<SWidget> SComfyRunView::BuildGallery()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SBorder)
			.BorderImage(Style().GetBrush("Comfy.Preview"))
			.Padding(8)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SScaleBox)
					.Stretch(EStretch::ScaleToFit)
					[
						SNew(SImage).Image_Lambda([this]() { return GetJobBrush(SelectedJob.Get()); })
					]
				]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(&Style(), "Comfy.Text.Muted")
					.Justification(ETextJustify::Center)
					.AutoWrapText(true)
					.Text(this, &SComfyRunView::GetPreviewText)
					.Visibility_Lambda([this]() { return GetJobBrush(SelectedJob.Get()) ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 10)
		[
			SNew(SHorizontalBox)
			.IsEnabled_Lambda([this]() { return SelectedJob.IsValid() && SelectedJob->Succeeded(); })
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(LOCTEXT("Import", "Import"), FOnClicked::CreateSP(this, &SComfyRunView::OnImport), "Comfy.Button.Primary")]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(LOCTEXT("Replace", "Replace on selection"), FOnClicked::CreateSP(this, &SComfyRunView::OnReplace))]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(LOCTEXT("Revert", "Revert"), FOnClicked::CreateSP(this, &SComfyRunView::OnRevert))]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(LOCTEXT("ShowInBrowser", "Show in Content Browser"), FOnClicked::CreateSP(this, &SComfyRunView::OnShowInContentBrowser))]
			+ SHorizontalBox::Slot().AutoWidth()[Button(LOCTEXT("OpenInGraph", "Open in Graph"), FOnClicked::CreateSP(this, &SComfyRunView::OnOpenInGraphClicked), "Comfy.Button.Ghost")]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HeightOverride(100.f)
			[
				SNew(SScrollBox)
				.Orientation(Orient_Horizontal)
				+ SScrollBox::Slot()
				[
					SAssignNew(Strip, SHorizontalBox)
				]
			]
		];
}

void SComfyRunView::RebuildStrip()
{
	Strip->ClearChildren();
	const UComfySubsystem* Comfy = UComfySubsystem::Get();
	if (!Comfy)
	{
		return;
	}
	TArray<UComfyJob*> Jobs = Comfy->GetJobs();
	for (int32 Index = Jobs.Num() - 1; Index >= 0; --Index)
	{
		Strip->AddSlot().AutoWidth().Padding(0, 0, 8, 0)[MakeJobTile(Jobs[Index])];
	}
}

TSharedRef<SWidget> SComfyRunView::MakeJobTile(UComfyJob* Job)
{
	const TWeakObjectPtr<UComfyJob> WeakJob = Job;
	return SNew(SButton)
		.ButtonStyle(&Style(), "Comfy.Button.Ghost")
		.ContentPadding(0)
		.ToolTipText_Lambda([WeakJob]() { return WeakJob.IsValid() ? FText::FromString(FString::Printf(TEXT("%s\n%s"), *WeakJob->Title, *WeakJob->GetStateText())) : FText::GetEmpty(); })
		.OnClicked_Lambda([this, WeakJob]() { SelectJob(WeakJob.Get()); return FReply::Handled(); })
		[
			SNew(SBorder)
			.BorderImage_Lambda([this, WeakJob]() { return Style().GetBrush(SelectedJob == WeakJob ? "Comfy.Card.Selected" : "Comfy.Card"); })
			.Padding(3)
			[
				SNew(SBox).WidthOverride(88.f).HeightOverride(88.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SScaleBox).Stretch(EStretch::ScaleToFill)
						[
							SNew(SImage).Image_Lambda([this, WeakJob]() { return GetJobBrush(WeakJob.Get()); })
						]
					]
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.TextStyle(&Style(), "Comfy.Text.Muted")
						.Visibility_Lambda([this, WeakJob]() { return GetJobBrush(WeakJob.Get()) ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
						.Text_Lambda([WeakJob]()
						{
							if (!WeakJob.IsValid())
							{
								return FText::GetEmpty();
							}
							if (WeakJob->Succeeded())
							{
								const TArray<FComfyOutputFile>& Outputs = WeakJob->Outputs;
								return Outputs.Num() > 0 ? FText::FromString(StaticEnum<EComfyOutputKind>()->GetNameStringByValue(int64(Outputs[0].Kind))) : LOCTEXT("Done", "Done");
							}
							return FText::FromString(WeakJob->GetStateText());
						})
					]
					+ SOverlay::Slot().VAlign(VAlign_Bottom).Padding(4)
					[
						SNew(SProgressBar)
						.Visibility_Lambda([WeakJob]() { return WeakJob.IsValid() && !WeakJob->IsFinished() ? EVisibility::Visible : EVisibility::Collapsed; })
						.Percent_Lambda([WeakJob]() -> TOptional<float>
						{
							return WeakJob.IsValid() && WeakJob->Progress.Fraction >= 0.f ? TOptional<float>(WeakJob->Progress.Fraction) : TOptional<float>();
						})
					]
				]
			]
		];
}

void SComfyRunView::SelectJob(UComfyJob* Job)
{
	SelectedJob = Job;
}

const FSlateBrush* SComfyRunView::GetJobBrush(const UComfyJob* Job)
{
	if (!Job || !Job->Succeeded())
	{
		return nullptr;
	}
	const TArray<FComfyOutputFile> Images = Job->GetOutputsOfKind(EComfyOutputKind::Image);
	if (Images.Num() == 0)
	{
		return nullptr;
	}
	const FString& Path = Images[0].LocalPath;
	if (const FPreview* Found = Previews.Find(Path))
	{
		return Found->Brush.Get();
	}
	// Load once; a missing file is cached as no image so it isn't retried every frame.
	FPreview& Preview = Previews.Add(Path);
	if (UTexture2D* Texture = UComfyRuntimeLibrary::LoadTextureFromFile(Path, true))
	{
		Preview.Texture.Reset(Texture);
		Preview.Brush = MakeShared<FSlateBrush>();
		Preview.Brush->SetResourceObject(Texture);
		Preview.Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		Preview.Brush->DrawAs = ESlateBrushDrawType::Image;
	}
	return Preview.Brush.Get();
}

FText SComfyRunView::GetPreviewText() const
{
	const UComfyJob* Job = SelectedJob.Get();
	if (!Job)
	{
		return LOCTEXT("NoResult", "Results appear here.\nPress Run.");
	}
	if (!Job->ErrorMessage.IsEmpty())
	{
		return FText::FromString(Job->ErrorMessage);
	}
	if (!Job->IsFinished())
	{
		const FString Detail = Job->Progress.Message.IsEmpty() ? Job->GetStateText() : Job->Progress.Message;
		return Job->Progress.Fraction >= 0.f
			? FText::Format(LOCTEXT("Progress", "{0}\n{1}"), FText::AsPercent(Job->Progress.Fraction), FText::FromString(Detail))
			: FText::FromString(Detail);
	}
	if (Job->Outputs.Num() > 0)
	{
		return FText::Format(LOCTEXT("NonImage", "{0}\n{1}"), FText::FromString(StaticEnum<EComfyOutputKind>()->GetNameStringByValue(int64(Job->Outputs[0].Kind))), FText::FromString(Job->Outputs[0].FileName));
	}
	return FText::FromString(Job->GetStateText());
}

// Result actions -----------------------------------------------------------------------------

void SComfyRunView::HandleJobFinished(UComfyJob* Job)
{
	if (!Job)
	{
		return;
	}
	if (!Job->Succeeded())
	{
		SetStatus(FText::Format(LOCTEXT("JobFailed", "{0} failed: {1}"), FText::FromString(Job->Title), FText::FromString(Job->ErrorMessage)), true);
		return;
	}
	if (SelectedJob == Job || !SelectedJob.IsValid())
	{
		SelectJob(Job);
	}
	if (bAutoImport)
	{
		const TArray<UObject*> Assets = ImportJob(Job);
		SetStatus(FText::Format(LOCTEXT("Imported", "{0}: {1} asset(s) in {2}"), FText::FromString(Job->Title), FText::AsNumber(Assets.Num()), FText::FromString(DestinationFolder)), false);
	}
}

TArray<UObject*> SComfyRunView::ImportJob(UComfyJob* Job)
{
	TArray<UObject*> Assets;
	if (!Job || !Job->Succeeded())
	{
		return Assets;
	}
	if (TArray<TWeakObjectPtr<UObject>>* Existing = ImportedAssets.Find(Job->JobId))
	{
		for (const TWeakObjectPtr<UObject>& Asset : *Existing)
		{
			if (Asset.IsValid())
			{
				Assets.Add(Asset.Get());
			}
		}
		if (Assets.Num() > 0)
		{
			return Assets;
		}
	}
	if (bCreateMaterial && ComfyRunViewPrivate::IsImageJob(Job))
	{
		FComfyMaterialOptions Options;
		Options.bMakeTileable = bMakeTileable;
		Options.bDeriveNormalMap = bMakeTileable;
		if (UMaterialInstanceConstant* Material = UComfyEditorLibrary::CreateMaterialFromJob(Job, DestinationFolder, FString(), Options))
		{
			Assets.Add(Material);
		}
	}
	if (Assets.Num() == 0)
	{
		Assets = UComfyEditorLibrary::ImportJobOutputs(Job, DestinationFolder, true);
	}
	TArray<TWeakObjectPtr<UObject>>& Stored = ImportedAssets.FindOrAdd(Job->JobId);
	Stored.Reset();
	for (UObject* Asset : Assets)
	{
		Stored.Add(Asset);
	}
	return Assets;
}

FReply SComfyRunView::OnImport()
{
	if (UComfyJob* Job = SelectedJob.Get())
	{
		const TArray<UObject*> Assets = ImportJob(Job);
		SetStatus(FText::Format(LOCTEXT("ImportedCount", "Imported {0} asset(s) to {1}"), FText::AsNumber(Assets.Num()), FText::FromString(DestinationFolder)), Assets.Num() == 0);
	}
	return FReply::Handled();
}

FReply SComfyRunView::OnReplace()
{
	UComfyJob* Job = SelectedJob.Get();
	if (!Job || !GEditor)
	{
		return FReply::Handled();
	}
	const TArray<UObject*> Assets = ImportJob(Job);
	UMaterialInterface* Material = nullptr;
	UStaticMesh* Mesh = nullptr;
	for (UObject* Asset : Assets)
	{
		Material = Material ? Material : Cast<UMaterialInterface>(Asset);
		Mesh = Mesh ? Mesh : Cast<UStaticMesh>(Asset);
	}
	if (!Material && !Mesh)
	{
		SetStatus(LOCTEXT("NothingToReplace", "Replace works with materials and static meshes. Turn on \"Make a material from images\" for textures."), true);
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("ReplaceTransaction", "Replace with Comfy result"));
	FReplacement& Record = Replacements.FindOrAdd(Job->JobId);
	int32 Changed = 0;
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		AActor* Actor = Cast<AActor>(*It);
		if (!Actor)
		{
			continue;
		}
		TInlineComponentArray<UMeshComponent*> Components(Actor);
		for (UMeshComponent* Component : Components)
		{
			UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
			Component->Modify();
			if (Mesh && StaticMeshComponent)
			{
				Record.Meshes.Add({ StaticMeshComponent, StaticMeshComponent->GetStaticMesh() });
				StaticMeshComponent->SetStaticMesh(Mesh);
				++Changed;
			}
			else if (Material)
			{
				for (int32 Slot = 0; Slot < FMath::Max(1, Component->GetNumMaterials()); ++Slot)
				{
					Record.Materials.Add({ Component, Slot, Component->GetMaterial(Slot) });
					Component->SetMaterial(Slot, Material);
				}
				++Changed;
			}
		}
	}
	SetStatus(Changed > 0 ? FText::Format(LOCTEXT("Replaced", "Replaced on {0} component(s). Revert or Ctrl+Z undoes it."), FText::AsNumber(Changed))
		: LOCTEXT("SelectActors", "Select actors in the level first."), Changed == 0);
	return FReply::Handled();
}

FReply SComfyRunView::OnRevert()
{
	const UComfyJob* Job = SelectedJob.Get();
	FReplacement* Record = Job ? Replacements.Find(Job->JobId) : nullptr;
	if (!Record)
	{
		SetStatus(LOCTEXT("NothingToRevert", "Nothing to revert for this result."), true);
		return FReply::Handled();
	}
	const FScopedTransaction Transaction(LOCTEXT("RevertTransaction", "Revert Comfy result"));
	// Restore in reverse so a component replaced twice ends on its original.
	for (int32 Index = Record->Materials.Num() - 1; Index >= 0; --Index)
	{
		const FPreviousMaterial& Previous = Record->Materials[Index];
		if (UMeshComponent* Component = Previous.Component.Get())
		{
			Component->Modify();
			Component->SetMaterial(Previous.Slot, Previous.Material.Get());
		}
	}
	for (int32 Index = Record->Meshes.Num() - 1; Index >= 0; --Index)
	{
		const FPreviousMesh& Previous = Record->Meshes[Index];
		if (UStaticMeshComponent* Component = Previous.Component.Get())
		{
			Component->Modify();
			Component->SetStaticMesh(Previous.Mesh.Get());
		}
	}
	Replacements.Remove(Job->JobId);
	SetStatus(LOCTEXT("Reverted", "Reverted. The imported assets are kept."), false);
	return FReply::Handled();
}

FReply SComfyRunView::OnShowInContentBrowser()
{
	UComfyJob* Job = SelectedJob.Get();
	if (Job && GEditor)
	{
		TArray<UObject*> Assets = ImportJob(Job);
		if (Assets.Num() > 0)
		{
			GEditor->SyncBrowserToObjects(Assets);
		}
	}
	return FReply::Handled();
}

FReply SComfyRunView::OnOpenInGraphClicked()
{
	const UComfyJob* Job = SelectedJob.Get();
	const UComfyWorkflow* Workflow = Job && Job->Workflow ? Job->Workflow.Get() : SelectedWorkflow.Get();
	OnOpenInGraph.ExecuteIfBound(Workflow);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
