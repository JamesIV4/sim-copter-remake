// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Styling/SlateTypes.h"

class SConstraintCanvas;
class SWidget;
class USimCopterHangarArt;
struct FSlateBrush;

// Shared scaffolding for the original's bitmap-page screens - the main menu (main1.bmp, page id
// 0x7d2), the career city select (career.bmp, 0x7d7), and the in-game Settings screen
// (playmenu.bmp, 0x7d3) with its four sub-dialogs.
//
// All of them are bitmap pages laid out in the original's 640x480 screen space with controls
// placed at hand-written rectangles, exactly like the Check-up dialog, so they are built the same
// way: an SConstraintCanvas in page pixels inside an SScaleBox. Decode and citations:
// Docs/scratchpad/mainmenu-DECODED.md and Docs/scratchpad/settings-DECODED.md.
namespace SimCopterFrontEnd
{
// The original's screen. Every coordinate in these screens is in this space.
constexpr float ScreenWidth = 640.0f;
constexpr float ScreenHeight = 480.0f;

// button.bmp's three frames (normal, pressed, disabled). The dialogs give their buttons
// degenerate 1x1 rectangles because the control sizes itself from this strip.
constexpr float ButtonWidth = 100.0f;
constexpr float ButtonHeight = 28.0f;

struct FRect
{
	float Left = 0.0f;
	float Top = 0.0f;
	float Right = 0.0f;
	float Bottom = 0.0f;

	constexpr float Width() const { return Right - Left; }
	constexpr float Height() const { return Bottom - Top; }
};

// The original's font sizes are Windows font *heights* - the whole cell, internal leading
// included - so the Slate point size that fills the same box is about three quarters of it. Same
// conversion the Check-up dialog uses.
constexpr int32 WindowsHeightToSlatePoints(const int32 WindowsHeight)
{
	return (WindowsHeight * 3) / 4;
}

FSlateFontInfo PageFont(int32 WindowsHeight, bool bBold = false);

// Places a widget at a page rectangle.
void AddAt(const TSharedRef<SConstraintCanvas>& Canvas, const FRect& Rect, TSharedRef<SWidget> Widget);

// The page bitmap itself at Rect, or a plain dark panel when the original game folder is absent.
TSharedRef<SWidget> MakePageImage(USimCopterHangarArt* Art, const FString& FileName);

// Wraps a page-space canvas so it fills the viewport at the original's aspect ratio.
TSharedRef<SWidget> MakeScaledScreen(TSharedRef<SWidget> PageContent);

// A button.bmp button. The style has to outlive Construct, so the caller keeps it.
TSharedRef<SWidget> MakeButton(
	USimCopterHangarArt* Art,
	const FText& Label,
	int32 WindowsFontHeight,
	FOnClicked OnClicked,
	TArray<TSharedRef<FButtonStyle>>& StyleKeepAlive);

// A button with no visuals at all, for the main menu's item rows: the artwork is already printed
// on the page and the row only needs to catch the pointer.
TSharedRef<SWidget> MakeInvisibleHitButton(
	FOnClicked OnClicked,
	FSimpleDelegate OnHovered,
	TArray<TSharedRef<FButtonStyle>>& StyleKeepAlive);

// The two text colours every list page writes into its descriptor, a byte at a time, as
// `80 85 4a` and `ea ef 9a` - Win32 COLORREF, so the low byte is red. An olive that reads as
// engraved on the plate, and a pale yellow-green for the item the selection is on. The main menu
// (FUN_00411900) and the Settings menu (FUN_00437d10) use exactly the same pair.
inline const FLinearColor ItemColor(0x80 / 255.0f, 0x85 / 255.0f, 0x4A / 255.0f, 1.0f);
inline const FLinearColor ItemSelectedColor(0xEA / 255.0f, 0xEF / 255.0f, 0x9A / 255.0f, 1.0f);

// SCHOOK: MenuPageKeyDown 0x0045f040
//
// The list-page key map. It belongs to the page class, not to any one screen, so the main menu and
// the Settings menu share it - the same generic code that lays both of them out (FUN_0045e920).
// Page Up is deliberately not Up: it refuses to wrap.
enum class ENavigation : uint8
{
	None,
	Next,           // Down, Page Down - wraps to the first item past the end
	Previous,       // Up - wraps to the last item at item 0
	PreviousNoWrap, // Page Up
	First,          // Home
	Last,           // End
};

// Returns the index the original's handler would move to, or INDEX_NONE for no change.
int32 GetNavigationTarget(ENavigation Navigation, int32 Selected, int32 Count);

// Maps a key to its navigation action, ENavigation::None when the key is not one of the six.
ENavigation GetNavigationForKey(const struct FKey& Key);

// The front-end screens do not use the 130-slot sound table - each one builds a standalone sound
// object for its own file (menu.wav, menuback.wav, career.wav, carsel.wav) - so these go by name.
// A Slate widget has no world of its own, so they resolve the viewport's.
void PlayScreenSound(const TCHAR* WavName);
void PlayScreenMusic(const TCHAR* WavName);
void StopScreenMusic();
}
