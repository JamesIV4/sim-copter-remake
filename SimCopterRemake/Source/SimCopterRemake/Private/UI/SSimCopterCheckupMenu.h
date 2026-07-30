// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/SimCopterCheckup.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ASimCopterHelicopterPawn;
class SSimCopterCheckupSlider;
class STextBlock;
class USimCopterHangarArt;
struct FButtonStyle;

// SCHOOK: CheckupDialog 0x00443c20
//
// The "Check-up" service panel the original raises when you set down on the airport: three
// vertical sliders (Damage, Fuel, Teargas), a live Total Cost against Funds, and OK/Cancel.
// FSimCopterCheckup owns every number; this widget only presents them.
namespace SimCopterCheckupLayout
{
// One control rectangle, exactly as FUN_00443c20 pushes it.
//
// The DECOMPILE of that function is useless for these - Ghidra aliases the same four stack slots
// across all sixteen controls, so its C output shows one rectangle reused - but the ASSEMBLY
// states each one outright, as four `MOV dword ptr [ESP + n], imm` stores immediately before the
// control is constructed. Every rectangle below was read out of that listing by
// `Docs/scratchpad/parse_checkup_rects.py` and then drawn back over the page art to confirm it
// lands on the printed furniture (`Docs/scratchpad/overlay_checkup_rects.py`). Watch for the
// frame shift: a PUSH between the stores bumps the later displacements by four, so the slots
// match up by address order, not by the literal offsets in the listing.
struct FCheckupRect
{
	float Left = 0.0f;
	float Top = 0.0f;
	float Right = 0.0f;
	float Bottom = 0.0f;

	constexpr float Width() const { return Right - Left; }
	constexpr float Height() const { return Bottom - Top; }
};

// CHECKUP.BMP's own pixel size; every coordinate below is in this space.
constexpr float PageWidth = 444.0f;
constexpr float PageHeight = 462.0f;

// The wide well across the TOP carries all four readouts on two lines: the title, then Funds and
// Total Cost side by side, each label immediately followed by its number. The recess along the
// bottom of the page is the button tray, not a readout.
constexpr FCheckupRect TitleRect      { 112.0f, 36.0f, 344.0f, 66.0f }; // string 590
constexpr FCheckupRect FundsLabelRect {  72.0f, 70.0f, 148.0f, 85.0f }; // string 591
constexpr FCheckupRect FundsValueRect { 154.0f, 70.0f, 206.0f, 85.0f };
constexpr FCheckupRect TotalLabelRect { 236.0f, 70.0f, 328.0f, 85.0f }; // string 592
constexpr FCheckupRect TotalValueRect { 332.0f, 70.0f, 380.0f, 85.0f };

// Control ids 3, 4, 5 in FUN_00443c20's order: Damage, Fuel, Teargas. All three tracks are the
// same 26x202, but the Fuel track is printed 68 px lower on the page than its neighbours and one
// track-width to the left of the panel's centre line - it is NOT the middle of a symmetrical row,
// which is what makes it the easy one to misplace.
constexpr int32 SliderCount = 3;
constexpr FCheckupRect SliderTrackRect[SliderCount] = {
	{  91.0f, 108.0f, 117.0f, 310.0f },
	{ 191.0f, 176.0f, 217.0f, 378.0f },
	{ 333.0f, 108.0f, 359.0f, 310.0f },
};

// The remake draws the 22x18 SLIDERTV thumb at 1.5x. The bitmap's visual indicator is not centred
// in FUN_00443c20's control rectangle, so the paint/hit rectangles put its midpoint 3.5 px right
// and 5.5 px above the decoded mathematical centreline. The travel endpoints are pulled inward
// by half of the rendered 27 px thumb at each end, so the printed line terminates at the thumb's
// red midpoint instead of its top or bottom edge.
constexpr float SliderThumbSourceWidth = 22.0f;
constexpr float SliderThumbSourceHeight = 18.0f;
constexpr float SliderThumbScale = 1.5f;
constexpr float SliderThumbVisualOffsetX = 3.5f;
constexpr float SliderThumbVisualOffsetY = -5.5f;
constexpr float SliderThumbHalfHeight = SliderThumbSourceHeight * SliderThumbScale * 0.5f;
constexpr FCheckupRect SliderControlRect[SliderCount] = {
	{  91.0f, 111.5f, 124.0f, 295.5f },
	{ 191.0f, 179.5f, 224.0f, 363.5f },
	{ 333.0f, 111.5f, 366.0f, 295.5f },
};

// Each slider's name plate and its live cost readout: below the outer two, above the middle one.
// These are visually tuned a few pixels below the decoded rectangles: the original's overlapping
// boxes crowded the two Slate baselines. Fuel moves down farther than the outer pair because its
// label sat visibly too high in the upper plate.
constexpr FCheckupRect SliderLabelRect[SliderCount] = {
	{  52.0f, 331.0f, 154.0f, 351.0f }, // string 593 Damage
	{ 147.0f, 114.0f, 268.0f, 134.0f }, // string 594 Fuel
	{ 287.0f, 331.0f, 405.0f, 351.0f }, // string 595 Teargas
};
constexpr FCheckupRect SliderValueRect[SliderCount] = {
	{  64.0f, 350.0f, 147.0f, 367.0f },
	{ 167.0f, 133.0f, 255.0f, 149.0f },
	{ 305.0f, 350.0f, 389.0f, 367.0f },
};

// FUN_00443c20 gives both buttons a degenerate 1x1 rectangle - (186,390)-(187,391) for OK and
// (288,390)-(289,391) for Cancel - because the control sizes itself from BUTTON.BMP's 100x28
// frames. Only the origin carries information, and it puts them side by side in the tray along
// the bottom of the page.
constexpr float ButtonWidth = 100.0f;
constexpr float ButtonHeight = 28.0f;
constexpr float ButtonY = 390.0f;
constexpr float OkButtonX = 186.0f;
constexpr float CancelButtonX = 288.0f;

// Every control is configured through three vtable slots after it is built: [vt+0xe0] sets the
// font size, [vt+0xe4] the justification and [vt+0xe8] a colour/font record. The title asks for
// 30 and everything else for 14. Those are Windows font heights - whole cell, internal leading
// included - so the Slate point sizes that fill the same boxes are roughly three quarters of
// them. The title's colour record differs from the body's; both live in uninitialised .data and
// are built at startup, so the actual faces cannot be recovered from the image, and the title is
// simply set bold here.
//
// [vt+0xe4] is called with 1 for every label and for the three slider cost readouts. The Funds
// and Total Cost numbers are the only two controls that never get the call, so they keep the
// control's default and sit hard against the left edge of their box, right after the label.
constexpr int32 TitleFontSize = 23;
constexpr int32 BodyFontSize = 11;
}

DECLARE_DELEGATE_OneParam(FOnSimCopterCheckupAccepted, FSimCopterCheckupOrder);

class SSimCopterCheckupMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterCheckupMenu) {}
		SLATE_ARGUMENT(FSimCopterCheckupState, State)
		SLATE_ARGUMENT(TObjectPtr<USimCopterHangarArt>, Art)
		SLATE_EVENT(FOnSimCopterCheckupAccepted, OnAccepted)
		SLATE_EVENT(FSimpleDelegate, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// Slider positions are normalised 0..1; these turn them into the original's units.
	FSimCopterCheckupOrder BuildOrder() const;
	void RefreshReadouts();

	TSharedRef<SWidget> BuildSlider(int32 Index);
	TSharedRef<SWidget> BuildButton(const FText& Label, FOnClicked OnClicked);

	FSimCopterCheckupState State;
	TObjectPtr<USimCopterHangarArt> Art;
	FOnSimCopterCheckupAccepted OnAccepted;
	FSimpleDelegate OnCancelled;

	// Index 0 = Damage (dollars), 1 = Fuel (dollars), 2 = Teargas (canisters).
	TSharedPtr<SSimCopterCheckupSlider> Sliders[SimCopterCheckupLayout::SliderCount];
	TSharedPtr<STextBlock> ValueTexts[SimCopterCheckupLayout::SliderCount];
	TSharedPtr<STextBlock> TotalCostText;
	TSharedPtr<STextBlock> FundsText;

	// BUTTON.BMP's three frames are wrapped in styles that have to outlive Construct.
	TArray<TSharedRef<FButtonStyle>> ButtonStyles;

	int32 SliderMaxima[SimCopterCheckupLayout::SliderCount] = { 0, 0, 0 };
};
