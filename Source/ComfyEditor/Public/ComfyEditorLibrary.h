// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ComfyTypes.h"
#include "ComfyEditorLibrary.generated.h"

class UComfyJob;
class UComfyProvenanceUserData;
class UComfyWorkflow;
class UMaterialInstanceConstant;
class UMaterialInterface;
class UTexture;

/** Options for turning a texture job into a material. */
USTRUCT(BlueprintType)
struct COMFYEDITOR_API FComfyMaterialOptions
{
	GENERATED_BODY()

	/** Blend the image so it repeats without seams before importing it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	bool bMakeTileable = true;

	/** Derive a normal map from the image when the workflow doesn't produce one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	bool bDeriveNormalMap = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy", meta = (ClampMin = "0", ClampMax = "20"))
	float NormalStrength = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy", meta = (ClampMin = "0", ClampMax = "1"))
	float Roughness = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy", meta = (ClampMin = "0.01"))
	float Tiling = 1.f;

	/** Put the material on every mesh slot of the actors selected in the level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	bool bAssignToSelection = false;
};

/**
 * Editor-only steps that turn Comfy results into project assets. Shared by Comfy Studio,
 * editor scripts (Python: unreal.ComfyEditorLibrary) and the agent toolset.
 */
UCLASS()
class COMFYEDITOR_API UComfyEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Import an API-format workflow JSON file as a Comfy Workflow asset in DestinationPath (for example /Game/Comfy/Workflows). */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Editor")
	static UComfyWorkflow* ImportWorkflowFromFile(const FString& FilePath, const FString& DestinationPath, FString& OutError);

	/** Save a transient workflow (such as a built-in one) into the project so it can be edited. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Editor")
	static UComfyWorkflow* SaveWorkflowAsAsset(UComfyWorkflow* Workflow, const FString& DestinationPath, const FString& AssetName);

	/** Every Comfy Workflow asset in the project, then the plugin's built-in workflows. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Editor")
	static TArray<UComfyWorkflow*> GetAllWorkflows();

	/** Find a workflow by asset path, object name or display name. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Editor")
	static UComfyWorkflow* FindWorkflow(const FString& NameOrPath);

	/**
	 * Import a finished job's outputs as assets: images become textures (normal maps and masks
	 * get linear compression), audio becomes sound waves, glb/gltf/fbx/obj become static meshes.
	 * Each asset records which job made it. Empty DestinationPath uses the project setting.
	 */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Editor")
	static TArray<UObject*> ImportJobOutputs(UComfyJob* Job, const FString& DestinationPath, bool bSave = true);

	/** Import one file with an explicit rule. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Editor")
	static TArray<UObject*> ImportFile(const FString& FilePath, const FString& DestinationPath, const FString& AssetName, EComfyImportRule Rule, bool bSave = true);

	/**
	 * Build a material instance from a texture job: optionally make the image tileable and
	 * derive a normal map, import both, and create an instance of the plugin's PBR material.
	 */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Editor")
	static UMaterialInstanceConstant* CreateMaterialFromJob(UComfyJob* Job, const FString& DestinationPath, const FString& MaterialName, const FComfyMaterialOptions& Options);

	/** Create an instance of the Comfy PBR material (created on first use in /Game/Comfy/Materials). */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Editor")
	static UMaterialInstanceConstant* CreatePBRMaterialInstance(const FString& DestinationPath, const FString& MaterialName, UTexture* BaseColor, UTexture* Normal, float Roughness = 0.6f, float Tiling = 1.f);

	/** Assign a material to every slot of the selected actors' meshes. Returns how many components changed. Undoable. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Editor")
	static int32 AssignMaterialToSelectedActors(UMaterialInterface* Material);

	/** The Comfy record on an asset the plugin imported, or null. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Editor")
	static UComfyProvenanceUserData* GetProvenance(UObject* Asset);

	/** The base PBR material, created and saved on first use. */
	static UMaterialInterface* GetOrCreateBaseMaterial();

	/** Package-safe asset name from free text. */
	static FString MakeAssetName(const FString& Prefix, const FString& Text);

	/**
	 * Save a graph from the ComfyUI frontend as a workflow asset in /Game/Comfy/Workflows, or update the one with that name.
	 * WorkflowJson is the canvas save (kept for reopening, and its extra.linearData picks the inputs when App mode set them);
	 * ApiJson is the API-format prompt that runs. Inputs are detected from the graph otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Editor")
	static UComfyWorkflow* SaveGraphAsWorkflow(const FString& WorkflowJson, const FString& ApiJson, const FString& Name, FString& OutError);

	/**
	 * A file on disk for a workflow's File input from a project asset: a texture's source image is written to
	 * Saved/Comfy/Inputs as PNG; a mesh or sound gives the file it was imported from. Empty with OutError otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Editor")
	static FString ExportAssetForInput(UObject* Asset, FString& OutError);

	/** Save the active level viewport as a PNG in Saved/Comfy/Inputs and return its path, for a File input. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Editor")
	static FString CaptureActiveViewport(FString& OutError);

	/** Open the Comfy Studio tab, or bring it to the front. View is "Run" or "Graph"; empty keeps the current one. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Editor")
	static bool OpenStudio(const FString& View);
};
