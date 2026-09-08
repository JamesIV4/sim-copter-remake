#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/CoreStyle.h"

// Any discrete press skips the current movie. Consume repeats and releases so a held
// key cannot skip the next movie or activate the menu underneath it.
class SSimCopterIntro : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterIntro) {}
		SLATE_ARGUMENT(const FSlateBrush*, MovieBrush)
		SLATE_EVENT(FSimpleDelegate, OnSkip)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args)
	{
		OnSkip = Args._OnSkip;
		ChildSlot
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor::Black).Padding(0)
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
				[
					SNew(SBox).WidthOverride(640).HeightOverride(480)
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(640).HeightOverride(280)
						[SNew(SImage).Image(Args._MovieBrush)]
					]
				]
			]
		];
	}
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
	{
		if (!Event.IsRepeat()) OnSkip.ExecuteIfBound();
		return FReply::Handled();
	}
	virtual FReply OnKeyUp(const FGeometry&, const FKeyEvent&) override { return FReply::Handled(); }
	virtual FReply OnKeyChar(const FGeometry&, const FCharacterEvent&) override { return FReply::Handled(); }
	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override
	{
		OnSkip.ExecuteIfBound();
		return FReply::Handled();
	}
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override
	{ return OnMouseButtonDown(Geometry, Event); }
	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
	virtual FReply OnTouchStarted(const FGeometry& Geometry, const FPointerEvent& Event) override
	{ return OnMouseButtonDown(Geometry, Event); }
private:
	FSimpleDelegate OnSkip;
};
