// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComfyGraphBridge.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class IWebBrowserPopupFeatures;
class IWebBrowserWindow;
class SBox;
class SWebBrowser;
class SWindow;
class UComfyWorkflow;

/**
 * Comfy Studio's Graph view: the ComfyUI frontend of the active backend (a local ComfyUI or Comfy
 * Desktop, or Comfy Cloud) in an embedded browser, with a bridge to move graphs between it and Unreal.
 */
class FComfyGraphPageChannel;

class SComfyGraph : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SComfyGraph) {}
		/** The workflow selected in the Run view, for "Open in Graph". */
		SLATE_ATTRIBUTE(UComfyWorkflow*, SelectedWorkflow)
		/** Called after "Send to Unreal" saved a workflow asset. */
		SLATE_EVENT(FSimpleDelegate, OnWorkflowSaved)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SComfyGraph() override;

	/** Load the active backend's frontend, creating the browser on first use or when the backend changed. */
	void Activate();

	/** Load a workflow into the ComfyUI canvas: its canvas save when it has one, else its API graph. */
	void OpenWorkflow(const UComfyWorkflow* Workflow);

private:
	FString GetFrontendUrl(FText& OutWhyNot) const;
	void ShowBrowser(const FString& Url);
	void SetMessage(const FText& Text, bool bError);
	bool HasBrowser() const;

	FReply OnSendToUnreal();
	FReply OnOpenSelected();
	FReply OnReload();
	FReply OnOpenInBrowser();

	void HandleGraph(const FString& WorkflowJson, const FString& ApiJson, const FString& Name);
	void HandleAccount(const FString& AccountJson);
	void HandleLoadStarted();
	void HandleLoadCompleted();
	bool IsCloudPage() const;
	/** The page bridge script from Resources/Bridge, read once. */
	const FString& GetBridgeScript();
	bool HandleCreateWindow(const TWeakPtr<IWebBrowserWindow>& NewWindow, const TWeakPtr<IWebBrowserPopupFeatures>& Features);
	bool HandleCloseWindow(const TWeakPtr<IWebBrowserWindow>& ClosingWindow);

	TAttribute<UComfyWorkflow*> SelectedWorkflow;
	FSimpleDelegate OnWorkflowSaved;

	TSharedPtr<SBox> BrowserHost;
	TSharedPtr<SWebBrowser> Browser;
	FString LoadedUrl;
	bool bPageLoading = false;
	/** A workflow asked for before the page finished loading; opened once it has. */
	TWeakObjectPtr<const UComfyWorkflow> PendingWorkflow;
	TStrongObjectPtr<UComfyGraphBridge> Bridge;
	/** Lets Comfy Core run calls in this page: Comfy Cloud jobs without an API key. */
	TSharedPtr<FComfyGraphPageChannel> Channel;
	FString BridgeScript;
	FDelegateHandle RefreshAccountHandle;

	FText Message;
	bool bMessageIsError = false;

	/** Sign-in and other popups, each in its own window. */
	TArray<TPair<TWeakPtr<IWebBrowserWindow>, TWeakPtr<SWindow>>> Popups;
};
