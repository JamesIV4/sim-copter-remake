#include "UI/SSimCopterRadialWheel.h"
#include "Flight/SimCopterControllerInput.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Styling/CoreStyle.h"

void SSimCopterRadialWheel::Construct(const FArguments& Args)
{
	Labels = Args._Labels;
	Title = Args._Title;
	Instructions = Args._Instructions;
	SelectedIndex = Args._SelectedIndex;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SSimCopterRadialWheel::OnPaint(const FPaintArgs&, const FGeometry& Geometry,
	const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
	const FWidgetStyle& Style, bool) const
{
	const FVector2f Centre(280, 260);
	const FLinearColor Amber(1.0f, 0.67f, 0.23f);
	const FLinearColor Ink(0.025f, 0.035f, 0.045f, 0.96f);
	const FLinearColor White(0.91f, 0.93f, 0.91f);
	const auto& Renderer = FSlateApplication::Get().GetRenderer();
	const FSlateResourceHandle Resource = Renderer->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
	const auto Ring = [&](float Inner, float Outer, float Start, float End, FLinearColor Color)
	{
		TArray<FSlateVertex> Vertices;
		TArray<SlateIndex> Indices;
		const int32 Steps = FMath::Max(2, FMath::CeilToInt((End - Start) * 32));
		// Slate's custom-vertex shader performs the output gamma conversion. Pre-encoding
		// these solid colors as sRGB washes a charcoal wheel out to light grey.
		const FColor Tint = (Color * Style.GetColorAndOpacityTint()).ToFColor(false);
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float Angle = FMath::Lerp(Start, End, static_cast<float>(Step) / Steps);
			const FVector2f Direction(FMath::Sin(Angle), -FMath::Cos(Angle));
			for (float Radius : {Inner, Outer})
			{
				Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
					Geometry.GetAccumulatedRenderTransform(), Centre + Direction * Radius,
					FVector2f(0.5f, 0.5f), Tint));
			}
			if (Step > 0)
			{
				const SlateIndex Base = static_cast<SlateIndex>((Step - 1) * 2);
				Indices.Append({Base, static_cast<SlateIndex>(Base+1), static_cast<SlateIndex>(Base+3),
					Base, static_cast<SlateIndex>(Base+3), static_cast<SlateIndex>(Base+2)});
			}
		}
		FSlateDrawElement::MakeCustomVerts(Out, Layer, Resource, Vertices, Indices, nullptr, 0, 0);
	};
	const auto Text = [&](const FString& String, FVector2f Position, int32 Size, FLinearColor Color, bool Bold)
	{
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(Bold ? "Bold" : "Regular", Size);
		const FVector2f Extent(Renderer->GetFontMeasureService()->Measure(String, Font));
		FSlateDrawElement::MakeText(Out, Layer + 1,
			Geometry.ToPaintGeometry(FVector2f(560, 600), FSlateLayoutTransform(Position - Extent * 0.5f)),
			String, Font, ESlateDrawEffect::None, Color * Style.GetColorAndOpacityTint());
	};

	Ring(0, 246, 0, 2 * UE_PI, FLinearColor(0, 0, 0, 0.4f));
	const int32 Selected = SelectedIndex.Get(INDEX_NONE);
	for (int32 Index = 0; Index < Labels.Num(); ++Index)
	{
		const float Angle = 2 * UE_PI * Index / Labels.Num();
		const float Half = UE_PI / Labels.Num();
		const bool bSelected = Index == Selected;
		Ring(108, 239, Angle - Half + 0.015f, Angle + Half - 0.015f,
			bSelected ? FLinearColor(0.22f, 0.14f, 0.055f, 0.98f) : Ink);
		Ring(236, 239, Angle - Half + 0.015f, Angle + Half - 0.015f,
			bSelected ? Amber : FLinearColor(0.25f, 0.29f, 0.30f, 0.8f));
		const FVector2f Position = Centre + FVector2f(SimCopterControllerInput::GetRadialSlotDirection(Index, Labels.Num())) * 174;
		// Two short lines stay inside a sector, even for the six-tool helicopter.
		TArray<FString> Words;
		Labels[Index].ParseIntoArray(Words, TEXT(" "), true);
		FString First, Second;
		for (const FString& Word : Words)
		{
			FString& Line = First.Len() < Labels[Index].Len() / 2 ? First : Second;
			if (!Line.IsEmpty()) Line += TEXT(" ");
			Line += Word;
		}
		Text(First, Position - FVector2f(0, Second.IsEmpty() ? 0 : 10), 13, bSelected ? Amber : White, true);
		if (!Second.IsEmpty()) Text(Second, Position + FVector2f(0, 10), 13, bSelected ? Amber : White, true);
	}
	Ring(0, 99, 0, 2 * UE_PI, Ink);
	Ring(98, 100, 0, 2 * UE_PI, FLinearColor(0.30f, 0.34f, 0.34f));
	Text(Title.ToString(), Centre - FVector2f(0, 15), 16, Amber, true);
	Text(TEXT("RIGHT STICK"), Centre + FVector2f(0, 15), 10, White, false);
	TArray<FString> Lines;
	Instructions.ToString().ParseIntoArrayLines(Lines);
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
		Text(Lines[Index], FVector2f(280, 530 + Index * 22), 12, White, Index == 0);
	return Layer + 2;
}
