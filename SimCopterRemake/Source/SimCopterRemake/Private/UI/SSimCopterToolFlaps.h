// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/SimCopterFlapLayout.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SCompoundWidget.h"

class ASimCopterHelicopterPawn;
class SButton;
class SConstraintCanvas;
class SMenuAnchor;
struct FButtonStyle;
struct FSlateBrush;

// The cockpit's right-hand column of control flaps, drawn from the original's own artwork.
//
// One flap per tool aboard (SimCopterFlapLayout has the decode), plus a dispatch flap the
// original does not have: the remake's emergency services are on F2-F5 with no on-screen
// control, so they get a strip above the tools with the service selector plus dispatch and clear
// buttons.
//
// The artwork is 640x480-era pixel art, so it is placed at page coordinates multiplied by Scale
// and let up-filter. Text is not scaled with it - it is laid out in screen pixels at a readable
// size, which is why the label boxes below are sized in screen pixels rather than page ones.
class SSimCopterToolFlaps : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterToolFlaps)
		: _Scale(1.768292f)
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<ASimCopterHelicopterPawn>, Pawn)
		SLATE_ARGUMENT(TWeakObjectPtr<USimCopterHangarArt>, Art)
		// Page pixels to screen pixels. The original ran at 640x480, where a flap was 138x58.
		SLATE_ARGUMENT(float, Scale)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SSimCopterToolFlaps() override;

	// Calibration mode: Ctrl+Alt+M toggles draggable element overlays & outlines.
	void ToggleCalibrationMode();
	bool IsCalibrationMode() const { return bCalibrationMode; }

	static FVector2D GetAuthoritativeDefaultOffset(const FString& Key);
	static FVector2D GetAuthoritativeDefaultScale(const FString& Key);

	FVector2D GetElementOffset(const FString& Key) const;
	void SetElementOffset(const FString& Key, const FVector2D& Offset);
	FVector2D GetElementScale(const FString& Key) const;
	void SetElementScale(const FString& Key, const FVector2D& ElementScale);
	void SetScale(float NewScale);
	float GetScale() const { return Scale; }

	FString GetSelectedCalibrationKey() const { return SelectedCalibrationKey; }
	void SetSelectedCalibrationKey(const FString& Key);
	void CycleSelectedCalibrationKey(int32 Direction);
	void NudgeSelectedElement(float DeltaX, float DeltaY, float DeltaScaleX, float DeltaScaleY);
	void ResetSelectedElement();

	FText GetSelectedControlText() const;
	FText GetSelectedXText() const;
	FText GetSelectedYText() const;
	FText GetSelectedScaleXText() const;
	FText GetSelectedScaleYText() const;
	FText GetGlobalScaleText() const;

	void LoadCalibrationData();
	void SaveCalibrationData() const;

	// The calibration control panel, built on demand for whoever hosts it. It is deliberately not
	// part of this widget's own layout: the flap column is right-aligned and one flap wide, so a
	// panel inside it renders over the flaps. The pawn puts it in its own top-centre viewport layer
	// (see ASimCopterHelicopterPawn::EnsureToolFlapsWidget) and keeps this widget alive for as long
	// as the panel is up - the panel's delegates bind to this.
	//
	// It collapses itself whenever calibration mode is off, so the host needs no visibility logic.
	TSharedRef<SWidget> BuildCalibrationDebugPanel();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	// Rebinds when possession changes so the flaps follow the controlled helicopter.
	void SetPawn(TWeakObjectPtr<ASimCopterHelicopterPawn> InPawn) { Pawn = InPawn; }

	// Each visible flap is its own marker obstacle, so tags can follow the stepped left edge of
	// the unequal-width dispatch/tool panels instead of being held off by one large column box.
	void AppendMissionMarkerAvoidanceWidgets(TArray<TSharedPtr<SWidget>>& OutWidgets) const;

private:
	// The dispatch strip's page width. Wider than a tool flap to carry full artwork and controls.
	static constexpr float DispatchPageWidth = 232.0f;

	// The Apache armament strip's page width.
	static constexpr float ApachePageWidth = 138.0f;
	static constexpr float MissileButtonInsetFromRight = 106.0f;
	static constexpr float GunButtonInsetFromRight = 64.0f;

	TWeakObjectPtr<ASimCopterHelicopterPawn> Pawn;
	TWeakObjectPtr<USimCopterHangarArt> Art;
	float Scale = 1.768292f;

	// SButton holds its style by pointer, so the styles have to outlive Construct.
	TArray<TSharedPtr<FButtonStyle>> ButtonStyles;

	// The dispatch strip's tiled fill is a copy of a cached brush with tiling switched on, so
	// unlike every other brush here it is not owned by the art object.
	TSharedPtr<FSlateBrush> DispatchFillBrush;
	TSharedPtr<FSlateBrush> ApacheFillBrush;

	TSharedPtr<SMenuAnchor> MegaphoneMenu;
	TArray<TSharedPtr<SWidget>> MissionMarkerAvoidancePanels;
	// The selector has the three service identities plus a fourth POLICE (CHASE) action. Keeping
	// that pseudo-entry here avoids turning chase into a fake service in the decoded dispatch core.
	int32 SelectedDispatchEntry = 0;

	bool bCalibrationMode = false;
	FString SelectedCalibrationKey;
	TMap<FString, FVector2D> CalibrationOffsets;
	TMap<FString, FVector2D> CalibrationScales;
	TMap<FString, SConstraintCanvas::FSlot*> ElementCanvasSlots;
	TMap<FString, FVector4f> ElementDefaultBounds;
	TSharedPtr<class IInputProcessor> CalibrationInputProcessor;

	ASimCopterHelicopterPawn* GetPawn() const { return Pawn.Get(); }

	// A canvas holding one panel, sized in screen pixels.
	TSharedRef<SWidget> MakePanel(float PageWidthUnits, TSharedRef<SConstraintCanvas> Canvas);

	// Places Content at page coordinates, scaled on the way in.
	void AddAtPage(SConstraintCanvas& Canvas, float X, float Y, float Width, float Height, TSharedRef<SWidget> Content) const;

	// Places Content at page coordinates with a calibration key for position offset and outline dragging.
	void AddAtPageKey(
		SConstraintCanvas& Canvas,
		const FString& Key,
		float DefaultX,
		float DefaultY,
		float Width,
		float Height,
		TSharedRef<SWidget> Content);

	// Places Content at a page position but at an unscaled screen size, for text.
	void AddTextAtPage(SConstraintCanvas& Canvas, float CentreX, float CentreY, float ScreenWidth, float ScreenHeight, TSharedRef<SWidget> Content) const;

	TSharedRef<SWidget> BuildToolFlap(const SimCopterFlapLayout::FFlap& Flap);
	TSharedRef<SWidget> BuildDispatchFlap();
	TSharedRef<SWidget> BuildApacheFlap();

	// The shared background for the two strips the original has no artwork for.
	void AddStripBackground(
		SConstraintCanvas& Canvas,
		float PageWidthUnits,
		TSharedPtr<FSlateBrush>& InOutFillBrush);

	EVisibility GetApacheFlapVisibility() const;

	// The invisible hit box plus the pressed sprite that lights under it. The unpressed button
	// is already painted on the page, so nothing is drawn until the player holds it down.
	void AddFlapButton(SConstraintCanvas& Canvas, const SimCopterFlapLayout::FButton& Button);

	// flap3's ten canister lamps. Like the original's repaint (0x00455790) this only draws the
	// spent ones: the page already prints a full row.
	void AddCanisterCounter(SConstraintCanvas& Canvas);
	EVisibility GetCanisterLampVisibility(int32 LampIndex) const;

	// flap0's eleven-cell water meter (0x00455700). Every cell picks one of three sprites, so
	// unlike the lamps this one draws over the page rather than beside it.
	void AddWaterGauge(SConstraintCanvas& Canvas);
	const FSlateBrush* GetWaterGaugeCellBrush(int32 CellIndex) const;

	// The three watergge.bmp cells, indexed by SimCopterFlapLayout::WaterGauge::ECell.
	const FSlateBrush* WaterGaugeBrushes[3] = { nullptr, nullptr, nullptr };

	// One piece of a bitmap as an image widget, or an empty widget when the art is missing.
	TSharedRef<SWidget> MakeImage(const TCHAR* FileName, const FIntRect& Source);

	const FSlateBrush* GetBrush(
		const TCHAR* FileName,
		const FIntRect& Source,
		ESimCopterArtRotation Rotation = ESimCopterArtRotation::None);

	TSharedRef<SWidget> MakeLabel(const FText& Text, int32 FontSize) const;

	// The held variant, for a control that fires while the button is down.
	TSharedRef<SWidget> MakeHeldArtButton(
		const TCHAR* FileName,
		const FIntRect& NormalFrame,
		const FIntRect& PressedFrame,
		SimCopterFlapLayout::EAction Action,
		const FText& ToolTip);

	// A dispatch-strip button, drawn from a normal/pressed pair rather than over page art.
	TSharedRef<SWidget> MakeArtButton(
		const TCHAR* FileName,
		const FIntRect& NormalFrame,
		const FIntRect& PressedFrame,
		ESimCopterArtRotation Rotation,
		FOnClicked OnClicked,
		const FText& ToolTip);

	// --- flap visibility and enablement ---

	EVisibility GetFlapVisibility(int32 EquipmentMask) const;
	bool IsToolButtonEnabled(ESimCopterHelicopterTool Tool) const;

	// --- actions ---

	void PressAction(SimCopterFlapLayout::EAction Action);
	void ReleaseAction(SimCopterFlapLayout::EAction Action);

	TSharedRef<SWidget> BuildMegaphoneMenu();
	FReply HandleMegaphoneMessageChosen(ESimCopterMegaphoneMessage Message);

	FReply HandleDispatchServiceStep(int32 Delta);
	FReply HandleDispatch();
	FReply HandleDispatchClear();
	FText GetDispatchServiceText() const;
};
