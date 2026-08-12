// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterTearGasPool.h"

#include "Audio/SimCopterAudioSubsystem.h"
#include "Audio/SimCopterSoundTable.h"
#include "City/SimCity2000CityActor.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Flight/SimCopterWaterGameplay.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisProceduralMeshBuilder.h"
#include "Ground/SimCopterEffectFX.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterInteraction.h"
#include "Ground/SimCopterParticleFX.h"
#include "Kismet/GameplayStatics.h"
#include "Missions/SimCopterRiotLog.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterTearGas, Log, All);

namespace
{
// The GEO packs are authored in the same units the helicopter and the city use.
constexpr float CanisterUnitsPerCentimeter = 2621.44f;
// FUN_0048e0b0 hands FUN_004704d1 a uniform scale of size << 16, and FUN_00484d20 launches
// tear gas with size 1, so the canister is drawn at its authored size.
constexpr float CanisterModelScale = 0.25f;
constexpr bool bCanisterRenderBackfaces = true;

// The original's per-frame clock. The drag term in FUN_0048ed00 is a per-*frame* multiplier, so
// a canister only carries the original's distance when the pool is stepped on this cadence -
// the same trap the water particles hit (SimCopterWaterGameplay::SimulationStep1616).
constexpr int32 SimulationStep1616 = SimCopterWaterGameplay::SimulationStep1616;

// A whole tick of accumulated time is dropped rather than replayed after a hitch, so a stalled
// frame cannot teleport every canister across the map.
constexpr int32 MaxStepsPerTick = 8;
constexpr uint32 TearGasRuntimeSaveMagic = 0x54474153; // 'TGAS'
constexpr int32 TearGasRuntimeSaveVersion = 1;

void SerializeTearGasBool(FArchive& Archive, bool& Value)
{
	uint8 Byte = Value ? 1 : 0;
	Archive << Byte;
	if (Archive.IsLoading()) Value = Byte != 0;
}

FVector Fixed1616ToCm(const FIntVector& Value1616)
{
	return FVector(
		static_cast<float>(Value1616.X) * SimCopterEffectFX::Fixed1616ToCm,
		static_cast<float>(Value1616.Y) * SimCopterEffectFX::Fixed1616ToCm,
		static_cast<float>(Value1616.Z) * SimCopterEffectFX::Fixed1616ToCm);
}
}

USimCopterTearGasPoolComponent::USimCopterTearGasPoolComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	Slots.SetNum(SimCopterTearGas::PoolSlots);
	CanisterMeshes.SetNum(SimCopterTearGas::PoolSlots);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ModelMaterialFinder(
		TEXT("/Game/Materials/M_SimCopterLitVertexColor.M_SimCopterLitVertexColor"));
	if (ModelMaterialFinder.Succeeded())
	{
		VertexColorMaterial = ModelMaterialFinder.Object;
	}
}

void USimCopterTearGasPoolComponent::OnRegister()
{
	Super::OnRegister();
	Slots.SetNum(SimCopterTearGas::PoolSlots);
	CanisterMeshes.SetNum(SimCopterTearGas::PoolSlots);
}

void USimCopterTearGasPoolComponent::SetEffectComponent(USimCopterParticleFXComponent* InEffects)
{
	Effects = InEffects;
}

ASimCopterHelicopterPawn* USimCopterTearGasPoolComponent::GetHelicopter() const
{
	return Cast<ASimCopterHelicopterPawn>(GetOwner());
}

ASimCity2000CityActor* USimCopterTearGasPoolComponent::ResolveCityActor()
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

UProceduralMeshComponent* USimCopterTearGasPoolComponent::EnsureCanisterMesh(const int32 SlotIndex)
{
	if (!CanisterMeshes.IsValidIndex(SlotIndex))
	{
		return nullptr;
	}
	if (UProceduralMeshComponent* Existing = CanisterMeshes[SlotIndex])
	{
		return Existing;
	}
	// TEARGAS, GEO id 0x147 in SIM3D2.MAX - the object FUN_0048db20 loads onto every slot of the
	// DAT_005d4bd0 pool. Built once and kept: loading the GEO packs walks every .MAX file, and
	// doing that per slot would stall the frame each of the first ten shots.
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
			SimCopterHelicopterObjects::TearGasCanister, &ColorMap)) == nullptr)
		{
			LastMeshError = FString::Printf(
				TEXT("Could not find TEARGAS (GEO id 0x%03x) in '%s'."),
				SimCopterHelicopterObjects::TearGasCanister,
				*OriginalGameRoot);
		}
		else
		{
			// The palette is baked into the vertex colours here, so nothing outlives the library.
			FMaxisProceduralMeshBuilder::BuildPaletteColoredSection(
				*Object,
				ColorMap,
				CanisterUnitsPerCentimeter,
				CanisterModelScale,
				bCanisterRenderBackfaces,
				FLinearColor(0.55f, 0.58f, 0.52f),
				CanisterSection);
			if (CanisterSection.IsEmpty())
			{
				LastMeshError = TEXT("TEARGAS built no triangles.");
			}
		}

		if (!LastMeshError.IsEmpty())
		{
			// The pool still runs without a model - the gas is what matters - so say so once
			// rather than leaving someone wondering why the canister is invisible.
			UE_LOG(LogSimCopterTearGas, Warning,
				TEXT("Tear gas canister mesh unavailable, firing without one: %s"), *LastMeshError);
		}
	}

	if (CanisterSection.IsEmpty())
	{
		return nullptr;
	}
	const FMaxisMeshSection& Section = CanisterSection;

	// One mesh per slot, created on demand: ten of them only ever exist if ten canisters are in
	// the air at once, which needs a ten-round magazine emptied inside five seconds.
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
	Mesh->SetupAttachment(this);
	Mesh->RegisterComponent();
	// World-space: the canister must not ride along with the helicopter that threw it.
	Mesh->SetUsingAbsoluteLocation(true);
	Mesh->SetUsingAbsoluteRotation(true);
	Mesh->SetUsingAbsoluteScale(true);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);
	Mesh->bUseAsyncCooking = false;
	Mesh->CreateMeshSection_LinearColor(
		0,
		Section.Vertices,
		Section.Triangles,
		Section.Normals,
		Section.UVs,
		Section.VertexColors,
		Section.Tangents,
		false);
	if (VertexColorMaterial != nullptr)
	{
		Mesh->SetMaterial(0, VertexColorMaterial);
	}
	Mesh->SetVisibility(false);

	CanisterMeshes[SlotIndex] = Mesh;
	return Mesh;
}

bool USimCopterTearGasPoolComponent::Launch(
	const FVector& LaunchWorldLocation,
	const FVector& Direction,
	const int32 ForwardSpeed1616,
	const int32 MissionEventId)
{
	// FUN_0048e0b0 walks the pool for the first slot whose active bit is clear and gives up when
	// all ten are busy; the shot is refused outright rather than recycling the oldest.
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		FSlot& Slot = Slots[Index];
		if (Slot.bActive)
		{
			continue;
		}

		Slot.bActive = true;
		Slot.State = SimCopterTearGas::MakeLaunchState(Direction, ForwardSpeed1616);
		Slot.Position = LaunchWorldLocation;
		Slot.MissionEventId = MissionEventId;
		Slot.SpinDegrees = FMath::FRandRange(0.0f, 360.0f);

		if (UProceduralMeshComponent* Mesh = EnsureCanisterMesh(Index))
		{
			Mesh->SetWorldLocation(Slot.Position);
			Mesh->SetVisibility(true);
		}
		return true;
	}

	return false;
}

int32 USimCopterTearGasPoolComponent::GetActiveCanisterCount() const
{
	int32 Count = 0;
	for (const FSlot& Slot : Slots)
	{
		Count += Slot.bActive && !Slot.State.bDetonated ? 1 : 0;
	}
	return Count;
}

int32 USimCopterTearGasPoolComponent::GetActiveCloudCount() const
{
	int32 Count = 0;
	for (const FSlot& Slot : Slots)
	{
		Count += Slot.bActive && Slot.State.bDetonated ? 1 : 0;
	}
	return Count;
}

bool USimCopterTearGasPoolComponent::CaptureRuntimeSaveState(TArray<uint8>& OutData) const
{
	OutData.Reset();
	FMemoryWriter Writer(OutData, true);
	uint32 Magic = TearGasRuntimeSaveMagic;
	int32 Version = TearGasRuntimeSaveVersion;
	int32 SavedStepAccumulator = StepAccumulator1616;
	int32 SlotCount = Slots.Num();
	Writer << Magic << Version << SavedStepAccumulator << SlotCount;
	for (const FSlot& ConstSlot : Slots)
	{
		FSlot Slot = ConstSlot;
		SerializeTearGasBool(Writer, Slot.bActive);
		Writer << Slot.State.Direction1616 << Slot.State.Speed1616;
		Writer << Slot.State.Life1616 << Slot.State.EffectTimer1616;
		SerializeTearGasBool(Writer, Slot.State.bDetonated);
		Writer << Slot.Position << Slot.MissionEventId << Slot.SpinDegrees;
	}
	if (Writer.IsError()) OutData.Reset();
	return !Writer.IsError();
}

bool USimCopterTearGasPoolComponent::RestoreRuntimeSaveState(const TArray<uint8>& Data)
{
	if (Data.IsEmpty()) return false;
	FMemoryReader Reader(Data, true);
	uint32 Magic = 0;
	int32 Version = 0;
	int32 SlotCount = 0;
	Reader << Magic << Version << StepAccumulator1616 << SlotCount;
	if (Magic != TearGasRuntimeSaveMagic || Version != TearGasRuntimeSaveVersion ||
		SlotCount != SimCopterTearGas::PoolSlots || SlotCount != Slots.Num())
	{
		return false;
	}

	ClearAll();
	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		FSlot& Slot = Slots[Index];
		SerializeTearGasBool(Reader, Slot.bActive);
		Reader << Slot.State.Direction1616 << Slot.State.Speed1616;
		Reader << Slot.State.Life1616 << Slot.State.EffectTimer1616;
		SerializeTearGasBool(Reader, Slot.State.bDetonated);
		Reader << Slot.Position << Slot.MissionEventId << Slot.SpinDegrees;
		if (Slot.bActive && !Slot.State.bDetonated)
		{
			if (UProceduralMeshComponent* Mesh = EnsureCanisterMesh(Index))
			{
				Mesh->SetWorldLocation(Slot.Position);
				const FVector Travel = SimCopterWaterGameplay::DirectionToFloat(Slot.State.Direction1616);
				Mesh->SetWorldRotation(FRotator(Slot.SpinDegrees, Travel.Rotation().Yaw, 0.0f));
				Mesh->SetVisibility(true);
			}
		}
	}
	return !Reader.IsError() && Reader.Tell() == Reader.TotalSize();
}

void USimCopterTearGasPoolComponent::ClearAll()
{
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		ReleaseSlot(Index);
	}
}

void USimCopterTearGasPoolComponent::ReleaseSlot(const int32 SlotIndex)
{
	if (!Slots.IsValidIndex(SlotIndex))
	{
		return;
	}
	Slots[SlotIndex].bActive = false;
	if (CanisterMeshes.IsValidIndex(SlotIndex) && CanisterMeshes[SlotIndex] != nullptr)
	{
		CanisterMeshes[SlotIndex]->SetVisibility(false);
	}
}

void USimCopterTearGasPoolComponent::TickComponent(
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
}

void USimCopterTearGasPoolComponent::AdvanceSlot(const int32 SlotIndex, const int32 Delta1616)
{
	FSlot& Slot = Slots[SlotIndex];

	const SimCopterTearGas::FCanisterFrame Frame =
		SimCopterTearGas::AdvanceCanisterFrame(Slot.State, Delta1616);

	USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this);
	if (Frame.bDetonatedThisFrame)
	{
		// FUN_0048ed00 plays TGPOP at the canister, not at the helicopter: the pop is where the
		// gas is, which is how the player hears a canister that went further than they meant.
		if (Audio != nullptr)
		{
			Audio->Play3D(SimCopterSound::SND_TGPOP, Slot.Position);
		}
		SIMCOPTER_RIOT_LOG(TEXT("TEARGAS canister burst; the 30 s cloud starts now."));
		// A burst canister is a cloud, not an object; the model goes away with the pop.
		if (CanisterMeshes.IsValidIndex(SlotIndex) && CanisterMeshes[SlotIndex] != nullptr)
		{
			CanisterMeshes[SlotIndex]->SetVisibility(false);
		}
	}

	if (!Frame.bAlive)
	{
		ReleaseSlot(SlotIndex);
		return;
	}

	const FVector Start = Slot.Position;
	const FVector End = Start + Fixed1616ToCm(Frame.Travel1616);

	FVector Resolved = End;
	if (ResolveImpact(Slot, Start, End, Resolved))
	{
		Slot.Position = Resolved;
		ReleaseSlot(SlotIndex);
		return;
	}
	Slot.Position = Resolved;

	// FUN_0048ed00 finishes the step by lifting the canister back to the terrain whenever the
	// integration has put it underground (FUN_004aea90), so it never sinks out of sight.
	if (ASimCity2000CityActor* City = ResolveCityActor())
	{
		float SurfaceZ = 0.0f;
		uint8 TerrainClass = 0xff;
		if (City->TryGetWaterGameplaySurface(Slot.Position, SurfaceZ, TerrainClass) &&
			Slot.Position.Z < SurfaceZ)
		{
			Slot.Position.Z = SurfaceZ;
		}
	}

	USimCopterParticleFXComponent* FX = Effects.Get();
	if (Frame.bEmitTrail && FX != nullptr)
	{
		// Kind 4: the same card the debris trail uses, at the canister itself.
		FX->SpawnTilePuff(Slot.Position, SimCopterTearGas::TrailPuffClass);
	}
	if (Frame.bEmitCloudPuff)
	{
		EmitCloudPuff(Slot);
	}

	if (CanisterMeshes.IsValidIndex(SlotIndex) && CanisterMeshes[SlotIndex] != nullptr &&
		!Slot.State.bDetonated)
	{
		UProceduralMeshComponent* Mesh = CanisterMeshes[SlotIndex];
		Mesh->SetWorldLocation(Slot.Position);
		// Tumble. The original never rotates the canister's node at all, but a static box sliding
		// through the air reads as a bug at this resolution, so the port spins it about the axis
		// of travel and leaves everything gameplay touches alone.
		Slot.SpinDegrees = FMath::Fmod(Slot.SpinDegrees + 22.0f, 360.0f);
		const FVector Travel = SimCopterWaterGameplay::DirectionToFloat(Slot.State.Direction1616);
		Mesh->SetWorldRotation(FRotator(Slot.SpinDegrees, Travel.Rotation().Yaw, 0.0f));
	}
}

bool USimCopterTearGasPoolComponent::ResolveImpact(
	FSlot& Slot,
	const FVector& Start,
	const FVector& End,
	FVector& OutPosition)
{
	OutPosition = End;

	UWorld* World = GetWorld();
	if (World == nullptr || Start.Equals(End))
	{
		return false;
	}

	ASimCopterHelicopterPawn* Helicopter = GetHelicopter();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterTearGasSweep), false, Helicopter);
	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, Start, End, ECC_Visibility, QueryParams);

	const FHitResult* Surface = nullptr;
	for (const FHitResult& Hit : Hits)
	{
		if (!Hit.bBlockingHit)
		{
			continue;
		}
		if (ASimCopterGroundAgent* Agent = Cast<ASimCopterGroundAgent>(Hit.GetActor()))
		{
			// FUN_00490690's object loop: the canister's class flag 0x8 is not in the 0x4006
			// despawn set, so a body it strikes takes the debris reaction (mode 0xe, BHAV 910)
			// and the canister carries on through.
			FSimCopterInteractionEvent Event;
			Event.Mode = ESimCopterInteractionMode::TearGasCanister;
			Event.Source = Helicopter;
			Event.TargetWorldLocation = Hit.ImpactPoint;
			Event.MissionEventId = Slot.MissionEventId;
			Agent->ApplyInteraction(Event);
			continue;
		}
		Surface = &Hit;
		break;
	}

	if (Surface == nullptr)
	{
		return false;
	}

	// The original only turns the canister around on a terrain hit while it is falling
	// (FUN_00490690's `param_1[5] < 0` gate); one on the way up is left alone so it does not
	// stick to the slope it has just left, and it keeps the whole step it was given.
	const bool bFalling = Slot.State.Direction1616.Z < 0;
	if (!bFalling && Surface->ImpactNormal.Z > 0.5f)
	{
		return false;
	}

	OutPosition = Surface->ImpactPoint;

	// A canister that lands on water or open country is swallowed: splash card, DOUSE, gone.
	// Everything else is in the 0x798 reflect set, so it bounces and keeps burning its fuse.
	if (ASimCity2000CityActor* City = ResolveCityActor())
	{
		float SurfaceZ = 0.0f;
		uint8 TerrainClass = 0xff;
		if (City->TryGetWaterGameplaySurface(OutPosition, SurfaceZ, TerrainClass) &&
			SimCopterWaterGameplay::IsWaterTerrainClass(TerrainClass))
		{
			if (USimCopterParticleFXComponent* FX = Effects.Get())
			{
				FX->SpawnTilePuff(OutPosition, SimCopterTearGas::SplashPuffClass);
			}
			if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
			{
				Audio->Play3D(SimCopterSound::SND_DOUSE, OutPosition);
			}
			return true;
		}
	}

	if (SimCopterTearGas::ApplyBounce(Slot.State, Surface->ImpactNormal))
	{
		if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
		{
			Audio->Play3D(SimCopterSound::SND_SOFTBMP2, OutPosition);
		}
	}
	// Lift it clear of the surface it just struck so the next step does not start inside it.
	OutPosition += Surface->ImpactNormal * 2.0f;
	return false;
}

void USimCopterTearGasPoolComponent::EmitCloudPuff(FSlot& Slot)
{
	// FUN_0048ed00's cloud arm: pick a whole-unit offset on the two horizontal axes, drop a
	// kind-9 card there, and gas every person standing on *that* tile - not the canister's.
	const FVector Offset(
		static_cast<float>(SimCopterTearGas::CloudOffsetAxis1616(FMath::Rand())) *
			SimCopterEffectFX::Fixed1616ToCm,
		static_cast<float>(SimCopterTearGas::CloudOffsetAxis1616(FMath::Rand())) *
			SimCopterEffectFX::Fixed1616ToCm,
		0.0f);
	const FVector PuffWorld = Slot.Position + Offset;

	if (USimCopterParticleFXComponent* FX = Effects.Get())
	{
		FX->SpawnTilePuff(PuffWorld, SimCopterTearGas::CloudPuffClass);
	}

	ASimCopterHelicopterPawn* Helicopter = GetHelicopter();
	ASimCity2000CityActor* City = ResolveCityActor();
	if (Helicopter == nullptr || City == nullptr)
	{
		return;
	}

	float SurfaceZ = 0.0f;
	uint8 TerrainClass = 0xff;
	FIntPoint Tile(INDEX_NONE, INDEX_NONE);
	if (!City->TryGetWaterGameplaySurface(PuffWorld, SurfaceZ, TerrainClass, &Tile))
	{
		return;
	}

	FSimCopterInteractionEvent Event;
	// Mode 5, not 7: BHAV 907 "Rxn: Teargas" is what calms a rioter down (its agitation falls by
	// two, or rises by five on a one-in-six roll), and BHAV 311 retires anyone whose agitation
	// drops below three - which is what actually ends a riot mission.
	Event.Mode = ESimCopterInteractionMode::TearGasCloud;
	Event.Source = Helicopter;
	Event.TargetTile = Tile;
	Event.TargetWorldLocation = FVector(PuffWorld.X, PuffWorld.Y, SurfaceZ);
	Event.MissionEventId = Slot.MissionEventId;
	const int32 Affected = Helicopter->DeliverInteractionToTile(Event);
	SIMCOPTER_RIOT_LOG(
		TEXT("TEARGAS puff on tile (%d,%d): %d person/people gassed (mode 5 -> BHAV 907)."),
		Tile.X, Tile.Y, Affected);
}
