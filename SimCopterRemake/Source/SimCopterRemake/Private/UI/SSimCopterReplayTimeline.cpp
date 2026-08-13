// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SSimCopterReplayTimeline.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
const FLinearColor TrackColor(0.06f, 0.07f, 0.10f, 0.95f);
const FLinearColor ElapsedColor(0.18f, 0.38f, 0.68f, 0.85f);
const FLinearColor PlayheadColor(0.98f, 0.82f, 0.28f, 1.0f);
const FLinearColor BorderColor(0.35f, 0.39f, 0.46f, 1.0f);

/** Wide enough to be hittable at 1080p without hiding the markers behind it. */
constexpr float PlayheadWidth = 2.0f;
constexpr float MarkerWidth = 2.0f;
}

void SSimCopterReplayTimeline::Construct(const FArguments& InArgs)
{
	DurationSeconds = InArgs._DurationSeconds;
	PlayheadSeconds = InArgs._PlayheadSeconds;
	BarHeight = InArgs._BarHeight;
	OnScrub = InArgs._OnScrub;
	OnScrubBegin = InArgs._OnScrubBegin;
	OnScrubEnd = InArgs._OnScrubEnd;
}

void SSimCopterReplayTimeline::SetMarkers(TArray<FSimCopterReplayTimelineMarker>&& InMarkers)
{
	Markers = MoveTemp(InMarkers);
}

FVector2D SSimCopterReplayTimeline::ComputeDesiredSize(float) const
{
	// Width is whatever the row gives it; only the height is the widget's own business.
	return FVector2D(120.0f, BarHeight);
}

float SSimCopterReplayTimeline::PositionToSeconds(
	const FGeometry& MyGeometry,
	const FVector2D& ScreenPosition) const
{
	const float Duration = DurationSeconds.Get(0.0f);
	const FVector2D Local = MyGeometry.AbsoluteToLocal(ScreenPosition);
	const float Width = FMath::Max(MyGeometry.GetLocalSize().X, 1.0f);
	const float Alpha = FMath::Clamp(static_cast<float>(Local.X) / Width, 0.0f, 1.0f);
	return Alpha * Duration;
}

FReply SSimCopterReplayTimeline::OnMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || DurationSeconds.Get(0.0f) <= 0.0f)
	{
		return FReply::Unhandled();
	}

	bDragging = true;
	OnScrubBegin.ExecuteIfBound();
	// Jump on press, not on release: a click anywhere on the bar means "go there", and waiting for
	// the release would make a click-and-hold show the old frame until the operator let go.
	OnScrub.ExecuteIfBound(PositionToSeconds(MyGeometry, MouseEvent.GetScreenSpacePosition()));

	// Captured, so a drag that wanders off the bar vertically - which it always does - keeps
	// scrubbing instead of stopping dead.
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SSimCopterReplayTimeline::OnMouseMove(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (!bDragging || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	OnScrub.ExecuteIfBound(PositionToSeconds(MyGeometry, MouseEvent.GetScreenSpacePosition()));
	return FReply::Handled();
}

FReply SSimCopterReplayTimeline::OnMouseButtonUp(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bDragging)
	{
		return FReply::Unhandled();
	}
	bDragging = false;
	OnScrub.ExecuteIfBound(PositionToSeconds(MyGeometry, MouseEvent.GetScreenSpacePosition()));
	OnScrubEnd.ExecuteIfBound();
	return FReply::Handled().ReleaseMouseCapture();
}

FCursorReply SSimCopterReplayTimeline::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return DurationSeconds.Get(0.0f) > 0.0f
		? FCursorReply::Cursor(EMouseCursor::ResizeLeftRight)
		: FCursorReply::Unhandled();
}

int32 SSimCopterReplayTimeline::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const float Width = FMath::Max(static_cast<float>(Size.X), 1.0f);
	const float Height = static_cast<float>(Size.Y);
	const float Duration = DurationSeconds.Get(0.0f);
	const ESlateDrawEffect DrawEffects = ESlateDrawEffect::None;

	// The track.
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		WhiteBrush,
		DrawEffects,
		TrackColor);

	if (Duration <= 0.0f)
	{
		// An empty clip still draws its track, so the panel does not visibly change shape the
		// moment a recording starts.
		return LayerId + 1;
	}

	const float PlayheadAlpha = FMath::Clamp(PlayheadSeconds.Get(0.0f) / Duration, 0.0f, 1.0f);
	const float PlayheadX = PlayheadAlpha * Width;

	// Everything up to the playhead, so the bar reads as progress rather than as a slider.
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId + 1,
		AllottedGeometry.ToPaintGeometry(FVector2f(PlayheadX, Height), FSlateLayoutTransform()),
		WhiteBrush,
		DrawEffects,
		ElapsedColor);

	// Event markers. They are drawn as short ticks off the bottom edge rather than full-height
	// lines: a busy riot puts dozens in a second, and full-height ticks would paint over the
	// progress fill entirely.
	const float MarkerHeight = FMath::Max(Height * 0.4f, 4.0f);
	for (const FSimCopterReplayTimelineMarker& Marker : Markers)
	{
		const float MarkerX = FMath::Clamp(Marker.Seconds / Duration, 0.0f, 1.0f) * Width;
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 2,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(MarkerWidth, MarkerHeight),
				FSlateLayoutTransform(FVector2f(MarkerX - MarkerWidth * 0.5f, Height - MarkerHeight))),
			WhiteBrush,
			DrawEffects,
			Marker.Color);
	}

	// The playhead, on top of everything.
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId + 3,
		AllottedGeometry.ToPaintGeometry(
			FVector2f(PlayheadWidth, Height),
			FSlateLayoutTransform(FVector2f(PlayheadX - PlayheadWidth * 0.5f, 0.0f))),
		WhiteBrush,
		DrawEffects,
		PlayheadColor);

	// A one-pixel frame, so the bar has an edge against the panel behind it.
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId + 4,
		AllottedGeometry.ToPaintGeometry(FVector2f(Width, 1.0f), FSlateLayoutTransform()),
		WhiteBrush,
		DrawEffects,
		BorderColor);
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId + 4,
		AllottedGeometry.ToPaintGeometry(
			FVector2f(Width, 1.0f),
			FSlateLayoutTransform(FVector2f(0.0f, Height - 1.0f))),
		WhiteBrush,
		DrawEffects,
		BorderColor);

	return LayerId + 5;
}
