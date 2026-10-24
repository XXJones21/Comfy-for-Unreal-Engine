// Copyright Comfy Org. All Rights Reserved.

#include "ComfyEditorLibrary.h"
#include "ComfyEditorModule.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "ComfyCoreModule.h"
#include "ComfyJob.h"
#include "ComfyProvenanceUserData.h"
#include "ComfyRuntimeLibrary.h"
#include "ComfySettings.h"
#include "ComfySubsystem.h"
#include "ComfyWorkflow.h"
#include "ComfyWorkflowFactory.h"
#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/MeshComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "ComfyEditor"

namespace ComfyEditorPrivate
{
	static const TCHAR* BaseMaterialFolder = TEXT("/Game/Comfy/Materials");
	static const TCHAR* BaseMaterialName = TEXT("M_Comfy_PBR");

	static FString ResolveDestination(const FString& DestinationPath)
	{
		FString Path = DestinationPath.IsEmpty() ? GetDefault<UComfySettings>()->DefaultImportFolder : DestinationPath;
		Path.RemoveFromEnd(TEXT("/"));
		return Path;
	}

	static void SavePackagesOf(const TArray<UObject*>& Objects)
	{
		TArray<UPackage*> Packages;
		for (UObject* Object : Objects)
		{
			if (Object)
			{
				Packages.AddUnique(Object->GetOutermost());
			}
		}
		if (Packages.Num() > 0)
		{
			UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
		}
	}

	static FString JobSource(const UComfyJob* Job)
	{
		if (!Job->Workflow)
		{
			return Job->ModelId;
		}
		return Job->Workflow->HasAnyFlags(RF_Transient) ? Job->Workflow->SourceFile : Job->Workflow->GetPathName();
	}

	static void AttachProvenance(UObject* Asset, const UComfyJob* Job, const FString& OutputName)
	{
		IInterface_AssetUserData* Owner = Cast<IInterface_AssetUserData>(Asset);
		if (!Owner || !Job)
		{
			return;
		}
		Asset->Modify();
		Owner->RemoveUserDataOfClass(UComfyProvenanceUserData::StaticClass());

		UComfyProvenanceUserData* Record = NewObject<UComfyProvenanceUserData>(Asset, NAME_None, RF_Public | RF_Transactional);
		Record->Source = JobSource(Job);
		Record->GraphHash = Job->Workflow ? Job->Workflow->GraphHash : FString();
		Record->Backend = Job->Backend;
		Record->JobId = Job->JobId;
		Record->RemoteId = Job->RemoteId;
		Record->OutputName = OutputName;
		Record->Inputs = Job->Inputs;
		Record->CreatedAt = Job->FinishedAt;
		Owner->AddAssetUserData(Record);
		Asset->MarkPackageDirty();
	}

	static EComfyImportRule ResolveRule(const FComfyOutputFile& File)
	{
		if (File.ImportRule != EComfyImportRule::Auto)
		{
			return File.ImportRule;
		}
		const FString Name = File.Name.ToLower();
		switch (File.Kind)
		{
		case EComfyOutputKind::Image:
			if (Name.Contains(TEXT("normal")))
			{
				return EComfyImportRule::NormalMap;
			}
			if (Name.Contains(TEXT("rough")) || Name.Contains(TEXT("metal")) || Name.Contains(TEXT("orm")) || Name.Contains(TEXT("mask"))
				|| Name.Contains(TEXT("height")) || Name.Contains(TEXT("ao")))
			{
				return EComfyImportRule::Masks;
			}
			return EComfyImportRule::Texture;
		case EComfyOutputKind::Audio:
			return EComfyImportRule::Sound;
		case EComfyOutputKind::Mesh:
			return EComfyImportRule::StaticMesh;
		default:
			return EComfyImportRule::None;
		}
	}

	static const TCHAR* PrefixFor(EComfyImportRule Rule)
	{
		switch (Rule)
		{
		case EComfyImportRule::Sound: return TEXT("SW_");
		case EComfyImportRule::StaticMesh: return TEXT("SM_");
		case EComfyImportRule::None: return TEXT("");
		default: return TEXT("T_");
		}
	}

	static void ApplyTextureRule(UTexture* Texture, EComfyImportRule Rule)
	{
		if (!Texture)
		{
			return;
		}
		Texture->PreEditChange(nullptr);
		switch (Rule)
		{
		case EComfyImportRule::NormalMap:
			Texture->CompressionSettings = TC_Normalmap;
			Texture->SRGB = false;
			Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;
			break;
		case EComfyImportRule::Masks:
			Texture->CompressionSettings = TC_Masks;
			Texture->SRGB = false;
			break;
		case EComfyImportRule::UserInterface:
			Texture->CompressionSettings = TC_EditorIcon;
			Texture->LODGroup = TEXTUREGROUP_UI;
			Texture->MipGenSettings = TMGS_NoMipmaps;
			break;
		default:
			Texture->CompressionSettings = TC_Default;
			Texture->SRGB = true;
			break;
		}
		Texture->PostEditChange();
	}

	static UTexture* FirstTexture(const TArray<UObject*>& Objects)
	{
		for (UObject* Object : Objects)
		{
			if (UTexture* Texture = Cast<UTexture>(Object))
			{
				return Texture;
			}
		}
		return nullptr;
	}
}

FString UComfyEditorLibrary::MakeAssetName(const FString& Prefix, const FString& Text)
{
	FString Clean;
	bool bLastUnderscore = false;
	for (const TCHAR Char : Text)
	{
		if (FChar::IsAlnum(Char))
		{
			Clean.AppendChar(Char);
			bLastUnderscore = false;
		}
		else if (!bLastUnderscore && !Clean.IsEmpty())
		{
			Clean.AppendChar(TEXT('_'));
			bLastUnderscore = true;
		}
	}
	Clean.RemoveFromEnd(TEXT("_"));
	Clean.LeftInline(48);
	if (Clean.IsEmpty())
	{
		Clean = TEXT("Comfy");
	}
	return Prefix + Clean;
}

UComfyWorkflow* UComfyEditorLibrary::ImportWorkflowFromFile(const FString& FilePath, const FString& DestinationPath, FString& OutError)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *FilePath))
	{
		OutError = FString::Printf(TEXT("Couldn't read %s."), *FilePath);
		return nullptr;
	}
	// Validate first so the caller gets the reason, not just an empty import.
	if (!UComfyWorkflow::CreateTransientFromJson(Json, OutError))
	{
		return nullptr;
	}

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->Filename = FilePath;
	Task->DestinationPath = DestinationPath.IsEmpty() ? FString(TEXT("/Game/Comfy/Workflows")) : DestinationPath;
	Task->DestinationName = MakeAssetName(TEXT("CW_"), FPaths::GetBaseFilename(FilePath));
	Task->Factory = NewObject<UComfyWorkflowFactory>();
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->bSave = true;
	FAssetToolsModule::GetModule().Get().ImportAssetTasks({ Task });

	for (UObject* Object : Task->GetObjects())
	{
		if (UComfyWorkflow* Workflow = Cast<UComfyWorkflow>(Object))
		{
			return Workflow;
		}
	}
	OutError = TEXT("The import didn't create a workflow asset. See the Output Log.");
	return nullptr;
}

UComfyWorkflow* UComfyEditorLibrary::SaveWorkflowAsAsset(UComfyWorkflow* Workflow, const FString& DestinationPath, const FString& AssetName)
{
	if (!Workflow)
	{
		return nullptr;
	}
	FString PackageName;
	FString Name;
	const FString Folder = DestinationPath.IsEmpty() ? FString(TEXT("/Game/Comfy/Workflows")) : DestinationPath;
	FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(Folder / MakeAssetName(TEXT("CW_"), AssetName.IsEmpty() ? Workflow->GetDisplayNameString() : AssetName), TEXT(""), PackageName, Name);

	UPackage* Package = CreatePackage(*PackageName);
	UComfyWorkflow* Copy = DuplicateObject<UComfyWorkflow>(Workflow, Package, FName(*Name));
	Copy->ClearFlags(RF_Transient);
	Copy->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
	FAssetRegistryModule::AssetCreated(Copy);
	Copy->MarkPackageDirty();
	ComfyEditorPrivate::SavePackagesOf({ Copy });
	return Copy;
}

TArray<UComfyWorkflow*> UComfyEditorLibrary::GetAllWorkflows()
{
	TArray<UComfyWorkflow*> Result;
	TArray<FAssetData> Assets;
	FAssetRegistryModule::GetRegistry().GetAssetsByClass(UComfyWorkflow::StaticClass()->GetClassPathName(), Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
	for (const FAssetData& Asset : Assets)
	{
		if (UComfyWorkflow* Workflow = Cast<UComfyWorkflow>(Asset.GetAsset()))
		{
			Result.Add(Workflow);
		}
	}
	if (UComfySubsystem* Comfy = UComfySubsystem::Get())
	{
		Result.Append(Comfy->GetBuiltInWorkflows());
	}
	return Result;
}

UComfyWorkflow* UComfyEditorLibrary::FindWorkflow(const FString& NameOrPath)
{
	if (NameOrPath.StartsWith(TEXT("/")))
	{
		if (UComfyWorkflow* Loaded = LoadObject<UComfyWorkflow>(nullptr, *NameOrPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Loaded;
		}
	}
	for (UComfyWorkflow* Workflow : GetAllWorkflows())
	{
		if (Workflow->GetName().Equals(NameOrPath, ESearchCase::IgnoreCase)
			|| Workflow->GetPathName().Equals(NameOrPath, ESearchCase::IgnoreCase)
			|| Workflow->GetDisplayNameString().Equals(NameOrPath, ESearchCase::IgnoreCase))
		{
			return Workflow;
		}
	}
	return nullptr;
}

TArray<UObject*> UComfyEditorLibrary::ImportFile(const FString& FilePath, const FString& DestinationPath, const FString& AssetName, EComfyImportRule Rule, bool bSave)
{
	using namespace ComfyEditorPrivate;

	if (Rule == EComfyImportRule::None || !FPaths::FileExists(FilePath))
	{
		return {};
	}

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->Filename = FilePath;
	Task->DestinationPath = ResolveDestination(DestinationPath);
	Task->DestinationName = AssetName;
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->bSave = false;
	FAssetToolsModule::GetModule().Get().ImportAssetTasks({ Task });

	TArray<UObject*> Objects = Task->GetObjects();
	for (UObject* Object : Objects)
	{
		ApplyTextureRule(Cast<UTexture>(Object), Rule);
	}
	if (Objects.Num() == 0)
	{
		UE_LOG(LogComfy, Warning, TEXT("Importing %s created no assets. See the Output Log for the importer's message."), *FilePath);
	}
	if (bSave)
	{
		SavePackagesOf(Objects);
	}
	return Objects;
}

TArray<UObject*> UComfyEditorLibrary::ImportJobOutputs(UComfyJob* Job, const FString& DestinationPath, bool bSave)
{
	using namespace ComfyEditorPrivate;

	TArray<UObject*> Imported;
	if (!Job || !Job->Succeeded())
	{
		return Imported;
	}

	const FString ShortId = Job->JobId.Left(6);
	for (const FComfyOutputFile& File : Job->Outputs)
	{
		const EComfyImportRule Rule = ResolveRule(File);
		if (Rule == EComfyImportRule::None)
		{
			continue;
		}
		const FString Name = MakeAssetName(PrefixFor(Rule), FString::Printf(TEXT("%s_%s_%s_%s"), *Job->Title, *File.Name, *FPaths::GetBaseFilename(File.FileName), *ShortId));
		for (UObject* Object : ImportFile(File.LocalPath, DestinationPath, Name, Rule, false))
		{
			AttachProvenance(Object, Job, File.Name);
			Imported.Add(Object);
		}
	}
	if (bSave)
	{
		SavePackagesOf(Imported);
	}
	return Imported;
}

UMaterialInterface* UComfyEditorLibrary::GetOrCreateBaseMaterial()
{
	using namespace ComfyEditorPrivate;

	const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), BaseMaterialFolder, BaseMaterialName, BaseMaterialName);
	if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return Existing;
	}

	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UMaterial* Material = Cast<UMaterial>(FAssetToolsModule::GetModule().Get().CreateAsset(BaseMaterialName, BaseMaterialFolder, UMaterial::StaticClass(), Factory));
	if (!Material)
	{
		return nullptr;
	}

	auto* Coordinates = Cast<UMaterialExpressionTextureCoordinate>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), -900, 0));
	auto* Tiling = Cast<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -900, 120));
	Tiling->ParameterName = TEXT("Tiling");
	Tiling->DefaultValue = 1.f;
	auto* Multiply = UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), -700, 40);
	UMaterialEditingLibrary::ConnectMaterialExpressions(Coordinates, TEXT(""), Multiply, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Tiling, TEXT(""), Multiply, TEXT("B"));

	auto* BaseColor = Cast<UMaterialExpressionTextureSampleParameter2D>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -450, -200));
	BaseColor->ParameterName = TEXT("BaseColor");
	BaseColor->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
	BaseColor->SamplerType = SAMPLERTYPE_Color;
	UMaterialEditingLibrary::ConnectMaterialExpressions(Multiply, TEXT(""), BaseColor, TEXT("UVs"));
	UMaterialEditingLibrary::ConnectMaterialProperty(BaseColor, TEXT("RGB"), MP_BaseColor);

	auto* Normal = Cast<UMaterialExpressionTextureSampleParameter2D>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -450, 150));
	Normal->ParameterName = TEXT("Normal");
	Normal->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"));
	Normal->SamplerType = SAMPLERTYPE_Normal;
	UMaterialEditingLibrary::ConnectMaterialExpressions(Multiply, TEXT(""), Normal, TEXT("UVs"));
	UMaterialEditingLibrary::ConnectMaterialProperty(Normal, TEXT("RGB"), MP_Normal);

	auto* Roughness = Cast<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -450, 450));
	Roughness->ParameterName = TEXT("Roughness");
	Roughness->DefaultValue = 0.6f;
	UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, TEXT(""), MP_Roughness);

	UMaterialEditingLibrary::RecompileMaterial(Material);
	ComfyEditorPrivate::SavePackagesOf({ Material });
	return Material;
}

UMaterialInstanceConstant* UComfyEditorLibrary::CreatePBRMaterialInstance(const FString& DestinationPath, const FString& MaterialName, UTexture* BaseColor, UTexture* Normal, float Roughness, float Tiling)
{
	UMaterialInterface* Parent = GetOrCreateBaseMaterial();
	if (!Parent)
	{
		return nullptr;
	}

	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
	FString PackageName;
	FString Name;
	AssetTools.CreateUniqueAssetName(ComfyEditorPrivate::ResolveDestination(DestinationPath) / MakeAssetName(TEXT("MI_"), MaterialName), TEXT(""), PackageName, Name);

	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	Factory->InitialParent = Parent;
	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(AssetTools.CreateAsset(Name, FPackageName::GetLongPackagePath(PackageName), UMaterialInstanceConstant::StaticClass(), Factory));
	if (!Instance)
	{
		return nullptr;
	}

	if (BaseColor)
	{
		UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, TEXT("BaseColor"), BaseColor);
	}
	if (Normal)
	{
		UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, TEXT("Normal"), Normal);
	}
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("Roughness"), Roughness);
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("Tiling"), Tiling);
	UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
	return Instance;
}

UMaterialInstanceConstant* UComfyEditorLibrary::CreateMaterialFromJob(UComfyJob* Job, const FString& DestinationPath, const FString& MaterialName, const FComfyMaterialOptions& Options)
{
	using namespace ComfyEditorPrivate;

	if (!Job || !Job->Succeeded())
	{
		return nullptr;
	}

	const FComfyOutputFile* Color = nullptr;
	const FComfyOutputFile* NormalOutput = nullptr;
	for (const FComfyOutputFile& File : Job->Outputs)
	{
		if (File.Kind != EComfyOutputKind::Image)
		{
			continue;
		}
		const EComfyImportRule Rule = ResolveRule(File);
		if (Rule == EComfyImportRule::NormalMap && !NormalOutput)
		{
			NormalOutput = &File;
		}
		else if (Rule == EComfyImportRule::Texture && !Color)
		{
			Color = &File;
		}
	}
	if (!Color)
	{
		UE_LOG(LogComfy, Warning, TEXT("Job %s has no color image to build a material from."), *Job->JobId);
		return nullptr;
	}

	const FString BaseName = MaterialName.IsEmpty() ? FString::Printf(TEXT("%s_%s"), *Job->Title, *Job->JobId.Left(6)) : MaterialName;
	const FString WorkFolder = Job->GetOutputDirectory();

	FString ColorPath = Color->LocalPath;
	if (Options.bMakeTileable)
	{
		const FString Tiled = WorkFolder / FPaths::GetBaseFilename(Color->FileName) + TEXT("_tile.png");
		if (UComfyRuntimeLibrary::MakeTileable(ColorPath, Tiled))
		{
			ColorPath = Tiled;
		}
	}

	FString NormalPath = NormalOutput ? NormalOutput->LocalPath : FString();
	if (NormalPath.IsEmpty() && Options.bDeriveNormalMap)
	{
		const FString Derived = WorkFolder / FPaths::GetBaseFilename(Color->FileName) + TEXT("_normal.png");
		if (UComfyRuntimeLibrary::MakeNormalMap(ColorPath, Derived, Options.NormalStrength))
		{
			NormalPath = Derived;
		}
	}

	TArray<UObject*> Created;
	UTexture* BaseTexture = FirstTexture(ImportFile(ColorPath, DestinationPath, MakeAssetName(TEXT("T_"), BaseName + TEXT("_D")), EComfyImportRule::Texture, false));
	UTexture* NormalTexture = NormalPath.IsEmpty() ? nullptr : FirstTexture(ImportFile(NormalPath, DestinationPath, MakeAssetName(TEXT("T_"), BaseName + TEXT("_N")), EComfyImportRule::NormalMap, false));
	if (BaseTexture)
	{
		AttachProvenance(BaseTexture, Job, Color->Name);
		Created.Add(BaseTexture);
	}
	if (NormalTexture)
	{
		AttachProvenance(NormalTexture, Job, NormalOutput ? NormalOutput->Name : FString(TEXT("derived_normal")));
		Created.Add(NormalTexture);
	}

	UMaterialInstanceConstant* Instance = CreatePBRMaterialInstance(DestinationPath, BaseName, BaseTexture, NormalTexture, Options.Roughness, Options.Tiling);
	if (Instance)
	{
		AttachProvenance(Instance, Job, Color->Name);
		Created.Add(Instance);
		if (Options.bAssignToSelection)
		{
			AssignMaterialToSelectedActors(Instance);
		}
	}
	SavePackagesOf(Created);
	return Instance;
}

int32 UComfyEditorLibrary::AssignMaterialToSelectedActors(UMaterialInterface* Material)
{
	if (!Material || !GEditor)
	{
		return 0;
	}

	const FScopedTransaction Transaction(LOCTEXT("AssignMaterial", "Assign Comfy Material"));
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
			const int32 Slots = FMath::Max(1, Component->GetNumMaterials());
			Component->Modify();
			for (int32 Slot = 0; Slot < Slots; ++Slot)
			{
				Component->SetMaterial(Slot, Material);
			}
			++Changed;
		}
	}
	return Changed;
}

UComfyProvenanceUserData* UComfyEditorLibrary::GetProvenance(UObject* Asset)
{
	IInterface_AssetUserData* Owner = Cast<IInterface_AssetUserData>(Asset);
	return Owner ? Cast<UComfyProvenanceUserData>(Owner->GetAssetUserDataOfClass(UComfyProvenanceUserData::StaticClass())) : nullptr;
}

namespace ComfyEditorPrivate
{
	/** The [node id, input name] pairs App mode exposed, from a canvas save's extra.linearData. Empty when absent or unreadable. */
	static TArray<TPair<FString, FString>> ReadLinearInputs(const FString& WorkflowJson)
	{
		TArray<TPair<FString, FString>> Pairs;
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(WorkflowJson);
		const TSharedPtr<FJsonObject>* Extra = nullptr;
		const TSharedPtr<FJsonObject>* Linear = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()
			|| !Root->TryGetObjectField(TEXT("extra"), Extra) || !(*Extra)->TryGetObjectField(TEXT("linearData"), Linear)
			|| !(*Linear)->TryGetArrayField(TEXT("inputs"), Inputs))
		{
			return Pairs;
		}
		// Accept [node, input] pairs or {nodeId, widgetName}-style objects, since the format is young.
		for (const TSharedPtr<FJsonValue>& Item : *Inputs)
		{
			const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
			const TSharedPtr<FJsonObject>* Object = nullptr;
			FString Node, Input;
			if (Item->TryGetArray(Pair) && Pair->Num() >= 2)
			{
				Node = (*Pair)[0]->Type == EJson::Number ? FString::FromInt(int32((*Pair)[0]->AsNumber())) : (*Pair)[0]->AsString();
				Input = (*Pair)[1]->AsString();
			}
			else if (Item->TryGetObject(Object))
			{
				for (const TCHAR* Key : { TEXT("nodeId"), TEXT("node_id"), TEXT("node") })
				{
					double Number = 0;
					if ((*Object)->TryGetStringField(Key, Node))
					{
						break;
					}
					if ((*Object)->TryGetNumberField(Key, Number))
					{
						Node = FString::FromInt(int32(Number));
						break;
					}
				}
				for (const TCHAR* Key : { TEXT("widgetName"), TEXT("name"), TEXT("input") })
				{
					if ((*Object)->TryGetStringField(Key, Input))
					{
						break;
					}
				}
			}
			if (!Node.IsEmpty() && !Input.IsEmpty())
			{
				Pairs.Emplace(Node, Input);
			}
		}
		return Pairs;
	}
}

UComfyWorkflow* UComfyEditorLibrary::SaveGraphAsWorkflow(const FString& WorkflowJson, const FString& ApiJson, const FString& Name, FString& OutError)
{
	const FString Folder = TEXT("/Game/Comfy/Workflows");
	const FString AssetName = MakeAssetName(TEXT("CW_"), FPaths::GetBaseFilename(Name.IsEmpty() ? FString(TEXT("Graph")) : Name));
	const FString PackageName = Folder / AssetName;

	UComfyWorkflow* Workflow = LoadObject<UComfyWorkflow>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet);
	const bool bExisting = Workflow != nullptr;
	if (!Workflow)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Workflow = NewObject<UComfyWorkflow>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	}
	if (!Workflow->SetGraphFromJson(ApiJson, !bExisting, OutError))
	{
		return nullptr;
	}
	if (bExisting && Workflow->Inputs.Num() == 0)
	{
		Workflow->DetectInterface();
	}
	Workflow->UiGraphJson = WorkflowJson;
	if (Workflow->DisplayName.IsEmpty())
	{
		Workflow->DisplayName = FText::FromString(FPaths::GetBaseFilename(Name.IsEmpty() ? FString(TEXT("Graph")) : Name));
	}

	// App mode's chosen inputs replace the detected ones; anything it names that detection missed is added as text.
	const TArray<TPair<FString, FString>> Linear = ComfyEditorPrivate::ReadLinearInputs(WorkflowJson);
	if (Linear.Num() > 0)
	{
		TArray<FComfyWorkflowInput> Chosen;
		for (const TPair<FString, FString>& Pair : Linear)
		{
			const FComfyWorkflowInput* Detected = Workflow->Inputs.FindByPredicate([&Pair](const FComfyWorkflowInput& Input) { return Input.NodeId == Pair.Key && Input.InputName == Pair.Value; });
			FComfyWorkflowInput Input = Detected ? *Detected : FComfyWorkflowInput();
			if (!Detected)
			{
				Input.NodeId = Pair.Key;
				Input.InputName = Pair.Value;
				Input.Name = FString::Printf(TEXT("%s_%s"), *Pair.Value, *Pair.Key);
				Input.Label = Pair.Value;
			}
			Chosen.Add(Input);
		}
		Workflow->Inputs = Chosen;
	}

	if (!bExisting)
	{
		FAssetRegistryModule::AssetCreated(Workflow);
	}
	Workflow->MarkPackageDirty();
	ComfyEditorPrivate::SavePackagesOf({ Workflow });
	return Workflow;
}

bool UComfyEditorLibrary::OpenStudio(const FString& View)
{
	const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(FComfyEditorModule::StudioTabName);
	if (Tab.IsValid() && !View.IsEmpty())
	{
		FComfyEditorModule::SetStudioView(View);
	}
	return Tab.IsValid();
}

#undef LOCTEXT_NAMESPACE
