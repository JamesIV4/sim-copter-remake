// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimCopterHelicopterRegistry.generated.h"

// Canonical helicopter/equipment tables decoded from SimCopter.exe.
//
// Evidence: Docs/scratchpad/ghidra/heli_tools_models_decode_20260724.md
//   FUN_00483c20  render hierarchy construction (per-type body/rotor/shadow object ids,
//                 the shared BUCKET/HARNESS/SPOTLITE/BRACKET/CANNON/ROTORTL objects)
//   DAT_005040e4  per-type static block, 0x5c bytes: seats, max load, the 14 tweak-bound
//                 controls, tail-rotor mount, NOTAR flag, costs, engine loop sound
//   FUN_0042d840  equipment/helicopter purchase: catalog-row -> index permutations,
//                 career+0x48 equipment mask, career+0x54 tear gas rounds
//   FUN_0048b0f0  equipment prices; FUN_0048b150 sell value (75%)
//   FUN_00485f50  per-action capability gates against the DAT_00504060 snapshot
//
// The executable's runtime type order is NOT the order of sections in heli.twk and is
// NOT the shop order. Never infer a runtime index from either.

// Every tool the player can operate. The first five are career purchases; the last two
// are Apache-only action overrides and never appear in the career equipment mask.
UENUM(BlueprintType)
enum class ESimCopterHelicopterTool : uint8
{
	WaterBucket,
	WaterCannon,
	Megaphone,
	RescueHarness,
	TearGas,
	ApacheMissile,
	ApacheMachineGun,
	Count UMETA(Hidden)
};

// Why the selected tool is (or is not) usable right now. Surfaced verbatim by the debug panel.
UENUM(BlueprintType)
enum class ESimCopterToolAvailability : uint8
{
	// Owned through the career equipment mask (career + 0x48).
	Career,
	// Granted for this session only by the debug panel; never written to the career record.
	DebugGrant,
	// Supplied by the active helicopter model (Apache missile / machine gun).
	Model,
	// Not owned, not granted, and not supplied by the model.
	Unavailable
};

// The five megaphone messages (original F6..F10 -> global command ids 0x26..0x2a,
// FUN_0044ac80 passes cmd - 0x26 to FUN_00424620 and FUN_0048a800).
UENUM(BlueprintType)
enum class ESimCopterMegaphoneMessage : uint8
{
	ReportTraffic,
	StopCriminal,
	Evacuate,
	Disperse,
	Greet,
	Count UMETA(Hidden)
};

// Shared GEO object ids bound by FUN_00483c20 regardless of helicopter type.
namespace SimCopterHelicopterObjects
{
constexpr int32 TailRotor = 0x083;  // ROTORTL
constexpr int32 Bucket = 0x07b;     // BUCKET
constexpr int32 Spotlight = 0x118;  // SPOTLITE
constexpr int32 Bracket = 0x16c;    // BRACKET
constexpr int32 Harness = 0x16d;    // HARNESS
constexpr int32 Cannon = 0x16e;     // CANNON
constexpr int32 Missile = 0x0ae;    // Apache missile projectile (FUN_0048db20)
constexpr int32 TearGasCanister = 0x147; // tear gas canister projectile (FUN_0048db20)
}

// One executable helicopter record. Purely static data; tuning still comes from heli.twk.
USTRUCT(BlueprintType)
struct SIMCOPTERREMAKE_API FSimCopterHelicopterDefinition
{
	GENERATED_BODY()

	// Index into the executable's type table (the value stored in heli[0]).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Model")
	int32 InternalTypeIndex = 0;

	// Display name; identical to the heli.twk section name for every type.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Model")
	FString DisplayName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Model")
	FString TweakSection;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Model")
	int32 BodyObjectId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Model")
	FString BodyObjectName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Model")
	int32 MainRotorObjectId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Model")
	FString MainRotorObjectName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Model")
	int32 BodyShadowObjectId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Model")
	int32 RotorShadowObjectId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Model")
	int32 PassengerSeats = 0;

	// DAT_005040e4 + 0x2c/0x30/0x34, in original world units on the model's local Maxis
	// axes (X right, Y up, Z forward). Convert with ToTailRotorOffsetCm().
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Model")
	FVector TailRotorOffsetUnits = FVector::ZeroVector;

	// DAT_005040e4 + 0x38: NOTAR types hide the tail rotor entirely (FUN_00487740).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Model")
	bool bNoTailRotor = false;

	// Runtime type 2 replaces actions 2 / 0x10 with the missile and machine gun.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Model")
	bool bApacheArmament = false;

	// DAT_005040e4 + 0x54: per-type engine loop sample.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Model")
	FString EngineLoopSound;

	// Row in the shop catalog (FUN_0042d840's {4,0,1,8,3,5,6,7} permutation), or
	// INDEX_NONE for the Apache, which is never sold.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Model")
	int32 CatalogIndex = INDEX_NONE;

	bool IsValid() const { return !DisplayName.IsEmpty(); }

	// Maxis local (X,Y,Z) -> Unreal (Z,X,Y) with the same unit scale the mesh builder uses.
	FVector ToTailRotorOffsetCm(float OriginalUnitToCm) const
	{
		return FVector(
			TailRotorOffsetUnits.Z * OriginalUnitToCm,
			TailRotorOffsetUnits.X * OriginalUnitToCm,
			TailRotorOffsetUnits.Y * OriginalUnitToCm);
	}
};

// One purchasable equipment row.
struct SIMCOPTERREMAKE_API FSimCopterEquipmentDefinition
{
	ESimCopterHelicopterTool Tool = ESimCopterHelicopterTool::WaterBucket;
	// Bit in career + 0x48 and in the per-frame snapshot DAT_00504060.
	int32 CareerBit = 0;
	// Bit index used by the purchase/sale tables (log2 of CareerBit).
	int32 EquipmentIndex = 0;
	int32 PriceDollars = 0;
	const TCHAR* DisplayName = nullptr;
	// Message id shown when the tool is used without owning it.
	int32 MissingMessageId = 0;
};

namespace SimCopterHelicopterRegistry
{
// All nine executable records, indexed by InternalTypeIndex.
SIMCOPTERREMAKE_API const TArray<FSimCopterHelicopterDefinition>& GetDefinitions();

// Null when TypeIndex is outside 0..8.
SIMCOPTERREMAKE_API const FSimCopterHelicopterDefinition* FindByTypeIndex(int32 TypeIndex);

// Case-insensitive match against DisplayName / TweakSection.
SIMCOPTERREMAKE_API const FSimCopterHelicopterDefinition* FindByDisplayName(const FString& Name);

SIMCOPTERREMAKE_API int32 GetDefinitionCount();

// The five purchasable tools, in equipment-index order (bucket, megaphone, harness, gas, cannon).
SIMCOPTERREMAKE_API const TArray<FSimCopterEquipmentDefinition>& GetEquipment();

SIMCOPTERREMAKE_API const FSimCopterEquipmentDefinition* FindEquipment(ESimCopterHelicopterTool Tool);

// Career bit for a purchasable tool, 0 for the Apache-only tools.
SIMCOPTERREMAKE_API int32 GetToolCareerBit(ESimCopterHelicopterTool Tool);

SIMCOPTERREMAKE_API const TCHAR* GetToolDisplayName(ESimCopterHelicopterTool Tool);

SIMCOPTERREMAKE_API const TCHAR* GetMegaphoneMessageName(ESimCopterMegaphoneMessage Message);

// FUN_0048b0f0 / FUN_0048b150: purchase price and the 75% trade-in value.
SIMCOPTERREMAKE_API int32 GetEquipmentPrice(ESimCopterHelicopterTool Tool);
SIMCOPTERREMAKE_API int32 GetEquipmentSellValue(ESimCopterHelicopterTool Tool);

// Career mask of every purchasable tool (0x1f).
constexpr int32 AllCareerEquipmentBits = 0x1f;

// FUN_0042d840: buying the tear gas launcher writes ten rounds; FUN_0048b130 refills at
// one round per $50 and FUN_00444750 offers maintenance below five rounds.
constexpr int32 TearGasCapacity = 10;
constexpr int32 TearGasDollarsPerRound = 50;
constexpr int32 TearGasMaintenanceThreshold = 5;
}

// Emitter constants decoded from FUN_0048e0b0 / FUN_0048ed00 / FUN_0048da50.
namespace SimCopterToolTiming
{
// DAT_00504570, shared by the Apache missile (type 1) and tear gas (type 3): 0x10000 = 1.0 s.
constexpr float ProjectileCooldownSeconds = 1.0f;

// Slot life, 0x50000 = 5.0 s for the missile, machine gun, and the tear gas canister phase.
constexpr float ProjectileLifeSeconds = 5.0f;

// After its fuse the canister becomes a gas cloud for 0x1e0000 = 30.0 s.
constexpr float TearGasCloudSeconds = 30.0f;

// Cloud puff cadence 0x4ccc = 0.3 s; canister smoke trail 0x8000 = 0.5 s.
constexpr float TearGasCloudPuffSeconds = 0.3f;
constexpr float ProjectileTrailSeconds = 0.5f;

// Cloud puffs land within +/-20 original units of the canister in X and Z.
constexpr float TearGasCloudSpreadUnits = 20.0f;

// Missile 0x1c20000 = 450.0 units/s, machine gun 0x2580000 = 600.0 units/s.
constexpr float MissileSpeedUnitsPerSecond = 450.0f;
constexpr float MachineGunSpeedUnitsPerSecond = 600.0f;
}

// The three explicit equipment layers from the plan. Career state is what a save would
// hold; the debug layers are session-only and must never be serialized.
USTRUCT(BlueprintType)
struct SIMCOPTERREMAKE_API FSimCopterEquipmentState
{
	GENERATED_BODY()

	// career + 0x48. Until the career/shop layer lands this is seeded from defaults.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Equipment")
	int32 CareerEquipmentMask = 0;

	// Transient overlay owned by the debug panel.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Equipment")
	int32 DebugGrantedEquipmentMask = 0;

	// career + 0x54.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Equipment")
	int32 CareerTearGasRounds = 0;

	// Transient rounds added by the debug panel; consumed before career rounds so a debug
	// refill can never inflate the career record.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Equipment")
	int32 DebugTearGasRounds = 0;

	// FUN_00484d20 snapshots career + 0x48 into DAT_00504060 once per frame and every
	// in-flight capability test reads the snapshot. The remake folds the debug grants in
	// here so both paths share one gate.
	int32 GetEffectiveEquipmentMask() const
	{
		return (CareerEquipmentMask | DebugGrantedEquipmentMask) &
			SimCopterHelicopterRegistry::AllCareerEquipmentBits;
	}

	int32 GetTearGasRounds() const { return CareerTearGasRounds + DebugTearGasRounds; }

	bool HasCareerBit(int32 Bit) const { return Bit != 0 && (CareerEquipmentMask & Bit) != 0; }
	bool HasDebugBit(int32 Bit) const { return Bit != 0 && (DebugGrantedEquipmentMask & Bit) != 0; }

	// FUN_0048e0b0 type 3: one round per shot, clamped at zero.
	bool ConsumeTearGasRound()
	{
		if (DebugTearGasRounds > 0)
		{
			--DebugTearGasRounds;
			return true;
		}
		if (CareerTearGasRounds > 0)
		{
			--CareerTearGasRounds;
			return true;
		}
		return false;
	}

	// Debug refill: tops the transient pool up to capacity without touching career rounds.
	void DebugRefillTearGas()
	{
		const int32 Missing = SimCopterHelicopterRegistry::TearGasCapacity - GetTearGasRounds();
		if (Missing > 0)
		{
			DebugTearGasRounds += Missing;
		}
	}

	void ClearDebugOverlay()
	{
		DebugGrantedEquipmentMask = 0;
		DebugTearGasRounds = 0;
	}
};
