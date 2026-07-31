// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/SimCopterFlapLayout.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
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
// control, so they get a strip above the tools with the service selector and the two dispatch
// buttons.
//
// The artwork is 640x480-era pixel art, so it is placed at page coordinates multiplied by Scale
// and let up-filter. Text is not scaled with it - it is laid out in screen pixels at a readable
// size, which is why the label boxes below are sized in screen pixels rather than page ones.
class SSimCopterToolFlaps : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterToolFlaps)
		: _Scale(2.0f)
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<ASimCopterHelicopterPawn>, Pawn)
		SLATE_ARGUMENT(TWeakObjectPtr<USimCopterHangarArt>, Art)
		// Page pixels to screen pixels. The original ran at 640x480, where a flap was 138x58.
		SLATE_ARGUMENT(float, Scale)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Rebinds when possession changes so the flaps follow the controlled helicopter.
	void SetPawn(TWeakObjectPtr<ASimCopterHelicopterPawn> InPawn) { Pawn = InPawn; }

	// Each visible flap is its own marker obstacle, so tags can follow the stepped left edge of
	// the unequal-width dispatch/tool panels instead of being held off by one large column box.
	void AppendMissionMarkerAvoidanceWidgets(TArray<TSharedPtr<SWidget>>& OutWidgets) const;

private:
	// The dispatch strip's page width. Wider than a tool flap because it carries a service name
	// and two labelled buttons; its background is a tool flap's frame with the middle tiled out
	// to width (see BuildDispatchFlap).
	static constexpr float DispatchPageWidth = 232.0f;

	TWeakObjectPtr<ASimCopterHelicopterPawn> Pawn;
	TWeakObjectPtr<USimCopterHangarArt> Art;
	float Scale = 2.0f;

	// SButton holds its style by pointer, so the styles have to outlive Construct.
	TArray<TSharedPtr<FButtonStyle>> ButtonStyles;

	// The dispatch strip's tiled fill is a copy of a cached brush with tiling switched on, so
	// unlike every other brush here it is not owned by the art object.
	TSharedPtr<FSlateBrush> DispatchFillBrush;

	TSharedPtr<SMenuAnchor> MegaphoneMenu;
	TArray<TSharedPtr<SWidget>> MissionMarkerAvoidancePanels;

	ASimCopterHelicopterPawn* GetPawn() const { return Pawn.Get(); }

	// A canvas holding one panel, sized in screen pixels.
	TSharedRef<SWidget> MakePanel(float PageWidthUnits, TSharedRef<SConstraintCanvas> Canvas);

	// Places Content at page coordinates, scaled on the way in.
	void AddAtPage(SConstraintCanvas& Canvas, float X, float Y, float Width, float Height, TSharedRef<SWidget> Content) const;

	// Places Content at a page position but at an unscaled screen size, for text.
	void AddTextAtPage(SConstraintCanvas& Canvas, float CentreX, float CentreY, float ScreenWidth, float ScreenHeight, TSharedRef<SWidget> Content) const;

	TSharedRef<SWidget> BuildToolFlap(const SimCopterFlapLayout::FFlap& Flap);
	TSharedRef<SWidget> BuildDispatchFlap();

	// The invisible hit box plus the pressed sprite that lights under it. The unpressed button
	// is already painted on the page, so nothing is drawn until the player holds it down.
	void AddFlapButton(SConstraintCanvas& Canvas, const SimCopterFlapLayout::FButton& Button);

	// One piece of a bitmap as an image widget, or an empty widget when the art is missing.
	TSharedRef<SWidget> MakeImage(const TCHAR* FileName, const FIntRect& Source);

	const FSlateBrush* GetBrush(
		const TCHAR* FileName,
		const FIntRect& Source,
		ESimCopterArtRotation Rotation = ESimCopterArtRotation::None);

	TSharedRef<SWidget> MakeLabel(const FText& Text, int32 FontSize) const;

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
	FReply HandleDispatch(bool bChase);
	FText GetDispatchServiceText() const;
};
