// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComfyTypes.h"
#include "ComfyHttp.h"

class UComfyJob;
class FJsonObject;
class FJsonValue;

enum class EComfyPollResult : uint8
{
	Pending,
	Done,
	Failed,
	Canceled,
};

/**
 * A service that runs API-format workflow graphs. One instance per backend profile.
 * All callbacks run on the game thread.
 */
class IComfyBackend : public TSharedFromThis<IComfyBackend>
{
public:
	using FUploadCallback = TFunction<void(bool bOk, TSharedPtr<FJsonValue> GraphValue, const FString& Error)>;
	using FResultCallback = TFunction<void(bool bOk, const FString& Error)>;
	using FPollCallback = TFunction<void(EComfyPollResult Result, const FString& Error, float NextPollSeconds)>;

	IComfyBackend(const FComfyBackendProfile& InProfile, const FString& InApiKey)
		: Profile(InProfile), ApiKey(InApiKey)
	{
		Profile.BaseUrl.RemoveFromEnd(TEXT("/"));
	}
	virtual ~IComfyBackend() = default;

	virtual void Probe(TFunction<void(const FComfyBackendStatus&)> Callback) = 0;

	/** Upload a local file and return the value to put in the graph in place of a filename. */
	virtual void UploadFile(const FString& LocalPath, FUploadCallback Callback) = 0;

	/** Submit a built graph. Sets Job->RemoteId on success. */
	virtual void Submit(UComfyJob* Job, const TSharedRef<FJsonObject>& Graph, FResultCallback Callback) = 0;

	/** Check on a submitted job, update its progress, and on Done fill Job->Outputs with remote references. */
	virtual void Poll(UComfyJob* Job, FPollCallback Callback) = 0;

	/** Download one output to OutputFile.LocalPath. */
	virtual void Download(const FComfyOutputFile& OutputFile, FResultCallback Callback) = 0;

	virtual void CancelJob(UComfyJob* Job) = 0;

	const FComfyBackendProfile& GetProfile() const { return Profile; }
	bool HasApiKey() const { return !ApiKey.IsEmpty(); }

protected:
	FComfyBackendProfile Profile;
	FString ApiKey;
};

/** Stock ComfyUI routes (Comfy Desktop, portable, manual installs). */
class FComfyNativeBackend : public IComfyBackend
{
public:
	FComfyNativeBackend(const FComfyBackendProfile& InProfile, const FString& InApiKey);
	virtual ~FComfyNativeBackend() override;

	virtual void Probe(TFunction<void(const FComfyBackendStatus&)> Callback) override;
	virtual void UploadFile(const FString& LocalPath, FUploadCallback Callback) override;
	virtual void Submit(UComfyJob* Job, const TSharedRef<FJsonObject>& Graph, FResultCallback Callback) override;
	virtual void Poll(UComfyJob* Job, FPollCallback Callback) override;
	virtual void Download(const FComfyOutputFile& OutputFile, FResultCallback Callback) override;
	virtual void CancelJob(UComfyJob* Job) override;

private:
	void EnsureSocket();
	void HandleSocketMessage(const FString& Message);

	FString ClientId;
	TSharedPtr<class IWebSocket> Socket;
	TMap<FString, TWeakObjectPtr<UComfyJob>> JobsByPromptId;
};

/** Comfy API v2: Comfy Cloud, Comfy API deployments, and comfy-api-proxy. */
class FComfyApiV2Backend : public IComfyBackend
{
public:
	using IComfyBackend::IComfyBackend;

	virtual void Probe(TFunction<void(const FComfyBackendStatus&)> Callback) override;
	virtual void UploadFile(const FString& LocalPath, FUploadCallback Callback) override;
	virtual void Submit(UComfyJob* Job, const TSharedRef<FJsonObject>& Graph, FResultCallback Callback) override;
	virtual void Poll(UComfyJob* Job, FPollCallback Callback) override;
	virtual void Download(const FComfyOutputFile& OutputFile, FResultCallback Callback) override;
	virtual void CancelJob(UComfyJob* Job) override;

private:
	ComfyHttp::FHeaders AuthHeaders() const;
	TMap<FString, FString> SelfUrls;
	TMap<FString, FString> CancelUrls;
};
