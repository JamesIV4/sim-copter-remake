// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SSimCopterReplayPanel.h"

#include "Framework/Application/SlateApplication.h"
#include "Replay/SimCopterReplayFreeCamera.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimCopterReplayPanel"

namespace
{
FSlateFontInfo PanelFont(const int32 Size, const bool bBold = false)
{
	return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
}

const FLinearColor PanelBackground(0.02f, 0.03f, 0.05f, 0.88f);
const FLinearColor ButtonColor(0.10f, 0.13f, 0.18f, 1.0f);
const FLinearColor ActiveButtonColor(0.18f, 0.38f, 0.68f, 1.0f);
const FLinearColor RecordButtonColor(0.62f, 0.14f, 0.14f, 1.0f);
const FLinearColor DimText(0.62f, 0.66f, 0.72f, 1.0f);

/** Colours the timeline ticks by what kind of thing happened, so the bar reads at a glance. */
FLinearColor MarkerColorForEvent(const SimCopterReplay::EReplayEventKind Kind)
{
	switch (Kind)
	{
	case SimCopterReplay::EReplayEventKind::MissionMessage: return FLinearColor(0.98f, 0.62f, 0.16f, 1.0f);
	case SimCopterReplay::EReplayEventKind::Bookmark:       return FLinearColor(0.42f, 0.92f, 0.48f, 1.0f);
	case SimCopterReplay::EReplayEventKind::PersonOpcode:   return FLinearColor(0.35f, 0.40f, 0.48f, 1.0f);
	default:                                                return FLinearColor(0.55f, 0.72f, 0.95f, 1.0f);
	}
}

/**
 * Most of a busy clip's events are one person's decisions, and a tick per decision paints the whole
 * bar solid. Only this many markers are drawn, spread evenly through the list, with mission
 * messages and bookmarks always kept - those are the ones an operator is actually looking for.
 */
constexpr int32 MaxTimelineMarkers = 400;

/** How many event lines the panel shows under the timeline. */
constexpr int32 EventListLength = 8;

TSharedRef<SWidget> MakePanelButton(
	const TAttribute<FText>& Label,
	const FOnClicked& OnClicked,
	const TAttribute<FSlateColor>& Color,
	const float MinWidth = 0.0f,
	const TAttribute<bool>& IsEnabled = true)
{
	return SNew(SBox)
		.MinDesiredWidth(MinWidth)
		[
			SNew(SButton)
			// Never focusable. The panel sits over a live world and, when free cam is on, over a
			// camera being flown with WASD - a focused button swallows those keys outright. The
			// helicopter's developer panel carries the same rule for the same reason.
			.IsFocusable(false)
			.IsEnabled(IsEnabled)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ContentPadding(FMargin(8.0f, 4.0f))
			.ButtonColorAndOpacity(Color)
			.OnClicked(OnClicked)
			[
				SNew(STextBlock).Text(Label).Font(PanelFont(11, true))
			]
		];
}
}

void SSimCopterReplayPanel::Construct(const FArguments& InArgs)
{
	Replay = InArgs._Replay;
	OnRequestClose = InArgs._OnRequestClose;

	if (USimCopterReplaySubsystem* Subsystem = GetReplay())
	{
		StateChangedHandle = Subsystem->OnStateChanged().AddSP(
			this, &SSimCopterReplayPanel::HandleReplayStateChanged);
	}

	// The widget is added as viewport content, so it covers the WHOLE screen even though the bar it
	// draws is bottom-docked. Left hit-testable it would swallow every mouse click aimed at the
	// world - the tools are on the left button - so the panel itself is transparent to the pointer
	// and only its children (the bar and its controls) take clicks.
	SetVisibility(EVisibility::SelfHitTestInvisible);

	ChildSlot
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Bottom)
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(PanelBackground)
		.Padding(FMargin(14.0f, 10.0f))
		[
			SNew(SVerticalBox)

			// --- title / state ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PanelTitle", "REPLAY"))
					.Font(PanelFont(13, true))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(12.0f, 0.0f, 0.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(this, &SSimCopterReplayPanel::GetStateText)
					.Font(PanelFont(11, true))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.98f, 0.82f, 0.28f, 1.0f)))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SSimCopterReplayPanel::GetStatusText)
					.Font(PanelFont(10))
					.ColorAndOpacity(FSlateColor(DimText))
				]
			]

			// --- the scrub bar ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
			[
				SAssignNew(Timeline, SSimCopterReplayTimeline)
				.DurationSeconds_Lambda([this]()
				{
					const USimCopterReplaySubsystem* Subsystem = GetReplay();
					if (Subsystem == nullptr)
					{
						return 0.0f;
					}
					// While recording, the bar grows with the take, so it doubles as a length
					// readout - there is nothing to scrub yet, and the timeline refuses a scrub on
					// a clip it is not reviewing.
					return Subsystem->GetState() == ESimCopterReplayState::Recording
						? Subsystem->GetRecordedSeconds()
						: Subsystem->GetClipDurationSeconds();
				})
				.PlayheadSeconds_Lambda([this]()
				{
					const USimCopterReplaySubsystem* Subsystem = GetReplay();
					if (Subsystem == nullptr)
					{
						return 0.0f;
					}
					return Subsystem->GetState() == ESimCopterReplayState::Recording
						? Subsystem->GetRecordedSeconds()
						: Subsystem->GetPlayheadSeconds();
				})
				.BarHeight(26.0f)
				.OnScrub(FOnSimCopterReplayScrub::CreateSP(this, &SSimCopterReplayPanel::HandleScrub))
				.OnScrubBegin(FSimpleDelegate::CreateSP(this, &SSimCopterReplayPanel::HandleScrubBegin))
				.OnScrubEnd(FSimpleDelegate::CreateSP(this, &SSimCopterReplayPanel::HandleScrubEnd))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 2.0f)) [ BuildTransportRow() ]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 2.0f)) [ BuildViewRow() ]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 2.0f)) [ BuildClipRow() ]

			// --- saved clips, when the list is expanded ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
			[
				SNew(SBox)
				.MaxDesiredHeight(140.0f)
				.Visibility(this, &SSimCopterReplayPanel::GetClipListVisibility)
				[
					SAssignNew(ClipListScrollBox, SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(ClipListBox, SVerticalBox)
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f)) [ BuildEventList() ]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(LOCTEXT(
					"PanelHint",
					"Tab hides these controls — a take keeps recording, a review keeps playing    C camera    H hide HUD    Space play/pause    ← → one frame    M marker\n"
					"Free cam: WASD move, Ctrl/Space down/up, hold right mouse to look, wheel FOV.    CLOSE ends the review and hands the city back."))
				.Font(PanelFont(9))
				.ColorAndOpacity(FSlateColor(DimText))
			]
		]
	];

	// Both scroll boxes accept keyboard focus by default, and either one taking it on a click would
	// move the keyboard off the game viewport and stop the player flying - the same fault
	// SupportsKeyboardFocus above exists to prevent for the panel as a whole.
	if (EventListBox.IsValid())
	{
		EventListBox->SetIsFocusable(false);
	}
	if (ClipListScrollBox.IsValid())
	{
		ClipListScrollBox->SetIsFocusable(false);
	}

	RebuildTimelineMarkers();
	RefreshEventList();
}

bool SSimCopterReplayPanel::IsTypingClipName() const
{
	return ClipNameBox.IsValid()
		&& (ClipNameBox->HasKeyboardFocus() || ClipNameBox->HasFocusedDescendants());
}

void SSimCopterReplayPanel::ReturnFocusToGame() const
{
	// The clip name box is the one widget in here that may hold the keyboard. It gives it back the
	// moment it is done, unconditionally - there is no state in which the panel should be holding
	// the keyboard, because every shortcut it needs is a controller binding.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

SSimCopterReplayPanel::~SSimCopterReplayPanel()
{
	// The subsystem outlives the panel every time the panel is closed, so the binding has to go
	// with it or the next broadcast reaches a destroyed widget.
	if (USimCopterReplaySubsystem* Subsystem = Replay.Get())
	{
		Subsystem->OnStateChanged().Remove(StateChangedHandle);
	}
}

// ---------------------------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------------------------

TSharedRef<SWidget> SSimCopterReplayPanel::BuildTransportRow()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
		[
			MakePanelButton(
				LOCTEXT("Record", "● REC"),
				FOnClicked::CreateSP(this, &SSimCopterReplayPanel::HandleRecord),
				TAttribute<FSlateColor>::CreateLambda([this]()
				{
					const USimCopterReplaySubsystem* Subsystem = GetReplay();
					const bool bRecording = Subsystem != nullptr
						&& Subsystem->GetState() == ESimCopterReplayState::Recording;
					return FSlateColor(bRecording ? RecordButtonColor : ButtonColor);
				}),
				76.0f,
				TAttribute<bool>::CreateLambda([this]()
				{
					const USimCopterReplaySubsystem* Subsystem = GetReplay();
					return Subsystem != nullptr && Subsystem->CanStartRecording();
				}))
		]

		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
		[
			MakePanelButton(
				LOCTEXT("Stop", "■ STOP"),
				FOnClicked::CreateSP(this, &SSimCopterReplayPanel::HandleStop),
				FSlateColor(ButtonColor),
				76.0f,
				TAttribute<bool>::CreateLambda([this]()
				{
					const USimCopterReplaySubsystem* Subsystem = GetReplay();
					return Subsystem != nullptr && Subsystem->GetState() == ESimCopterReplayState::Recording;
				}))
		]

		// CLOSE ends a review and hands the world back, keeping the clip; RESET throws the clip
		// away. Two very different things, so they are never the same button.
		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
		[
			MakePanelButton(
				LOCTEXT("CloseClip", "CLOSE"),
				FOnClicked::CreateSP(this, &SSimCopterReplayPanel::HandleCloseClip),
				FSlateColor(ButtonColor),
				66.0f,
				TAttribute<bool>::CreateLambda([this]()
				{
					const USimCopterReplaySubsystem* Subsystem = GetReplay();
					return Subsystem != nullptr && Subsystem->GetState() == ESimCopterReplayState::Reviewing;
				}))
		]

		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
		[
			MakePanelButton(
				LOCTEXT("Review", "REVIEW"),
				FOnClicked::CreateSP(this, &SSimCopterReplayPanel::HandleReview),
				FSlateColor(ButtonColor),
				72.0f,
				TAttribute<bool>::CreateLambda([this]()
				{
					const USimCopterReplaySubsystem* Subsystem = GetReplay();
					return Subsystem != nullptr && Subsystem->CanReviewLoadedClip();
				}))
		]

		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 14.0f, 0.0f))
		[
			MakePanelButton(
				LOCTEXT("Reset", "RESET"),
				FOnClicked::CreateSP(this, &SSimCopterReplayPanel::HandleReset),
				FSlateColor(ButtonColor),
				66.0f)
		]

		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
		[
			MakePanelButton(
				LOCTEXT("GoToStart", "|◀"),
				FOnClicked::CreateSP(this, &SSimCopterReplayPanel::HandleGoToStart),
				FSlateColor(ButtonColor),
				40.0f,
				TAttribute<bool>::CreateLambda([this]()
				{
					const USimCopterReplaySubsystem* Subsystem = GetReplay();
					return Subsystem != nullptr && Subsystem->GetState() == ESimCopterReplayState::Reviewing;
				}))
		]

		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 14.0f, 0.0f))
		[
			MakePanelButton(
				TAttribute<FText>::CreateSP(this, &SSimCopterReplayPanel::GetPlayPauseText),
				FOnClicked::CreateSP(this, &SSimCopterReplayPanel::HandlePlayPause),
				TAttribute<FSlateColor>::CreateLambda([this]()
				{
					const USimCopterReplaySubsystem* Subsystem = GetReplay();
					return FSlateColor(Subsystem != nullptr && Subsystem->IsPlaying()
						? ActiveButtonColor
						: ButtonColor);
				}),
				52.0f,
				TAttribute<bool>::CreateLambda([this]()
				{
					const USimCopterReplaySubsystem* Subsystem = GetReplay();
					return Subsystem != nullptr && Subsystem->GetState() == ESimCopterReplayState::Reviewing;
				}))
		]

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(this, &SSimCopterReplayPanel::GetTimecodeText)
			.Font(PanelFont(12, true))
		];
}

TSharedRef<SWidget> SSimCopterReplayPanel::BuildViewRow()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SpeedLabel", "SPEED"))
			.Font(PanelFont(10, true))
			.ColorAndOpacity(FSlateColor(DimText))
		]

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
		[
			SNew(SBox)
			.WidthOverride(150.0f)
			[
				SNew(SSlider)
				.IsFocusable(false)
				// The slider runs 0..1 over the log of the speed range, so 1x sits in the middle of
				// the travel and the slow end - which is what a replay tool is used for - gets half
				// the bar instead of a fifth of it.
				.Value_Lambda([this]()
				{
					const USimCopterReplaySubsystem* Subsystem = GetReplay();
					if (Subsystem == nullptr)
					{
						return 0.5f;
					}
					const float LogMin = FMath::Loge(USimCopterReplaySubsystem::MinPlaybackSpeed);
					const float LogMax = FMath::Loge(USimCopterReplaySubsystem::MaxPlaybackSpeed);
					const float LogValue = FMath::Loge(Subsystem->GetPlaybackSpeed());
					return FMath::Clamp((LogValue - LogMin) / (LogMax - LogMin), 0.0f, 1.0f);
				})
				.OnValueChanged(this, &SSimCopterReplayPanel::HandleSpeedChanged)
			]
		]

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 14.0f, 0.0f))
		[
			SNew(SBox)
			.MinDesiredWidth(52.0f)
			[
				SNew(STextBlock)
				.Text(this, &SSimCopterReplayPanel::GetSpeedText)
				.Font(PanelFont(11, true))
			]
		]

		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
		[
			MakePanelButton(
				TAttribute<FText>::CreateSP(this, &SSimCopterReplayPanel::GetCameraText),
				FOnClicked::CreateSP(this, &SSimCopterReplayPanel::HandleCycleCamera),
				TAttribute<FSlateColor>::CreateLambda([this]()
				{
					const USimCopterReplaySubsystem* Subsystem = GetReplay();
					return FSlateColor(Subsystem != nullptr && Subsystem->IsFreeCameraActive()
						? ActiveButtonColor
						: ButtonColor);
				}),
				136.0f)
		]

		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
		[
			MakePanelButton(
				TAttribute<FText>::CreateSP(this, &SSimCopterReplayPanel::GetSmoothText),
				FOnClicked::CreateSP(this, &SSimCopterReplayPanel::HandleToggleSmooth),
				TAttribute<FSlateColor>::CreateLambda([this]()
				{
					const USimCopterReplaySubsystem* Subsystem = GetReplay();
					return FSlateColor(Subsystem != nullptr && Subsystem->IsSmoothCameraEnabled()
						? ActiveButtonColor
						: ButtonColor);
				}),
				126.0f)
		]

		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakePanelButton(
				TAttribute<FText>::CreateSP(this, &SSimCopterReplayPanel::GetHudText),
				FOnClicked::CreateSP(this, &SSimCopterReplayPanel::HandleToggleHud),
				TAttribute<FSlateColor>::CreateLambda([this]()
				{
					const USimCopterReplaySubsystem* Subsystem = GetReplay();
					return FSlateColor(Subsystem != nullptr && Subsystem->IsHudHidden()
						? ActiveButtonColor
						: ButtonColor);
				}),
				110.0f)
		];
}

TSharedRef<SWidget> SSimCopterReplayPanel::BuildClipRow()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ClipNameLabel", "NAME"))
			.Font(PanelFont(10, true))
			.ColorAndOpacity(FSlateColor(DimText))
		]

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
		[
			SNew(SBox)
			.WidthOverride(240.0f)
			[
				SAssignNew(ClipNameBox, SEditableTextBox)
				.Font(PanelFont(11))
				.HintText(LOCTEXT("ClipNameHint", "Name this clip"))
				.OnTextCommitted_Lambda([this](const FText&, const ETextCommit::Type CommitType)
				{
					// Enter in the name box saves, which is what everyone tries first.
					if (CommitType == ETextCommit::OnEnter)
					{
						HandleSave();
					}
					// The text box is the one thing in the panel that legitimately takes the
					// keyboard, so it has to hand it straight back - otherwise naming a clip while
					// a take is running leaves the player unable to fly.
					ReturnFocusToGame();
				})
			]
		]

		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
		[
			MakePanelButton(
				LOCTEXT("Save", "SAVE"),
				FOnClicked::CreateSP(this, &SSimCopterReplayPanel::HandleSave),
				FSlateColor(ButtonColor),
				66.0f,
				TAttribute<bool>::CreateLambda([this]()
				{
					const USimCopterReplaySubsystem* Subsystem = GetReplay();
					return Subsystem != nullptr && Subsystem->HasClip();
				}))
		]

		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 14.0f, 0.0f))
		[
			MakePanelButton(
				LOCTEXT("Load", "LOAD"),
				FOnClicked::CreateSP(this, &SSimCopterReplayPanel::HandleToggleClipList),
				TAttribute<FSlateColor>::CreateLambda([this]()
				{
					return FSlateColor(bClipListExpanded ? ActiveButtonColor : ButtonColor);
				}),
				66.0f)
		]

		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				// Clips belong to the level they were recorded in, and the panel says which one so
				// an empty Load list reads as "none here yet" rather than as a bug.
				USimCopterReplaySubsystem* Subsystem = GetReplay();
				if (Subsystem == nullptr)
				{
					return FText::GetEmpty();
				}
				return FText::Format(
					LOCTEXT("ClipsForCity", "Clips for {0}"),
					FText::FromString(Subsystem->GetLevelDisplayName()));
			})
			.Font(PanelFont(10))
			.ColorAndOpacity(FSlateColor(DimText))
		];
}

TSharedRef<SWidget> SSimCopterReplayPanel::BuildEventList()
{
	return SNew(SBox)
		.HeightOverride(112.0f)
		[
			SAssignNew(EventListBox, SScrollBox)
		];
}

// ---------------------------------------------------------------------------------------------
// Content
// ---------------------------------------------------------------------------------------------

void SSimCopterReplayPanel::RebuildTimelineMarkers()
{
	if (!Timeline.IsValid())
	{
		return;
	}

	TArray<FSimCopterReplayTimelineMarker> NewMarkers;
	const USimCopterReplaySubsystem* Subsystem = GetReplay();
	if (Subsystem == nullptr)
	{
		Timeline->SetMarkers(MoveTemp(NewMarkers));
		return;
	}

	const SimCopterReplay::FReplayClip& Clip = Subsystem->GetClip();
	if (Clip.Events.Num() == 0)
	{
		Timeline->SetMarkers(MoveTemp(NewMarkers));
		return;
	}

	// Mission messages and bookmarks always get a tick; the decision stream is thinned so a riot
	// cannot paint the bar solid.
	int32 DecisionCount = 0;
	for (const SimCopterReplay::FReplayEvent& Event : Clip.Events)
	{
		const bool bAlwaysShow =
			Event.Kind == SimCopterReplay::EReplayEventKind::MissionMessage
			|| Event.Kind == SimCopterReplay::EReplayEventKind::Bookmark;
		if (!bAlwaysShow)
		{
			++DecisionCount;
		}
	}
	const int32 Stride = FMath::Max(1, FMath::DivideAndRoundUp(DecisionCount, MaxTimelineMarkers));

	int32 DecisionIndex = 0;
	for (const SimCopterReplay::FReplayEvent& Event : Clip.Events)
	{
		const bool bAlwaysShow =
			Event.Kind == SimCopterReplay::EReplayEventKind::MissionMessage
			|| Event.Kind == SimCopterReplay::EReplayEventKind::Bookmark;
		if (!bAlwaysShow && (DecisionIndex++ % Stride) != 0)
		{
			continue;
		}

		FSimCopterReplayTimelineMarker& Marker = NewMarkers.AddDefaulted_GetRef();
		Marker.Seconds = Clip.FrameToTime(static_cast<float>(Event.FrameIndex));
		Marker.Color = MarkerColorForEvent(Event.Kind);
	}

	Timeline->SetMarkers(MoveTemp(NewMarkers));
}

void SSimCopterReplayPanel::RefreshEventList()
{
	if (!EventListBox.IsValid())
	{
		return;
	}
	EventListBox->ClearChildren();

	const USimCopterReplaySubsystem* Subsystem = GetReplay();
	if (Subsystem == nullptr)
	{
		return;
	}

	TArray<const SimCopterReplay::FReplayEvent*> Events;
	Subsystem->GetEventsAroundPlayhead(EventListLength, Events);
	if (Events.Num() == 0)
	{
		EventListBox->AddSlot()
		[
			SNew(STextBlock)
			.Text(LOCTEXT(
				"NoEvents",
				"Nothing recorded yet. Press REC and play; every decision the city's people make is logged here."))
			.Font(PanelFont(10))
			.ColorAndOpacity(FSlateColor(DimText))
			.AutoWrapText(true)
		];
		return;
	}

	const SimCopterReplay::FReplayClip& Clip = Subsystem->GetClip();
	for (const SimCopterReplay::FReplayEvent* Event : Events)
	{
		const FString Line = FString::Printf(
			TEXT("%s  %-8s  %s"),
			*FormatTimecode(Clip.FrameToTime(static_cast<float>(Event->FrameIndex))).ToString(),
			SimCopterReplay::GetEventKindName(Event->Kind),
			*Event->Text);

		EventListBox->AddSlot()
		[
			SNew(STextBlock)
			.Text(FText::FromString(Line))
			.Font(PanelFont(9))
			.ColorAndOpacity(FSlateColor(MarkerColorForEvent(Event->Kind)))
		];
	}
}

void SSimCopterReplayPanel::RebuildClipList()
{
	if (!ClipListBox.IsValid())
	{
		return;
	}
	ClipListBox->ClearChildren();

	USimCopterReplaySubsystem* Subsystem = GetReplay();
	if (Subsystem == nullptr)
	{
		return;
	}

	TArray<FSimCopterReplayClipSummary> Summaries;
	Subsystem->GetClipSummaries(Summaries);
	if (Summaries.Num() == 0)
	{
		ClipListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoClips", "No clips saved in this city yet."))
			.Font(PanelFont(10))
			.ColorAndOpacity(FSlateColor(DimText))
		];
		return;
	}

	for (const FSimCopterReplayClipSummary& Summary : Summaries)
	{
		const FString FileName = Summary.FileName;
		ClipListBox->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 1.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SButton)
				.IsFocusable(false)
				.HAlign(HAlign_Left)
				.ContentPadding(FMargin(8.0f, 3.0f))
				.ButtonColorAndOpacity(FSlateColor(ButtonColor))
				.OnClicked(FOnClicked::CreateSP(this, &SSimCopterReplayPanel::HandleLoadClip, FileName))
				[
					SNew(STextBlock)
					.Text(FText::Format(
						LOCTEXT("ClipRow", "{0}   —   {1}   —   {2} actors, {3} events"),
						FText::FromString(Summary.DisplayName),
						FormatTimecode(Summary.DurationSeconds),
						FText::AsNumber(Summary.TrackCount),
						FText::AsNumber(Summary.EventCount)))
					.Font(PanelFont(10))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			[
				MakePanelButton(
					LOCTEXT("DeleteClip", "✕"),
					FOnClicked::CreateSP(this, &SSimCopterReplayPanel::HandleDeleteClip, FileName),
					FSlateColor(ButtonColor),
					28.0f)
			]
		];
	}
}

void SSimCopterReplayPanel::HandleReplayStateChanged()
{
	RebuildTimelineMarkers();
	RefreshEventList();
	if (bClipListExpanded)
	{
		RebuildClipList();
	}
}

// ---------------------------------------------------------------------------------------------
// Readouts
// ---------------------------------------------------------------------------------------------

FText SSimCopterReplayPanel::FormatTimecode(const float Seconds)
{
	const float Clamped = FMath::Max(Seconds, 0.0f);
	const int32 Minutes = FMath::FloorToInt(Clamped / 60.0f);
	const float Remainder = Clamped - static_cast<float>(Minutes) * 60.0f;
	return FText::FromString(FString::Printf(TEXT("%d:%05.2f"), Minutes, Remainder));
}

FText SSimCopterReplayPanel::GetStateText() const
{
	const USimCopterReplaySubsystem* Subsystem = GetReplay();
	if (Subsystem == nullptr)
	{
		return FText::GetEmpty();
	}
	switch (Subsystem->GetState())
	{
	case ESimCopterReplayState::Recording:
		return LOCTEXT("StateRecording", "RECORDING — Tab to hide these controls and keep flying");
	case ESimCopterReplayState::Reviewing:
		return LOCTEXT("StateReviewing", "REVIEWING — Tab to watch it full screen, CLOSE to end it");
	default:
		return Subsystem->HasClip()
			? LOCTEXT("StateHasClip", "CLIP LOADED — REVIEW to watch it, SAVE to keep it")
			: LOCTEXT("StateIdle", "READY — press REC");
	}
}

FText SSimCopterReplayPanel::GetTimecodeText() const
{
	const USimCopterReplaySubsystem* Subsystem = GetReplay();
	if (Subsystem == nullptr)
	{
		return FText::GetEmpty();
	}
	if (Subsystem->GetState() == ESimCopterReplayState::Recording)
	{
		return FText::Format(LOCTEXT("RecTime", "{0}"), FormatTimecode(Subsystem->GetRecordedSeconds()));
	}
	return FText::Format(
		LOCTEXT("PlayTime", "{0} / {1}"),
		FormatTimecode(Subsystem->GetPlayheadSeconds()),
		FormatTimecode(Subsystem->GetClipDurationSeconds()));
}

FText SSimCopterReplayPanel::GetPlayPauseText() const
{
	const USimCopterReplaySubsystem* Subsystem = GetReplay();
	return (Subsystem != nullptr && Subsystem->IsPlaying())
		? LOCTEXT("Pause", "‖")
		: LOCTEXT("Play", "▶");
}

FText SSimCopterReplayPanel::GetSpeedText() const
{
	const USimCopterReplaySubsystem* Subsystem = GetReplay();
	const float Speed = Subsystem != nullptr ? Subsystem->GetPlaybackSpeed() : 1.0f;
	return FText::FromString(FString::Printf(TEXT("%.2fx"), Speed));
}

FText SSimCopterReplayPanel::GetCameraText() const
{
	const USimCopterReplaySubsystem* Subsystem = GetReplay();
	if (Subsystem == nullptr)
	{
		return FText::GetEmpty();
	}
	switch (Subsystem->GetCameraView())
	{
	case ESimCopterReplayCameraView::Orbit:   return LOCTEXT("CamOrbit", "CAM: ORBIT");
	case ESimCopterReplayCameraView::Rescue:  return LOCTEXT("CamRescue", "CAM: RESCUE");
	case ESimCopterReplayCameraView::Cockpit: return LOCTEXT("CamCockpit", "CAM: COCKPIT");
	case ESimCopterReplayCameraView::Free:
		return FText::Format(
			LOCTEXT("CamFree", "CAM: FREE {0}°"),
			FText::AsNumber(FMath::RoundToInt(Subsystem->GetFreeCameraFov())));
	default:                                  return LOCTEXT("CamChase", "CAM: CHASE");
	}
}

FText SSimCopterReplayPanel::GetSmoothText() const
{
	const USimCopterReplaySubsystem* Subsystem = GetReplay();
	return (Subsystem != nullptr && Subsystem->IsSmoothCameraEnabled())
		? LOCTEXT("SmoothOn", "SMOOTH: ON")
		: LOCTEXT("SmoothOff", "SMOOTH: OFF");
}

FText SSimCopterReplayPanel::GetHudText() const
{
	const USimCopterReplaySubsystem* Subsystem = GetReplay();
	return (Subsystem != nullptr && Subsystem->IsHudHidden())
		? LOCTEXT("HudHidden", "HUD: OFF")
		: LOCTEXT("HudShown", "HUD: ON");
}

FText SSimCopterReplayPanel::GetStatusText() const
{
	const USimCopterReplaySubsystem* Subsystem = GetReplay();
	if (Subsystem != nullptr && Subsystem->DidRecordingHitBudget())
	{
		return LOCTEXT(
			"BudgetHit",
			"Recording stopped: the take reached its memory budget (SimCopter.Replay.MemoryBudgetMB).");
	}
	return StatusMessage;
}

EVisibility SSimCopterReplayPanel::GetClipListVisibility() const
{
	return bClipListExpanded ? EVisibility::Visible : EVisibility::Collapsed;
}

// ---------------------------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------------------------

FReply SSimCopterReplayPanel::HandleRecord()
{
	if (USimCopterReplaySubsystem* Subsystem = GetReplay())
	{
		StatusMessage = FText::GetEmpty();
		Subsystem->StartRecording();
	}
	return FReply::Handled();
}

FReply SSimCopterReplayPanel::HandleStop()
{
	USimCopterReplaySubsystem* Subsystem = GetReplay();
	if (Subsystem == nullptr)
	{
		return FReply::Handled();
	}

	Subsystem->StopRecording();
	StatusMessage = LOCTEXT("StoppedNameIt", "Take finished. Click the name box, type a name, then SAVE.");
	// Deliberately does NOT focus the name box. Nothing in this panel grabs the keyboard on its own
	// - the player clicks into the box when they want to type, and it hands the keyboard straight
	// back when they are done. See SupportsKeyboardFocus.
	return FReply::Handled();
}

FReply SSimCopterReplayPanel::HandleReset()
{
	if (USimCopterReplaySubsystem* Subsystem = GetReplay())
	{
		Subsystem->ResetClip();
		StatusMessage = LOCTEXT("ClipDiscarded", "Clip discarded.");
		if (ClipNameBox.IsValid())
		{
			ClipNameBox->SetText(FText::GetEmpty());
		}
	}
	return FReply::Handled();
}

FReply SSimCopterReplayPanel::HandleCloseClip()
{
	// Leave the review first so the world is already running again by the time the panel goes; the
	// clip stays in memory, so SAVE and REVIEW both still work after this.
	if (USimCopterReplaySubsystem* Subsystem = GetReplay())
	{
		Subsystem->ExitReview();
	}
	OnRequestClose.ExecuteIfBound();
	return FReply::Handled();
}

FReply SSimCopterReplayPanel::HandleReview()
{
	if (USimCopterReplaySubsystem* Subsystem = GetReplay())
	{
		Subsystem->ReviewLoadedClip();
		StatusMessage = LOCTEXT("ReviewStarted", "Reviewing — the city is paused. CLOSE hands it back.");
	}
	return FReply::Handled();
}

FReply SSimCopterReplayPanel::HandlePlayPause()
{
	if (USimCopterReplaySubsystem* Subsystem = GetReplay())
	{
		Subsystem->TogglePlayPause();
	}
	return FReply::Handled();
}

FReply SSimCopterReplayPanel::HandleGoToStart()
{
	if (USimCopterReplaySubsystem* Subsystem = GetReplay())
	{
		Subsystem->GoToStart();
	}
	return FReply::Handled();
}

FReply SSimCopterReplayPanel::HandleCycleCamera()
{
	if (USimCopterReplaySubsystem* Subsystem = GetReplay())
	{
		Subsystem->CycleCameraView();
	}
	return FReply::Handled();
}

FReply SSimCopterReplayPanel::HandleToggleSmooth()
{
	if (USimCopterReplaySubsystem* Subsystem = GetReplay())
	{
		Subsystem->ToggleSmoothCamera();
	}
	return FReply::Handled();
}

FReply SSimCopterReplayPanel::HandleToggleHud()
{
	if (USimCopterReplaySubsystem* Subsystem = GetReplay())
	{
		Subsystem->ToggleHudHidden();
	}
	return FReply::Handled();
}

FReply SSimCopterReplayPanel::HandleSave()
{
	USimCopterReplaySubsystem* Subsystem = GetReplay();
	if (Subsystem == nullptr || !ClipNameBox.IsValid())
	{
		return FReply::Handled();
	}

	FString Error;
	if (Subsystem->SaveClip(ClipNameBox->GetText().ToString(), Error))
	{
		StatusMessage = FText::Format(
			LOCTEXT("ClipSaved", "Saved “{0}”."),
			FText::FromString(Subsystem->GetClip().Name));
		if (bClipListExpanded)
		{
			RebuildClipList();
		}
	}
	else
	{
		StatusMessage = FText::FromString(Error);
	}
	return FReply::Handled();
}

FReply SSimCopterReplayPanel::HandleToggleClipList()
{
	bClipListExpanded = !bClipListExpanded;
	if (bClipListExpanded)
	{
		RebuildClipList();
	}
	return FReply::Handled();
}

FReply SSimCopterReplayPanel::HandleLoadClip(FString FileName)
{
	USimCopterReplaySubsystem* Subsystem = GetReplay();
	if (Subsystem == nullptr)
	{
		return FReply::Handled();
	}

	FString Error;
	if (Subsystem->LoadClip(FileName, Error))
	{
		bClipListExpanded = false;
		StatusMessage = FText::Format(
			LOCTEXT("ClipLoaded", "Loaded “{0}”."),
			FText::FromString(Subsystem->GetClip().Name));
		if (ClipNameBox.IsValid())
		{
			ClipNameBox->SetText(FText::FromString(Subsystem->GetClip().Name));
		}
	}
	else
	{
		StatusMessage = FText::FromString(Error);
	}
	return FReply::Handled();
}

FReply SSimCopterReplayPanel::HandleDeleteClip(FString FileName)
{
	USimCopterReplaySubsystem* Subsystem = GetReplay();
	if (Subsystem == nullptr)
	{
		return FReply::Handled();
	}

	FString Error;
	if (Subsystem->DeleteClip(FileName, Error))
	{
		StatusMessage = LOCTEXT("ClipDeleted", "Clip deleted.");
	}
	else
	{
		StatusMessage = FText::FromString(Error);
	}
	RebuildClipList();
	return FReply::Handled();
}

void SSimCopterReplayPanel::HandleScrub(const float Seconds)
{
	if (USimCopterReplaySubsystem* Subsystem = GetReplay())
	{
		Subsystem->SetPlayheadSeconds(Seconds);
	}
}

void SSimCopterReplayPanel::HandleScrubBegin()
{
	USimCopterReplaySubsystem* Subsystem = GetReplay();
	if (Subsystem == nullptr)
	{
		return;
	}
	// Playback and a drag would fight over the playhead every frame. Suspend for the drag and put
	// it back afterwards, so scrubbing out of a running clip does not silently stop it.
	bResumeAfterScrub = Subsystem->IsPlaying();
	Subsystem->Pause();
}

void SSimCopterReplayPanel::HandleScrubEnd()
{
	USimCopterReplaySubsystem* Subsystem = GetReplay();
	if (Subsystem != nullptr && bResumeAfterScrub)
	{
		Subsystem->Play();
	}
	bResumeAfterScrub = false;
}

void SSimCopterReplayPanel::HandleSpeedChanged(const float Value)
{
	USimCopterReplaySubsystem* Subsystem = GetReplay();
	if (Subsystem == nullptr)
	{
		return;
	}
	const float LogMin = FMath::Loge(USimCopterReplaySubsystem::MinPlaybackSpeed);
	const float LogMax = FMath::Loge(USimCopterReplaySubsystem::MaxPlaybackSpeed);
	Subsystem->SetPlaybackSpeed(FMath::Exp(FMath::Lerp(LogMin, LogMax, FMath::Clamp(Value, 0.0f, 1.0f))));
}

// There is deliberately NO OnKeyDown here, and no SupportsKeyboardFocus. Every shortcut the panel
// needs - Tab, C, H, Space, the arrows, Home, M - is an ordinary input binding on
// ASimCopterPlayerController, which works without the panel ever holding keyboard focus. See the
// comment on SupportsKeyboardFocus for why that matters: a focus-taking widget over the viewport
// stops the player flying.

#undef LOCTEXT_NAMESPACE
