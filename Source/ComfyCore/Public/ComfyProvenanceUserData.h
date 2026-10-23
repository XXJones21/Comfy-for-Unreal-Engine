// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "ComfyProvenanceUserData.generated.h"

/**
 * Attached to every asset the plugin imports, so you can tell which workflow, inputs and
 * backend made it and run it again. Visible under Asset User Data in the asset's details.
 */
UCLASS(BlueprintType, EditInlineNew)
class COMFYCORE_API UComfyProvenanceUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	/** Workflow asset path, or the Router model id. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Comfy")
	FString Source;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Comfy")
	FString GraphHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Comfy")
	FName Backend;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Comfy")
	FString JobId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Comfy")
	FString RemoteId;

	/** Output name within the job. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Comfy")
	FString OutputName;

	/** The inputs the job ran with, seed included. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Comfy")
	TMap<FString, FString> Inputs;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Comfy")
	FDateTime CreatedAt;
};
