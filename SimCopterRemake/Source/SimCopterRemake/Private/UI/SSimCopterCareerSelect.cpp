// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterCareerSelect.h"

#include "Framework/Application/SlateApplication.h"
#include "Game/SimCopterCareerProgression.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/SlateBrush.h"
#include "Textures/SlateShaderResource.h"
#include "UI/SimCopterHangarArt.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimCopterCareerSelect"

using namespace SimCopterFrontEnd;
using namespace SimCopterCareerSelectLayout;

namespace
{
const TCHAR* const CareerPage = TEXT("CAREER.BMP");
const TCHAR* const HighlightSheet = TEXT("CARSEL.BMP");

// FUN_00457c90 builds both of these as its own sound objects: career.wav once when the screen
// opens, carsel.wav on every selection move.
const TCHAR* const OpenSound = TEXT("career");
const TCHAR* const SelectionSound = TEXT("carsel");

// Slate's built-in child clipping is rectangular. This draws the live media texture as an opaque
// rounded rectangle plus a transparent outer ring, giving the movie a genuinely feathered mask
// without baking an effect into any of the 30 authentic loops.
class SSimCopterRoundedMovie final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterRoundedMovie) {}
		SLATE_ARGUMENT(const FSlateBrush*, MovieBrush)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		MovieBrush = Args._MovieBrush;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return MovieBrush != nullptr ? MovieBrush->ImageSize : FVector2D::ZeroVector;
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		const int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		const bool bParentEnabled) const override
	{
		if (MovieBrush == nullptr)
		{
			return LayerId;
		}

		const FSlateResourceHandle Resource =
			FSlateApplication::Get().GetRenderer()->GetResourceHandle(*MovieBrush, FVector2f(AllottedGeometry.GetLocalSize()), 1.0f);
		const FSlateShaderResourceProxy* Proxy = Resource.GetResourceProxy();
		if (Proxy == nullptr)
		{
			return LayerId;
		}

		const FVector2f Size(AllottedGeometry.GetLocalSize());
		if (Size.X <= 0.0f || Size.Y <= 0.0f)
		{
			return LayerId;
		}

		constexpr int32 SegmentsPerCorner = 8;
		constexpr int32 PerimeterCount = SegmentsPerCorner * 4;
		const float OuterRadius = FMath::Min(PreviewCornerRadius, FMath::Min(Size.X, Size.Y) * 0.5f);
		const float Feather = FMath::Min(PreviewFeatherWidth, OuterRadius);
		const float InnerRadius = FMath::Max(0.0f, OuterRadius - Feather);
		const FVector2f InnerSize(Size.X - Feather * 2.0f, Size.Y - Feather * 2.0f);

		TArray<FSlateVertex> Vertices;
		TArray<SlateIndex> Indices;
		Vertices.Reserve(1 + PerimeterCount * 2);
		Indices.Reserve(PerimeterCount * 9);

		const FSlateRenderTransform& Transform = AllottedGeometry.GetAccumulatedRenderTransform();
		const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
		const auto AddVertex = [&Vertices, &Transform, &Size, Proxy, &WidgetTint](
			const FVector2f& Position, const float Alpha)
		{
			const FVector2f NormalizedUV(Position.X / Size.X, Position.Y / Size.Y);
			const FVector2f UV = Proxy->StartUV + NormalizedUV * Proxy->SizeUV;
			FLinearColor Tint = WidgetTint;
			Tint.A *= Alpha;
			Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
				Transform, Position, UV, Tint.ToFColor(true)));
		};

		AddVertex(Size * 0.5f, 1.0f);
		const auto AddRoundedPerimeter = [&AddVertex](
			const FVector2f& RectOffset,
			const FVector2f& RectSize,
			const float Radius,
			const float Alpha)
		{
			const FVector2f Centres[4] = {
				RectOffset + FVector2f(Radius, Radius),
				RectOffset + FVector2f(RectSize.X - Radius, Radius),
				RectOffset + FVector2f(RectSize.X - Radius, RectSize.Y - Radius),
				RectOffset + FVector2f(Radius, RectSize.Y - Radius),
			};
			const float StartAngles[4] = { UE_PI, -UE_PI * 0.5f, 0.0f, UE_PI * 0.5f };
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				for (int32 Segment = 0; Segment < SegmentsPerCorner; ++Segment)
				{
					const float Angle = StartAngles[Corner]
						+ (UE_PI * 0.5f) * static_cast<float>(Segment) / static_cast<float>(SegmentsPerCorner);
					AddVertex(
						Centres[Corner] + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius,
						Alpha);
				}
			}
		};

		AddRoundedPerimeter(FVector2f(Feather, Feather), InnerSize, InnerRadius, 1.0f);
		AddRoundedPerimeter(FVector2f::ZeroVector, Size, OuterRadius, 0.0f);

		constexpr int32 InnerStart = 1;
		constexpr int32 OuterStart = InnerStart + PerimeterCount;
		for (int32 Point = 0; Point < PerimeterCount; ++Point)
		{
			const int32 Next = (Point + 1) % PerimeterCount;
			Indices.Add(0);
			Indices.Add(static_cast<SlateIndex>(InnerStart + Point));
			Indices.Add(static_cast<SlateIndex>(InnerStart + Next));

			Indices.Add(static_cast<SlateIndex>(InnerStart + Point));
			Indices.Add(static_cast<SlateIndex>(OuterStart + Point));
			Indices.Add(static_cast<SlateIndex>(OuterStart + Next));
			Indices.Add(static_cast<SlateIndex>(InnerStart + Point));
			Indices.Add(static_cast<SlateIndex>(OuterStart + Next));
			Indices.Add(static_cast<SlateIndex>(InnerStart + Next));
		}

		FSlateDrawElement::MakeCustomVerts(
			OutDrawElements, LayerId, Resource, Vertices, Indices, nullptr, 0, 0);
		return LayerId;
	}

private:
	const FSlateBrush* MovieBrush = nullptr;
};

// CARSEL contains a complete copy of the panel artwork, including an opaque orange centre. Draw
// it through an inverse rounded mask so only its frame/glow remains over the movie. The signed-
// distance feather prevents the hollow opening from gaining a hard cut line.
class SSimCopterHollowHighlight final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterHollowHighlight) {}
		SLATE_ARGUMENT(const FSlateBrush*, HighlightBrush)
		SLATE_ARGUMENT(FRect, HoleRect)
		SLATE_ATTRIBUTE(float, Opacity)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		HighlightBrush = Args._HighlightBrush;
		HoleRect = Args._HoleRect;
		Opacity = Args._Opacity;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return HighlightBrush != nullptr ? HighlightBrush->ImageSize : FVector2D::ZeroVector;
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		const int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		const bool bParentEnabled) const override
	{
		if (HighlightBrush == nullptr)
		{
			return LayerId;
		}

		const FVector2f Size(AllottedGeometry.GetLocalSize());
		const FSlateResourceHandle Resource =
			FSlateApplication::Get().GetRenderer()->GetResourceHandle(*HighlightBrush, Size, 1.0f);
		const FSlateShaderResourceProxy* Proxy = Resource.GetResourceProxy();
		if (Proxy == nullptr || Size.X <= 0.0f || Size.Y <= 0.0f)
		{
			return LayerId;
		}

		FVector2f UVStart = Proxy->StartUV;
		FVector2f UVSize = Proxy->SizeUV;
		const FBox2f BrushUV = HighlightBrush->GetUVRegion();
		if (BrushUV.bIsValid)
		{
			UVStart = BrushUV.Min;
			UVSize = BrushUV.GetSize();
		}

		constexpr int32 GridColumns = 48;
		constexpr int32 GridRows = 32;
		TArray<FSlateVertex> Vertices;
		TArray<SlateIndex> Indices;
		Vertices.Reserve((GridColumns + 1) * (GridRows + 1));
		Indices.Reserve(GridColumns * GridRows * 6);

		const FVector2f HoleMin(HoleRect.Left, HoleRect.Top);
		const FVector2f HoleMax(HoleRect.Right, HoleRect.Bottom);
		const FVector2f HoleCentre = (HoleMin + HoleMax) * 0.5f;
		const FVector2f HoleHalfSize = (HoleMax - HoleMin) * 0.5f;
		const float Radius = FMath::Min(
			HighlightHoleCornerRadius, FMath::Min(HoleHalfSize.X, HoleHalfSize.Y));
		const FVector2f RoundedHalfSize = HoleHalfSize - FVector2f(Radius, Radius);
		const float HalfFeather = HighlightHoleFeatherWidth * 0.5f;
		const float SelectionAlpha = FMath::Clamp(Opacity.Get(1.0f), 0.0f, 1.0f);
		const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
		const FSlateRenderTransform& Transform = AllottedGeometry.GetAccumulatedRenderTransform();

		for (int32 Row = 0; Row <= GridRows; ++Row)
		{
			for (int32 Column = 0; Column <= GridColumns; ++Column)
			{
				const FVector2f Normalized(
					static_cast<float>(Column) / static_cast<float>(GridColumns),
					static_cast<float>(Row) / static_cast<float>(GridRows));
				const FVector2f Position = Normalized * Size;
				const FVector2f Delta = (Position - HoleCentre).GetAbs() - RoundedHalfSize;
				const FVector2f Outside(FMath::Max(Delta.X, 0.0f), FMath::Max(Delta.Y, 0.0f));
				const float SignedDistance = Outside.Length()
					+ FMath::Min(FMath::Max(Delta.X, Delta.Y), 0.0f) - Radius;
				const float MaskAlpha = FMath::SmoothStep(-HalfFeather, HalfFeather, SignedDistance);
				FLinearColor Tint = WidgetTint;
				Tint.A *= SelectionAlpha * MaskAlpha;
				Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
					Transform, Position, UVStart + Normalized * UVSize, Tint.ToFColor(true)));
			}
		}

		for (int32 Row = 0; Row < GridRows; ++Row)
		{
			for (int32 Column = 0; Column < GridColumns; ++Column)
			{
				const SlateIndex TopLeft = static_cast<SlateIndex>(Row * (GridColumns + 1) + Column);
				const SlateIndex TopRight = TopLeft + 1;
				const SlateIndex BottomLeft = TopLeft + GridColumns + 1;
				const SlateIndex BottomRight = BottomLeft + 1;
				Indices.Append({ TopLeft, BottomLeft, TopRight, TopRight, BottomLeft, BottomRight });
			}
		}

		FSlateDrawElement::MakeCustomVerts(
			OutDrawElements, LayerId, Resource, Vertices, Indices, nullptr, 0, 0);
		return LayerId;
	}

private:
	const FSlateBrush* HighlightBrush = nullptr;
	FRect HoleRect;
	TAttribute<float> Opacity;
};
}

namespace SimCopterCareerSelectLayout
{
int32 GetNavigationTarget(const EPanelNavigation Navigation, const int32 Selected, const int32 Count)
{
	if (Selected < 0 || Selected >= Count || Count < 2)
	{
		// FUN_00458a90 falls out of every branch when there is only one panel.
		return INDEX_NONE;
	}

	if (Count == 2)
	{
		// Every key toggles between the two.
		return 1 - Selected;
	}

	// Three panels. Left and Right are a clean ring; Up and Down are not, and both land on 0 from
	// panel 2, which is what the original's fall-through does.
	static constexpr int32 Wheel[4][3] = {
		/* Left  */ { 2, 0, 1 },
		/* Right */ { 1, 2, 0 },
		/* Up    */ { 2, 0, 0 },
		/* Down  */ { 2, 2, 0 },
	};

	return Wheel[static_cast<int32>(Navigation)][Selected];
}
}

void SSimCopterCareerSelect::Construct(const FArguments& InArgs)
{
	Art = InArgs._Art;
	Cities = InArgs._Cities;
	bAllowCancel = InArgs._AllowCancel;
	OnAccepted = InArgs._OnAccepted;
	OnCancelled = InArgs._OnCancelled;

	// FUN_00457c90 drops the -1 slots, so the screen shows one, two or three panels.
	Cities.RemoveAll([](const int32 City) { return City < 0 || City >= SimCopterCareerProgression::CityCount; });
	if (Cities.Num() > PanelCount)
	{
		Cities.SetNum(PanelCount);
	}

	USimCopterHangarArt* ArtObject = Art;
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	AddAt(Canvas, FRect{ 0.0f, 0.0f, PageWidth, PageHeight }, MakePageImage(ArtObject, CareerPage));

	for (int32 Panel = 0; Panel < Cities.Num(); ++Panel)
	{
		// The movie is the bottom panel layer. Its own rounded feather prevents its rectangular
		// corners from covering the frame beneath it.
		if (ArtObject != nullptr)
		{
			if (const FSlateBrush* MovieBrush = ArtObject->GetCareerCityMovieBrush(Cities[Panel]))
			{
				AddAt(
					Canvas,
					PanelRect[Panel],
					SNew(SSimCopterRoundedMovie)
					.MovieBrush(MovieBrush)
					.Visibility(EVisibility::HitTestInvisible));
			}
		}

		// The glowing CARSEL border is deliberately added after the movie so it remains the top
		// visual layer while fading smoothly between selection states.
		if (ArtObject != nullptr)
		{
			const FRect& Rect = HighlightPanelRect[Panel];
			const FSlateBrush* Brush = ArtObject->GetSubImage(
				HighlightSheet,
				FIntRect(
					FMath::RoundToInt(Rect.Left),
					FMath::RoundToInt(Rect.Top),
					FMath::RoundToInt(Rect.Right),
					FMath::RoundToInt(Rect.Bottom)),
				/*bColorKeyed=*/false);
			if (Brush != nullptr)
			{
				const FRect& PreviewRect = PanelRect[Panel];
				AddAt(
					Canvas,
					Rect,
					SNew(SSimCopterHollowHighlight)
					.HighlightBrush(Brush)
					.HoleRect(FRect{
						PreviewRect.Left - Rect.Left,
						PreviewRect.Top - Rect.Top,
						PreviewRect.Right - Rect.Left,
						PreviewRect.Bottom - Rect.Top })
					.Opacity_Lambda([this, Panel]() { return PanelOpacity[Panel]; })
					.Visibility_Lambda([this, Panel]()
					{
						return PanelOpacity[Panel] > 0.005f ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					}));
			}
		}

		AddAt(
			Canvas,
			PanelRect[Panel],
			MakeInvisibleHitButton(
				FOnClicked::CreateLambda([this, Panel]()
				{
					SetSelectedPanel(Panel);
					return FReply::Handled();
				}),
				FSimpleDelegate::CreateLambda([this, Panel]() { SetSelectedPanel(Panel); }),
				ButtonStyles));
	}

	AddAt(Canvas, CityNameRect,
		SNew(SBox)
		.VAlign(VAlign_Center)
		[
			SAssignNew(CityNameText, STextBlock)
			.Justification(ETextJustify::Center)
			.Font(PageFont(ReadoutFontHeight, /*bBold=*/true))
			.ColorAndOpacity(FSlateColor(ReadoutColor))
		]);
	AddAt(Canvas, LevelNameRect,
		SNew(SBox)
		.VAlign(VAlign_Center)
		[
			SAssignNew(LevelNameText, STextBlock)
			.Justification(ETextJustify::Center)
			.Font(PageFont(ReadoutFontHeight, /*bBold=*/true))
			.ColorAndOpacity(FSlateColor(ReadoutColor))
		]);

	const float AcceptX = bAllowCancel ? OkButtonX : OkOnlyButtonX;
	AddAt(Canvas, FRect{ AcceptX, ButtonY, AcceptX + ButtonWidth, ButtonY + ButtonHeight },
		MakeButton(
			ArtObject,
			LOCTEXT("Ok", "OK"), // STRINGTABLE 81
			ButtonFontHeight,
			FOnClicked::CreateLambda([this]() { Accept(); return FReply::Handled(); }),
			ButtonStyles));

	if (bAllowCancel)
	{
		AddAt(Canvas, FRect{ CancelButtonX, ButtonY, CancelButtonX + ButtonWidth, ButtonY + ButtonHeight },
			MakeButton(
				ArtObject,
				LOCTEXT("Cancel", "Cancel"), // STRINGTABLE 82
				ButtonFontHeight,
				FOnClicked::CreateLambda([this]() { Cancel(); return FReply::Handled(); }),
				ButtonStyles));
	}

	ChildSlot
	[
		MakeScaledScreen(Canvas)
	];

	RefreshReadouts();
	PlayScreenSound(OpenSound);
}

void SSimCopterCareerSelect::SetSelectedPanel(const int32 Panel)
{
	if (Panel < 0 || Panel >= Cities.Num() || Panel == SelectedPanel)
	{
		return;
	}

	SelectedPanel = Panel;
	RefreshReadouts();
	PlayScreenSound(SelectionSound);
}

void SSimCopterCareerSelect::RefreshReadouts()
{
	const int32 City = Cities.IsValidIndex(SelectedPanel) ? Cities[SelectedPanel] : INDEX_NONE;

	if (CityNameText.IsValid())
	{
		CityNameText->SetText(FText::FromString(SimCopterCareerProgression::GetCityName(City)));
	}
	if (LevelNameText.IsValid())
	{
		LevelNameText->SetText(FText::FromString(
			SimCopterCareerProgression::GetLevelName(SimCopterCareerProgression::GetLevel(City))));
	}
}

void SSimCopterCareerSelect::Accept()
{
	if (Cities.IsValidIndex(SelectedPanel))
	{
		OnAccepted.ExecuteIfBound(Cities[SelectedPanel]);
	}
}

void SSimCopterCareerSelect::Cancel()
{
	// FUN_00458a90 only posts 0x3ea when Cancel exists at all.
	if (bAllowCancel)
	{
		OnCancelled.ExecuteIfBound();
	}
}

FReply SSimCopterCareerSelect::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::Escape)
	{
		Cancel();
		return FReply::Handled();
	}
	if (Key == EKeys::Enter)
	{
		Accept();
		return FReply::Handled();
	}

	EPanelNavigation Navigation = EPanelNavigation::Left;
	bool bIsNavigation = true;
	if (Key == EKeys::Left)        { Navigation = EPanelNavigation::Left; }
	else if (Key == EKeys::Right)  { Navigation = EPanelNavigation::Right; }
	else if (Key == EKeys::Up)     { Navigation = EPanelNavigation::Up; }
	else if (Key == EKeys::Down)   { Navigation = EPanelNavigation::Down; }
	else                           { bIsNavigation = false; }

	if (bIsNavigation)
	{
		// The original plays carsel.wav for any of the four arrows, even when the wheel does not
		// move, so SetSelectedPanel's own tick would not be enough on its own.
		PlayScreenSound(SelectionSound);
		const int32 Target = GetNavigationTarget(Navigation, SelectedPanel, Cities.Num());
		if (Target != INDEX_NONE && Target != SelectedPanel)
		{
			SelectedPanel = Target;
			RefreshReadouts();
		}
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SSimCopterCareerSelect::Tick(const FGeometry& AllottedGeometry, const double CurrentTime, const float DeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, CurrentTime, DeltaTime);

	for (int32 Panel = 0; Panel < PanelCount; ++Panel)
	{
		const float Target = (Panel == SelectedPanel) ? 1.0f : 0.0f;
		PanelOpacity[Panel] = FMath::FInterpTo(PanelOpacity[Panel], Target, static_cast<float>(DeltaTime), 14.0f);
	}
}

#undef LOCTEXT_NAMESPACE
