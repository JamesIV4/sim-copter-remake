// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterHelicopterRegistry.h"

namespace
{
// SCHOOK: HelicopterTypeTable 0x004040e4 (DAT_005040e4 per-type static block)
// SCHOOK: HelicopterRenderObjects 0x00483c20
//
// Object ids come from the FUN_00483c20 switch; seats, tail-rotor mount, NOTAR flag and
// the engine loop name come from the 0x5c-byte static block at DAT_005040e4 (raw bytes in
// Docs/scratchpad/ghidra/out_heli_statics.txt, decoded in heli_tools_models_decode_20260724.md).
// The tail offsets are 16.16 fixed point in the raw block; they are written here as the
// decoded float value in original world units.
struct FRegistryRow
{
	int32 TypeIndex;
	const TCHAR* DisplayName;
	int32 BodyId;
	const TCHAR* BodyName;
	int32 RotorId;
	const TCHAR* RotorName;
	int32 BodyShadowId;
	int32 RotorShadowId;
	int32 Seats;
	float TailX;
	float TailY;
	float TailZ;
	bool bNoTailRotor;
	bool bApache;
	const TCHAR* EngineLoop;
	int32 CatalogIndex;
};

const FRegistryRow RegistryRows[] = {
	{ 0, TEXT("Jet Ranger"),    0x076, TEXT("JETRANG"),  0x117, TEXT("JETRROTR"), 0x159, 0x160,  4,  0.534f,  8.0f, -25.00f, false, false, TEXT("COPLOOP5.WAV"), 1 },
	{ 1, TEXT("Hughes 500"),    0x116, TEXT("HUGH500"),  0x078, TEXT("H500ROTR"), 0x158, 0x15f,  4,  0.534f,  8.0f, -20.00f, false, false, TEXT("COPLOOP6.WAV"), 2 },
	{ 2, TEXT("Apache"),        0x119, TEXT("APACHE"),   0x11a, TEXT("APACROTR"), 0x15b, 0x162,  0,  2.400f, 11.5f, -26.70f, false, true,  TEXT("COPLOOP.WAV"),  INDEX_NONE },
	{ 3, TEXT("Bell 212"),      0x124, TEXT("BELL212"),  0x126, TEXT("BELLROTR"), 0x156, 0x15d, 14, -0.400f, 11.0f, -29.00f, false, false, TEXT("COPLOOP3.WAV"), 4 },
	{ 4, TEXT("Schwiezer 300"), 0x125, TEXT("SCWZR300"), 0x127, TEXT("SCWZROTR"), 0x15a, 0x161,  2, -0.400f,  6.1f, -18.20f, false, false, TEXT("COPLOOP4.WAV"), 0 },
	{ 5, TEXT("Agusta"),        0x141, TEXT("AGUSTA"),   0x142, TEXT("AGUSROTR"), 0x155, 0x15c,  7, -0.400f, 10.0f, -23.00f, false, false, TEXT("COPLOOP6.WAV"), 5 },
	{ 6, TEXT("Dauphin"),       0x153, TEXT("DAUPHIN"),  0x154, TEXT("DAUPROTR"), 0x157, 0x15e, 13,  0.000f,  5.5f, -22.11f, false, false, TEXT("COPLOOP.WAV"),  6 },
	{ 7, TEXT("MDEXPLORER"),    0x170, TEXT("MDEXPLRR"), 0x172, TEXT("MDEXROTR"), 0x174, 0x176,  7,  0.000f,  5.5f, -22.11f, true,  false, TEXT("COPLOOP2.WAV"), 7 },
	{ 8, TEXT("MD520"),         0x171, TEXT("MD520"),    0x173, TEXT("MD52ROTR"), 0x175, 0x177,  4,  0.000f,  5.5f, -22.11f, true,  false, TEXT("COPLOOP2.WAV"), 3 },
};

// SCHOOK: EquipmentCatalog 0x0042d840 / 0x0048b0f0
// Catalog row -> equipment index is {0,1,3,4,2} (FUN_0042d840); this table is in
// equipment-index order and carries the price table from FUN_0048b0f0.
const FSimCopterEquipmentDefinition EquipmentRows[] = {
	{ ESimCopterHelicopterTool::WaterBucket,   0x01, 0,  500, TEXT("Water Bucket"),        0x2a8 },
	{ ESimCopterHelicopterTool::Megaphone,     0x02, 1,  500, TEXT("Megaphone"),           0x2aa },
	{ ESimCopterHelicopterTool::RescueHarness, 0x04, 2,  800, TEXT("Rescue Harness"),      0x2ab },
	{ ESimCopterHelicopterTool::TearGas,       0x08, 3, 2500, TEXT("Tear Gas Launcher"),   0x2ac },
	{ ESimCopterHelicopterTool::WaterCannon,   0x10, 4, 1500, TEXT("Water Cannon"),        0x2a9 },
};

const TCHAR* const MegaphoneMessageNames[] = {
	TEXT("Report Traffic"),
	TEXT("Stop Criminal"),
	TEXT("Evacuate"),
	TEXT("Disperse"),
	TEXT("Greet"),
};

const TCHAR* const ToolNames[] = {
	TEXT("Water Bucket"),
	TEXT("Water Cannon"),
	TEXT("Megaphone"),
	TEXT("Rescue Harness"),
	TEXT("Tear Gas"),
	TEXT("Apache Missile"),
	TEXT("Apache Machine Gun"),
};

static_assert(UE_ARRAY_COUNT(ToolNames) == static_cast<int32>(ESimCopterHelicopterTool::Count),
	"Tool name table must cover every ESimCopterHelicopterTool entry.");
static_assert(UE_ARRAY_COUNT(MegaphoneMessageNames) == static_cast<int32>(ESimCopterMegaphoneMessage::Count),
	"Megaphone message names must cover every ESimCopterMegaphoneMessage entry.");
}

namespace SimCopterHelicopterRegistry
{
const TArray<FSimCopterHelicopterDefinition>& GetDefinitions()
{
	static const TArray<FSimCopterHelicopterDefinition> Definitions = []()
	{
		TArray<FSimCopterHelicopterDefinition> Result;
		Result.Reserve(UE_ARRAY_COUNT(RegistryRows));
		for (const FRegistryRow& Row : RegistryRows)
		{
			FSimCopterHelicopterDefinition Definition;
			Definition.InternalTypeIndex = Row.TypeIndex;
			Definition.DisplayName = Row.DisplayName;
			Definition.TweakSection = Row.DisplayName;
			Definition.BodyObjectId = Row.BodyId;
			Definition.BodyObjectName = Row.BodyName;
			Definition.MainRotorObjectId = Row.RotorId;
			Definition.MainRotorObjectName = Row.RotorName;
			Definition.BodyShadowObjectId = Row.BodyShadowId;
			Definition.RotorShadowObjectId = Row.RotorShadowId;
			Definition.PassengerSeats = Row.Seats;
			Definition.TailRotorOffsetUnits = FVector(Row.TailX, Row.TailY, Row.TailZ);
			Definition.bNoTailRotor = Row.bNoTailRotor;
			Definition.bApacheArmament = Row.bApache;
			Definition.EngineLoopSound = Row.EngineLoop;
			Definition.CatalogIndex = Row.CatalogIndex;
			Result.Add(MoveTemp(Definition));
		}
		return Result;
	}();

	return Definitions;
}

const FSimCopterHelicopterDefinition* FindByTypeIndex(int32 TypeIndex)
{
	const TArray<FSimCopterHelicopterDefinition>& Definitions = GetDefinitions();
	return Definitions.IsValidIndex(TypeIndex) ? &Definitions[TypeIndex] : nullptr;
}

const FSimCopterHelicopterDefinition* FindByDisplayName(const FString& Name)
{
	const FString Trimmed = Name.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return nullptr;
	}

	for (const FSimCopterHelicopterDefinition& Definition : GetDefinitions())
	{
		if (Trimmed.Equals(Definition.DisplayName, ESearchCase::IgnoreCase) ||
			Trimmed.Equals(Definition.TweakSection, ESearchCase::IgnoreCase))
		{
			return &Definition;
		}
	}

	// Alias check: heli.twk uses "Schwiezer 300" (original typo) while standard UI uses "Schweizer 300".
	if (Trimmed.Equals(TEXT("Schweizer 300"), ESearchCase::IgnoreCase) ||
		Trimmed.Equals(TEXT("Schwiezer 300"), ESearchCase::IgnoreCase))
	{
		return FindByTypeIndex(4);
	}

	return nullptr;
}

int32 GetDefinitionCount()
{
	return GetDefinitions().Num();
}

const TArray<FSimCopterEquipmentDefinition>& GetEquipment()
{
	static const TArray<FSimCopterEquipmentDefinition> Equipment(EquipmentRows, UE_ARRAY_COUNT(EquipmentRows));
	return Equipment;
}

const FSimCopterEquipmentDefinition* FindEquipment(ESimCopterHelicopterTool Tool)
{
	for (const FSimCopterEquipmentDefinition& Definition : GetEquipment())
	{
		if (Definition.Tool == Tool)
		{
			return &Definition;
		}
	}

	return nullptr;
}

int32 GetToolCareerBit(ESimCopterHelicopterTool Tool)
{
	const FSimCopterEquipmentDefinition* Definition = FindEquipment(Tool);
	return Definition != nullptr ? Definition->CareerBit : 0;
}

const TCHAR* GetToolDisplayName(ESimCopterHelicopterTool Tool)
{
	const int32 Index = static_cast<int32>(Tool);
	return ToolNames[FMath::Clamp(Index, 0, static_cast<int32>(ESimCopterHelicopterTool::Count) - 1)];
}

const TCHAR* GetMegaphoneMessageName(ESimCopterMegaphoneMessage Message)
{
	const int32 Index = static_cast<int32>(Message);
	return MegaphoneMessageNames[FMath::Clamp(Index, 0, static_cast<int32>(ESimCopterMegaphoneMessage::Count) - 1)];
}

int32 GetEquipmentPrice(ESimCopterHelicopterTool Tool)
{
	const FSimCopterEquipmentDefinition* Definition = FindEquipment(Tool);
	return Definition != nullptr ? Definition->PriceDollars : 0;
}

int32 GetEquipmentSellValue(ESimCopterHelicopterTool Tool)
{
	// FUN_0048b150: (price * 0x4b) / 100.
	return (GetEquipmentPrice(Tool) * 0x4b) / 100;
}
}
