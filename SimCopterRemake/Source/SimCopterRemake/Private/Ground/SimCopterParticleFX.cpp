// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterParticleFX.h"

#include "Audio/SimCopterAudioSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "City/SimCity2000CityActor.h"
#include "City/SimCopterEffectExposure.h"
#include "Replay/SimCopterReplayTypes.h"
#include "Engine/World.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Flight/SimCopterWaterGameplay.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisMeshReader.h"
#include "GameFramework/PlayerController.h"
#include "Ground/SimCopterEffectFX.h"
#include "Ground/SimCopterEffectRasterizer.h"
#include "Ground/SimCopterInteraction.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float Fixed1616Scale = 65536.0f;
	constexpr float OriginalPointStepCm = 10.0f * SimCopterEffectFX::OriginalUnitToCm;
	constexpr uint8 SplashFrames[] = { 0x0F, 0x10, 0x11, 0x12, 0x19, 0x1A, 0x1B, 0x1C, 0x1D };
	constexpr uint8 FireTipFrames[] = { 0x73, 0x7B, 0x70, 0xEA };
	constexpr uint32 ParticleRuntimeSaveMagic = 0x50465853; // 'PFXS'
	constexpr int32 ParticleRuntimeSaveVersion = 1;

	void SerializeParticleBool(FArchive& Archive, bool& Value)
	{
		uint8 Byte = Value ? 1 : 0;
		Archive << Byte;
		if (Archive.IsLoading()) Value = Byte != 0;
	}

	int32 SecondsToFixed(float Seconds)
	{
		return FMath::RoundToInt(Seconds * Fixed1616Scale);
	}

	ESimCopterEffectPool PoolForType(ESimCopterEffectType Type)
	{
		switch (Type)
		{
		case ESimCopterEffectType::Smoke: return ESimCopterEffectPool::Smoke10;
		case ESimCopterEffectType::SmallSmoke: return ESimCopterEffectPool::Geo10;
		case ESimCopterEffectType::GeoSmoke: return ESimCopterEffectPool::Geo2;
		case ESimCopterEffectType::Debris:
		case ESimCopterEffectType::HeavyDebris: return ESimCopterEffectPool::Debris30;
		case ESimCopterEffectType::RotorWash: return ESimCopterEffectPool::Wash20;
		case ESimCopterEffectType::BuildingFireSmoke:
		case ESimCopterEffectType::BuildingFireEmber: return ESimCopterEffectPool::Fire25;
		default: return ESimCopterEffectPool::Trajectory70;
		}
	}

	uint8 InitialSplatClass(ESimCopterEffectType Type, int32 MotionScale1616)
	{
		switch (Type)
		{
		case ESimCopterEffectType::Smoke:
		case ESimCopterEffectType::GeoSmoke: return 1;
		case ESimCopterEffectType::FireTrajectory:
		case ESimCopterEffectType::FireTrajectoryAlt:
		case ESimCopterEffectType::SmallSmoke:
		case ESimCopterEffectType::SplashSubParticle: return 4;
		case ESimCopterEffectType::Spray:
		case ESimCopterEffectType::BucketDrip:
		case ESimCopterEffectType::BuildingFireEmber: return 8;
		case ESimCopterEffectType::SmallSpray: return 3;
		case ESimCopterEffectType::BuildingFireSmoke: return 9;
		case ESimCopterEffectType::Debris:
		case ESimCopterEffectType::HeavyDebris: return MotionScale1616 < 0x30000 ? 4 : 1;
		default: return 1;
		}
	}

	const FVector SplashDirections[] = {
		FVector(1, 0, 0), FVector(-1, 0, 0),
		FVector(0, 1, 0), FVector(0, -1, 0),
		FVector(0, 0, 1), FVector(0, 0, -1),
		FVector(1, 1, 1).GetSafeNormal(), FVector(-1, -1, -1).GetSafeNormal(),
		FVector(1, 1, -1).GetSafeNormal(), FVector(-1, -1, 1).GetSafeNormal(),
		FVector(1, -1, 1).GetSafeNormal(), FVector(-1, 1, -1).GetSafeNormal(),
		FVector(1, -1, -1).GetSafeNormal(), FVector(-1, 1, 1).GetSafeNormal(),
	};
	static_assert(UE_ARRAY_COUNT(SplashDirections) == 14);
}

USimCopterParticleFXComponent::USimCopterParticleFXComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	Slots.SetNum(GetPoolCapacity(ESimCopterEffectPool::Smoke10) + GetPoolCapacity(ESimCopterEffectPool::Geo10) +
		GetPoolCapacity(ESimCopterEffectPool::Geo2) + GetPoolCapacity(ESimCopterEffectPool::Debris30) +
		GetPoolCapacity(ESimCopterEffectPool::Wash20) + GetPoolCapacity(ESimCopterEffectPool::Trajectory70) +
		GetPoolCapacity(ESimCopterEffectPool::Fire25) + GetPoolCapacity(ESimCopterEffectPool::SplashColumns20) +
		GetPoolCapacity(ESimCopterEffectPool::TilePuffs100));

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FxMaterialFinder(
		TEXT("/Game/Materials/M_SimCopterParticleFX.M_SimCopterParticleFX"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> KernelMaterialFinder(
		TEXT("/Game/Materials/M_SimCopterSpriteTexture.M_SimCopterSpriteTexture"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FallbackFinder(
		TEXT("/Game/Materials/M_SimCopterLitVertexColor.M_SimCopterLitVertexColor"));
	CardMaterial = FxMaterialFinder.Succeeded() ? FxMaterialFinder.Object : FallbackFinder.Object;
	KernelMaterial = KernelMaterialFinder.Object;
}

bool USimCopterParticleFXComponent::CaptureRuntimeSaveState(TArray<uint8>& OutData) const
{
	OutData.Reset();
	FMemoryWriter Writer(OutData, true);
	uint32 Magic = ParticleRuntimeSaveMagic;
	int32 Version = ParticleRuntimeSaveVersion;
	Writer << Magic << Version;
	uint8 SavedFireRampCursor = FireRampCursor;
	uint8 SavedFireTipCursor = FireTipCursor;
	int32 SavedDebrisObjectCursor = DebrisObjectCursor;
	Writer << SavedFireRampCursor << SavedFireTipCursor << SavedDebrisObjectCursor;
	int32 SlotCount = Slots.Num();
	Writer << SlotCount;
	for (const FSimCopterEffectSlot& ConstSlot : Slots)
	{
		FSimCopterEffectSlot Slot = ConstSlot;
		SerializeParticleBool(Writer, Slot.bActive);
		uint8 Type = static_cast<uint8>(Slot.Type);
		uint8 Pool = static_cast<uint8>(Slot.Pool);
		Writer << Type << Pool << Slot.Position << Slot.Velocity << Slot.Direction1616 << Slot.Cell;
		Writer << Slot.Age1616 << Slot.Life1616 << Slot.SpawnTimer1616 << Slot.MotionScale1616;
		Writer << Slot.StepCarry1616 << Slot.SizeCm << Slot.PaletteIndex;
		for (uint8& Palette : Slot.PointPaletteIndices) Writer << Palette;
		Writer << Slot.PointCount << Slot.FaceType << Slot.EffectClass << Slot.FrameCursor;
		Writer << Slot.GeoObjectId;
		SerializeParticleBool(Writer, Slot.bTrajectory);
		SerializeParticleBool(Writer, Slot.bApplyGravity);
		SerializeParticleBool(Writer, Slot.bBurstEmitted);
	}
	if (Writer.IsError()) OutData.Reset();
	return !Writer.IsError();
}

bool USimCopterParticleFXComponent::RestoreRuntimeSaveState(const TArray<uint8>& Data)
{
	if (Data.IsEmpty()) return false;
	FMemoryReader Reader(Data, true);
	uint32 Magic = 0;
	int32 Version = 0;
	Reader << Magic << Version << FireRampCursor << FireTipCursor << DebrisObjectCursor;
	int32 SlotCount = 0;
	Reader << SlotCount;
	if (Magic != ParticleRuntimeSaveMagic || Version != ParticleRuntimeSaveVersion ||
		SlotCount != Slots.Num())
	{
		return false;
	}

	for (FSimCopterEffectSlot& Slot : Slots)
	{
		SerializeParticleBool(Reader, Slot.bActive);
		uint8 Type = 0;
		uint8 Pool = 0;
		Reader << Type << Pool << Slot.Position << Slot.Velocity << Slot.Direction1616 << Slot.Cell;
		Reader << Slot.Age1616 << Slot.Life1616 << Slot.SpawnTimer1616 << Slot.MotionScale1616;
		Reader << Slot.StepCarry1616 << Slot.SizeCm << Slot.PaletteIndex;
		for (uint8& Palette : Slot.PointPaletteIndices) Reader << Palette;
		Reader << Slot.PointCount << Slot.FaceType << Slot.EffectClass << Slot.FrameCursor;
		Reader << Slot.GeoObjectId;
		SerializeParticleBool(Reader, Slot.bTrajectory);
		SerializeParticleBool(Reader, Slot.bApplyGravity);
		SerializeParticleBool(Reader, Slot.bBurstEmitted);
		if (Type < static_cast<uint8>(ESimCopterEffectType::Smoke) ||
			Type > static_cast<uint8>(ESimCopterEffectType::FireTrajectoryAlt) ||
			Pool > static_cast<uint8>(ESimCopterEffectPool::TilePuffs100) ||
			Slot.PointCount > UE_ARRAY_COUNT(Slot.PointPaletteIndices))
		{
			return false;
		}
		Slot.Type = static_cast<ESimCopterEffectType>(Type);
		Slot.Pool = static_cast<ESimCopterEffectPool>(Pool);
	}
	if (Reader.IsError() || Reader.Tell() != Reader.TotalSize()) return false;
	RebuildMesh(GetCameraLocation());
	return true;
}

bool USimCopterParticleFXComponent::InitEffectAssets(const FString& OriginalGameRoot, FString& OutError)
{
	FMaxisMeshLibrary MeshLibrary;
	if (!MeshLibrary.LoadFromOriginalGameRoot(OriginalGameRoot, OutError))
	{
		return false;
	}
	const TArray<FColor>* Palette = MeshLibrary.GetSharedColorMap();
	if (Palette == nullptr || Palette->Num() < 256)
	{
		OutError = TEXT("SIM3D shared palette is missing or incomplete.");
		return false;
	}
	SharedPalette = *Palette;

	SelectorAtlas = FSimCopterEffectRasterizer::CreateSelectorAtlas(this, SharedPalette);
	if (SelectorAtlas == nullptr)
	{
		OutError = TEXT("Could not build the original effect selector atlas.");
		return false;
	}
	if (KernelMaterial != nullptr)
	{
		KernelMaterialInstance = UMaterialInstanceDynamic::Create(KernelMaterial, this);
		if (KernelMaterialInstance != nullptr)
		{
			KernelMaterialInstance->SetTextureParameterValue(TEXT("Texture"), SelectorAtlas);
		}
	}

	// These are the exact GEO resources cloned into the decoded effect pools. Preserve their
	// authored point markers and polygon silhouettes instead of substituting generic cards.
	static constexpr int32 EffectObjectIds[] = {
		0x7c, 0xae, 0x147, 0x148, 0x149, 0x14a, 0x14b
	};
	GeoTemplates.Reset();
	for (const int32 ObjectId : EffectObjectIds)
	{
		const TArray<FColor>* ObjectPalette = nullptr;
		const FMaxisMeshObject* Object = MeshLibrary.FindObjectByObjectId(ObjectId, &ObjectPalette);
		if (Object == nullptr)
		{
			continue;
		}

		FGeoEffectTemplate Template;
		FBox TemplateBounds(ForceInit);
		TArray<FVector> ConvertedVertices;
		ConvertedVertices.Reserve(Object->Vertices.Num());
		for (const FMaxisMeshVertex& Vertex : Object->Vertices)
		{
			const FVector Converted = FMaxisMeshReader::ConvertMaxisVertexToUnreal(Vertex);
			ConvertedVertices.Add(Converted);
			TemplateBounds += Converted;
		}

		for (const FMaxisMeshFace& Face : Object->Faces)
		{
			if (Face.VertexIndices.Num() == 1 &&
				(Face.FaceType == 0x17 || Face.FaceType == 0x1a) &&
				ConvertedVertices.IsValidIndex(Face.VertexIndices[0]))
			{
				FGeoEffectPoint Point;
				Point.LocalOffset = ConvertedVertices[Face.VertexIndices[0]];
				Point.FaceType = Face.FaceType;
				Point.MaterialIndex = Face.MaterialIndex;
				Template.Points.Add(Point);
				continue;
			}
			if (Face.VertexIndices.Num() < 2)
			{
				continue;
			}

			FGeoEffectFace RuntimeFace;
			RuntimeFace.Color =
				ObjectPalette != nullptr && ObjectPalette->IsValidIndex(Face.MaterialIndex)
					? FLinearColor((*ObjectPalette)[Face.MaterialIndex])
					: FLinearColor::White;
			for (const uint16 VertexIndex : Face.VertexIndices)
			{
				if (ConvertedVertices.IsValidIndex(VertexIndex))
				{
					RuntimeFace.Vertices.Add(ConvertedVertices[VertexIndex]);
				}
			}
			if (RuntimeFace.Vertices.Num() >= 2)
			{
				Template.Faces.Add(MoveTemp(RuntimeFace));
			}
		}

		if (TemplateBounds.IsValid)
		{
			const FVector Size = TemplateBounds.GetSize();
			Template.SourceSpanCm = FMath::Max3(
				static_cast<float>(Size.X),
				static_cast<float>(Size.Y),
				static_cast<float>(Size.Z));
			Template.SourceSpanCm = FMath::Max(Template.SourceSpanCm, 1.0f);
		}
		GeoTemplates.Add(ObjectId, MoveTemp(Template));
	}
	return true;
}

void USimCopterParticleFXComponent::OnRegister()
{
	Super::OnRegister();
	if (MeshComponent != nullptr)
	{
		return;
	}
	MeshComponent = NewObject<UProceduralMeshComponent>(this, TEXT("TypedEffectPrimitives"));
	MeshComponent->SetupAttachment(this);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCanEverAffectNavigation(false);
	MeshComponent->bUseAsyncCooking = false;
	MeshComponent->SetCastShadow(false);
	MeshComponent->RegisterComponent();
}

int32 USimCopterParticleFXComponent::GetPoolCapacity(ESimCopterEffectPool Pool)
{
	switch (Pool)
	{
	case ESimCopterEffectPool::Smoke10: return 10;
	case ESimCopterEffectPool::Geo10: return 10;
	case ESimCopterEffectPool::Geo2: return 2;
	case ESimCopterEffectPool::Debris30: return 30;
	case ESimCopterEffectPool::Wash20: return 300;
	case ESimCopterEffectPool::Trajectory70: return 70;
	case ESimCopterEffectPool::Fire25: return 25;
	case ESimCopterEffectPool::SplashColumns20: return 20;
	case ESimCopterEffectPool::TilePuffs100: return 100;
	default: return 0;
	}
}

int32 USimCopterParticleFXComponent::GetPointCountForType(ESimCopterEffectType Type)
{
	switch (Type)
	{
	case ESimCopterEffectType::RotorWash: return 1;
	case ESimCopterEffectType::BuildingFireEmber: return 4;
	case ESimCopterEffectType::FireTrajectory:
	case ESimCopterEffectType::FireTrajectoryAlt:
	case ESimCopterEffectType::Spray:
	case ESimCopterEffectType::BucketDrip:
	case ESimCopterEffectType::SmallSpray:
	case ESimCopterEffectType::SplashSubParticle: return 3;
	default: return 1;
	}
}

int32 USimCopterParticleFXComponent::GetLifetime1616ForType(ESimCopterEffectType Type)
{
	switch (Type)
	{
	case ESimCopterEffectType::SmallSpray: return 0x1CCCC;
	case ESimCopterEffectType::SplashSubParticle: return 0xE666;
	case ESimCopterEffectType::Debris: return 0x1E0000;
	case ESimCopterEffectType::HeavyDebris: return 0x40000;
	case ESimCopterEffectType::RotorWash: return 0x60000;
	case ESimCopterEffectType::BuildingFireEmber: return 0x30000;
	default: return 0x50000;
	}
}

float USimCopterParticleFXComponent::GetLifetimeForType(ESimCopterEffectType Type)
{
	return static_cast<float>(GetLifetime1616ForType(Type)) / Fixed1616Scale;
}

int32 USimCopterParticleFXComponent::GetFaceTypeForType(ESimCopterEffectType Type)
{
	switch (Type)
	{
	case ESimCopterEffectType::FireTrajectory:
	case ESimCopterEffectType::FireTrajectoryAlt:
	case ESimCopterEffectType::BuildingFireEmber:
	case ESimCopterEffectType::RotorWash: return 0x17;
	case ESimCopterEffectType::Spray:
	case ESimCopterEffectType::BucketDrip:
	case ESimCopterEffectType::SmallSpray:
	case ESimCopterEffectType::SplashSubParticle: return 0x1A;
	default: return 0;
	}
}

float USimCopterParticleFXComponent::GetDefaultSizeCmForType(ESimCopterEffectType Type)
{
	switch (Type)
	{
	case ESimCopterEffectType::Smoke:
	case ESimCopterEffectType::GeoSmoke: return 6.0f * SimCopterEffectFX::OriginalUnitToCm;
	case ESimCopterEffectType::SmallSmoke:
	case ESimCopterEffectType::Debris:
	case ESimCopterEffectType::HeavyDebris: return 3.0f * SimCopterEffectFX::OriginalUnitToCm;
	case ESimCopterEffectType::RotorWash: return 1.0f * SimCopterEffectFX::OriginalUnitToCm;
	case ESimCopterEffectType::BuildingFireSmoke:
	case ESimCopterEffectType::BuildingFireEmber: return 5.0f * SimCopterEffectFX::OriginalUnitToCm;
	default: return 20.0f * SimCopterEffectFX::OriginalUnitToCm;
	}
}

float USimCopterParticleFXComponent::GetTilePuffRiseSpeedCmPerSec(uint8 EffectClass)
{
	int32 OriginalUnitsPerSecond = 15;
	switch (EffectClass & 0x7F)
	{
	case 0: OriginalUnitsPerSecond = 10; break;
	case 1: OriginalUnitsPerSecond = 25; break;
	case 2: OriginalUnitsPerSecond = 17; break;
	case 3: OriginalUnitsPerSecond = 13; break;
	case 4: OriginalUnitsPerSecond = 30; break;
	case 5: OriginalUnitsPerSecond = 25; break;
	case 10: OriginalUnitsPerSecond = 20; break;
	default: break;
	}
	return static_cast<float>(OriginalUnitsPerSecond) * SimCopterEffectFX::OriginalUnitToCm;
}

int32 USimCopterParticleFXComponent::GetTilePuffLife1616()
{
	// FUN_004af220 stores the fixed countdown in slot[2]. The class switch writes
	// slot[5], which FUN_004af3b0 multiplies by dt and adds to vertical position;
	// those values are rise velocities, not lifetimes.
	return 0x20000;
}

bool USimCopterParticleFXComponent::IsRotorWashEligible(float HeightCm, int32 RotorSpeed1616)
{
	return HeightCm < 20.0f * SimCopterEffectFX::OriginalUnitToCm && RotorSpeed1616 > 0x1180000;
}

bool USimCopterParticleFXComponent::Allocate(ESimCopterEffectPool Pool, FSimCopterEffectSlot*& OutSlot)
{
	int32 Used = 0;
	for (const FSimCopterEffectSlot& Slot : Slots)
	{
		Used += Slot.bActive && Slot.Pool == Pool ? 1 : 0;
	}
	if (Used >= GetPoolCapacity(Pool))
	{
		return false;
	}
	for (FSimCopterEffectSlot& Slot : Slots)
	{
		if (!Slot.bActive)
		{
			Slot = FSimCopterEffectSlot();
			Slot.bActive = true;
			Slot.Pool = Pool;
			OutSlot = &Slot;
			return true;
		}
	}
	return false;
}

FIntPoint USimCopterParticleFXComponent::GetCellForWorld(const FVector& World) const
{
	return FIntPoint(FMath::FloorToInt(World.X / 400.0f), FMath::FloorToInt(World.Y / 400.0f));
}

void USimCopterParticleFXComponent::ConfigureEffect(FSimCopterEffectSlot& Slot, ESimCopterEffectType Type,
	const FVector& World, const FVector& VelocityCmPerSec, float SizeCm, const FIntPoint& Cell)
{
	Slot.Type = Type;
	Slot.Position = World;
	Slot.Velocity = VelocityCmPerSec;
	Slot.Direction1616 = SimCopterWaterGameplay::DirectionToFixed(VelocityCmPerSec);
	Slot.Cell = Cell;
	Slot.Life1616 = GetLifetime1616ForType(Type);
	Slot.SpawnTimer1616 = Type == ESimCopterEffectType::BuildingFireSmoke ? 0x38000 :
		(Type == ESimCopterEffectType::BuildingFireEmber ? 0x18000 : 0x8000);
	Slot.MotionScale1616 = FMath::RoundToInt(VelocityCmPerSec.Size() / SimCopterEffectFX::OriginalUnitToCm * Fixed1616Scale);
	Slot.PointCount = static_cast<uint8>(GetPointCountForType(Type));
	Slot.FaceType = static_cast<uint8>(GetFaceTypeForType(Type));
	Slot.bTrajectory = Slot.FaceType != 0;
	Slot.bApplyGravity = true;
	Slot.SizeCm = SizeCm > 0.0f ? SizeCm : GetDefaultSizeCmForType(Type);

	switch (Type)
	{
	case ESimCopterEffectType::Smoke: Slot.GeoObjectId = 0xAE; break;
	case ESimCopterEffectType::SmallSmoke: Slot.GeoObjectId = 0x147; break;
	case ESimCopterEffectType::GeoSmoke: Slot.GeoObjectId = 0x7C; break;
	case ESimCopterEffectType::Debris:
	case ESimCopterEffectType::HeavyDebris:
		Slot.GeoObjectId = 0x149 + DebrisObjectCursor;
		DebrisObjectCursor = (DebrisObjectCursor + 1) % 3;
		break;
	case ESimCopterEffectType::BuildingFireSmoke: Slot.GeoObjectId = 0xAE; break;
	default: break;
	}

	uint8 Color = 0;
	switch (Type)
	{
	case ESimCopterEffectType::FireTrajectory:
	case ESimCopterEffectType::FireTrajectoryAlt:
		Color = FireRampCursor++;
		if (FireRampCursor > 0x1F)
		{
			FireRampCursor = 0x10;
		}
		break;
	case ESimCopterEffectType::Spray:
	case ESimCopterEffectType::SmallSpray: Color = 3; break;
	case ESimCopterEffectType::BucketDrip: Color = 0; break;
	case ESimCopterEffectType::SplashSubParticle: Color = 5; break;
	case ESimCopterEffectType::RotorWash: Color = 8; break;
	case ESimCopterEffectType::BuildingFireEmber:
		Color = FireTipFrames[FireTipCursor++ & 3];
		break;
	default: Color = 0; break;
	}
	Slot.PaletteIndex = Color;
	for (int32 Point = 0; Point < Slot.PointCount; ++Point)
	{
		Slot.PointPaletteIndices[Point] = Color;
	}
}

bool USimCopterParticleFXComponent::SpawnEffect(ESimCopterEffectType Type, const FVector& World,
	const FVector& VelocityCmPerSec, float SizeCm, int32 CellX, int32 CellY)
{
	FSimCopterEffectSlot* Slot = nullptr;
	if (!Allocate(PoolForType(Type), Slot))
	{
		return false;
	}
	const FIntPoint Cell = CellX == INDEX_NONE || CellY == INDEX_NONE ? GetCellForWorld(World) : FIntPoint(CellX, CellY);
	ConfigureEffect(*Slot, Type, World, VelocityCmPerSec, SizeCm, Cell);

	// FUN_0048e0b0 bypasses this insertion for type 8 and suppresses it for type 7.
	if (Type != ESimCopterEffectType::RotorWash && Type != ESimCopterEffectType::SmallSpray)
	{
		SpawnTilePuff(
			World + VelocityCmPerSec.GetSafeNormal() * OriginalPointStepCm,
			InitialSplatClass(Type, Slot->MotionScale1616),
			Cell.X,
			Cell.Y);
	}
	return true;
}

bool USimCopterParticleFXComponent::SpawnTilePuff(const FVector& World, uint8 EffectClass, int32 CellX, int32 CellY)
{
	FSimCopterEffectSlot* Slot = nullptr;
	if (!Allocate(ESimCopterEffectPool::TilePuffs100, Slot))
	{
		return false;
	}
	Slot->Type = ESimCopterEffectType::Smoke;
	Slot->Position = World;
	Slot->Cell = CellX == INDEX_NONE || CellY == INDEX_NONE ? GetCellForWorld(World) : FIntPoint(CellX, CellY);
	Slot->Life1616 = GetTilePuffLife1616();
	Slot->Velocity.Z = GetTilePuffRiseSpeedCmPerSec(EffectClass);
	Slot->SizeCm = 20.0f * SimCopterEffectFX::OriginalUnitToCm;
	Slot->EffectClass = EffectClass;
	Slot->PaletteIndex = EffectClass;
	Slot->PointPaletteIndices[0] = EffectClass;
	Slot->GeoObjectId = 0x148;
	Slot->FaceType = 0x1a;
	Slot->PointCount = 1;
	return true;
}

bool USimCopterParticleFXComponent::SpawnSplashColumn(const FVector& World, int32 ScaleExponent,
	uint8, int32 CellX, int32 CellY, bool bSubmergeOrigin)
{
	FSimCopterEffectSlot* Slot = nullptr;
	if (!Allocate(ESimCopterEffectPool::SplashColumns20, Slot))
	{
		return false;
	}
	Slot->Type = ESimCopterEffectType::SplashSubParticle;
	Slot->Position = bSubmergeOrigin
		? World - FVector::UpVector * (32.0f * SimCopterEffectFX::OriginalUnitToCm)
		: World;
	Slot->Cell = CellX == INDEX_NONE || CellY == INDEX_NONE ? GetCellForWorld(World) : FIntPoint(CellX, CellY);
	Slot->SizeCm = static_cast<float>(4 << FMath::Clamp(ScaleExponent, 0, 8)) * SimCopterEffectFX::OriginalUnitToCm;
	Slot->PaletteIndex = SplashFrames[0];
	Slot->PointPaletteIndices[0] = SplashFrames[0];
	Slot->FrameCursor = -1;
	Slot->MotionScale1616 = 0x140000 << FMath::Clamp(ScaleExponent, 0, 8);
	Slot->GeoObjectId = 0x148;
	return true;
}

void USimCopterParticleFXComponent::SpawnHardLanding(const FVector& World, bool, int32 CellX, int32 CellY)
{
	SpawnTilePuff(World, 1, CellX, CellY);
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const float YawDegrees = FMath::FRandRange(0.0f, 359.9f);
		const float PitchDegrees = FMath::FRandRange(65.0f, 84.9f);
		const float SpeedCmPerSec = FMath::FRandRange(50.0f, 149.0f) * SimCopterEffectFX::OriginalUnitToCm;
		const FVector Direction = FRotator(PitchDegrees, YawDegrees, 0.0f).RotateVector(FVector::ForwardVector);
		SpawnEffect(ESimCopterEffectType::Debris, World, Direction * SpeedCmPerSec, 0.0f, CellX, CellY);
	}
	SpawnSplashColumn(World, 4, 0xFF, CellX, CellY);
}

void USimCopterParticleFXComponent::SpawnParticle(const FVector& World, const FVector& VelocityCmPerSec,
	float SizeCm, const FLinearColor& Color, float LifeSeconds, float GravityCmPerSec2)
{
	if (!SpawnEffect(ESimCopterEffectType::RotorWash, World, VelocityCmPerSec))
	{
		return;
	}
	for (FSimCopterEffectSlot& Slot : Slots)
	{
		if (Slot.bActive && Slot.Type == ESimCopterEffectType::RotorWash && Slot.Position.Equals(World))
		{
			Slot.Life1616 = SecondsToFixed(LifeSeconds);
			Slot.SizeCm = SizeCm > 0.0f ? SizeCm : 20.0f;
			Slot.bApplyGravity = FMath::Abs(GravityCmPerSec2) > 0.01f;
			Slot.GravityCmPerSec2 = GravityCmPerSec2;
			Slot.PaletteIndex = Color.B > Color.R ? 0x09 : (Color.G > Color.R ? 0x73 : 0x7B);
			Slot.PointPaletteIndices[0] = Slot.PaletteIndex;
			break;
		}
	}
}

void USimCopterParticleFXComponent::SpawnRing(const FVector& World, int32 Count, float SpeedCmPerSec,
	float InitialRiseCmPerSec, float SizeCm, const FLinearColor& Color, float LifeSeconds, float GravityCmPerSec2)
{
	for (int32 Index = 0; Index < FMath::Clamp(Count, 1, 32); ++Index)
	{
		const float Angle = 2.0f * PI * static_cast<float>(Index) / static_cast<float>(Count);
		SpawnParticle(World, FVector(FMath::Cos(Angle) * SpeedCmPerSec, FMath::Sin(Angle) * SpeedCmPerSec,
			InitialRiseCmPerSec), SizeCm, Color, LifeSeconds, GravityCmPerSec2);
	}
}

bool USimCopterParticleFXComponent::HasActiveParticles() const
{
	return Slots.ContainsByPredicate([](const FSimCopterEffectSlot& Slot) { return Slot.bActive; });
}

int32 USimCopterParticleFXComponent::GetActiveCount(ESimCopterEffectPool Pool) const
{
	int32 Count = 0;
	for (const FSimCopterEffectSlot& Slot : Slots)
	{
		Count += Slot.bActive && Slot.Pool == Pool ? 1 : 0;
	}
	return Count;
}

FVector USimCopterParticleFXComponent::GetCameraLocation() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (const APlayerCameraManager* Camera = PC->PlayerCameraManager)
			{
				return Camera->GetCameraLocation();
			}
		}
	}
	return GetComponentLocation();
}

void USimCopterParticleFXComponent::UpdateSplashColumns()
{
	for (FSimCopterEffectSlot& Slot : Slots)
	{
		if (!Slot.bActive || Slot.Pool != ESimCopterEffectPool::SplashColumns20)
		{
			continue;
		}
		++Slot.FrameCursor;
		if (Slot.FrameCursor >= UE_ARRAY_COUNT(SplashFrames))
		{
			Slot.bActive = false;
			continue;
		}
		Slot.PaletteIndex = SplashFrames[Slot.FrameCursor];
		Slot.PointPaletteIndices[0] = Slot.PaletteIndex;
		if (Slot.FrameCursor == 0 && !Slot.bBurstEmitted)
		{
			Slot.bBurstEmitted = true;
			const float SpeedCmPerSec =
				static_cast<float>(Slot.MotionScale1616) / Fixed1616Scale * SimCopterEffectFX::OriginalUnitToCm;
			for (const FVector& Direction : SplashDirections)
			{
				SpawnEffect(ESimCopterEffectType::SplashSubParticle, Slot.Position,
					Direction * SpeedCmPerSec, 0.0f, Slot.Cell.X, Slot.Cell.Y);
			}
		}
	}
}

void USimCopterParticleFXComponent::EmitFireBurst(const FSimCopterEffectSlot& Source)
{
	FVector Direction = FVector::ForwardVector;
	for (int32 Index = 0; Index < 24; ++Index)
	{
		Direction = Direction.RotateAngleAxis(240.0f, FVector::UpVector);
		SpawnEffect(ESimCopterEffectType::BuildingFireEmber, Source.Position,
			Direction * (60.0f * SimCopterEffectFX::OriginalUnitToCm), 0.0f, Source.Cell.X, Source.Cell.Y);
	}
}

ASimCity2000CityActor* USimCopterParticleFXComponent::ResolveCityActor()
{
	if (CachedCityActor.IsValid())
	{
		return CachedCityActor.Get();
	}
	if (UWorld* World = GetWorld())
	{
		CachedCityActor = Cast<ASimCity2000CityActor>(
			UGameplayStatics::GetActorOfClass(World, ASimCity2000CityActor::StaticClass()));
	}
	return CachedCityActor.Get();
}

ASimCopterMissionSystemActor* USimCopterParticleFXComponent::ResolveMissionActor()
{
	if (CachedMissionActor.IsValid())
	{
		return CachedMissionActor.Get();
	}
	if (UWorld* World = GetWorld())
	{
		CachedMissionActor = Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(World, ASimCopterMissionSystemActor::StaticClass()));
	}
	return CachedMissionActor.Get();
}

bool USimCopterParticleFXComponent::FindWaterTrajectoryImpact(
	const FVector& Start,
	const FVector& End,
	FVector& OutImpact,
	bool& bOutWaterSurface,
	FIntPoint& OutCell)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	float BestTime = 2.0f;
	bool bFound = false;
	bool bBestIsWater = false;
	FIntPoint BestCell = GetCellForWorld(End);
	FVector BestImpact = End;
	ASimCity2000CityActor* City = ResolveCityActor();

	// SCHOOK: WaterObjectCollision 0x00491370 0x0046efe0
	FHitResult ObjectHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterWaterTrajectory), false, GetOwner());
	if (World->LineTraceSingleByChannel(ObjectHit, Start, End, ECC_Visibility, QueryParams))
	{
		BestTime = ObjectHit.Time;
		BestImpact = ObjectHit.ImpactPoint;
		bFound = true;

		float SurfaceZ = 0.0f;
		uint8 TerrainClass = 0xff;
		FIntPoint SurfaceCell = BestCell;
		if (City != nullptr &&
			City->TryGetWaterGameplaySurface(BestImpact, SurfaceZ, TerrainClass, &SurfaceCell))
		{
			BestCell = SurfaceCell;
			bBestIsWater =
				City->IsTerrainCollisionComponent(ObjectHit.GetComponent()) &&
				SimCopterWaterGameplay::IsWaterTerrainClass(TerrainClass) &&
				FMath::Abs(BestImpact.Z - SurfaceZ) <=
					SimCopterEffectFX::OriginalUnitToCm;
		}
	}

	// SCHOOK: WaterTerrainCollision 0x00490690
	// The rendered water material can move visually, but gameplay samples the conditioned terrain
	// grid. Find the segment crossing against that grid even when terrain collision is disabled.
	if (City != nullptr)
	{
		float StartSurfaceZ = 0.0f;
		float EndSurfaceZ = 0.0f;
		uint8 StartClass = 0xff;
		uint8 EndClass = 0xff;
		FIntPoint EndCell = BestCell;
		if (City->TryGetWaterGameplaySurface(Start, StartSurfaceZ, StartClass) &&
			City->TryGetWaterGameplaySurface(End, EndSurfaceZ, EndClass, &EndCell))
		{
			const float StartClearance = Start.Z - StartSurfaceZ;
			const float EndClearance = End.Z - EndSurfaceZ;
			if (EndClearance <= 0.0f)
			{
				const float Denominator = StartClearance - EndClearance;
				const float HitTime = StartClearance <= 0.0f
					? 0.0f
					: (Denominator > KINDA_SMALL_NUMBER
						? FMath::Clamp(StartClearance / Denominator, 0.0f, 1.0f)
						: 1.0f);
				if (HitTime < BestTime)
				{
					FVector TerrainImpact = FMath::Lerp(Start, End, HitTime);
					float ImpactSurfaceZ = EndSurfaceZ;
					uint8 ImpactClass = EndClass;
					FIntPoint ImpactCell = EndCell;
					City->TryGetWaterGameplaySurface(
						TerrainImpact,
						ImpactSurfaceZ,
						ImpactClass,
						&ImpactCell);
					TerrainImpact.Z = ImpactSurfaceZ;
					BestTime = HitTime;
					BestImpact = TerrainImpact;
					BestCell = ImpactCell;
					bBestIsWater = SimCopterWaterGameplay::IsWaterTerrainClass(ImpactClass);
					bFound = true;
				}
			}
		}
	}

	if (!bFound)
	{
		return false;
	}
	OutImpact = BestImpact;
	bOutWaterSurface = bBestIsWater;
	OutCell = BestCell;
	return true;
}

void USimCopterParticleFXComponent::AdvanceWaterTrajectory(
	FSimCopterEffectSlot& Slot,
	const int32 Delta1616)
{
	// FUN_0048ed00 multiplies the speed by a fixed drag factor once per game frame, so how far a
	// droplet carries depends on how many frames it lives - not on how much time it lives. Run
	// the water on the original's own clock instead of the rendered frame; otherwise a fire
	// truck's reach shrinks as the frame rate rises and the jet falls short of the fire.
	Slot.StepCarry1616 += Delta1616;
	while (Slot.bActive && Slot.StepCarry1616 >= SimCopterWaterGameplay::SimulationStep1616)
	{
		Slot.StepCarry1616 -= SimCopterWaterGameplay::SimulationStep1616;
		AdvanceWaterTrajectoryStep(Slot, SimCopterWaterGameplay::SimulationStep1616);
	}
}

void USimCopterParticleFXComponent::AdvanceWaterTrajectoryStep(
	FSimCopterEffectSlot& Slot,
	const int32 Delta1616)
{
	const SimCopterWaterGameplay::EWaterEmitter Emitter =
		Slot.Type == ESimCopterEffectType::Spray
			? SimCopterWaterGameplay::EWaterEmitter::Cannon
			: SimCopterWaterGameplay::EWaterEmitter::Bucket;
	SimCopterWaterGameplay::FWaterParticleMotion Motion;
	Motion.Direction1616 = Slot.Direction1616;
	Motion.Speed1616 = Slot.MotionScale1616;
	Motion.Life1616 = Slot.Life1616;
	const SimCopterWaterGameplay::FWaterParticleFrame Frame =
		SimCopterWaterGameplay::AdvanceParticleFrame(Motion, Emitter, Delta1616);
	Slot.Direction1616 = Motion.Direction1616;
	Slot.MotionScale1616 = Motion.Speed1616;
	Slot.Life1616 = Motion.Life1616;
	if (!Frame.bAlive)
	{
		Slot.bActive = false;
		return;
	}

	const FVector Start = Slot.Position;
	const FVector End = Start + FVector(
		static_cast<float>(Frame.Travel1616.X) * SimCopterEffectFX::Fixed1616ToCm,
		static_cast<float>(Frame.Travel1616.Y) * SimCopterEffectFX::Fixed1616ToCm,
		static_cast<float>(Frame.Travel1616.Z) * SimCopterEffectFX::Fixed1616ToCm);
	FVector Impact;
	bool bWaterSurface = false;
	FIntPoint ImpactCell = Slot.Cell;
	if (FindWaterTrajectoryImpact(Start, End, Impact, bWaterSurface, ImpactCell))
	{
		const SimCopterWaterGameplay::FWaterImpact Result =
			SimCopterWaterGameplay::ResolveImpact(Emitter, Slot.Life1616, bWaterSurface);
		Slot.bActive = false;
		SpawnTilePuff(Impact, Result.PuffClass, ImpactCell.X, ImpactCell.Y);

		// SCHOOK: WaterImpactSound 0x00490690 - SPLISH where the water douses, DOUSE where it
		// hits the surface. Both are one-shots on a shared slot, so the hundreds of droplets a
		// cannon throws collapse into one continuous hiss instead of machine-gunning: Play is a
		// no-op while the slot is already sounding, which is the whole reason the original gets
		// away with playing this per particle.
		if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
		{
			Audio->Play3D(Result.SoundId, Impact);
		}

		if (Result.bDouse)
		{
			if (ASimCopterMissionSystemActor* Mission = ResolveMissionActor())
			{
				Mission->ApplyWaterParticleImpact(Impact, Result.DouseStrength1616);
			}
		}

		// SCHOOK: WaterHitsPeople 0x00490690. Water does not only put fires out - the impact loop's
		// class-to-mode table sends the whole water family to interaction mode 4:
		//     if ((uVar9 & 0xe0) == 0) { ... } else { local_d0 = 4; }
		// 0xe0 is the cannon (0x20), the bucket (0x40) and type 7 (0x80) together, and mode 4 is
		// DAT_0058d728[4] = BHAV 908 "Rxn: Water" - which drops a rioter's agitation, exactly as
		// tear gas does through 907. The remake had the douse half of this line and not the people
		// half, so hosing a crowd did nothing at all; this is what makes the water cannon a riot
		// tool and not just a fire hose.
		if (ASimCopterHelicopterPawn* Helicopter =
			Cast<ASimCopterHelicopterPawn>(UGameplayStatics::GetActorOfClass(
				GetWorld(), ASimCopterHelicopterPawn::StaticClass())))
		{
			FSimCopterInteractionEvent Event;
			Event.Mode = ESimCopterInteractionMode::Water;
			Event.Source = GetOwner();
			Event.TargetTile = ImpactCell;
			Event.TargetWorldLocation = Impact;
			Event.ImpactStrength = static_cast<float>(Result.DouseStrength1616) / Fixed1616Scale;
			Helicopter->DeliverInteractionToTile(Event);
		}
		return;
	}

	Slot.Position = End;
	Slot.Cell = GetCellForWorld(End);
	Slot.Velocity =
		SimCopterWaterGameplay::DirectionToFloat(Slot.Direction1616) *
		(static_cast<float>(Slot.MotionScale1616) / Fixed1616Scale) *
		SimCopterEffectFX::OriginalUnitToCm;
}

void USimCopterParticleFXComponent::UpdateSlots(float DeltaTime)
{
	const int32 Delta1616 = FMath::Max(1, SecondsToFixed(DeltaTime));
	for (FSimCopterEffectSlot& Slot : Slots)
	{
		if (!Slot.bActive || Slot.Pool == ESimCopterEffectPool::SplashColumns20)
		{
			continue;
		}
		Slot.Age1616 += Delta1616;
		if (Slot.Type == ESimCopterEffectType::Spray ||
			Slot.Type == ESimCopterEffectType::BucketDrip)
		{
			AdvanceWaterTrajectory(Slot, Delta1616);
			continue;
		}
		Slot.Life1616 -= Delta1616;
		if (Slot.Life1616 <= 0)
		{
			const bool bFireSmoke = Slot.Type == ESimCopterEffectType::BuildingFireSmoke;
			const FVector End = Slot.Position;
			const FIntPoint Cell = Slot.Cell;
			Slot.bActive = false;
			if (bFireSmoke)
			{
				SpawnSplashColumn(End, 3, 0xFF, Cell.X, Cell.Y);
			}
			continue;
		}

		if (Slot.Pool == ESimCopterEffectPool::TilePuffs100)
		{
			Slot.Position.Z += Slot.Velocity.Z * DeltaTime;
			continue;
		}

		Slot.SpawnTimer1616 -= Delta1616;
		if (Slot.Type == ESimCopterEffectType::BuildingFireSmoke &&
			Slot.SpawnTimer1616 < 0 && !Slot.bBurstEmitted)
		{
			Slot.bBurstEmitted = true;
			EmitFireBurst(Slot);
		}
		else if ((Slot.Type == ESimCopterEffectType::Debris || Slot.Type == ESimCopterEffectType::HeavyDebris) &&
			Slot.SpawnTimer1616 < 0)
		{
			Slot.SpawnTimer1616 = 0x8000;
			SpawnTilePuff(Slot.Position, Slot.MotionScale1616 < 0x30000 ? 4 : 1, Slot.Cell.X, Slot.Cell.Y);
			SpawnTilePuff(Slot.Position, 5, Slot.Cell.X, Slot.Cell.Y);
		}

		if (Slot.bApplyGravity)
		{
			const float GravityAcc = FMath::Abs(Slot.GravityCmPerSec2) > 0.01f
				? FMath::Abs(Slot.GravityCmPerSec2)
				: SimCopterEffectFX::GravityCmPerSec2;
			Slot.Velocity.Z -= GravityAcc * DeltaTime;
		}
		Slot.Position += Slot.Velocity * DeltaTime;
		Slot.Cell = GetCellForWorld(Slot.Position);
	}
	UpdateSplashColumns();
}

void USimCopterParticleFXComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Presentation time: a replay review freezes the world, and these particles are the clip's fire,
	// water, dust and rotor wash. Frozen, the replay shows a city where nothing is happening.
	const float Delta = SimCopterReplay::GetPresentationDeltaSeconds(DeltaTime);
	UpdateSlots(Delta);
	RebuildMesh(GetCameraLocation());
}

FLinearColor USimCopterParticleFXComponent::PaletteColor(uint8 PaletteIndex) const
{
	if (SharedPalette.IsValidIndex(PaletteIndex))
	{
		return FLinearColor(SharedPalette[PaletteIndex]);
	}
	static const FColor FireRamp[16] = {
		FColor(0x8F,0x10,0x05), FColor(0x90,0x1A,0x05), FColor(0x9A,0x20,0x05), FColor(0x9F,0x2F,0x05),
		FColor(0xA5,0x35,0x05), FColor(0xAA,0x3F,0x0A), FColor(0xB0,0x45,0x0A), FColor(0xB5,0x50,0x0A),
		FColor(0xBF,0x5A,0x0A), FColor(0xC0,0x60,0x0A), FColor(0xCA,0x6F,0x0A), FColor(0xCF,0x75,0x0A),
		FColor(0xD0,0x7F,0x0A), FColor(0xDA,0x85,0x0A), FColor(0xDF,0x90,0x0A), FColor(0xE5,0x9A,0x0A),
	};
	if (PaletteIndex >= 0x10 && PaletteIndex <= 0x1F)
	{
		return FLinearColor(FireRamp[PaletteIndex - 0x10]);
	}
	switch (PaletteIndex)
	{
	case 0x08: return FLinearColor(FColor(0xC0, 0xDF, 0xC0));
	case 0x09: return FLinearColor(FColor(0xA5, 0xCA, 0xF0));
	case 0x73: return FLinearColor(FColor(0xFF, 0xF0, 0x1F));
	case 0x7B: return FLinearColor(FColor(0xFF, 0xDA, 0x6F));
	default: return FLinearColor::White;
	}
}

void USimCopterParticleFXComponent::RebuildMesh(const FVector& CameraLocation)
{
	if (MeshComponent == nullptr)
	{
		return;
	}

	struct FRuntimeSection
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;
	};
	FRuntimeSection Kernels;
	FRuntimeSection Solids;
	const FTransform WorldToLocal = GetComponentTransform().Inverse();

	FRotator CameraRotation = (GetComponentLocation() - CameraLocation).Rotation();
	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (const APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
		{
			CameraRotation = CameraManager->GetCameraRotation();
		}
	}
	const FVector CameraForward = CameraRotation.Vector();
	const FVector CameraRight = FRotationMatrix(CameraRotation).GetScaledAxis(EAxis::Y);
	const FVector CameraUp = FRotationMatrix(CameraRotation).GetScaledAxis(EAxis::Z);
	const int32 RasterFrame = FMath::FloorToInt(
		(GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0f) * 20.0f);

	auto AddQuad = [&WorldToLocal](
		FRuntimeSection& Section,
		const FVector& Center,
		const FVector& Right,
		const FVector& Up,
		float HalfWidth,
		float HalfHeight,
		const FVector2D& UV0,
		const FVector2D& UV1,
		const FLinearColor& Color,
		const FVector& Normal)
	{
		const int32 Base = Section.Vertices.Num();
		Section.Vertices.Add(WorldToLocal.TransformPosition(Center - Right * HalfWidth - Up * HalfHeight));
		Section.Vertices.Add(WorldToLocal.TransformPosition(Center + Right * HalfWidth - Up * HalfHeight));
		Section.Vertices.Add(WorldToLocal.TransformPosition(Center + Right * HalfWidth + Up * HalfHeight));
		Section.Vertices.Add(WorldToLocal.TransformPosition(Center - Right * HalfWidth + Up * HalfHeight));
		Section.UVs.Append({
			FVector2D(UV0.X, UV1.Y),
			UV1,
			FVector2D(UV1.X, UV0.Y),
			UV0
		});
		const FVector LocalNormal = WorldToLocal.TransformVectorNoScale(Normal);
		const FProcMeshTangent Tangent(WorldToLocal.TransformVectorNoScale(Right), false);
		for (int32 Vertex = 0; Vertex < 4; ++Vertex)
		{
			Section.Normals.Add(LocalNormal);
			Section.Colors.Add(Color);
			Section.Tangents.Add(Tangent);
		}
		Section.Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
	};

	// One original 560x400 viewport pixel, in centimetres at that camera depth. Both
	// effect handlers measure their output in those pixels and the original's pixel
	// aspect is 1.0, so a single square size drives every card.
	auto GetPixelSizeCm = [](float CameraDepth)
	{
		return FSimCopterEffectRasterizer::GetWorldSizePerViewportPixel(CameraDepth);
	};

	auto AddFace17 = [&](const FVector& Center, uint8 PaletteIndex)
	{
		const float CameraDepth = FVector::DotProduct(Center - CameraLocation, CameraForward);
		if (CameraDepth <= 1.0f)
		{
			return;
		}
		// FUN_00491520 writes a 2x2 block in the 0x10 renderer, so the half extent is
		// exactly one pixel on each axis.
		const float PixelSizeCm = GetPixelSizeCm(CameraDepth);
		FLinearColor Color = PaletteColor(PaletteIndex);
		Color.A = 1.0f;
		AddQuad(
			Solids,
			Center,
			CameraRight,
			CameraUp,
			PixelSizeCm,
			PixelSizeCm,
			FVector2D::ZeroVector,
			FVector2D(1.0f, 1.0f),
			Color,
			-CameraForward);
	};

	auto AddFace1A = [&](const FVector& Center, uint8 EffectClass, uint32 RandomSeed)
	{
		const float CameraDepth = FVector::DotProduct(Center - CameraLocation, CameraForward);
		if (CameraDepth <= 1.0f)
		{
			return;
		}
		const FSimCopterEffectKernelMetrics Metrics =
			FSimCopterEffectRasterizer::ComputeKernelMetrics(
				EffectClass,
				FSimCopterEffectRasterizer::ComputeDepthScale1616(CameraDepth));
		if (Metrics.Iterations <= 0)
		{
			return;
		}
		const float PixelSizeCm = GetPixelSizeCm(CameraDepth);
		uint32 RandomState = RandomSeed ^ static_cast<uint32>(RasterFrame) * 0xc2b2ae35u;
		// Preserve the source rand()%7 remap-row sample even though the 0x10
		// selector-write path does not consume the selected remap row.
		FSimCopterEffectRasterizer::AdvanceRandom(RandomState);
		for (int32 Iteration = 0; Iteration < Metrics.Iterations; ++Iteration)
		{
			const int32 JitterX =
				static_cast<int32>(FSimCopterEffectRasterizer::AdvanceRandom(RandomState) %
					static_cast<uint32>(Metrics.JitterSpanPixels)) -
				Metrics.JitterHalfExtentPixels;
			const int32 JitterY =
				static_cast<int32>(FSimCopterEffectRasterizer::AdvanceRandom(RandomState) %
					static_cast<uint32>(Metrics.JitterSpanPixels)) -
				Metrics.JitterHalfExtentPixels;
			const int32 Radius =
				Metrics.MinRadius +
				static_cast<int32>(FSimCopterEffectRasterizer::AdvanceRandom(RandomState) %
					static_cast<uint32>(Metrics.RadiusChoiceCount));
			const FSimCopterEffectStencilMetrics Stencil =
				FSimCopterEffectRasterizer::GetStencilMetrics(Radius);
			const float RasterWidth = static_cast<float>(Stencil.Width);
			const float RasterHeight = static_cast<float>(Stencil.Height);
			// The original writes the kernel's first pixel at the jittered projected
			// point and grows right/down, so the covered span is centred half a pixel
			// short of the geometric middle.
			const FVector KernelCenter =
				Center +
				CameraRight *
					((static_cast<float>(JitterX) + (RasterWidth - 1.0f) * 0.5f) * PixelSizeCm) -
				CameraUp *
					((static_cast<float>(JitterY) + (RasterHeight - 1.0f) * 0.5f) * PixelSizeCm);
			const int32 SelectorPhase =
				FSimCopterEffectRasterizer::ConsumeSelectorPhase(Radius);
			const FVector2D UV0 = FSimCopterEffectRasterizer::GetAtlasUV(
				EffectClass, SelectorPhase, Radius, 0, 0);
			const FVector2D UV1 = FSimCopterEffectRasterizer::GetAtlasUV(
				EffectClass, SelectorPhase, Radius, Stencil.Width, Stencil.Height);
			AddQuad(
				Kernels,
				KernelCenter,
				CameraRight,
				CameraUp,
				RasterWidth * PixelSizeCm * 0.5f,
				RasterHeight * PixelSizeCm * 0.5f,
				UV0,
				UV1,
				FLinearColor::White,
				-CameraForward);
		}
	};

	auto AddGeoTemplate = [&](const FSimCopterEffectSlot& Slot, int32 RenderIndex)
	{
		const FGeoEffectTemplate* Template = GeoTemplates.Find(Slot.GeoObjectId);
		if (Template == nullptr)
		{
			return;
		}
		const float TemplateScale = Slot.SizeCm / FMath::Max(Template->SourceSpanCm, 1.0f);
		auto ToWorld = [&](const FVector& Local)
		{
			return Slot.Position +
				(-CameraForward * Local.X + CameraRight * Local.Y + CameraUp * Local.Z) *
					TemplateScale;
		};

		for (int32 PointIndex = 0; PointIndex < Template->Points.Num(); ++PointIndex)
		{
			const FGeoEffectPoint& Point = Template->Points[PointIndex];
			const FVector WorldPoint = ToWorld(Point.LocalOffset);
			if (Point.FaceType == 0x17)
			{
				AddFace17(WorldPoint, Point.MaterialIndex);
			}
			else
			{
				AddFace1A(
					WorldPoint,
					Point.MaterialIndex,
					static_cast<uint32>(RenderIndex * 257 + PointIndex * 17));
			}
		}

		for (const FGeoEffectFace& Face : Template->Faces)
		{
			if (Face.Vertices.Num() == 2)
			{
				const FVector A = ToWorld(Face.Vertices[0]);
				const FVector B = ToWorld(Face.Vertices[1]);
				const FVector Center = (A + B) * 0.5f;
				const FVector LineUp = (B - A).GetSafeNormal();
				const float CameraDepth =
					FVector::DotProduct(Center - CameraLocation, CameraForward);
				if (CameraDepth > 1.0f)
				{
					AddQuad(
						Solids,
						Center,
						CameraRight,
						LineUp,
						GetPixelSizeCm(CameraDepth),
						FVector::Distance(A, B) * 0.5f,
						FVector2D::ZeroVector,
						FVector2D(1.0f, 1.0f),
						Face.Color,
						-CameraForward);
				}
				continue;
			}

			const int32 Base = Solids.Vertices.Num();
			for (const FVector& LocalVertex : Face.Vertices)
			{
				Solids.Vertices.Add(WorldToLocal.TransformPosition(ToWorld(LocalVertex)));
				Solids.UVs.Add(FVector2D::ZeroVector);
				Solids.Colors.Add(Face.Color);
				Solids.Tangents.Add(FProcMeshTangent(
					WorldToLocal.TransformVectorNoScale(CameraRight),
					false));
			}
			const FVector LocalNormal = WorldToLocal.TransformVectorNoScale(-CameraForward);
			for (int32 Vertex = 0; Vertex < Face.Vertices.Num(); ++Vertex)
			{
				Solids.Normals.Add(LocalNormal);
			}
			for (int32 Triangle = 1; Triangle < Face.Vertices.Num() - 1; ++Triangle)
			{
				Solids.Triangles.Append({
					Base,
					Base + Triangle,
					Base + Triangle + 1
				});
			}
		}
	};

	for (int32 RenderIndex = 0; RenderIndex < Slots.Num(); ++RenderIndex)
	{
		const FSimCopterEffectSlot& Slot = Slots[RenderIndex];
		if (!Slot.bActive)
		{
			continue;
		}

		if (Slot.FaceType == 0 && Slot.GeoObjectId != INDEX_NONE)
		{
			AddGeoTemplate(Slot, RenderIndex);
			continue;
		}

		FVector TrajectoryDirection = Slot.Velocity.GetSafeNormal();
		if (TrajectoryDirection.IsNearlyZero())
		{
			TrajectoryDirection = FVector::UpVector;
		}
		for (int32 PointIndex = 0; PointIndex < Slot.PointCount; ++PointIndex)
		{
			const FVector Center =
				Slot.Position +
				(Slot.bTrajectory
					? TrajectoryDirection * OriginalPointStepCm * PointIndex
					: FVector::ZeroVector);
			if (Slot.FaceType == 0x17)
			{
				AddFace17(Center, Slot.PointPaletteIndices[PointIndex]);
			}
			else if (Slot.FaceType == 0x1a)
			{
				const uint8 EffectClass =
					Slot.Pool == ESimCopterEffectPool::TilePuffs100
						? Slot.EffectClass
						: Slot.PointPaletteIndices[PointIndex];
				AddFace1A(
					Center,
					EffectClass,
					static_cast<uint32>(RenderIndex * 257 + PointIndex * 17));
			}
		}
	}

	// Both card materials are UNLIT - the original stamped a palette colour into the frame buffer -
	// so their brightness is an absolute number of nits that has to be put on the same scale as the
	// sun lighting the city, or the cards tonemap to black. See USimCopterEffectExposureSubsystem.
	ApplyEffectExposure();

	if (Kernels.Vertices.IsEmpty())
	{
		MeshComponent->ClearMeshSection(0);
	}
	else
	{
		MeshComponent->CreateMeshSection_LinearColor(
			0,
			Kernels.Vertices,
			Kernels.Triangles,
			Kernels.Normals,
			Kernels.UVs,
			Kernels.Colors,
			Kernels.Tangents,
			false);
		if (KernelMaterialInstance != nullptr)
		{
			MeshComponent->SetMaterial(0, KernelMaterialInstance);
		}
	}

	if (Solids.Vertices.IsEmpty())
	{
		MeshComponent->ClearMeshSection(1);
	}
	else
	{
		MeshComponent->CreateMeshSection_LinearColor(
			1,
			Solids.Vertices,
			Solids.Triangles,
			Solids.Normals,
			Solids.UVs,
			Solids.Colors,
			Solids.Tangents,
			false);
		if (UMaterialInterface* Material = CardMaterialInstance != nullptr
			? static_cast<UMaterialInterface*>(CardMaterialInstance.Get())
			: (CardMaterialOverride != nullptr ? CardMaterialOverride.Get() : CardMaterial.Get()))
		{
			MeshComponent->SetMaterial(1, Material);
		}
	}
}

void USimCopterParticleFXComponent::ApplyEffectExposure()
{
	UMaterialInterface* Parent =
		CardMaterialOverride != nullptr ? CardMaterialOverride.Get() : CardMaterial.Get();
	if (CardMaterialInstance == nullptr ||
		(Parent != nullptr && CardMaterialInstance->Parent != Parent))
	{
		// Rebuilt rather than kept when the override changes, so a material swapped in the details
		// panel still gets the exposure scale rather than silently reverting to the black default.
		CardMaterialInstance = Parent != nullptr ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
	}

	const float Nits = USimCopterEffectExposureSubsystem::GetEffectEmissiveNitsForWorld(GetWorld());
	const FName ParameterName = USimCopterEffectExposureSubsystem::GetEmissiveNitsParameterName();
	if (CardMaterialInstance != nullptr)
	{
		CardMaterialInstance->SetScalarParameterValue(ParameterName, Nits);
	}
	if (KernelMaterialInstance != nullptr)
	{
		// The kernels ride M_SimCopterSpriteTexture, which is unlit for the same reason and needs
		// the same scale. A material without the parameter ignores this silently.
		KernelMaterialInstance->SetScalarParameterValue(ParameterName, Nits);
	}
}
