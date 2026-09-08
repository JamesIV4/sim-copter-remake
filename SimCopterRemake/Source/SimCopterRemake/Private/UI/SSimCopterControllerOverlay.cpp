// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SSimCopterControllerOverlay.h"
#include "UI/SSimCopterRadialWheel.h"
#include "Widgets/Layout/SScaleBox.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Flight/SimCopterControllerInput.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const FLinearColor BackdropColor(0.015f, 0.025f, 0.045f, 0.86f);
const FLinearColor TextColor(0.92f, 0.96f, 1.0f, 1.0f);

FSlateFontInfo ControllerFont(const int32 Size, const bool bBold = false)
{
	return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
}

const TCHAR* PassengerKindName(const ESimCopterMissionPassengerKind Kind)
{
	switch (Kind)
	{
	case ESimCopterMissionPassengerKind::Medevac: return TEXT("MEDEVAC");
	case ESimCopterMissionPassengerKind::Rescue: return TEXT("RESCUE");
	default: return TEXT("TRANSPORT");
	}
}
}

void SSimCopterControllerOverlay::Construct(const FArguments& InArgs)
{
	Pawn = InArgs._Pawn;

	ChildSlot
	[
		SNew(SOverlay)

		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SAssignNew(PassengerPanel, SBorder)
			.Visibility(this, &SSimCopterControllerOverlay::GetPauseVisibility)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.58f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SSimCopterControllerOverlay::GetPauseText)
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(TextColor)
				.Font(ControllerFont(28, true))
			]
		]

		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(24.0f)
		[
			SAssignNew(DispatchWheelHost, SBox)
				.Visibility(this, &SSimCopterControllerOverlay::GetDispatchWheelVisibility)
		]

		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(24.0f)
		[
			SAssignNew(ToolWheelHost, SBox)
				.Visibility(this, &SSimCopterControllerOverlay::GetToolWheelVisibility)
		]

		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(FMargin(0.0f, 54.0f, 0.0f, 0.0f))
		[
			SNew(SBorder)
				.Visibility(this, &SSimCopterControllerOverlay::GetPassengerVisibility)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(BackdropColor)
				.Padding(FMargin(24.0f, 14.0f))
				[
					SNew(SBox)
					.MinDesiredWidth(430.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(STextBlock)
								.Text(this, &SSimCopterControllerOverlay::GetPassengerTitle)
								.Justification(ETextJustify::Center)
								.ColorAndOpacity(FLinearColor(1.0f, 0.70f, 0.25f, 1.0f))
								.Font(ControllerFont(19, true))
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
						[
							SNew(STextBlock)
								.Text(this, &SSimCopterControllerOverlay::GetPassengerBody)
								.Justification(ETextJustify::Center)
								.ColorAndOpacity(TextColor)
								.Font(ControllerFont(13, true))
						]
					]
				]
		]
	];

	RefreshRadials();
	SetVisibility(EVisibility::HitTestInvisible);
}

void SSimCopterControllerOverlay::AppendMissionMarkerAvoidanceWidgets(TArray<TSharedPtr<SWidget>>& OutWidgets) const
{
	const auto AddIfVisible = [&OutWidgets](const TSharedPtr<SWidget>& Widget)
	{
		if (Widget.IsValid() && Widget->GetVisibility().IsVisible())
		{
			OutWidgets.Add(Widget);
		}
	};
	AddIfVisible(DispatchWheelHost);
	AddIfVisible(ToolWheelHost);
	AddIfVisible(PassengerPanel);
}

void SSimCopterControllerOverlay::RefreshRadials()
{
	if (DispatchWheelHost.IsValid())
	{
		DispatchWheelHost->SetContent(BuildDispatchWheel());
	}
	if (ToolWheelHost.IsValid())
	{
		ToolWheelHost->SetContent(BuildToolWheel());
	}
}

TSharedRef<SWidget> SSimCopterControllerOverlay::BuildDispatchWheel()
{
	TArray<FString> Labels;
	for (int32 Slot = 0; Slot < SimCopterControllerInput::DispatchSlotCount; ++Slot)
		Labels.Add(SimCopterControllerInput::GetDispatchSelection(Slot).Label);
	return BuildRadialWheel(
		Labels,
		NSLOCTEXT("SimCopterController", "DispatchWheel", "DISPATCH"),
		NSLOCTEXT(
			"SimCopterController",
			"DispatchInstructions",
			"RELEASE RB  DISPATCH\nB  CANCEL     Y  RECALL ALL"), DispatchWheel);
}

TSharedRef<SWidget> SSimCopterControllerOverlay::BuildToolWheel()
{
	TArray<FString> Labels;
	if (const ASimCopterHelicopterPawn* Helicopter = Pawn.Get())
	{
		for (const ESimCopterHelicopterTool Tool : Helicopter->GetControllerToolWheelTools())
		{
			Labels.Add(SimCopterHelicopterRegistry::GetToolDisplayName(Tool));
		}
	}

	if (Labels.Num() == 0)
	{
		Labels.Add(TEXT("NO TOOLS INSTALLED"));
	}

	return BuildRadialWheel(
		Labels,
		NSLOCTEXT("SimCopterController", "ToolWheel", "SELECT TOOL"),
		NSLOCTEXT(
			"SimCopterController",
			"ToolInstructions",
			"RELEASE LB  EQUIP     B  CANCEL\nX  PASSENGERS"), ToolWheel);
}

TSharedRef<SWidget> SSimCopterControllerOverlay::BuildRadialWheel(
	const TArray<FString>& Labels,
	const FText& Title,
	const FText& Instructions, TSharedPtr<SSimCopterRadialWheel>& Wheel)
{
	const TWeakObjectPtr<ASimCopterHelicopterPawn> WeakPawn = Pawn;
	return SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
	[
		SNew(SBox).WidthOverride(560).HeightOverride(600)
		[
			SAssignNew(Wheel, SSimCopterRadialWheel).Labels(Labels).Title(Title).Instructions(Instructions)
			.SelectedIndex_Lambda([WeakPawn]()
			{
				const ASimCopterHelicopterPawn* Helicopter = WeakPawn.Get();
				return Helicopter ? Helicopter->GetControllerRadialIndex() : INDEX_NONE;
			})
		]
	];
}

void SSimCopterControllerOverlay::ReleaseRadial(bool bDispatch, int32 ActivatedIndex)
{
	const auto& Wheel = bDispatch ? DispatchWheel : ToolWheel;
	if (Wheel.IsValid()) Wheel->BeginRelease(ActivatedIndex);
}

EVisibility SSimCopterControllerOverlay::GetDispatchWheelVisibility() const
{
	const ASimCopterHelicopterPawn* Helicopter = Pawn.Get();
	return Helicopter != nullptr &&
		(Helicopter->GetControllerMode() == ESimCopterControllerMode::DispatchWheel ||
			(DispatchWheel.IsValid() && DispatchWheel->IsReleaseVisible()))
			? EVisibility::Visible
			: EVisibility::Collapsed;
}

EVisibility SSimCopterControllerOverlay::GetToolWheelVisibility() const
{
	const ASimCopterHelicopterPawn* Helicopter = Pawn.Get();
	return Helicopter != nullptr &&
		(Helicopter->GetControllerMode() == ESimCopterControllerMode::ToolWheel ||
			(ToolWheel.IsValid() && ToolWheel->IsReleaseVisible()))
			? EVisibility::Visible
			: EVisibility::Collapsed;
}

EVisibility SSimCopterControllerOverlay::GetPassengerVisibility() const
{
	const ASimCopterHelicopterPawn* Helicopter = Pawn.Get();
	if (Helicopter == nullptr)
	{
		return EVisibility::Collapsed;
	}
	const ESimCopterControllerMode Mode = Helicopter->GetControllerMode();
	return Mode == ESimCopterControllerMode::PassengerSelect ||
		Mode == ESimCopterControllerMode::PassengerConfirm
			? EVisibility::Visible
			: EVisibility::Collapsed;
}

EVisibility SSimCopterControllerOverlay::GetPauseVisibility() const
{
	const UWorld* World =
		GEngine != nullptr && GEngine->GameViewport != nullptr
			? GEngine->GameViewport->GetWorld()
			: nullptr;
	return World != nullptr && World->IsPaused()
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

FText SSimCopterControllerOverlay::GetPassengerTitle() const
{
	const ASimCopterHelicopterPawn* Helicopter = Pawn.Get();
	return Helicopter != nullptr &&
		Helicopter->GetControllerMode() == ESimCopterControllerMode::PassengerConfirm
			? NSLOCTEXT("SimCopterController", "PassengerAction", "PASSENGER ACTION")
			: NSLOCTEXT("SimCopterController", "PassengerSelect", "SELECT PASSENGER");
}

FText SSimCopterControllerOverlay::GetPassengerBody() const
{
	const ASimCopterHelicopterPawn* Helicopter = Pawn.Get();
	if (Helicopter == nullptr)
	{
		return FText::GetEmpty();
	}

	const TArray<FSimCopterMissionPassengerSlot>& Slots = Helicopter->GetMissionPassengerSlots();
	const int32 SelectedSlot = Helicopter->GetControllerPassengerSlot();
	if (!Slots.IsValidIndex(SelectedSlot))
	{
		return NSLOCTEXT(
			"SimCopterController",
			"NoPassengers",
			"NO PASSENGERS\nB / X  EXIT");
	}

	const FString PassengerLine = FString::Printf(
		TEXT("%s PASSENGER  %d / %d"),
		PassengerKindName(Slots[SelectedSlot].Kind),
		SelectedSlot + 1,
		Slots.Num());

	if (Helicopter->GetControllerMode() == ESimCopterControllerMode::PassengerConfirm)
	{
		const bool bDropSelected = Helicopter->GetControllerPassengerConfirmChoice() == 0;
		return FText::FromString(FString::Printf(
			TEXT("%s\n\n%s A  DROP     %s A  CANCEL\nB  BACK"),
			*PassengerLine,
			bDropSelected ? TEXT(">") : TEXT(" "),
			bDropSelected ? TEXT(" ") : TEXT(">")));
	}

	return FText::FromString(FString::Printf(
		TEXT("%s\nDPAD LEFT / RIGHT  SELECT     A  ACTION\nB / X  EXIT"),
		*PassengerLine));
}

FText SSimCopterControllerOverlay::GetPauseText() const
{
	return NSLOCTEXT("SimCopterController", "Paused", "PAUSED\n\nSTART  RESUME");
}
