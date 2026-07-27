// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ASimCopterHelicopterPawn;
class ASimCopterMissionSystemActor;
class SConstraintCanvas;
struct FSlateBrush;

// The bottom-right of the original's cockpit: the seat window, then the instrument panel.
//
// Three shipped bitmaps make it up, all placed in their own page pixels and scaled once on the
// way out, exactly like the tool flaps:
//
//   seatwin2.bmp  186x115  the seat window. One portrait per seat, cut from people1.bmp, whose
//                          leftmost 27x33 cell is the empty seat.
//   dash6.bmp     458x82   FUN_004521a0. Money, the mission points meter, six damage lamps, the
//                          fuel gauge, the joystick, the altimeter and the airspeed dial.
//   dash4.bmp     455x43   FUN_00451980. Sits above dash6: the radio, and the compass window
//                          that compass1.bmp scrolls behind.
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

	// Rebuilt whenever the seat count or the passengers change, so the window is only laid out
	// when it has to be.
	mutable int32 BuiltSeatCount = -1;
	mutable int32 BuiltPassengerCount = -1;
	TSharedPtr<SConstraintCanvas> SeatCanvas;

	// The seat well's tiled fill is a copy of a cached brush with tiling switched on, so unlike
	// every other brush here it is not owned by the art object.
	TSharedPtr<FSlateBrush> SeatFillBrush;

	ASimCopterHelicopterPawn* GetPawn() const { return Pawn.Get(); }
	ASimCopterMissionSystemActor* GetMissionSystem() const;

	const FSlateBrush* GetBrush(const TCHAR* FileName, const FIntRect& Source, bool bColorKeyed = true) const;
	TSharedRef<SWidget> MakeImage(const TCHAR* FileName, const FIntRect& Source, bool bColorKeyed = true) const;
	void AddAtPage(SConstraintCanvas& Canvas, float X, float Y, float Width, float Height, TSharedRef<SWidget> Content) const;

	TSharedRef<SWidget> BuildSeatWindow();
	TSharedRef<SWidget> BuildDash6();
	TSharedRef<SWidget> BuildDash4();

	void RebuildSeats() const;

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
