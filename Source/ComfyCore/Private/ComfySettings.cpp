// Copyright Comfy Org. All Rights Reserved.

#include "ComfySettings.h"

UComfySettings::UComfySettings()
{
	FComfyBackendProfile Desktop;
	Desktop.Name = TEXT("Desktop");
	Desktop.Kind = EComfyBackendKind::Local;
	Desktop.BaseUrl = TEXT("http://127.0.0.1:8188");
	Desktop.LocalProtocol = EComfyLocalProtocol::Native;
	Backends.Add(Desktop);

	FComfyBackendProfile Cloud;
	Cloud.Name = TEXT("Comfy Cloud");
	Cloud.Kind = EComfyBackendKind::Cloud;
	Cloud.BaseUrl = TEXT("https://cloud.comfy.org");
	Backends.Add(Cloud);

	FComfyBackendProfile Router;
	Router.Name = TEXT("Comfy Router");
	Router.Kind = EComfyBackendKind::Router;
	Router.BaseUrl = TEXT("https://api.comfy.org");
	Backends.Add(Router);

	DefaultBackend = Desktop.Name;
}

const FComfyBackendProfile* UComfySettings::FindBackend(FName Name) const
{
	return Backends.FindByPredicate([Name](const FComfyBackendProfile& Profile) { return Profile.Name == Name; });
}
