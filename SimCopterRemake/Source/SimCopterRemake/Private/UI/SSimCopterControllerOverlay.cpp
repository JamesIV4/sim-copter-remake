// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SSimCopterControllerOverlay.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Flight/SimCopterHelicopterPawn.h"
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
constexpr float WheelSize = 650.0f;
constexpr float WheelCentre = WheelSize * 0.5f;
constexpr float WheelRadius = 220.0f;
constexpr float EntryWidth = 174.0f;
constexpr float EntryHeight = 54.0f;

const FLinearColor BackdropColor(0.015f, 0.025f, 0.045f, 0.86f);
const FLinearColor EntryColor(0.035f, 0.075f, 0.12f, 0.96f);
const FLinearColor SelectedColor(0.95f, 0.57f, 0.12f, 0.98f);
const FLinearColor TextColor(0.92f, 0.96f, 1.0f, 1.0f);
const FLinearColor SelectedTextColor(0.02f, 0.025f, 0.035f, 1.0f);

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
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SAssignNew(DispatchWheelHost, SBox)
				.Visibility(this, &SSimCopterControllerOverlay::GetDispatchWheelVisibility)
				.WidthOverride(WheelSize)
				.HeightOverride(WheelSize)
		]

		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SAssignNew(ToolWheelHost, SBox)
				.Visibility(this, &SSimCopterControllerOverlay::GetToolWheelVisibility)
				.WidthOverride(WheelSize)
				.HeightOverride(WheelSize)
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

TSharedRef<SWidget> SSimCopterControllerOverlay::BuildDispatchWheel() const
{
	TArray<FString> Labels;
	Labels.Add(TEXT("FIRE TRUCK"));
	Labels.Add(TEXT("POLICE"));
	Labels.Add(TEXT("AMBULANCE"));
	return BuildRadialWheel(
		Labels,
		NSLOCTEXT("SimCopterController", "DispatchWheel", "DISPATCH"),
		NSLOCTEXT(
			"SimCopterController",
			"DispatchInstructions",
			"RS  SELECT\nA  DISPATCH     X  CHASE     B  CLEAR ALL\nRELEASE LB  CLOSE"));
}

TSharedRef<SWidget> SSimCopterControllerOverlay::BuildToolWheel() const
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
			"RS  SELECT\nRELEASE LT  EQUIP     B  CANCEL"));
}

TSharedRef<SWidget> SSimCopterControllerOverlay::BuildRadialWheel(
	const TArray<FString>& Labels,
	const FText& Title,
	const FText& Instructions) const
{
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	Canvas->AddSlot()
		.Offset(FMargin(90.0f, 90.0f, WheelSize - 180.0f, WheelSize - 180.0f))
		[
			SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(BackdropColor)
		];

	const TWeakObjectPtr<ASimCopterHelicopterPawn> WeakPawn = Pawn;
	for (int32 Index = 0; Index < Labels.Num(); ++Index)
	{
		const float Angle = -0.5f * UE_PI +
			2.0f * UE_PI * static_cast<float>(Index) / static_cast<float>(Labels.Num());
		const float X = WheelCentre + FMath::Cos(Angle) * WheelRadius - EntryWidth * 0.5f;
		const float Y = WheelCentre + FMath::Sin(Angle) * WheelRadius - EntryHeight * 0.5f;

		Canvas->AddSlot()
			.Offset(FMargin(X, Y, EntryWidth, EntryHeight))
			[
				SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
					.BorderBackgroundColor_Lambda([WeakPawn, Index]()
					{
						const ASimCopterHelicopterPawn* Helicopter = WeakPawn.Get();
						return Helicopter != nullptr && Helicopter->GetControllerRadialIndex() == Index
							? SelectedColor
							: EntryColor;
					})
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(FMargin(8.0f, 5.0f))
					[
						SNew(STextBlock)
							.Text(FText::FromString(Labels[Index]))
							.Justification(ETextJustify::Center)
							.ColorAndOpacity_Lambda([WeakPawn, Index]()
							{
								const ASimCopterHelicopterPawn* Helicopter = WeakPawn.Get();
								return Helicopter != nullptr && Helicopter->GetControllerRadialIndex() == Index
									? SelectedTextColor
									: TextColor;
							})
							.Font(ControllerFont(12, true))
					]
			];
	}

	Canvas->AddSlot()
		.Offset(FMargin(WheelCentre - 150.0f, WheelCentre - 70.0f, 300.0f, 140.0f))
		[
			SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(FLinearColor(0.01f, 0.02f, 0.035f, 0.98f))
				.Padding(FMargin(12.0f, 10.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock)
							.Text(Title)
							.Justification(ETextJustify::Center)
							.ColorAndOpacity(FLinearColor(1.0f, 0.70f, 0.25f, 1.0f))
							.Font(ControllerFont(20, true))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
					[
						SNew(STextBlock)
							.Text(Instructions)
							.Justification(ETextJustify::Center)
							.ColorAndOpacity(TextColor)
							.Font(ControllerFont(11, true))
					]
				]
		];

	return Canvas;
}

EVisibility SSimCopterControllerOverlay::GetDispatchWheelVisibility() const
{
	const ASimCopterHelicopterPawn* Helicopter = Pawn.Get();
	return Helicopter != nullptr &&
		Helicopter->GetControllerMode() == ESimCopterControllerMode::DispatchWheel
			? EVisibility::Visible
			: EVisibility::Collapsed;
}

EVisibility SSimCopterControllerOverlay::GetToolWheelVisibility() const
{
	const ASimCopterHelicopterPawn* Helicopter = Pawn.Get();
	return Helicopter != nullptr &&
		Helicopter->GetControllerMode() == ESimCopterControllerMode::ToolWheel
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
