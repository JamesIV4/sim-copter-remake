// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

struct FSlateBrush;

// SCHOOK: SliderControl 0x0040af00
//
// The original's slider control draws two bitmaps, a bar and a thumb, and takes the name of each
// as an argument. FUN_00443c20 builds the Check-up dialog's three sliders with SLIDCHK.BMP for
// the bar and a NULL thumb, so the thumb falls back to the constructor's built-in default: it
// picks SLIDERTH.BMP for a horizontal control and SLIDERTV.BMP for a vertical one, and all three
// of these are vertical. SLIDERTV.BMP is 22x18 - a grey metal cap with a red indicator stripe
// across its middle - and carries no colour key, so it is drawn opaque.
//
// SLIDCHK.BMP is deliberately NOT drawn: CHECKUP.BMP already prints the same recessed track at
// each of the three control rectangles, and the loose bitmap is 193 px tall against a 202 px
// rect, so drawing it over the page would only shift the rivets out of register.
//
// This is a hand-rolled widget rather than a styled SSlider because SSlider lays a vertical
// slider out as a horizontal one and then applies a -90 degree render transform to the result
// (SSlider.cpp, "we draw the slider like a horizontal slider regardless of the orientation").
// That rotates the thumb bitmap with everything else, which would stand the 22x18 cap on its end
// and run its red stripe vertically.
//
// Zero is at the BOTTOM of the track, as in the original.
class SSimCopterCheckupSlider : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterCheckupSlider)
		: _ThumbBrush(nullptr)
		, _Locked(false)
	{}
		// SLIDERTV.BMP, or null when the original artwork is not installed.
		SLATE_ARGUMENT(const FSlateBrush*, ThumbBrush)
		// FUN_00444690 leaves a slider with a zero maximum disabled - a full tank, an undamaged
		// airframe, or the tear-gas launcher not fitted.
		SLATE_ARGUMENT(bool, Locked)
		SLATE_EVENT(FOnFloatValueChanged, OnValueChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	float GetValue() const { return Value; }
	void SetValue(float InValue);

	// Thumb travel, split out so the geometry can be tested without a live Slate widget.
	//
	// The thumb is centred across the track and slides its own height short of the full run, so a
	// value of 1 parks its top edge at the top of the track rather than above it.
	static FVector2f GetThumbTopLeft(const FVector2f& TrackSize, const FVector2f& ThumbSize, float InValue);

	// Inverse of the above: the value a click at LocalY selects, taking the cursor as the middle
	// of the thumb rather than its top edge.
	static float GetValueAtLocalY(float TrackHeight, float ThumbHeight, float LocalY);

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	const FSlateBrush* ThumbBrush = nullptr;
	bool bLocked = false;
	float Value = 0.0f;
	FOnFloatValueChanged OnValueChanged;

	FVector2f GetThumbSize() const;
	void ApplyMouse(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
};
