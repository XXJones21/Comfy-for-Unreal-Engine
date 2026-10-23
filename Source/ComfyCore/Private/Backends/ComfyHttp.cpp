// Copyright Comfy Org. All Rights Reserved.

#include "ComfyHttp.h"
#include "ComfyCoreModule.h"
#include "HttpModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Containers/Ticker.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformHttp.h"

namespace ComfyHttp
{
	float FResponse::RetryAfter(float Default) const
	{
		if (Raw.IsValid())
		{
			const FString Header = Raw->GetHeader(TEXT("Retry-After"));
			if (!Header.IsEmpty() && Header.IsNumeric())
			{
				return FMath::Clamp(FCString::Atof(*Header), 0.25f, 60.f);
			}
		}
		return Default;
	}

	FString FResponse::Describe(const FString& What) const
	{
		if (!bConnected)
		{
			return FString::Printf(TEXT("%s: couldn't connect. Check that the server is running and the URL in Project Settings > Plugins > Comfy is right."), *What);
		}
		FString Message;
		if (Json.IsValid())
		{
			const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
			FString ErrorString;
			if (Json->TryGetObjectField(TEXT("error"), ErrorObject) && ErrorObject)
			{
				FString Code;
				(*ErrorObject)->TryGetStringField(TEXT("code"), Code);
				(*ErrorObject)->TryGetStringField(TEXT("message"), Message);
				if (Message.IsEmpty())
				{
					(*ErrorObject)->TryGetStringField(TEXT("type"), Code);
				}
				if (!Code.IsEmpty())
				{
					Message = FString::Printf(TEXT("%s (%s)"), *Message, *Code);
				}
			}
			else if (Json->TryGetStringField(TEXT("error"), ErrorString))
			{
				Message = ErrorString;
			}
			else if (Json->TryGetStringField(TEXT("detail"), ErrorString))
			{
				Message = ErrorString;
			}
			FString ErrorType;
			if (Json->TryGetStringField(TEXT("error_type"), ErrorType))
			{
				Message = FString::Printf(TEXT("%s (%s)"), *Message, *ErrorType);
			}
		}
		if (Message.IsEmpty())
		{
			Message = Body.Left(300);
		}
		if (Code == 401)
		{
			return FString::Printf(TEXT("%s: the API key was refused (401). Set it in Editor Preferences > Plugins > Comfy or the COMFY_API_KEY environment variable. %s"), *What, *Message);
		}
		if (Code == 402)
		{
			return FString::Printf(TEXT("%s: out of credits (402). Add credits at platform.comfy.org. %s"), *What, *Message);
		}
		return FString::Printf(TEXT("%s failed with HTTP %d: %s"), *What, Code, *Message);
	}

	static void Bind(const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request, FCallback Callback)
	{
		Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr HttpResponse, bool bConnected)
		{
			FResponse Response;
			Response.bConnected = bConnected && HttpResponse.IsValid();
			if (HttpResponse.IsValid())
			{
				Response.Raw = HttpResponse;
				Response.Code = HttpResponse->GetResponseCode();
				Response.ContentType = HttpResponse->GetContentType();
				Response.Bytes = HttpResponse->GetContent();
				if (!Response.ContentType.StartsWith(TEXT("image/")) && !Response.ContentType.StartsWith(TEXT("audio/")) && !Response.ContentType.StartsWith(TEXT("video/")) && !Response.ContentType.StartsWith(TEXT("model/")) && !Response.ContentType.StartsWith(TEXT("application/octet-stream")))
				{
					Response.Body = HttpResponse->GetContentAsString();
					if (Response.Body.StartsWith(TEXT("{")))
					{
						const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response.Body);
						FJsonSerializer::Deserialize(Reader, Response.Json);
					}
				}
			}
			Callback(Response);
		});
	}

	static TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Make(const FString& Verb, const FString& Url, const FHeaders& Headers, float TimeoutSeconds)
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetVerb(Verb);
		Request->SetURL(Url);
		Request->SetTimeout(TimeoutSeconds);
		Request->SetHeader(TEXT("User-Agent"), TEXT("ComfyUnreal/0.1"));
		for (const TPair<FString, FString>& Header : Headers)
		{
			Request->SetHeader(Header.Key, Header.Value);
		}
		return Request;
	}

	void Send(const FString& Verb, const FString& Url, const FHeaders& Headers, FCallback Callback, float TimeoutSeconds)
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Make(Verb, Url, Headers, TimeoutSeconds);
		Bind(Request, MoveTemp(Callback));
		Request->ProcessRequest();
	}

	void SendJson(const FString& Verb, const FString& Url, const FHeaders& Headers, const TSharedRef<FJsonObject>& Body, FCallback Callback, float TimeoutSeconds)
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Make(Verb, Url, Headers, TimeoutSeconds);
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(JsonToString(Body));
		Bind(Request, MoveTemp(Callback));
		Request->ProcessRequest();
	}

	void SendMultipart(const FString& Url, const FHeaders& Headers, const TArray<FMultipartField>& Fields, const FString& FileField, const FString& FileName, const FString& FileContentType, const TArray<uint8>& FileBytes, FCallback Callback, float TimeoutSeconds)
	{
		const FString Boundary = FString::Printf(TEXT("----ComfyUnreal%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		TArray<uint8> Body;
		auto Append = [&Body](const FString& Text)
		{
			const FTCHARToUTF8 Utf8(*Text);
			Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		};
		for (const FMultipartField& Field : Fields)
		{
			Append(FString::Printf(TEXT("--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n"), *Boundary, *Field.Name, *Field.Value));
		}
		Append(FString::Printf(TEXT("--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\nContent-Type: %s\r\n\r\n"), *Boundary, *FileField, *FileName, *FileContentType));
		Body.Append(FileBytes);
		Append(FString::Printf(TEXT("\r\n--%s--\r\n"), *Boundary));

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Make(TEXT("POST"), Url, Headers, TimeoutSeconds);
		Request->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
		Request->SetContent(MoveTemp(Body));
		Bind(Request, MoveTemp(Callback));
		Request->ProcessRequest();
	}

	void Download(const FString& Url, const FHeaders& Headers, const FString& LocalPath, TFunction<void(bool, const FString&)> Callback)
	{
		Send(TEXT("GET"), Url, Headers, [LocalPath, Url, Callback = MoveTemp(Callback)](const FResponse& Response)
		{
			if (!Response.IsOk())
			{
				Callback(false, Response.Describe(FString::Printf(TEXT("Downloading %s"), *FPaths::GetCleanFilename(LocalPath))));
				return;
			}
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			PlatformFile.CreateDirectoryTree(*FPaths::GetPath(LocalPath));
			if (!FFileHelper::SaveArrayToFile(Response.Bytes, *LocalPath))
			{
				Callback(false, FString::Printf(TEXT("Couldn't write %s."), *LocalPath));
				return;
			}
			Callback(true, FString());
		}, 600.f);
	}

	void Delay(float Seconds, TFunction<void()> Function)
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Function = MoveTemp(Function)](float) -> bool
		{
			Function();
			return false;
		}), FMath::Max(0.f, Seconds));
	}

	FString Origin(const FString& Url)
	{
		const int32 SchemeEnd = Url.Find(TEXT("://"));
		if (SchemeEnd == INDEX_NONE)
		{
			return Url;
		}
		const int32 PathStart = Url.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SchemeEnd + 3);
		return PathStart == INDEX_NONE ? Url : Url.Left(PathStart);
	}

	FString ResolveLink(const FString& BaseUrl, const FString& Link)
	{
		if (Link.StartsWith(TEXT("http://")) || Link.StartsWith(TEXT("https://")))
		{
			return Link;
		}
		if (Link.StartsWith(TEXT("/")))
		{
			return Origin(BaseUrl) + Link;
		}
		FString Base = BaseUrl;
		Base.RemoveFromEnd(TEXT("/"));
		return Base + TEXT("/") + Link;
	}

	FString UrlEncode(const FString& Value)
	{
		return FGenericPlatformHttp::UrlEncode(Value);
	}

	FString NewIdempotencyKey()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	FString JsonToString(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}
}
