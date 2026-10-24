// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComfyTypes.generated.h"

/** Which kind of service a backend profile points at. */
UENUM(BlueprintType)
enum class EComfyBackendKind : uint8
{
	/** A ComfyUI on this machine or your network, such as Comfy Desktop. Uses the native /prompt API, or API v2 when it sits behind comfy-api-proxy. */
	Local,
	/** Comfy Cloud (cloud.comfy.org), Comfy API v2 with an API key. */
	Cloud,
	/** A Comfy API deployment ({deployment}.run.comfy.app), Comfy API v2 with an API key. */
	Deployment,
	/** Comfy Router (api.comfy.org): direct partner-model calls, no workflow graph. */
	Router,
};

/** Wire protocol a Local profile speaks. */
UENUM(BlueprintType)
enum class EComfyLocalProtocol : uint8
{
	/** Stock ComfyUI routes: /prompt, /history, /view, /upload/image, /ws. Works with Comfy Desktop out of the box. */
	Native,
	/** Comfy API v2, served by comfy-api-proxy in front of ComfyUI. */
	ApiV2,
};

/** A named connection to a Comfy service. */
USTRUCT(BlueprintType)
struct COMFYCORE_API FComfyBackendProfile
{
	GENERATED_BODY()

	/** Name shown in the editor and used by Blueprints, Python and agents to pick this backend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	EComfyBackendKind Kind = EComfyBackendKind::Local;

	/** Base URL, for example http://127.0.0.1:8188 or https://cloud.comfy.org. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString BaseUrl;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy", meta = (EditCondition = "Kind == EComfyBackendKind::Local", EditConditionHides))
	EComfyLocalProtocol LocalProtocol = EComfyLocalProtocol::Native;

	/** Environment variable that holds this backend's API key. Checked before the per-user key in Editor Preferences. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString ApiKeyEnvironmentVariable = TEXT("COMFY_API_KEY");

	/** Send the Comfy account key with local runs so partner (API) nodes work. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	bool bSendPartnerNodeKey = true;

	bool UsesApiV2() const
	{
		return Kind == EComfyBackendKind::Cloud || Kind == EComfyBackendKind::Deployment
			|| (Kind == EComfyBackendKind::Local && LocalProtocol == EComfyLocalProtocol::ApiV2);
	}
};

UENUM(BlueprintType)
enum class EComfyJobState : uint8
{
	Pending,
	Uploading,
	Queued,
	Running,
	Downloading,
	Succeeded,
	Failed,
	Canceled,
};

UENUM(BlueprintType)
enum class EComfyOutputKind : uint8
{
	Image,
	Audio,
	Video,
	Mesh,
	Text,
	File,
};

/** How an output is imported into the project. */
UENUM(BlueprintType)
enum class EComfyImportRule : uint8
{
	/** Pick from the file type: images become textures, audio becomes sound waves, glb/gltf/fbx/obj become static meshes. */
	Auto,
	/** sRGB color texture. */
	Texture,
	/** Linear normal map (TC_Normalmap). */
	NormalMap,
	/** Linear packed masks such as ORM (TC_Masks). */
	Masks,
	/** User interface texture, no mips. */
	UserInterface,
	Sound,
	StaticMesh,
	/** Keep the file on disk only. */
	None,
};

UENUM(BlueprintType)
enum class EComfyInputType : uint8
{
	String,
	Text,
	Int,
	Float,
	Bool,
	Seed,
	/** A file to upload: a path on disk. Images, audio or meshes. */
	File,
	/** A choice from a list (for example a checkpoint name). */
	Enum,
	/** A model file the loader node reads. Choices come from the connected backend; Prefer picks the default. */
	Model,
};

/** One workflow input that people, Blueprints and agents may set. */
USTRUCT(BlueprintType)
struct COMFYCORE_API FComfyWorkflowInput
{
	GENERATED_BODY()

	/** Stable name used by code: "prompt", "seed", "steps". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString Name;

	/** Label for the form. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString Label;

	/** Node id in the API-format graph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString NodeId;

	/** Input name on that node, for example "text" or "seed". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString InputName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	EComfyInputType Type = EComfyInputType::String;

	/** Value from the exported graph, as text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString DefaultValue;

	/** Allowed values for Enum inputs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	TArray<FString> Options;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString Tooltip;

	/** Model inputs: names to prefer among the backend's files, in order. Each is tried as an exact filename, then as a substring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	TArray<FString> Prefer;

	/** Model inputs: what kind of model fits, for people and agents (for example "flux-vae"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString Family;
};

/** One workflow output and how it lands in Unreal. */
USTRUCT(BlueprintType)
struct COMFYCORE_API FComfyWorkflowOutput
{
	GENERATED_BODY()

	/** Stable name used by code: "image", "albedo", "sound". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString Name;

	/** Node id of the save node in the API-format graph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString NodeId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	EComfyOutputKind Kind = EComfyOutputKind::Image;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	EComfyImportRule ImportRule = EComfyImportRule::Auto;
};

/** A file a job produced, downloaded to this machine. */
USTRUCT(BlueprintType)
struct COMFYCORE_API FComfyOutputFile
{
	GENERATED_BODY()

	/** Output name from the workflow manifest, or the node id when the manifest doesn't name it. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString NodeId;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString FileName;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	EComfyOutputKind Kind = EComfyOutputKind::File;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	EComfyImportRule ImportRule = EComfyImportRule::Auto;

	/** Absolute path of the downloaded file, under Saved/Comfy/Jobs. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString LocalPath;

	/** Remote reference used to download it (native: filename/subfolder/type; v2: asset id; Router: URL). */
	FString RemoteRef;
	FString RemoteSubfolder;
	FString RemoteType;
};

/** Progress snapshot for a running job. */
USTRUCT(BlueprintType)
struct COMFYCORE_API FComfyProgress
{
	GENERATED_BODY()

	/** 0 to 1, or -1 when unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	float Fraction = -1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	int32 QueuePosition = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString CurrentNode;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString Message;
};

/** Result of probing a backend. */
USTRUCT(BlueprintType)
struct COMFYCORE_API FComfyBackendStatus
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FName Backend;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	bool bReachable = false;

	/** Short description: "ComfyUI 0.41.0, RTX 4090, 22.1 GB free" or the error. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString Summary;

	/** Node classes the backend reports (native backends only; empty when unknown). */
	TSet<FString> NodeClasses;

	/** Choices for list inputs, keyed "NodeClass.input", from the backend's object_info. Empty when unknown. */
	TMap<FString, TArray<FString>> InputOptions;
};

namespace ComfyTypes
{
	COMFYCORE_API EComfyOutputKind KindFromFileName(const FString& FileName);
	COMFYCORE_API FString ContentTypeFromFileName(const FString& FileName);
	COMFYCORE_API const TCHAR* StateToString(EComfyJobState State);
	COMFYCORE_API bool IsTerminal(EComfyJobState State);
	/** The first option matching Prefer (exact name, then substring, per entry); else Fallback if it's an option or there are none; else the first option. */
	COMFYCORE_API FString PickModel(const TArray<FString>& Options, const TArray<FString>& Prefer, const FString& Fallback);
	/** Fill InputOptions from an object_info response: every input whose spec is a list of strings. */
	COMFYCORE_API void ReadInputOptions(const TSharedPtr<class FJsonObject>& ObjectInfo, TMap<FString, TArray<FString>>& OutOptions);
}
