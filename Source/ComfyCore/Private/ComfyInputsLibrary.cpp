// Copyright Comfy Org. All Rights Reserved.

#include "ComfyInputsLibrary.h"
#include "ComfyRuntimeLibrary.h"
#include "ComfySubsystem.h"
#include "ComfyWorkflow.h"
#include "Engine/Texture.h"
#include "Misc/Paths.h"

TFunction<FString(UObject* Asset, FString& OutError)> UComfyInputsLibrary::EditorAssetExporter;

FComfyInputs UComfyInputsLibrary::SetComfyText(const FComfyInputs& Inputs, const FString& Name, const FString& Value)
{
	FComfyInputs Result = Inputs;
	Result.Assets.Remove(Name);
	Result.Values.Add(Name, Value);
	return Result;
}

FComfyInputs UComfyInputsLibrary::SetComfyInt(const FComfyInputs& Inputs, const FString& Name, int64 Value)
{
	return SetComfyText(Inputs, Name, LexToString(Value));
}

FComfyInputs UComfyInputsLibrary::SetComfyFloat(const FComfyInputs& Inputs, const FString& Name, double Value)
{
	return SetComfyText(Inputs, Name, FString::SanitizeFloat(Value));
}

FComfyInputs UComfyInputsLibrary::SetComfyBool(const FComfyInputs& Inputs, const FString& Name, bool Value)
{
	return SetComfyText(Inputs, Name, Value ? TEXT("true") : TEXT("false"));
}

FComfyInputs UComfyInputsLibrary::SetComfySeed(const FComfyInputs& Inputs, const FString& Name, int64 Seed)
{
	return SetComfyText(Inputs, Name, LexToString(Seed));
}

FComfyInputs UComfyInputsLibrary::SetComfyFile(const FComfyInputs& Inputs, const FString& Name, const FString& FilePath)
{
	return SetComfyText(Inputs, Name, FilePath);
}

FComfyInputs UComfyInputsLibrary::SetComfyTexture(const FComfyInputs& Inputs, const FString& Name, UTexture* Texture)
{
	return SetComfyAsset(Inputs, Name, Texture);
}

FComfyInputs UComfyInputsLibrary::SetComfyAsset(const FComfyInputs& Inputs, const FString& Name, UObject* Asset)
{
	FComfyInputs Result = Inputs;
	Result.Values.Remove(Name);
	Result.Assets.Add(Name, Asset);
	return Result;
}

bool UComfyInputsLibrary::GetComfyInput(const FComfyInputs& Inputs, const FString& Name, FString& Value)
{
	if (const FString* Text = Inputs.Values.Find(Name))
	{
		Value = *Text;
		return true;
	}
	const TObjectPtr<UObject>* Asset = Inputs.Assets.Find(Name);
	if (Asset && *Asset)
	{
		Value = (*Asset)->GetPathName();
		return true;
	}
	Value.Reset();
	return false;
}

FComfyInputs UComfyInputsLibrary::ClearComfyInput(const FComfyInputs& Inputs, const FString& Name)
{
	FComfyInputs Result = Inputs;
	Result.Values.Remove(Name);
	Result.Assets.Remove(Name);
	return Result;
}

TArray<FString> UComfyInputsLibrary::GetComfyInputChoices(const UComfyWorkflow* Workflow, const FString& Name)
{
	const FComfyWorkflowInput* Input = Workflow ? Workflow->Inputs.FindByPredicate([&Name](const FComfyWorkflowInput& Each) { return Each.Name.Equals(Name, ESearchCase::IgnoreCase); }) : nullptr;
	if (!Input)
	{
		return {};
	}
	if (Input->Type == EComfyInputType::Enum)
	{
		return Input->Options;
	}
	if (Input->Type != EComfyInputType::Model)
	{
		return {};
	}

	// A local ComfyUI lists what's installed. Cloud and Router (or a local ComfyUI nobody has checked yet) get the
	// workflow's preferred files, which Cloud has.
	TArray<FString> Choices;
	if (UComfySubsystem* Comfy = UComfySubsystem::Get())
	{
		const FName Backend = Comfy->GetActiveBackend();
		FComfyBackendProfile Profile;
		if (Comfy->GetBackendProfile(Backend, Profile) && Profile.Kind == EComfyBackendKind::Local)
		{
			Choices = Comfy->GetInputOptions(Backend, Workflow, Input->Name);
		}
	}
	if (Choices.Num() == 0)
	{
		if (!Input->DefaultValue.IsEmpty())
		{
			Choices.Add(Input->DefaultValue);
		}
		for (const FString& Preferred : Input->Prefer)
		{
			// Prefer entries can be name fragments; only whole file names make sense as a choice.
			if (FPaths::GetExtension(Preferred).Len() > 0)
			{
				Choices.AddUnique(Preferred);
			}
		}
	}
	Choices.Remove(FString());
	Choices.Insert(FString(), 0);
	return Choices;
}

bool UComfyInputsLibrary::InputTakesImage(const UComfyWorkflow* Workflow, const FComfyWorkflowInput& Input)
{
	return Input.Type == EComfyInputType::File
		&& (Input.Source.Equals(TEXT("SelectedTextures"), ESearchCase::IgnoreCase)
			|| Input.Source.Equals(TEXT("ViewportCapture"), ESearchCase::IgnoreCase)
			|| (Workflow && Workflow->GetNodeClass(Input.NodeId).Contains(TEXT("LoadImage"))));
}

FComfyInputs UComfyInputsLibrary::MakeComfyInputsFromMap(const TMap<FString, FString>& Values)
{
	FComfyInputs Result;
	Result.Values = Values;
	return Result;
}

TArray<FString> UComfyInputsLibrary::GetComfyInputNames(const UComfyWorkflow* Workflow)
{
	return Workflow ? Workflow->GetInputNames() : TArray<FString>();
}

bool UComfyInputsLibrary::ValidateComfyInputs(const UComfyWorkflow* Workflow, const FComfyInputs& Inputs, FString& OutError)
{
	TMap<FString, FString> Values;
	return ResolveComfyInputs(Workflow, Inputs, Values, OutError, false);
}

namespace ComfyInputsPrivate
{
	static const FComfyWorkflowInput* FindInput(const UComfyWorkflow* Workflow, const FString& Name)
	{
		return Workflow->Inputs.FindByPredicate([&Name](const FComfyWorkflowInput& Input) { return Input.Name.Equals(Name, ESearchCase::IgnoreCase); });
	}

	static bool IsWholeNumber(const FString& Value)
	{
		FString Digits = Value.TrimStartAndEnd();
		Digits.RemoveFromStart(TEXT("-"));
		if (Digits.IsEmpty())
		{
			return false;
		}
		for (const TCHAR Char : Digits)
		{
			if (!FChar::IsDigit(Char))
			{
				return false;
			}
		}
		return true;
	}

	/** Check a value against its input and put it in the form a run takes. */
	static bool CheckValue(const FComfyWorkflowInput& Input, FString& Value, FString& OutError)
	{
		const FString Label = Input.Label.IsEmpty() ? Input.Name : Input.Label;
		switch (Input.Type)
		{
		case EComfyInputType::Int:
			if (!IsWholeNumber(Value))
			{
				OutError = FString::Printf(TEXT("%s (%s) takes a whole number, not \"%s\"."), *Label, *Input.Name, *Value);
				return false;
			}
			break;
		case EComfyInputType::Seed:
			if (!IsWholeNumber(Value))
			{
				OutError = FString::Printf(TEXT("%s (%s) takes a whole number, or -1 for a new seed every run, not \"%s\"."), *Label, *Input.Name, *Value);
				return false;
			}
			if (Value.TrimStart().StartsWith(TEXT("-")))
			{
				Value = LexToString(UComfyRuntimeLibrary::NewSeed());
			}
			break;
		case EComfyInputType::Float:
			if (!Value.TrimStartAndEnd().IsNumeric())
			{
				OutError = FString::Printf(TEXT("%s (%s) takes a number, not \"%s\"."), *Label, *Input.Name, *Value);
				return false;
			}
			break;
		case EComfyInputType::Bool:
			Value = Value.ToBool() ? TEXT("true") : TEXT("false");
			break;
		case EComfyInputType::Enum:
			if (Input.Options.Num() > 0 && !Input.Options.Contains(Value))
			{
				OutError = FString::Printf(TEXT("%s (%s) can't be \"%s\". Choose one of: %s."), *Label, *Input.Name, *Value, *FString::Join(Input.Options, TEXT(", ")));
				return false;
			}
			break;
		default:
			break;
		}
		return true;
	}
}

bool UComfyInputsLibrary::ResolveComfyInputs(const UComfyWorkflow* Workflow, const FComfyInputs& Inputs, TMap<FString, FString>& OutValues, FString& OutError, bool bExportAssets)
{
	using namespace ComfyInputsPrivate;

	OutValues.Reset();
	if (!Workflow)
	{
		Workflow = Inputs.Workflow;
	}
	if (!Workflow)
	{
		OutError = TEXT("No workflow given. Set the Workflow pin, or the Workflow of the Comfy Inputs.");
		return false;
	}

	TArray<FString> Unknown;
	for (const TPair<FString, FString>& Pair : Inputs.Values)
	{
		const FComfyWorkflowInput* Input = FindInput(Workflow, Pair.Key);
		if (!Input)
		{
			Unknown.Add(Pair.Key);
			continue;
		}
		FString Value = Pair.Value;
		if (!CheckValue(*Input, Value, OutError))
		{
			return false;
		}
		OutValues.Add(Input->Name, Value);
	}

	for (const TPair<FString, TObjectPtr<UObject>>& Pair : Inputs.Assets)
	{
		const FComfyWorkflowInput* Input = FindInput(Workflow, Pair.Key);
		if (!Input)
		{
			Unknown.Add(Pair.Key);
			continue;
		}
		const FString Label = Input->Label.IsEmpty() ? Input->Name : Input->Label;
		if (Input->Type != EComfyInputType::File)
		{
			OutError = FString::Printf(TEXT("%s (%s) isn't a file input, so it can't take an asset."), *Label, *Input->Name);
			return false;
		}
		UObject* Asset = Pair.Value;
		if (!Asset)
		{
			// An unset asset pin leaves the input as the workflow has it.
			continue;
		}
		if (!bExportAssets)
		{
			continue;
		}

		FString Error;
		FString Path;
		if (UTexture* Texture = Cast<UTexture>(Asset))
		{
			Path = UComfyRuntimeLibrary::SaveTextureForInput(Texture, Error);
		}
		if (Path.IsEmpty() && EditorAssetExporter)
		{
			FString EditorError;
			Path = EditorAssetExporter(Asset, EditorError);
			if (Error.IsEmpty())
			{
				Error = EditorError;
			}
		}
		if (Path.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s (%s): %s"), *Label, *Input->Name, Error.IsEmpty() ? *FString::Printf(TEXT("%s can't be sent to ComfyUI."), *Asset->GetName()) : *Error);
			return false;
		}
		OutValues.Add(Input->Name, Path);
	}

	// A game wants a new image each run, so a seed nobody set gets a new one rather than the graph's fixed value.
	for (const FComfyWorkflowInput& Input : Workflow->Inputs)
	{
		if (Input.Type == EComfyInputType::Seed && !OutValues.Contains(Input.Name))
		{
			OutValues.Add(Input.Name, LexToString(UComfyRuntimeLibrary::NewSeed()));
		}
	}

	if (Unknown.Num() > 0)
	{
		OutError = FString::Printf(TEXT("%s has no input named %s. Its inputs: %s."),
			*Workflow->GetDisplayNameString(),
			*FString::JoinBy(Unknown, TEXT(", "), [](const FString& Name) { return FString::Printf(TEXT("\"%s\""), *Name); }),
			*FString::Join(Workflow->GetInputNames(), TEXT(", ")));
		return false;
	}
	return true;
}
