// Copyright Comfy Org. All Rights Reserved.

#include "SComfyStudio.h"
#include "ComfyEditorStyle.h"
#include "ComfyJob.h"
#include "ComfySubsystem.h"
#include "ISettingsModule.h"
#include "SComfyGraph.h"
#include "SComfyRunView.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ComfyStudio"

TWeakPtr<SComfyStudio> SComfyStudio::Instance;

void SComfyStudio::SetActiveView(const FString& View)
{
	if (const TSharedPtr<SComfyStudio> Studio = Instance.Pin())
	{
		Studio->ShowView(View);
	}
}

void SComfyStudio::ShowView(const FString& View)
{
	const bool bGraph = View.Equals(TEXT("Graph"), ESearchCase::IgnoreCase);
	ViewSwitcher->SetActiveWidgetIndex(bGraph ? 1 : 0);
	if (bGraph)
	{
		// The browser starts on first use, so opening Comfy Studio stays cheap.
		Graph->Activate();
	}
}

void SComfyStudio::OpenInGraph(const UComfyWorkflow* Workflow)
{
	ShowView(TEXT("Graph"));
	Graph->OpenWorkflow(Workflow);
}

void SComfyStudio::Construct(const FArguments& InArgs)
{
	const ISlateStyle& Style = FComfyEditorStyle::Get();
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(Style.GetBrush("Comfy.Panel"))
		.Padding(0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildStatusLine()
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(1.f)[SNew(SImage).Image(Style.GetBrush("Comfy.Divider"))]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SAssignNew(ViewSwitcher, SWidgetSwitcher)
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(RunView, SComfyRunView)
					.OnOpenInGraph_Lambda([this](const UComfyWorkflow* Workflow) { OpenInGraph(Workflow); })
					.OnShowGraph_Lambda([this]() { ShowView(TEXT("Graph")); })
					.OnCheckBackend_Lambda([this]() { CheckActiveBackend(); })
				]
				+ SWidgetSwitcher::Slot().Padding(8)
				[
					SAssignNew(Graph, SComfyGraph)
					.SelectedWorkflow_Lambda([this]() { return RunView->GetSelectedWorkflow(); })
					.OnWorkflowSaved_Lambda([this]() { RunView->RefreshWorkflows(); })
				]
			]
		]
	];

	if (UComfySubsystem* Comfy = UComfySubsystem::Get())
	{
		StatusHandle = Comfy->OnBackendStatus.AddSPLambda(this, [this](const FComfyBackendStatus& Status) { HandleBackendStatus(Status); });
		ActiveBackendHandle = Comfy->OnActiveBackendChanged.AddSP(this, &SComfyStudio::HandleActiveBackendChanged);
		ServerWorkflowsHandle = Comfy->OnServerWorkflowsChanged.AddSPLambda(this, [this]() { RunView->RefreshWorkflows(); });
		Comfy->RefreshServerWorkflows();
		Comfy->SendHeartbeat();
	}
	// While the panel is open, tell the Comfy for Unreal extension an editor is connected.
	HeartbeatHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
	{
		if (UComfySubsystem* Comfy = UComfySubsystem::Get())
		{
			Comfy->SendHeartbeat();
		}
		return true;
	}), 15.f);

	RefreshBackends();
	CheckActiveBackend();
	LoadCloudPageIfActive();
	Instance = SharedThis(this);
}

SComfyStudio::~SComfyStudio()
{
	FTSTicker::GetCoreTicker().RemoveTicker(HeartbeatHandle);
	if (UComfySubsystem* Comfy = UComfySubsystem::Get())
	{
		Comfy->OnBackendStatus.Remove(StatusHandle);
		Comfy->OnActiveBackendChanged.Remove(ActiveBackendHandle);
		Comfy->OnServerWorkflowsChanged.Remove(ServerWorkflowsHandle);
	}
}

TSharedRef<SWidget> SComfyStudio::BuildStatusLine()
{
	const ISlateStyle& Style = FComfyEditorStyle::Get();
	return SNew(SBox)
		.Padding(FMargin(10, 6))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
			[
				SNew(SSegmentedControl<int32>)
				.Value_Lambda([this]() { return ViewSwitcher.IsValid() ? ViewSwitcher->GetActiveWidgetIndex() : 0; })
				.OnValueChanged_Lambda([this](int32 Index) { ShowView(Index == 1 ? TEXT("Graph") : TEXT("Run")); })
				+ SSegmentedControl<int32>::Slot(0).Text(LOCTEXT("RunView", "Run")).ToolTip(LOCTEXT("RunViewTooltip", "Run a workflow and bring the results into the project."))
				+ SSegmentedControl<int32>::Slot(1).Text(LOCTEXT("GraphView", "Graph")).ToolTip(LOCTEXT("GraphViewTooltip", "Edit graphs in ComfyUI on the active backend, and send them to Unreal."))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).MinDesiredWidth(150.f)
				[
					SAssignNew(BackendCombo, SComboBox<TSharedPtr<FName>>)
					.OptionsSource(&BackendOptions)
					.OnGenerateWidget_Lambda([](TSharedPtr<FName> Item) { return SNew(STextBlock).Text(FText::FromName(Item.IsValid() ? *Item : NAME_None)); })
					.OnSelectionChanged_Lambda([this](TSharedPtr<FName> Item, ESelectInfo::Type Type)
					{
						if (Item.IsValid() && Type != ESelectInfo::Direct)
						{
							UComfySubsystem::Get()->SetActiveBackend(*Item, true);
						}
					})
					[
						SNew(STextBlock).Text_Lambda([]()
						{
							const UComfySubsystem* Comfy = UComfySubsystem::Get();
							return Comfy ? FText::FromName(Comfy->GetActiveBackend()) : FText::GetEmpty();
						})
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 6, 0)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("●"))).ColorAndOpacity(this, &SComfyStudio::GetStatusColor)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(&Style, "Comfy.Text.Muted")
				.Text(this, &SComfyStudio::GetStatusText)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)
			[
				SNew(STextBlock)
				.TextStyle(&Style, "Comfy.Text.Muted")
				.Text_Lambda([this]() { return FText::Format(LOCTEXT("Queue", "{0} running"), FText::AsNumber(GetActiveJobCount())); })
				.Visibility_Lambda([this]() { return GetActiveJobCount() > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.ButtonStyle(&Style, "Comfy.Button")
				.Visibility_Lambda([this]() { return Connection == EConnection::Connecting ? EVisibility::Collapsed : EVisibility::Visible; })
				.ToolTipText(LOCTEXT("RetryTooltip", "Check that the backend answers and accepts your key."))
				.OnClicked_Lambda([this]() { CheckActiveBackend(); return FReply::Handled(); })
				[
					SNew(STextBlock).TextStyle(&Style, "Comfy.Text")
					.Text_Lambda([this]() { return Connection == EConnection::Connected ? LOCTEXT("Check", "Check") : LOCTEXT("Retry", "Retry"); })
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(&Style, "Comfy.Button.Ghost")
				.ToolTipText(LOCTEXT("SettingsTooltip", "Backends live in Project Settings > Plugins > Comfy. Your API key lives in Editor Preferences > Plugins > Comfy."))
				.OnClicked_Lambda([]()
				{
					FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Plugins", "ComfySettings");
					return FReply::Handled();
				})
				[
					SNew(STextBlock).TextStyle(&Style, "Comfy.Text").Text(LOCTEXT("Settings", "Settings"))
				]
			]
		];
}

void SComfyStudio::RefreshBackends()
{
	BackendOptions.Reset();
	const UComfySubsystem* Comfy = UComfySubsystem::Get();
	if (!Comfy)
	{
		return;
	}
	TSharedPtr<FName> Active;
	for (const FName Name : Comfy->GetBackendNames())
	{
		TSharedPtr<FName> Option = MakeShared<FName>(Name);
		if (Name == Comfy->GetActiveBackend())
		{
			Active = Option;
		}
		BackendOptions.Add(Option);
	}
	if (BackendCombo.IsValid())
	{
		BackendCombo->RefreshOptions();
		BackendCombo->SetSelectedItem(Active);
	}
}

void SComfyStudio::HandleActiveBackendChanged()
{
	RefreshBackends();
	CheckActiveBackend();
	if (UComfySubsystem* Comfy = UComfySubsystem::Get())
	{
		Comfy->RefreshServerWorkflows();
	}
	if (ViewSwitcher->GetActiveWidgetIndex() == 1)
	{
		Graph->Activate();
	}
	LoadCloudPageIfActive();
}

void SComfyStudio::LoadCloudPageIfActive()
{
	// Comfy Cloud's sign-in lives in the Graph view's page, so it loads in the background
	// to report the account and run jobs even while the Run view is showing.
	FComfyBackendProfile Profile;
	const UComfySubsystem* Comfy = UComfySubsystem::Get();
	if (Comfy && Comfy->GetBackendProfile(NAME_None, Profile) && Profile.Kind == EComfyBackendKind::Cloud)
	{
		Graph->Activate();
	}
}

void SComfyStudio::CheckActiveBackend()
{
	if (UComfySubsystem* Comfy = UComfySubsystem::Get())
	{
		Connection = EConnection::Connecting;
		Comfy->ProbeBackend(Comfy->GetActiveBackend(), FComfyBackendStatusDelegate());
	}
}

void SComfyStudio::HandleBackendStatus(const FComfyBackendStatus& Status)
{
	const UComfySubsystem* Comfy = UComfySubsystem::Get();
	if (Comfy && Status.Backend == Comfy->GetActiveBackend())
	{
		Connection = Status.bReachable ? EConnection::Connected : Status.bNeedsSignIn ? EConnection::SignIn : EConnection::Failed;
	}
	RunView->HandleBackendStatus(Status);
}

FText SComfyStudio::GetStatusText() const
{
	const UComfySubsystem* Comfy = UComfySubsystem::Get();
	if (!Comfy)
	{
		return FText::GetEmpty();
	}
	const FComfyBackendStatus Status = Comfy->GetLastStatus(Comfy->GetActiveBackend());
	switch (Connection)
	{
	case EConnection::Connecting:
		return LOCTEXT("Connecting", "Connecting...");
	case EConnection::Connected:
		return FText::Format(LOCTEXT("Connected", "Connected  ·  {0}"), FText::FromString(Status.Summary));
	case EConnection::Failed:
		return FText::Format(LOCTEXT("Failed", "Not connected  ·  {0}"), FText::FromString(Status.Summary));
	case EConnection::SignIn:
		return FText::FromString(Status.Summary);
	default:
		return LOCTEXT("Disconnected", "Disconnected");
	}
}

FSlateColor SComfyStudio::GetStatusColor() const
{
	switch (Connection)
	{
	case EConnection::Connected:
		return FComfyEditorStyle::FPalette::Success;
	case EConnection::Failed:
		return FComfyEditorStyle::FPalette::Danger;
	case EConnection::Connecting:
	case EConnection::SignIn:
		return FComfyEditorStyle::FPalette::Accent;
	default:
		return FComfyEditorStyle::FPalette::TextMuted;
	}
}

int32 SComfyStudio::GetActiveJobCount() const
{
	int32 Count = 0;
	if (const UComfySubsystem* Comfy = UComfySubsystem::Get())
	{
		for (const UComfyJob* Job : Comfy->GetJobs())
		{
			Count += Job && !Job->IsFinished() ? 1 : 0;
		}
	}
	return Count;
}

#undef LOCTEXT_NAMESPACE
