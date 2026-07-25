// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterHelicopterRegistry.h"
#include "Misc/AutomationTest.h"

// Regression gates for the decoded helicopter/equipment tables. Every expected value here
// comes from Docs/scratchpad/ghidra/heli_tools_models_decode_20260724.md, so a typo in the
// registry fails the test instead of quietly flying the wrong airframe.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterHelicopterRegistryShapeTest,
	"SimCopter.Model.RegistryShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterHelicopterRegistryShapeTest::RunTest(const FString& Parameters)
{
	const TArray<FSimCopterHelicopterDefinition>& Definitions =
		SimCopterHelicopterRegistry::GetDefinitions();

	TestEqual(TEXT("nine executable helicopter records"), Definitions.Num(), 9);

	TSet<int32> SeenTypeIndices;
	TSet<int32> SeenBodyIds;
	TSet<int32> SeenRotorIds;
	TSet<int32> SeenBodyShadowIds;
	TSet<int32> SeenRotorShadowIds;
	TSet<FString> SeenNames;
	TSet<int32> SeenCatalogIndices;

	for (int32 Index = 0; Index < Definitions.Num(); ++Index)
	{
		const FSimCopterHelicopterDefinition& Definition = Definitions[Index];

		TestEqual(
			FString::Printf(TEXT("entry %d is stored at its runtime index"), Index),
			Definition.InternalTypeIndex,
			Index);
		TestFalse(TEXT("display name is set"), Definition.DisplayName.IsEmpty());
		TestEqual(
			TEXT("tweak section matches the display name"),
			Definition.TweakSection,
			Definition.DisplayName);
		TestFalse(TEXT("body object name is set"), Definition.BodyObjectName.IsEmpty());
		TestFalse(TEXT("main rotor object name is set"), Definition.MainRotorObjectName.IsEmpty());
		TestFalse(TEXT("engine loop sound is set"), Definition.EngineLoopSound.IsEmpty());

		bool bAlreadyPresent = false;
		SeenTypeIndices.Add(Definition.InternalTypeIndex, &bAlreadyPresent);
		TestFalse(TEXT("runtime type indices are unique"), bAlreadyPresent);
		SeenNames.Add(Definition.DisplayName, &bAlreadyPresent);
		TestFalse(TEXT("display names are unique"), bAlreadyPresent);
		SeenBodyIds.Add(Definition.BodyObjectId, &bAlreadyPresent);
		TestFalse(TEXT("body object ids are unique"), bAlreadyPresent);
		SeenRotorIds.Add(Definition.MainRotorObjectId, &bAlreadyPresent);
		TestFalse(TEXT("main rotor object ids are unique"), bAlreadyPresent);
		SeenBodyShadowIds.Add(Definition.BodyShadowObjectId, &bAlreadyPresent);
		TestFalse(TEXT("body shadow object ids are unique"), bAlreadyPresent);
		SeenRotorShadowIds.Add(Definition.RotorShadowObjectId, &bAlreadyPresent);
		TestFalse(TEXT("rotor shadow object ids are unique"), bAlreadyPresent);

		if (Definition.CatalogIndex != INDEX_NONE)
		{
			SeenCatalogIndices.Add(Definition.CatalogIndex, &bAlreadyPresent);
			TestFalse(TEXT("catalog rows are unique"), bAlreadyPresent);
		}

		// Round-tripping the name is what BeginPlay does with the map's seed property.
		const FSimCopterHelicopterDefinition* ByName =
			SimCopterHelicopterRegistry::FindByDisplayName(Definition.DisplayName);
		TestNotNull(TEXT("display-name lookup resolves"), ByName);
		if (ByName != nullptr)
		{
			TestEqual(TEXT("name lookup returns the same type"), ByName->InternalTypeIndex, Index);
		}
	}

	// FUN_0042d840's helicopter permutation {4,0,1,8,3,5,6,7}: eight civilian rows, Apache
	// deliberately absent from the shop.
	TestEqual(TEXT("eight catalog rows"), SeenCatalogIndices.Num(), 8);
	TestNull(TEXT("out-of-range type index has no entry"), SimCopterHelicopterRegistry::FindByTypeIndex(9));
	TestNull(TEXT("negative type index has no entry"), SimCopterHelicopterRegistry::FindByTypeIndex(-1));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterHelicopterRegistryValuesTest,
	"SimCopter.Model.RegistryValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterHelicopterRegistryValuesTest::RunTest(const FString& Parameters)
{
	// FUN_00483c20's per-type object ids and DAT_005040e4's seats / NOTAR flag.
	struct FExpected
	{
		int32 TypeIndex;
		const TCHAR* Name;
		int32 BodyId;
		int32 RotorId;
		int32 BodyShadowId;
		int32 RotorShadowId;
		int32 Seats;
		bool bNoTailRotor;
		bool bApache;
	};

	static const FExpected Expected[] = {
		{ 0, TEXT("Jet Ranger"),    0x076, 0x117, 0x159, 0x160,  4, false, false },
		{ 1, TEXT("Hughes 500"),    0x116, 0x078, 0x158, 0x15f,  4, false, false },
		{ 2, TEXT("Apache"),        0x119, 0x11a, 0x15b, 0x162,  0, false, true  },
		{ 3, TEXT("Bell 212"),      0x124, 0x126, 0x156, 0x15d, 14, false, false },
		{ 4, TEXT("Schwiezer 300"), 0x125, 0x127, 0x15a, 0x161,  2, false, false },
		{ 5, TEXT("Agusta"),        0x141, 0x142, 0x155, 0x15c,  7, false, false },
		{ 6, TEXT("Dauphin"),       0x153, 0x154, 0x157, 0x15e, 13, false, false },
		{ 7, TEXT("MDEXPLORER"),    0x170, 0x172, 0x174, 0x176,  7, true,  false },
		{ 8, TEXT("MD520"),         0x171, 0x173, 0x175, 0x177,  4, true,  false },
	};

	for (const FExpected& Row : Expected)
	{
		const FSimCopterHelicopterDefinition* Definition =
			SimCopterHelicopterRegistry::FindByTypeIndex(Row.TypeIndex);
		if (!TestNotNull(FString::Printf(TEXT("type %d exists"), Row.TypeIndex), Definition))
		{
			continue;
		}

		const FString Prefix = FString::Printf(TEXT("type %d (%s)"), Row.TypeIndex, Row.Name);
		TestEqual(Prefix + TEXT(" name"), Definition->DisplayName, FString(Row.Name));
		TestEqual(Prefix + TEXT(" body id"), Definition->BodyObjectId, Row.BodyId);
		TestEqual(Prefix + TEXT(" rotor id"), Definition->MainRotorObjectId, Row.RotorId);
		TestEqual(Prefix + TEXT(" body shadow id"), Definition->BodyShadowObjectId, Row.BodyShadowId);
		TestEqual(Prefix + TEXT(" rotor shadow id"), Definition->RotorShadowObjectId, Row.RotorShadowId);
		TestEqual(Prefix + TEXT(" seats"), Definition->PassengerSeats, Row.Seats);
		TestEqual(Prefix + TEXT(" NOTAR"), Definition->bNoTailRotor, Row.bNoTailRotor);
		TestEqual(Prefix + TEXT(" Apache armament"), Definition->bApacheArmament, Row.bApache);
	}

	// Maxis local (X,Y,Z) -> Unreal (Z,X,Y) at 6.25 cm per original unit.
	const FSimCopterHelicopterDefinition* JetRanger = SimCopterHelicopterRegistry::FindByTypeIndex(0);
	if (JetRanger != nullptr)
	{
		const FVector OffsetCm = JetRanger->ToTailRotorOffsetCm(6.25f);
		TestTrue(TEXT("Jet Ranger tail sits behind the body"), OffsetCm.X < -150.0f && OffsetCm.X > -160.0f);
		TestTrue(TEXT("Jet Ranger tail sits above the body origin"), FMath::IsNearlyEqual(OffsetCm.Z, 50.0f, 0.01f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterEquipmentTableTest,
	"SimCopter.Tools.EquipmentTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterEquipmentTableTest::RunTest(const FString& Parameters)
{
	// FUN_0042d840 bit assignment + FUN_0048b0f0 prices + FUN_00485f50 refusal messages.
	struct FExpected
	{
		ESimCopterHelicopterTool Tool;
		int32 Bit;
		int32 Price;
		int32 SellValue;
		int32 MissingMessage;
	};

	static const FExpected Expected[] = {
		{ ESimCopterHelicopterTool::WaterBucket,   0x01,  500,  375, 0x2a8 },
		{ ESimCopterHelicopterTool::Megaphone,     0x02,  500,  375, 0x2aa },
		{ ESimCopterHelicopterTool::RescueHarness, 0x04,  800,  600, 0x2ab },
		{ ESimCopterHelicopterTool::TearGas,       0x08, 2500, 1875, 0x2ac },
		{ ESimCopterHelicopterTool::WaterCannon,   0x10, 1500, 1125, 0x2a9 },
	};

	TestEqual(TEXT("five purchasable tools"), SimCopterHelicopterRegistry::GetEquipment().Num(), 5);

	int32 UnionMask = 0;
	for (const FExpected& Row : Expected)
	{
		const FSimCopterEquipmentDefinition* Definition = SimCopterHelicopterRegistry::FindEquipment(Row.Tool);
		const FString Prefix = SimCopterHelicopterRegistry::GetToolDisplayName(Row.Tool);
		if (!TestNotNull(Prefix + TEXT(" is in the table"), Definition))
		{
			continue;
		}

		TestEqual(Prefix + TEXT(" career bit"), Definition->CareerBit, Row.Bit);
		TestEqual(Prefix + TEXT(" bit index"), 1 << Definition->EquipmentIndex, Row.Bit);
		TestEqual(Prefix + TEXT(" price"), SimCopterHelicopterRegistry::GetEquipmentPrice(Row.Tool), Row.Price);
		TestEqual(Prefix + TEXT(" sell value"), SimCopterHelicopterRegistry::GetEquipmentSellValue(Row.Tool), Row.SellValue);
		TestEqual(Prefix + TEXT(" missing message"), Definition->MissingMessageId, Row.MissingMessage);
		UnionMask |= Definition->CareerBit;
	}

	TestEqual(TEXT("career equipment mask covers 0x1f"), UnionMask, SimCopterHelicopterRegistry::AllCareerEquipmentBits);

	// Apache weapons are model capabilities, never career equipment.
	TestEqual(
		TEXT("Apache missile has no career bit"),
		SimCopterHelicopterRegistry::GetToolCareerBit(ESimCopterHelicopterTool::ApacheMissile),
		0);
	TestEqual(
		TEXT("Apache machine gun has no career bit"),
		SimCopterHelicopterRegistry::GetToolCareerBit(ESimCopterHelicopterTool::ApacheMachineGun),
		0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterEquipmentStateTest,
	"SimCopter.Tools.EquipmentStateLayers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterEquipmentStateTest::RunTest(const FString& Parameters)
{
	FSimCopterEquipmentState State;
	State.CareerEquipmentMask = 0x01; // bucket only

	TestEqual(TEXT("effective mask starts at the career mask"), State.GetEffectiveEquipmentMask(), 0x01);
	TestTrue(TEXT("career bucket is owned"), State.HasCareerBit(0x01));
	TestFalse(TEXT("career cannon is not owned"), State.HasCareerBit(0x10));

	// A debug grant is visible to the effective mask but must not touch the career mask.
	State.DebugGrantedEquipmentMask = 0x10;
	TestEqual(TEXT("grant shows in the effective mask"), State.GetEffectiveEquipmentMask(), 0x11);
	TestEqual(TEXT("career mask is untouched by the grant"), State.CareerEquipmentMask, 0x01);
	TestFalse(TEXT("granted tool is not career-owned"), State.HasCareerBit(0x10));
	TestTrue(TEXT("granted tool reports as a debug grant"), State.HasDebugBit(0x10));

	// Bits outside 0x1f can never leak in from an authoring mistake.
	State.CareerEquipmentMask = 0x7fffffff;
	TestEqual(
		TEXT("effective mask is masked to the five real bits"),
		State.GetEffectiveEquipmentMask(),
		SimCopterHelicopterRegistry::AllCareerEquipmentBits);

	// Ammo: debug rounds drain first so a debug refill can never inflate career rounds.
	FSimCopterEquipmentState Ammo;
	Ammo.CareerTearGasRounds = 2;
	Ammo.DebugRefillTearGas();
	TestEqual(TEXT("refill tops up to capacity"), Ammo.GetTearGasRounds(), SimCopterHelicopterRegistry::TearGasCapacity);
	TestEqual(TEXT("career rounds unchanged by refill"), Ammo.CareerTearGasRounds, 2);
	TestEqual(TEXT("the rest came from the debug pool"), Ammo.DebugTearGasRounds, 8);

	for (int32 Shot = 0; Shot < 8; ++Shot)
	{
		TestTrue(TEXT("debug rounds fire"), Ammo.ConsumeTearGasRound());
	}
	TestEqual(TEXT("debug pool drained first"), Ammo.DebugTearGasRounds, 0);
	TestEqual(TEXT("career rounds still intact"), Ammo.CareerTearGasRounds, 2);

	TestTrue(TEXT("career rounds fire next"), Ammo.ConsumeTearGasRound());
	TestTrue(TEXT("career rounds fire next"), Ammo.ConsumeTearGasRound());
	TestFalse(TEXT("cannot fire at zero ammo"), Ammo.ConsumeTearGasRound());
	TestEqual(TEXT("ammo clamps at zero"), Ammo.GetTearGasRounds(), 0);

	// Clearing the overlay is what a career load/save boundary would do.
	Ammo.DebugRefillTearGas();
	Ammo.DebugGrantedEquipmentMask = 0x1f;
	Ammo.ClearDebugOverlay();
	TestEqual(TEXT("overlay grants cleared"), Ammo.DebugGrantedEquipmentMask, 0);
	TestEqual(TEXT("overlay ammo cleared"), Ammo.DebugTearGasRounds, 0);

	return true;
}
