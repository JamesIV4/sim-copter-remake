#pragma once

#include "Rendering/DrawElements.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SCompoundWidget.h"

// Remake-only equipped-tool indicator. Paint only: cockpit input still belongs to its child.
class SSimCopterToolGlow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterToolGlow) : _Active(false) {}
		SLATE_ATTRIBUTE(bool, Active)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args)
	{
		Active = Args._Active;
		ChildSlot[Args._Content.Widget];
		SetVisibility(EVisibility::SelfHitTestInvisible);
	}
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override
	{
		Layer = SCompoundWidget::OnPaint(Args, Geometry, Culling, Elements, Layer, Style, bEnabled);
		if (!Active.Get()) return Layer;
		const FVector2f Size = Geometry.GetLocalSize();
		// Whole-card halo: concentric rounded outlines fade smoothly on both sides of the edge.
		for (int32 Pass = 8; Pass >= 0; --Pass)
		{
			const float Width = Pass == 0 ? 2.5f : 2.5f + Pass * 3.0f;
			const float Alpha = Pass == 0 ? 0.85f : 0.26f / Pass;
			const float Inset = 3.0f - Width * 0.5f;
			const FSlateRoundedBoxBrush Outline(FLinearColor::Transparent, 6.0f + Width * 0.5f,
				FLinearColor(1.0f, 0.65f, 0.12f, Alpha), Width);
			FSlateDrawElement::MakeBox(Elements, ++Layer,
				Geometry.ToPaintGeometry(Size - FVector2f(Inset * 2), FSlateLayoutTransform(FVector2f(Inset))),
				&Outline, ESlateDrawEffect::None, FLinearColor::Transparent);
		}
		return Layer;
	}
private:
	TAttribute<bool> Active;
};
