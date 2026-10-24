// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ComfyTypes.h"
#include "ComfyWorkflow.generated.h"

class FJsonObject;
class FJsonValue;

/**
 * A ComfyUI workflow packaged for Unreal: the API-format graph (what ComfyUI's
 * "Export (API)" writes) plus a manifest of the inputs people may change and the
 * outputs that come back. The manifest drives the editor form, Blueprint and
 * Python calls, and the agent tool schema.
 */
UCLASS(BlueprintType)
class COMFYCORE_API UComfyWorkflow : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Workflow", AssetRegistrySearchable)
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Workflow", meta = (MultiLine = true))
	FText Description;

	/** Workflow type: Material, Texture, Sound, Music, Mesh, Upscale or Other. Older files say Textures, Audio, Meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Workflow", AssetRegistrySearchable)
	FString Category;

	/** Free grouping inside a type, from the manifest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Workflow", AssetRegistrySearchable)
	FString Group;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Manifest")
	TArray<FComfyWorkflowInput> Inputs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Manifest")
	TArray<FComfyWorkflowOutput> Outputs;

	/** Node classes the graph needs. Checked against a backend before submitting. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Graph")
	TArray<FString> RequiredNodeClasses;

	/** True when the graph uses partner (API) nodes, which spend Comfy credits on every backend. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Graph", AssetRegistrySearchable)
	bool bUsesPartnerNodes = false;

	/** MD5 of the graph, stored on generated assets so you can tell which version made them. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Graph", AssetRegistrySearchable)
	FString GraphHash;

	/** File the graph was imported from. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Graph")
	FString SourceFile;

	/** The canvas (UI) save of the graph when it came from the ComfyUI frontend, so the Graph view can reopen it with layout. */
	UPROPERTY(VisibleAnywhere, Category = "Graph", AdvancedDisplay, meta = (MultiLine = true))
	FString UiGraphJson;

	/** The API-format graph, verbatim. Re-import the JSON to change it; don't edit by hand. */
	UPROPERTY(VisibleAnywhere, Category = "Graph", AdvancedDisplay, meta = (MultiLine = true))
	FString ApiGraphJson;

	/**
	 * Load an API-format graph. Rejects the canvas save format (with "nodes" and "links").
	 * When bDetectInterface is true, fills Inputs and Outputs from the graph.
	 */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Workflow")
	bool SetGraphFromJson(const FString& Json, bool bDetectInterface, FString& OutError);

	/** Rebuild Inputs and Outputs from the graph: prompts, seeds, sizes, steps, model names, load and save nodes. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Workflow")
	void DetectInterface();

	UFUNCTION(BlueprintPure, Category = "Comfy|Workflow")
	bool FindInput(const FString& Name, FComfyWorkflowInput& OutInput) const;

	UFUNCTION(BlueprintPure, Category = "Comfy|Workflow")
	TArray<FString> GetInputNames() const;

	/** The manifest as JSON Schema, for agents and tooling. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Workflow")
	FString GetInputSchemaJson() const;

	/**
	 * Apply the comfy_unreal manifest stored in a ComfyUI canvas save (extra.comfy_unreal), as the
	 * Comfy for Unreal extension writes it. Returns false when the save carries none.
	 */
	bool ApplyManifestFromCanvas(const FString& CanvasJson);

	/** Apply a comfy_unreal manifest object (docs/MANIFEST.md). Inputs and outputs it lists replace what detection found. */
	void ApplyManifestObject(const TSharedPtr<FJsonObject>& Manifest);

	/** Create a transient workflow from API-format JSON, for runtime use without an asset. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Workflow")
	static UComfyWorkflow* CreateTransientFromJson(const FString& Json, FString& OutError);

	/**
	 * Build the graph to submit. Values are keyed by input name; FileReferences hold the
	 * backend's reference for each File input after upload (a filename or a core/ASSET object).
	 */
	TSharedPtr<FJsonObject> BuildGraph(const TMap<FString, FString>& Values, const TMap<FString, TSharedPtr<FJsonValue>>& FileReferences, FString& OutError) const;

	/** class_type of a node in the graph, or empty. */
	FString GetNodeClass(const FString& NodeId) const;

	/** Manifest output name for a node id, or empty. */
	const FComfyWorkflowOutput* FindOutputForNode(const FString& NodeId) const;

	FString GetDisplayNameString() const;

private:
	TSharedPtr<FJsonObject> ParseGraph(FString& OutError) const;

};
