// Copyright Comfy Org. All Rights Reserved.

#include "ComfyLocalDetect.h"
#include "Backends/ComfyHttp.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace ComfyLocalDetect
{
	/** Comfy Desktop's app data folder, where it keeps installations.json. */
	static FString DesktopDataDir()
	{
#if PLATFORM_WINDOWS
		const FString AppData = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
		return AppData.IsEmpty() ? FString() : AppData / TEXT("Comfy Desktop");
#elif PLATFORM_MAC
		const FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
		return Home.IsEmpty() ? FString() : Home / TEXT("Library/Application Support/Comfy Desktop");
#else
		FString Config = FPlatformMisc::GetEnvironmentVariable(TEXT("XDG_CONFIG_HOME"));
		if (Config.IsEmpty())
		{
			const FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
			Config = Home.IsEmpty() ? FString() : Home / TEXT(".config");
		}
		return Config.IsEmpty() ? FString() : Config / TEXT("Comfy Desktop");
#endif
	}

	/** The value of --port in a launch-arguments string, or 0. */
	static int32 ParsePort(const FString& LaunchArgs)
	{
		TArray<FString> Parts;
		LaunchArgs.ParseIntoArrayWS(Parts);
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			FString Value;
			if (Parts[Index] == TEXT("--port") && Parts.IsValidIndex(Index + 1))
			{
				Value = Parts[Index + 1];
			}
			else if (Parts[Index].StartsWith(TEXT("--port=")))
			{
				Value = Parts[Index].Mid(7);
			}
			if (Value.IsNumeric())
			{
				return FCString::Atoi(*Value);
			}
		}
		return 0;
	}

	FDesktopInfo ReadDesktop()
	{
		FDesktopInfo Info;
		const FString Dir = DesktopDataDir();
		if (Dir.IsEmpty() || !FPaths::DirectoryExists(Dir))
		{
			return Info;
		}
		Info.Install = EComfyDesktopInstall::NoLocal;

		FString Text;
		TArray<TSharedPtr<FJsonValue>> Installations;
		if (!FFileHelper::LoadFileToString(Text, *(Dir / TEXT("installations.json")))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Installations))
		{
			return Info;
		}
		for (const TSharedPtr<FJsonValue>& Value : Installations)
		{
			const TSharedPtr<FJsonObject> Installation = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Installation.IsValid())
			{
				continue;
			}
			FString SourceId, RemoteUrl, LaunchArgs;
			Installation->TryGetStringField(TEXT("sourceId"), SourceId);
			Installation->TryGetStringField(TEXT("remoteUrl"), RemoteUrl);
			Installation->TryGetStringField(TEXT("launchArgs"), LaunchArgs);
			if (SourceId == TEXT("cloud") || RemoteUrl.Contains(TEXT("comfy.org")))
			{
				continue;
			}
			Info.Install = EComfyDesktopInstall::HasLocal;
			if (!RemoteUrl.IsEmpty())
			{
				RemoteUrl.RemoveFromEnd(TEXT("/"));
				Info.RemoteUrls.AddUnique(RemoteUrl);
			}
			else if (const int32 Port = ParsePort(LaunchArgs))
			{
				Info.Ports.AddUnique(Port);
			}
		}
		return Info;
	}

	TArray<FString> Candidates(const FString& Preferred, const FString& Configured, const FDesktopInfo& Desktop)
	{
		TArray<FString> Urls;
		auto Add = [&Urls](FString Url)
		{
			Url.RemoveFromEnd(TEXT("/"));
			if (!Url.IsEmpty())
			{
				Urls.AddUnique(Url);
			}
		};
		Add(Preferred);
		Add(Configured);
		for (const int32 Port : Desktop.Ports)
		{
			Add(FString::Printf(TEXT("http://127.0.0.1:%d"), Port));
		}
		Add(TEXT("http://127.0.0.1:8000"));
		Add(TEXT("http://127.0.0.1:8188"));
		for (const FString& Remote : Desktop.RemoteUrls)
		{
			Add(Remote);
		}
		return Urls;
	}

	void FindFirst(TArray<FString> Urls, TFunction<void(const FString& Found)> Done)
	{
		if (Urls.Num() == 0)
		{
			Done(FString());
			return;
		}
		const FString Url = Urls[0];
		Urls.RemoveAt(0);
		// A refused local connection fails at once; the timeout only matters for remote addresses.
		ComfyHttp::Send(TEXT("GET"), Url + TEXT("/system_stats"), {}, [Url, Urls = MoveTemp(Urls), Done = MoveTemp(Done)](const ComfyHttp::FResponse& Response) mutable
		{
			if (Response.IsOk() && Response.Json.IsValid() && Response.Json->HasField(TEXT("system")))
			{
				Done(Url);
				return;
			}
			FindFirst(MoveTemp(Urls), MoveTemp(Done));
		}, 4.f);
	}

	FString HostPort(const FString& Url)
	{
		FString Result = Url;
		Result.RemoveFromStart(TEXT("http://"));
		Result.RemoveFromStart(TEXT("https://"));
		Result.RemoveFromEnd(TEXT("/"));
		return Result;
	}

	FString DescribeNotFound(const FDesktopInfo& Desktop, const TArray<FString>& Tried)
	{
		const FString TriedList = FString::JoinBy(Tried, TEXT(", "), [](const FString& Url) { return HostPort(Url); });
		switch (Desktop.Install)
		{
		case EComfyDesktopInstall::NoLocal:
			return FString::Printf(TEXT("Comfy Desktop is installed but has no local ComfyUI yet (tried %s)."), *TriedList);
		case EComfyDesktopInstall::HasLocal:
			return FString::Printf(TEXT("Comfy Desktop has a local ComfyUI, but it isn't running (tried %s)."), *TriedList);
		default:
			return FString::Printf(TEXT("No ComfyUI is running on this computer (tried %s)."), *TriedList);
		}
	}
}
