// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SimCopterHangarArt.generated.h"

class UTexture2D;
class UMediaPlayer;
class UMediaTexture;
struct FSlateBrush;

// Quarter turns applied after a sub-rectangle is cut out. The cockpit's dispatch strip needs
// left and right arrows and the original only ships an up/down rocker, so its two halves are
// turned on their side.
enum class ESimCopterArtRotation : uint8
{
	None,
	Clockwise90,
	CounterClockwise90,
};

// Loads the original artwork out of the shipped BMP folder and hands it to Slate as brushes.
// The hangar shell was the first caller and named the class; the cockpit's tool flaps
// (SimCopterFlapLayout) load their pages and button strips through it too.
//
// Every page the shell draws is one 8-bit Windows bitmap under BMP/, listed here with the
// executable pointer that names it:
//
//   dhangar.bmp / nhangar.bmp   PTR_s_dhangar_bmp_004f931c / _004f9320, chosen by FUN_0043c540
//                               on the day/night flag at ui + 0x112
//   catalog.bmp / cataloge.bmp  PTR_s_catalog_bmp_004f8d50 / _004f8d54, the two catalog pages
//                               FUN_0042b840 swaps between (a helicopter row, or the upgrades
//                               page when the row is outside 0..7)
//   cat_<model>.bmp             the eight 430x228 blueprint drawings
//   cat_<model>t.bmp            the nine 470x40 tab strips (eight models plus cat_equt.bmp for
//                               the upgrades page)
//   cat_btn.bmp                 the catalog's three-frame 86x28 button
//   button.bmp                  the shell's three-frame 100x28 button
//   mssnlog.bmp                 the mission log page
//   invntory.bmp / invnchk.bmp  the inventory clipboard and its tick mark
//
// Palette index 254 (cyan) is the colour key across the original's sprite bitmaps - the same
// index FSimCopterPopulationSprite uses for PEOPLE1 - so the button strips and the tick load
// with it keyed out and the full-page backgrounds load opaque.
UCLASS()
class SIMCOPTERREMAKE_API USimCopterHangarArt : public UObject
{
	GENERATED_BODY()

public:
	// Palette index the original keys out of its sprite bitmaps.
	static constexpr int32 TransparentPaletteIndex = 254;

	// Original page size every layout coordinate in the hangar shell is expressed in.
	static constexpr float PageWidth = 640.0f;
	static constexpr float PageHeight = 480.0f;

	void SetOriginalGameRoot(const FString& InOriginalGameRoot);
	const FString& GetOriginalGameRoot() const { return OriginalGameRoot; }

	// True when the BMP folder was found. A missing folder is not fatal: every accessor returns
	// null and the shell falls back to plain Slate panels.
	bool IsUsable() const;

	// Whole bitmap as a brush, cached by file name. Null when the file is missing or unreadable.
	const FSlateBrush* GetBitmap(const FString& FileName, bool bColorKeyed = false);

	// Full-colour artwork bundled with the remake under Content/Slate. This is kept separate
	// from GetBitmap because these images are modern RGBA files, not original paletted BMPs.
	const FSlateBrush* GetBundledSlateImage(const FString& FileName);

	// The transcoded original MENUSKY.SMK loop. Smacker is not a UE-supported runtime format,
	// so Tools/Unreal/BakeMenuSky.py produces the media file without changing its 201 frames or
	// 71 ms cadence. Null keeps the front end usable when the user's original data was not baked.
	const FSlateBrush* GetMenuSkyMovieBrush();
	void StopMenuSkyMovie();

	// The authentic 200x108 CITY<N>_S.SMK loop shown inside a career-selection panel.
	// Tools/Unreal/BakeCareerPreviews.py transcodes the user-provided Smacker files for UE.
	const FSlateBrush* GetCareerCityMovieBrush(int32 CityIndex);
	void StopCareerCityMovies();

	// One frame of a horizontal strip: button.bmp is three 100x28 frames (normal, pressed,
	// disabled) and cat_btn.bmp three 86x28 ones.
	const FSlateBrush* GetStripFrame(const FString& FileName, int32 FrameIndex, int32 FrameCount);

	// Any sub-rectangle of a bitmap, optionally turned a quarter. The cockpit's flapbtn strips
	// pack frames of unequal width (flapbtn0.bmp is two 17x29 rocker frames followed by two
	// 20x29 ones), so they cannot go through GetStripFrame.
	const FSlateBrush* GetSubImage(
		const FString& FileName,
		const FIntRect& Source,
		bool bColorKeyed = true,
		ESimCopterArtRotation Rotation = ESimCopterArtRotation::None);

	// Somewhere UObject-shaped to hang a texture the remake builds at runtime rather than loads.
	// The cockpit map re-uploads its rasterised buffer every tick and its Slate widget cannot own
	// the texture itself.
	void RegisterRuntimeTexture(const FString& Key, UTexture2D* Texture);
	UTexture2D* FindRuntimeTexture(const FString& Key) const;

	// cat_<model>.bmp for a catalog row, or null for a row without a drawing.
	const FSlateBrush* GetCatalogDrawing(int32 CatalogRow);

	// cat_<model>t.bmp for a catalog row; CatalogRow == INDEX_NONE gives cat_equt.bmp, the
	// upgrades page's strip.
	const FSlateBrush* GetCatalogTabStrip(int32 CatalogRow);

private:
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> Textures;

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> MenuSkyPlayer;

	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> MenuSkyTexture;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UMediaPlayer>> CareerCityPlayers;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UMediaTexture>> CareerCityTextures;

	// Brushes point at the textures above; they are plain structs, so they live outside the
	// UPROPERTY graph and are keyed the same way.
	TMap<FString, TSharedPtr<FSlateBrush>> Brushes;
	TSharedPtr<FSlateBrush> MenuSkyMovieBrush;
	TMap<int32, TSharedPtr<FSlateBrush>> CareerCityMovieBrushes;

	FString OriginalGameRoot;

	// Resolves BMP/<FileName> case-insensitively; empty when it is not there.
	FString ResolveBitmapPath(const FString& FileName) const;
	FString ResolveMenuSkyMoviePath() const;
	FString ResolveCareerCityMoviePath(int32 CityIndex) const;

	// An empty Source takes the whole bitmap.
	const FSlateBrush* BuildBrush(
		const FString& CacheKey,
		const FString& FileName,
		bool bColorKeyed,
		const FIntRect& Source,
		ESimCopterArtRotation Rotation);
};

// Where the hangar shell puts things on the original's 640x480 pages.
//
// The numbers are measured off the shipped bitmaps themselves - the panels the original draws
// into are printed on the page art - and cross-checked against the two rectangles FUN_0042b840
// sets by hand for the value readouts (upgrades page: label (0x152,0x138)-(0x1ca,0x154) and
// (0x152,0x147)-(0x1ca,0x15a), value (0x1cc,0x147)-(0x244,0x15a); helicopter page: value
// (0x212,0xb6)-(0x25c,0xd2)).
namespace SimCopterHangarLayout
{
// --- catalog, helicopter page (catalog.bmp) ---

// The page's graph-paper area runs (56,61)-(495,296) and each cat_<model>.bmp carries its own
// 3px grid margin, so the drawing lands here for the two grids to line up.
constexpr float CatalogDrawingX = 53.0f;
constexpr float CatalogDrawingY = 57.0f;
constexpr float CatalogDrawingWidth = 430.0f;
constexpr float CatalogDrawingHeight = 228.0f;

// The tab strip covers the row of tabs printed on the page at x 92..547; the strips carry the
// same row at x 12..467, which fixes the origin.
constexpr float CatalogTabStripX = 80.0f;
constexpr float CatalogTabStripY = 420.0f;
constexpr float CatalogTabStripWidth = 470.0f;
constexpr float CatalogTabStripHeight = 40.0f;

// Catalog rows in tab order, in page coordinates. Every strip draws the eight models in catalog
// order at the same eight positions - only the page corner behind them moves - so one table
// serves all nine strips.
constexpr int32 CatalogTabCount = 8;
constexpr float CatalogTabLeft[CatalogTabCount] = { 92.0f, 151.0f, 209.0f, 266.0f, 323.0f, 381.0f, 438.0f, 497.0f };
constexpr float CatalogTabRight[CatalogTabCount] = { 141.0f, 201.0f, 257.0f, 316.0f, 372.0f, 430.0f, 488.0f, 547.0f };
constexpr float CatalogTabHitHeight = 26.0f;

// Right-hand panel printed at (507,68)-(595,296): funds and value above, three buttons below.
constexpr float CatalogPanelX = 509.0f;
constexpr float CatalogPanelWidth = 86.0f;
constexpr float CatalogFundsLabelY = 72.0f;
constexpr float CatalogFundsValueY = 106.0f;
constexpr float CatalogValueLabelY = 140.0f;
constexpr float CatalogValueValueY = 178.0f;
constexpr float CatalogButtonY[3] = { 213.0f, 240.0f, 267.0f };
constexpr float CatalogButtonWidth = 86.0f;
constexpr float CatalogButtonHeight = 28.0f;

// The two ruled panels under the drawing: History/Specialties on the left, Description right.
constexpr float CatalogHistoryX = 58.0f;
constexpr float CatalogHistoryY = 303.0f;
constexpr float CatalogHistoryWidth = 260.0f;
constexpr float CatalogHistoryHeight = 109.0f;
constexpr float CatalogDescriptionX = 327.0f;
constexpr float CatalogDescriptionY = 303.0f;
constexpr float CatalogDescriptionWidth = 268.0f;
constexpr float CatalogDescriptionHeight = 109.0f;

// --- catalog, upgrades page (cataloge.bmp) ---

// Five equipment cells in the order the page prints them, which is column-major: the left column
// top to bottom, then the right. That order is the shop's catalog row order, and FUN_0042d840's
// literal {0, 1, 3, 4, 2} maps it to the equipment bit index - so row 0 is the bucket, row 1 the
// megaphone, row 2 the tear gas, row 3 the water cannon and row 4 the harness.
constexpr int32 UpgradeRowCount = 5;
// Icon cells (the artwork is printed on the page; these are the click targets).
constexpr float UpgradeIconLeft[UpgradeRowCount] = { 60.0f, 60.0f, 60.0f, 329.0f, 329.0f };
constexpr float UpgradeIconTop[UpgradeRowCount] = { 100.0f, 207.0f, 314.0f, 100.0f, 207.0f };
constexpr float UpgradeIconWidth = 97.0f;
constexpr float UpgradeIconHeight = 98.0f;
// Description cells.
constexpr float UpgradeTextLeft[UpgradeRowCount] = { 159.0f, 159.0f, 159.0f, 428.0f, 428.0f };
constexpr float UpgradeTextTop[UpgradeRowCount] = { 99.0f, 207.0f, 312.0f, 99.0f, 207.0f };
constexpr float UpgradeTextWidth = 167.0f;
constexpr float UpgradeTextHeight = 96.0f;

// The upgrades page's three letterheads (strings 439..441) are *printed on cataloge.bmp*, unlike
// the inventory clipboard's, which are drawn over blank paper. Nothing is laid out for them here
// for that reason.

// The bottom-right cell (329,312)-(593,411) holds the readouts and the buttons.
constexpr float UpgradeFundsLabelX = 338.0f;
constexpr float UpgradeFundsValueX = 460.0f;
constexpr float UpgradeFundsRowY = 311.0f;
constexpr float UpgradeValueRowY = 327.0f;
constexpr float UpgradeLabelWidth = 120.0f;
constexpr float UpgradeValueWidth = 120.0f;
constexpr float UpgradeBuyX = 355.0f;
constexpr float UpgradeBuyY = 348.0f;
constexpr float UpgradeSellY = 378.0f;
constexpr float UpgradeDoneX = 478.0f;
constexpr float UpgradeDoneY = 363.0f;
constexpr float ShellButtonWidth = 100.0f;
constexpr float ShellButtonHeight = 28.0f;

// --- mission log (mssnlog.bmp) ---

constexpr float LogHeaderX = 56.0f;
constexpr float LogHeaderY = 67.0f;
constexpr float LogHeaderWidth = 516.0f;
constexpr float LogHeaderHeight = 30.0f;
constexpr float LogPageX = 56.0f;
constexpr float LogPageY = 98.0f;
constexpr float LogPageWidth = 516.0f;
constexpr float LogPageHeight = 314.0f;
constexpr float LogLineHeight = 15.0f;
constexpr float LogButtonY = 424.0f;
constexpr float LogByTimeX = 92.0f;
constexpr float LogByTypeX = 220.0f;
constexpr float LogDoneX = 420.0f;

// --- inventory (invntory.bmp) ---

// The clipboard's letterhead block, then the table: a name column and five tick columns whose
// order is the original's (strings 410..414) - harness, bucket, cannon, megaphone, tear gas -
// which is also the order FUN_004077f0 stamps the owned-equipment icons in.
//
// The paper starts at y 105 (the pen and the metal clip are printed above it) and the table's
// top rule is at y 165, so the three letterheads have that band to themselves.
constexpr float InventoryHeaderY = 110.0f;
constexpr float InventoryHeaderHeight = 52.0f;
constexpr float InventoryHeaderLeftX = 176.0f;
constexpr float InventoryHeaderCentreX = 268.0f;
constexpr float InventoryHeaderRightX = 372.0f;
constexpr float InventoryHeaderWidth = 100.0f;

constexpr float InventoryNameColumnX = 172.0f;
constexpr float InventoryNameColumnWidth = 123.0f;
constexpr int32 InventoryColumnCount = 5;
constexpr float InventoryColumnLeft[InventoryColumnCount] = { 297.0f, 322.0f, 348.0f, 376.0f, 404.0f };
constexpr float InventoryColumnWidth = 26.0f;
constexpr int32 InventoryRowCount = 11;
constexpr float InventoryFirstRowY = 241.0f;
constexpr float InventoryRowHeight = 18.7f;
constexpr float InventoryDoneX = 505.0f;
constexpr float InventoryDoneY = 424.0f;

// --- hangar shell (dhangar.bmp) ---

// The original's four hangar buttons (strings 125..128). The backdrop is 1131x480 and is drawn
// wider than the 640x480 page, so the buttons are placed in page space along the bottom.
constexpr float HangarButtonY = 436.0f;
constexpr float HangarButtonX[4] = { 60.0f, 205.0f, 350.0f, 495.0f };

// Row of the catalog a helicopter runtime type sits on, or INDEX_NONE for the Apache, which the
// shop never lists. FUN_0042d840's permutation {4, 0, 1, 8, 3, 5, 6, 7} read the other way.
SIMCOPTERREMAKE_API int32 GetCatalogRowForTypeIndex(int32 TypeIndex);

// Inverse of the above: catalog row 0..7 -> runtime type index.
SIMCOPTERREMAKE_API int32 GetTypeIndexForCatalogRow(int32 CatalogRow);
}
