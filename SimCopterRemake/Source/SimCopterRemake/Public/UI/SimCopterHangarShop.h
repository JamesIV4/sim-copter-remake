// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/SimCopterHelicopterRegistry.h"

class ASimCopterHelicopterPawn;
class ASimCopterMissionSystemActor;
class USimCopterCareerSubsystem;

// The hangar catalog's data and its two transactions.
//
// Every string here is the shipped executable's own, read out of SimCopter.exe's English
// STRINGTABLE (resource language 1033; the other languages are the same ids plus a per-language
// block offset). The ids are quoted against each table so a translation pass has somewhere to
// start:
//
//   125..128   the four hangar buttons: Catalog, Mission Log, Inventory, Done
//   400..408   helicopter names, indexed by runtime type
//   410..414   equipment names in the inventory's column order
//   420..422   the inventory clipboard's letterheads
//   430..444   the catalog's field labels and buttons
//   439..441   the upgrades page's letterheads
//   460..467   catalog History, by catalog row
//   470..477   catalog Specialties, by catalog row
//   480..487   catalog Description, by catalog row
//   490..494   the five upgrade descriptions
//   530..532   the mission log's buttons
//   570..587   mission type names
//
// The transactions are FUN_0042d840 (buy) and FUN_0042d9f0 (sell). Both work in *catalog rows*
// and permute to the runtime index: {4, 0, 1, 8, 3, 5, 6, 7} for helicopters (the Apache is not
// on sale) and {0, 1, 3, 4, 2} for equipment. Buying tear gas also writes ten rounds into
// career + 0x54; selling it writes zero.
namespace SimCopterHangarShop
{
// --- upgrades page rows (the page's own column-major order) ---

constexpr int32 UpgradeRowCount = 5;

// FUN_0042d840's literal {0, 1, 3, 4, 2}: upgrades page row -> equipment bit index.
SIMCOPTERREMAKE_API int32 GetEquipmentIndexForUpgradeRow(int32 UpgradeRow);
SIMCOPTERREMAKE_API ESimCopterHelicopterTool GetToolForUpgradeRow(int32 UpgradeRow);
// Strings 490..494.
SIMCOPTERREMAKE_API const TCHAR* GetUpgradeDescription(int32 UpgradeRow);

// --- inventory columns (strings 410..414, also FUN_004077f0's icon stamp order) ---

constexpr int32 InventoryColumnCount = 5;
SIMCOPTERREMAKE_API ESimCopterHelicopterTool GetToolForInventoryColumn(int32 ColumnIndex);
SIMCOPTERREMAKE_API const TCHAR* GetInventoryColumnName(int32 ColumnIndex);

// --- catalog copy ---

// String 400 + runtime type index. This is the shop's spelling, which differs from the
// registry's heli.twk section names ("MD 500" vs "Hughes 500", "Schweizer" vs "Schwiezer").
SIMCOPTERREMAKE_API const TCHAR* GetModelDisplayName(int32 TypeIndex);
SIMCOPTERREMAKE_API const TCHAR* GetCatalogHistory(int32 CatalogRow);
SIMCOPTERREMAKE_API const TCHAR* GetCatalogSpecialties(int32 CatalogRow);
SIMCOPTERREMAKE_API const TCHAR* GetCatalogDescription(int32 CatalogRow);

// --- letterheads and labels ---

// The inventory clipboard's header band is blank paper, so these three are drawn at runtime.
SIMCOPTERREMAKE_API const TCHAR* GetInventoryHeaderCentre();  // 420
SIMCOPTERREMAKE_API const TCHAR* GetInventoryHeaderLeft();    // 421
SIMCOPTERREMAKE_API const TCHAR* GetInventoryHeaderRight();   // 422

// The upgrades page's equivalents. cataloge.bmp already has them printed on it, so the shell
// does not draw them - they are kept here because the ids belong with the rest of the table and
// a localisation pass will need them.
SIMCOPTERREMAKE_API const TCHAR* GetUpgradeHeaderCentre();    // 439
SIMCOPTERREMAKE_API const TCHAR* GetUpgradeHeaderLeft();      // 440
SIMCOPTERREMAKE_API const TCHAR* GetUpgradeHeaderRight();     // 441

// String 570 + type slot: the mission log's own name for a mission type mask.
SIMCOPTERREMAKE_API const TCHAR* GetMissionTypeLogName(int32 TypeMask);

// --- transactions ---

// Everything the hangar needs to price and settle a sale. Any member may be null; the
// operations below check before they touch anything and report what stopped them.
struct SIMCOPTERREMAKE_API FContext
{
	TWeakObjectPtr<USimCopterCareerSubsystem> Career;
	TWeakObjectPtr<ASimCopterMissionSystemActor> Missions;
	TWeakObjectPtr<ASimCopterHelicopterPawn> Helicopter;

	bool IsUsable() const;
};

// What the catalog's Buy/Sell pair may do with the row that is showing.
struct SIMCOPTERREMAKE_API FRowState
{
	bool bOwned = false;
	// FUN_0048b050 / FUN_0048b0f0 when not owned, FUN_0048b070 / FUN_0048b150 when owned - the
	// number the page prints as "Item Value".
	int32 ItemValue = 0;
	bool bCanBuy = false;
	bool bCanSell = false;
	// Why Buy or Sell is greyed out, for the status line. Empty when both are live.
	FString Reason;
};

SIMCOPTERREMAKE_API int32 GetCurrentFunds(const FContext& Context);

// The helicopter catalog row that is on the books plus what its buttons can do.
SIMCOPTERREMAKE_API FRowState GetHelicopterRowState(const FContext& Context, int32 CatalogRow);
SIMCOPTERREMAKE_API FRowState GetUpgradeRowState(const FContext& Context, int32 UpgradeRow);

// FUN_0042d840's helicopter half: pay the price, set career + 0x44's bit, and put the player in
// the new airframe. Returns false with a reason when the money or the row is not there.
SIMCOPTERREMAKE_API bool BuyHelicopter(const FContext& Context, int32 CatalogRow, FString& OutMessage);

// FUN_0042d9f0's helicopter half: bank the trade-in, clear the bit, and move the player onto
// whatever else is on the books. The last airframe cannot be sold - the original has nothing to
// fly afterwards either.
SIMCOPTERREMAKE_API bool SellHelicopter(const FContext& Context, int32 CatalogRow, FString& OutMessage);

// FUN_0042d840's equipment half, tear gas rounds included.
SIMCOPTERREMAKE_API bool BuyUpgrade(const FContext& Context, int32 UpgradeRow, FString& OutMessage);
SIMCOPTERREMAKE_API bool SellUpgrade(const FContext& Context, int32 UpgradeRow, FString& OutMessage);
}
