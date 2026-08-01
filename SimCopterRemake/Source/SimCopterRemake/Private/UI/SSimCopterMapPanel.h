// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/SimCopterHangarArt.h"
#include "UI/SimCopterMapRaster.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ASimCopterHelicopterPawn;
class ASimCopterMissionSystemActor;
class ASimCopterTrafficSystemActor;
class SImage;
class STextBlock;
class UTexture2D;
struct FSlateBrush;

// The cockpit map: a high-resolution DASH5 replacement mapped to the original page rectangle,
// its rasterised 124x98 buffer, six mapbttn.bmp buttons and the current mission's name. Ported
// from the widget the original constructs at FUN_00454420, whose class vtable sits at 0x004f3068.
//
// The original sits this panel at screen (455,290)-(640,438) of its 640x480 cockpit, hard against
// the right edge and directly above the instrument strip along the bottom. The remake keeps that
// relationship rather than the absolute rectangle, because its own instrument panel is
// right-aligned.
//
// The raster runs on the original's cadence: FUN_004547a0 rebuilds the buffer on every *other*
// frame (`_DAT_004f9944 & 1`), which at the original's 0.05 s frame is ten passes a second. That
// rate is not cosmetic - the fire cells animate by one palette step per pass, so running it at
// the remake's frame rate would make burning tiles strobe.
class SSimCopterMapPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterMapPanel)
		: _Scale(2.0f)
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<ASimCopterHelicopterPawn>, Pawn)
		SLATE_ARGUMENT(TWeakObjectPtr<USimCopterHangarArt>, Art)
		SLATE_ARGUMENT(float, Scale)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetPawn(TWeakObjectPtr<ASimCopterHelicopterPawn> InPawn);

	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return false; }

	// The original's two zoom commands (0x1b and 0x1c in FUN_0044ac80's dispatch), so a key
	// binding reaches the same state the panel's own buttons change.
	void ZoomIn();
	void ZoomOut();

private:
	TWeakObjectPtr<ASimCopterHelicopterPawn> Pawn;
	TWeakObjectPtr<USimCopterHangarArt> Art;
	float Scale = 2.0f;

	SimCopterMap::FSimCopterMapRaster Raster;
	SimCopterMap::FSimCopterMapSettings Settings;

	// The city grids are static once a city is loaded, so they are gathered once and only the
	// burning tiles are refreshed per pass.
	SimCopterMap::FSimCopterMapCity CityData;
	TWeakObjectPtr<class ASimCity2000CityActor> CachedCityActor;

	SimCopterMap::FSimCopterMapIconSheet MissionIcons;
	SimCopterMap::FSimCopterMapIconSheet ServiceIcons;
	TArray<FColor> Palette;
	bool bArtLoaded = false;

	// Which mission the buttons cycle and the label names. The original keeps this as a pointer
	// in DAT_0057f9d8, which the mission layer also writes when a job is announced; here it is an
	// index into the mission system's record array, re-resolved by event id each pass so a record
	// shifting slots does not move the selection.
	int32 CurrentMissionEventId = INDEX_NONE;

	// Which button is held, and the click that started it. FUN_00454ad0 stores the index at +0x80
	// and FUN_00454c40 only fires when the release lands back inside the same rect.
	int32 PressedButton = INDEX_NONE;

	double NextRasterTime = 0.0;

	TSharedPtr<SImage> RasterImage;
	TSharedPtr<FSlateBrush> RasterBrush;
	TSharedPtr<SImage> ButtonImages[SimCopterMap::ButtonCount];
	TSharedPtr<STextBlock> MissionLabel;

	ASimCopterHelicopterPawn* GetPawn() const { return Pawn.Get(); }
	ASimCopterMissionSystemActor* GetMissionSystem() const;
	ASimCopterTrafficSystemActor* GetTrafficSystem() const;

	void LoadArt();
	void EnsureCityData();
	void RefreshFireTiles();

	// Assembles one FUN_004a28e0 pass's worth of world state.
	bool BuildFrame(SimCopterMap::FSimCopterMapFrame& OutFrame);
	void RunRasterPass();
	void RefreshButtonBrushes();
	void RefreshMissionLabel(const SimCopterMap::FSimCopterMapFrame& Frame);

	// FUN_00454ad0's dispatch, in panel pixels.
	void PressButton(int32 ButtonIndex);
	void ReleaseButton(int32 ButtonIndex, bool bInsideRect);
	void SelectPreviousMission();
	void SelectNextMission();

	const FSlateBrush* GetButtonBrush(int32 ButtonIndex, bool bPressed) const;
	// True when the button is a toggle that is currently on, so its face stays pressed.
	bool IsButtonLatched(int32 ButtonIndex) const;

	// Viewport point -> panel pixel, undoing the scale and the widget's own offset.
	bool TryGetPanelPoint(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FIntPoint& OutPoint) const;
};
