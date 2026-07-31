// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ASimCopterHelicopterPawn;
class ASimCopterMissionSystemActor;
class SConstraintCanvas;
class USimCopterRadioSubsystem;
struct FSlateBrush;

// The bottom-right of the original's cockpit: the seat window, then the instrument panel.
//
// The remake uses a combined high-resolution image for the two instrument-panel pages while
// retaining their original 458x125 screen-space footprint. The seat window and every live
// overlay stay in original page pixels and are scaled once on the way out, like the tool flaps:
//
//   seatwin2.bmp  186x115  the seat window. One portrait per seat, cut from people1.bmp, whose
//                          leftmost 27x33 cell is the empty seat.
//   dashboard PNG 458x125  High-resolution replacement for dash4 above dash6.
//   dash6 region  458x82   FUN_004521a0. Money, the mission points meter, six damage lamps, the
//                          fuel gauge, the joystick, the altimeter and the airspeed dial.
//   dash4 region  455x43   FUN_00451980. The radio and scrolling compass window.
//
// The three gauge geometries are FUN_004521a0's own. Each writes a centre, a radius, a start
// angle and degrees-per-unit, and the needle runs *clockwise* from the start angle:
//
//   angle = StartAngle - Value * DegreesPerUnit
//   tip   = (cx + r*cos(angle), cy - r*sin(angle))
//
// which is what puts fuel 0 at the bottom left and 100 at the bottom right, and airspeed 250 at
// the top left with one segment left unused.
class SSimCopterDashboard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterDashboard)
		: _Scale(2.0f)
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<ASimCopterHelicopterPawn>, Pawn)
		SLATE_ARGUMENT(TWeakObjectPtr<USimCopterHangarArt>, Art)
		SLATE_ARGUMENT(float, Scale)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetPawn(TWeakObjectPtr<ASimCopterHelicopterPawn> InPawn) { Pawn = InPawn; }

private:
	TWeakObjectPtr<ASimCopterHelicopterPawn> Pawn;
	TWeakObjectPtr<USimCopterHangarArt> Art;
	float Scale = 2.0f;
	bool bUseUpscaledDashboardArt = false;
	bool bUseUpscaledAltimeterArt = false;

	// Rebuilt whenever the seat count or the passengers change, so the window is only laid out
	// when it has to be.
	int32 BuiltSeatCount = -1;
	int32 BuiltPassengerCount = -1;
	TSharedPtr<SConstraintCanvas> SeatCanvas;
	TSharedPtr<class SBox> SeatWindowBox;

	// The fixed-width seat window uses a copy of the high-resolution brush with its bottom
	// cropped in UV space for helicopters that need fewer than three seat rows.
	TSharedPtr<FSlateBrush> SeatWindowBrush;

	ASimCopterHelicopterPawn* GetPawn() const { return Pawn.Get(); }
	ASimCopterMissionSystemActor* GetMissionSystem() const;

	const FSlateBrush* GetBrush(const TCHAR* FileName, const FIntRect& Source, bool bColorKeyed = true) const;
	TSharedRef<SWidget> MakeImage(const TCHAR* FileName, const FIntRect& Source, bool bColorKeyed = true) const;
	void AddAtPage(SConstraintCanvas& Canvas, float X, float Y, float Width, float Height, TSharedRef<SWidget> Content) const;

	TSharedRef<SWidget> BuildSeatWindow();
	TSharedRef<SWidget> BuildDash6();
	TSharedRef<SWidget> BuildDash4();

	void RebuildSeats();

	// --- live readouts ---

	FText GetMoneyText() const;
	float GetPointsFraction() const;
	int32 GetDamageLampLevel(int32 LampIndex) const;
	EVisibility GetPointsBlockVisibility(int32 BlockIndex) const;

	// Both in the original's world units, straight off the flight model.
	float GetAltitudeUnits() const;
	float GetAirspeedDialKnots() const;
	float GetHeadingDegrees() const;

	const FSlateBrush* GetAltimeterRolloverBrush() const;
	FMargin GetAltimeterRolloverOffset() const;
	// The world's radio, for the dash4 tuner needle. Null when there is no world or no station
	// folder was found, which the tuner paints as "not fitted" - nothing at all.
	USimCopterRadioSubsystem* GetRadio() const;

	const FSlateBrush* GetCompassBrush() const;
	// Where the compass strip sits inside its window, in screen pixels. Two copies are drawn a
	// revolution apart so the scroll wraps without a seam.
	FMargin GetCompassSlotOffset() const;
	FMargin GetCompassWrapSlotOffset() const;
	FMargin MakeCompassSlotOffset(float ExtraStripPixels) const;

public:
	// Re-lays the seat window after a passenger boards or leaves.
	void RefreshSeats();
};
