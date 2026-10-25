// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComfyTypes.h"

/** Finding a ComfyUI on this computer: Comfy Desktop, a portable install, or a manual one. */
namespace ComfyLocalDetect
{
	struct FDesktopInfo
	{
		EComfyDesktopInstall Install = EComfyDesktopInstall::NotInstalled;
		/** Ports set with --port on Comfy Desktop's local installations. */
		TArray<int32> Ports;
		/** Remote Connection installations that point at another ComfyUI. */
		TArray<FString> RemoteUrls;
	};

	/** Read Comfy Desktop's installations list from its app data folder. */
	FDesktopInfo ReadDesktop();

	/** Addresses to try, best first: Preferred (the last one that answered), the profile's URL, then local ports. */
	TArray<FString> Candidates(const FString& Preferred, const FString& Configured, const FDesktopInfo& Desktop);

	/** Try each URL's /system_stats in order and report the first that answers, or an empty string. */
	void FindFirst(TArray<FString> Urls, TFunction<void(const FString& Found)> Done);

	/** "127.0.0.1:8000" for display. */
	FString HostPort(const FString& Url);

	/** Why nothing answered, in words that fit what's installed. */
	FString DescribeNotFound(const FDesktopInfo& Desktop, const TArray<FString>& Tried);
}
