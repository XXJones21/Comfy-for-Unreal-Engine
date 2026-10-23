// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ComfyTypes.h"
#include "ComfySettings.generated.h"

/**
 * Project-wide Comfy settings, shared through source control (Config/DefaultGame.ini).
 * Project Settings > Plugins > Comfy.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Comfy"))
class COMFYCORE_API UComfySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UComfySettings();

	/** Services this project can run workflows on. Keys never go here; see Editor Preferences > Plugins > Comfy. */
	UPROPERTY(config, EditAnywhere, Category = "Backends")
	TArray<FComfyBackendProfile> Backends;

	/** Backend used when a run doesn't name one. Each user can override it in Editor Preferences. */
	UPROPERTY(config, EditAnywhere, Category = "Backends")
	FName DefaultBackend;

	/** Content folder that generated assets are imported into. */
	UPROPERTY(config, EditAnywhere, Category = "Import", meta = (ContentDir))
	FString DefaultImportFolder = TEXT("/Game/Comfy/Generated");

	/** Seconds between status checks while a job runs. */
	UPROPERTY(config, EditAnywhere, Category = "Advanced", meta = (ClampMin = "0.25", ClampMax = "10.0"))
	float PollIntervalSeconds = 1.0f;

	/** Give up on a job after this many minutes. */
	UPROPERTY(config, EditAnywhere, Category = "Advanced", meta = (ClampMin = "1", ClampMax = "240"))
	int32 JobTimeoutMinutes = 30;

	/** Agents must get a person's confirmation before running workflows on backends that spend credits (Cloud, deployments, Router, or local partner nodes). */
	UPROPERTY(config, EditAnywhere, Category = "Agents")
	bool bAgentsMustConfirmCreditSpend = true;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	const FComfyBackendProfile* FindBackend(FName Name) const;
};

/**
 * Per-user Comfy settings, never committed (Saved/Config/.../EditorPerProjectUserSettings.ini).
 * Editor Preferences > Plugins > Comfy.
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Comfy"))
class COMFYCORE_API UComfyUserSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Your backend for this project. Empty uses the project default. */
	UPROPERTY(config, EditAnywhere, Category = "Comfy")
	FName ActiveBackend;

	/** Your Comfy API key (comfyui-...), used for Cloud, deployments, Router and partner nodes. An environment variable named on the backend wins over this. */
	UPROPERTY(config, EditAnywhere, Category = "Comfy", meta = (PasswordField = true))
	FString ApiKey;

	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
