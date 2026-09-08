#pragma once

#include "Widgets/SLeafWidget.h"

// Paint-only: gameplay keeps focus and owns input, including release-to-commit.
class SSimCopterRadialWheel : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterRadialWheel) {}
		SLATE_ARGUMENT(TArray<FString>, Labels)
		SLATE_ARGUMENT(FText, Title)
		SLATE_ARGUMENT(FText, Instructions)
		SLATE_ATTRIBUTE(int32, SelectedIndex)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(560, 600); }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&,
		FSlateWindowElementList&, int32, const FWidgetStyle&, bool) const override;
private:
	TArray<FString> Labels;
	FText Title;
	FText Instructions;
	TAttribute<int32> SelectedIndex;
};
