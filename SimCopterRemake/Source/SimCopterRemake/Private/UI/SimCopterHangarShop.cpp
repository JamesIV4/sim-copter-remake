// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SimCopterHangarShop.h"

#include "Flight/SimCopterHelicopterPawn.h"
#include "Game/SimCopterCareerSubsystem.h"
#include "Missions/SimCopterMissionSystem.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "UI/SimCopterHangarArt.h"

namespace
{
using namespace SimCopterMissions;

// FUN_0042d840's equipment permutation: upgrades page row -> equipment bit index.
constexpr int32 UpgradeRowToEquipmentIndex[SimCopterHangarShop::UpgradeRowCount] = { 0, 1, 3, 4, 2 };

// The tool each equipment bit index stands for (registry order: bucket, megaphone, harness,
// tear gas, cannon - the same order FSimCopterEquipmentDefinition::EquipmentIndex numbers).
constexpr ESimCopterHelicopterTool EquipmentIndexToTool[5] = {
	ESimCopterHelicopterTool::WaterBucket,
	ESimCopterHelicopterTool::Megaphone,
	ESimCopterHelicopterTool::RescueHarness,
	ESimCopterHelicopterTool::TearGas,
	ESimCopterHelicopterTool::WaterCannon,
};

// Strings 490..494, in upgrades page row order.
const TCHAR* const UpgradeDescriptions[SimCopterHangarShop::UpgradeRowCount] = {
	TEXT("Bambi Bucket - No one seems to know why it's called this. Used for dropping water on fires. Refilled by dropping it into any convenient open water source."),
	TEXT("Megaphone -Allows you to communicate with people and cars below. Useful in emergency, traffic and law enforcement missions. Effective range: 300 meters."),
	TEXT("Tear Gas Launcher - For law enforcement use only. Useful for when your citizens get out of line. Holds ten canisters in delightful non-toxic and biodegradable forms."),
	TEXT("Water Cannon - Basically a giant squirt gun. Eliminates need to fly directly over fires. You'll need the bucket for refills. The high pressure causes significant recoil."),
	TEXT("Rescue Harness - Used for picking up people in emergency situations. When you need to rescue people and you can't land then the rescue harness is the right tool for the job."),
};

// Strings 410..414: the inventory's five tick columns, left to right.
const TCHAR* const InventoryColumnNames[SimCopterHangarShop::InventoryColumnCount] = {
	TEXT("Rescue Harness"),
	TEXT("Bambi Bucket"),
	TEXT("Water Cannon"),
	TEXT("Megaphone"),
	TEXT("Teargas"),
};

constexpr ESimCopterHelicopterTool InventoryColumnTools[SimCopterHangarShop::InventoryColumnCount] = {
	ESimCopterHelicopterTool::RescueHarness,
	ESimCopterHelicopterTool::WaterBucket,
	ESimCopterHelicopterTool::WaterCannon,
	ESimCopterHelicopterTool::Megaphone,
	ESimCopterHelicopterTool::TearGas,
};

// Strings 400..408, indexed by runtime type.
const TCHAR* const ModelNames[9] = {
	TEXT("Jet Ranger"),
	TEXT("MD 500"),
	TEXT("Apache"),
	TEXT("Bell 212"),
	TEXT("Schweizer 300"),
	TEXT("Agusta"),
	TEXT("Dauphin"),
	TEXT("MD Explorer"),
	TEXT("MD 520"),
};

// Strings 460..467, by catalog row.
const TCHAR* const CatalogHistory[SimCopterHangarLayout::CatalogTabCount] = {
	TEXT("Originally the Hughes 300.\nFirst Flight as 300C: 1969."),
	TEXT("Originally designed for the US Army.\nFirst Flight as JetRanger III: 1977."),
	TEXT("Also known as the Hughes 500\nFirst Flight of MD500E: 1982."),
	TEXT("First NOTAR helicopter.\nFirst Flight: 1990."),
	TEXT("Twin engine derivative of Bell 204/205.\nFirst Flight: 1969."),
	TEXT("First Flight: 1971.\nUpgraded to A 109A MkII in 1981."),
	TEXT("Twin engine derivative of Dauphin.\nFirst Flight: 1975."),
	TEXT("First new helicopter in 1990s.\nFirst Flight: 1992."),
};

// Strings 470..477.
const TCHAR* const CatalogSpecialties[SimCopterHangarLayout::CatalogTabCount] = {
	TEXT("Training and utility.\nAlso for agricultural spraying."),
	TEXT("Light utility and news station.\nSmaller version of LongRanger."),
	TEXT("One of the most successful ever.\nMilitary/commercial/medical uses."),
	TEXT("Light utility uses.\nQuietest helicopter in the world."),
	TEXT("Commerical: Search and rescue.\nMilitary: Utility, assault and rescue."),
	TEXT("Utility and corporate use.\nRetractable wheeled undercarriage."),
	TEXT("Wide range of uses, from\nrescue to military."),
	TEXT("Executive and utility.\nQuiet NOTAR performance."),
};

// Strings 480..487.
const TCHAR* const CatalogDescriptions[SimCopterHangarLayout::CatalogTabCount] = {
	TEXT("Engine: Single Textron Piston\nEmpty Weight: 500 kg/1100 lbs.\nCapacity: 430 kg/950 lbs.\nSeating: 2 passengers\nSpeed: 150 km/h/80 kt"),
	TEXT("Engine: Single Allison Turboshaft\nEmpty Weight: 750 kg/1650 lbs.\nCapacity: 700kg/1550 lbs.\nSeating: 4 passengers\nSpeed: 220 km/h/120 kt"),
	TEXT("Engine: Single Allison Turboshaft\nEmpty Weight: 670 kg/1500 lbs.\nCapacity: 690 kg/1500 lbs.\nSeating: 4 passengers\nSpeed: 230 km/h/125 kt"),
	TEXT("Engine: Single Allison Turboshaft\nEmpty Weight: 720 kg/1600 lbs.\nCapacity: 800 kg/1750 lbs.\nSeating: 4 passengers\nSpeed: 240 km/h/130 kt"),
	TEXT("Engine: Twin Turbine\nEmpty Weight: 2800 kg/6300 lbs.\nCapacity: 2200 kg/4900 lbs.\nSeating: 14 passengers\nSpeed: 185 km/h/100kt"),
	TEXT("Engine: Twin Turbine\nEmpty Weight: 1600 kg/3000 lbs.\nCapacity: 1100 kg/2500 lbs.\nSeating: 7 passengers\nSpeed: 280 km/h/150 kt"),
	// The accented e is escaped so this table survives a non-UTF-8 read of the file.
	TEXT("Engine: Two Turbom\u00E9ca Turboshafts\nEmpty Weight: 2300 kg/5000 lbs.\nCapacity: 2000 kg/4350 lbs.\nSeating: 13 passengers\nSpeed: 280 km/h/150 kt"),
	TEXT("Engine: Two PW206A Turboshafts\nEmpty Weight: 1500 kg/3300 lbs.\nCapacity: 1200 kg/2600 lbs.\nSeating: 7 passengers\nSpeed: 275 km/h/150 kt"),
};

// Strings 570..587, paired with the type bit each one names.
struct FMissionTypeName
{
	int32 TypeMask;
	const TCHAR* Name;
};

const FMissionTypeName MissionTypeNames[] = {
	{ TYPE_Riot,          TEXT("Riot") },            // 571
	{ TYPE_RooftopRescue, TEXT("Rooftop Rescue") },  // 572
	{ TYPE_BoatRescue,    TEXT("Boat Rescue") },     // 573
	{ TYPE_TrainRescue,   TEXT("Train Rescue") },    // 574
	{ TYPE_Medevac,       TEXT("Medevac") },         // 575
	{ TYPE_Transport,     TEXT("Transport") },       // 576
	{ TYPE_BuildingFire,  TEXT("Fire") },            // 577
	{ TYPE_PlaneCrash,    TEXT("Plane Crash") },     // 578
	{ TYPE_TrainCrash,    TEXT("Train Crash") },     // 579
	{ TYPE_Burglar,       TEXT("Burglar") },         // 580
	{ TYPE_Arsonist,      TEXT("Arsonist") },        // 581
	{ TYPE_Mugger,        TEXT("Mugger") },          // 582
	{ TYPE_Robber,        TEXT("Robber") },          // 583
	{ TYPE_CarFire,       TEXT("Burning Car") },     // 584
	{ TYPE_TrafficJam,    TEXT("Traffic Jam") },     // 585
	{ TYPE_Ufo,           TEXT("Non-Mission Event") }, // 587
};

USimCopterCareerSubsystem* GetCareer(const SimCopterHangarShop::FContext& Context)
{
	return Context.Career.Get();
}
}

namespace SimCopterHangarShop
{
int32 GetEquipmentIndexForUpgradeRow(const int32 UpgradeRow)
{
	return (UpgradeRow >= 0 && UpgradeRow < UpgradeRowCount) ? UpgradeRowToEquipmentIndex[UpgradeRow] : INDEX_NONE;
}

ESimCopterHelicopterTool GetToolForUpgradeRow(const int32 UpgradeRow)
{
	const int32 EquipmentIndex = GetEquipmentIndexForUpgradeRow(UpgradeRow);
	return EquipmentIndex == INDEX_NONE ? ESimCopterHelicopterTool::Count : EquipmentIndexToTool[EquipmentIndex];
}

const TCHAR* GetUpgradeDescription(const int32 UpgradeRow)
{
	return (UpgradeRow >= 0 && UpgradeRow < UpgradeRowCount) ? UpgradeDescriptions[UpgradeRow] : TEXT("");
}

ESimCopterHelicopterTool GetToolForInventoryColumn(const int32 ColumnIndex)
{
	return (ColumnIndex >= 0 && ColumnIndex < InventoryColumnCount)
		? InventoryColumnTools[ColumnIndex]
		: ESimCopterHelicopterTool::Count;
}

const TCHAR* GetInventoryColumnName(const int32 ColumnIndex)
{
	return (ColumnIndex >= 0 && ColumnIndex < InventoryColumnCount) ? InventoryColumnNames[ColumnIndex] : TEXT("");
}

const TCHAR* GetModelDisplayName(const int32 TypeIndex)
{
	return (TypeIndex >= 0 && TypeIndex < UE_ARRAY_COUNT(ModelNames)) ? ModelNames[TypeIndex] : TEXT("");
}

const TCHAR* GetCatalogHistory(const int32 CatalogRow)
{
	return (CatalogRow >= 0 && CatalogRow < SimCopterHangarLayout::CatalogTabCount) ? CatalogHistory[CatalogRow] : TEXT("");
}

const TCHAR* GetCatalogSpecialties(const int32 CatalogRow)
{
	return (CatalogRow >= 0 && CatalogRow < SimCopterHangarLayout::CatalogTabCount) ? CatalogSpecialties[CatalogRow] : TEXT("");
}

const TCHAR* GetCatalogDescription(const int32 CatalogRow)
{
	return (CatalogRow >= 0 && CatalogRow < SimCopterHangarLayout::CatalogTabCount) ? CatalogDescriptions[CatalogRow] : TEXT("");
}

const TCHAR* GetInventoryHeaderCentre() { return TEXT("MOOSE\nAVIONICS"); }
const TCHAR* GetInventoryHeaderLeft() { return TEXT("MAX AERO\n3547 N. CAROLINA AVE.\nSIMCITY, SIMWORLD\n(707) 492-3154"); }
const TCHAR* GetInventoryHeaderRight() { return TEXT("REECE INC.\n777 HILARY DR.\nBUBBER, IN SIMUSA"); }
const TCHAR* GetUpgradeHeaderCentre() { return TEXT("MATTIE T./MIKE B. ELECTRONIX, INC."); }
const TCHAR* GetUpgradeHeaderLeft() { return TEXT("MOOSE\nAVIONICS\n1220 JASE DR.\n997 TALI, SIMUSA 47078"); }
const TCHAR* GetUpgradeHeaderRight() { return TEXT("MYKA/MONIQUE-  QUESTAR VID. CORP.\nROALAINE VIA JOSE K.\nROIANNE QUO ORIANNA T."); }

const TCHAR* GetMissionTypeLogName(const int32 TypeMask)
{
	// Compound masks (rescue 0x90 / 0x110 / 0x80010) have to win over the single bits they
	// contain, so the table is walked most-specific first.
	const FMissionTypeName* Best = nullptr;
	for (const FMissionTypeName& Entry : MissionTypeNames)
	{
		if ((TypeMask & Entry.TypeMask) != Entry.TypeMask)
		{
			continue;
		}
		if (Best == nullptr || FMath::CountBits(static_cast<uint32>(Entry.TypeMask)) > FMath::CountBits(static_cast<uint32>(Best->TypeMask)))
		{
			Best = &Entry;
		}
	}

	// String 570.
	return Best != nullptr ? Best->Name : TEXT("Unknown");
}

bool FContext::IsUsable() const
{
	return Career.IsValid();
}

int32 GetCurrentFunds(const FContext& Context)
{
	const ASimCopterMissionSystemActor* Missions = Context.Missions.Get();
	return Missions != nullptr ? Missions->GetSessionCash() : 0;
}

FRowState GetHelicopterRowState(const FContext& Context, const int32 CatalogRow)
{
	FRowState State;

	USimCopterCareerSubsystem* Career = GetCareer(Context);
	const int32 TypeIndex = SimCopterHangarLayout::GetTypeIndexForCatalogRow(CatalogRow);
	if (Career == nullptr || TypeIndex == INDEX_NONE)
	{
		State.Reason = TEXT("This model is not for sale.");
		return State;
	}

	State.bOwned = Career->OwnsHelicopter(TypeIndex);
	State.ItemValue = State.bOwned ? Career->GetHelicopterTradeInValue(TypeIndex) : Career->GetHelicopterPrice(TypeIndex);

	if (State.bOwned)
	{
		// The original never lets the books empty: something has to be on the pad.
		State.bCanSell = Career->GetOwnedHelicopterCount() > 1;
		if (!State.bCanSell)
		{
			State.Reason = TEXT("You cannot sell your only helicopter.");
		}
	}
	else
	{
		State.bCanBuy = State.ItemValue > 0 && GetCurrentFunds(Context) >= State.ItemValue;
		if (!State.bCanBuy)
		{
			State.Reason = State.ItemValue > 0 ? TEXT("Not enough funds.") : TEXT("No price for this model.");
		}
	}

	return State;
}

FRowState GetUpgradeRowState(const FContext& Context, const int32 UpgradeRow)
{
	FRowState State;

	const ASimCopterHelicopterPawn* Helicopter = Context.Helicopter.Get();
	const ESimCopterHelicopterTool Tool = GetToolForUpgradeRow(UpgradeRow);
	if (Helicopter == nullptr || Tool == ESimCopterHelicopterTool::Count)
	{
		State.Reason = TEXT("No helicopter to fit this to.");
		return State;
	}

	const int32 Bit = SimCopterHelicopterRegistry::GetToolCareerBit(Tool);
	State.bOwned = Helicopter->GetEquipmentState().HasCareerBit(Bit);
	State.ItemValue = State.bOwned
		? SimCopterHelicopterRegistry::GetEquipmentSellValue(Tool)
		: SimCopterHelicopterRegistry::GetEquipmentPrice(Tool);

	if (State.bOwned)
	{
		State.bCanSell = true;
	}
	else
	{
		State.bCanBuy = GetCurrentFunds(Context) >= State.ItemValue;
		if (!State.bCanBuy)
		{
			State.Reason = TEXT("Not enough funds.");
		}
	}

	return State;
}

bool BuyHelicopter(const FContext& Context, const int32 CatalogRow, FString& OutMessage)
{
	USimCopterCareerSubsystem* Career = GetCareer(Context);
	ASimCopterMissionSystemActor* Missions = Context.Missions.Get();
	ASimCopterHelicopterPawn* Helicopter = Context.Helicopter.Get();
	const int32 TypeIndex = SimCopterHangarLayout::GetTypeIndexForCatalogRow(CatalogRow);

	const FRowState State = GetHelicopterRowState(Context, CatalogRow);
	if (Career == nullptr || Missions == nullptr || TypeIndex == INDEX_NONE || !State.bCanBuy)
	{
		OutMessage = State.Reason.IsEmpty() ? TEXT("That purchase is not available.") : State.Reason;
		return false;
	}

	Missions->AddSessionCash(-State.ItemValue);
	Career->SetHelicopterOwned(TypeIndex, true);

	// FUN_0048b1a0 also makes the new airframe the active one.
	if (Helicopter != nullptr && !Helicopter->SwitchHelicopterModel(TypeIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter hangar: bought %s but could not switch to it - %s"),
			GetModelDisplayName(TypeIndex),
			*Helicopter->GetLastModelSwitchStatus());
	}

	OutMessage = FString::Printf(TEXT("Bought %s for %d Bucks."), GetModelDisplayName(TypeIndex), State.ItemValue);
	return true;
}

bool SellHelicopter(const FContext& Context, const int32 CatalogRow, FString& OutMessage)
{
	USimCopterCareerSubsystem* Career = GetCareer(Context);
	ASimCopterMissionSystemActor* Missions = Context.Missions.Get();
	ASimCopterHelicopterPawn* Helicopter = Context.Helicopter.Get();
	const int32 TypeIndex = SimCopterHangarLayout::GetTypeIndexForCatalogRow(CatalogRow);

	const FRowState State = GetHelicopterRowState(Context, CatalogRow);
	if (Career == nullptr || Missions == nullptr || TypeIndex == INDEX_NONE || !State.bCanSell)
	{
		OutMessage = State.Reason.IsEmpty() ? TEXT("That sale is not available.") : State.Reason;
		return false;
	}

	// Move off the airframe first: switching after the books are updated would have nothing to
	// move to if this was the one being flown.
	const int32 Replacement = Career->FindFirstOwnedHelicopterTypeIndex(TypeIndex);
	if (Helicopter != nullptr && Helicopter->GetHelicopterTypeIndex() == TypeIndex && Replacement != INDEX_NONE)
	{
		Helicopter->SwitchHelicopterModel(Replacement);
	}

	Missions->AddSessionCash(State.ItemValue);
	Career->SetHelicopterOwned(TypeIndex, false);

	OutMessage = FString::Printf(TEXT("Sold %s for %d Bucks."), GetModelDisplayName(TypeIndex), State.ItemValue);
	return true;
}

bool BuyUpgrade(const FContext& Context, const int32 UpgradeRow, FString& OutMessage)
{
	ASimCopterMissionSystemActor* Missions = Context.Missions.Get();
	ASimCopterHelicopterPawn* Helicopter = Context.Helicopter.Get();
	const ESimCopterHelicopterTool Tool = GetToolForUpgradeRow(UpgradeRow);

	const FRowState State = GetUpgradeRowState(Context, UpgradeRow);
	if (Missions == nullptr || Helicopter == nullptr || Tool == ESimCopterHelicopterTool::Count || !State.bCanBuy)
	{
		OutMessage = State.Reason.IsEmpty() ? TEXT("That purchase is not available.") : State.Reason;
		return false;
	}

	Missions->AddSessionCash(-State.ItemValue);
	Helicopter->SetCareerEquipmentOwned(Tool, true);

	OutMessage = FString::Printf(
		TEXT("Bought the %s for %d Bucks."),
		SimCopterHelicopterRegistry::GetToolDisplayName(Tool),
		State.ItemValue);
	return true;
}

bool SellUpgrade(const FContext& Context, const int32 UpgradeRow, FString& OutMessage)
{
	ASimCopterMissionSystemActor* Missions = Context.Missions.Get();
	ASimCopterHelicopterPawn* Helicopter = Context.Helicopter.Get();
	const ESimCopterHelicopterTool Tool = GetToolForUpgradeRow(UpgradeRow);

	const FRowState State = GetUpgradeRowState(Context, UpgradeRow);
	if (Missions == nullptr || Helicopter == nullptr || Tool == ESimCopterHelicopterTool::Count || !State.bCanSell)
	{
		OutMessage = State.Reason.IsEmpty() ? TEXT("That sale is not available.") : State.Reason;
		return false;
	}

	Missions->AddSessionCash(State.ItemValue);
	Helicopter->SetCareerEquipmentOwned(Tool, false);

	OutMessage = FString::Printf(
		TEXT("Sold the %s for %d Bucks."),
		SimCopterHelicopterRegistry::GetToolDisplayName(Tool),
		State.ItemValue);
	return true;
}
}
