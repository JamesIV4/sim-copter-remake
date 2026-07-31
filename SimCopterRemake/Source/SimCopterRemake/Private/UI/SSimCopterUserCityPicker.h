// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimCopterFrontEndPage.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class USimCopterHangarArt;
struct FButtonStyle;

// "New User Game" - the remake's stand-in for the Win32 file dialog.
//
// REMAKE DIVERGENCE. FUN_0044c710 answers this item by calling FUN_00406400 with STRINGTABLE
// title 40 ("Open A SimCity File") and filter 41 ("SimCity 2000 Files (*.sc2)"), i.e. a common
// GetOpenFileName dialog - an OS window, not one of the game's own bitmap pages, so there is
// nothing to port pixel for pixel. This picker lists the same .sc2 files from the original game's
// cities folder on the original's own furniture (menu4.bmp, the page its "Show All Keyboard
// Shortcuts" list dialog uses) and keeps the title from string 40.
//
// The panel rectangles are measured off menu4.bmp (Docs/scratchpad/measure_menu4*.py) rather than
// decoded, because the original never lays a file list out on this page.
namespace SimCopterUserCityPickerLayout
{
using SimCopterFrontEnd::FRect;

constexpr float PageWidth = 510.0f;
constexpr float PageHeight = 436.0f;

// The navy band across the top of the page.
constexpr FRect TitleRect{ 88.0f, 38.0f, 410.0f, 70.0f };
// The big pale list panel, whose printed edges are x 59..455, y 90..338.
constexpr FRect ListRect{ 64.0f, 95.0f, 450.0f, 333.0f };
// The printed button plate is x 336..461, y 360..398 - one 100x28 button wide, so Cancel sits on
// the plate beside it.
constexpr float ButtonY = 366.0f;
constexpr float AcceptButtonX = 349.0f;
constexpr float CancelButtonX = 243.0f;
constexpr int32 TitleFontHeight = 22;
constexpr int32 ListFontHeight = 17;
constexpr int32 ButtonFontHeight = 14;
}

DECLARE_DELEGATE_OneParam(FOnSimCopterUserCityChosen, const FString&);

class SSimCopterUserCityPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterUserCityPicker) {}
		SLATE_ARGUMENT(TObjectPtr<USimCopterHangarArt>, Art)
		SLATE_ARGUMENT(TArray<FString>, CityFilePaths)
		SLATE_EVENT(FOnSimCopterUserCityChosen, OnAccepted)
		SLATE_EVENT(FSimpleDelegate, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	TArray<TSharedPtr<FString>> Entries;
	FOnSimCopterUserCityChosen OnAccepted;
	FSimpleDelegate OnCancelled;

	TSharedPtr<SListView<TSharedPtr<FString>>> ListView;
	TArray<TSharedRef<FButtonStyle>> ButtonStyles;

	// Slate's default list and row styles paint an opaque dark background, which would cover the
	// pale panel printed on the page. Both are overridden with transparent brushes and have to
	// outlive Construct.
	TSharedPtr<FTableViewStyle> ListStyle;
	TSharedPtr<FTableRowStyle> RowStyle;

	TSharedRef<ITableRow> MakeRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void Accept();
};
