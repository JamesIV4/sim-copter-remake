#include "UI/SSimCopterMegaphoneCarousel.h"

#include "Flight/SimCopterHelicopterRegistry.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void SSimCopterMegaphoneCarousel::Construct(const FArguments& Args)
{
	SetVisibility(EVisibility::HitTestInvisible);
	ForceVolatile(true);
}

double SSimCopterMegaphoneCarousel::PositionAt(double Now) const
{
	const double T = FMath::Clamp((Now - ChangedAt) / 0.22, 0.0, 1.0);
	return FMath::Lerp(FromPosition, TargetPosition, 1.0 - FMath::Pow(1.0 - T, 3.0));
}

void SSimCopterMegaphoneCarousel::ShowSelection(int32 Previous, int32 Selected)
{
	const double Now = FPlatformTime::Seconds();
	const int32 Count = static_cast<int32>(ESimCopterMegaphoneMessage::Count);
	int32 Delta = Selected - Previous;
	if (Delta > Count / 2) Delta -= Count;
	if (Delta < -Count / 2) Delta += Count;
	if (Now - ChangedAt >= 3.75)
	{
		FromPosition = Previous;
		TargetPosition = Previous;
	}
	else
	{
		FromPosition = PositionAt(Now); // rapid changes continue from the visible position
	}
	TargetPosition += Delta;
	ChangedAt = Now;
}

int32 SSimCopterMegaphoneCarousel::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& Culling, FSlateWindowElementList& Elements, int32 Layer,
	const FWidgetStyle& Style, bool bEnabled) const
{
	const double Now = FPlatformTime::Seconds();
	const float Fade = FMath::Clamp(static_cast<float>((Now - ChangedAt - 3.0) / 0.75), 0.0f, 1.0f);
	const float Opacity = 1.0f - FMath::SmoothStep(0.0f, 1.0f, Fade);
	if (Opacity <= 0) return Layer;
	const FVector2D Size = Geometry.GetLocalSize();
	const float Scale = 0.5f * FMath::Min(Size.X / 720.0, Size.Y / 600.0);
	const double Position = PositionAt(Now);
	const int32 Count = static_cast<int32>(ESimCopterMegaphoneMessage::Count);
	const FSlateBrush* Box = FCoreStyle::Get().GetBrush("WhiteBrush");
	const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	// One stationary ribbon. Only the labels translate; the broad edge fades
	// leave the centre legible without a row of moving boxes over the scene.
	const FVector2D RibbonSize(640 * Scale, 48 * Scale);
	const FVector2D RibbonPosition((Size.X - RibbonSize.X) * 0.5, 40 * Scale);
	TArray<FSlateGradientStop> Stops;
	for (int32 StopIndex = 0; StopIndex <= 24; ++StopIndex)
	{
		const float X = StopIndex / 24.0f;
		const float Strength = FMath::SmoothStep(0.0f, 0.3f, 1.0f - FMath::Abs(2.0f * X - 1.0f));
		Stops.Add(FSlateGradientStop(FVector2D(X * RibbonSize.X, 0),
			FLinearColor(0.025f, 0.03f, 0.035f, Strength * Opacity)));
	}
	FSlateDrawElement::MakeGradient(Elements, Layer,
		Geometry.ToPaintGeometry(RibbonSize, FSlateLayoutTransform(RibbonPosition)), MoveTemp(Stops), Orient_Vertical);
	FSlateDrawElement::MakeBox(Elements, Layer + 1,
		Geometry.ToPaintGeometry(FVector2D(100 * Scale, Scale), FSlateLayoutTransform(
			FVector2D(Size.X * 0.5 - 50 * Scale, RibbonPosition.Y + RibbonSize.Y))),
		Box, ESlateDrawEffect::None, FLinearColor(1.0f, 0.72f, 0.15f, Opacity * 0.8f));
	for (int32 Slot = FMath::FloorToInt(Position) - 2; Slot <= FMath::CeilToInt(Position) + 2; ++Slot)
	{
		const float Distance = FMath::Abs(Slot - Position);
		const float EdgeAlpha = 1.0f - FMath::SmoothStep(1.2f, 2.0f, Distance);
		if (EdgeAlpha <= 0) continue;
		const float Emphasis = FMath::Clamp(1.0f - Distance, 0.0f, 1.0f);
		const FVector2D CardSize(200 * Scale, 48 * Scale);
		const FVector2D CardPosition(Size.X * 0.5 + (Slot - Position) * 210 * Scale - CardSize.X * 0.5, RibbonPosition.Y);
		const float Alpha = Opacity * EdgeAlpha;
		const int32 Index = (Slot % Count + Count) % Count;
		const FString Label = SimCopterHelicopterRegistry::GetMegaphoneMessageName(static_cast<ESimCopterMegaphoneMessage>(Index));
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 13);
		const FVector2D TextSize = Measure->Measure(Label, Font);
		const float TextScale = Scale * FMath::Min(1.0, 188.0 / FMath::Max(TextSize.X, 1.0));
		const FVector2D TextPosition = CardPosition + (CardSize - TextSize * TextScale) * 0.5;
		const float TextAlpha = Alpha * (0.8f + Emphasis * 0.2f);
		FSlateDrawElement::MakeText(Elements, Layer + 2,
			Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TextScale, TextPosition + FVector2D(1.5f, 2.0f) * Scale)),
			Label, Font, ESlateDrawEffect::None, FLinearColor(0, 0, 0, TextAlpha * 0.9f));
		FSlateDrawElement::MakeText(Elements, Layer + 3, Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TextScale, TextPosition)),
			Label, Font, ESlateDrawEffect::None, FLinearColor(1, 1, 1, TextAlpha));
	}
	return Layer + 3;
}
