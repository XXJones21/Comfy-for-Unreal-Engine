// Copyright Comfy Org. All Rights Reserved.

#include "SComfyGraph.h"
#include "ComfyEditorLibrary.h"
#include "ComfyGraphBridge.h"
#include "ComfySubsystem.h"
#include "ComfyWorkflow.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "IWebBrowserPopupFeatures.h"
#include "IWebBrowserSingleton.h"
#include "IWebBrowserWindow.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SWebBrowser.h"
#include "Styling/AppStyle.h"
#include "WebBrowserModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ComfyGraph"

namespace ComfyGraphPrivate
{
	/** The page's ComfyUI app object, or a reported error. Newer frontends keep window.app for extensions. */
	static const TCHAR* FindApp = TEXT(
		"const bridge = window.ue && window.ue.comfyunreal;"
		"const app = window.app || (window.comfyAPI && window.comfyAPI.app && window.comfyAPI.app.app);"
		"if (!bridge) { return; }"
		"if (!app || typeof app.graphToPrompt !== 'function') { bridge.reporterror('This page is not the ComfyUI graph editor, or it has not finished loading.'); return; }");

	static const TCHAR* SendScript = TEXT(
		"const wf = (app.extensionManager && app.extensionManager.workflow && app.extensionManager.workflow.activeWorkflow) || (app.workflowManager && app.workflowManager.activeWorkflow);"
		"const name = (wf && (wf.filename || wf.path)) || 'Graph';"
		"try { const p = await app.graphToPrompt(); bridge.receivegraph(JSON.stringify(p.workflow), JSON.stringify(p.output), String(name)); }"
		"catch (e) { bridge.reporterror('Reading the graph failed: ' + (e && e.message ? e.message : String(e))); }");

	static FString Wrap(const FString& Body)
	{
		return FString::Printf(TEXT("(async () => { %s %s })();"), FindApp, *Body);
	}

	static FString JsString(const FString& Text)
	{
		return TEXT("\"") + Text.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\"")).Replace(TEXT("\n"), TEXT("\\n")).Replace(TEXT("\r"), TEXT("")) + TEXT("\"");
	}
}

void SComfyGraph::Construct(const FArguments& InArgs)
{
	SelectedWorkflow = InArgs._SelectedWorkflow;
	OnWorkflowSaved = InArgs._OnWorkflowSaved;

	Bridge.Reset(NewObject<UComfyGraphBridge>());
	TWeakPtr<SComfyGraph> WeakThis = SharedThis(this);
	Bridge->OnGraph = [WeakThis](const FString& WorkflowJson, const FString& ApiJson, const FString& Name)
	{
		if (const TSharedPtr<SComfyGraph> This = WeakThis.Pin())
		{
			This->HandleGraph(WorkflowJson, ApiJson, Name);
		}
	};
	Bridge->OnError = [WeakThis](const FString& Text)
	{
		if (const TSharedPtr<SComfyGraph> This = WeakThis.Pin())
		{
			This->SetMessage(FText::FromString(Text), true);
		}
	};

	auto Button = [](const FText& Label, const FText& Tooltip, FOnClicked OnClicked, TAttribute<bool> Enabled)
	{
		return SNew(SButton).Text(Label).ToolTipText(Tooltip).OnClicked(OnClicked).IsEnabled(Enabled);
	};
	const TAttribute<bool> BrowserReady = TAttribute<bool>::CreateSP(this, &SComfyGraph::HasBrowser);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				Button(LOCTEXT("Send", "Send to Unreal"), LOCTEXT("SendTooltip", "Save the graph open in ComfyUI as a Comfy Workflow asset in /Game/Comfy/Workflows, ready to run from the Run view, Blueprints and agents."),
					FOnClicked::CreateSP(this, &SComfyGraph::OnSendToUnreal), BrowserReady)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				Button(LOCTEXT("OpenSelected", "Open in Graph"), LOCTEXT("OpenSelectedTooltip", "Load the workflow selected in the Run view into ComfyUI."),
					FOnClicked::CreateSP(this, &SComfyGraph::OnOpenSelected),
					TAttribute<bool>::CreateLambda([this]() { return HasBrowser() && SelectedWorkflow.Get() != nullptr; }))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				Button(LOCTEXT("Reload", "Reload"), FText::GetEmpty(), FOnClicked::CreateSP(this, &SComfyGraph::OnReload), true)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				Button(LOCTEXT("OpenInBrowser", "Open in browser"), LOCTEXT("OpenInBrowserTooltip", "Open this ComfyUI in your web browser."),
					FOnClicked::CreateSP(this, &SComfyGraph::OnOpenInBrowser), TAttribute<bool>::CreateLambda([this]() { return !LoadedUrl.IsEmpty(); }))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return Message; })
				.ColorAndOpacity_Lambda([this]() { return bMessageIsError ? FSlateColor(FLinearColor(0.9f, 0.35f, 0.3f)) : FSlateColor::UseSubduedForeground(); })
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SAssignNew(BrowserHost, SBox)
		]
	];
}

bool SComfyGraph::HasBrowser() const
{
	return Browser.IsValid();
}

void SComfyGraph::SetMessage(const FText& Text, bool bError)
{
	Message = Text;
	bMessageIsError = bError;
}

FString SComfyGraph::GetFrontendUrl(FText& OutWhyNot) const
{
	const UComfySubsystem* Comfy = UComfySubsystem::Get();
	FComfyBackendProfile Profile;
	if (!Comfy || !Comfy->GetBackendProfile(NAME_None, Profile))
	{
		OutWhyNot = LOCTEXT("NoBackend", "No active backend. Pick one in the Run view.");
		return FString();
	}
	switch (Profile.Kind)
	{
	case EComfyBackendKind::Local:
	{
		FString Url = Profile.BaseUrl;
		Url.RemoveFromEnd(TEXT("/"));
		return Url + TEXT("/");
	}
	case EComfyBackendKind::Cloud:
		return TEXT("https://cloud.comfy.org/");
	case EComfyBackendKind::Deployment:
		OutWhyNot = LOCTEXT("Deployment", "A Comfy API deployment has no graph editor. Switch to Desktop or Comfy Cloud to edit graphs.");
		return FString();
	case EComfyBackendKind::Router:
		OutWhyNot = LOCTEXT("Router", "Comfy Router runs single models and has no graph editor. Switch to Desktop or Comfy Cloud to edit graphs.");
		return FString();
	}
	return FString();
}

void SComfyGraph::Activate()
{
	FText WhyNot;
	const FString Url = GetFrontendUrl(WhyNot);
	if (Url.IsEmpty())
	{
		BrowserHost->SetContent(SNew(SBox).Padding(12)[SNew(STextBlock).AutoWrapText(true).Text(WhyNot)]);
		Browser.Reset();
		LoadedUrl.Reset();
		SetMessage(FText::GetEmpty(), false);
		return;
	}
	if (Url != LoadedUrl || !Browser.IsValid())
	{
		ShowBrowser(Url);
	}
}

void SComfyGraph::ShowBrowser(const FString& Url)
{
	// Load the module on first use: it isn't loaded at editor start, and loading it brings up CEF.
	IWebBrowserModule* WebBrowser = FModuleManager::Get().LoadModulePtr<IWebBrowserModule>(TEXT("WebBrowser"));
	if (!WebBrowser || !WebBrowser->IsWebModuleAvailable())
	{
		BrowserHost->SetContent(SNew(SBox).Padding(12)[SNew(STextBlock).AutoWrapText(true)
			.Text(LOCTEXT("NoWebBrowser", "The embedded web browser isn't available in this editor, so the graph can't be shown here. Use \"Open in browser\" instead."))]);
		LoadedUrl = Url;
		return;
	}

	// A persistent context keeps the Comfy Cloud sign-in between sessions.
	FBrowserContextSettings Context(TEXT("ComfyUnreal"));
	Context.CookieStorageLocation = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Comfy") / TEXT("Browser"));
	Context.bPersistSessionCookies = true;

	FCreateBrowserWindowSettings Settings;
	Settings.InitialURL = Url;
	Settings.BrowserFrameRate = 60;
	Settings.Context = Context;
	const TSharedPtr<IWebBrowserWindow> Window = WebBrowser->GetSingleton()->CreateBrowserWindow(Settings);

	BrowserHost->SetContent(
		SAssignNew(Browser, SWebBrowser, Window)
		.ShowControls(false)
		.ShowAddressBar(false)
		.OnCreateWindow(this, &SComfyGraph::HandleCreateWindow)
		.OnCloseWindow(this, &SComfyGraph::HandleCloseWindow));
	Browser->BindUObject(TEXT("comfyunreal"), Bridge.Get(), true);
	LoadedUrl = Url;
	SetMessage(FText::Format(LOCTEXT("Showing", "ComfyUI at {0}"), FText::FromString(Url)), false);
}

FReply SComfyGraph::OnSendToUnreal()
{
	if (Browser.IsValid())
	{
		SetMessage(LOCTEXT("Reading", "Reading the graph..."), false);
		Browser->ExecuteJavascript(ComfyGraphPrivate::Wrap(ComfyGraphPrivate::SendScript));
	}
	return FReply::Handled();
}

void SComfyGraph::HandleGraph(const FString& WorkflowJson, const FString& ApiJson, const FString& Name)
{
	FString Error;
	if (const UComfyWorkflow* Workflow = UComfyEditorLibrary::SaveGraphAsWorkflow(WorkflowJson, ApiJson, Name, Error))
	{
		SetMessage(FText::Format(LOCTEXT("Saved", "Saved {0} with {1} input(s)."), FText::FromString(Workflow->GetPathName()), FText::AsNumber(Workflow->Inputs.Num())), false);
		OnWorkflowSaved.ExecuteIfBound();
	}
	else
	{
		SetMessage(FText::Format(LOCTEXT("SaveFailed", "Couldn't save the graph: {0}"), FText::FromString(Error)), true);
	}
}

FReply SComfyGraph::OnOpenSelected()
{
	const UComfyWorkflow* Workflow = SelectedWorkflow.Get();
	if (!Browser.IsValid() || !Workflow)
	{
		return FReply::Handled();
	}
	// The canvas save keeps the layout; an API graph gets laid out by the frontend.
	const FString Body = Workflow->UiGraphJson.IsEmpty()
		? FString::Printf(TEXT("await app.loadApiJson(%s, %s);"), *Workflow->ApiGraphJson, *ComfyGraphPrivate::JsString(Workflow->GetDisplayNameString()))
		: FString::Printf(TEXT("await app.loadGraphData(%s);"), *Workflow->UiGraphJson);
	Browser->ExecuteJavascript(ComfyGraphPrivate::Wrap(Body));
	SetMessage(FText::Format(LOCTEXT("Opened", "Opened {0} in the graph."), FText::FromString(Workflow->GetDisplayNameString())), false);
	return FReply::Handled();
}

FReply SComfyGraph::OnReload()
{
	if (Browser.IsValid())
	{
		Browser->Reload();
	}
	else
	{
		Activate();
	}
	return FReply::Handled();
}

FReply SComfyGraph::OnOpenInBrowser()
{
	if (!LoadedUrl.IsEmpty())
	{
		FPlatformProcess::LaunchURL(*LoadedUrl, nullptr, nullptr);
	}
	return FReply::Handled();
}

bool SComfyGraph::HandleCreateWindow(const TWeakPtr<IWebBrowserWindow>& NewWindow, const TWeakPtr<IWebBrowserPopupFeatures>& Features)
{
	const TSharedPtr<IWebBrowserWindow> NewWindowPinned = NewWindow.Pin();
	if (!NewWindowPinned.IsValid())
	{
		return false;
	}
	const TSharedPtr<IWebBrowserPopupFeatures> FeaturesPinned = Features.Pin();
	const int32 Width = FeaturesPinned.IsValid() && FeaturesPinned->IsWidthSet() ? FeaturesPinned->GetWidth() : 520;
	const int32 Height = FeaturesPinned.IsValid() && FeaturesPinned->IsHeightSet() ? FeaturesPinned->GetHeight() : 680;

	// Sign-in popups (Google, GitHub) talk back to the page through window.opener, so they stay in this browser's context.
	const TSharedRef<SWindow> PopupWindow = SNew(SWindow)
		.Title(LOCTEXT("PopupTitle", "Comfy"))
		.ClientSize(FVector2D(Width, Height))
		.SupportsMaximize(true)
		.SupportsMinimize(false);
	PopupWindow->SetContent(
		SNew(SWebBrowser, NewWindowPinned)
		.ShowControls(false)
		.ShowAddressBar(false)
		.OnCreateWindow(this, &SComfyGraph::HandleCreateWindow)
		.OnCloseWindow(this, &SComfyGraph::HandleCloseWindow));

	const TSharedPtr<SWindow> Parent = FSlateApplication::Get().FindWidgetWindow(SharedThis(this));
	if (Parent.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(PopupWindow, Parent.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(PopupWindow);
	}
	Popups.Emplace(NewWindow, PopupWindow);
	return true;
}

bool SComfyGraph::HandleCloseWindow(const TWeakPtr<IWebBrowserWindow>& ClosingWindow)
{
	for (int32 Index = Popups.Num() - 1; Index >= 0; --Index)
	{
		if (Popups[Index].Key == ClosingWindow)
		{
			if (const TSharedPtr<SWindow> Window = Popups[Index].Value.Pin())
			{
				Window->RequestDestroyWindow();
			}
			Popups.RemoveAt(Index);
			return true;
		}
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
