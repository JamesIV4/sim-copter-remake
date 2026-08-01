// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterApachePool.h"

#include "Audio/SimCopterAudioSubsystem.h"
#include "Audio/SimCopterSoundTable.h"
#include "Camera/PlayerCameraManager.h"
#include "City/SimCity2000CityActor.h"
#include "CollisionShape.h"
#include "Engine/OverlapResult.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Flight/SimCopterWaterGameplay.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Ground/SimCopterEffectFX.h"
#include "Ground/SimCopterEffectRasterizer.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterInteraction.h"
#include "Ground/SimCopterParticleFX.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Missions/SimCopterMissionSystem.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterApache, Log, All);

namespace
{
// FUN_0048db20's pool sizes.
constexpr int32 MissileSlots = 10;
constexpr int32 BulletSlots = 70;
constexpr int32 TotalSlots = MissileSlots + BulletSlots;

// FUN_0048e0b0: both live 0x50000 = 5.0 s.
constexpr int32 ProjectileLife1616 = 0x50000;

// FUN_00484d20's per-weapon speed bonus, added to heli[0x4e].
constexpr int32 MissileSpeedBonus1616 = 0x1c20000; // 450.0 units/s
constexpr int32 BulletSpeedBonus1616 = 0x2580000;  // 600.0 units/s

// FUN_004af100 column scales. The missile passes a plain 2; a bullet passes 0x80000001, whose
// low bits make it a scale-1 burst.
constexpr int32 MissileImpactColumnScale = 2;
constexpr int32 BulletImpactColumnScale = 1;
// The terrain arm of the missile's branch throws a bigger one.
constexpr int32 MissileTerrainColumnScale = 4;

// The debris burst a missile leaves on the ground: 3 + rand % height emitters at 0x640000.
constexpr int32 MissileDebrisMinimum = 3;
constexpr int32 MissileDebrisRandomRange = 6;
constexpr float MissileDebrisSpeedCmPerSec = 100.0f * SimCopterEffectFX::OriginalUnitToCm;

// The muzzle card each weapon registers (FUN_0048e0b0's local_c8).
constexpr uint8 MissileMuzzlePuffClass = 1;
constexpr uint8 BulletMuzzlePuffClass = 4;

constexpr float MissileUnitsPerCentimeter = 2621.44f;
constexpr float MissileModelScale = 0.25f;

// Same fixed clock the tear gas pool runs on, for the same reason.
constexpr int32 SimulationStep1616 = SimCopterWaterGameplay::SimulationStep1616;
constexpr int32 MaxStepsPerTick = 8;
constexpr uint32 ApacheRuntimeSaveMagic = 0x41504143; // 'APAC'
constexpr int32 ApacheRuntimeSaveVersion = 1;

void SerializeApacheBool(FArchive& Archive, bool& Value)
{
	uint8 Byte = Value ? 1 : 0;
	Archive << Byte;
	if (Archive.IsLoading()) Value = Byte != 0;
}
}

USimCopterApachePoolComponent::USimCopterApachePoolComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	Slots.SetNum(TotalSlots);
	MissileMeshes.SetNum(MissileSlots);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ModelMaterialFinder(
		TEXT("/Game/Materials/M_SimCopterLitVertexColor.M_SimCopterLitVertexColor"));
	if (ModelMaterialFinder.Succeeded())
	{
		VertexColorMaterial = ModelMaterialFinder.Object;
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BulletMaterialFinder(
		TEXT("/Game/Materials/M_SimCopterParticleFX.M_SimCopterParticleFX"));
	BulletMaterial = BulletMaterialFinder.Succeeded()
		? BulletMaterialFinder.Object.Get()
		: VertexColorMaterial.Get();
}

void USimCopterApachePoolComponent::OnRegister()
{
	Super::OnRegister();
	Slots.SetNum(TotalSlots);
	MissileMeshes.SetNum(MissileSlots);
	if (BulletMesh == nullptr)
	{
		BulletMesh = NewObject<UProceduralMeshComponent>(this, TEXT("ApacheMachineGunTracers"));
		BulletMesh->SetupAttachment(this);
		BulletMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BulletMesh->SetCanEverAffectNavigation(false);
		BulletMesh->SetCastShadow(false);
		BulletMesh->bUseAsyncCooking = false;
		BulletMesh->RegisterComponent();
	}
}

void USimCopterApachePoolComponent::SetEffectComponent(USimCopterParticleFXComponent* InEffects)
{
	Effects = InEffects;
}

ASimCopterHelicopterPawn* USimCopterApachePoolComponent::GetHelicopter() const
{
	return Cast<ASimCopterHelicopterPawn>(GetOwner());
}

ASimCity2000CityActor* USimCopterApachePoolComponent::ResolveCityActor()
{
	if (ASimCity2000CityActor* Cached = CachedCityActor.Get())
	{
		return Cached;
	}
	ASimCity2000CityActor* Found = Cast<ASimCity2000CityActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCity2000CityActor::StaticClass()));
	CachedCityActor = Found;
	return Found;
}

UProceduralMeshComponent* USimCopterApachePoolComponent::EnsureMissileMesh(const int32 SlotIndex)
{
	if (!MissileMeshes.IsValidIndex(SlotIndex))
	{
		return nullptr;
	}
	if (UProceduralMeshComponent* Existing = MissileMeshes[SlotIndex])
	{
		return Existing;
	}

	// GEO 0x0ae, the object FUN_0048db20 puts on every slot of DAT_005d4900. Built once and
	// shared, like the tear gas canister.
	if (!bMeshBuildAttempted)
	{
		bMeshBuildAttempted = true;

		FMaxisMeshLibrary MeshLibrary;
		FString Error;
		const TArray<FColor>* ColorMap = nullptr;
		const FMaxisMeshObject* Object = nullptr;
		if (!MeshLibrary.LoadFromOriginalGameRoot(OriginalGameRoot, Error))
		{
			LastMeshError = Error;
		}
		else if ((Object = MeshLibrary.FindObjectByObjectId(
			SimCopterHelicopterObjects::Missile, &ColorMap)) == nullptr)
		{
			LastMeshError = FString::Printf(
				TEXT("Could not find the Apache missile (GEO id 0x%03x) in '%s'."),
				SimCopterHelicopterObjects::Missile,
				*OriginalGameRoot);
		}
		else
		{
			FMaxisProceduralMeshBuilder::BuildPaletteColoredSection(
				*Object,
				ColorMap,
				MissileUnitsPerCentimeter,
				MissileModelScale,
				/*bAddBackfaces=*/true,
				FLinearColor(0.45f, 0.45f, 0.48f),
				MissileSection);
			if (MissileSection.IsEmpty())
			{
				LastMeshError = TEXT("The Apache missile built no triangles.");
			}
		}

		if (!LastMeshError.IsEmpty())
		{
			UE_LOG(LogSimCopterApache, Warning,
				TEXT("Apache missile mesh unavailable, firing without one: %s"), *LastMeshError);
		}
	}

	if (MissileSection.IsEmpty())
	{
		return nullptr;
	}

	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
	Mesh->SetupAttachment(this);
	Mesh->RegisterComponent();
	// World space: a missile does not ride the launcher.
	Mesh->SetUsingAbsoluteLocation(true);
	Mesh->SetUsingAbsoluteRotation(true);
	Mesh->SetUsingAbsoluteScale(true);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);
	Mesh->bUseAsyncCooking = false;
	Mesh->CreateMeshSection_LinearColor(
		0,
		MissileSection.Vertices,
		MissileSection.Triangles,
		MissileSection.Normals,
		MissileSection.UVs,
		MissileSection.VertexColors,
		MissileSection.Tangents,
		false);
	if (VertexColorMaterial != nullptr)
	{
		Mesh->SetMaterial(0, VertexColorMaterial);
	}
	Mesh->SetVisibility(false);

	MissileMeshes[SlotIndex] = Mesh;
	return Mesh;
}

bool USimCopterApachePoolComponent::LaunchMissile(
	const FVector& World, const FVector& Direction, const int32 ForwardSpeed1616)
{
	return Launch(EKind::Missile, World, Direction, ForwardSpeed1616);
}

bool USimCopterApachePoolComponent::LaunchBullet(
	const FVector& World, const FVector& Direction, const int32 ForwardSpeed1616)
{
	return Launch(EKind::Bullet, World, Direction, ForwardSpeed1616);
}

bool USimCopterApachePoolComponent::Launch(
	const EKind Kind,
	const FVector& World,
	const FVector& Direction,
	const int32 ForwardSpeed1616)
{
	// The two pools are separate arrays in the original, so a magazine of tracers can never
	// starve the missiles. Keeping them in one array with a fixed split does the same.
	const int32 First = Kind == EKind::Missile ? 0 : MissileSlots;
	const int32 Last = Kind == EKind::Missile ? MissileSlots : TotalSlots;

	for (int32 Index = First; Index < Last; ++Index)
	{
		FSlot& Slot = Slots[Index];
		if (Slot.bActive)
		{
			continue;
		}

		const int32 Bonus = Kind == EKind::Missile ? MissileSpeedBonus1616 : BulletSpeedBonus1616;
		Slot.bActive = true;
		Slot.Kind = Kind;
		Slot.Position = World;
		Slot.Direction = Direction.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
		// Constant speed for the whole flight - neither pool takes drag or gravity in
		// FUN_0048ed00, which is what makes them fly flat where the tear gas canister arcs.
		Slot.SpeedCmPerSec =
			SimCopterFixed::ToFloat(ForwardSpeed1616 + Bonus) * SimCopterEffectFX::OriginalUnitToCm;
		Slot.Life1616 = ProjectileLife1616;
		Slot.MeshIndex = Kind == EKind::Missile ? Index : INDEX_NONE;
		if (Kind == EKind::Bullet)
		{
			Slot.PaletteIndex = TracerPaletteCursor;
			TracerPaletteCursor = TracerPaletteCursor >= 0x1f ? 0x10 : TracerPaletteCursor + 1;
		}

		if (Slot.MeshIndex != INDEX_NONE)
		{
			if (UProceduralMeshComponent* Mesh = EnsureMissileMesh(Slot.MeshIndex))
			{
				Mesh->SetWorldLocation(Slot.Position);
				Mesh->SetWorldRotation(Slot.Direction.Rotation());
				Mesh->SetVisibility(true);
			}
		}

		// The muzzle card FUN_0048e0b0 registers at position + direction * 10.
		if (USimCopterParticleFXComponent* FX = Effects.Get())
		{
			const FVector MuzzleWorld =
				Slot.Position + Slot.Direction * (10.0f * SimCopterEffectFX::OriginalUnitToCm);
			FX->SpawnTilePuff(
				MuzzleWorld,
				Kind == EKind::Missile ? MissileMuzzlePuffClass : BulletMuzzlePuffClass);
		}
		return true;
	}

	return false;
}

int32 USimCopterApachePoolComponent::GetActiveMissileCount() const
{
	int32 Count = 0;
	for (int32 Index = 0; Index < MissileSlots; ++Index)
	{
		Count += Slots[Index].bActive ? 1 : 0;
	}
	return Count;
}

int32 USimCopterApachePoolComponent::GetActiveBulletCount() const
{
	int32 Count = 0;
	for (int32 Index = MissileSlots; Index < TotalSlots; ++Index)
	{
		Count += Slots[Index].bActive ? 1 : 0;
	}
	return Count;
}

bool USimCopterApachePoolComponent::CaptureRuntimeSaveState(TArray<uint8>& OutData) const
{
	OutData.Reset();
	FMemoryWriter Writer(OutData, true);
	uint32 Magic = ApacheRuntimeSaveMagic;
	int32 Version = ApacheRuntimeSaveVersion;
	int32 SavedStepAccumulator = StepAccumulator1616;
	uint8 SavedPaletteCursor = TracerPaletteCursor;
	int32 SlotCount = Slots.Num();
	Writer << Magic << Version << SavedStepAccumulator << SavedPaletteCursor << SlotCount;
	for (const FSlot& ConstSlot : Slots)
	{
		FSlot Slot = ConstSlot;
		SerializeApacheBool(Writer, Slot.bActive);
		uint8 Kind = static_cast<uint8>(Slot.Kind);
		Writer << Kind << Slot.Position << Slot.Direction << Slot.SpeedCmPerSec;
		Writer << Slot.Life1616 << Slot.MeshIndex << Slot.PaletteIndex;
	}
	if (Writer.IsError()) OutData.Reset();
	return !Writer.IsError();
}

bool USimCopterApachePoolComponent::RestoreRuntimeSaveState(const TArray<uint8>& Data)
{
	if (Data.IsEmpty()) return false;
	FMemoryReader Reader(Data, true);
	uint32 Magic = 0;
	int32 Version = 0;
	int32 SlotCount = 0;
	Reader << Magic << Version << StepAccumulator1616 << TracerPaletteCursor << SlotCount;
	if (Magic != ApacheRuntimeSaveMagic || Version != ApacheRuntimeSaveVersion ||
		SlotCount != TotalSlots || SlotCount != Slots.Num())
	{
		return false;
	}

	ClearAll();
	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		FSlot& Slot = Slots[Index];
		SerializeApacheBool(Reader, Slot.bActive);
		uint8 Kind = 0;
		Reader << Kind << Slot.Position << Slot.Direction << Slot.SpeedCmPerSec;
		Reader << Slot.Life1616 << Slot.MeshIndex << Slot.PaletteIndex;
		if (Kind > static_cast<uint8>(EKind::Bullet) ||
			(Slot.bActive && ((Index < MissileSlots) !=
				(Kind == static_cast<uint8>(EKind::Missile)))))
		{
			return false;
		}
		Slot.Kind = static_cast<EKind>(Kind);
		if (Slot.bActive && Slot.Kind == EKind::Missile)
		{
			if (UProceduralMeshComponent* Mesh = EnsureMissileMesh(Index))
			{
				Mesh->SetWorldLocation(Slot.Position);
				Mesh->SetWorldRotation(Slot.Direction.Rotation());
				Mesh->SetVisibility(true);
			}
		}
	}
	if (Reader.IsError() || Reader.Tell() != Reader.TotalSize()) return false;
	RebuildBulletMesh();
	return true;
}

void USimCopterApachePoolComponent::ClearAll()
{
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		ReleaseSlot(Index);
	}
	if (BulletMesh != nullptr)
	{
		BulletMesh->ClearMeshSection(0);
	}
}

void USimCopterApachePoolComponent::ReleaseSlot(const int32 SlotIndex)
{
	if (!Slots.IsValidIndex(SlotIndex))
	{
		return;
	}
	FSlot& Slot = Slots[SlotIndex];
	Slot.bActive = false;
	if (MissileMeshes.IsValidIndex(Slot.MeshIndex) && MissileMeshes[Slot.MeshIndex] != nullptr)
	{
		MissileMeshes[Slot.MeshIndex]->SetVisibility(false);
	}
}

void USimCopterApachePoolComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	bool bAnyActive = false;
	for (const FSlot& Slot : Slots)
	{
		bAnyActive |= Slot.bActive;
	}
	if (!bAnyActive)
	{
		StepAccumulator1616 = 0;
		RebuildBulletMesh();
		return;
	}

	StepAccumulator1616 += FMath::RoundToInt(DeltaTime * 65536.0f);
	int32 Steps = 0;
	while (StepAccumulator1616 >= SimulationStep1616 && Steps < MaxStepsPerTick)
	{
		StepAccumulator1616 -= SimulationStep1616;
		++Steps;
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			if (Slots[Index].bActive)
			{
				AdvanceSlot(Index, SimulationStep1616);
			}
		}
	}
	if (Steps >= MaxStepsPerTick)
	{
		StepAccumulator1616 = 0;
	}
	RebuildBulletMesh();
}

void USimCopterApachePoolComponent::AdvanceSlot(const int32 SlotIndex, const int32 Delta1616)
{
	FSlot& Slot = Slots[SlotIndex];

	Slot.Life1616 -= Delta1616;
	if (Slot.Life1616 < 1)
	{
		ReleaseSlot(SlotIndex);
		return;
	}

	const float DeltaSeconds = SimCopterFixed::ToFloat(Delta1616);
	const FVector Start = Slot.Position;
	const FVector End = Start + Slot.Direction * (Slot.SpeedCmPerSec * DeltaSeconds);

	if (ResolveImpact(Slot, Start, End))
	{
		ReleaseSlot(SlotIndex);
		return;
	}
	Slot.Position = End;

	if (Slot.Kind == EKind::Missile)
	{
		if (MissileMeshes.IsValidIndex(Slot.MeshIndex) && MissileMeshes[Slot.MeshIndex] != nullptr)
		{
			MissileMeshes[Slot.MeshIndex]->SetWorldLocation(Slot.Position);
		}
		// The exhaust. The original registers one kind-1 card at the muzzle and lets the
		// missile's own render node carry the rest; a trail reads better at this speed.
		if (USimCopterParticleFXComponent* FX = Effects.Get())
		{
			FX->SpawnTilePuff(Slot.Position, MissileMuzzlePuffClass);
		}
	}
}

void USimCopterApachePoolComponent::RebuildBulletMesh()
{
	if (BulletMesh == nullptr)
	{
		return;
	}

	const APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	const APlayerCameraManager* CameraManager = PC != nullptr ? PC->PlayerCameraManager : nullptr;
	if (CameraManager == nullptr)
	{
		BulletMesh->ClearMeshSection(0);
		return;
	}

	const FVector CameraLocation = CameraManager->GetCameraLocation();
	const FRotator CameraRotation = CameraManager->GetCameraRotation();
	const FVector CameraForward = CameraRotation.Vector();
	const FVector CameraRight = FRotationMatrix(CameraRotation).GetScaledAxis(EAxis::Y);
	const FVector CameraUp = FRotationMatrix(CameraRotation).GetScaledAxis(EAxis::Z);
	const FTransform WorldToLocal = GetComponentTransform().Inverse();

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	Vertices.Reserve(BulletSlots * 3 * 4);
	Triangles.Reserve(BulletSlots * 3 * 6);

	for (int32 SlotIndex = MissileSlots; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		const FSlot& Slot = Slots[SlotIndex];
		if (!Slot.bActive || Slot.Kind != EKind::Bullet)
		{
			continue;
		}

		FLinearColor Color = Effects != nullptr
			? Effects->PaletteColor(Slot.PaletteIndex)
			: SimCopterEffectFX::FireTipBright();
		Color.A = 1.0f;
		for (int32 PointIndex = 0; PointIndex < 3; ++PointIndex)
		{
			// FUN_0048e0b0 builds the tracer from three face-0x17 points ten original units
			// apart. Each point is the original renderer's fixed 2x2-pixel square.
			const FVector Center = Slot.Position +
				Slot.Direction * (10.0f * SimCopterEffectFX::OriginalUnitToCm * PointIndex);
			const float CameraDepth = FVector::DotProduct(Center - CameraLocation, CameraForward);
			if (CameraDepth <= 1.0f)
			{
				continue;
			}
			const float HalfExtent =
				FSimCopterEffectRasterizer::GetWorldSizePerViewportPixel(CameraDepth);
			const int32 Base = Vertices.Num();
			Vertices.Append({
				WorldToLocal.TransformPosition(Center - CameraRight * HalfExtent - CameraUp * HalfExtent),
				WorldToLocal.TransformPosition(Center + CameraRight * HalfExtent - CameraUp * HalfExtent),
				WorldToLocal.TransformPosition(Center + CameraRight * HalfExtent + CameraUp * HalfExtent),
				WorldToLocal.TransformPosition(Center - CameraRight * HalfExtent + CameraUp * HalfExtent)
			});
			Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
			UVs.Append({ FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(0, 0) });
			const FVector LocalNormal = WorldToLocal.TransformVectorNoScale(-CameraForward);
			const FProcMeshTangent Tangent(WorldToLocal.TransformVectorNoScale(CameraRight), false);
			for (int32 VertexIndex = 0; VertexIndex < 4; ++VertexIndex)
			{
				Normals.Add(LocalNormal);
				Colors.Add(Color);
				Tangents.Add(Tangent);
			}
		}
	}

	if (Vertices.IsEmpty())
	{
		BulletMesh->ClearMeshSection(0);
		return;
	}

	BulletMesh->CreateMeshSection_LinearColor(
		0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	if (BulletMaterial != nullptr)
	{
		BulletMesh->SetMaterial(0, BulletMaterial);
	}
}

bool USimCopterApachePoolComponent::ResolveImpact(
	FSlot& Slot,
	const FVector& Start,
	const FVector& End)
{
	UWorld* World = GetWorld();
	if (World == nullptr || Start.Equals(End))
	{
		return false;
	}

	ASimCopterHelicopterPawn* Helicopter = GetHelicopter();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterApacheSweep), false, Helicopter);
	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, Start, End, ECC_Visibility, QueryParams);

	for (const FHitResult& Hit : Hits)
	{
		if (!Hit.bBlockingHit)
		{
			continue;
		}

		// Both weapons are in FUN_00490690's 0x4006 despawn set, so the first thing either of
		// them touches is the last: no bouncing, no passing through. The original reaction stays
		// attached to that exact actor, while the remake's new mission effect uses the round impact
		// area represented by the explosion/strike visual.
		ASimCopterGroundAgent* Agent = Cast<ASimCopterGroundAgent>(Hit.GetActor());
		const bool bDirectAgentConverted =
			ApplyPlayerCausedMissionBlast(Slot, Hit.ImpactPoint, Agent);
		if (Agent != nullptr)
		{
			const bool bMissile = Slot.Kind == EKind::Missile;
			if (!bDirectAgentConverted)
			{
				FSimCopterInteractionEvent Event;
				// Both map to BHAV 915 "Rxn: Missile/bullet" - mode 3 for the missile's
				// 0x802 and mode 7 for a bullet's 0x004.
				Event.Mode = bMissile
					? ESimCopterInteractionMode::Missile
					: ESimCopterInteractionMode::MachineGun;
				Event.Source = Helicopter;
				Event.TargetWorldLocation = Hit.ImpactPoint;
				Agent->ApplyInteraction(Event);
			}
			Detonate(Slot, Hit.ImpactPoint, Hit.ImpactNormal, /*bHitTerrain=*/false);
			return true;
		}

		Detonate(Slot, Hit.ImpactPoint, Hit.ImpactNormal, /*bHitTerrain=*/true);
		return true;
	}

	return false;
}

bool USimCopterApachePoolComponent::ApplyPlayerCausedMissionBlast(
	const FSlot& Slot,
	const FVector& ImpactWorld,
	ASimCopterGroundAgent* DirectHitAgent)
{
	UWorld* World = GetWorld();
	ASimCopterMissionSystemActor* Missions = World != nullptr
		? Cast<ASimCopterMissionSystemActor>(UGameplayStatics::GetActorOfClass(
			World, ASimCopterMissionSystemActor::StaticClass()))
		: nullptr;
	if (World == nullptr || Missions == nullptr)
	{
		return false;
	}

	const bool bMissile = Slot.Kind == EKind::Missile;
	const float RadiusCm = FMath::Max(
		0.0f,
		bMissile ? MissileMissionBlastRadiusCm : MachineGunMissionBlastRadiusCm);
	TArray<ASimCopterGroundAgent*> Agents;
	if (RadiusCm > 0.0f)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionObjectQueryParams ObjectParams;
		ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
		FCollisionQueryParams QueryParams(
			SCENE_QUERY_STAT(SimCopterApacheMissionBlast), false, GetHelicopter());
		World->OverlapMultiByObjectType(
			Overlaps,
			ImpactWorld,
			FQuat::Identity,
			ObjectParams,
			FCollisionShape::MakeSphere(RadiusCm),
			QueryParams);

		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (ASimCopterGroundAgent* Agent = Cast<ASimCopterGroundAgent>(Overlap.GetActor()))
			{
				Agents.AddUnique(Agent);
			}
		}
	}

	// Keep the old exact-hit behavior even if a custom radius is zero or an actor's collision
	// capsule is temporarily disabled. The sphere only broadens the mission selection.
	if (DirectHitAgent != nullptr)
	{
		Agents.AddUnique(DirectHitAgent);
	}

	// Overlap result order is not stable. Nearest-first makes mission allocation deterministic if
	// a dense impact exhausts the fixed event-record pool; names break equal-distance ties.
	Agents.Sort([&ImpactWorld](
		const ASimCopterGroundAgent& Left,
		const ASimCopterGroundAgent& Right)
	{
		const float LeftDistance = FVector::DistSquared(Left.GetActorLocation(), ImpactWorld);
		const float RightDistance = FVector::DistSquared(Right.GetActorLocation(), ImpactWorld);
		if (!FMath::IsNearlyEqual(LeftDistance, RightDistance))
		{
			return LeftDistance < RightDistance;
		}
		return Left.GetName() < Right.GetName();
	});

	bool bDirectAgentConverted = false;
	for (ASimCopterGroundAgent* Agent : Agents)
	{
		bool bConvertedToMission = false;
		if (Agent->GetAgentKind() == ESimCopterGroundAgentKind::Vehicle)
		{
			bConvertedToMission = Missions->CreatePlayerCausedCarFireForVehicle(Agent);
		}
		else if (Agent->IsMedevacVictim())
		{
			// State 6 never accepts reactions in FUN_004c1050. It is already the exact injured
			// person of a medevac record, so consume the impact without creating a duplicate.
			bConvertedToMission = true;
		}
		else if (Agent->PrepareForPlayerCausedMedevac())
		{
			bConvertedToMission = Missions->CreatePlayerCausedMedevacForVictim(Agent);
		}

		if (Agent == DirectHitAgent)
		{
			bDirectAgentConverted = bConvertedToMission;
		}
	}
	return bDirectAgentConverted;
}

void USimCopterApachePoolComponent::Detonate(
	const FSlot& Slot,
	const FVector& World,
	const FVector& Normal,
	const bool bHitTerrain)
{
	USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this);
	USimCopterParticleFXComponent* FX = Effects.Get();

	if (Slot.Kind == EKind::Bullet)
	{
		// FUN_00490690's 0x4004 arm: FIREDMG against scenery, PUNCH3 into a body, and the small
		// 0x80000001 column.
		if (Audio != nullptr)
		{
			Audio->Play3D(bHitTerrain ? SimCopterSound::SND_FIREDMG : SimCopterSound::SND_PUNCH3, World);
		}
		if (FX != nullptr)
		{
			FX->SpawnSplashColumn(World, BulletImpactColumnScale, 0xFF, INDEX_NONE, INDEX_NONE,
				/*bSubmergeOrigin=*/false);
		}
		return;
	}

	// The missile. Sound 7 either way, and a bigger column on the ground than in the air.
	if (Audio != nullptr)
	{
		Audio->Play3D(SimCopterSound::SND_BOOM1, World);
	}

	ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass()));
	ASimCity2000CityActor* City = ResolveCityActor();

	bool bStartedFire = false;
	if (bHitTerrain && Missions != nullptr && City != nullptr)
	{
		float SurfaceZ = 0.0f;
		uint8 TerrainClass = 0xff;
		FIntPoint Tile(INDEX_NONE, INDEX_NONE);
		if (City->TryGetWaterGameplaySurface(World, SurfaceZ, TerrainClass, &Tile) &&
			Missions->CanIgniteCrashSite(Tile.X, Tile.Y))
		{
			// FUN_00490690's missile-into-terrain arm: FUN_004a5f60 says the tile will burn, so
			// the shot opens a fire the same way a crashing plane does.
			bStartedFire =
				Missions->CreateMissionAt(Tile.X, Tile.Y, SimCopterMissions::TYPE_BuildingFire) !=
					INDEX_NONE;
		}
	}

	if (FX != nullptr)
	{
		FX->SpawnSplashColumn(
			World,
			bHitTerrain ? MissileTerrainColumnScale : MissileImpactColumnScale,
			0xFF,
			INDEX_NONE,
			INDEX_NONE,
			/*bSubmergeOrigin=*/false);

		// `3 + rand % objectHeight` type-4 debris emitters in random directions. The original
		// scales the count by what it hit; the remake has no per-object height here, so it takes
		// the same floor and a comparable spread.
		const int32 DebrisCount = MissileDebrisMinimum + FMath::Rand() % MissileDebrisRandomRange;
		for (int32 Index = 0; Index < DebrisCount; ++Index)
		{
			const float Yaw = FMath::FRandRange(0.0f, 359.9f);
			const float Pitch = FMath::FRandRange(20.0f, 80.0f);
			const FVector Scatter =
				FRotator(Pitch, Yaw, 0.0f).RotateVector(FVector::ForwardVector) +
				Normal * 0.35f;
			FX->SpawnEffect(
				ESimCopterEffectType::Debris,
				World,
				Scatter.GetSafeNormal() * MissileDebrisSpeedCmPerSec);
		}
	}

	if (bStartedFire && Audio != nullptr)
	{
		// A structure going up gets its own report on top of the detonation.
		Audio->Play3D(SimCopterSound::SND_BLDEXPL, World);
	}
}
