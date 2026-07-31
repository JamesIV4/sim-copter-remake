// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimCopterFrontEndPage.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;
class USimCopterHangarArt;
struct FButtonStyle;

// SCHOOK: CareerSelectPage 0x00457c90
//
// The screen "New Career Game" leads to - page id 0x7d7, drawn on career.bmp - where you pick one
// of up to three cities. Reached from the main menu for a new career (the trio is always
// {0, 1, 2}) and from the end of a city for a career advancement (the trio is that city's
// successors in FUN_00408370's graph).
//
// Full decode: Docs/scratchpad/mainmenu-DECODED.md; the rectangles were drawn back over the page
// art by Docs/scratchpad/overlay_career_rects.py.
namespace SimCopterCareerSelectLayout
{
using SimCopterFrontEnd::FRect;

// career.bmp is 614x435 and FUN_00411ca0 gives the page a degenerate rect, so it sits at (0,0)
// of the original's 640x480 screen and every coordinate below is in its own pixels.
constexpr float PageWidth = 614.0f;
constexpr float PageHeight = 435.0f;

constexpr int32 PanelCount = 3;

// FUN_00457c90 screen[0x1e..0x29]: two panels across the top, one below the left of them.
constexpr FRect PanelRect[PanelCount] = {
	{  77.0f,  71.0f, 277.0f, 179.0f },
	{ 339.0f,  71.0f, 539.0f, 179.0f },
	{  77.0f, 249.0f, 277.0f, 357.0f },
};

// The two readouts in the upper right well: STRINGTABLE 240 + city, then 290 + career level.
// Font height 18, centred, and a colour written as the bytes b5 f0 00.
constexpr FRect CityNameRect{ 334.0f, 236.0f, 534.0f, 262.0f };
constexpr FRect LevelNameRect{ 334.0f, 271.0f, 534.0f, 297.0f };
constexpr int32 ReadoutFontHeight = 18;
inline const FLinearColor ReadoutColor(0xB5 / 255.0f, 0xF0 / 255.0f, 0x00 / 255.0f, 1.0f);

// FUN_004580b0 gives every button a degenerate rect and lets button.bmp size it. A new career
// gets OK and Cancel side by side in the lower well; an advancement gets one centred OK, because
// there is nothing to go back to.
constexpr float ButtonY = 338.0f;
constexpr float OkButtonX = 327.0f;
constexpr float CancelButtonX = 431.0f;
constexpr float OkOnlyButtonX = 380.0f;
constexpr int32 ButtonFontHeight = 14;

// FUN_004590b0's four border strips per panel - left, top, right, bottom. carsel.bmp holds two
// copies of them in the page's own coordinate space: the glowing frames at y + 0 and the plain
// ones at y + UnselectedSourceOffsetY, which is how FUN_00458e70 puts a panel back to normal.
// The remake only ever draws the glowing copy, because career.bmp already prints the plain one.
constexpr int32 HighlightStripCount = 4;
constexpr FRect HighlightStrip[PanelCount][HighlightStripCount] = {
	{
		{  54.0f,  51.0f,  77.0f, 216.0f },
		{  77.0f,  51.0f, 276.0f,  69.0f },
		{ 276.0f,  51.0f, 305.0f, 216.0f },
		{  77.0f, 180.0f, 276.0f, 216.0f },
	},
	{
		{ 312.0f,  51.0f, 339.0f, 216.0f },
		{ 339.0f,  51.0f, 537.0f,  69.0f },
		{ 537.0f,  51.0f, 556.0f, 216.0f },
		{ 339.0f, 180.0f, 556.0f, 216.0f },
	},
	{
		{  54.0f, 217.0f,  77.0f, 382.0f },
		{  77.0f, 217.0f, 276.0f, 248.0f },
		{ 276.0f, 217.0f, 305.0f, 382.0f },
		{  77.0f, 359.0f, 276.0f, 382.0f },
	},
};
constexpr float UnselectedSourceOffsetY = 360.0f;

// FUN_00458a90's selection wheel. It is hand-written rather than modular - Up and Down are not
// the inverses of each other - so the table is transcribed instead of derived. INDEX_NONE means
// the key does nothing from there.
enum class EPanelNavigation : uint8
{
	Left,
	Right,
	Up,
	Down,
};

int32 GetNavigationTarget(EPanelNavigation Navigation, int32 Selected, int32 Count);
}

DECLARE_DELEGATE_OneParam(FOnSimCopterCareerCityChosen, int32);

class SSimCopterCareerSelect : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterCareerSelect) {}
		SLATE_ARGUMENT(TObjectPtr<USimCopterHangarArt>, Art)
		// Career city indices, 1..3 of them, in panel order.
		SLATE_ARGUMENT(TArray<int32>, Cities)
		// FUN_004580b0 only builds Cancel when the screen was opened for a new career.
		SLATE_ARGUMENT(bool, AllowCancel)
		SLATE_EVENT(FOnSimCopterCareerCityChosen, OnAccepted)
		SLATE_EVENT(FSimpleDelegate, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	int32 GetSelectedPanel() const { return SelectedPanel; }

private:
	TObjectPtr<USimCopterHangarArt> Art;
	TArray<int32> Cities;
	bool bAllowCancel = true;
	FOnSimCopterCareerCityChosen OnAccepted;
	FSimpleDelegate OnCancelled;

	// screen[0x1d]. The original starts at -1 and FUN_004580b0 immediately selects panel 0.
	int32 SelectedPanel = 0;

	TSharedPtr<STextBlock> CityNameText;
	TSharedPtr<STextBlock> LevelNameText;
	TArray<TSharedRef<FButtonStyle>> ButtonStyles;

	// SCHOOK: CareerSelectSetSelection 0x00458d90 - move the glow and refresh both readouts.
	void SetSelectedPanel(int32 Panel);
	void RefreshReadouts();
	void Accept();
	void Cancel();
};
