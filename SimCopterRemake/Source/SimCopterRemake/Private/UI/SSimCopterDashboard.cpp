// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterDashboard.h"

#include "Audio/SimCopterRadio.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Ground/SimCopterPopulationSprite.h"
#include "Missions/SimCopterMissionSystem.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Styling/StyleDefaults.h"
#include "UI/SimCopterSegmentedBar.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
// --- dragging a passenger out of the seat window ----------------------------------------------
//
// The original lets the player pull a portrait out of the seat well to put that passenger on the
// ground, and dropping it back inside the well cancels. The drag carries the seat index; whether
// the passenger leaves is decided by where the pointer let go: the seat well marks a drop as
// handled, so an unhandled drop is by definition somewhere else.
class FSimCopterPassengerDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FSimCopterPassengerDragDropOp, FDragDropOperation)

	static TSharedRef<FSimCopterPassengerDragDropOp> New(
		TWeakObjectPtr<ASimCopterHelicopterPawn> InPawn,
		int32 InSlotIndex,
		const FSlateBrush* InPortrait)
	{
		TSharedRef<FSimCopterPassengerDragDropOp> Operation = MakeShared<FSimCopterPassengerDragDropOp>();
		Operation->Pawn = InPawn;
		Operation->SlotIndex = InSlotIndex;
		Operation->Portrait = InPortrait;
		Operation->Construct();
		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		if (Portrait == nullptr)
		{
			return nullptr;
		}
		return SNew(SImage).Image(Portrait);
	}

	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override
	{
		if (!bDropWasHandled)
		{
			if (ASimCopterHelicopterPawn* Helicopter = Pawn.Get())
			{
				Helicopter->DropPassengerAtSlot(SlotIndex);
			}
		}
		FDragDropOperation::OnDrop(bDropWasHandled, MouseEvent);
	}

private:
	TWeakObjectPtr<ASimCopterHelicopterPawn> Pawn;
	int32 SlotIndex = INDEX_NONE;
	const FSlateBrush* Portrait = nullptr;
};

// The portrait itself: press and move to start the drag.
class SSimCopterSeatPortrait : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterSeatPortrait) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ASimCopterHelicopterPawn>, Pawn)
		SLATE_ARGUMENT(int32, SlotIndex)
		SLATE_ARGUMENT(const FSlateBrush*, Portrait)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Pawn = InArgs._Pawn;
		SlotIndex = InArgs._SlotIndex;
		Portrait = InArgs._Portrait;
		ChildSlot[InArgs._Content.Widget];
	}

	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
		}
		return FReply::Unhandled();
	}

	virtual FReply OnDragDetected(const FGeometry&, const FPointerEvent&) override
	{
		return FReply::Handled().BeginDragDrop(
			FSimCopterPassengerDragDropOp::New(Pawn, SlotIndex, Portrait));
	}

private:
	TWeakObjectPtr<ASimCopterHelicopterPawn> Pawn;
	int32 SlotIndex = INDEX_NONE;
	const FSlateBrush* Portrait = nullptr;
};

// The seat well. Accepting the drop is what makes "put them back in the seat" mean "do nothing".
class SSimCopterSeatWell : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterSeatWell) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) { ChildSlot[InArgs._Content.Widget]; }

	virtual FReply OnDragOver(const FGeometry&, const FDragDropEvent& DragDropEvent) override
	{
		return DragDropEvent.GetOperationAs<FSimCopterPassengerDragDropOp>().IsValid()
			? FReply::Handled()
			: FReply::Unhandled();
	}

	virtual FReply OnDrop(const FGeometry&, const FDragDropEvent& DragDropEvent) override
	{
		// Handled and nothing else: the passenger stays where they were.
		return DragDropEvent.GetOperationAs<FSimCopterPassengerDragDropOp>().IsValid()
			? FReply::Handled()
			: FReply::Unhandled();
	}
};

const TCHAR* const UpscaledDashboardFile = TEXT("DASH6-plus-DASH4-upscaled.png");
const TCHAR* const GaugeNeedleFile = TEXT("gauge-needle.png");
constexpr float GaugeNeedleSourceWidth = 47.0f;
constexpr float GaugeNeedleSourceHeight = 350.0f;
constexpr float GaugeNeedlePivotFromLeft = 24.0f;
constexpr float GaugeNeedlePivotFromBottom = 68.0f;
constexpr float GaugeNeedlePivotFromTop = GaugeNeedleSourceHeight - GaugeNeedlePivotFromBottom;
constexpr float GaugeNeedleTipLength = GaugeNeedlePivotFromTop;

// --- seatwin2, 186x115 page space -----------------------------------------------------------
//
// The high-resolution reconstruction is kept at the original fixed width. Shorter seat lists
// crop its bottom in UV space; the fourteen-seat airframe uses the full 115-pixel height.
const TCHAR* const SeatWindowFile = TEXT("SEATWIN2.BMP");
const TCHAR* const UpscaledSeatWindowFile = TEXT("SEATWIN2-upscaled-small.png");
constexpr int32 SeatWindowWidth = 186;
constexpr int32 SeatWindowHeight = 115;

// FUN_00453f70 blits seat `i` to ((i % cols) * 0x20 + 0x0e, (i / cols) * 0x23 + 0x0a), and
// FUN_0048bf60 fixes cols at 5 (manifest +0x14). Three rows of five at that pitch fill the page
// exactly - 10 + 3 * 35 = 115 - and centre the block in the printed well, which the earlier
// measured-by-eye stride did not. The well runs to the bottom edge with no frame under it, so the
// window is cut to whatever depth the seat rows need rather than always standing 115 tall.
constexpr float SeatWellTop = 10.0f;
constexpr int32 SeatsPerRow = 5;
constexpr float SeatFirstPortraitX = 14.0f;
constexpr float SeatPortraitStride = 32.0f;
constexpr float SeatRowStride = 35.0f;

// people1.bmp is a 12x3 grid of 27x33 cells. FUN_00453f70's source rect is
// x0 = (record[0] * 3 + 3) * 9, y0 = record[1] * 0x21, i.e. column = head image index + 1 and row
// = the opcode-54 face. Column 0 row 0 is the empty seat, which is also what the clearing pass
// stamps into every seat before the occupied ones are drawn over it.
constexpr int32 PeopleEmptySeatColumn = 0;
constexpr int32 PeopleFirstFaceColumn = 1;
constexpr int32 PeopleColumns = 12;

// --- dash6.bmp, 458x82 ---------------------------------------------------------------------
const TCHAR* const Dash6File = TEXT("DASH6.BMP");
constexpr int32 Dash6Width = 458;
constexpr int32 Dash6Height = 82;

// FUN_004521a0 writes this rect by hand for the money readout.
const FIntRect MoneyRect(20, 12, 94, 26);
constexpr float UpscaledMoneyTextXOffset = -1.0f;
constexpr float UpscaledMoneyTextYOffset = 0.0f;

// The points bar, in the second black well (x 19..96, y 36..51). managge.bmp is NOT one block:
// like watergge.bmp it is a strip of three states - full, leading edge, empty - and the repaint
// at 0x004534f2 lays FIFTEEN 5x13 cells from x 0x14, y 0x25, which is what fills the well.
//
//   mov edi, 0x14                              ; the cursor, stepping +5
//   value = score * 15 / pointsNeeded          ; lea/lea/idiv, score clamped to needed first
//   loop 1  value times   src (0,0)-(5,13)
//   loop 2  once, if n<15 src (5,0)-(10,13)
//   loop 3  15-n times    src (10,0)-(15,13)
constexpr int32 PointsCellCount = 15;
constexpr int32 PointsCellWidth = 5;
constexpr int32 PointsCellHeight = 13;
constexpr float PointsBarX = 20.0f;  // 0x14
constexpr float PointsBarY = 37.0f;  // 0x25
const TCHAR* const PointsBarFile = TEXT("MANAGGE.BMP");

// damage.bmp is three 15x14 lamp frames: unlit, amber, red. Six lamps are stamped along the
// bottom of dash6 - the positions are where the unlit frame matches the page.
const TCHAR* const DamageFile = TEXT("DAMAGE.BMP");
constexpr int32 DamageLampCount = 6;
constexpr float DamageLampX[DamageLampCount] = { 11.0f, 25.0f, 39.0f, 52.0f, 66.0f, 80.0f };
constexpr float DamageLampY = 63.0f;
constexpr int32 DamageFrameWidth = 15;
constexpr int32 DamageFrameHeight = 14;

// The three dials, straight out of FUN_004521a0.
struct FGauge
{
	float CentreX;
	float CentreY;
	float Radius;
	float StartAngleDegrees;
	float DegreesPerUnit;
};
constexpr FGauge FuelGauge{ 161.0f, 47.0f, 28.0f, 258.0f, 3.35f };
constexpr FGauge AltimeterGauge{ 316.0f, 38.0f, 24.0f, 90.0f, 3.6f };
constexpr FGauge AirspeedGauge{ 398.0f, 32.0f, 26.0f, 90.0f, 14.0f };

// Both altimeter readout assets are vertical strips of eleven digits (0..9 then 0 again). The
// replacement is rendered into a slightly narrower window and lowered within the taller opening
// reconstructed in the high-resolution dashboard.
const TCHAR* const AltimeterDigitFile = TEXT("ALTNUMBR.BMP");
const TCHAR* const UpscaledAltimeterDigitFile = TEXT("altimeter-indicator-400h.png");
constexpr int32 AltimeterDigitWidth = 8;
constexpr int32 AltimeterDigitHeight = 9;
constexpr int32 AltimeterDigitFrameCount = 11;
constexpr float AltimeterDigitX = 300.0f;
constexpr float AltimeterDigitY = 31.0f;
constexpr float UpscaledAltimeterDigitWidth = 7.5f;
constexpr float UpscaledAltimeterDigitY = 35.0f;

// One turn of the altimeter face is the 100 units its ten divisions cover at 3.6 degrees each,
// and the rollover window counts the turns, so the instrument reads 0..1000 world units end to
// end. That full scale lines up with the top of the fire-damage altitude band (6110cm, which is
// 978 units at 6.25cm per unit), which is the check that says these are world units and not
// feet - a foot-scaled face would barely leave its stop over the whole flight envelope.
constexpr float AltimeterUnitsPerTurn = 100.0f;

// --- dash4.bmp, 455x43 ---------------------------------------------------------------------
const TCHAR* const Dash4File = TEXT("DASH4.BMP");
constexpr int32 Dash4Width = 455;
constexpr int32 Dash4Height = 43;
constexpr int32 DashboardWidth = Dash6Width;
constexpr int32 DashboardHeight = Dash4Height + Dash6Height;

// The replacement compass strip occupies compass1.bmp's original 160x16 page-space footprint.
// Its 512px source has direction marks every 48px, so one eight-point revolution is 384 source
// pixels (120 page pixels) and the extra marks provide the overlap used for seamless wrapping.
const TCHAR* const CompassFile = TEXT("COMPASS1.BMP");
const TCHAR* const UpscaledCompassFile = TEXT("compass-indicator-512w.png");
constexpr int32 CompassStripWidth = 160;
constexpr int32 CompassStripHeight = 16;
constexpr float UpscaledCompassSourceWidth = 512.0f;
constexpr float UpscaledCompassPointStride = 48.0f;
constexpr float UpscaledCompassNorthCentre = 65.5f;
constexpr float CompassSourceToPageScale =
	static_cast<float>(CompassStripWidth) / UpscaledCompassSourceWidth;
constexpr float CompassPixelsPerRevolution =
	UpscaledCompassPointStride * 8.0f * CompassSourceToPageScale;
constexpr float CompassNorthCentre =
	UpscaledCompassNorthCentre * CompassSourceToPageScale;
constexpr float CompassWindowX = 402.0f;
constexpr float CompassWindowY = 11.0f;
constexpr float CompassWindowWidth = 37.0f;
constexpr float UpscaledCompassWindowXOffset = -3.0f;
constexpr float UpscaledCompassWindowYOffset = 3.0f;

// --- the radio tuner --------------------------------------------------------------------------
//
// dash4's left third is an FM head unit: a dark body with a lit scale printed 88 / 92 / 96 /
// 100 / 104, and a needle that slides along it. The original stores the needle position in the
// dash4 widget (FUN_00451980 sets [0x24] = -1 for "no station yet") and repaints it from the
// "REIO" state message the radio publishes each time it changes.
//
// These bounds are MEASURED from DASH4.BMP rather than taken from the exe, because the widget's
// own rectangle (20, 15, 86, 38) bounds the whole head unit, not the lit scale. The measurement
// is a luminance profile - Docs/scratchpad/sound/radio/measure_tuner.py - which puts the lit
// band at rows 24..30 and columns 17..92, with the printed labels centred at x = 22, 38, 53,
// 69 and 84.
constexpr float RadioScaleX = 17.0f;
constexpr float RadioScaleY = 24.0f;
constexpr float RadioScaleWidth = 76.0f;   // x 17..92 inclusive
constexpr float RadioScaleHeight = 7.0f;   // y 24..30 inclusive

// The needle's two end detents sit on the OUTERMOST PRINTED LABELS - x = 22 and x = 84 in page
// pixels - rather than on the ends of the lit band, so a tuned station lines up with a printed
// frequency instead of floating between two of them. Expressed relative to the scale, because
// the tuner widget is sized to exactly that band.
constexpr float RadioDialFirstX = 22.0f - RadioScaleX;
constexpr float RadioDialLastX = 84.0f - RadioScaleX;

// One page pixel, the same weight as the printed tick marks the needle moves between.
constexpr float RadioNeedleWidth = 1.0f;

// The clickable head unit, from FUN_00451980's stored rectangle. Clicking anywhere on it tunes
// to the station nearest that point along the scale.
constexpr float RadioBodyX = 20.0f;
constexpr float RadioBodyY = 15.0f;
constexpr float RadioBodyWidth = 66.0f;    // 20..86
constexpr float RadioBodyHeight = 23.0f;   // 15..38

// The upscale reconstructed the three dial faces about one-and-a-half page pixels below the
// original bitmap centres. Keep the original decoded gauge geometry and compensate only while
// the replacement panel is active.
constexpr float UpscaledGaugeNeedleYOffset = 1.5f;
const FVector2D UpscaledAirspeedNeedleOffset(1.0f, 1.0f);

const FLinearColor ReadoutInk(1.0f, 0.86f, 0.42f, 1.0f);

// Screen pixels, deliberately not scaled with the art. Keep the decoded well and its vertical
// placement unchanged; the slightly smaller face leaves four-digit balances clear of its frame.
constexpr int32 MoneyFontSize = 18;

FSlateFontInfo DashFont(const int32 Size)
{
	return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
}

// A gauge-needle image rotated around the artwork's authored pivot: 24 pixels from the left and
// 68 pixels above the bottom. BuildDash6 places that exact pivot on each decoded gauge centre.
class SSimCopterGaugeNeedle : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterGaugeNeedle)
		: _Image(nullptr)
	{}
		// Standard maths convention: 0 is to the right, positive is anticlockwise.
		SLATE_ATTRIBUTE(float, AngleDegrees)
		SLATE_ARGUMENT(const FSlateBrush*, Image)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		AngleDegrees = InArgs._AngleDegrees;
		Image = InArgs._Image;
		SetCanTick(false);
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override
	{
		if (Image == nullptr || Image->DrawAs == ESlateBrushDrawType::NoDrawType)
		{
			return LayerId;
		}

		const FVector2D Size = AllottedGeometry.GetLocalSize();
		const FVector2f RotationPoint(
			static_cast<float>(Size.X) * GaugeNeedlePivotFromLeft / GaugeNeedleSourceWidth,
			static_cast<float>(Size.Y) * GaugeNeedlePivotFromTop / GaugeNeedleSourceHeight);

		// The source artwork points straight up. Slate's positive rotation is clockwise in screen
		// space, so 90-angle maps the existing mathematical gauge angle onto the image.
		const float RotationRadians =
			FMath::DegreesToRadians(90.0f - AngleDegrees.Get(0.0f));
		const ESlateDrawEffect DrawEffects =
			bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
		const FLinearColor Tint =
			InWidgetStyle.GetColorAndOpacityTint() * Image->GetTint(InWidgetStyle);

		FSlateDrawElement::MakeRotatedBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			Image,
			DrawEffects,
			RotationRadians,
			RotationPoint,
			FSlateDrawElement::RelativeToElement,
			Tint);
		return LayerId + 1;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(GaugeNeedleSourceWidth, GaugeNeedleSourceHeight);
	}

private:
	TAttribute<float> AngleDegrees;
	const FSlateBrush* Image = nullptr;
};

/**
 * The radio tuner: paints the station needle over the printed scale and takes the click that
 * tunes it. It is sized to the lit scale exactly, so the click position maps straight onto the
 * dial without the widget needing to know anything about the dashboard's layout or scaling.
 */
class SSimCopterRadioTuner : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterRadioTuner) {}
		SLATE_ARGUMENT(TWeakObjectPtr<USimCopterRadioSubsystem>, Radio)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Radio = InArgs._Radio;
		SetCanTick(false);

		SetToolTipText(TAttribute<FText>::CreateSPLambda(this, [this]()
		{
			const USimCopterRadioSubsystem* RadioPtr = Radio.Get();
			if (RadioPtr != nullptr && !RadioPtr->IsPowered())
			{
				return NSLOCTEXT("SimCopterDashboard", "RadioToolTipOff", "Radio (Right-click to turn on)");
			}
			return NSLOCTEXT("SimCopterDashboard", "RadioToolTipOn", "Radio (Right-click to turn off)");
		}));
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override
	{
		USimCopterRadioSubsystem* RadioPtr = Radio.Get();
		if (RadioPtr == nullptr || RadioPtr->GetStationCount() == 0)
		{
			return LayerId;
		}

		const FVector2D Size = AllottedGeometry.GetLocalSize();
		const float PixelsPerPage = static_cast<float>(Size.X) / FMath::Max(RadioScaleWidth, 1.0f);
		const float NeedleX = PixelsPerPage *
			(RadioDialFirstX + RadioPtr->GetDialAlpha() * (RadioDialLastX - RadioDialFirstX));
		const float NeedleWidth = FMath::Max(1.0f, RadioNeedleWidth * PixelsPerPage);

		// Dimmed when the set is off, so the dial still reads as tuned to something.
		const FLinearColor Tint = RadioPtr->IsPowered()
			? FLinearColor(1.0f, 0.16f, 0.12f, 1.0f)
			: FLinearColor(0.45f, 0.12f, 0.10f, 0.65f);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(NeedleWidth, static_cast<float>(Size.Y)),
				FSlateLayoutTransform(FVector2f(NeedleX - NeedleWidth * 0.5f, 0.0f))),
			FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),
			bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
			Tint * InWidgetStyle.GetColorAndOpacityTint());

		return LayerId + 1;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(RadioScaleWidth, RadioScaleHeight);
	}

	virtual bool SupportsKeyboardFocus() const override { return false; }

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		USimCopterRadioSubsystem* RadioPtr = Radio.Get();
		if (RadioPtr == nullptr || RadioPtr->GetStationCount() == 0)
		{
			return FReply::Unhandled();
		}

		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			// Right-click is the power switch: the original's radio has an on/off of its own
			// (FUN_004309c0 takes a plain on/off), and the dial is the only radio surface here.
			RadioPtr->SetPowered(!RadioPtr->IsPowered());
			return FReply::Handled();
		}

		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}

		const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const float Width = FMath::Max(static_cast<float>(MyGeometry.GetLocalSize().X), 1.0f);
		const float PixelsPerPage = Width / FMath::Max(RadioScaleWidth, 1.0f);
		// Inverse of the paint mapping: page pixels back onto the first..last detent span.
		const float PageX = static_cast<float>(Local.X) / FMath::Max(PixelsPerPage, KINDA_SMALL_NUMBER);
		const float Alpha =
			(PageX - RadioDialFirstX) / FMath::Max(RadioDialLastX - RadioDialFirstX, 1.0f);

		const int32 Station = RadioPtr->GetStationForDialAlpha(Alpha);
		if (Station != INDEX_NONE)
		{
			RadioPtr->SetPowered(true);
			RadioPtr->SetStationIndex(Station);
		}
		return FReply::Handled();
	}

	virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override
	{
		return FCursorReply::Cursor(EMouseCursor::Hand);
	}

private:
	TWeakObjectPtr<USimCopterRadioSubsystem> Radio;
};
}

void SSimCopterDashboard::Construct(const FArguments& InArgs)
{
	Pawn = InArgs._Pawn;
	Art = InArgs._Art;
	Scale = FMath::Max(0.5f, InArgs._Scale);
	HudScale = FMath::Max(0.1f, InArgs._HudScale);

	const FSlateBrush* UpscaledDashboardBrush = nullptr;
	if (USimCopterHangarArt* ArtObject = Art.Get())
	{
		UpscaledDashboardBrush = ArtObject->GetBundledSlateImage(UpscaledDashboardFile);
		bUseUpscaledAltimeterArt =
			ArtObject->GetBundledSlateImage(UpscaledAltimeterDigitFile) != nullptr;
	}
	bUseUpscaledDashboardArt = UpscaledDashboardBrush != nullptr;

	TSharedRef<SVerticalBox> InstrumentLayers =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
		[
			BuildDash4()
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
		[
			BuildDash6()
		];

	TSharedRef<SWidget> InstrumentPanel = InstrumentLayers;
	if (UpscaledDashboardBrush != nullptr)
	{
		// The PNG has much more source detail, but it occupies the exact same 458x125 page-space
		// rectangle as dash4 stacked over dash6. The transparent live layers stay at their old
		// coordinates, so readouts and needles continue to line up with the replacement art.
		InstrumentPanel =
			SNew(SBox)
			.WidthOverride(DashboardWidth * Scale)
			.HeightOverride(DashboardHeight * Scale)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SImage).Image(UpscaledDashboardBrush)
				]
				+ SOverlay::Slot()
				[
					InstrumentLayers
				]
			];
	}

	ChildSlot
	[
		SNew(SHorizontalBox)

		// The seat window sits to the left of the instruments, sharing their bottom edge.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
		[
			BuildSeatWindow()
		]

		// dash4 has to sit directly on dash6, so the two live in their own column - putting
		// dash4 above the whole row instead would leave it floating over the taller seat window.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
		[
			InstrumentPanel
		]
	];
}

ASimCopterMissionSystemActor* SSimCopterDashboard::GetMissionSystem() const
{
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	if (Helicopter == nullptr || Helicopter->GetWorld() == nullptr)
	{
		return nullptr;
	}
	for (TActorIterator<ASimCopterMissionSystemActor> It(Helicopter->GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

const FSlateBrush* SSimCopterDashboard::GetBrush(
	const TCHAR* FileName,
	const FIntRect& Source,
	const bool bColorKeyed) const
{
	USimCopterHangarArt* ArtObject = Art.Get();
	if (ArtObject == nullptr)
	{
		return nullptr;
	}
	return ArtObject->GetSubImage(FileName, Source, bColorKeyed);
}

TSharedRef<SWidget> SSimCopterDashboard::MakeImage(
	const TCHAR* FileName,
	const FIntRect& Source,
	const bool bColorKeyed) const
{
	const FSlateBrush* Brush = GetBrush(FileName, Source, bColorKeyed);
	if (Brush == nullptr)
	{
		return SNullWidget::NullWidget;
	}
	return SNew(SImage).Image(Brush);
}

void SSimCopterDashboard::AddAtPage(
	SConstraintCanvas& Canvas,
	const float X,
	const float Y,
	const float Width,
	const float Height,
	TSharedRef<SWidget> Content) const
{
	Canvas.AddSlot()
		.Offset(FMargin(X * Scale, Y * Scale, Width * Scale, Height * Scale))
		.Alignment(FVector2D::ZeroVector)
		[
			Content
		];
}

// --- seat window -------------------------------------------------------------------------------

TSharedRef<SWidget> SSimCopterDashboard::BuildSeatWindow()
{
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
	SeatCanvas = Canvas;

	TSharedRef<SBox> Window =
		SNew(SBox)
		.WidthOverride(static_cast<float>(SeatWindowWidth) * Scale)
		[
			// The well accepts a passenger drop and does nothing with it, which is how dragging a
			// portrait back into the seats cancels. Anything dropped outside is unhandled, and the
			// drag operation puts that passenger on the ground.
			SNew(SSimCopterSeatWell)
			[
				Canvas
			]
		];
	SeatWindowBox = Window;

	RebuildSeats();

	return Window;
}

void SSimCopterDashboard::RefreshSeats()
{
	RebuildSeats();
}

void SSimCopterDashboard::RebuildSeats()
{
	if (!SeatCanvas.IsValid() || !SeatWindowBox.IsValid())
	{
		return;
	}

	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	if (Helicopter == nullptr)
	{
		return;
	}

	const int32 Seats = FMath::Max(0, Helicopter->GetPassengerSeatCount());
	const int32 Rows = FMath::Clamp(FMath::DivideAndRoundUp(Seats, SeatsPerRow), 1, 3);
	const float PageWidth = static_cast<float>(SeatWindowWidth);
	const float PageHeight = SeatWellTop + Rows * SeatRowStride;

	// The dashboard's outer row is bottom-aligned. Updating this desired height therefore moves
	// the panel up by one row at 6 seats and another at 11 seats while keeping its bottom edge
	// beside the instrument panel.
	SeatWindowBox->SetHeightOverride(
		TAttribute<FOptionalSize>(FOptionalSize(PageHeight * Scale)));

	// Capacity can change when the active helicopter model changes. Rebuild the frame as well as
	// the portraits so its crop and canvas extent follow the newly required row count.
	SeatCanvas->ClearChildren();

	const int32 SourceHeight = FMath::Clamp(FMath::CeilToInt(PageHeight), 1, SeatWindowHeight);
	TSharedRef<SWidget> WindowImage =
		MakeImage(SeatWindowFile, FIntRect(0, 0, SeatWindowWidth, SourceHeight), /*bColorKeyed=*/false);

	SeatWindowBrush.Reset();
	if (USimCopterHangarArt* ArtObject = Art.Get())
	{
		if (const FSlateBrush* Upscaled = ArtObject->GetBundledSlateImage(UpscaledSeatWindowFile))
		{
			TSharedRef<FSlateBrush> Cropped = MakeShared<FSlateBrush>(*Upscaled);
			Cropped->SetUVRegion(FBox2f(
				FVector2f(0.0f, 0.0f),
				FVector2f(1.0f, PageHeight / static_cast<float>(SeatWindowHeight))));
			Cropped->ImageSize = FVector2D(PageWidth * Scale, PageHeight * Scale);
			SeatWindowBrush = Cropped;
			WindowImage = SNew(SImage).Image(&Cropped.Get());
		}
	}

	AddAtPage(*SeatCanvas, 0.0f, 0.0f, PageWidth, PageHeight, WindowImage);

	const TArray<FSimCopterMissionPassengerSlot>& Slots = Helicopter->GetMissionPassengerSlots();

	const int32 Width = FSimCopterPopulationSprite::People1FrameWidth;
	const int32 Height = FSimCopterPopulationSprite::People1FrameHeight;

	for (int32 SeatIndex = 0; SeatIndex < Seats; ++SeatIndex)
	{
		int32 Column = PeopleEmptySeatColumn;
		int32 Row = 0;

		if (Slots.IsValidIndex(SeatIndex))
		{
			const FSimCopterMissionPassengerSlot& Slot = Slots[SeatIndex];

			// The face is the passenger's own head (person+0x18e, copied into the seat record when
			// they boarded), and the row is whatever opcode 54 last wrote. A medevac victim always
			// carries head 10, the bandaged one, so an injured passenger reads as injured; a
			// transport fare or an officer wears the head their behavior class was given.
			Column = FMath::Clamp(
				PeopleFirstFaceColumn + Slot.HeadImageIndex,
				PeopleFirstFaceColumn,
				PeopleColumns - 1);

			// BHAV 264 drives this: for a casualty, face 0 above 50 health, face 1 below it and
			// face 2 once they are gone. Everybody else follows the helicopter instead - face 1
			// while you crawl, face 0 at a cruise, face 2 past the program's 250 threshold, which
			// a damaged machine reaches at a much lower real speed.
			Row = FMath::Clamp(Slot.PortraitState, 0, FSimCopterPopulationSprite::People1Rows - 1);
		}

		const FIntRect Source(Column * Width, Row * Height, (Column + 1) * Width, (Row + 1) * Height);

		// Placed at the cell's own 27x33, so the portrait keeps its aspect ratio - the old
		// placeholder squashed it into a 24x30 box.
		const int32 Column2 = SeatIndex % SeatsPerRow;
		const int32 SeatRow = SeatIndex / SeatsPerRow;
		const float PortraitX = SeatFirstPortraitX + Column2 * SeatPortraitStride;
		const float PortraitY = SeatWellTop + SeatRow * SeatRowStride;
		TSharedRef<SWidget> Portrait = MakeImage(TEXT("PEOPLE1.BMP"), Source);

		// An occupied seat can be dragged out to put that passenger down; an empty one is scenery.
		if (Slots.IsValidIndex(SeatIndex))
		{
			const TWeakObjectPtr<ASimCopterHelicopterPawn> WeakPawn = Pawn;
			AddAtPage(
				*SeatCanvas,
				PortraitX - 2.0f,
				PortraitY - 2.0f,
				static_cast<float>(Width) + 4.0f,
				static_cast<float>(Height) + 4.0f,
				SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
					.BorderBackgroundColor_Lambda([WeakPawn, SeatIndex]()
					{
						const ASimCopterHelicopterPawn* Helicopter = WeakPawn.Get();
						return Helicopter != nullptr &&
							Helicopter->IsPassengerSlotControllerSelected(SeatIndex)
								? FLinearColor(1.0f, 0.68f, 0.12f, 1.0f)
								: FLinearColor::Transparent;
					}));

			Portrait = SNew(SSimCopterSeatPortrait)
				.Pawn(Pawn)
				.SlotIndex(SeatIndex)
				.Portrait(nullptr)
				[
					Portrait
				];
		}

		AddAtPage(*SeatCanvas,
			PortraitX,
			PortraitY,
			static_cast<float>(Width),
			static_cast<float>(Height),
			Portrait);
	}

	BuiltSeatCount = Seats;
	BuiltPassengerCount = Slots.Num();
}

// --- dash6 -------------------------------------------------------------------------------------

TSharedRef<SWidget> SSimCopterDashboard::BuildDash6()
{
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	if (!bUseUpscaledDashboardArt)
	{
		AddAtPage(*Canvas, 0.0f, 0.0f, static_cast<float>(Dash6Width), static_cast<float>(Dash6Height),
			MakeImage(Dash6File, FIntRect(0, 0, Dash6Width, Dash6Height), /*bColorKeyed=*/false));
	}

	// Money, in the rect FUN_004521a0 sets aside for it. The original low-resolution page needed
	// the larger Slate font lifted slightly; the reconstructed well sits lower and a touch left.
	const float MoneyTextX = MoneyRect.Min.X
		+ (bUseUpscaledDashboardArt ? UpscaledMoneyTextXOffset : 0.0f);
	const float MoneyTextY = MoneyRect.Min.Y
		+ (bUseUpscaledDashboardArt ? UpscaledMoneyTextYOffset : -2.0f);
	AddAtPage(*Canvas,
		MoneyTextX, MoneyTextY,
		static_cast<float>(MoneyRect.Width()), static_cast<float>(MoneyRect.Height()),
		SNew(STextBlock)
		.Text(this, &SSimCopterDashboard::GetMoneyText)
		.Justification(ETextJustify::Right)
		.ColorAndOpacity(ReadoutInk)
		.Font(DashFont(FMath::Max(1, FMath::RoundToInt(MoneyFontSize * HudScale)))));

	// The points meter: fifteen cells, each showing one of managge.bmp's three states as the
	// score climbs towards the city's requirement.
	PointsCellBrushes[0] = GetBrush(PointsBarFile,
		SimCopterSegmentedBar::GetCellFrame(
			SimCopterSegmentedBar::ECell::Full, PointsCellWidth, PointsCellHeight));
	PointsCellBrushes[1] = GetBrush(PointsBarFile,
		SimCopterSegmentedBar::GetCellFrame(
			SimCopterSegmentedBar::ECell::LeadingEdge, PointsCellWidth, PointsCellHeight));
	PointsCellBrushes[2] = GetBrush(PointsBarFile,
		SimCopterSegmentedBar::GetCellFrame(
			SimCopterSegmentedBar::ECell::Empty, PointsCellWidth, PointsCellHeight));
	if (PointsCellBrushes[0] != nullptr)
	{
		for (int32 Cell = 0; Cell < PointsCellCount; ++Cell)
		{
			AddAtPage(*Canvas,
				PointsBarX + Cell * PointsCellWidth,
				PointsBarY,
				static_cast<float>(PointsCellWidth),
				static_cast<float>(PointsCellHeight),
				SNew(SImage)
				.Image(TAttribute<const FSlateBrush*>::CreateSP(
					this, &SSimCopterDashboard::GetPointsCellBrush, Cell))
				.Visibility(EVisibility::HitTestInvisible));
		}
	}

	// The bar is fifteen coarse cells, so it can only ever say "roughly this far along". Hovering
	// gives the actual numbers. One hover target over the whole well rather than per cell - the
	// reading is the same wherever the pointer is inside it. Drawn last so it is the topmost
	// widget in the canvas and therefore the one hit testing finds.
	AddAtPage(*Canvas,
		PointsBarX,
		PointsBarY,
		static_cast<float>(PointsCellCount * PointsCellWidth),
		static_cast<float>(PointsCellHeight),
		SNew(SBorder)
		.BorderImage(FStyleDefaults::GetNoBrush())
		.ToolTipText(this, &SSimCopterDashboard::GetPointsToolTipText));

	// Six damage lamps, each showing one of damage.bmp's three frames.
	for (int32 Lamp = 0; Lamp < DamageLampCount; ++Lamp)
	{
		for (int32 Level = 1; Level <= 2; ++Level)
		{
			// The unlit frame is already printed on the page, so only the lit ones are overlaid.
			TSharedRef<SWidget> Image = MakeImage(
				DamageFile,
				FIntRect(Level * DamageFrameWidth, 0, (Level + 1) * DamageFrameWidth, DamageFrameHeight));
			const int32 CapturedLamp = Lamp;
			const int32 CapturedLevel = Level;
			Image->SetVisibility(TAttribute<EVisibility>::CreateSPLambda(
				this,
				[this, CapturedLamp, CapturedLevel]()
				{
					return GetDamageLampLevel(CapturedLamp) == CapturedLevel
						? EVisibility::HitTestInvisible
						: EVisibility::Hidden;
				}));
			AddAtPage(*Canvas, DamageLampX[Lamp], DamageLampY,
				static_cast<float>(DamageFrameWidth), static_cast<float>(DamageFrameHeight), Image);
		}
	}

	USimCopterHangarArt* ArtObject = Art.Get();
	const FSlateBrush* GaugeNeedleBrush =
		ArtObject != nullptr ? ArtObject->GetBundledSlateImage(GaugeNeedleFile) : nullptr;

	// Scale the artwork independently for each dial so its centre-to-tip length reaches that
	// gauge's decoded radius, then place the authored pivot exactly on the gauge centre.
	auto AddNeedle = [this, &Canvas](
		const FGauge& Gauge,
		TAttribute<float> Angle,
		const FSlateBrush* NeedleBrush,
		const FVector2D UpscaledOffset = FVector2D::ZeroVector)
	{
		if (NeedleBrush == nullptr)
		{
			return;
		}

		const float NeedleYOffset = bUseUpscaledDashboardArt ? UpscaledGaugeNeedleYOffset : 0.0f;
		const FVector2D ArtOffset = bUseUpscaledDashboardArt ? UpscaledOffset : FVector2D::ZeroVector;
		const FVector2D GaugeCentre(
			Gauge.CentreX + ArtOffset.X,
			Gauge.CentreY + NeedleYOffset + ArtOffset.Y);
		const float NeedleArtScale = Gauge.Radius / GaugeNeedleTipLength;
		const FVector2D NeedleSize(
			GaugeNeedleSourceWidth * NeedleArtScale,
			GaugeNeedleSourceHeight * NeedleArtScale);
		const FVector2D PivotInNeedle(
			GaugeNeedlePivotFromLeft * NeedleArtScale,
			GaugeNeedlePivotFromTop * NeedleArtScale);

		AddAtPage(*Canvas,
			GaugeCentre.X - PivotInNeedle.X,
			GaugeCentre.Y - PivotInNeedle.Y,
			NeedleSize.X,
			NeedleSize.Y,
			SNew(SSimCopterGaugeNeedle)
			.AngleDegrees(Angle)
			.Image(NeedleBrush));
	};

	// Keep the full eleven-digit strip behind a clipped opening. It is added before the needles
	// so the altimeter needle crosses over the readout, as it would on the physical instrument.
	const float DigitWidth = bUseUpscaledAltimeterArt
		? UpscaledAltimeterDigitWidth
		: static_cast<float>(AltimeterDigitWidth);
	const float DigitY = bUseUpscaledAltimeterArt
		? UpscaledAltimeterDigitY
		: AltimeterDigitY;
	AddAtPage(*Canvas, AltimeterDigitX, DigitY,
		DigitWidth, static_cast<float>(AltimeterDigitHeight),
		SNew(SBox)
		.Clipping(EWidgetClipping::ClipToBounds)
		[
			SNew(SConstraintCanvas)
			+ SConstraintCanvas::Slot()
			.Offset(TAttribute<FMargin>::CreateSP(
				this, &SSimCopterDashboard::GetAltimeterRolloverOffset))
			.Alignment(FVector2D::ZeroVector)
			[
				SNew(SImage).Image(TAttribute<const FSlateBrush*>::CreateSP(
					this, &SSimCopterDashboard::GetAltimeterRolloverBrush))
			]
		]);

	AddNeedle(FuelGauge, TAttribute<float>::CreateSPLambda(this, [this]()
	{
		const ASimCopterHelicopterPawn* Helicopter = GetPawn();
		const float Percent = Helicopter != nullptr ? FMath::Clamp(Helicopter->GetFuelFraction(), 0.0f, 1.0f) * 100.0f : 0.0f;
		return FuelGauge.StartAngleDegrees - Percent * FuelGauge.DegreesPerUnit;
	}), GaugeNeedleBrush);

	AddNeedle(AltimeterGauge, TAttribute<float>::CreateSPLambda(this, [this]()
	{
		// 100 units of 3.6 degrees is one full turn of the face, and the rollover window counts
		// the turns.
		const float Within = FMath::Fmod(FMath::Max(0.0f, GetAltitudeUnits()), AltimeterUnitsPerTurn);
		return AltimeterGauge.StartAngleDegrees - Within * AltimeterGauge.DegreesPerUnit;
	}), GaugeNeedleBrush);

	AddNeedle(AirspeedGauge, TAttribute<float>::CreateSPLambda(this, [this]()
	{
		// 25 units of 14 degrees, ten on the face each: 250 lands at the top left with one
		// segment spare.
		const float Units = FMath::Clamp(GetAirspeedDialKnots() / 10.0f, 0.0f, 25.0f);
		return AirspeedGauge.StartAngleDegrees - Units * AirspeedGauge.DegreesPerUnit;
	}), GaugeNeedleBrush, UpscaledAirspeedNeedleOffset);

	return SNew(SBox)
		.WidthOverride(Dash6Width * Scale)
		.HeightOverride(Dash6Height * Scale)
		[
			Canvas
		];
}

// --- dash4 -------------------------------------------------------------------------------------

TSharedRef<SWidget> SSimCopterDashboard::BuildDash4()
{
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	if (!bUseUpscaledDashboardArt)
	{
		AddAtPage(*Canvas, 0.0f, 0.0f, static_cast<float>(Dash4Width), static_cast<float>(Dash4Height),
			MakeImage(Dash4File, FIntRect(0, 0, Dash4Width, Dash4Height)));
	}

	// The strip goes on top of the page, not under it: the compass window is painted as a solid
	// screen on dash4, not left as a hole, so anything drawn first is simply covered up.
	const float CompassX = CompassWindowX
		+ (bUseUpscaledDashboardArt ? UpscaledCompassWindowXOffset : 0.0f);
	const float CompassY = CompassWindowY
		+ (bUseUpscaledDashboardArt ? UpscaledCompassWindowYOffset : 0.0f);
	AddAtPage(*Canvas, CompassX, CompassY, CompassWindowWidth,
		static_cast<float>(CompassStripHeight),
		SNew(SBox)
		.Clipping(EWidgetClipping::ClipToBounds)
		[
			// Two copies a revolution apart. The scroll wraps into one revolution, so whichever
			// copy the window is over there is always strip under it and the seam never shows.
			SNew(SConstraintCanvas)
			+ SConstraintCanvas::Slot()
			.Offset(TAttribute<FMargin>::CreateSP(this, &SSimCopterDashboard::GetCompassSlotOffset))
			.Alignment(FVector2D::ZeroVector)
			[
				SNew(SImage).Image(TAttribute<const FSlateBrush*>::CreateSP(
					this, &SSimCopterDashboard::GetCompassBrush))
			]
			+ SConstraintCanvas::Slot()
			.Offset(TAttribute<FMargin>::CreateSP(this, &SSimCopterDashboard::GetCompassWrapSlotOffset))
			.Alignment(FVector2D::ZeroVector)
			[
				SNew(SImage).Image(TAttribute<const FSlateBrush*>::CreateSP(
					this, &SSimCopterDashboard::GetCompassBrush))
			]
		]);

	// The radio needle, over the printed scale. Sized to the lit band so the tuner widget's own
	// local space is the dial: it needs no knowledge of Scale or of where dash4 ended up.
	AddAtPage(*Canvas, RadioScaleX, RadioScaleY, RadioScaleWidth, RadioScaleHeight,
		SNew(SSimCopterRadioTuner).Radio(GetRadio()));

	return SNew(SBox)
		.WidthOverride(Dash4Width * Scale)
		.HeightOverride(Dash4Height * Scale)
		[
			Canvas
		];
}

USimCopterRadioSubsystem* SSimCopterDashboard::GetRadio() const
{
	const ASimCopterHelicopterPawn* PawnPtr = Pawn.Get();
	const UWorld* World = PawnPtr != nullptr ? PawnPtr->GetWorld() : nullptr;
	return World != nullptr ? World->GetSubsystem<USimCopterRadioSubsystem>() : nullptr;
}

// --- readouts ------------------------------------------------------------------------------------

FText SSimCopterDashboard::GetMoneyText() const
{
	const ASimCopterMissionSystemActor* Missions = GetMissionSystem();
	if (Missions == nullptr)
	{
		return FText::GetEmpty();
	}
	// The original prints its readouts as plain integers, no thousands separator.
	return FText::FromString(FString::Printf(TEXT("$%d"), Missions->GetSessionCash()));
}

// The repaint's `score * 15 / pointsNeeded`. The original clamps the score to the requirement
// before dividing, so a city already earned reads as a full bar rather than overflowing it.
int32 SSimCopterDashboard::GetPointsLevel() const
{
	const ASimCopterMissionSystemActor* Missions = GetMissionSystem();
	if (Missions == nullptr)
	{
		return 0;
	}
	if (!SimCopterMissionSession::HasPointsGoal(Missions->GetSessionMode()))
	{
		return 0;
	}

	SimCopterMissions::FSimCopterCareerCity City;
	if (!Missions->GetCareerCityInfo(Missions->GetSessionCareerCityIndex(), City))
	{
		return 0;
	}
	return SimCopterSegmentedBar::GetLevel(
		Missions->GetSessionScore(), City.PointsNeeded, PointsCellCount);
}

FText SSimCopterDashboard::GetPointsToolTipText() const
{
	const ASimCopterMissionSystemActor* Missions = GetMissionSystem();
	if (Missions == nullptr)
	{
		return FText::GetEmpty();
	}

	const int32 Score = Missions->GetSessionScore();

	// UserCityJobs adopts City0's scheduler tuning, so the existence of a career data record cannot
	// determine presentation. Its explicit sandbox mode owns no target and shows only the score.
	if (!SimCopterMissionSession::HasPointsGoal(Missions->GetSessionMode()))
	{
		return FText::Format(
			NSLOCTEXT("SimCopterDashboard", "PointsToolTipUserCity", "Points: {0}"),
			FText::AsNumber(Score));
	}

	SimCopterMissions::FSimCopterCareerCity City;
	if (!Missions->GetCareerCityInfo(Missions->GetSessionCareerCityIndex(), City) ||
		City.PointsNeeded <= 0)
	{
		return FText::Format(
			NSLOCTEXT("SimCopterDashboard", "PointsToolTipUserCity", "Points: {0}"),
			FText::AsNumber(Score));
	}

	return FText::Format(
		NSLOCTEXT("SimCopterDashboard", "PointsToolTipCareer", "Points: {0}/{1}"),
		FText::AsNumber(Score),
		FText::AsNumber(City.PointsNeeded));
}

const FSlateBrush* SSimCopterDashboard::GetPointsCellBrush(const int32 CellIndex) const
{
	const SimCopterSegmentedBar::ECell Cell =
		SimCopterSegmentedBar::GetCellState(CellIndex, GetPointsLevel());
	return PointsCellBrushes[static_cast<int32>(Cell)];
}

int32 SSimCopterDashboard::GetDamageLampLevel(const int32 LampIndex) const
{
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	if (Helicopter == nullptr)
	{
		return 0;
	}

	// The lamps fill left to right; the last two go red rather than amber, so a nearly wrecked
	// airframe reads at a glance.
	const float Lit = FMath::Clamp(Helicopter->GetDamageFraction(), 0.0f, 1.0f) * DamageLampCount;
	if (Lit < static_cast<float>(LampIndex + 1))
	{
		return 0;
	}
	return LampIndex >= DamageLampCount - 2 ? 2 : 1;
}

float SSimCopterDashboard::GetAltitudeUnits() const
{
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	return Helicopter != nullptr ? Helicopter->GetAltimeterUnits() : 0.0f;
}

float SSimCopterDashboard::GetAirspeedDialKnots() const
{
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	return Helicopter != nullptr ? Helicopter->GetAirspeedDialKnots() : 0.0f;
}

float SSimCopterDashboard::GetHeadingDegrees() const
{
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	if (Helicopter == nullptr)
	{
		return 0.0f;
	}
	return FRotator::ClampAxis(static_cast<float>(Helicopter->GetActorRotation().Yaw));
}

const FSlateBrush* SSimCopterDashboard::GetAltimeterRolloverBrush() const
{
	if (bUseUpscaledAltimeterArt)
	{
		if (USimCopterHangarArt* ArtObject = Art.Get())
		{
			if (const FSlateBrush* Upscaled =
				ArtObject->GetBundledSlateImage(UpscaledAltimeterDigitFile))
			{
				return Upscaled;
			}
		}
	}

	return GetBrush(
		AltimeterDigitFile,
		FIntRect(
			0,
			0,
			AltimeterDigitWidth,
			AltimeterDigitFrameCount * AltimeterDigitHeight));
}

FMargin SSimCopterDashboard::GetAltimeterRolloverOffset() const
{
	// The strip moves continuously with the needle instead of snapping at each complete turn.
	// Its repeated final zero lets the ninth digit roll smoothly back to zero near full scale.
	const float Turns = FMath::Clamp(
		FMath::Max(0.0f, GetAltitudeUnits()) / AltimeterUnitsPerTurn,
		0.0f,
		static_cast<float>(AltimeterDigitFrameCount - 1));
	const float DigitWidth = bUseUpscaledAltimeterArt
		? UpscaledAltimeterDigitWidth
		: static_cast<float>(AltimeterDigitWidth);
	const float FrameHeight = static_cast<float>(AltimeterDigitHeight);
	return FMargin(
		0.0f,
		-Turns * FrameHeight * Scale,
		DigitWidth * Scale,
		AltimeterDigitFrameCount * FrameHeight * Scale);
}

const FSlateBrush* SSimCopterDashboard::GetCompassBrush() const
{
	if (USimCopterHangarArt* ArtObject = Art.Get())
	{
		if (const FSlateBrush* Upscaled = ArtObject->GetBundledSlateImage(UpscaledCompassFile))
		{
			return Upscaled;
		}
	}
	return GetBrush(CompassFile, FIntRect(0, 0, CompassStripWidth, CompassStripHeight));
}

FMargin SSimCopterDashboard::GetCompassSlotOffset() const
{
	return MakeCompassSlotOffset(0.0f);
}

FMargin SSimCopterDashboard::GetCompassWrapSlotOffset() const
{
	return MakeCompassSlotOffset(CompassPixelsPerRevolution);
}

FMargin SSimCopterDashboard::MakeCompassSlotOffset(const float ExtraStripPixels) const
{
	// Slide the strip so the heading's mark sits under the middle of the window, wrapped into a
	// single revolution so the offset can never walk off the end of the bitmap.
	const float Travel = FMath::Fmod(
		GetHeadingDegrees() / 360.0f * CompassPixelsPerRevolution, CompassPixelsPerRevolution);
	const float StripX = CompassNorthCentre + Travel - ExtraStripPixels;
	const float Left = (CompassWindowWidth * 0.5f - StripX) * Scale;
	return FMargin(Left, 0.0f, CompassStripWidth * Scale, CompassStripHeight * Scale);
}
