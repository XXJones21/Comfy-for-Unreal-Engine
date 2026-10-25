// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ComfyTypes.h"
#include "ComfyRuntimeLibrary.generated.h"

class UComfyJob;
class UTexture;
class UTexture2D;
class USoundWaveProcedural;

/**
 * Helpers for using Comfy results while the game runs, without the editor: load an output
 * as a transient texture or sound, and the image steps the default texture workflows use.
 */
UCLASS()
class COMFYCORE_API UComfyRuntimeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Load an image file (png, jpg, exr, ...) as a transient texture. Pass bSRGB false for normal maps and masks. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Runtime")
	static UTexture2D* LoadTextureFromFile(const FString& FilePath, bool bSRGB = true);

	/** Load a job's image output as a transient texture. Empty OutputName takes the first image. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Runtime")
	static UTexture2D* LoadTextureFromJob(UComfyJob* Job, const FString& OutputName, bool bSRGB = true);

	/** Load a 16-bit PCM WAV file as a playable transient sound. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Runtime")
	static USoundWaveProcedural* LoadSoundFromFile(const FString& FilePath);

	/** Load a job's audio output (16-bit PCM WAV) as a playable transient sound. Empty OutputName takes the first audio file. */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Runtime")
	static USoundWaveProcedural* LoadSoundFromJob(UComfyJob* Job, const FString& OutputName);

	/**
	 * Make an image tile seamlessly: offset it by half, then blend the original back over the
	 * seams it exposes. BlendWidth is the fraction of the image used for the blend (0.05 to 0.5).
	 * Writes OutputPath (any format Unreal can save, png recommended).
	 */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Image")
	static bool MakeTileable(const FString& InputPath, const FString& OutputPath, float BlendWidth = 0.25f);

	/**
	 * Derive a tangent-space normal map from an image's luminance with a Sobel filter that
	 * wraps at the edges, so tileable input gives tileable output. Uses Unreal's convention (DirectX, green down).
	 */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Image")
	static bool MakeNormalMap(const FString& InputPath, const FString& OutputPath, float Strength = 6.f);

	/**
	 * Write a texture to a PNG in Saved/Comfy/Inputs, for a workflow's File input, and return its path.
	 * Works for render targets and for textures from Load Texture From File or Job anywhere, and for
	 * texture assets in the editor. A packaged game can't read other textures' pixels back.
	 */
	UFUNCTION(BlueprintCallable, Category = "Comfy|Image")
	static FString SaveTextureForInput(UTexture* Texture, FString& OutError);

	/** A random seed in the range ComfyUI accepts. */
	UFUNCTION(BlueprintPure, Category = "Comfy", meta = (DisplayName = "New Comfy Seed"))
	static int64 NewSeed();

	/** Find an output file by name or kind. */
	static const FComfyOutputFile* FindOutput(const UComfyJob* Job, const FString& OutputName, EComfyOutputKind Kind);
};
