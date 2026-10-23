// Copyright Comfy Org. All Rights Reserved.

#include "ComfyTypes.h"
#include "Misc/Paths.h"

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
}
