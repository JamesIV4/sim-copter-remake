// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterCheckupSlider.h"

#include "Rendering/DrawElements.h"
#include "Styling/SlateBrush.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"

namespace
{
// SLIDERTV.BMP's own size, used when the original artwork is missing so a brushless slider still
// has sane travel instead of collapsing to a point.
const FVector2f FallbackThumbSize(22.0f, 18.0f);
}

void SSimCopterCheckupSlider::Construct(const FArguments& InArgs)
{
	ThumbBrush = InArgs._ThumbBrush;
	TrackBrush = InArgs._TrackBrush;
	ThumbScale = FMath::Max(InArgs._ThumbScale, UE_SMALL_NUMBER);
	bLocked = InArgs._Locked;
	Orientation = InArgs._Orientation;
	OnValueChanged = InArgs._OnValueChanged;
	ValueTooltipText = InArgs._ValueTooltipText;

	SetCanTick(ValueTooltipText.IsSet());
}

void SSimCopterCheckupSlider::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (ValueTooltipOpacity > 0.0f)
	{
		// Slate time keeps the one-second fade running while the settings menu pauses the game.
		ValueTooltipOpacity = HasMouseCapture() ? 1.0f : FMath::Max(0.0f, ValueTooltipOpacity - InDeltaTime);
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FVector2f SSimCopterCheckupSlider::GetThumbSize() const
{
	if (ThumbBrush == nullptr || ThumbBrush->ImageSize.IsNearlyZero())
	{
		return FallbackThumbSize * ThumbScale;
	}
	return FVector2f(ThumbBrush->ImageSize) * ThumbScale;
}

FVector2f SSimCopterCheckupSlider::GetThumbTopLeft(
	const FVector2f& TrackSize,
	const FVector2f& ThumbSize,
	const float InValue,
	const EOrientation InOrientation)
{
	const float Clamped = FMath::Clamp(InValue, 0.0f, 1.0f);

	if (InOrientation == Orient_Horizontal)
	{
		const float Travel = FMath::Max(0.0f, TrackSize.X - ThumbSize.X);
		return FVector2f(Clamped * Travel, (TrackSize.Y - ThumbSize.Y) * 0.5f);
	}

	const float Travel = FMath::Max(0.0f, TrackSize.Y - ThumbSize.Y);
	return FVector2f((TrackSize.X - ThumbSize.X) * 0.5f, (1.0f - Clamped) * Travel);
}

float SSimCopterCheckupSlider::GetValueAtLocalY(
	const float TrackHeight,
	const float ThumbHeight,
	const float LocalY)
{
	const float Travel = FMath::Max(0.0f, TrackHeight - ThumbHeight);
	if (Travel <= 0.0f)
	{
		return 0.0f;
	}
	const float TopEdge = FMath::Clamp(LocalY - ThumbHeight * 0.5f, 0.0f, Travel);
	return 1.0f - TopEdge / Travel;
}

float SSimCopterCheckupSlider::GetValueAtLocalX(
	const float TrackWidth,
	const float ThumbWidth,
	const float LocalX)
{
	const float Travel = FMath::Max(0.0f, TrackWidth - ThumbWidth);
	if (Travel <= 0.0f)
	{
		return 0.0f;
	}
	const float LeftEdge = FMath::Clamp(LocalX - ThumbWidth * 0.5f, 0.0f, Travel);
	return LeftEdge / Travel;
}

void SSimCopterCheckupSlider::SetValue(const float InValue)
{
	const float Clamped = FMath::Clamp(InValue, 0.0f, 1.0f);
	if (Clamped == Value)
	{
		return;
	}
	Value = Clamped;
	OnValueChanged.ExecuteIfBound(Value);
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SSimCopterCheckupSlider::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	if (ThumbBrush == nullptr && TrackBrush == nullptr && ValueTooltipOpacity <= 0.0f)
	{
		return LayerId;
	}

	const ESlateDrawEffect Effects = (bParentEnabled && !bLocked)
		? ESlateDrawEffect::None
		: ESlateDrawEffect::DisabledEffect;

	int32 CurrentLayer = LayerId;

	if (TrackBrush != nullptr)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			CurrentLayer,
			AllottedGeometry.ToPaintGeometry(),
			TrackBrush,
			Effects,
			TrackBrush->GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint());
		CurrentLayer++;
	}

	if (ThumbBrush != nullptr)
	{
		const FVector2f ThumbSize = GetThumbSize();
		const FVector2f TopLeft = GetThumbTopLeft(AllottedGeometry.GetLocalSize(), ThumbSize, Value, Orientation);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			CurrentLayer,
			AllottedGeometry.ToPaintGeometry(ThumbSize, FSlateLayoutTransform(TopLeft)),
			ThumbBrush,
			Effects,
			ThumbBrush->GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint());
	}

	if (ValueTooltipOpacity > 0.0f && ValueTooltipText.IsSet())
	{
		const FText Text = ValueTooltipText.Get();
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 12);
		const FVector2f TextSize(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font));
		const FVector2f ThumbSize = GetThumbSize();
		const FVector2f ThumbPosition = GetThumbTopLeft(AllottedGeometry.GetLocalSize(), ThumbSize, Value, Orientation);
		const FVector2f Padding(5.0f, 3.0f);
		const FVector2f BubbleSize = TextSize + Padding * 2.0f;
		const FVector2f BubblePosition(ThumbPosition.X + ThumbSize.X + 3.0f,
			ThumbPosition.Y + (ThumbSize.Y - BubbleSize.Y) * 0.5f);
		const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint() * FLinearColor(1, 1, 1, ValueTooltipOpacity);
		FSlateDrawElement::MakeBox(OutDrawElements, ++CurrentLayer,
			AllottedGeometry.ToPaintGeometry(BubbleSize, FSlateLayoutTransform(BubblePosition)),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None,
			FLinearColor(0.04f, 0.04f, 0.04f, 0.95f) * Tint);
		FSlateDrawElement::MakeText(OutDrawElements, ++CurrentLayer,
			AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(BubblePosition + Padding)),
			Text, Font, ESlateDrawEffect::None, Tint);
	}

	return CurrentLayer;
}

FVector2D SSimCopterCheckupSlider::ComputeDesiredSize(float) const
{
	// The dialog is a fixed canvas laid out in CHECKUP.BMP pixels, so this only matters if the
	// slider is ever placed somewhere that asks it how big it wants to be.
	return FVector2D(GetThumbSize());
}

void SSimCopterCheckupSlider::ApplyMouse(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2f Track = MyGeometry.GetLocalSize();
	const FVector2f Thumb = GetThumbSize();
	SetValue(Orientation == Orient_Horizontal
		? GetValueAtLocalX(Track.X, Thumb.X, Local.X)
		: GetValueAtLocalY(Track.Y, Thumb.Y, Local.Y));
}

FReply SSimCopterCheckupSlider::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bLocked || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		// Still handled: a click anywhere on the panel must never fall through to the world's
		// primary action behind it.
		return FReply::Handled();
	}

	ApplyMouse(MyGeometry, MouseEvent);
	if (ValueTooltipText.IsSet())
	{
		ValueTooltipOpacity = 1.0f;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SSimCopterCheckupSlider::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		return FReply::Handled();
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SSimCopterCheckupSlider::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture() || bLocked)
	{
		return FReply::Unhandled();
	}
	ApplyMouse(MyGeometry, MouseEvent);
	return FReply::Handled();
}

void SSimCopterCheckupSlider::AdjustForController(FVector2D Direction)
{
	if (!bLocked)
	{
		const float Step = Orientation == Orient_Horizontal ? Direction.X : -Direction.Y;
		SetValue(GetValue() + Step * 0.05f);
		if (!FMath::IsNearlyZero(Step) && ValueTooltipText.IsSet())
		{
			ValueTooltipOpacity = 1.0f;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
}
