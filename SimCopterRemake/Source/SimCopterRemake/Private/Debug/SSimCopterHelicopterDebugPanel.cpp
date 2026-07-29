// Copyright Epic Games, Inc. All Rights Reserved.

#include "Debug/SSimCopterHelicopterDebugPanel.h"

#include "Flight/SimCopterHelicopterPawn.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Ground/SimCopterDispatch.h"
#include "Missions/SimCopterMissionSystem.h"
#include "Styling/CoreStyle.h"
#include "UI/SimCopterMissionCatalog.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FSlateFontInfo PanelFont(int32 Size, bool bBold = false)
{
	return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
}

TSharedRef<SWidget> MakeArrow(const FText& Label, FOnClicked OnClicked)
{
	return SNew(SBox)
		.WidthOverride(26.0f)
		[
			SNew(SButton)
			// Never take keyboard focus: this panel sits over the flying helicopter, and a
			// focused button swallows the space bar the player is holding for collective.
			.IsFocusable(false)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ContentPadding(FMargin(2.0f, 0.0f))
			.OnClicked(OnClicked)
			[
				SNew(STextBlock).Text(Label).Font(PanelFont(11, true))
			]
		];
}
}

void SSimCopterHelicopterDebugPanel::Construct(const FArguments& InArgs)
{
	Pawn = InArgs._Pawn;

	const FLinearColor LabelColor(0.62f, 0.72f, 0.82f, 1.0f);
	const FLinearColor ValueColor(0.94f, 0.97f, 1.0f, 1.0f);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FLinearColor(0.02f, 0.03f, 0.05f, 0.82f))
		.Padding(FMargin(10.0f, 8.0f))
		[
			SNew(SBox)
			.WidthOverride(410.0f)
			[
				SNew(SVerticalBox)

				// --- HELICOPTER ---
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "Helicopter", "HELICOPTER"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(10, true))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						MakeArrow(
							NSLOCTEXT("SimCopterDebug", "Prev", "<"),
							FOnClicked::CreateSP(this, &SSimCopterHelicopterDebugPanel::HandleModelPrev))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(FMargin(6.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(this, &SSimCopterHelicopterDebugPanel::GetModelLineText)
						.ColorAndOpacity(ValueColor)
						.Font(PanelFont(12, true))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						MakeArrow(
							NSLOCTEXT("SimCopterDebug", "Next", ">"),
							FOnClicked::CreateSP(this, &SSimCopterHelicopterDebugPanel::HandleModelNext))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(86.0f, 1.0f, 0.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(this, &SSimCopterHelicopterDebugPanel::GetModelDetailText)
					.ColorAndOpacity(LabelColor)
					.Font(PanelFont(10))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(86.0f, 1.0f, 0.0f, 6.0f))
				[
					SNew(STextBlock)
					.Text(this, &SSimCopterHelicopterDebugPanel::GetModelStatusText)
					.ColorAndOpacity(LabelColor)
					.AutoWrapText(true)
					.Font(PanelFont(10))
				]

				// --- CAMERA: persistent offsets for the currently active view ---
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "Camera", "CAMERA"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(10, true))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SSimCopterHelicopterDebugPanel::GetCameraModeText)
						.ColorAndOpacity(ValueColor)
						.Font(PanelFont(12, true))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SButton)
						.IsFocusable(false)
						.ContentPadding(FMargin(7.0f, 1.0f))
						.ToolTipText(NSLOCTEXT(
							"SimCopterDebug",
							"ResetCameraOffsetTip",
							"Restore the default translation, rotation, and zoom framing "
							"and maximum zoom for the current camera view."))
						.OnClicked(FOnClicked::CreateSP(
							this,
							&SSimCopterHelicopterDebugPanel::HandleResetCameraOffset))
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "ResetCameraOffset", "RESET"))
							.Font(PanelFont(10))
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 0.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"CameraTranslationTip",
						"Helicopter-local centimetres: X forward, Y right, Z up."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "CameraPosition", "POSITION CM"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(9, true))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("X"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f, 8.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(-10000.0f)
						.MaxValue(10000.0f)
						.MinSliderValue(-1000.0f)
						.MaxSliderValue(1000.0f)
						.Delta(1.0f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCameraTranslationX)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleCameraTranslationXChanged)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Y"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f, 8.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(-10000.0f)
						.MaxValue(10000.0f)
						.MinSliderValue(-1000.0f)
						.MaxSliderValue(1000.0f)
						.Delta(1.0f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCameraTranslationY)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleCameraTranslationYChanged)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Z"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(-10000.0f)
						.MaxValue(10000.0f)
						.MinSliderValue(-1000.0f)
						.MaxSliderValue(1000.0f)
						.Delta(1.0f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCameraTranslationZ)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleCameraTranslationZChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 2.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"CameraRotationTip",
						"Relative camera rotation in degrees: pitch, yaw, roll."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "CameraRotation", "ROTATION DEG"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(9, true))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("P"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f, 8.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(-180.0f)
						.MaxValue(180.0f)
						.MinSliderValue(-180.0f)
						.MaxSliderValue(180.0f)
						.Delta(0.5f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCameraRotationPitch)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleCameraRotationPitchChanged)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Y"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f, 8.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(-180.0f)
						.MaxValue(180.0f)
						.MinSliderValue(-180.0f)
						.MaxSliderValue(180.0f)
						.Delta(0.5f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCameraRotationYaw)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleCameraRotationYawChanged)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("R"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(-180.0f)
						.MaxValue(180.0f)
						.MinSliderValue(-180.0f)
						.MaxSliderValue(180.0f)
						.Delta(0.5f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCameraRotationRoll)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleCameraRotationRollChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 2.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"CameraZoomVerticalFramingTip",
						"Scales the camera framing offset with zoom distance so the helicopter "
						"stays at the same vertical screen position during zoom and right-drag "
						"camera movement. 0 disables; 1 is full correction."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "CameraZoomVerticalFraming", "ZOOM V-LOCK"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(9, true))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(0.0f)
						.MaxValue(2.0f)
						.MinSliderValue(0.0f)
						.MaxSliderValue(2.0f)
						.Delta(0.05f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(2)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCameraZoomVerticalFramingStrength)
						.OnValueChanged(
							this,
							&SSimCopterHelicopterDebugPanel::HandleCameraZoomVerticalFramingStrengthChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"CameraMaxZoomDistanceTip",
						"Maximum zoom-out distance for the current camera view, in centimetres."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "CameraMaxZoomDistance", "MAX ZOOM CM"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(9, true))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(100.0f)
						.MaxValue(10000.0f)
						.MinSliderValue(900.0f)
						.MaxSliderValue(5000.0f)
						.Delta(25.0f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(0)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCameraMaxZoomDistance)
						.OnValueChanged(
							this,
							&SSimCopterHelicopterDebugPanel::HandleCameraMaxZoomDistanceChanged)
					]
				]

				// --- TOOL ---
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "Tool", "TOOL"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(10, true))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						MakeArrow(
							NSLOCTEXT("SimCopterDebug", "Prev", "<"),
							FOnClicked::CreateSP(this, &SSimCopterHelicopterDebugPanel::HandleToolPrev))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(FMargin(6.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(this, &SSimCopterHelicopterDebugPanel::GetToolLineText)
						.ColorAndOpacity(ValueColor)
						.Font(PanelFont(12, true))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						MakeArrow(
							NSLOCTEXT("SimCopterDebug", "Next", ">"),
							FOnClicked::CreateSP(this, &SSimCopterHelicopterDebugPanel::HandleToolNext))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
					[
						SNew(SButton)
						.IsFocusable(false)
						.ContentPadding(FMargin(8.0f, 1.0f))
						.OnPressed(FSimpleDelegate::CreateLambda([this]() { HandleUsePressed(); }))
						.OnReleased(FSimpleDelegate::CreateLambda([this]() { HandleUseReleased(); }))
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "Use", "USE"))
							.Font(PanelFont(11, true))
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(86.0f, 2.0f, 0.0f, 0.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SSimCopterHelicopterDebugPanel::GetToolAvailabilityText)
						.ColorAndOpacity(LabelColor)
						.Font(PanelFont(10, true))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
					[
						SNew(SButton)
						.IsFocusable(false)
						.ContentPadding(FMargin(6.0f, 0.0f))
						.Visibility(this, &SSimCopterHelicopterDebugPanel::GetGrantButtonVisibility)
						.OnClicked(FOnClicked::CreateSP(this, &SSimCopterHelicopterDebugPanel::HandleToggleGrant))
						[
							SNew(STextBlock)
							.Text(this, &SSimCopterHelicopterDebugPanel::GetGrantButtonText)
							.Font(PanelFont(10))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
					[
						SNew(SButton)
						.IsFocusable(false)
						.ContentPadding(FMargin(6.0f, 0.0f))
						.OnClicked(FOnClicked::CreateSP(this, &SSimCopterHelicopterDebugPanel::HandleRopeToggle))
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "Rope", "ROPE"))
							.Font(PanelFont(10))
						]
					]
				]

				// --- context row: megaphone message ---
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(86.0f, 2.0f, 0.0f, 0.0f))
				[
					SNew(SHorizontalBox)
					.Visibility(this, &SSimCopterHelicopterDebugPanel::GetMegaphoneRowVisibility)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						MakeArrow(
							NSLOCTEXT("SimCopterDebug", "Prev", "<"),
							FOnClicked::CreateSP(this, &SSimCopterHelicopterDebugPanel::HandleMessagePrev))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(6.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(this, &SSimCopterHelicopterDebugPanel::GetToolContextText)
						.ColorAndOpacity(ValueColor)
						.Font(PanelFont(11))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						MakeArrow(
							NSLOCTEXT("SimCopterDebug", "Next", ">"),
							FOnClicked::CreateSP(this, &SSimCopterHelicopterDebugPanel::HandleMessageNext))
					]
				]

				// --- context row: tear gas ammo ---
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(86.0f, 2.0f, 0.0f, 0.0f))
				[
					SNew(SHorizontalBox)
					.Visibility(this, &SSimCopterHelicopterDebugPanel::GetTearGasRowVisibility)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SSimCopterHelicopterDebugPanel::GetToolContextText)
						.ColorAndOpacity(ValueColor)
						.Font(PanelFont(11))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
					[
						SNew(SButton)
						.IsFocusable(false)
						.ContentPadding(FMargin(6.0f, 0.0f))
						.OnClicked(FOnClicked::CreateSP(this, &SSimCopterHelicopterDebugPanel::HandleRefillTearGas))
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "Refill", "REFILL DEBUG"))
							.Font(PanelFont(10))
						]
					]
				]

				// --- DISPATCH (original F2-F5; the buttons drive the same pawn path) ---
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "Dispatch", "DISPATCH"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(10, true))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						MakeArrow(
							NSLOCTEXT("SimCopterDebug", "Prev", "<"),
							FOnClicked::CreateSP(this, &SSimCopterHelicopterDebugPanel::HandleDispatchServicePrev))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(FMargin(6.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(this, &SSimCopterHelicopterDebugPanel::GetDispatchServiceText)
						.ColorAndOpacity(ValueColor)
						.Font(PanelFont(12, true))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						MakeArrow(
							NSLOCTEXT("SimCopterDebug", "Next", ">"),
							FOnClicked::CreateSP(this, &SSimCopterHelicopterDebugPanel::HandleDispatchServiceNext))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(86.0f, 2.0f, 0.0f, 0.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SButton)
						.IsFocusable(false)
						.ContentPadding(FMargin(8.0f, 1.0f))
						.OnClicked(FOnClicked::CreateSP(this, &SSimCopterHelicopterDebugPanel::HandleDispatch))
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "DispatchGo", "DISPATCH"))
							.Font(PanelFont(10, true))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
					[
						SNew(SButton)
						.IsFocusable(false)
						.ContentPadding(FMargin(8.0f, 1.0f))
						.OnClicked(FOnClicked::CreateSP(this, &SSimCopterHelicopterDebugPanel::HandleDispatchChase))
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "DispatchChase", "CHASE"))
							.Font(PanelFont(10))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
					[
						SNew(SButton)
						.IsFocusable(false)
						.ContentPadding(FMargin(8.0f, 1.0f))
						.OnClicked(FOnClicked::CreateSP(this, &SSimCopterHelicopterDebugPanel::HandleDispatchClear))
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "DispatchClear", "CLEAR"))
							.Font(PanelFont(10))
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(86.0f, 2.0f, 0.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(this, &SSimCopterHelicopterDebugPanel::GetDispatchStatusText)
					.ColorAndOpacity(LabelColor)
					.AutoWrapText(true)
					.Font(PanelFont(10))
				]

				// --- MISSIONS: one button per mask the placer can dispatch ---
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(NSLOCTEXT("SimCopterDebug", "Missions", "MISSIONS"))
					.ColorAndOpacity(LabelColor)
					.Font(PanelFont(10, true))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 3.0f, 0.0f, 0.0f))
				[
					BuildMissionButtons()
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 3.0f, 0.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(this, &SSimCopterHelicopterDebugPanel::GetMissionStatusText)
					.ColorAndOpacity(LabelColor)
					.AutoWrapText(true)
					.Font(PanelFont(10))
				]
			]
		]
	];
}

TSharedRef<SWidget> SSimCopterHelicopterDebugPanel::BuildMissionButtons()
{
	const TArrayView<const FSimCopterMissionCatalogEntry> Missions = GetSimCopterMissionCatalog();

	TSharedRef<SWrapBox> Box = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(3.0f, 3.0f));
	for (const FSimCopterMissionCatalogEntry& Entry : Missions)
	{
		// A type whose world hook is still a stub is shown greyed and still clickable, so the
		// panel reports the same "no suitable tile" the placer does rather than hiding it.
		const FLinearColor TextColor = Entry.bWorldHookPorted
			? FLinearColor(0.94f, 0.97f, 1.0f, 1.0f)
			: FLinearColor(0.62f, 0.62f, 0.62f, 1.0f);

		Box->AddSlot()
		[
			SNew(SButton)
			.IsFocusable(false)
			.ContentPadding(FMargin(6.0f, 1.0f))
			.ToolTipText(FText::FromString(FString::Printf(
				TEXT("mask 0x%x   %s bucket   %s%s"),
				Entry.TypeMask,
				Entry.Bucket,
				Entry.Note,
				Entry.bWorldHookPorted ? TEXT("") : TEXT("  (world hook not ported)"))))
			.OnClicked(FOnClicked::CreateSP(this, &SSimCopterHelicopterDebugPanel::HandleStartMission, Entry.TypeMask))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Entry.Label))
				.ColorAndOpacity(TextColor)
				.Font(PanelFont(10))
			]
		];
	}

	return Box;
}

FText SSimCopterHelicopterDebugPanel::GetMissionStatusText() const
{
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	if (Helicopter == nullptr)
	{
		return FText::GetEmpty();
	}
	return FText::FromString(Helicopter->GetLastDebugMissionStatus());
}

FReply SSimCopterHelicopterDebugPanel::HandleStartMission(int32 TypeMask)
{
	if (ASimCopterHelicopterPawn* Helicopter = GetPawn())
	{
		Helicopter->DebugStartMission(TypeMask);
	}
	return FReply::Handled();
}

namespace
{
const TCHAR* GetDispatchServiceLabel(int32 ServiceIndex)
{
	switch (static_cast<SimCopterDispatch::EService>(ServiceIndex))
	{
	case SimCopterDispatch::EService::FireTruck: return TEXT("Fire Truck  (F2)");
	case SimCopterDispatch::EService::Police: return TEXT("Police  (F4 / F5 chase)");
	case SimCopterDispatch::EService::Ambulance: return TEXT("Ambulance  (F3)");
	default: return TEXT("?");
	}
}
}

FText SSimCopterHelicopterDebugPanel::GetDispatchServiceText() const
{
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	if (Helicopter == nullptr)
	{
		return FText::GetEmpty();
	}
	return FText::FromString(GetDispatchServiceLabel(Helicopter->GetSelectedDispatchService()));
}

FText SSimCopterHelicopterDebugPanel::GetDispatchStatusText() const
{
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	if (Helicopter == nullptr)
	{
		return FText::GetEmpty();
	}

	const FString Status = Helicopter->GetSelectedDispatchServiceStatus();
	const FString Last = Helicopter->GetLastDispatchStatus();
	if (Last.IsEmpty())
	{
		return FText::FromString(Status);
	}
	return FText::FromString(FString::Printf(TEXT("%s\n%s"), *Status, *Last));
}

FReply SSimCopterHelicopterDebugPanel::HandleDispatchServicePrev()
{
	if (ASimCopterHelicopterPawn* Helicopter = GetPawn())
	{
		Helicopter->CycleSelectedDispatchService(-1);
	}
	return FReply::Handled();
}

FReply SSimCopterHelicopterDebugPanel::HandleDispatchServiceNext()
{
	if (ASimCopterHelicopterPawn* Helicopter = GetPawn())
	{
		Helicopter->CycleSelectedDispatchService(1);
	}
	return FReply::Handled();
}

FReply SSimCopterHelicopterDebugPanel::HandleDispatch()
{
	if (ASimCopterHelicopterPawn* Helicopter = GetPawn())
	{
		Helicopter->RequestDispatch(Helicopter->GetSelectedDispatchService(), false, false);
	}
	return FReply::Handled();
}

FReply SSimCopterHelicopterDebugPanel::HandleDispatchChase()
{
	if (ASimCopterHelicopterPawn* Helicopter = GetPawn())
	{
		// F5's special dispatch is police-only in the original; the panel routes the
		// request the same way and lets the service selection decide.
		Helicopter->RequestDispatch(Helicopter->GetSelectedDispatchService(), true, false);
	}
	return FReply::Handled();
}

FReply SSimCopterHelicopterDebugPanel::HandleDispatchClear()
{
	if (ASimCopterHelicopterPawn* Helicopter = GetPawn())
	{
		Helicopter->RequestDispatch(Helicopter->GetSelectedDispatchService(), false, true);
	}
	return FReply::Handled();
}

FText SSimCopterHelicopterDebugPanel::GetModelLineText() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	if (HelicopterPawn == nullptr)
	{
		return NSLOCTEXT("SimCopterDebug", "NoPawn", "(no helicopter)");
	}

	const FSimCopterHelicopterDefinition* Definition = HelicopterPawn->GetHelicopterDefinition();
	if (Definition == nullptr)
	{
		return NSLOCTEXT("SimCopterDebug", "NoDefinition", "(unknown model)");
	}

	return FText::FromString(FString::Printf(
		TEXT("%s     TYPE %d%s"),
		*Definition->DisplayName,
		Definition->InternalTypeIndex,
		Definition->bApacheArmament ? TEXT("  SPECIAL") : TEXT("")));
}

FText SSimCopterHelicopterDebugPanel::GetModelDetailText() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	if (HelicopterPawn == nullptr)
	{
		return FText::GetEmpty();
	}
	const FSimCopterHelicopterDefinition* Definition = HelicopterPawn->GetHelicopterDefinition();
	if (Definition == nullptr)
	{
		return FText::GetEmpty();
	}

	return FText::FromString(FString::Printf(
		TEXT("%s / %s   Seats %d   Max load %d lb   %s   %s"),
		*Definition->BodyObjectName,
		*Definition->MainRotorObjectName,
		Definition->PassengerSeats,
		HelicopterPawn->GetMaxLoadPounds(),
		Definition->bNoTailRotor ? TEXT("NOTAR") : TEXT("Tail rotor"),
		HelicopterPawn->IsUsingOriginalMesh() ? TEXT("Model ready") : TEXT("PLACEHOLDER MESH")));
}

FText SSimCopterHelicopterDebugPanel::GetModelStatusText() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	if (HelicopterPawn == nullptr)
	{
		return FText::GetEmpty();
	}
	return FText::FromString(HelicopterPawn->GetLastModelSwitchStatus());
}

FText SSimCopterHelicopterDebugPanel::GetCameraModeText() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	if (HelicopterPawn == nullptr)
	{
		return FText::GetEmpty();
	}

	const TCHAR* Label = TEXT("CHASE VIEW");
	switch (HelicopterPawn->GetCameraMode())
	{
	case ESimCopterCameraMode::Orbit:
		Label = TEXT("ORBIT VIEW");
		break;
	case ESimCopterCameraMode::Rescue:
		Label = TEXT("RESCUE VIEW");
		break;
	default:
		break;
	}
	return FText::FromString(FString::Printf(TEXT("%s   [C] cycle"), Label));
}

FReply SSimCopterHelicopterDebugPanel::HandleResetCameraOffset()
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->ResetCameraViewDebugOffset(HelicopterPawn->GetCameraMode());
	}
	return FReply::Handled();
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetCameraTranslationX() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetCameraViewDebugOffset(HelicopterPawn->GetCameraMode()).TranslationCm.X)
		: TOptional<float>();
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetCameraTranslationY() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetCameraViewDebugOffset(HelicopterPawn->GetCameraMode()).TranslationCm.Y)
		: TOptional<float>();
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetCameraTranslationZ() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetCameraViewDebugOffset(HelicopterPawn->GetCameraMode()).TranslationCm.Z)
		: TOptional<float>();
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetCameraRotationPitch() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetCameraViewDebugOffset(HelicopterPawn->GetCameraMode()).RotationDeg.Pitch)
		: TOptional<float>();
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetCameraRotationYaw() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetCameraViewDebugOffset(HelicopterPawn->GetCameraMode()).RotationDeg.Yaw)
		: TOptional<float>();
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetCameraRotationRoll() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetCameraViewDebugOffset(HelicopterPawn->GetCameraMode()).RotationDeg.Roll)
		: TOptional<float>();
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetCameraZoomVerticalFramingStrength() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(
			HelicopterPawn->GetCameraViewDebugOffset(
				HelicopterPawn->GetCameraMode()).ZoomVerticalFramingStrength)
		: TOptional<float>();
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetCameraMaxZoomDistance() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(
			HelicopterPawn->GetCameraViewMaxZoomDistanceCm(
				HelicopterPawn->GetCameraMode()))
		: TOptional<float>();
}

void SSimCopterHelicopterDebugPanel::HandleCameraTranslationXChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		const ESimCopterCameraMode Mode = HelicopterPawn->GetCameraMode();
		FVector Translation = HelicopterPawn->GetCameraViewDebugOffset(Mode).TranslationCm;
		Translation.X = Value;
		HelicopterPawn->SetCameraViewDebugTranslation(Mode, Translation);
	}
}

void SSimCopterHelicopterDebugPanel::HandleCameraTranslationYChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		const ESimCopterCameraMode Mode = HelicopterPawn->GetCameraMode();
		FVector Translation = HelicopterPawn->GetCameraViewDebugOffset(Mode).TranslationCm;
		Translation.Y = Value;
		HelicopterPawn->SetCameraViewDebugTranslation(Mode, Translation);
	}
}

void SSimCopterHelicopterDebugPanel::HandleCameraTranslationZChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		const ESimCopterCameraMode Mode = HelicopterPawn->GetCameraMode();
		FVector Translation = HelicopterPawn->GetCameraViewDebugOffset(Mode).TranslationCm;
		Translation.Z = Value;
		HelicopterPawn->SetCameraViewDebugTranslation(Mode, Translation);
	}
}

void SSimCopterHelicopterDebugPanel::HandleCameraRotationPitchChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		const ESimCopterCameraMode Mode = HelicopterPawn->GetCameraMode();
		FRotator Rotation = HelicopterPawn->GetCameraViewDebugOffset(Mode).RotationDeg;
		Rotation.Pitch = Value;
		HelicopterPawn->SetCameraViewDebugRotation(Mode, Rotation);
	}
}

void SSimCopterHelicopterDebugPanel::HandleCameraRotationYawChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		const ESimCopterCameraMode Mode = HelicopterPawn->GetCameraMode();
		FRotator Rotation = HelicopterPawn->GetCameraViewDebugOffset(Mode).RotationDeg;
		Rotation.Yaw = Value;
		HelicopterPawn->SetCameraViewDebugRotation(Mode, Rotation);
	}
}

void SSimCopterHelicopterDebugPanel::HandleCameraRotationRollChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		const ESimCopterCameraMode Mode = HelicopterPawn->GetCameraMode();
		FRotator Rotation = HelicopterPawn->GetCameraViewDebugOffset(Mode).RotationDeg;
		Rotation.Roll = Value;
		HelicopterPawn->SetCameraViewDebugRotation(Mode, Rotation);
	}
}

void SSimCopterHelicopterDebugPanel::HandleCameraZoomVerticalFramingStrengthChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetCameraViewZoomVerticalFramingStrength(
			HelicopterPawn->GetCameraMode(),
			Value);
	}
}

void SSimCopterHelicopterDebugPanel::HandleCameraMaxZoomDistanceChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetCameraViewMaxZoomDistanceCm(
			HelicopterPawn->GetCameraMode(),
			Value);
	}
}

FText SSimCopterHelicopterDebugPanel::GetToolLineText() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	if (HelicopterPawn == nullptr)
	{
		return FText::GetEmpty();
	}

	const ESimCopterHelicopterTool Selected = HelicopterPawn->GetSelectedTool();
	const ESimCopterHelicopterTool Active = HelicopterPawn->GetActiveTool();
	FString Line = SimCopterHelicopterRegistry::GetToolDisplayName(Selected);
	if (Active != Selected)
	{
		// The selection is remembered; explain what input is actually driving instead.
		Line += FString::Printf(
			TEXT("  (using %s)"), SimCopterHelicopterRegistry::GetToolDisplayName(Active));
	}
	return FText::FromString(Line);
}

FText SSimCopterHelicopterDebugPanel::GetToolAvailabilityText() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	if (HelicopterPawn == nullptr)
	{
		return FText::GetEmpty();
	}

	FString Line = HelicopterPawn->DescribeToolAvailability(HelicopterPawn->GetSelectedTool());
	const FString Status = HelicopterPawn->GetLastToolStatus();
	if (!Status.IsEmpty())
	{
		Line += TEXT("   ");
		Line += Status;
	}
	return FText::FromString(Line);
}

FText SSimCopterHelicopterDebugPanel::GetToolContextText() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	if (HelicopterPawn == nullptr)
	{
		return FText::GetEmpty();
	}

	switch (HelicopterPawn->GetSelectedTool())
	{
	case ESimCopterHelicopterTool::Megaphone:
		return FText::FromString(
			SimCopterHelicopterRegistry::GetMegaphoneMessageName(HelicopterPawn->GetSelectedMegaphoneMessage()));
	case ESimCopterHelicopterTool::TearGas:
		return FText::FromString(FString::Printf(
			TEXT("Rounds %d / %d"),
			HelicopterPawn->GetEquipmentState().GetTearGasRounds(),
			SimCopterHelicopterRegistry::TearGasCapacity));
	default:
		return FText::GetEmpty();
	}
}

FText SSimCopterHelicopterDebugPanel::GetGrantButtonText() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	if (HelicopterPawn == nullptr)
	{
		return FText::GetEmpty();
	}
	return HelicopterPawn->GetToolAvailability(HelicopterPawn->GetSelectedTool()) ==
			ESimCopterToolAvailability::DebugGrant
		? NSLOCTEXT("SimCopterDebug", "Revoke", "Revoke")
		: NSLOCTEXT("SimCopterDebug", "Grant", "Grant for session");
}

EVisibility SSimCopterHelicopterDebugPanel::GetMegaphoneRowVisibility() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return (HelicopterPawn != nullptr &&
			HelicopterPawn->GetSelectedTool() == ESimCopterHelicopterTool::Megaphone)
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

EVisibility SSimCopterHelicopterDebugPanel::GetTearGasRowVisibility() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return (HelicopterPawn != nullptr &&
			HelicopterPawn->GetSelectedTool() == ESimCopterHelicopterTool::TearGas)
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

EVisibility SSimCopterHelicopterDebugPanel::GetGrantButtonVisibility() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	if (HelicopterPawn == nullptr)
	{
		return EVisibility::Collapsed;
	}

	// Apache weapons come from the model, so there is nothing to grant or revoke.
	const ESimCopterToolAvailability Availability =
		HelicopterPawn->GetToolAvailability(HelicopterPawn->GetSelectedTool());
	return Availability == ESimCopterToolAvailability::Model
		? EVisibility::Collapsed
		: EVisibility::Visible;
}

FReply SSimCopterHelicopterDebugPanel::HandleModelPrev()
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->CycleHelicopterModel(-1);
	}
	return FReply::Handled();
}

FReply SSimCopterHelicopterDebugPanel::HandleModelNext()
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->CycleHelicopterModel(1);
	}
	return FReply::Handled();
}

FReply SSimCopterHelicopterDebugPanel::HandleToolPrev()
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->CycleSelectedTool(-1);
	}
	return FReply::Handled();
}

FReply SSimCopterHelicopterDebugPanel::HandleToolNext()
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->CycleSelectedTool(1);
	}
	return FReply::Handled();
}

FReply SSimCopterHelicopterDebugPanel::HandleUsePressed()
{
	// Routes through exactly the same entry point as left click, so held tools behave the
	// same from the button as from the world input.
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->StartPrimaryToolUse();
	}
	return FReply::Handled();
}

FReply SSimCopterHelicopterDebugPanel::HandleUseReleased()
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->StopPrimaryToolUse();
	}
	return FReply::Handled();
}

FReply SSimCopterHelicopterDebugPanel::HandleToggleGrant()
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		const ESimCopterHelicopterTool Tool = HelicopterPawn->GetSelectedTool();
		const bool bGranted =
			HelicopterPawn->GetToolAvailability(Tool) == ESimCopterToolAvailability::DebugGrant;
		HelicopterPawn->SetDebugToolGrant(Tool, !bGranted);
	}
	return FReply::Handled();
}

FReply SSimCopterHelicopterDebugPanel::HandleMessagePrev()
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->CycleMegaphoneMessage(-1);
	}
	return FReply::Handled();
}

FReply SSimCopterHelicopterDebugPanel::HandleMessageNext()
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->CycleMegaphoneMessage(1);
	}
	return FReply::Handled();
}

FReply SSimCopterHelicopterDebugPanel::HandleRefillTearGas()
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->DebugRefillTearGas();
	}
	return FReply::Handled();
}

FReply SSimCopterHelicopterDebugPanel::HandleRopeToggle()
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->ToggleRopeFromDebugPanel();
	}
	return FReply::Handled();
}
