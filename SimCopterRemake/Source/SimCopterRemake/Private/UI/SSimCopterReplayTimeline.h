// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

DECLARE_DELEGATE_OneParam(FOnSimCopterReplayScrub, float /*Seconds*/);

/** One tick on the timeline: something the clip recorded happening at a point in time. */
struct FSimCopterReplayTimelineMarker
{
	float Seconds = 0.0f;
	FLinearColor Color = FLinearColor::White;
};

/**
 * The replay panel's scrub bar.
 *
 * A leaf widget rather than an `SSlider`, for two reasons: the bar has to draw the clip's event
 * markers along its length (that is most of what makes it worth looking at), and a slider's handle
 * is a grab target while this wants a click anywhere on the bar to jump there - which is what
 * scrubbing means.
 *
 * Dragging is captured, so the pointer may leave the bar vertically without dropping the scrub. The
 * widget holds no playhead of its own: it reads one attribute and reports where the pointer is, and
 * the subsystem remains the only thing that knows what time it is.
 */
class SSimCopterReplayTimeline : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterReplayTimeline)
		: _DurationSeconds(0.0f)
		, _PlayheadSeconds(0.0f)
		, _BarHeight(26.0f)
	{}
		SLATE_ATTRIBUTE(float, DurationSeconds)
		SLATE_ATTRIBUTE(float, PlayheadSeconds)
		SLATE_ARGUMENT(float, BarHeight)
		/** Fired continuously while dragging, and once on a click. */
		SLATE_EVENT(FOnSimCopterReplayScrub, OnScrub)
		/** True while the operator is dragging, so the panel can suspend playback for the duration. */
		SLATE_EVENT(FSimpleDelegate, OnScrubBegin)
		SLATE_EVENT(FSimpleDelegate, OnScrubEnd)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Pushed by the panel whenever the clip changes, rather than read through an attribute: a long
	 * take holds hundreds of markers and the bar repaints every frame, so an attribute would copy
	 * the whole array once per frame to draw the same picture.
	 */
	void SetMarkers(TArray<FSimCopterReplayTimelineMarker>&& InMarkers);

	// --- SWidget ---
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
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	TAttribute<float> DurationSeconds;
	TAttribute<float> PlayheadSeconds;
	TArray<FSimCopterReplayTimelineMarker> Markers;
	float BarHeight = 26.0f;

	FOnSimCopterReplayScrub OnScrub;
	FSimpleDelegate OnScrubBegin;
	FSimpleDelegate OnScrubEnd;

	bool bDragging = false;

	/** Local pointer X -> clip time, clamped to the clip. */
	float PositionToSeconds(const FGeometry& MyGeometry, const FVector2D& ScreenPosition) const;
};
