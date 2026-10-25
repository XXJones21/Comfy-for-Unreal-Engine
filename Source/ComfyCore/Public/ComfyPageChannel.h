// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * A ComfyUI frontend page that is signed in to Comfy Cloud and runs calls for the plugin. In the editor
 * this is Comfy Studio's Graph view. Calls run inside the page with the page's own sign-in, so Unreal
 * never holds the account's token. Comfy Cloud jobs use it when no API key is set.
 */
class IComfyPageChannel
{
public:
	virtual ~IComfyPageChannel() = default;

	using FReply = TFunction<void(bool bOk, const FString& ResultJson, const FString& Error)>;

	/** True once the page has loaded, the bridge script is running, and an account is signed in. */
	virtual bool IsReady() const = 0;

	/**
	 * Run one bridge method in the page (see Resources/Bridge/comfy_unreal_page.js). ArgsJson is a JSON
	 * object. Reply runs on the game thread, with the method's JSON result or an error.
	 */
	virtual void Call(const FString& Method, const FString& ArgsJson, FReply Reply, float TimeoutSeconds = 60.f) = 0;
};
