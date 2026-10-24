// Copyright Comfy Org. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Widgets/SCompoundWidget.h"
#include "ComfyTypes.h"

class SComfyGraph;
class SComfyRunView;
class SWidgetSwitcher;
template <typename OptionType> class SComboBox;

/**
 * The Comfy Studio tab: a slim status line over two views. Run is laid out like ComfyUI's App mode;
 * Graph is the ComfyUI frontend itself. Generated assets land in the project.
 */
class SComfyStudio : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SComfyStudio) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SComfyStudio() override;

	/** Switch the open panel to "Run" or "Graph". */
	static void SetActiveView(const FString& View);

private:
	enum class EConnection : uint8
	{
		Unknown,
		Connecting,
		Connected,
		Failed,
	};

	TSharedRef<SWidget> BuildStatusLine();
	void RefreshBackends();
	void CheckActiveBackend();
	void HandleBackendStatus(const FComfyBackendStatus& Status);
	void HandleActiveBackendChanged();
	FText GetStatusText() const;
	FSlateColor GetStatusColor() const;
	int32 GetActiveJobCount() const;
	void ShowView(const FString& View);
	void OpenInGraph(const class UComfyWorkflow* Workflow);

	TArray<TSharedPtr<FName>> BackendOptions;
	TSharedPtr<SComboBox<TSharedPtr<FName>>> BackendCombo;
	EConnection Connection = EConnection::Unknown;

	TSharedPtr<SWidgetSwitcher> ViewSwitcher;
	TSharedPtr<SComfyRunView> RunView;
	TSharedPtr<SComfyGraph> Graph;

	/** The open panel, for SetActiveView. Comfy Studio is a single nomad tab. */
	static TWeakPtr<SComfyStudio> Instance;

	FDelegateHandle StatusHandle;
	FDelegateHandle ActiveBackendHandle;
	FDelegateHandle ServerWorkflowsHandle;
	FTSTicker::FDelegateHandle HeartbeatHandle;
};
