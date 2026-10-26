// Copyright Comfy Org. All Rights Reserved.

#include "ComfyInputsCustomization.h"
#include "ComfyInputsLibrary.h"
#include "ComfyTypes.h"
#include "ComfyWorkflow.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Engine/Texture.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ComfyInputsCustomization"

namespace ComfyInputsCustomizationPrivate
{
	static FString LabelOf(const FComfyWorkflowInput& Input)
	{
		return Input.Label.IsEmpty() ? FName::NameToDisplayString(Input.Name, false) : Input.Label;
	}

	static FText TooltipOf(const FComfyWorkflowInput& Input)
	{
		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("Input \"%s\""), *Input.Name));
		if (!Input.Tooltip.IsEmpty())
		{
			Lines.Add(Input.Tooltip);
		}
		Lines.Add(Input.Type == EComfyInputType::Seed
			? FString(TEXT("Left empty, or -1, it gets a new seed on every run."))
			: FString(TEXT("Left empty, the workflow's own value is used.")));
		return FText::FromString(FString::Join(Lines, TEXT("\n")));
	}

	/** What a row shows when nothing is set: the value the run will use. */
	static FText DefaultText(const FComfyWorkflowInput& Input)
	{
		switch (Input.Type)
		{
		case EComfyInputType::Seed:
			return LOCTEXT("NewSeed", "New seed every run");
		case EComfyInputType::Model:
			return LOCTEXT("Auto", "Auto");
		default:
			return Input.DefaultValue.IsEmpty()
				? LOCTEXT("WorkflowValue", "Workflow's value")
				: FText::Format(LOCTEXT("DefaultFmt", "{0} (workflow's value)"), FText::FromString(Input.DefaultValue));
		}
	}

	static bool HasInput(const UComfyWorkflow* Workflow, const FString& Name)
	{
		return Workflow->Inputs.ContainsByPredicate([&Name](const FComfyWorkflowInput& Input) { return Input.Name.Equals(Name, ESearchCase::IgnoreCase); });
	}
}

TSharedRef<IPropertyTypeCustomization> FComfyInputsCustomization::MakeInstance()
{
	return MakeShared<FComfyInputsCustomization>();
}

const FComfyInputs* FComfyInputsCustomization::GetInputs() const
{
	TArray<const void*> Raw;
	if (StructHandle.IsValid())
	{
		StructHandle->AccessRawData(Raw);
	}
	return Raw.Num() > 0 ? static_cast<const FComfyInputs*>(Raw[0]) : nullptr;
}

UComfyWorkflow* FComfyInputsCustomization::GetWorkflow() const
{
	TArray<const void*> Raw;
	if (StructHandle.IsValid())
	{
		StructHandle->AccessRawData(Raw);
	}
	UComfyWorkflow* Workflow = nullptr;
	for (int32 Index = 0; Index < Raw.Num(); ++Index)
	{
		UComfyWorkflow* Each = Raw[Index] ? static_cast<const FComfyInputs*>(Raw[Index])->Workflow.Get() : nullptr;
		if (Index > 0 && Each != Workflow)
		{
			return nullptr;
		}
		Workflow = Each;
	}
	return Workflow;
}

void FComfyInputsCustomization::Edit(const FText& Description, TFunctionRef<void(FComfyInputs&)> Change)
{
	if (!StructHandle.IsValid())
	{
		return;
	}
	const FScopedTransaction Transaction(Description);
	TArray<UObject*> Outers;
	StructHandle->GetOuterObjects(Outers);
	for (UObject* Outer : Outers)
	{
		if (Outer)
		{
			Outer->Modify();
		}
	}
	StructHandle->NotifyPreChange();
	TArray<void*> Raw;
	StructHandle->AccessRawData(Raw);
	for (void* Data : Raw)
	{
		if (Data)
		{
			Change(*static_cast<FComfyInputs*>(Data));
		}
	}
	StructHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
	StructHandle->NotifyFinishedChangingProperties();
}

void FComfyInputsCustomization::SetValue(const FString& Name, const FString& Value)
{
	if (IsSet(Name) && GetValue(Name) == Value)
	{
		return;
	}
	Edit(LOCTEXT("SetInput", "Set Comfy Input"), [&Name, &Value](FComfyInputs& Inputs)
	{
		Inputs.Assets.Remove(Name);
		Inputs.Values.Add(Name, Value);
	});
}

void FComfyInputsCustomization::SetAsset(const FString& Name, UObject* Asset)
{
	if (!Asset)
	{
		Clear(Name);
		return;
	}
	Edit(LOCTEXT("SetInputAsset", "Set Comfy Input"), [&Name, Asset](FComfyInputs& Inputs)
	{
		Inputs.Values.Remove(Name);
		Inputs.Assets.Add(Name, Asset);
	});
}

void FComfyInputsCustomization::Clear(const FString& Name)
{
	if (!IsSet(Name))
	{
		return;
	}
	Edit(LOCTEXT("ClearInput", "Clear Comfy Input"), [&Name](FComfyInputs& Inputs)
	{
		Inputs.Values.Remove(Name);
		Inputs.Assets.Remove(Name);
	});
}

bool FComfyInputsCustomization::IsSet(const FString& Name) const
{
	const FComfyInputs* Inputs = GetInputs();
	return Inputs && (Inputs->Values.Contains(Name) || Inputs->Assets.Contains(Name));
}

FString FComfyInputsCustomization::GetValue(const FString& Name) const
{
	const FComfyInputs* Inputs = GetInputs();
	const FString* Value = Inputs ? Inputs->Values.Find(Name) : nullptr;
	return Value ? *Value : FString();
}

void FComfyInputsCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructHandle = PropertyHandle;
	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text_Lambda([this]()
			{
				const FComfyInputs* Inputs = GetInputs();
				if (!Inputs)
				{
					return FText::GetEmpty();
				}
				const int32 Count = Inputs->Values.Num() + Inputs->Assets.Num();
				if (!Inputs->Workflow)
				{
					return Count > 0 ? FText::Format(LOCTEXT("SummaryNoWorkflow", "{0} set, no workflow"), Count) : LOCTEXT("SummaryEmpty", "Pick a workflow");
				}
				return FText::Format(LOCTEXT("Summary", "{0} · {1} set"), FText::FromString(Inputs->Workflow->GetDisplayNameString()), Count);
			})
		];
}

void FComfyInputsCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	using namespace ComfyInputsCustomizationPrivate;

	StructHandle = PropertyHandle;
	Utilities = CustomizationUtils.GetPropertyUtilities();
	ThumbnailPool = CustomizationUtils.GetThumbnailPool();
	OptionLists.Reset();

	TSharedPtr<IPropertyHandle> WorkflowHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComfyInputs, Workflow));
	TSharedPtr<IPropertyHandle> ValuesHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComfyInputs, Values));
	TSharedPtr<IPropertyHandle> AssetsHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComfyInputs, Assets));
	if (!WorkflowHandle.IsValid() || !ValuesHandle.IsValid() || !AssetsHandle.IsValid())
	{
		return;
	}

	// A new workflow means new rows.
	TWeakPtr<IPropertyUtilities> WeakUtilities = Utilities;
	WorkflowHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakUtilities]()
	{
		if (const TSharedPtr<IPropertyUtilities> Pinned = WeakUtilities.Pin())
		{
			Pinned->ForceRefresh();
		}
	}));
	ChildBuilder.AddProperty(WorkflowHandle.ToSharedRef());

	UComfyWorkflow* Workflow = GetWorkflow();
	if (!Workflow)
	{
		ChildBuilder.AddProperty(ValuesHandle.ToSharedRef());
		ChildBuilder.AddProperty(AssetsHandle.ToSharedRef());
		return;
	}

	for (const FComfyWorkflowInput& Input : Workflow->Inputs)
	{
		const FText Label = FText::FromString(LabelOf(Input));
		ChildBuilder.AddCustomRow(Label)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(Label)
				.ToolTipText(TooltipOf(Input))
			]
			.ValueContent()
			.MinDesiredWidth(250.f)
			.MaxDesiredWidth(600.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					MakeValueWidget(Workflow, Input)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
				[
					MakeResetButton(Input.Name)
				]
			];
	}

	// Names the workflow doesn't have would fail the run; list them so they can be removed.
	if (const FComfyInputs* Inputs = GetInputs())
	{
		TArray<FString> Unknown;
		for (const TPair<FString, FString>& Pair : Inputs->Values)
		{
			if (!HasInput(Workflow, Pair.Key))
			{
				Unknown.AddUnique(Pair.Key);
			}
		}
		for (const TPair<FString, TObjectPtr<UObject>>& Pair : Inputs->Assets)
		{
			if (!HasInput(Workflow, Pair.Key))
			{
				Unknown.AddUnique(Pair.Key);
			}
		}
		for (const FString& Name : Unknown)
		{
			ChildBuilder.AddCustomRow(FText::FromString(Name))
				.NameContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FAppStyle::GetSlateColor("Colors.Error"))
					.Text(FText::FromString(Name))
				]
				.ValueContent()
				.MinDesiredWidth(250.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.AutoWrapText(true)
						.Text(FText::Format(LOCTEXT("UnknownInput", "{0} has no input with this name, so a run fails."), FText::FromString(Workflow->GetDisplayNameString())))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("Remove", "Remove"))
						.OnClicked_Lambda([this, Name, WeakUtilities]()
						{
							Clear(Name);
							if (const TSharedPtr<IPropertyUtilities> Pinned = WeakUtilities.Pin())
							{
								Pinned->ForceRefresh();
							}
							return FReply::Handled();
						})
					]
				];
		}
	}

	// The maps behind the rows, for checking what's sent.
	IDetailGroup& Raw = ChildBuilder.AddGroup(TEXT("ComfyInputsRaw"), LOCTEXT("RawGroup", "Values as text"));
	Raw.AddPropertyRow(ValuesHandle.ToSharedRef());
	Raw.AddPropertyRow(AssetsHandle.ToSharedRef());
}

TSharedRef<SWidget> FComfyInputsCustomization::MakeResetButton(const FString& Name)
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(0.f)
		.ToolTipText(LOCTEXT("ResetTooltip", "Clear: use the workflow's value"))
		.Visibility_Lambda([this, Name]() { return IsSet(Name) ? EVisibility::Visible : EVisibility::Hidden; })
		.OnClicked_Lambda([this, Name]()
		{
			Clear(Name);
			return FReply::Handled();
		})
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
			.ColorAndOpacity(FSlateColor::UseForeground())
		];
}

TSharedRef<SWidget> FComfyInputsCustomization::MakeChoiceWidget(const FComfyWorkflowInput& Input, const TArray<FString>& Choices)
{
	using namespace ComfyInputsCustomizationPrivate;

	TSharedPtr<TArray<TSharedPtr<FString>>> Options = MakeShared<TArray<TSharedPtr<FString>>>();
	for (const FString& Choice : Choices)
	{
		Options->Add(MakeShared<FString>(Choice));
	}
	OptionLists.Add(Options);

	const FString Name = Input.Name;
	const FText Default = DefaultText(Input);
	auto Display = [Default](const FString& Value) { return Value.IsEmpty() ? Default : FText::FromString(Value); };

	return SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(Options.Get())
		.OnGenerateWidget_Lambda([Display](TSharedPtr<FString> Item)
		{
			return SNew(STextBlock).Font(IDetailLayoutBuilder::GetDetailFont()).Text(Display(Item.IsValid() ? *Item : FString()));
		})
		.OnSelectionChanged_Lambda([this, Name](TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
		{
			if (!Item.IsValid() || SelectInfo == ESelectInfo::Direct)
			{
				return;
			}
			if (Item->IsEmpty())
			{
				Clear(Name);
			}
			else
			{
				SetValue(Name, *Item);
			}
		})
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text_Lambda([this, Name, Display]() { return Display(GetValue(Name)); })
		];
}

TSharedRef<SWidget> FComfyInputsCustomization::MakeValueWidget(const UComfyWorkflow* Workflow, const FComfyWorkflowInput& Input)
{
	using namespace ComfyInputsCustomizationPrivate;

	const FString Name = Input.Name;
	const FText Default = DefaultText(Input);
	auto CommitText = [this, Name](const FText& Text, ETextCommit::Type)
	{
		// An empty box means "not set", so the workflow's value applies.
		if (Text.IsEmpty())
		{
			Clear(Name);
		}
		else
		{
			SetValue(Name, Text.ToString());
		}
	};

	switch (Input.Type)
	{
	case EComfyInputType::Text:
		return SNew(SBox)
			.MinDesiredHeight(48.f)
			.MaxDesiredHeight(160.f)
			[
				SNew(SMultiLineEditableTextBox)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.AutoWrapText(true)
				.HintText(Default)
				.Text_Lambda([this, Name]() { return FText::FromString(GetValue(Name)); })
				.OnTextCommitted_Lambda(CommitText)
			];

	case EComfyInputType::Int:
	case EComfyInputType::Seed:
		return SNew(SNumericEntryBox<int64>)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.AllowSpin(false)
			.UndeterminedString(Default)
			.Value_Lambda([this, Name]() { return IsSet(Name) ? TOptional<int64>(FCString::Atoi64(*GetValue(Name))) : TOptional<int64>(); })
			.OnValueCommitted_Lambda([this, Name](int64 Value, ETextCommit::Type) { SetValue(Name, LexToString(Value)); });

	case EComfyInputType::Float:
		return SNew(SNumericEntryBox<double>)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.AllowSpin(false)
			.UndeterminedString(Default)
			.Value_Lambda([this, Name]() { return IsSet(Name) ? TOptional<double>(FCString::Atod(*GetValue(Name))) : TOptional<double>(); })
			.OnValueCommitted_Lambda([this, Name](double Value, ETextCommit::Type) { SetValue(Name, FString::SanitizeFloat(Value)); });

	case EComfyInputType::Bool:
	{
		const bool bDefault = Input.DefaultValue.ToBool();
		return SNew(SCheckBox)
			.IsChecked_Lambda([this, Name, bDefault]()
			{
				const bool bOn = IsSet(Name) ? GetValue(Name).ToBool() : bDefault;
				return bOn ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, Name](ECheckBoxState State) { SetValue(Name, State == ECheckBoxState::Checked ? TEXT("true") : TEXT("false")); });
	}

	case EComfyInputType::Enum:
		if (Input.Options.Num() > 0)
		{
			TArray<FString> Choices = Input.Options;
			Choices.Insert(FString(), 0);
			return MakeChoiceWidget(Input, Choices);
		}
		break;

	case EComfyInputType::Model:
	{
		const TArray<FString> Choices = UComfyInputsLibrary::GetComfyInputChoices(Workflow, Name);
		if (Choices.Num() > 1)
		{
			return MakeChoiceWidget(Input, Choices);
		}
		break;
	}

	case EComfyInputType::File:
		if (UComfyInputsLibrary::InputTakesImage(Workflow, Input))
		{
			return SNew(SObjectPropertyEntryBox)
				.AllowedClass(UTexture::StaticClass())
				.ThumbnailPool(ThumbnailPool)
				.AllowClear(true)
				.ObjectPath_Lambda([this, Name]()
				{
					const FComfyInputs* Inputs = GetInputs();
					const TObjectPtr<UObject>* Asset = Inputs ? Inputs->Assets.Find(Name) : nullptr;
					return Asset && *Asset ? (*Asset)->GetPathName() : FString();
				})
				.OnObjectChanged_Lambda([this, Name](const FAssetData& AssetData) { SetAsset(Name, AssetData.GetAsset()); });
		}
		break;

	default:
		break;
	}

	return SNew(SEditableTextBox)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.HintText(Input.Type == EComfyInputType::File ? LOCTEXT("FileHint", "Path to a file") : Default)
		.Text_Lambda([this, Name]() { return FText::FromString(GetValue(Name)); })
		.OnTextCommitted_Lambda(CommitText);
}

#undef LOCTEXT_NAMESPACE
