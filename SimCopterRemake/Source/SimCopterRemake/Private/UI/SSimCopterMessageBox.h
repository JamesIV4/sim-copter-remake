// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimCopterFrontEndPage.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class USimCopterHangarArt;
struct FButtonStyle;

// SCHOOK: MessageBoxDialog 0x004426c0
//
// The original's own message box - MBox.bmp, built by FUN_004354c0 -> FUN_004426c0 -> its
// [vt+0x04] FUN_004428f0 -> FUN_0043d0c0. One text well and either one centred button or two side
// by side, all four rectangles read out of the assembly.
//
// The front end uses it for the two menu items the remake cannot honour, which is what the
// original's own demo build did with them (STRINGTABLE 653).
namespace SimCopterMessageBoxLayout
{
using SimCopterFrontEnd::FRect;

// MBox.bmp's own size; the page is centred on the original's 640x480 screen.
constexpr float PageWidth = 465.0f;
constexpr float PageHeight = 353.0f;

// FUN_004426c0's text control, font height 18.
constexpr FRect TextRect{ 100.0f, 100.0f, 370.0f, 200.0f };
constexpr int32 TextFontHeight = 18;
constexpr int32 ButtonFontHeight = 14;

// FUN_0043d0c0: one button at (244,256), or two at (194,256) and (294,256).
constexpr float ButtonY = 256.0f;
constexpr float SingleButtonX = 244.0f;
constexpr float LeftButtonX = 194.0f;
constexpr float RightButtonX = 294.0f;
}

class SSimCopterMessageBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterMessageBox) {}
		SLATE_ARGUMENT(TObjectPtr<USimCopterHangarArt>, Art)
		SLATE_ARGUMENT(FText, Message)
		SLATE_EVENT(FSimpleDelegate, OnDismissed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	FSimpleDelegate OnDismissed;
	TArray<TSharedRef<FButtonStyle>> ButtonStyles;
};
