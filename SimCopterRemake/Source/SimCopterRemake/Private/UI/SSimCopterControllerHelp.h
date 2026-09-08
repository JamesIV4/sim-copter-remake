#pragma once
#include "UI/SimCopterControllerHelp.h"
#include "Widgets/SCompoundWidget.h"

class APawn;
struct FSlateDynamicImageBrush;
class SVerticalBox;

// The list is paint-only. The viewport keeps gameplay input even while help is expanded.
class SSimCopterControllerHelp : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterControllerHelp) : _IconStyle(SimCopterControllerHelp::EStyle::Xbox), _Scale(1.0f) {}
		SLATE_ATTRIBUTE(SimCopterControllerHelp::FState, State)
		SLATE_ATTRIBUTE(SimCopterControllerHelp::EStyle, IconStyle)
		SLATE_ARGUMENT(float, Scale)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual void Tick(const FGeometry&, double, float) override;
	static TSharedRef<SWidget> ForPawn(TWeakObjectPtr<APawn> Pawn, float Scale = 1.0f);
private:
	TAttribute<SimCopterControllerHelp::FState> State;
	TAttribute<SimCopterControllerHelp::EStyle> IconStyle;
	TSharedPtr<SVerticalBox> Rows;
	TMap<FString, TSharedPtr<FSlateDynamicImageBrush>> Icons;
	FString PreviousLayout;
	float Scale = 1;
	void Refresh();
	const FSlateBrush* GetIcon(const FString& Name);
};
