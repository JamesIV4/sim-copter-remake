// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterDashboard.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Ground/SimCopterPopulationSprite.h"
#include "Missions/SimCopterMissionSystem.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
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

// The well starts at y 10 and runs to the bottom edge - there is no bottom frame - so the window
// is cut to whatever depth the seat rows need rather than always standing 115 tall. The biggest
// airframe carries fourteen, which is three rows of five.
constexpr float SeatWellTop = 10.0f;
constexpr int32 SeatsPerRow = 5;
constexpr float SeatFirstPortraitX = 13.0f;
constexpr float SeatPortraitStride = 29.0f;
constexpr float SeatRowStride = 34.0f;
constexpr float SeatRowPadding = 3.0f;

// people1.bmp is a 12x3 grid of 27x33 cells. Column 0 is the empty seat.
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

// The points bar is the second black well, measured at x 19..96, y 36..51. managge.bmp is a
// 15x13 block, so five of them fill it.
constexpr int32 PointsBlockCount = 5;
constexpr float PointsBlockX = 20.0f;
constexpr float PointsBlockY = 37.0f;
constexpr float PointsBlockStride = 15.0f;
const TCHAR* const PointsBlockFile = TEXT("MANAGGE.BMP");

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

// altnumbr.bmp is a vertical strip of eleven 8x9 digits (0..9 then 0 again) shown in the
// altimeter's little rollover window.
const TCHAR* const AltimeterDigitFile = TEXT("ALTNUMBR.BMP");
constexpr int32 AltimeterDigitWidth = 8;
constexpr int32 AltimeterDigitHeight = 9;
constexpr float AltimeterDigitX = 300.0f;
constexpr float AltimeterDigitY = 31.0f;

// One turn of the altimeter face is the 100 units its ten divisions cover at 3.6 degrees each,
// and the rollover window counts the turns, so the instrument reads 0..1000 world units end to
// end. That full scale lines up with the top of the fire-damage altitude band (6110cm, which is
// 978 units at 6.25cm per unit), which is the check that says these are world units and not
// feet - a foot-scaled face would barely leave its stop over the whole flight envelope.
constexpr float AltimeterUnitsPerTurn = 100.0f;

// The joystick well is the black square at (223,20)-(255,57); bootsqur.bmp is centred on it and
// boot.bmp rides on top.
const TCHAR* const JoystickBaseFile = TEXT("BOOTSQUR.BMP");
const TCHAR* const JoystickStickFile = TEXT("BOOT.BMP");
constexpr float JoystickCentreX = 239.0f;
constexpr float JoystickCentreY = 38.0f;
constexpr int32 JoystickBaseWidth = 51;
constexpr int32 JoystickBaseHeight = 64;

// --- dash4.bmp, 455x43 ---------------------------------------------------------------------
const TCHAR* const Dash4File = TEXT("DASH4.BMP");
constexpr int32 Dash4Width = 455;
constexpr int32 Dash4Height = 43;
constexpr int32 DashboardWidth = Dash6Width;
constexpr int32 DashboardHeight = Dash4Height + Dash6Height;

// compass1.bmp is 160x16 and reads NW N NE E SE S SW W NW N NE - eight points at 16px each, so
// one revolution is 128px and the remaining 32px is the wrap overlap.
const TCHAR* const CompassFile = TEXT("COMPASS1.BMP");
constexpr int32 CompassStripWidth = 160;
constexpr int32 CompassStripHeight = 16;
constexpr float CompassPixelsPerRevolution = 128.0f;
// Ten 16px cells: NW N NE E SE S SW W NW N. North is the second, so its glyph is centred one and
// a half cells in - not one, which drew the whole strip half a cell off and made north look like
// it spanned a wide arc as the second N came round.
constexpr float CompassNorthCentre = 24.0f;
constexpr float CompassWindowX = 402.0f;
constexpr float CompassWindowY = 11.0f;
constexpr float CompassWindowWidth = 37.0f;
constexpr float UpscaledCompassWindowXOffset = -3.0f;
constexpr float UpscaledCompassWindowYOffset = 3.0f;

// The upscale reconstructed the three dial faces about one-and-a-half page pixels below the
// original bitmap centres. Keep the original decoded gauge geometry and compensate only while
// the replacement panel is active.
constexpr float UpscaledGaugeNeedleYOffset = 1.5f;
const FVector2D UpscaledAirspeedNeedleOffset(1.0f, 1.0f);

const FLinearColor ReadoutInk(1.0f, 0.86f, 0.42f, 1.0f);

// Screen pixels, deliberately not scaled with the art: the money well is 74x14 page pixels, which
// is a lot of room once the panel is up-filtered.
constexpr int32 MoneyFontSize = 20;

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
}

void SSimCopterDashboard::Construct(const FArguments& InArgs)
{
	Pawn = InArgs._Pawn;
	Art = InArgs._Art;
	Scale = FMath::Max(0.5f, InArgs._Scale);

	const FSlateBrush* UpscaledDashboardBrush = nullptr;
	if (USimCopterHangarArt* ArtObject = Art.Get())
	{
		UpscaledDashboardBrush = ArtObject->GetBundledSlateImage(UpscaledDashboardFile);
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
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	const int32 Seats = Helicopter != nullptr ? FMath::Max(0, Helicopter->GetPassengerSeatCount()) : 0;

	const int32 Rows = FMath::Max(1, FMath::DivideAndRoundUp(Seats, SeatsPerRow));

	// SEATWIN2 always keeps its original width. Only its open-bottomed depth follows the number
	// of seat rows: one row for smaller helicopters and all three for the fourteen-seater.
	const float PageWidth = static_cast<float>(SeatWindowWidth);
	const float PageHeight = SeatWellTop + Rows * SeatRowStride + SeatRowPadding;

	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
	SeatCanvas = Canvas;

	const int32 SourceHeight = FMath::Clamp(FMath::CeilToInt(PageHeight), 1, SeatWindowHeight);
	TSharedRef<SWidget> WindowImage =
		MakeImage(SeatWindowFile, FIntRect(0, 0, SeatWindowWidth, SourceHeight), /*bColorKeyed=*/false);

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

	AddAtPage(*Canvas, 0.0f, 0.0f, PageWidth, PageHeight, WindowImage);

	RebuildSeats();

	return SNew(SBox)
		.WidthOverride(PageWidth * Scale)
		.HeightOverride(PageHeight * Scale)
		[
			Canvas
		];
}

void SSimCopterDashboard::RefreshSeats()
{
	RebuildSeats();
}

void SSimCopterDashboard::RebuildSeats() const
{
	if (!SeatCanvas.IsValid())
	{
		return;
	}

	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	if (Helicopter == nullptr)
	{
		return;
	}

	const int32 Seats = FMath::Max(0, Helicopter->GetPassengerSeatCount());

	// Only the portraits are rebuilt; the window frame slot stays where BuildSeatWindow put it.
	const int32 FrameSlotCount = 1;
	while (SeatCanvas->GetChildren()->Num() > FrameSlotCount)
	{
		SeatCanvas->RemoveSlot(SeatCanvas->GetChildren()->GetChildAt(SeatCanvas->GetChildren()->Num() - 1));
	}

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

			// One face per passenger, stable for as long as they are aboard: the seat has to keep
			// showing the same person, so the choice is derived from the job they came off rather
			// than drawn fresh each frame.
			const uint32 Mix = static_cast<uint32>(Slot.EventId * 2654435761u + SeatIndex * 40503u);
			Column = PeopleFirstFaceColumn + static_cast<int32>(Mix % (PeopleColumns - PeopleFirstFaceColumn));

			// Row 2 of the sheet is the distressed set - the right look for someone being lifted
			// out of trouble. A transport fare gets the calm row.
			Row = Slot.Kind == ESimCopterMissionPassengerKind::Transport ? 0 : 2;
		}

		const FIntRect Source(Column * Width, Row * Height, (Column + 1) * Width, (Row + 1) * Height);

		// Placed at the cell's own 27x33, so the portrait keeps its aspect ratio - the old
		// placeholder squashed it into a 24x30 box.
		const int32 Column2 = SeatIndex % SeatsPerRow;
		const int32 SeatRow = SeatIndex / SeatsPerRow;
		AddAtPage(*SeatCanvas,
			SeatFirstPortraitX + Column2 * SeatPortraitStride,
			SeatWellTop + SeatRow * SeatRowStride,
			static_cast<float>(Width),
			static_cast<float>(Height),
			MakeImage(TEXT("PEOPLE1.BMP"), Source));
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
		.Font(DashFont(MoneyFontSize)));

	// The points meter: five blocks that light as the score approaches the city's requirement.
	for (int32 Block = 0; Block < PointsBlockCount; ++Block)
	{
		TSharedRef<SWidget> Image = MakeImage(PointsBlockFile, FIntRect(0, 0, 15, 13));
		Image->SetVisibility(TAttribute<EVisibility>::CreateSP(
			this, &SSimCopterDashboard::GetPointsBlockVisibility, Block));
		AddAtPage(*Canvas, PointsBlockX + Block * PointsBlockStride, PointsBlockY, 15.0f, 13.0f, Image);
	}

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

	// The joystick, centred on its well.
	AddAtPage(*Canvas,
		JoystickCentreX - JoystickBaseWidth * 0.5f,
		JoystickCentreY - JoystickBaseHeight * 0.5f,
		static_cast<float>(JoystickBaseWidth),
		static_cast<float>(JoystickBaseHeight),
		MakeImage(JoystickBaseFile, FIntRect(0, 0, JoystickBaseWidth, JoystickBaseHeight)));

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

	// The altimeter's rollover window, counting thousands of feet.
	AddAtPage(*Canvas, AltimeterDigitX, AltimeterDigitY,
		static_cast<float>(AltimeterDigitWidth), static_cast<float>(AltimeterDigitHeight),
		SNew(SImage).Image(TAttribute<const FSlateBrush*>::CreateSP(
			this, &SSimCopterDashboard::GetAltimeterRolloverBrush)));

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

	return SNew(SBox)
		.WidthOverride(Dash4Width * Scale)
		.HeightOverride(Dash4Height * Scale)
		[
			Canvas
		];
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

float SSimCopterDashboard::GetPointsFraction() const
{
	const ASimCopterMissionSystemActor* Missions = GetMissionSystem();
	if (Missions == nullptr)
	{
		return 0.0f;
	}

	SimCopterMissions::FSimCopterCareerCity City;
	if (!Missions->GetCareerCityInfo(Missions->GetSessionCareerCityIndex(), City) || City.PointsNeeded <= 0)
	{
		return 0.0f;
	}
	return FMath::Clamp(static_cast<float>(Missions->GetSessionScore()) / static_cast<float>(City.PointsNeeded), 0.0f, 1.0f);
}

EVisibility SSimCopterDashboard::GetPointsBlockVisibility(const int32 BlockIndex) const
{
	const float Filled = GetPointsFraction() * PointsBlockCount;
	return Filled >= static_cast<float>(BlockIndex + 1) ? EVisibility::HitTestInvisible : EVisibility::Hidden;
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
	const int32 Turns = FMath::Clamp(
		FMath::FloorToInt(FMath::Max(0.0f, GetAltitudeUnits()) / AltimeterUnitsPerTurn), 0, 9);
	return GetBrush(
		AltimeterDigitFile,
		FIntRect(0, Turns * AltimeterDigitHeight, AltimeterDigitWidth, (Turns + 1) * AltimeterDigitHeight));
}

const FSlateBrush* SSimCopterDashboard::GetCompassBrush() const
{
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
