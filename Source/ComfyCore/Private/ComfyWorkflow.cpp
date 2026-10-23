// Copyright Comfy Org. All Rights Reserved.

#include "ComfyWorkflow.h"
#include "ComfyCoreModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/SecureHash.h"
#include "Math/UnrealMathUtility.h"

namespace ComfyWorkflowPrivate
{
	/** Partner (API) node classes share these prefixes and suffixes in ComfyUI's comfy_api_nodes. */
	static bool IsPartnerNodeClass(const FString& ClassType)
	{
		static const TCHAR* Markers[] = {
			TEXT("ApiNode"), TEXT("OpenAI"), TEXT("Gemini"), TEXT("Kling"), TEXT("Runway"), TEXT("Luma"), TEXT("Pika"),
			TEXT("Ideogram"), TEXT("Recraft"), TEXT("Tripo"), TEXT("Meshy"), TEXT("Rodin"), TEXT("Minimax"), TEXT("ElevenLabs"),
			TEXT("Veo"), TEXT("ByteDance"), TEXT("Seedance"), TEXT("Seedream"), TEXT("Higgsfield"), TEXT("FluxPro"), TEXT("BFL"),
			TEXT("StabilityStable"), TEXT("Moonvalley"), TEXT("Vidu"), TEXT("Wan2"), TEXT("FishAudio"), TEXT("Tencent3D"),
		};
		for (const TCHAR* Marker : Markers)
		{
			if (ClassType.Contains(Marker))
			{
				return true;
			}
		}
		return false;
	}

	static FString ToSnake(const FString& In)
	{
		FString Out;
		bool bLastUnderscore = true;
		for (int32 i = 0; i < In.Len(); ++i)
		{
			const TCHAR C = In[i];
			if (FChar::IsAlnum(C))
			{
				if (FChar::IsUpper(C) && i > 0 && !bLastUnderscore && FChar::IsLower(In[i - 1]))
				{
					Out.AppendChar(TEXT('_'));
				}
				Out.AppendChar(FChar::ToLower(C));
				bLastUnderscore = false;
			}
			else if (!bLastUnderscore)
			{
				Out.AppendChar(TEXT('_'));
				bLastUnderscore = true;
			}
		}
		Out.RemoveFromEnd(TEXT("_"));
		return Out.IsEmpty() ? TEXT("input") : Out;
	}

	static FString NodeTitle(const TSharedPtr<FJsonObject>& Node)
	{
		const TSharedPtr<FJsonObject>* Meta = nullptr;
		FString Title;
		if (Node->TryGetObjectField(TEXT("_meta"), Meta) && Meta && (*Meta)->TryGetStringField(TEXT("title"), Title) && !Title.IsEmpty())
		{
			return Title;
		}
		return Node->GetStringField(TEXT("class_type"));
	}

	static TArray<FString> SortedNodeIds(const TSharedPtr<FJsonObject>& Graph)
	{
		TArray<FString> Ids;
		Graph->Values.GetKeys(Ids);
		Ids.Sort([](const FString& A, const FString& B)
		{
			const bool bANum = A.IsNumeric();
			const bool bBNum = B.IsNumeric();
			if (bANum && bBNum)
			{
				return FCString::Atoi64(*A) < FCString::Atoi64(*B);
			}
			return A < B;
		});
		return Ids;
	}

	static FString JsonValueToText(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return FString();
		}
		switch (Value->Type)
		{
		case EJson::String: return Value->AsString();
		case EJson::Number:
		{
			const double D = Value->AsNumber();
			if (FMath::IsNearlyEqual(D, FMath::RoundToDouble(D)) && FMath::Abs(D) < 9.0e15)
			{
				return FString::Printf(TEXT("%lld"), (int64)FMath::RoundToDouble(D));
			}
			return FString::SanitizeFloat(D);
		}
		case EJson::Boolean: return Value->AsBool() ? TEXT("true") : TEXT("false");
		default: return FString();
		}
	}

	struct FInputRule
	{
		const TCHAR* InputName;
		EComfyInputType Type;
	};

	/** Widget inputs worth exposing by default. Everything else stays in the graph and can be added in the details panel. */
	static const FInputRule ExposedInputs[] = {
		{ TEXT("text"), EComfyInputType::Text },
		{ TEXT("prompt"), EComfyInputType::Text },
		{ TEXT("negative_prompt"), EComfyInputType::Text },
		{ TEXT("tags"), EComfyInputType::Text },
		{ TEXT("lyrics"), EComfyInputType::Text },
		{ TEXT("seed"), EComfyInputType::Seed },
		{ TEXT("noise_seed"), EComfyInputType::Seed },
		{ TEXT("steps"), EComfyInputType::Int },
		{ TEXT("cfg"), EComfyInputType::Float },
		{ TEXT("denoise"), EComfyInputType::Float },
		{ TEXT("width"), EComfyInputType::Int },
		{ TEXT("height"), EComfyInputType::Int },
		{ TEXT("batch_size"), EComfyInputType::Int },
		{ TEXT("seconds"), EComfyInputType::Float },
		{ TEXT("duration"), EComfyInputType::Float },
		{ TEXT("length"), EComfyInputType::Int },
		{ TEXT("ckpt_name"), EComfyInputType::Enum },
		{ TEXT("unet_name"), EComfyInputType::Enum },
		{ TEXT("lora_name"), EComfyInputType::Enum },
	};

	static bool IsLoadNode(const FString& ClassType, FString& OutInputName)
	{
		if (ClassType == TEXT("LoadImage") || ClassType == TEXT("LoadImageMask") || ClassType == TEXT("LoadImageOutput"))
		{
			OutInputName = TEXT("image");
			return true;
		}
		if (ClassType == TEXT("LoadAudio"))
		{
			OutInputName = TEXT("audio");
			return true;
		}
		if (ClassType == TEXT("LoadVideo"))
		{
			OutInputName = TEXT("file");
			return true;
		}
		if (ClassType == TEXT("Load3D") || ClassType == TEXT("Load3DModel"))
		{
			OutInputName = TEXT("model_file");
			return true;
		}
		return false;
	}

	static bool IsSaveNode(const FString& ClassType, EComfyOutputKind& OutKind)
	{
		if (!ClassType.StartsWith(TEXT("Save")) && ClassType != TEXT("PreviewImage") && ClassType != TEXT("PreviewAudio") && ClassType != TEXT("Preview3D"))
		{
			return false;
		}
		if (ClassType.Contains(TEXT("Audio")))
		{
			OutKind = EComfyOutputKind::Audio;
		}
		else if (ClassType.Contains(TEXT("GLB")) || ClassType.Contains(TEXT("3D")) || ClassType.Contains(TEXT("Mesh")))
		{
			OutKind = EComfyOutputKind::Mesh;
		}
		else if (ClassType.Contains(TEXT("Video")) || ClassType.Contains(TEXT("Animated")) || ClassType.Contains(TEXT("WEBM")))
		{
			OutKind = EComfyOutputKind::Video;
		}
		else if (ClassType.Contains(TEXT("Text")) || ClassType.Contains(TEXT("String")))
		{
			OutKind = EComfyOutputKind::Text;
		}
		else
		{
			OutKind = EComfyOutputKind::Image;
		}
		return true;
	}
}

bool UComfyWorkflow::SetGraphFromJson(const FString& Json, bool bDetectInterface, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("The file isn't valid JSON.");
		return false;
	}
	if (Root->HasField(TEXT("nodes")) && Root->HasField(TEXT("links")))
	{
		OutError = TEXT("This is ComfyUI's canvas save format. In ComfyUI, use Export (API) and import that file instead.");
		return false;
	}
	// Some exports wrap the graph as {"prompt": {...}}. The plugin's own workflow files use the
	// same wrapper with a "comfy_unreal" manifest beside the graph.
	TSharedPtr<FJsonObject> Manifest;
	const TSharedPtr<FJsonObject>* Wrapped = nullptr;
	if (Root->TryGetObjectField(TEXT("prompt"), Wrapped) && Wrapped)
	{
		const TSharedPtr<FJsonObject>* ManifestObject = nullptr;
		if (Root->TryGetObjectField(TEXT("comfy_unreal"), ManifestObject) && ManifestObject)
		{
			Manifest = *ManifestObject;
		}
		Root = *Wrapped;
	}

	TSet<FString> Classes;
	bool bPartner = false;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Root->Values)
	{
		const TSharedPtr<FJsonObject>* Node = nullptr;
		FString ClassType;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Node) || !Node || !(*Node)->TryGetStringField(TEXT("class_type"), ClassType))
		{
			OutError = FString::Printf(TEXT("Node \"%s\" has no class_type, so this isn't an API-format workflow."), *Pair.Key);
			return false;
		}
		Classes.Add(ClassType);
		bPartner |= ComfyWorkflowPrivate::IsPartnerNodeClass(ClassType);
	}
	if (Classes.Num() == 0)
	{
		OutError = TEXT("The workflow has no nodes.");
		return false;
	}

	FString Condensed;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Condensed);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	Modify();
	ApiGraphJson = Condensed;
	GraphHash = FMD5::HashAnsiString(*Condensed);
	RequiredNodeClasses = Classes.Array();
	RequiredNodeClasses.Sort();
	bUsesPartnerNodes = bPartner;
	if (bDetectInterface)
	{
		DetectInterface();
	}
	if (Manifest.IsValid())
	{
		ApplyManifest(Manifest);
	}
	return true;
}

void UComfyWorkflow::ApplyManifest(const TSharedPtr<FJsonObject>& Manifest)
{
	FString Text;
	if (Manifest->TryGetStringField(TEXT("display_name"), Text))
	{
		DisplayName = FText::FromString(Text);
	}
	if (Manifest->TryGetStringField(TEXT("description"), Text))
	{
		Description = FText::FromString(Text);
	}
	Manifest->TryGetStringField(TEXT("category"), Category);

	auto ParseEnum = [](const UEnum* Enum, const FString& Name, int64 Fallback)
	{
		const int64 Value = Enum->GetValueByNameString(Name, EGetByNameFlags::None);
		return Value == INDEX_NONE ? Fallback : Value;
	};

	// An explicit input list replaces what detection found.
	const TArray<TSharedPtr<FJsonValue>>* InputList = nullptr;
	if (Manifest->TryGetArrayField(TEXT("inputs"), InputList))
	{
		Inputs.Reset();
		for (const TSharedPtr<FJsonValue>& Value : *InputList)
		{
			const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Entry.IsValid())
			{
				continue;
			}
			FComfyWorkflowInput& Input = Inputs.AddDefaulted_GetRef();
			Entry->TryGetStringField(TEXT("name"), Input.Name);
			Entry->TryGetStringField(TEXT("node_id"), Input.NodeId);
			Entry->TryGetStringField(TEXT("input"), Input.InputName);
			Entry->TryGetStringField(TEXT("tooltip"), Input.Tooltip);
			if (!Entry->TryGetStringField(TEXT("label"), Input.Label))
			{
				Input.Label = FName::NameToDisplayString(Input.Name, false);
			}
			if (Entry->TryGetStringField(TEXT("type"), Text))
			{
				Input.Type = EComfyInputType(ParseEnum(StaticEnum<EComfyInputType>(), Text, int64(EComfyInputType::String)));
			}
			Entry->TryGetStringArrayField(TEXT("options"), Input.Options);

			// The default comes from the graph unless the manifest overrides it.
			if (!Entry->TryGetStringField(TEXT("default"), Input.DefaultValue))
			{
				FString Error;
				const TSharedPtr<FJsonObject> Graph = ParseGraph(Error);
				const TSharedPtr<FJsonObject>* Node = nullptr;
				const TSharedPtr<FJsonObject>* NodeInputs = nullptr;
				if (Graph.IsValid() && Graph->TryGetObjectField(Input.NodeId, Node) && (*Node)->TryGetObjectField(TEXT("inputs"), NodeInputs))
				{
					(*NodeInputs)->TryGetStringField(Input.InputName, Input.DefaultValue);
				}
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* OutputList = nullptr;
	if (Manifest->TryGetArrayField(TEXT("outputs"), OutputList))
	{
		Outputs.Reset();
		for (const TSharedPtr<FJsonValue>& Value : *OutputList)
		{
			const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Entry.IsValid())
			{
				continue;
			}
			FComfyWorkflowOutput& Output = Outputs.AddDefaulted_GetRef();
			Entry->TryGetStringField(TEXT("name"), Output.Name);
			Entry->TryGetStringField(TEXT("node_id"), Output.NodeId);
			if (Entry->TryGetStringField(TEXT("kind"), Text))
			{
				Output.Kind = EComfyOutputKind(ParseEnum(StaticEnum<EComfyOutputKind>(), Text, int64(EComfyOutputKind::Image)));
			}
			if (Entry->TryGetStringField(TEXT("import"), Text))
			{
				Output.ImportRule = EComfyImportRule(ParseEnum(StaticEnum<EComfyImportRule>(), Text, int64(EComfyImportRule::Auto)));
			}
		}
	}
}

TSharedPtr<FJsonObject> UComfyWorkflow::ParseGraph(FString& OutError) const
{
	TSharedPtr<FJsonObject> Graph;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ApiGraphJson);
	if (ApiGraphJson.IsEmpty() || !FJsonSerializer::Deserialize(Reader, Graph) || !Graph.IsValid())
	{
		OutError = FString::Printf(TEXT("Workflow %s has no graph. Re-import its API-format JSON."), *GetName());
		return nullptr;
	}
	return Graph;
}

void UComfyWorkflow::DetectInterface()
{
	using namespace ComfyWorkflowPrivate;

	FString Error;
	const TSharedPtr<FJsonObject> Graph = ParseGraph(Error);
	if (!Graph.IsValid())
	{
		UE_LOG(LogComfy, Warning, TEXT("%s"), *Error);
		return;
	}

	Modify();
	Inputs.Reset();
	Outputs.Reset();
	TSet<FString> UsedNames;
	auto UniqueName = [&UsedNames](FString Name)
	{
		FString Candidate = Name;
		int32 Suffix = 2;
		while (UsedNames.Contains(Candidate))
		{
			Candidate = FString::Printf(TEXT("%s_%d"), *Name, Suffix++);
		}
		UsedNames.Add(Candidate);
		return Candidate;
	};

	int32 PositivePrompts = 0;
	for (const FString& NodeId : SortedNodeIds(Graph))
	{
		const TSharedPtr<FJsonObject> Node = Graph->GetObjectField(NodeId);
		const FString ClassType = Node->GetStringField(TEXT("class_type"));
		const FString Title = NodeTitle(Node);
		const TSharedPtr<FJsonObject>* NodeInputs = nullptr;
		Node->TryGetObjectField(TEXT("inputs"), NodeInputs);

		EComfyOutputKind OutputKind;
		if (IsSaveNode(ClassType, OutputKind))
		{
			FComfyWorkflowOutput Output;
			Output.NodeId = NodeId;
			Output.Kind = OutputKind;
			Output.Name = UniqueName(Title == ClassType ? (OutputKind == EComfyOutputKind::Audio ? TEXT("sound") : OutputKind == EComfyOutputKind::Mesh ? TEXT("mesh") : OutputKind == EComfyOutputKind::Video ? TEXT("video") : TEXT("image")) : ToSnake(Title));
			Outputs.Add(Output);
			continue;
		}

		if (!NodeInputs || !NodeInputs->IsValid())
		{
			continue;
		}

		FString LoadInputName;
		if (IsLoadNode(ClassType, LoadInputName))
		{
			FComfyWorkflowInput Input;
			Input.NodeId = NodeId;
			Input.InputName = LoadInputName;
			Input.Type = EComfyInputType::File;
			Input.Label = Title;
			Input.Name = UniqueName(Title == ClassType ? LoadInputName : ToSnake(Title));
			Input.DefaultValue = JsonValueToText((*NodeInputs)->TryGetField(LoadInputName));
			Input.Tooltip = TEXT("A file on disk to upload. Leave empty to use the file named in the graph.");
			Inputs.Add(Input);
			continue;
		}

		for (const FInputRule& Rule : ExposedInputs)
		{
			const TSharedPtr<FJsonValue> Value = (*NodeInputs)->TryGetField(Rule.InputName);
			if (!Value.IsValid() || Value->Type == EJson::Array || Value->Type == EJson::Object)
			{
				// Arrays are links to other nodes, not widget values.
				continue;
			}

			FComfyWorkflowInput Input;
			Input.NodeId = NodeId;
			Input.InputName = Rule.InputName;
			Input.Type = Rule.Type;
			Input.DefaultValue = JsonValueToText(Value);
			Input.Label = FString::Printf(TEXT("%s · %s"), *Title, Rule.InputName);

			FString Name;
			if (Rule.Type == EComfyInputType::Text)
			{
				const bool bNegative = Title.Contains(TEXT("negative"), ESearchCase::IgnoreCase) || FString(Rule.InputName).Contains(TEXT("negative"));
				if (bNegative)
				{
					Name = TEXT("negative_prompt");
				}
				else if (Title != ClassType)
				{
					Name = ToSnake(Title);
				}
				else
				{
					Name = (PositivePrompts++ == 0) ? FString(TEXT("prompt")) : FString::Printf(TEXT("prompt_%d"), PositivePrompts);
				}
			}
			else if (Rule.Type == EComfyInputType::Seed)
			{
				Name = TEXT("seed");
			}
			else
			{
				Name = Title == ClassType ? FString(Rule.InputName) : ToSnake(Title) + TEXT("_") + Rule.InputName;
			}
			Input.Name = UniqueName(Name);
			Inputs.Add(Input);
		}
	}
}

bool UComfyWorkflow::FindInput(const FString& Name, FComfyWorkflowInput& OutInput) const
{
	if (const FComfyWorkflowInput* Found = Inputs.FindByPredicate([&Name](const FComfyWorkflowInput& Input) { return Input.Name == Name; }))
	{
		OutInput = *Found;
		return true;
	}
	return false;
}

TArray<FString> UComfyWorkflow::GetInputNames() const
{
	TArray<FString> Names;
	for (const FComfyWorkflowInput& Input : Inputs)
	{
		Names.Add(Input.Name);
	}
	return Names;
}

FString UComfyWorkflow::GetInputSchemaJson() const
{
	const TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	const TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	for (const FComfyWorkflowInput& Input : Inputs)
	{
		const TSharedRef<FJsonObject> Prop = MakeShared<FJsonObject>();
		switch (Input.Type)
		{
		case EComfyInputType::Int:
		case EComfyInputType::Seed:
			Prop->SetStringField(TEXT("type"), TEXT("integer"));
			break;
		case EComfyInputType::Float:
			Prop->SetStringField(TEXT("type"), TEXT("number"));
			break;
		case EComfyInputType::Bool:
			Prop->SetStringField(TEXT("type"), TEXT("boolean"));
			break;
		default:
			Prop->SetStringField(TEXT("type"), TEXT("string"));
			break;
		}
		FString Description = Input.Label;
		if (Input.Type == EComfyInputType::File)
		{
			Description += TEXT(". Absolute path of a local file to upload.");
		}
		if (Input.Type == EComfyInputType::Seed)
		{
			Description += TEXT(". Use \"random\" for a new seed.");
		}
		Prop->SetStringField(TEXT("description"), Description);
		Prop->SetStringField(TEXT("default"), Input.DefaultValue);
		if (Input.Options.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Enum;
			for (const FString& Option : Input.Options)
			{
				Enum.Add(MakeShared<FJsonValueString>(Option));
			}
			Prop->SetArrayField(TEXT("enum"), Enum);
		}
		Properties->SetObjectField(Input.Name, Prop);
	}
	Schema->SetObjectField(TEXT("properties"), Properties);

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Schema, Writer);
	return Out;
}

UComfyWorkflow* UComfyWorkflow::CreateTransientFromJson(const FString& Json, FString& OutError)
{
	UComfyWorkflow* Workflow = NewObject<UComfyWorkflow>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!Workflow->SetGraphFromJson(Json, true, OutError))
	{
		return nullptr;
	}
	Workflow->DisplayName = FText::FromString(TEXT("Inline workflow"));
	return Workflow;
}

TSharedPtr<FJsonObject> UComfyWorkflow::BuildGraph(const TMap<FString, FString>& Values, const TMap<FString, TSharedPtr<FJsonValue>>& FileReferences, FString& OutError) const
{
	const TSharedPtr<FJsonObject> Graph = ParseGraph(OutError);
	if (!Graph.IsValid())
	{
		return nullptr;
	}

	for (const TPair<FString, FString>& Pair : Values)
	{
		if (!Inputs.ContainsByPredicate([&Pair](const FComfyWorkflowInput& Input) { return Input.Name == Pair.Key; }))
		{
			OutError = FString::Printf(TEXT("Workflow %s has no input named \"%s\". Inputs: %s"), *GetDisplayNameString(), *Pair.Key, *FString::Join(GetInputNames(), TEXT(", ")));
			return nullptr;
		}
	}

	for (const FComfyWorkflowInput& Input : Inputs)
	{
		const TSharedPtr<FJsonObject>* NodePtr = nullptr;
		if (!Graph->TryGetObjectField(Input.NodeId, NodePtr) || !NodePtr)
		{
			OutError = FString::Printf(TEXT("Input \"%s\" points at node %s, which isn't in the graph."), *Input.Name, *Input.NodeId);
			return nullptr;
		}
		const TSharedPtr<FJsonObject> NodeInputs = (*NodePtr)->GetObjectField(TEXT("inputs"));

		if (Input.Type == EComfyInputType::File)
		{
			if (const TSharedPtr<FJsonValue>* Ref = FileReferences.Find(Input.Name))
			{
				NodeInputs->SetField(Input.InputName, *Ref);
			}
			continue;
		}

		const FString* Provided = Values.Find(Input.Name);
		FString Value = Provided ? *Provided : Input.DefaultValue;
		if (Input.Type == EComfyInputType::Seed && (Value.Equals(TEXT("random"), ESearchCase::IgnoreCase) || Value.IsEmpty()))
		{
			Value = FString::Printf(TEXT("%d"), FMath::RandRange(0, MAX_int32 - 1));
		}
		if (!Provided && Input.Type != EComfyInputType::Seed)
		{
			continue;
		}

		switch (Input.Type)
		{
		case EComfyInputType::Int:
		case EComfyInputType::Seed:
			if (!Value.IsNumeric())
			{
				OutError = FString::Printf(TEXT("Input \"%s\" needs a whole number, got \"%s\"."), *Input.Name, *Value);
				return nullptr;
			}
			NodeInputs->SetNumberField(Input.InputName, (double)FCString::Atoi64(*Value));
			break;
		case EComfyInputType::Float:
			if (!Value.IsNumeric())
			{
				OutError = FString::Printf(TEXT("Input \"%s\" needs a number, got \"%s\"."), *Input.Name, *Value);
				return nullptr;
			}
			NodeInputs->SetNumberField(Input.InputName, FCString::Atod(*Value));
			break;
		case EComfyInputType::Bool:
			NodeInputs->SetBoolField(Input.InputName, Value.ToBool());
			break;
		default:
			NodeInputs->SetStringField(Input.InputName, Value);
			break;
		}
	}
	return Graph;
}

const FComfyWorkflowOutput* UComfyWorkflow::FindOutputForNode(const FString& NodeId) const
{
	return Outputs.FindByPredicate([&NodeId](const FComfyWorkflowOutput& Output) { return Output.NodeId == NodeId; });
}

FString UComfyWorkflow::GetDisplayNameString() const
{
	return DisplayName.IsEmpty() ? GetName() : DisplayName.ToString();
}
