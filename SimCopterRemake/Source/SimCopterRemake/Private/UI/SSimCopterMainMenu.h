// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimCopterFrontEndPage.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class USimCopterHangarArt;
struct FButtonStyle;

// SCHOOK: MainMenuPage 0x00411900
//
// SimCopter's main menu - page id 0x7d2, drawn on main1.bmp with the five items the shipped help
// documents (help/English/37ref.htm) and the executable's STRINGTABLE 55..59 agree on.
//
// Full decode with citations: Docs/scratchpad/mainmenu-DECODED.md. The layout below is read out of
// FUN_00411900's page descriptor (via dump-asm, because Ghidra aliases its stack slots) and out of
// the two blit tables FUN_0045fe10 / FUN_0045fed0 build, and every rectangle was drawn back over
// the page art to confirm it lands on the printed furniture
// (Docs/scratchpad/overlay_mainmenu_rects.py).
namespace SimCopterMainMenuLayout
{
using SimCopterFrontEnd::FRect;

// FUN_00411900's page rect is (2, 29)-(426, 416), but the control sizes itself from main1.bmp, so
// only the origin matters and the size is the bitmap's own 425x414.
constexpr float PageX = 2.0f;
constexpr float PageY = 29.0f;
constexpr float PageWidth = 425.0f;
constexpr float PageHeight = 414.0f;

// main2.bmp and main3.bmp, the two hose decorations. FUN_0045f3d0 gives both degenerate 1x1
// rects at these *screen* positions and lets each size itself; at 81x29 and 213x165 they meet the
// page's top and right edges on the original's 640x480 screen.
constexpr float HoseTopX = 289.0f;
constexpr float HoseTopY = 0.0f;
constexpr float HoseTopWidth = 81.0f;
constexpr float HoseTopHeight = 29.0f;
constexpr float HoseCornerX = 427.0f;
constexpr float HoseCornerY = 315.0f;
constexpr float HoseCornerWidth = 213.0f;
constexpr float HoseCornerHeight = 165.0f;

// The five items. Descriptor +0x1c/+0x20/+0x30/+0x34/+0x38/+0x3c: first string id 55, five of
// them, x 116, first y 42, font height 26, stride 64. (Languages other than 1 and 2 get y 45 and
// font 20; English is language 1.)
constexpr int32 ItemCount = 5;
constexpr int32 FirstItemStringId = 55;
constexpr float ItemX = 116.0f;
constexpr float FirstItemY = 42.0f;
constexpr float ItemStride = 64.0f;
constexpr int32 ItemFontHeight = 26;

// FUN_0045fc60's hit test is `29 < x < 394` and the item control's own top and bottom. A text
// control is as tall as its font, so a row catches the pointer over its text band, not over the
// whole 64 px plate.
constexpr float HitLeft = 29.0f;
constexpr float HitRight = 394.0f;

constexpr FRect GetItemTextRect(const int32 Index)
{
	return FRect{
		ItemX,
		FirstItemY + ItemStride * static_cast<float>(Index),
		PageWidth,
		FirstItemY + ItemStride * static_cast<float>(Index) + static_cast<float>(ItemFontHeight) };
}

constexpr FRect GetItemHitRect(const int32 Index)
{
	return FRect{
		HitLeft,
		FirstItemY + ItemStride * static_cast<float>(Index),
		HitRight,
		FirstItemY + ItemStride * static_cast<float>(Index) + static_cast<float>(ItemFontHeight) };
}

// Descriptor +0x28/+0x2c, written a byte at a time as 80 85 4a and ea ef 9a - Win32 COLORREF, so
// low byte is red: an olive that reads as engraved on the plate, and a pale yellow-green for the
// item the selection is on.
inline const FLinearColor ItemColor(0x80 / 255.0f, 0x85 / 255.0f, 0x4A / 255.0f, 1.0f);
inline const FLinearColor ItemSelectedColor(0xEA / 255.0f, 0xEF / 255.0f, 0x9A / 255.0f, 1.0f);

// FUN_0045fe10 (main4.bmp, the round indicator lamps) and FUN_0045fed0 (main5.bmp, the arrow
// keys down the left edge). Both bitmaps are two columns: the selected row's source is shifted
// one column right, which lights the lamp and presses the key.
constexpr float LampX = 334.0f;
constexpr float LampColumnWidth = 60.0f;
constexpr float LampTop[ItemCount] = { 31.0f, 87.0f, 151.0f, 215.0f, 279.0f };
constexpr float LampSourceTop[ItemCount] = { 0.0f, 56.0f, 120.0f, 184.0f, 248.0f };
constexpr float LampSourceBottom[ItemCount] = { 56.0f, 120.0f, 184.0f, 248.0f, 312.0f };

constexpr float KeyX = 33.0f;
constexpr float KeyColumnWidth = 39.0f;
constexpr float KeyTop[ItemCount] = { 35.0f, 100.0f, 164.0f, 227.0f, 289.0f };
constexpr float KeySourceTop[ItemCount] = { 0.0f, 65.0f, 129.0f, 192.0f, 254.0f };
constexpr float KeySourceBottom[ItemCount] = { 65.0f, 129.0f, 192.0f, 254.0f, 297.0f };

// FUN_0045f040's key map. Page Up is deliberately not Up: it refuses to wrap.
enum class ENavigation : uint8
{
	None,
	Next,      // Down, Page Down - wraps to the first item past the end
	Previous,  // Up - wraps to the last item at item 0
	PreviousNoWrap, // Page Up
	First,     // Home
	Last,      // End
};

// Returns the index the original's handler would move to, or INDEX_NONE for no change.
int32 GetNavigationTarget(ENavigation Navigation, int32 Selected, int32 Count);
}

// Which item was chosen. The values are the original's own indices, which are also the message
// values FUN_0045f250 posts and FUN_0044c710 switches on.
enum class ESimCopterMainMenuItem : uint8
{
	NewCareerGame = 0,
	OpenCareerGame = 1,
	NewUserGame = 2,
	OpenUserGame = 3,
	Quit = 4,
};

DECLARE_DELEGATE_OneParam(FOnSimCopterMainMenuItemChosen, ESimCopterMainMenuItem);

class SSimCopterMainMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterMainMenu) {}
		SLATE_ARGUMENT(TObjectPtr<USimCopterHangarArt>, Art)
		SLATE_EVENT(FOnSimCopterMainMenuItemChosen, OnItemChosen)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	int32 GetSelectedIndex() const { return SelectedIndex; }

private:
	TObjectPtr<USimCopterHangarArt> Art;
	FOnSimCopterMainMenuItemChosen OnItemChosen;

	// The page object's +0xd4.
	int32 SelectedIndex = 0;

	// Styles handed to SButton have to outlive Construct.
	TArray<TSharedRef<FButtonStyle>> ButtonStyles;

	// SCHOOK: MainMenuSetSelection 0x0045ed60 - recolour, store, and play menu.wav once.
	void SetSelectedIndex(int32 Index);
	// SCHOOK: MainMenuActivate 0x0045f250 - post message 0x3e9 with the item's index.
	void ActivateSelected();
	void Navigate(SimCopterMainMenuLayout::ENavigation Navigation);
	// SCHOOK: MainMenuMnemonic 0x0045eed0 - select the first item whose text starts with Character.
	bool SelectByMnemonic(TCHAR Character);

	static const FText& GetItemLabel(int32 Index);
};
