// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

class FJsonObject;
class FJsonValue;

/** Small helpers over FHttpModule. Every callback runs on the game thread. */
namespace ComfyHttp
{
	struct FResponse
	{
		bool bConnected = false;
		int32 Code = 0;
		FString Body;
		TArray<uint8> Bytes;
		TSharedPtr<FJsonObject> Json;
		FString ContentType;
		FHttpResponsePtr Raw;

		bool IsOk() const { return bConnected && Code >= 200 && Code < 300; }
		/** Retry-After in seconds, or Default. */
		float RetryAfter(float Default) const;
		/** A readable error: the API's error message when it sent one, otherwise the HTTP status. */
		FString Describe(const FString& What) const;
	};

	using FCallback = TFunction<void(const FResponse&)>;
	using FHeaders = TMap<FString, FString>;

	struct FMultipartField
	{
		FString Name;
		FString Value;
	};

	void Send(const FString& Verb, const FString& Url, const FHeaders& Headers, FCallback Callback, float TimeoutSeconds = 60.f);
	void SendJson(const FString& Verb, const FString& Url, const FHeaders& Headers, const TSharedRef<FJsonObject>& Body, FCallback Callback, float TimeoutSeconds = 60.f);
	void SendMultipart(const FString& Url, const FHeaders& Headers, const TArray<FMultipartField>& Fields, const FString& FileField, const FString& FileName, const FString& FileContentType, const TArray<uint8>& FileBytes, FCallback Callback, float TimeoutSeconds = 300.f);
	/** Download to a file. Creates the folder. */
	void Download(const FString& Url, const FHeaders& Headers, const FString& LocalPath, TFunction<void(bool bOk, const FString& Error)> Callback);

	/** Run a function after a delay, on the game thread. */
	void Delay(float Seconds, TFunction<void()> Function);

	/** Resolve a link from an API response: absolute URLs pass through; host-relative links (leading /) join the origin of BaseUrl. */
	FString ResolveLink(const FString& BaseUrl, const FString& Link);
	FString Origin(const FString& Url);
	FString UrlEncode(const FString& Value);
	FString NewIdempotencyKey();
	FString JsonToString(const TSharedRef<FJsonObject>& Object);
}
