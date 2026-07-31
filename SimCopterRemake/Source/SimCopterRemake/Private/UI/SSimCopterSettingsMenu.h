// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimCopterFrontEndPage.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class USimCopterHangarArt;
struct FButtonStyle;

// SCHOOK: SettingsMenuPage 0x00437d10
//
// SimCopter's in-game Settings screen - page id 0x7d3, drawn on playmenu.bmp, raised by app
// command 0x3f (which pauses the sim first through the reference-counted FUN_004346c0).
//
// Full decode with citations: Docs/scratchpad/settings-DECODED.md. Like the main menu, the whole
// layout is one 0x54-byte stack descriptor and Ghidra aliases its slots, so the values below come
// from dump-asm (Docs/scratchpad/asm-00437d10.txt) and were drawn back over the page art to
// confirm they land on the printed plates (Docs/scratchpad/overlay_settings_rects.py).
namespace SimCopterSettingsMenuLayout
{
using SimCopterFrontEnd::FRect;
using SimCopterFrontEnd::ItemColor;
using SimCopterFrontEnd::ItemSelectedColor;

// Descriptor +0x0c: (50, 10, 51, 11), degenerate, so only the origin matters; playmenu.bmp is
// 343x433 at its own size.
constexpr float PageX = 50.0f;
constexpr float PageY = 10.0f;
constexpr float PageWidth = 343.0f;
constexpr float PageHeight = 433.0f;

// Descriptor +0x30/+0x38/+0x3c. (Languages other than 1 and 2 get first y + 3 and font 20;
// English is language 1.)
constexpr float ItemX = 102.0f;
constexpr float ItemStride = 40.0f;
constexpr int32 ItemFontHeight = 26;

// The page prints eight plates. Descriptor +0x1c/+0x20/+0x24/+0x34 come in two variants and the
// only difference is whether the first plate is used:
//
//   DAT_00518d50 == 1 (a USER game): string 60, eight items, command base 0, first y 64
//   otherwise         (a CAREER)   : string 61, seven items, command base 1, first y 104
//
// so an item's command id is the same either way - which is what lets ESimCopterSettingsItem be a
// single enum. City Settings is missing from a career because FUN_00407bb0 hands that dialog the
// city's own fixed career record rather than the editable global block.
constexpr int32 FullItemCount = 8;
constexpr int32 FirstItemStringId = 60;
constexpr float FirstItemYWithCitySettings = 64.0f;
constexpr float FirstItemYWithoutCitySettings = 104.0f;

// The item rows are text controls, so - as on the main menu (FUN_0045fc60) - a row catches the
// pointer over its font-tall text band, not over the whole 40 px plate.
constexpr float HitLeft = 20.0f;
constexpr float HitRight = 330.0f;

// Row is the position on the page, 0..(VisibleCount-1), not the command id.
constexpr float GetRowTop(const int32 Row, const bool bHasCitySettings)
{
	return (bHasCitySettings ? FirstItemYWithCitySettings : FirstItemYWithoutCitySettings)
		+ ItemStride * static_cast<float>(Row);
}

constexpr FRect GetRowTextRect(const int32 Row, const bool bHasCitySettings)
{
	const float Top = GetRowTop(Row, bHasCitySettings);
	return FRect{ ItemX, Top, PageWidth, Top + static_cast<float>(ItemFontHeight) };
}

constexpr FRect GetRowHitRect(const int32 Row, const bool bHasCitySettings)
{
	const float Top = GetRowTop(Row, bHasCitySettings);
	return FRect{ HitLeft, Top, HitRight, Top + static_cast<float>(ItemFontHeight) };
}
}

// The command ids FUN_0044c9e0 switches on, which are also STRINGTABLE 60..67 in order.
enum class ESimCopterSettingsItem : uint8
{
	CitySettings = 0,
	Graphics = 1,
	Sound = 2,
	Controls = 3,
	SaveGame = 4,
	SaveGameAs = 5,
	LeaveCity = 6,
	Continue = 7,
};

DECLARE_DELEGATE_OneParam(FOnSimCopterSettingsItemChosen, ESimCopterSettingsItem);

class SSimCopterSettingsMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterSettingsMenu)
		: _AllowCitySettings(false)
	{}
		SLATE_ARGUMENT(TObjectPtr<USimCopterHangarArt>, Art)
		/** DAT_00518d50 == 1: a user game, whose city rates are editable. */
		SLATE_ARGUMENT(bool, AllowCitySettings)
		SLATE_EVENT(FOnSimCopterSettingsItemChosen, OnItemChosen)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	int32 GetSelectedRow() const { return SelectedRow; }

	/** Row on the page -> the command id, i.e. the descriptor's command base added back on. */
	static ESimCopterSettingsItem GetItemForRow(int32 Row, bool bHasCitySettings);
	static const FText& GetItemLabel(ESimCopterSettingsItem Item);

private:
	TObjectPtr<USimCopterHangarArt> Art;
	FOnSimCopterSettingsItemChosen OnItemChosen;
	bool bHasCitySettings = false;
	int32 VisibleCount = SimCopterSettingsMenuLayout::FullItemCount;

	// The page object's +0xd4.
	int32 SelectedRow = 0;

	TArray<TSharedRef<FButtonStyle>> ButtonStyles;

	void SetSelectedRow(int32 Row);
	void ActivateSelected();
	bool SelectByMnemonic(TCHAR Character);
};
