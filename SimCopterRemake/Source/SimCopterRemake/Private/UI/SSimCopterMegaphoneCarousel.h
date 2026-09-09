#pragma once

#include "Widgets/SLeafWidget.h"

class SSimCopterMegaphoneCarousel final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterMegaphoneCarousel) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	void ShowSelection(int32 Previous, int32 Selected);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&,
		FSlateWindowElementList&, int32, const FWidgetStyle&, bool) const override;
private:
	double PositionAt(double Now) const;
	double ChangedAt = -100;
	double FromPosition = 0;
	double TargetPosition = 0;
};
