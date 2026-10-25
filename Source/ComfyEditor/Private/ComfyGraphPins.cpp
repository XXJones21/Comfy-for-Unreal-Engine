// Copyright Comfy Org. All Rights Reserved.

#include "ComfyGraphPins.h"
#include "K2Node_RunComfyWorkflow.h"
#include "EdGraphSchema_K2.h"
#include "ScopedTransaction.h"
#include "SGraphPin.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ComfyGraphPins"

/** A string pin with a dropdown. An empty choice reads "Auto": the backend picks the workflow's preferred model. */
class SGraphPinComfyChoice : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPinComfyChoice) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin, const TArray<FString>& InChoices)
	{
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
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Choice)
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
	static FText Label(const FString& Choice)
	{
		return Choice.IsEmpty() ? LOCTEXT("Auto", "Auto") : FText::FromString(Choice);
	}

	FText GetCurrentText() const
	{
		return Label(GraphPinObj ? GraphPinObj->GetDefaultAsString() : FString());
	}

	void OnChoiceSelected(TSharedPtr<FString> Choice, ESelectInfo::Type)
	{
		const FString Value = Choice.IsValid() ? *Choice : FString();
		if (GraphPinObj && GraphPinObj->GetDefaultAsString() != Value)
		{
			const FScopedTransaction Transaction(LOCTEXT("ChangeChoice", "Change Comfy Input"));
			GraphPinObj->Modify();
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Value);
		}
	}

	TArray<TSharedPtr<FString>> Choices;
};

TSharedPtr<SGraphPin> FComfyGraphPinFactory::CreatePin(UEdGraphPin* Pin) const
{
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
	return SNew(SGraphPinComfyChoice, Pin, Choices);
}

#undef LOCTEXT_NAMESPACE
