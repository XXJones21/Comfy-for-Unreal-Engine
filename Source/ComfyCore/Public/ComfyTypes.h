// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComfyTypes.generated.h"

class FJsonObject;
class UComfyWorkflow;

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

	/**
	 * When the URL above doesn't answer, look for ComfyUI on this computer: ports 8000 (Comfy Desktop)
	 * and 8188 (ComfyUI's default), and the ports set on Comfy Desktop's installations.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy", meta = (EditCondition = "Kind == EComfyBackendKind::Local", EditConditionHides))
	bool bFindLocalComfy = true;

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

/** Where to get a Model input's file when the backend doesn't have it. */
USTRUCT(BlueprintType)
struct COMFYCORE_API FComfyModelDownload
{
	GENERATED_BODY()

	/** File name, for example "flux2-vae.safetensors". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString File;

	/** Folder under ComfyUI/models, for example "vae". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString Folder;

	/** Download size as text, for example "321 MB". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString Size;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString Url;
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

	/** Model inputs: the file to get when the backend has none of Prefer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FComfyModelDownload Download;

	/** Where Unreal fills it from: Constant (the form), Prompt, SelectedTextures, SelectedMeshes or ViewportCapture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString Source;
};

/** A named set of input values from the manifest, such as "Brick" for a texture prompt. */
USTRUCT(BlueprintType)
struct COMFYCORE_API FComfyWorkflowPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString Name;

	/** Input name to value, as text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	TMap<FString, FString> Values;
};

/**
 * Values for a workflow's inputs, keyed by the names in its manifest. Pick a workflow and the Details panel lists
 * its inputs, as the Run view does; or build it with the Set nodes in Comfy|Inputs. Pass it to Run Comfy Workflow.
 * Names are checked against the workflow when the run starts, so a misspelled name fails the run instead of being
 * ignored. Inputs left out keep the workflow's values, except seeds, which get a new seed on every run.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Comfy Inputs"))
struct COMFYCORE_API FComfyInputs
{
	GENERATED_BODY()

	/** The workflow these inputs are for. Run Comfy Workflow runs it when its own Workflow pin is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	TObjectPtr<UComfyWorkflow> Workflow;

	/** Input name to value, as text. A Seed of -1 gets a new seed on every run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	TMap<FString, FString> Values;

	/** Assets for File inputs, such as a texture or render target. Each is written to a file when the run starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	TMap<FString, TObjectPtr<UObject>> Assets;
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

	/** For textures: the material slot it fills (BaseColor, Normal, Roughness, Metallic, AO, Height, ORM, Emissive, Opacity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfy")
	FString Slot;
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

/** What Comfy Desktop has on this computer, read from its installations list. */
UENUM(BlueprintType)
enum class EComfyDesktopInstall : uint8
{
	/** Not looked for: the backend isn't a local one. */
	Unknown,
	/** Comfy Desktop isn't installed. */
	NotInstalled,
	/** Comfy Desktop is installed but has no local ComfyUI, only Comfy Cloud. */
	NoLocal,
	/** Comfy Desktop has at least one local ComfyUI (or a remote connection). */
	HasLocal,
};

/** Where the API key a backend uses comes from. */
UENUM(BlueprintType)
enum class EComfyApiKeySource : uint8
{
	None,
	/** An environment variable: only changing or removing it, then restarting the editor, replaces it. */
	Environment,
	/** Set for this editor session (SetSessionApiKey). */
	Session,
	/** Editor Preferences > Plugins > Comfy. */
	EditorPreferences,
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

	/** The address that answered, which can differ from the profile's when a local ComfyUI was found on another port. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString Url;

	/** Local backends only: what Comfy Desktop has installed, for explaining why nothing answered. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	EComfyDesktopInstall DesktopInstall = EComfyDesktopInstall::Unknown;

	/** Comfy Cloud only: true when neither an API key nor a signed-in Comfy Studio page is available. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	bool bNeedsSignIn = false;

	/** The backend refused the API key (401/403). Comfy Cloud then runs through the sign-in, if there is one. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	bool bApiKeyRefused = false;

	/** Where the API key comes from, and its name ("COMFY_API_KEY") when that's an environment variable. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	EComfyApiKeySource KeySource = EComfyApiKeySource::None;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString KeySourceName;

	/** The backend's own words when Summary puts them plainly, for tooltips. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString Detail;

	/** Node classes the backend reports (native backends only; empty when unknown). */
	TSet<FString> NodeClasses;

	/** Choices for list inputs, keyed "NodeClass.input", from the backend's object_info. Empty when unknown. */
	TMap<FString, TArray<FString>> InputOptions;
};

/**
 * The Comfy account signed in to Comfy Studio's Comfy Cloud page. Read from the page itself, so it
 * shows what Comfy Cloud shows; the sign-in token never leaves the page.
 */
USTRUCT(BlueprintType)
struct COMFYCORE_API FComfyCloudAccount
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	bool bSignedIn = false;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString Email;

	/** The active workspace when it's a team one; empty for a personal workspace. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FString Workspace;

	/** Free runs left this period, or -1 when the account has no free-run allowance. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	int32 FreeRunsRemaining = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	int32 FreeRunsAllowance = -1;

	/** Comfy credits available, or -1 when unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	double Credits = -1.0;

	UPROPERTY(BlueprintReadOnly, Category = "Comfy")
	FDateTime UpdatedAt;

	/** "Signed in as Joshua · 5 of 5 free runs left · 1,200 credits", or "Not signed in". */
	FString Describe() const;

	bool operator==(const FComfyCloudAccount& Other) const;
	bool operator!=(const FComfyCloudAccount& Other) const { return !(*this == Other); }
};

namespace ComfyTypes
{
	COMFYCORE_API EComfyOutputKind KindFromFileName(const FString& FileName);
	COMFYCORE_API FString ContentTypeFromFileName(const FString& FileName);
	COMFYCORE_API const TCHAR* StateToString(EComfyJobState State);
	COMFYCORE_API bool IsTerminal(EComfyJobState State);
	/** A workflow type from docs/MANIFEST.md, with older plural names (Textures, Audio, Meshes) mapped onto it. */
	COMFYCORE_API FString NormalizeCategory(const FString& Category);
	/** The first option matching Prefer (exact name, then substring, per entry), or empty when none does. */
	COMFYCORE_API FString MatchModel(const TArray<FString>& Options, const TArray<FString>& Prefer);
	/** The first option matching Prefer (exact name, then substring, per entry); else Fallback if it's an option or there are none; else the first option. */
	COMFYCORE_API FString PickModel(const TArray<FString>& Options, const TArray<FString>& Prefer, const FString& Fallback);
	/** Fill InputOptions from an object_info response: every input whose spec is a list of strings. */
	COMFYCORE_API void ReadInputOptions(const TSharedPtr<FJsonObject>& ObjectInfo, TMap<FString, TArray<FString>>& OutOptions);
}
