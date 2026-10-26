// Copyright Comfy Org. All Rights Reserved.

#include "ComfyGraphPins.h"
#include "K2Node_RunComfyWorkflow.h"
#include "ComfySubsystem.h"
#include "EdGraphSchema_K2.h"
#include "ScopedTransaction.h"
#include "SGraphPin.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ComfyGraphPins"

/** A string or name pin with a dropdown. An empty choice shows EmptyLabel: "Auto" for a model, "Active backend" for a backend. */
class SGraphPinComfyChoice : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPinComfyChoice) {}
		SLATE_ARGUMENT(FText, EmptyLabel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin, const TArray<FString>& InChoices)
	{
		EmptyLabel = InArgs._EmptyLabel;
		for (const FString& Choice : InChoices)
		{
			Choices.Add(MakeShared<FString>(Choice));
		}
		SGraphPin::Construct(SGraphPin::FArguments(), InPin);
	}

protected:
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override
	{
		TSharedPtr<FString> Current;
		const FString Value = GraphPinObj ? GraphPinObj->GetDefaultAsString() : FString();
		for (const TSharedPtr<FString>& Choice : Choices)
		{
			if (*Choice == Value)
			{
				Current = Choice;
				break;
			}
		}

		return SNew(SComboBox<TSharedPtr<FString>>)
			.ContentPadding(FMargin(6.f, 2.f))
			.OptionsSource(&Choices)
			.InitiallySelectedItem(Current)
			.OnGenerateWidget_Lambda([this](TSharedPtr<FString> Choice)
			{
				return SNew(STextBlock).Text(Label(*Choice));
			})
			.OnSelectionChanged(this, &SGraphPinComfyChoice::OnChoiceSelected)
			.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
			[
				SNew(STextBlock).Text(this, &SGraphPinComfyChoice::GetCurrentText)
			];
	}

private:
	FText Label(const FString& Choice) const
	{
		// A name pin's empty value is "None".
		return Choice.IsEmpty() || Choice == TEXT("None") ? EmptyLabel : FText::FromString(Choice);
	}

	FText GetCurrentText() const
	{
		return Label(GraphPinObj ? GraphPinObj->GetDefaultAsString() : FString());
	}

	void OnChoiceSelected(TSharedPtr<FString> Choice, ESelectInfo::Type)
	{
		FString Value = Choice.IsValid() ? *Choice : FString();
		if (Value.IsEmpty() && GraphPinObj && GraphPinObj->PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
		{
			Value = TEXT("None");
		}
		if (GraphPinObj && GraphPinObj->GetDefaultAsString() != Value)
		{
			const FScopedTransaction Transaction(LOCTEXT("ChangeChoice", "Change Comfy Input"));
			GraphPinObj->Modify();
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Value);
		}
	}

	TArray<TSharedPtr<FString>> Choices;
	FText EmptyLabel;
};

TSharedPtr<SGraphPin> FComfyGraphPinFactory::CreatePin(UEdGraphPin* Pin) const
{
	// Backend parameters tagged ComfyBackend pick from the backend profiles, like an enum that follows the settings.
	UEdGraphNode* OwningNode = Pin ? Pin->GetOwningNodeUnchecked() : nullptr;
	if (OwningNode && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Name
		&& !OwningNode->GetPinMetaData(Pin->PinName, TEXT("ComfyBackend")).IsEmpty())
	{
		TArray<FString> Backends = { FString() };
		if (const UComfySubsystem* Comfy = UComfySubsystem::Get())
		{
			for (const FName& Name : Comfy->GetBackendNames())
			{
				Backends.Add(Name.ToString());
			}
		}
		return SNew(SGraphPinComfyChoice, Pin, Backends).EmptyLabel(LOCTEXT("ActiveBackend", "Active backend"));
	}

	const UK2Node_RunComfyWorkflow* Node = Pin ? Cast<UK2Node_RunComfyWorkflow>(Pin->GetOwningNodeUnchecked()) : nullptr;
	if (!Node || Pin->Direction != EGPD_Input || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_String)
	{
		return nullptr;
	}
	const TArray<FString> Choices = Node->GetPinChoices(Pin);
	if (Choices.Num() == 0)
	{
		return nullptr;
	}
	return SNew(SGraphPinComfyChoice, Pin, Choices).EmptyLabel(LOCTEXT("Auto", "Auto"));
}

#undef LOCTEXT_NAMESPACE
