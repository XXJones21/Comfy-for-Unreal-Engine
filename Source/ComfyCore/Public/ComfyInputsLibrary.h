// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ComfyTypes.h"
#include "ComfyInputsLibrary.generated.h"

class UComfyWorkflow;
class UTexture;

/**
 * Build Comfy Inputs for a run, one typed value at a time. Each Set node returns the inputs with
 * that value added, so they chain without exec wires. Python can fill the struct's maps directly.
 */
UCLASS()
class COMFYCORE_API UComfyInputsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Set a text input: a prompt, a string, a choice or a model file name. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Inputs", meta = (DisplayName = "Set Text (Comfy Inputs)", AutoCreateRefTerm = "Inputs", Keywords = "prompt string choice model"))
	static FComfyInputs SetComfyText(const FComfyInputs& Inputs, const FString& Name, const FString& Value);

	UFUNCTION(BlueprintPure, Category = "Comfy|Inputs", meta = (DisplayName = "Set Int (Comfy Inputs)", AutoCreateRefTerm = "Inputs", Keywords = "steps width height integer"))
	static FComfyInputs SetComfyInt(const FComfyInputs& Inputs, const FString& Name, int64 Value);

	UFUNCTION(BlueprintPure, Category = "Comfy|Inputs", meta = (DisplayName = "Set Float (Comfy Inputs)", AutoCreateRefTerm = "Inputs", Keywords = "cfg denoise strength number"))
	static FComfyInputs SetComfyFloat(const FComfyInputs& Inputs, const FString& Name, double Value);

	UFUNCTION(BlueprintPure, Category = "Comfy|Inputs", meta = (DisplayName = "Set Bool (Comfy Inputs)", AutoCreateRefTerm = "Inputs"))
	static FComfyInputs SetComfyBool(const FComfyInputs& Inputs, const FString& Name, bool Value);

	/** Set a seed. -1 gets a new seed on every run. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Inputs", meta = (DisplayName = "Set Seed (Comfy Inputs)", AutoCreateRefTerm = "Inputs"))
	static FComfyInputs SetComfySeed(const FComfyInputs& Inputs, const FString& Name, int64 Seed = -1);

	/** Set a File input to a file on disk (an image, sound or mesh). */
	UFUNCTION(BlueprintPure, Category = "Comfy|Inputs", meta = (DisplayName = "Set File (Comfy Inputs)", AutoCreateRefTerm = "Inputs", Keywords = "image path"))
	static FComfyInputs SetComfyFile(const FComfyInputs& Inputs, const FString& Name, const FString& FilePath);

	/**
	 * Set a File input to a texture. It's written to Saved/Comfy/Inputs as a PNG when the run starts.
	 * Works for render targets and textures from Load Texture From File or Job anywhere, and for any
	 * texture asset in the editor.
	 */
	UFUNCTION(BlueprintPure, Category = "Comfy|Inputs", meta = (DisplayName = "Set Texture (Comfy Inputs)", AutoCreateRefTerm = "Inputs", Keywords = "image render target"))
	static FComfyInputs SetComfyTexture(const FComfyInputs& Inputs, const FString& Name, UTexture* Texture);

	/** Set a File input to an asset. In the editor, meshes and sounds send the file they were imported from. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Inputs", meta = (DisplayName = "Set Asset (Comfy Inputs)", AutoCreateRefTerm = "Inputs", Keywords = "mesh sound"))
	static FComfyInputs SetComfyAsset(const FComfyInputs& Inputs, const FString& Name, UObject* Asset);

	/** The value set for an input: text, or the asset's path for a File input. False when it isn't set, so the workflow's value applies. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Inputs", meta = (DisplayName = "Get Comfy Input"))
	static bool GetComfyInput(const FComfyInputs& Inputs, const FString& Name, FString& Value);

	/** Remove an input's value, so the workflow's own value applies (a seed gets a new one each run). */
	UFUNCTION(BlueprintPure, Category = "Comfy|Inputs", meta = (DisplayName = "Clear Comfy Input", AutoCreateRefTerm = "Inputs"))
	static FComfyInputs ClearComfyInput(const FComfyInputs& Inputs, const FString& Name);

	/**
	 * The choices for a Choice or Model input. For a model, a local ComfyUI that Studio has checked lists its installed
	 * files; other backends list the workflow's preferred files. Model choices start with an empty entry, which lets the
	 * backend pick. Empty for other inputs.
	 */
	UFUNCTION(BlueprintPure, Category = "Comfy|Inputs")
	static TArray<FString> GetComfyInputChoices(const UComfyWorkflow* Workflow, const FString& Name);

	/** True for a File input that takes an image, so it can take a texture. */
	static bool InputTakesImage(const UComfyWorkflow* Workflow, const FComfyWorkflowInput& Input);

	/** Comfy Inputs from a name-to-text map, the form Run Comfy Workflow (String Map) takes. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Inputs", meta = (DisplayName = "To Comfy Inputs (Map)", CompactNodeTitle = "->", BlueprintAutocast))
	static FComfyInputs MakeComfyInputsFromMap(const TMap<FString, FString>& Values);

	/** The workflow's input names, for building inputs by name. */
	UFUNCTION(BlueprintPure, Category = "Comfy|Inputs")
	static TArray<FString> GetComfyInputNames(const UComfyWorkflow* Workflow);

	/**
	 * Check inputs against a workflow without running it: every name must be one of its inputs and every
	 * value must fit the input's type (a whole number for Int, one of the choices for a list). Assets aren't exported.
	 * With no Workflow, checks against the workflow the inputs name.
	 */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Inputs")
	static bool ValidateComfyInputs(const UComfyWorkflow* Workflow, const FComfyInputs& Inputs, FString& OutError);

	/**
	 * Check the inputs and turn them into the name-to-text map a run takes: seeds of -1 become new seeds
	 * and assets become files. False with OutError when a name or value doesn't fit the workflow.
	 */
	static bool ResolveComfyInputs(const UComfyWorkflow* Workflow, const FComfyInputs& Inputs, TMap<FString, FString>& OutValues, FString& OutError, bool bExportAssets = true);

	/**
	 * Writes an editor asset (a texture with source art, a mesh or sound with its import file) to a file for a File input.
	 * Set by the editor module; empty in a packaged game. Returns the path, or empty with the error.
	 */
	static TFunction<FString(UObject* Asset, FString& OutError)> EditorAssetExporter;
};
