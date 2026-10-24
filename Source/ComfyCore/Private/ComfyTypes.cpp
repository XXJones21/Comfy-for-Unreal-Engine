// Copyright Comfy Org. All Rights Reserved.

#include "ComfyTypes.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace ComfyTypes
{
	EComfyOutputKind KindFromFileName(const FString& FileName)
	{
		const FString Ext = FPaths::GetExtension(FileName).ToLower();
		if (Ext == TEXT("png") || Ext == TEXT("jpg") || Ext == TEXT("jpeg") || Ext == TEXT("webp") || Ext == TEXT("exr") || Ext == TEXT("tga") || Ext == TEXT("bmp"))
		{
			return EComfyOutputKind::Image;
		}
		if (Ext == TEXT("wav") || Ext == TEXT("flac") || Ext == TEXT("mp3") || Ext == TEXT("ogg") || Ext == TEXT("opus"))
		{
			return EComfyOutputKind::Audio;
		}
		if (Ext == TEXT("glb") || Ext == TEXT("gltf") || Ext == TEXT("fbx") || Ext == TEXT("obj") || Ext == TEXT("usd") || Ext == TEXT("usdz"))
		{
			return EComfyOutputKind::Mesh;
		}
		if (Ext == TEXT("mp4") || Ext == TEXT("webm") || Ext == TEXT("mov") || Ext == TEXT("gif"))
		{
			return EComfyOutputKind::Video;
		}
		if (Ext == TEXT("txt") || Ext == TEXT("json"))
		{
			return EComfyOutputKind::Text;
		}
		return EComfyOutputKind::File;
	}

	FString ContentTypeFromFileName(const FString& FileName)
	{
		const FString Ext = FPaths::GetExtension(FileName).ToLower();
		if (Ext == TEXT("png")) return TEXT("image/png");
		if (Ext == TEXT("jpg") || Ext == TEXT("jpeg")) return TEXT("image/jpeg");
		if (Ext == TEXT("webp")) return TEXT("image/webp");
		if (Ext == TEXT("exr")) return TEXT("image/x-exr");
		if (Ext == TEXT("wav")) return TEXT("audio/wav");
		if (Ext == TEXT("flac")) return TEXT("audio/flac");
		if (Ext == TEXT("mp3")) return TEXT("audio/mpeg");
		if (Ext == TEXT("ogg")) return TEXT("audio/ogg");
		if (Ext == TEXT("glb")) return TEXT("model/gltf-binary");
		if (Ext == TEXT("gltf")) return TEXT("model/gltf+json");
		if (Ext == TEXT("mp4")) return TEXT("video/mp4");
		return TEXT("application/octet-stream");
	}

	const TCHAR* StateToString(EComfyJobState State)
	{
		switch (State)
		{
		case EComfyJobState::Pending: return TEXT("Pending");
		case EComfyJobState::Uploading: return TEXT("Uploading inputs");
		case EComfyJobState::Queued: return TEXT("Queued");
		case EComfyJobState::Running: return TEXT("Running");
		case EComfyJobState::Downloading: return TEXT("Downloading");
		case EComfyJobState::Succeeded: return TEXT("Succeeded");
		case EComfyJobState::Failed: return TEXT("Failed");
		case EComfyJobState::Canceled: return TEXT("Canceled");
		}
		return TEXT("Unknown");
	}

	bool IsTerminal(EComfyJobState State)
	{
		return State == EComfyJobState::Succeeded || State == EComfyJobState::Failed || State == EComfyJobState::Canceled;
	}

	FString NormalizeCategory(const FString& Category)
	{
		if (Category.Equals(TEXT("Textures"), ESearchCase::IgnoreCase)) return TEXT("Texture");
		if (Category.Equals(TEXT("Audio"), ESearchCase::IgnoreCase)) return TEXT("Sound");
		if (Category.Equals(TEXT("Meshes"), ESearchCase::IgnoreCase)) return TEXT("Mesh");
		if (Category.Equals(TEXT("Materials"), ESearchCase::IgnoreCase)) return TEXT("Material");
		return Category;
	}

	FString PickModel(const TArray<FString>& Options, const TArray<FString>& Prefer, const FString& Fallback)
	{
		for (const FString& Wanted : Prefer)
		{
			for (const FString& Option : Options)
			{
				if (Option.Equals(Wanted, ESearchCase::IgnoreCase) || FPaths::GetCleanFilename(Option).Equals(Wanted, ESearchCase::IgnoreCase))
				{
					return Option;
				}
			}
			for (const FString& Option : Options)
			{
				if (Option.Contains(Wanted, ESearchCase::IgnoreCase))
				{
					return Option;
				}
			}
		}
		if (Options.Num() == 0 || Options.Contains(Fallback))
		{
			return Fallback;
		}
		return Options[0];
	}

	void ReadInputOptions(const TSharedPtr<FJsonObject>& ObjectInfo, TMap<FString, TArray<FString>>& OutOptions)
	{
		if (!ObjectInfo.IsValid())
		{
			return;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Node : ObjectInfo->Values)
		{
			const TSharedPtr<FJsonObject> NodeObject = Node.Value.IsValid() ? Node.Value->AsObject() : nullptr;
			const TSharedPtr<FJsonObject>* Input = nullptr;
			if (!NodeObject.IsValid() || !NodeObject->TryGetObjectField(TEXT("input"), Input) || !Input)
			{
				continue;
			}
			for (const TCHAR* Group : { TEXT("required"), TEXT("optional") })
			{
				const TSharedPtr<FJsonObject>* Fields = nullptr;
				if (!(*Input)->TryGetObjectField(Group, Fields) || !Fields)
				{
					continue;
				}
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*Fields)->Values)
				{
					// A list input is [[choice, ...], {options}]; other inputs start with a type name.
					const TArray<TSharedPtr<FJsonValue>>* Spec = nullptr;
					const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
					if (!Field.Value.IsValid() || !Field.Value->TryGetArray(Spec) || Spec->Num() == 0 || !(*Spec)[0]->TryGetArray(Choices))
					{
						continue;
					}
					TArray<FString> Names;
					for (const TSharedPtr<FJsonValue>& Choice : *Choices)
					{
						FString Name;
						if (Choice.IsValid() && Choice->TryGetString(Name))
						{
							Names.Add(Name);
						}
					}
					if (Names.Num() > 0)
					{
						OutOptions.Add(FString(Node.Key) + TEXT(".") + FString(Field.Key), MoveTemp(Names));
					}
				}
			}
		}
	}
}
