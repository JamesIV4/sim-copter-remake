// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Types/SlateEnums.h"
#include "UI/SimCopterHangarShop.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SConstraintCanvas;
class USimCopterHangarArt;
struct FButtonStyle;

// The hangar shell: the screen the original drops you into when you walk into your own hangar.
//
// It is four pages, and every one of them is the original's artwork with the original's text
// laid back over it:
//
//   Hangar       High-resolution DHANGAR reconstruction, with the four buttons the shell offers
//                (strings 125..128): Catalog, Mission Log, Inventory, Done.
//   Catalog      catalog.bmp with one of the eight cat_<model>.bmp blueprints, its History /
//                Specialties / Description (strings 460..487), the funds and item value
//                readouts, and Buy / Sell. The Upgrades tab printed at the page's bottom-left
//                swaps in cataloge.bmp and its five equipment rows (strings 490..494).
//   Mission Log  mssnlog.bmp, printing the career log the mission system writes, sorted By Time
//                or By Type (strings 530..532).
//   Inventory    invntory.bmp, one row per airframe on the books and a tick (invnchk.bmp) in
//                each of the five equipment columns (strings 410..414) the career owns.
//
// Layout is done in the original's own 640x480 page space (see SimCopterHangarLayout) and the
// whole page is then scaled to whatever the viewport is, so the coordinates stay comparable to
// the decompiled ones.
class SSimCopterHangarMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterHangarMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<USimCopterHangarArt>, Art)
		SLATE_ARGUMENT(SimCopterHangarShop::FContext, Shop)
		// The player pressed Done on the hangar page: leave the shell.
		SLATE_EVENT(FSimpleDelegate, OnDoneRequested)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	TSharedPtr<SWidget> GetInitialFocusWidget() const { return InitialFocusWidget; }

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	enum class EPage : uint8
	{
		Hangar,
		Catalog,
		MissionLog,
		Inventory,
	};

	// How the mission log is ordered - the original's two buttons (strings 530 / 531).
	enum class ELogSort : uint8
	{
		ByTime,
		ByType,
	};

	TWeakObjectPtr<USimCopterHangarArt> Art;
	SimCopterHangarShop::FContext Shop;
	FSimpleDelegate OnDoneRequested;

	EPage Page = EPage::Hangar;
	ELogSort LogSort = ELogSort::ByTime;

	// Which catalog row is showing. INDEX_NONE is the upgrades page, exactly as FUN_0042b840
	// treats a row outside 0..7.
	int32 CatalogRow = 0;
	// Which of the five upgrade rows the upgrades page has selected (ui + 0x172).
	int32 UpgradeRow = 0;

	FString StatusText;

	// Button styles are held by pointer by SButton, so they have to outlive the rebuild that
	// created them.
	TArray<TSharedPtr<FButtonStyle>> ButtonStyles;
	TArray<TSharedPtr<SWidget>> ControllerFocusableWidgets;

	TSharedPtr<SConstraintCanvas> PageCanvas;
	TSharedPtr<class SOverlay> Backdrop;
	TSharedPtr<SWidget> InitialFocusWidget;

	void ShowPage(EPage NewPage);
	void RebuildPage();

	void BuildHangarPage(SConstraintCanvas& Canvas);
	void BuildCatalogPage(SConstraintCanvas& Canvas);
	void BuildCatalogHelicopterPage(SConstraintCanvas& Canvas);
	void BuildCatalogUpgradesPage(SConstraintCanvas& Canvas);
	void BuildMissionLogPage(SConstraintCanvas& Canvas);
	void BuildInventoryPage(SConstraintCanvas& Canvas);

	// Adds the eight catalog tabs plus the Upgrades tab printed under them.
	void BuildCatalogTabs(SConstraintCanvas& Canvas);

	// Places Content at page coordinates.
	void AddAt(SConstraintCanvas& Canvas, float X, float Y, float Width, float Height, TSharedRef<SWidget> Content);

	// The page background, or a flat panel when the original art is not installed.
	void AddPageBackground(SConstraintCanvas& Canvas, const TCHAR* FileName);

	// A button drawn from one of the original's three-frame button strips.
	TSharedRef<SWidget> MakeArtButton(
		const TCHAR* StripFileName,
		int32 FrameCount,
		const FString& Label,
		FOnClicked OnClicked,
		bool bEnabled = true,
		int32 FontSize = 11);

	// An invisible click target - the catalog tabs and the upgrade cells are printed on the page
	// art, so all they need is a hit box.
	TSharedRef<SWidget> MakeHotspot(FOnClicked OnClicked, const FString& ToolTip);

	static TSharedRef<SWidget> MakePageText(
		const FString& Text,
		int32 FontSize,
		const FLinearColor& Color,
		ETextJustify::Type Justify = ETextJustify::Left,
		bool bBold = false,
		bool bWrap = true,
		float WrapWidth = 0.0f);

	FReply HandleShowPage(EPage NewPage);
	FReply HandleDone();
	FReply HandleSelectCatalogRow(int32 NewRow);
	FReply HandleSelectUpgradeRow(int32 NewRow);
	FReply HandleBuy();
	FReply HandleSell();
	FReply HandleSetLogSort(ELogSort NewSort);
};
