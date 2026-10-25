// Copyright Comfy Org. All Rights Reserved.

#include "ComfyBuiltInAssets.h"
#include "ComfyCoreModule.h"
#include "ComfySubsystem.h"
#include "ComfyWorkflow.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

void ComfyBuiltInAssets::Sync()
{
	if (IsRunningCommandlet() || !GIsEditor)
	{
		return;
	}
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ComfyUnreal"));
	if (!Plugin.IsValid() || !Plugin->CanContainContent())
	{
		return;
	}

	const FString Folder = Plugin->GetBaseDir() / TEXT("Resources") / TEXT("Workflows");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.json")), true, false);

	int32 SavedCount = 0;
	for (const FString& File : Files)
	{
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *(Folder / File)))
		{
			continue;
		}
		const FString Hash = FMD5::HashAnsiString(*Json);
		const FString AssetName = FPaths::GetBaseFilename(File);
		const FString PackageName = FString(UComfyWorkflow::BuiltInAssetPath) / AssetName;

		UComfyWorkflow* Workflow = FPackageName::DoesPackageExist(PackageName)
			? LoadObject<UComfyWorkflow>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet)
			: nullptr;
		if (Workflow && Workflow->BuiltInSourceHash == Hash)
		{
			continue;
		}

		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		if (IFileManager::Get().FileExists(*Filename) && IFileManager::Get().IsReadOnly(*Filename))
		{
			UE_LOG(LogComfy, Warning, TEXT("Built-in workflow %s changed, but %s is read-only (checked in?). Studio uses the JSON until it's writable."), *File, *Filename);
			continue;
		}

		const bool bNew = Workflow == nullptr;
		UPackage* Package = bNew ? CreatePackage(*PackageName) : Workflow->GetPackage();
		if (bNew)
		{
			Workflow = NewObject<UComfyWorkflow>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		}
		FString Error;
		if (!Workflow->SetGraphFromJson(Json, true, Error))
		{
			UE_LOG(LogComfy, Warning, TEXT("Skipped built-in workflow %s: %s"), *File, *Error);
			continue;
		}
		if (Workflow->DisplayName.IsEmpty())
		{
			Workflow->DisplayName = FText::FromString(FName::NameToDisplayString(AssetName, false));
		}
		Workflow->BuiltInSourceHash = Hash;
		Package->MarkPackageDirty();
		if (bNew)
		{
			FAssetRegistryModule::AssetCreated(Workflow);
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (UPackage::SavePackage(Package, Workflow, *Filename, SaveArgs))
		{
			++SavedCount;
		}
		else
		{
			UE_LOG(LogComfy, Warning, TEXT("Couldn't save built-in workflow asset %s."), *Filename);
		}
	}

	if (SavedCount > 0)
	{
		UE_LOG(LogComfy, Log, TEXT("Saved %d built-in workflow asset(s) in %s."), SavedCount, UComfyWorkflow::BuiltInAssetPath);
		if (UComfySubsystem* Comfy = UComfySubsystem::Get())
		{
			Comfy->GetBuiltInWorkflows(true);
		}
	}
}
