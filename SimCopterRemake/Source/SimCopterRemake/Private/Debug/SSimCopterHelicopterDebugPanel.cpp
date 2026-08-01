// Copyright Epic Games, Inc. All Rights Reserved.

#include "Debug/SSimCopterHelicopterDebugPanel.h"

#include "Flight/SimCopterHelicopterPawn.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Ground/SimCopterApachePool.h"
#include "Ground/SimCopterDispatch.h"
#include "Ground/SimCopterTearGasPool.h"
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

				// --- FLIGHT: which of the original's two handling models is running ---
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"FlightModelTip",
						"The original shipped two handling models and picked between them by camera "
						"view. EASY halves the pitch key ramp and the pitch clamp, holds a trimmed "
						"nose longer, doubles the airspeed a degree of pitch buys, and bleeds speed "
						"off twice as fast. Switching is safe in flight."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "FlightModel", "FLIGHT"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(10, true))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SSimCopterHelicopterDebugPanel::GetFlightModelText)
						.ColorAndOpacity(ValueColor)
						.Font(PanelFont(12, true))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SButton)
						.IsFocusable(false)
						.ContentPadding(FMargin(7.0f, 1.0f))
						.OnClicked(FOnClicked::CreateSP(
							this,
							&SSimCopterHelicopterDebugPanel::HandleToggleEasyFlightModel))
						[
							SNew(STextBlock)
							.Text(this, &SSimCopterHelicopterDebugPanel::GetFlightModelButtonText)
							.Font(PanelFont(10))
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"WaterTextureFpsTip",
						"Playback rate for the original five-cell water texture sequence on terrain water "
						"and mesh pools. This is independent of the geometric wave shader. The previous "
						"four-second rate came from treating FUN_004814c0's fixed-time threshold as "
						"milliseconds. Set 0 to freeze frame zero for inspection."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "WaterTextureFps", "WATER TEX FPS"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(9, true))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(0.0f)
						.MaxValue(120.0f)
						.MinSliderValue(0.0f)
						.MaxSliderValue(60.0f)
						.Delta(1.0f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetWaterTextureFramesPerSecond)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleWaterTextureFramesPerSecondChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 2.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"FrameRateReferenceTip",
						"The frame rate the original's per-frame rules are assumed to have been "
						"written for. The executable only ever names 20, and had no fixed timestep, "
						"so this is feel as much as fidelity - the simulation stays identical at any "
						"display rate whatever you set.\n\n"
						"TURB is the airframe shake alone. SIM is how far the helicopter coasts when "
						"you release the collective, how fast fire burns you, and the attitude "
						"window. ACCEL is how quickly it gets moving. They are separate because "
						"raising one number to sharpen acceleration also makes the shake busier."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "FrameRateReference", "REF FPS"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(9, true))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("TURB"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f, 8.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(5.0f)
						.MaxValue(240.0f)
						.MinSliderValue(10.0f)
						.MaxSliderValue(120.0f)
						.Delta(1.0f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetTurbulenceReferenceFps)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleTurbulenceReferenceFpsChanged)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("SIM"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f, 8.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(5.0f)
						.MaxValue(240.0f)
						.MinSliderValue(10.0f)
						.MaxSliderValue(120.0f)
						.Delta(1.0f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetFlightReferenceFps)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleFlightReferenceFpsChanged)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("ACCEL"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(5.0f)
						.MaxValue(240.0f)
						.MinSliderValue(10.0f)
						.MaxSliderValue(120.0f)
						.Delta(1.0f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetSpeedChaseReferenceFps)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleSpeedChaseReferenceFpsChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"RotorSpinTip",
						"How much faster than the original's 39.1-degrees-per-frame strobe the blades "
						"are drawn. 1 is the original's 782 deg/s, which reads as slow motion on a "
						"modern display. Presentation only - nothing in the flight model reads the "
						"blade angle, and this is independent of REF FPS on purpose."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "RotorSpin", "ROTOR SPIN x"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(9, true))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(0.1f)
						.MaxValue(40.0f)
						.MinSliderValue(1.0f)
						.MaxSliderValue(20.0f)
						.Delta(0.25f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(2)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetRotorVisualMultiplier)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleRotorVisualMultiplierChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"VehicleMetallicTip",
						"Metallic on the shared vehicle material - the fuselage, the ground cars and the "
						"ambient planes/trains/boats all move together. The original had no PBR at all, so "
						"0 (dielectric) is the faithful look and anything above it is taste.\n\n"
						"The city's buildings deliberately stay on the plain material, so this cannot turn "
						"the skyline to chrome.\n\n"
						"Does nothing until M_SimCopterLitVertexColor exposes a \"Metallic\" scalar "
						"parameter wired to its Metallic input."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "VehicleMetallic", "METALLIC"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(9, true))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(0.0f)
						.MaxValue(1.0f)
						.MinSliderValue(0.0f)
						.MaxSliderValue(1.0f)
						.Delta(0.01f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(2)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetVehicleMetallic)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleVehicleMetallicChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"FlashingLightIntensityTip",
						"Multiplies the point-light intensity of the face-type-25 blink markers "
						"(FUN_00496c00) - this airframe's four position lights and the city's building "
						"beacons together. It is a multiplier rather than an absolute value because the "
						"two are tuned to different bases, and one number would flatten that.\n\n"
						"The original had no dynamic lighting at all; 0 leaves the coloured cards drawing "
						"with no light cast."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "FlashingLightIntensity", "BLINK LIGHT x"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(9, true))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(0.0f)
						.MaxValue(20.0f)
						// Ranged around the tuned 0.02: with MegaLights solving every marker, useful
						// values sit well under 1 and a 0..5 spin stepped straight past them.
						.MinSliderValue(0.0f)
						.MaxSliderValue(0.5f)
						.Delta(0.005f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(3)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetFlashingLightIntensityScale)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleFlashingLightIntensityScaleChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"GroundLiftTip",
						"Framing only: as the camera nears the ground it raises the boom PIVOT, which walks "
						"the helicopter down the screen. Parked it should sit around the middle instead of "
						"up near the top. Terrain only - buildings are the avoidance search's job.\n\n"
						"LIFT is how far the pivot rises at full strength; raise it to push the aircraft "
						"further down the frame.\n\n"
						"FULL is the clearance at or below which the lift is at maximum - the knob to reach "
						"for if a landed helicopter is not sitting where you want, since it has to be above "
						"the camera's actual parked height. START is where the effect begins easing in on "
						"the way down; wider is gentler.\n\n"
						"Persisted to GameUserSettings like the camera offsets."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "GroundLift", "GROUND LIFT"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(9, true))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("LIFT"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f, 8.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(0.0f)
						.MaxValue(4000.0f)
						.MinSliderValue(0.0f)
						.MaxSliderValue(1200.0f)
						.Delta(5.0f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCameraGroundLiftHeightCm)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleCameraGroundLiftHeightCmChanged)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("FULL"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f, 8.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(0.0f)
						.MaxValue(20000.0f)
						.MinSliderValue(0.0f)
						.MaxSliderValue(1000.0f)
						.Delta(5.0f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCameraGroundLiftFullDistanceCm)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleCameraGroundLiftFullDistanceCmChanged)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("START"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(1.0f)
						.MaxValue(20000.0f)
						.MinSliderValue(50.0f)
						.MaxSliderValue(3000.0f)
						.Delta(10.0f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCameraGroundLiftProbeRangeCm)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleCameraGroundLiftProbeRangeCmChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"GroundLiftStatusTip",
						"What the lift is doing right now: the clearance the downward probe measured from "
						"the camera, and the lift currently applied.\n\n"
						"\"no ground\" means the probe hit nothing at all - the lift cannot work and the "
						"numbers above are irrelevant. A clearance that never falls below START means the "
						"camera is further from the ground than you think."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "GroundLiftStatus", "  live"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(9, true))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SSimCopterHelicopterDebugPanel::GetCameraGroundLiftStatusText)
						.ColorAndOpacity(ValueColor)
						.Font(PanelFont(9))
					]
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
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 2.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"CockpitStabilizationTip",
						"Cockpit view only. AMT is how much of the airframe's pitch/roll the eye "
						"adopts (1 rides the model rigidly, 0 keeps the horizon level); SPD is how "
						"quickly it catches up. Camera filter only - flight handling and tool aim "
						"are unaffected."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "CockpitStabilization", "COCKPIT STAB"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(9, true))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("AMT"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f, 8.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(0.0f)
						.MaxValue(1.0f)
						.MinSliderValue(0.0f)
						.MaxSliderValue(1.0f)
						.Delta(0.05f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(2)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCockpitAttitudeFollowStrength)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleCockpitAttitudeFollowStrengthChanged)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("SPD"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(0.1f)
						.MaxValue(30.0f)
						.MinSliderValue(0.5f)
						.MaxSliderValue(20.0f)
						.Delta(0.5f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCockpitAttitudeLerpSpeed)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleCockpitAttitudeLerpSpeedChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 2.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"CockpitCannonOffsetTip",
						"Where the water cannon view model sits in the cockpit view, in camera-space "
						"centimetres from the eye: X forward, Y right, Z up. It is carried by the "
						"camera, so this is a fixed position in the frame."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "CockpitCannonOffset", "CANNON VM"))
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
						.MinValue(-1000.0f)
						.MaxValue(1000.0f)
						.MinSliderValue(-300.0f)
						.MaxSliderValue(300.0f)
						.Delta(1.0f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCockpitCannonOffsetX)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleCockpitCannonOffsetXChanged)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Y"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f, 8.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(-1000.0f)
						.MaxValue(1000.0f)
						.MinSliderValue(-300.0f)
						.MaxSliderValue(300.0f)
						.Delta(1.0f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCockpitCannonOffsetY)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleCockpitCannonOffsetYChanged)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Z"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(-1000.0f)
						.MaxValue(1000.0f)
						.MinSliderValue(-300.0f)
						.MaxSliderValue(300.0f)
						.Delta(1.0f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(1)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetCockpitCannonOffsetZ)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleCockpitCannonOffsetZChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 2.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"RotorDiscOpacityTip",
						"Opacity of the spinning-rotor blur disc. 0 hides it; 1 is a solid disc."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "RotorDiscOpacity", "ROTOR ALPHA"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(9, true))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(0.0f)
						.MaxValue(1.0f)
						.MinSliderValue(0.0f)
						.MaxSliderValue(1.0f)
						.Delta(0.01f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(2)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetRotorDiscOpacity)
						.OnValueChanged(
							this,
							&SSimCopterHelicopterDebugPanel::HandleRotorDiscOpacityChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					.ToolTipText(NSLOCTEXT(
						"SimCopterDebug",
						"RotorDiscColorTip",
						"Colour of the spinning-rotor blur disc, as linear RGB in 0..1."))
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(86.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("SimCopterDebug", "RotorDiscColor", "ROTOR RGB"))
							.ColorAndOpacity(LabelColor)
							.Font(PanelFont(9, true))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("R"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f, 8.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(0.0f)
						.MaxValue(1.0f)
						.MinSliderValue(0.0f)
						.MaxSliderValue(1.0f)
						.Delta(0.01f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(2)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetRotorDiscColorR)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleRotorDiscColorRChanged)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("G"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f, 8.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(0.0f)
						.MaxValue(1.0f)
						.MinSliderValue(0.0f)
						.MaxSliderValue(1.0f)
						.Delta(0.01f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(2)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetRotorDiscColorG)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleRotorDiscColorGChanged)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("B"))).ColorAndOpacity(LabelColor).Font(PanelFont(9, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(3.0f, 0.0f))
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.MinValue(0.0f)
						.MaxValue(1.0f)
						.MinSliderValue(0.0f)
						.MaxSliderValue(1.0f)
						.Delta(0.01f)
						.MinFractionalDigits(0)
						.MaxFractionalDigits(2)
						.Value(this, &SSimCopterHelicopterDebugPanel::GetRotorDiscColorB)
						.OnValueChanged(this, &SSimCopterHelicopterDebugPanel::HandleRotorDiscColorBChanged)
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
	case ESimCopterCameraMode::Cockpit:
		Label = TEXT("COCKPIT VIEW");
		break;
	default:
		break;
	}
	return FText::FromString(FString::Printf(TEXT("%s   [C] cycle"), Label));
}

FText SSimCopterHelicopterDebugPanel::GetFlightModelText() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	if (HelicopterPawn == nullptr)
	{
		return FText::GetEmpty();
	}
	return HelicopterPawn->IsEasyFlightModelEnabled()
		? NSLOCTEXT("SimCopterDebug", "FlightModelEasy", "EASY MODEL")
		: NSLOCTEXT("SimCopterDebug", "FlightModelStandard", "STANDARD MODEL");
}

FText SSimCopterHelicopterDebugPanel::GetFlightModelButtonText() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	if (HelicopterPawn == nullptr)
	{
		return FText::GetEmpty();
	}
	return HelicopterPawn->IsEasyFlightModelEnabled()
		? NSLOCTEXT("SimCopterDebug", "UseStandardModel", "USE STANDARD")
		: NSLOCTEXT("SimCopterDebug", "UseEasyModel", "USE EASY");
}

FReply SSimCopterHelicopterDebugPanel::HandleToggleEasyFlightModel()
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetEasyFlightModelEnabled(!HelicopterPawn->IsEasyFlightModelEnabled());
	}
	return FReply::Handled();
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

TOptional<float> SSimCopterHelicopterDebugPanel::GetCockpitAttitudeFollowStrength() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetCockpitAttitudeFollowStrength())
		: TOptional<float>();
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetCockpitAttitudeLerpSpeed() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetCockpitAttitudeLerpSpeed())
		: TOptional<float>();
}

void SSimCopterHelicopterDebugPanel::HandleCockpitAttitudeFollowStrengthChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetCockpitAttitudeFollowStrength(Value);
	}
}

void SSimCopterHelicopterDebugPanel::HandleCockpitAttitudeLerpSpeedChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetCockpitAttitudeLerpSpeed(Value);
	}
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetCockpitCannonOffsetX() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(static_cast<float>(HelicopterPawn->GetCockpitCannonViewModelOffsetCm().X))
		: TOptional<float>();
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetCockpitCannonOffsetY() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(static_cast<float>(HelicopterPawn->GetCockpitCannonViewModelOffsetCm().Y))
		: TOptional<float>();
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetCockpitCannonOffsetZ() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(static_cast<float>(HelicopterPawn->GetCockpitCannonViewModelOffsetCm().Z))
		: TOptional<float>();
}

void SSimCopterHelicopterDebugPanel::HandleCockpitCannonOffsetXChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		FVector Offset = HelicopterPawn->GetCockpitCannonViewModelOffsetCm();
		Offset.X = Value;
		HelicopterPawn->SetCockpitCannonViewModelOffsetCm(Offset);
	}
}

void SSimCopterHelicopterDebugPanel::HandleCockpitCannonOffsetYChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		FVector Offset = HelicopterPawn->GetCockpitCannonViewModelOffsetCm();
		Offset.Y = Value;
		HelicopterPawn->SetCockpitCannonViewModelOffsetCm(Offset);
	}
}

void SSimCopterHelicopterDebugPanel::HandleCockpitCannonOffsetZChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		FVector Offset = HelicopterPawn->GetCockpitCannonViewModelOffsetCm();
		Offset.Z = Value;
		HelicopterPawn->SetCockpitCannonViewModelOffsetCm(Offset);
	}
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetTurbulenceReferenceFps() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetTurbulenceReferenceFps())
		: TOptional<float>();
}

void SSimCopterHelicopterDebugPanel::HandleTurbulenceReferenceFpsChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetTurbulenceReferenceFps(Value);
	}
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetFlightReferenceFps() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetFlightReferenceFps())
		: TOptional<float>();
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetSpeedChaseReferenceFps() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetSpeedChaseReferenceFps())
		: TOptional<float>();
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetRotorVisualMultiplier() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetRotorVisualMultiplier())
		: TOptional<float>();
}

void SSimCopterHelicopterDebugPanel::HandleFlightReferenceFpsChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetFlightReferenceFps(Value);
	}
}

void SSimCopterHelicopterDebugPanel::HandleSpeedChaseReferenceFpsChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetSpeedChaseReferenceFps(Value);
	}
}

void SSimCopterHelicopterDebugPanel::HandleRotorVisualMultiplierChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetRotorVisualMultiplier(Value);
	}
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetVehicleMetallic() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetVehicleMetallic())
		: TOptional<float>();
}

void SSimCopterHelicopterDebugPanel::HandleVehicleMetallicChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetVehicleMetallic(Value);
	}
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetFlashingLightIntensityScale() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetFlashingLightIntensityScale())
		: TOptional<float>();
}

void SSimCopterHelicopterDebugPanel::HandleFlashingLightIntensityScaleChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetFlashingLightIntensityScale(Value);
	}
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetWaterTextureFramesPerSecond() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetWaterTextureFramesPerSecond())
		: TOptional<float>();
}

void SSimCopterHelicopterDebugPanel::HandleWaterTextureFramesPerSecondChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetWaterTextureFramesPerSecond(Value);
	}
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetCameraGroundLiftHeightCm() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetCameraGroundLiftHeightCm())
		: TOptional<float>();
}

void SSimCopterHelicopterDebugPanel::HandleCameraGroundLiftHeightCmChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetCameraGroundLiftHeightCm(Value);
	}
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetCameraGroundLiftProbeRangeCm() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetCameraGroundLiftProbeRangeCm())
		: TOptional<float>();
}

void SSimCopterHelicopterDebugPanel::HandleCameraGroundLiftProbeRangeCmChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetCameraGroundLiftProbeRangeCm(Value);
	}
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetCameraGroundLiftFullDistanceCm() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetCameraGroundLiftFullDistanceCm())
		: TOptional<float>();
}

void SSimCopterHelicopterDebugPanel::HandleCameraGroundLiftFullDistanceCmChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetCameraGroundLiftFullDistanceCm(Value);
	}
}

FText SSimCopterHelicopterDebugPanel::GetCameraGroundLiftStatusText() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	if (HelicopterPawn == nullptr)
	{
		return FText::GetEmpty();
	}

	const float ProbeDistanceCm = HelicopterPawn->GetLastCameraGroundProbeDistanceCm();
	const float AppliedLiftCm = HelicopterPawn->GetCurrentCameraGroundLiftCm();
	if (ProbeDistanceCm < 0.0f)
	{
		return FText::FromString(FString::Printf(
			TEXT("no ground under camera    lift %.0f cm"), AppliedLiftCm));
	}

	return FText::FromString(FString::Printf(
		TEXT("clearance %.0f cm    lift %.0f cm"), ProbeDistanceCm, AppliedLiftCm));
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetRotorDiscOpacity() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetRotorDiscOpacity())
		: TOptional<float>();
}

void SSimCopterHelicopterDebugPanel::HandleRotorDiscOpacityChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		HelicopterPawn->SetRotorDiscOpacity(Value);
	}
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetRotorDiscColorR() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetRotorDiscColor().R)
		: TOptional<float>();
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetRotorDiscColorG() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetRotorDiscColor().G)
		: TOptional<float>();
}

TOptional<float> SSimCopterHelicopterDebugPanel::GetRotorDiscColorB() const
{
	const ASimCopterHelicopterPawn* HelicopterPawn = GetPawn();
	return HelicopterPawn != nullptr
		? TOptional<float>(HelicopterPawn->GetRotorDiscColor().B)
		: TOptional<float>();
}

void SSimCopterHelicopterDebugPanel::HandleRotorDiscColorRChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		FLinearColor Color = HelicopterPawn->GetRotorDiscColor();
		Color.R = Value;
		HelicopterPawn->SetRotorDiscColor(Color);
	}
}

void SSimCopterHelicopterDebugPanel::HandleRotorDiscColorGChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		FLinearColor Color = HelicopterPawn->GetRotorDiscColor();
		Color.G = Value;
		HelicopterPawn->SetRotorDiscColor(Color);
	}
}

void SSimCopterHelicopterDebugPanel::HandleRotorDiscColorBChanged(float Value)
{
	if (ASimCopterHelicopterPawn* HelicopterPawn = GetPawn())
	{
		FLinearColor Color = HelicopterPawn->GetRotorDiscColor();
		Color.B = Value;
		HelicopterPawn->SetRotorDiscColor(Color);
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
	{
		// The ten pool slots (DAT_005d4bd0) are what actually limits rapid fire, so show what is
		// in the air beside the magazine: a shot is refused while all ten are busy.
		const USimCopterTearGasPoolComponent* Pool = HelicopterPawn->GetTearGasPool();
		return FText::FromString(FString::Printf(
			TEXT("Rounds %d / %d   in flight %d   clouds %d"),
			HelicopterPawn->GetEquipmentState().GetTearGasRounds(),
			SimCopterHelicopterRegistry::TearGasCapacity,
			Pool != nullptr ? Pool->GetActiveCanisterCount() : 0,
			Pool != nullptr ? Pool->GetActiveCloudCount() : 0));
	}
	case ESimCopterHelicopterTool::ApacheMissile:
	case ESimCopterHelicopterTool::ApacheMachineGun:
	{
		// The Apache's cockpit strip is deliberately just two fire buttons, so what is actually
		// in the air is only visible here.
		const USimCopterApachePoolComponent* Pool = HelicopterPawn->GetApachePool();
		return FText::FromString(FString::Printf(
			TEXT("missiles %d / 10   tracers %d / 70"),
			Pool != nullptr ? Pool->GetActiveMissileCount() : 0,
			Pool != nullptr ? Pool->GetActiveBulletCount() : 0));
	}
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
