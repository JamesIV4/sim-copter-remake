// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterGroundAgent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisProceduralMeshBuilder.h"
#include "Formats/SimCopterPeopleCityRules.h"
#include "Formats/SimCopterPeopleReader.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "GameFramework/Pawn.h"
#include "Ground/SimCopterOnFootPawn.h"
#include "Ground/SimCopterPopulationBody.h"
#include "Ground/SimCopterPopulationSprite.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterGroundAgent, Log, All);

namespace
{
// The city (and its cars/helicopters) is rendered at 0.25x of real-cm so a 400cm remake tile
// stands in for the original 1600-unit tile. The population capsules and bodies were authored in
// real cm, which made pedestrians (and the on-foot player) read ~4x too tall next to the shrunk
// world. Everything visual/collision below is multiplied by this to match.
constexpr float PopulationWorldScale = 0.25f;

constexpr float VehicleCapsuleRadiusCm = 135.0f;
constexpr float VehicleCapsuleHalfHeightCm = 82.0f;
constexpr float PedestrianCapsuleRadiusCm = 32.0f;
constexpr float PedestrianCapsuleHalfHeightCm = 88.0f;
constexpr float TargetStopDistanceCm = 75.0f;
constexpr float VehicleFallbackZCm = 52.0f;
constexpr float PedestrianFallbackZCm = 88.0f;
constexpr float PedestrianSpriteHeightCm = 162.0f;
constexpr float PedestrianBodyHeightCm = 176.0f;
constexpr const TCHAR* SpriteMaterialPath = TEXT("/Game/Materials/M_SimCopterSpriteTexture.M_SimCopterSpriteTexture");

uint16 GetPlayerBehaviorSpeedScalar(const APawn& Pawn)
{
	FVector Velocity = Pawn.GetVelocity();
	float ReferenceSpeed = 1.0f;
	if (const ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(&Pawn))
	{
		Velocity = Helicopter->GetVelocityCmPerSec();
		ReferenceSpeed = FMath::Max(1.0f, Helicopter->GetMaxForwardSpeedCmPerSec());
	}
	else if (const ASimCopterOnFootPawn* OnFoot = Cast<ASimCopterOnFootPawn>(&Pawn))
	{
		Velocity = OnFoot->GetCurrentVelocityCmPerSec();
		ReferenceSpeed = FMath::Max(1.0f, OnFoot->GetWalkSpeedCmPerSec());
	}

	return uint16(FMath::Clamp(FMath::RoundToInt((Velocity.Size2D() / ReferenceSpeed) * 10.0f), 0, 10));
}

// Process-wide people.df behavior model cache (one per original-game root).
TSharedPtr<FPeopleBehaviorModel> GetSharedBehaviorModel(const FString& RootPath)
{
	static TMap<FString, TSharedPtr<FPeopleBehaviorModel>> Cache;
	static TSet<FString> FailedRoots;
	const FString Key = FPaths::ConvertRelativePathToFull(RootPath);
	if (const TSharedPtr<FPeopleBehaviorModel>* Found = Cache.Find(Key))
	{
		return *Found;
	}
	if (FailedRoots.Contains(Key))
	{
		return nullptr;
	}
	const FString PeoplePath = FSimCopterPeopleReader::ResolvePeoplePath(RootPath);
	TSharedPtr<FPeopleBehaviorModel> Model = MakeShared<FPeopleBehaviorModel>();
	FString Error;
	if (PeoplePath.IsEmpty() || !FSimCopterPeopleReader::LoadFromFile(PeoplePath, *Model, Error))
	{
		FailedRoots.Add(Key);
		return nullptr;
	}
	Cache.Add(Key, Model);
	return Model;
}

UMaterialInterface* LoadSpriteMaterialNoWarn()
{
	return Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, SpriteMaterialPath, nullptr, LOAD_NoWarn));
}
}

ASimCopterGroundAgent::ASimCopterGroundAgent()
{
	PrimaryActorTick.bCanEverTick = true;

	CollisionComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionComponent"));
	CollisionComponent->InitCapsuleSize(PedestrianCapsuleRadiusCm, PedestrianCapsuleHalfHeightCm);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComponent->SetCollisionObjectType(ECC_Pawn);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CollisionComponent->SetCanEverAffectNavigation(false);
	SetRootComponent(CollisionComponent);

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(CollisionComponent);

	OriginalMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("OriginalMesh"));
	OriginalMeshComponent->SetupAttachment(VisualRoot);
	OriginalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OriginalMeshComponent->SetCanEverAffectNavigation(false);
	OriginalMeshComponent->SetVisibility(false);

	ProxyMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProxyMesh"));
	ProxyMeshComponent->SetupAttachment(VisualRoot);
	ProxyMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProxyMeshComponent->SetCanEverAffectNavigation(false);

	// Headlights are cheap dynamic spotlights (no shadows) - there can be many cars on screen.
	HeadlightLeft = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadlightLeft"));
	HeadlightLeft->SetupAttachment(VisualRoot);
	HeadlightLeft->CastShadows = false;
	HeadlightLeft->SetVisibility(false);

	HeadlightRight = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadlightRight"));
	HeadlightRight->SetupAttachment(VisualRoot);
	HeadlightRight->CastShadows = false;
	HeadlightRight->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		ProxyMeshComponent->SetStaticMesh(CubeMeshFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ModelMaterialFinder(TEXT("/Game/Materials/M_SimCopterLitVertexColor.M_SimCopterLitVertexColor"));
	if (ModelMaterialFinder.Succeeded())
	{
		VertexColorMaterial = ModelMaterialFinder.Object;
	}

	SpriteMaterial = LoadSpriteMaterialNoWarn();

	OriginalGameRoot.Path = TEXT("../Reference/SimCopterOriginalGame");
	AnimationPhase = FMath::FRandRange(0.0f, UE_TWO_PI);

	// Everyday street mix (service figures and easter eggs are opt-in via PedestrianFigureName).
	PedestrianFigurePool = {
		TEXT("fatman"), TEXT("2blonde"), TEXT("Child"), TEXT("5.5man"), TEXT("SUIT"),
		TEXT("5man"), TEXT("SHADES"), TEXT("Blonde"), TEXT("2woman"), TEXT("Woman")};

	ApplyAgentShape();
}

void ASimCopterGroundAgent::BeginPlay()
{
	Super::BeginPlay();

	ApplyAgentShape();
	if (AgentKind == ESimCopterGroundAgentKind::Pedestrian)
	{
		BuildPedestrianBody();
		StartOriginalBehavior();
	}
	else if (!MeshTableName.IsEmpty())
	{
		LoadOriginalMeshFromOriginalGameRoot();
	}
}

void ASimCopterGroundAgent::StartOriginalBehavior()
{
	bBehaviorActive = false;
	if (!bUseOriginalBehaviors || AgentKind != ESimCopterGroundAgentKind::Pedestrian)
	{
		return;
	}
	BehaviorModel = GetSharedBehaviorModel(ResolveOriginalGameRoot());
	if (!BehaviorModel.IsValid())
	{
		return;
	}
	BehaviorContext = FSimCopterPersonContext();
	// Seed the people PRNG per agent so crowds don't move in lockstep.
	BehaviorContext.Lfsr = uint16(GetTypeHash(GetFName()) | 1);
	BehaviorContext.Attributes[EBhavAttr::Facing] = uint16(FMath::RoundToInt(GetActorRotation().Yaw / 45.0f) & 7);
	BehaviorContext.Attributes[EBhavAttr::BehaviorClass] = uint16(FMath::Clamp(InitialBehaviorClass, 0, 21));
	// FUN_004c7090 enables the move core's clockwise retry loop (+0x16a) for every spawned
	// person; the move speed (+0x164) starts 0 and is assigned by the shipped programs
	// ("movespeed := 6/8/12/16/25" expressions in people.df).
	BehaviorContext.Attributes[EBhavAttr::AutoTurn] = 1;
	BehaviorContext.ResetToState(InitialPersonState);
	LastAppliedBehaviorFacing = INDEX_NONE;
	BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepTimeRemainingSeconds = 0.0f;
	bBehaviorActive = true;
}

void ASimCopterGroundAgent::UpdateOriginalBehavior(float DeltaSeconds)
{
	// The people VM only ever drives pedestrians; vehicles keep the road-route movement.
	if (!bBehaviorActive || !BehaviorModel.IsValid() || AgentKind != ESimCopterGroundAgentKind::Pedestrian)
	{
		return;
	}

	BehaviorTickAccumulator += DeltaSeconds * BehaviorTickRate;
	int32 Steps = FMath::FloorToInt(BehaviorTickAccumulator);
	if (Steps <= 0)
	{
		return;
	}
	BehaviorTickAccumulator -= float(Steps);
	Steps = FMath::Min(Steps, 4); // don't burst after hitches

	for (int32 Step = 0; Step < Steps; ++Step)
	{
		const EBhavStepResult Result = FSimCopterBehaviorVM::Tick(BehaviorContext, *BehaviorModel, *this);
		if (Result == EBhavStepResult::Stopped || BehaviorContext.bRequestDespawn)
		{
			// Original 'Disappear'/deactivate path: hide and let the spawner recycle us.
			bBehaviorActive = false;
			SetActorHiddenInGame(true);
			SetLifeSpan(1.0f);
			return;
		}
		if (Result == EBhavStepResult::Failed)
		{
			bBehaviorActive = false;
			return;
		}
	}

	// Anim binds map straight onto the figure clips (same 4-char mnemonics as ARLU).
	if (!BehaviorContext.PendingAnimMnemonic.IsEmpty())
	{
		if (bUsingPedestrianFigure && BehaviorContext.PendingAnimMnemonic != FigureMnemonic)
		{
			RebuildFigureClip(BehaviorContext.PendingAnimMnemonic);
		}
		BehaviorContext.PendingAnimMnemonic.Reset();
	}

	// The original driver (FUN_004c6450) advances the bound clip one frame per behavior tick,
	// wrapping at the clip's ARPP row count - playback rate is the tick rate, not wall time.
	AdvanceBehaviorFigureFrames(Steps);

	// The figure renders at the person's stored octant facing; op29/scatter turns must show
	// even when standing, so the yaw comes from the attribute, not from velocity.
	ApplyBehaviorFacingRotation();
}

void ASimCopterGroundAgent::ApplyBehaviorFacingRotation()
{
	const int32 Facing = int32(BehaviorContext.Attributes[EBhavAttr::Facing]) & 7;
	if (Facing == LastAppliedBehaviorFacing)
	{
		return;
	}
	if (const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner()))
	{
		const FVector WorldDirection = TrafficSystem->GetPeopleFacingWorldDirection(Facing);
		if (!WorldDirection.IsNearlyZero())
		{
			// The original snaps to the 45-degree heading instantly - no turn interpolation.
			SetActorRotation(FRotator(0.0f, WorldDirection.Rotation().Yaw, 0.0f));
			LastAppliedBehaviorFacing = Facing;
		}
	}
}

void ASimCopterGroundAgent::AdvanceBehaviorFigureFrames(int32 TickCount)
{
	if (!bUsingPedestrianFigure || FigureFrameCount <= 1 || TickCount <= 0)
	{
		return;
	}
	FigureCurrentFrame = (FigureCurrentFrame + TickCount) % FigureFrameCount;
	FSimCopterPopulationFigure::ShowFrame(OriginalMeshComponent, FigureFrameCount, FigureCurrentFrame, bFigureHasHeadSection);
}

int32 ASimCopterGroundAgent::GetCurrentTileClass() const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner()))
	{
		const int32 TileClass = TrafficSystem->GetPeopleTileClassAtWorldLocation(GetActorLocation());
		if (TileClass != INDEX_NONE)
		{
			return TileClass;
		}
	}

	// Standalone/unit-test fallback when no traffic system owns the agent.
	return RouteTargetNodeIndex != INDEX_NONE ? 7 : 10;
}

bool ASimCopterGroundAgent::TryGetCurrentTileCoordinate(int32& OutFileX, int32& OutFileY) const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner()))
	{
		return TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(GetActorLocation(), OutFileX, OutFileY);
	}

	OutFileX = INDEX_NONE;
	OutFileY = INDEX_NONE;
	return false;
}

bool ASimCopterGroundAgent::IsTileClassAllowedForState(int32 StateIndex, int32 TileClass) const
{
	return FSimCopterBehaviorVM::GetAllowedTileClasses(StateIndex).Contains(TileClass);
}

void ASimCopterGroundAgent::SetInitialBehaviorClass(int32 NewInitialBehaviorClass)
{
	InitialBehaviorClass = FMath::Clamp(NewInitialBehaviorClass, 0, 21);
	// The original binds the figure from the behavior class at spawn (FUN_004c71c0): dogs,
	// cows, Elvis and the everyday street mix all come from this one table.
	PedestrianFigureName = FSimCopterPeopleCityRules::GetFigureNameForBehaviorClass(InitialBehaviorClass);
	if (bBehaviorActive)
	{
		BehaviorContext.Attributes[EBhavAttr::BehaviorClass] = uint16(InitialBehaviorClass);
	}
}

bool ASimCopterGroundAgent::MoveStep(FSimCopterPersonContext& Context)
{
	// FUN_004c6970: every move tick rebinds the clip from the move result and speed -
	// 0 = NoMo, 1..6 = 1Wal, 7+ = 1Run (dogs/cows remap to DgRn/DgSt in the clip bind).
	// The magnitude comes from the MoveSpeed attribute (+0x164), which the shipped programs
	// assign directly; one tick displaces octantDir * MoveSpeed / 12 original units.
	const int32 MoveSpeed = FMath::Max(0, int32(int16(Context.Attributes[EBhavAttr::MoveSpeed])));
	const TCHAR* SpeedMnemonic = MoveSpeed <= 0 ? TEXT("NoMo") : (MoveSpeed < 7 ? TEXT("1Wal") : TEXT("1Run"));

	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	if (TrafficSystem == nullptr)
	{
		// Standalone/unit-test agents have no city to validate against: accept the move.
		Context.PendingAnimMnemonic = SpeedMnemonic;
		return true;
	}

	if (MoveSpeed <= 0)
	{
		// A zero-speed move succeeds without displacement (result 0, speed 0 -> NoMo).
		Context.PendingAnimMnemonic = SpeedMnemonic;
		BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
		BehaviorStepTimeRemainingSeconds = 0.0f;
		return true;
	}

	const float UnitCm = TrafficSystem->GetPeopleWorldCmPerOriginalUnit();
	const float StepDistanceCm = float(MoveSpeed) / 12.0f * UnitCm;
	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	const float CurrentFeetZ = GetActorLocation().Z - HalfHeight;
	const float MaxClimbCm = MaxStepClimbOriginalUnits * UnitCm;
	const float MaxDropCm = (MaxStepClimbOriginalUnits + 0.5f) * UnitCm; // -(0x8000 + max) in 16.16

	const int32 StartFacing = int32(Context.Attributes[EBhavAttr::Facing]) & 7;
	// FUN_004c9300 retries clockwise up to 8 extra facings only while +0x16a is set.
	const int32 MaxAttempts = Context.Attributes[EBhavAttr::AutoTurn] != 0 ? 8 : 1;
	const bool bMoveThroughWalls = Context.Attributes[EBhavAttr::MoveThroughWalls] != 0;
	const bool bAmbient = Context.Attributes[EBhavAttr::AmbientFlag] != 0;
	const int32 BehaviorClass = Context.Attributes[EBhavAttr::BehaviorClass];

	int32 LastBlockResult = 0;
	for (int32 Turn = 0; Turn < MaxAttempts; ++Turn)
	{
		const int32 Facing = (StartFacing + Turn) & 7;
		FVector TargetLocation = FVector::ZeroVector;
		int32 TargetTileClass = INDEX_NONE;
		if (!TrafficSystem->TryGetPeopleFacingStepTarget(GetActorLocation(), Facing, StepDistanceCm, TargetLocation, TargetTileClass))
		{
			LastBlockResult = 3;
			continue;
		}

		// FUN_004c9470: ambient people (+0x168) may only enter tile classes from their
		// behavior-class row (DAT_0058ec00); result 3 otherwise. Non-ambient movement keeps
		// the pre-VM rows as a safety net (missions steer via goto-object opcodes instead).
		if (bAmbient)
		{
			if (!FSimCopterPeopleCityRules::GetAmbientStateTileClasses(BehaviorClass).Contains(TargetTileClass))
			{
				LastBlockResult = 3;
				continue;
			}
		}
		else if (!IsTileClassAllowedForState(BehaviorClass, TargetTileClass) &&
			!FSimCopterPeopleCityRules::GetAmbientStateTileClasses(BehaviorClass).Contains(TargetTileClass))
		{
			LastBlockResult = 3;
			continue;
		}

		// The max-climb/drop gate against the walked surface (highest geometry at the target
		// column). This - not the tile class - is what stops people at building walls: the
		// surface there is the roof, far above the 5-unit climb allowance. BHAV 308's
		// "move through walls" flag (+0x190) bypasses it after repeated failures.
		if (!bMoveThroughWalls)
		{
			float SurfaceZ = 0.0f;
			if (TryGetWalkSurfaceZAt(TargetLocation, SurfaceZ))
			{
				const float Rise = SurfaceZ - CurrentFeetZ;
				if (Rise > MaxClimbCm)
				{
					LastBlockResult = 1; // FaCl recoil
					continue;
				}
				if (Rise < -MaxDropCm)
				{
					LastBlockResult = 2; // Whoa
					continue;
				}
			}
		}

		// Success: renew the constant step velocity until the next behavior tick (the ground
		// snap performs the original's per-step warp onto the walked surface).
		Context.Attributes[EBhavAttr::Facing] = uint16(Facing);
		Context.PendingAnimMnemonic = SpeedMnemonic;
		const FVector FlatDelta(TargetLocation.X - GetActorLocation().X, TargetLocation.Y - GetActorLocation().Y, 0.0f);
		BehaviorStepVelocityCmPerSec = FlatDelta * BehaviorTickRate;
		BehaviorStepTimeRemainingSeconds = 1.25f / FMath::Max(BehaviorTickRate, 1.0f);
		return true;
	}

	// Blocked on every allowed facing: bind the recoil clip for the final attempt's result,
	// exactly like the single post-move call after FUN_004c9300's retry loop.
	Context.PendingAnimMnemonic = LastBlockResult == 1 ? TEXT("FaCl") : (LastBlockResult == 2 ? TEXT("Whoa") : TEXT("NoMo"));
	BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
	BehaviorStepTimeRemainingSeconds = 0.0f;
	return false;
}

bool ASimCopterGroundAgent::TryGetWalkSurfaceZAt(const FVector& WorldLocation, float& OutSurfaceZ) const
{
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	if (GetWorld() == nullptr)
	{
		return false;
	}

	// FUN_004c82c0 returns the highest of object tops and terrain at the point, so the probe
	// starts far above any roof and takes the first blocking hit downward. The Camera channel
	// is blocked by city geometry but ignored by agent/player capsules.
	const float StartZ = GetActorLocation().Z + 12000.0f;
	const FVector Start(WorldLocation.X, WorldLocation.Y, StartZ);
	const FVector End(WorldLocation.X, WorldLocation.Y, GetActorLocation().Z - GroundProbeDistanceCm);
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterWalkSurface), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Camera, QueryParams) && Hit.bBlockingHit)
	{
		OutSurfaceZ = Hit.ImpactPoint.Z;
		return true;
	}

	float TerrainWorldZ = 0.0f;
	if (TrafficSystem != nullptr && TrafficSystem->TryGetTerrainWorldZAtWorldLocation(WorldLocation, TerrainWorldZ))
	{
		OutSurfaceZ = TerrainWorldZ;
		return true;
	}

	return false;
}

bool ASimCopterGroundAgent::IsThreatNearby(const FSimCopterPersonContext& Context) const
{
	// FUN_004c9bc0: the player's helicopter close and low. Original radius DAT_0058dc32 is a
	// few tiles; use ~4 remake tiles horizontally and a low-altitude gate.
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (PlayerPawn == nullptr)
	{
		return false;
	}
	const FVector Delta = PlayerPawn->GetActorLocation() - GetActorLocation();
	return FMath::Abs(Delta.Z) < 1200.0f && Delta.SizeSquared2D() < FMath::Square(1600.0f);
}

bool ASimCopterGroundAgent::TryGetPlayerTileProbe(
	const FSimCopterPersonContext& Context,
	FSimCopterBehaviorPlayerTileProbe& OutProbe) const
{
	(void)Context;
	OutProbe = FSimCopterBehaviorPlayerTileProbe();
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner());
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (TrafficSystem == nullptr || PlayerPawn == nullptr)
	{
		return false;
	}

	if (!TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(
		PlayerPawn->GetActorLocation(),
		OutProbe.FileX,
		OutProbe.FileY))
	{
		return false;
	}

	OutProbe.Speed = GetPlayerBehaviorSpeedScalar(*PlayerPawn);
	OutProbe.Facing = uint16(TrafficSystem->GetPeopleStoredFacingFromWorldLocations(GetActorLocation(), PlayerPawn->GetActorLocation()));
	return true;
}

void ASimCopterGroundAgent::OnUnknownOpcode(int32 Opcode)
{
	if (!ReportedUnknownOpcodes.Contains(Opcode))
	{
		ReportedUnknownOpcodes.Add(Opcode);
		UE_LOG(LogSimCopterGroundAgent, Verbose, TEXT("%s: BHAV opcode %d not yet ported (following false edge)."), *GetName(), Opcode);
	}
}

void ASimCopterGroundAgent::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (AvoidanceMoveTimeRemainingSeconds > 0.0f)
	{
		AvoidanceMoveTimeRemainingSeconds = FMath::Max(0.0f, AvoidanceMoveTimeRemainingSeconds - DeltaSeconds);
	}
	if (AvoidancePathOffsetTimeRemainingSeconds > 0.0f)
	{
		AvoidancePathOffsetTimeRemainingSeconds = FMath::Max(0.0f, AvoidancePathOffsetTimeRemainingSeconds - DeltaSeconds);
	}
	if (GuidanceMoveTargetTimeRemainingSeconds > 0.0f)
	{
		GuidanceMoveTargetTimeRemainingSeconds = FMath::Max(0.0f, GuidanceMoveTargetTimeRemainingSeconds - DeltaSeconds);
	}
	UpdateOriginalBehavior(DeltaSeconds);
	UpdateMovement(DeltaSeconds);
	if (bSnapToGround)
	{
		UpdateGroundSnap();
	}
	UpdateJankyAnimation(DeltaSeconds);
}

void ASimCopterGroundAgent::ConfigureAgent(
	ESimCopterGroundAgentKind NewAgentKind,
	const FString& NewMeshTableName,
	const FString& NewOriginalGameRoot,
	float NewMovementSpeedCmPerSec)
{
	AgentKind = NewAgentKind;
	MeshTableName = NewMeshTableName;
	OriginalGameRoot.Path = NewOriginalGameRoot;
	MovementSpeedCmPerSec = FMath::Max(1.0f, NewMovementSpeedCmPerSec);
	AnimationPhase = FMath::FRandRange(0.0f, UE_TWO_PI);
	// BeginPlay already ran with the default (pedestrian) kind; stop that behavior VM before
	// retyping. The pedestrian branch below restarts it cleanly.
	bBehaviorActive = false;
	BehaviorTickAccumulator = 0.0f;
	ApplyAgentShape();

	if (AgentKind == ESimCopterGroundAgentKind::Pedestrian)
	{
		BuildPedestrianBody();
		StartOriginalBehavior();
	}
	else if (!MeshTableName.IsEmpty())
	{
		LoadOriginalMeshFromOriginalGameRoot();
	}
	else
	{
		DisableVehicleHeadlights();
		ShowOriginalMesh(false);
	}
}

bool ASimCopterGroundAgent::LoadOriginalMeshFromOriginalGameRoot()
{
	if (AgentKind == ESimCopterGroundAgentKind::Pedestrian && FSimCopterPopulationSprite::IsPeople1Name(MeshTableName))
	{
		return LoadOriginalPedestrianSpriteFromOriginalGameRoot();
	}

	LastMeshLoadError.Reset();
	OriginalMeshComponent->ClearAllMeshSections();
	bUsingPedestrianSprite = false;
	// BeginPlay may have built a pedestrian figure before ConfigureAgent retyped this agent
	// (SpawnActor runs BeginPlay with the default kind); drop that state or the behavior VM
	// rebuilds person clips over the vehicle mesh.
	bUsingPedestrianBody = false;
	bUsingPedestrianFigure = false;

	const FString RootPath = ResolveOriginalGameRoot();
	if (RootPath.IsEmpty())
	{
		LastMeshLoadError = TEXT("Original game root is empty.");
		ShowOriginalMesh(false);
		return false;
	}

	FMaxisMeshLibrary MeshLibrary;
	FString Error;
	if (!MeshLibrary.LoadFromOriginalGameRoot(RootPath, Error))
	{
		LastMeshLoadError = Error;
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	const TArray<FColor>* ColorMap = nullptr;
	const FMaxisMeshObject* MeshObject = MeshLibrary.FindObjectByTableName(MeshTableName, &ColorMap);
	if (MeshObject == nullptr)
	{
		LastMeshLoadError = FString::Printf(TEXT("Could not find ground-agent mesh '%s' in '%s'."), *MeshTableName, *RootPath);
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	const float ModelScale = AgentKind == ESimCopterGroundAgentKind::Vehicle ? VehicleModelScale : PedestrianModelScale;
	const FLinearColor FallbackColor = AgentKind == ESimCopterGroundAgentKind::Vehicle
		? FLinearColor(0.33f, 0.34f, 0.35f)
		: FLinearColor(0.72f, 0.63f, 0.52f);

	// The original cars carry translucent "headlight beam" cards (Maxis face type 11) projecting
	// off the front of the body. Route those into a throwaway translucent section so they are
	// dropped from the rendered vehicle - real spotlights below take their place. (Without this
	// the single-section build renders them as opaque grey/blue blocks: the "opaque headlights".)
	FMaxisMeshSection MeshSection;
	FMaxisMeshSection DiscardedBeamSection;
	FMaxisProceduralMeshBuilder::BuildPaletteColoredSections(
		*MeshObject,
		ColorMap,
		ModelUnitsPerCentimeter,
		ModelScale,
		bRenderModelBackfaces,
		FallbackColor,
		MeshSection,
		&DiscardedBeamSection);

	if (MeshSection.IsEmpty())
	{
		LastMeshLoadError = FString::Printf(TEXT("Ground-agent mesh '%s' built no triangles."), *MeshTableName);
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	OriginalMeshComponent->CreateMeshSection_LinearColor(
		0,
		MeshSection.Vertices,
		MeshSection.Triangles,
		MeshSection.Normals,
		MeshSection.UVs,
		MeshSection.VertexColors,
		MeshSection.Tangents,
		false);

	if (VertexColorMaterial != nullptr)
	{
		OriginalMeshComponent->SetMaterial(0, VertexColorMaterial);
	}

	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	OriginalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfHeight - MeshSection.LocalBounds.Min.Z));
	ShowOriginalMesh(true);

	if (AgentKind == ESimCopterGroundAgentKind::Vehicle)
	{
		ConfigureVehicleHeadlights(MeshSection.LocalBounds);
	}
	else
	{
		DisableVehicleHeadlights();
	}

	return true;
}

bool ASimCopterGroundAgent::LoadOriginalPedestrianSpriteFromOriginalGameRoot()
{
	LastMeshLoadError.Reset();
	bUsingPedestrianSprite = false;
	bUsingPedestrianFigure = false;
	PedestrianSpriteTexture = nullptr;
	SpriteMaterialInstance = nullptr;
	OriginalMeshComponent->ClearAllMeshSections();

	const FString RootPath = ResolveOriginalGameRoot();
	if (RootPath.IsEmpty())
	{
		LastMeshLoadError = TEXT("Original game root is empty.");
		ShowOriginalMesh(false);
		return false;
	}

	if (SpriteMaterial == nullptr)
	{
		SpriteMaterial = LoadSpriteMaterialNoWarn();
	}

	if (SpriteMaterial == nullptr)
	{
		LastMeshLoadError = FString::Printf(TEXT("Missing %s for original pedestrian sprites."), SpriteMaterialPath);
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	UTexture2D* LoadedTexture = nullptr;
	FString Error;
	if (!FSimCopterPopulationSprite::LoadPeople1Texture(this, RootPath, LoadedTexture, Error))
	{
		LastMeshLoadError = Error;
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	PedestrianSpriteTexture = LoadedTexture;
	PedestrianSpriteColumn = FSimCopterPopulationSprite::ResolvePeople1Column(MeshTableName, this);
	PedestrianSpriteRow = 0;
	FSimCopterPopulationSprite::BuildPeople1FrameQuad(OriginalMeshComponent, PedestrianSpriteColumn, PedestrianSpriteRow, PedestrianSpriteHeightCm);

	SpriteMaterialInstance = UMaterialInstanceDynamic::Create(SpriteMaterial, this);
	if (SpriteMaterialInstance != nullptr)
	{
		SpriteMaterialInstance->SetTextureParameterValue(TEXT("Texture"), PedestrianSpriteTexture);
		OriginalMeshComponent->SetMaterial(0, SpriteMaterialInstance);
	}

	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	OriginalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfHeight));
	bUsingPedestrianSprite = true;
	ShowOriginalMesh(true);
	return true;
}

bool ASimCopterGroundAgent::BuildPedestrianBody()
{
	// Prefer the decoded original privanim.df figures; the box body is the offline fallback.
	if (bUseOriginalFigures && BuildPedestrianFigure())
	{
		return true;
	}

	LastMeshLoadError.Reset();
	bUsingPedestrianSprite = false;
	bUsingPedestrianBody = false;
	bUsingPedestrianFigure = false;
	PedestrianSpriteTexture = nullptr;
	SpriteMaterialInstance = nullptr;
	OriginalMeshComponent->ClearAllMeshSections();
	DisableVehicleHeadlights();

	PedestrianOutfitIndex = FSimCopterPopulationBody::ResolveOutfitIndex(this);
	FSimCopterPopulationBody::BuildPerson(OriginalMeshComponent, PedestrianOutfitIndex, PedestrianBodyHeightCm * PopulationWorldScale);

	if (VertexColorMaterial != nullptr)
	{
		OriginalMeshComponent->SetMaterial(0, VertexColorMaterial);
	}

	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	// Feet sit at the capsule bottom; the body is modelled from Z=0 (feet) upward.
	OriginalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfHeight));
	bUsingPedestrianBody = true;
	ShowOriginalMesh(true);
	return true;
}

bool ASimCopterGroundAgent::BuildPedestrianFigure()
{
	LastMeshLoadError.Reset();
	bUsingPedestrianSprite = false;
	bUsingPedestrianBody = false;
	bUsingPedestrianFigure = false;
	PedestrianSpriteTexture = nullptr;
	SpriteMaterialInstance = nullptr;
	FigureHeadTexture = nullptr;
	FigureHeadMaterialInstance = nullptr;
	DisableVehicleHeadlights();

	const FString RootPath = ResolveOriginalGameRoot();
	if (RootPath.IsEmpty())
	{
		LastMeshLoadError = TEXT("Original game root is empty.");
		return false;
	}

	FString Error;
	FigureShared = FSimCopterPopulationFigure::GetShared(RootPath, Error);
	if (!FigureShared.IsValid())
	{
		LastMeshLoadError = Error;
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("privanim figures unavailable: %s"), *Error);
		return false;
	}

	// Stable per-agent choices so a pedestrian keeps its identity for its whole life.
	const uint32 Hash = GetTypeHash(GetFName());
	FString FigureName = PedestrianFigureName;
	if (FigureName.IsEmpty() && PedestrianFigurePool.Num() > 0)
	{
		FigureName = PedestrianFigurePool[Hash % uint32(PedestrianFigurePool.Num())];
	}
	FigureIndex = FigureShared->Model.FindFigureIndex(FigureName);
	if (FigureIndex == INDEX_NONE)
	{
		LastMeshLoadError = FString::Printf(TEXT("Figure '%s' not found in privanim.df."), *FigureName);
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		return false;
	}
	FigureClothesOffset = int32((Hash / 7u) % 14u);

	// Head sprite: pick from the original's SIM3D.BMP head-image table.
	const TArray<int32>& HeadTable = FSimCopterPopulationFigure::GetHeadImageTable();
	FigureHeadIndex = int32((Hash / 3u) % uint32(HeadTable.Num()));
	if (SpriteMaterial == nullptr)
	{
		SpriteMaterial = LoadSpriteMaterialNoWarn();
	}
	if (SpriteMaterial != nullptr)
	{
		if (const FMaxisTextureImage* HeadImage = FigureShared->HeadImages.Find(HeadTable[FigureHeadIndex]))
		{
			FigureHeadTexture = FSimCopterPopulationSprite::CreateTextureFromImage(this, *HeadImage, TEXT("SimCopterFigureHead"));
		}
		if (FigureHeadTexture != nullptr)
		{
			FigureHeadMaterialInstance = UMaterialInstanceDynamic::Create(SpriteMaterial, this);
			if (FigureHeadMaterialInstance != nullptr)
			{
				FigureHeadMaterialInstance->SetTextureParameterValue(TEXT("Texture"), FigureHeadTexture);
			}
		}
	}

	bUsingPedestrianFigure = true; // set before the clip build so RebuildFigureClip can run
	if (!RebuildFigureClip(TEXT("NoMo")))
	{
		bUsingPedestrianFigure = false;
		return false;
	}

	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	// Feet sit at the capsule bottom; the figure is built from Z=0 (feet) upward.
	OriginalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfHeight));
	ShowOriginalMesh(true);
	return true;
}

bool ASimCopterGroundAgent::RebuildFigureClip(const FString& Mnemonic)
{
	if (!bUsingPedestrianFigure || !FigureShared.IsValid() || !FigureShared->Model.Figures.IsValidIndex(FigureIndex))
	{
		return false;
	}
	const FPrivAnimFigure& Figure = FigureShared->Model.Figures[FigureIndex];

	// FUN_004c68f0: figures keyed '2DOG'/'Coww' substitute their quadruped clips -
	// 1Wal/1Run/Tote play DgRn, everything else DgSt.
	FString EffectiveMnemonic = Mnemonic;
	if (Figure.Name.StartsWith(TEXT("2DOG")) || Figure.Name.StartsWith(TEXT("Coww")))
	{
		EffectiveMnemonic = (Mnemonic == TEXT("1Wal") || Mnemonic == TEXT("1Run") || Mnemonic == TEXT("Tote"))
			? TEXT("DgRn")
			: TEXT("DgSt");
	}

	const FPrivAnimClip* Clip = FigureShared->Model.FindClip(Figure, EffectiveMnemonic);
	if (Clip == nullptr)
	{
		Clip = FigureShared->Model.FindClip(Figure, TEXT("NoMo"));
	}
	if (Clip == nullptr)
	{
		LastMeshLoadError = FString::Printf(TEXT("Figure '%s' has no clip for '%s'."), *Figure.Name, *Mnemonic);
		return false;
	}

	const float HeightCm = PedestrianBodyHeightCm * PopulationWorldScale;
	// Calibrate model units from the standing walk clip so poses like "Dead" keep their scale.
	const FPrivAnimClip* StandingClip = FigureShared->Model.FindClip(Figure, TEXT("1Wal"));
	FigureCalibration = FSimCopterPopulationFigure::Calibrate(StandingClip != nullptr ? *StandingClip : *Clip, HeightCm);

	FSimCopterPopulationFigure::FBuildParams Params;
	Params.HeightCm = HeightCm;
	Params.ClothesOffset = FigureClothesOffset;
	Params.bTexturedHead = FigureHeadMaterialInstance != nullptr;

	if (!FSimCopterPopulationFigure::BuildClipSections(
			OriginalMeshComponent, Figure, *Clip, FigureShared->Palette, Params, FigureCalibration, bFigureHasHeadSection))
	{
		LastMeshLoadError = FString::Printf(TEXT("Failed to build figure '%s' clip '%s'."), *Figure.Name, *Clip->Name);
		return false;
	}

	for (int32 Frame = 0; Frame < Clip->FrameCount; ++Frame)
	{
		if (VertexColorMaterial != nullptr)
		{
			OriginalMeshComponent->SetMaterial(Frame * 2, VertexColorMaterial);
		}
		OriginalMeshComponent->SetMaterial(
			Frame * 2 + 1,
			FigureHeadMaterialInstance != nullptr ? static_cast<UMaterialInterface*>(FigureHeadMaterialInstance) : VertexColorMaterial.Get());
	}

	FigureMnemonic = Mnemonic;
	FigureFrameCount = Clip->FrameCount;
	FigureCurrentFrame = 0;
	FigureFrameTime = 0.0f;
	FSimCopterPopulationFigure::ShowFrame(OriginalMeshComponent, FigureFrameCount, 0, bFigureHasHeadSection);
	return true;
}

void ASimCopterGroundAgent::UpdateFigureAnimation(float DeltaSeconds, float SpeedAlpha)
{
	// The original figures animate through whole-pose frames - no lean/bob overlay.
	VisualRoot->SetRelativeRotation(FRotator::ZeroRotator);
	VisualRoot->SetRelativeLocation(FVector::ZeroVector);

	// With the behavior VM active, clips are bound by the programs/post-move selector and
	// frames advance one per behavior tick (FUN_004c6450) in UpdateOriginalBehavior.
	if (bBehaviorActive)
	{
		return;
	}

	const bool bWalking = SpeedAlpha > 0.12f;
	{
		const FString Desired = bWalking ? TEXT("1Wal") : TEXT("NoMo");
		if (Desired != FigureMnemonic)
		{
			RebuildFigureClip(Desired);
		}
	}
	if (FigureFrameCount <= 1)
	{
		return;
	}

	// Idle clips tick at half rate, matching the original's lazier off-screen cadence.
	FigureFrameTime += DeltaSeconds * (bWalking ? FigureFrameRate : FigureFrameRate * 0.5f);
	const int32 DesiredFrame = FMath::FloorToInt(FigureFrameTime) % FigureFrameCount;
	if (DesiredFrame != FigureCurrentFrame)
	{
		FigureCurrentFrame = DesiredFrame;
		FSimCopterPopulationFigure::ShowFrame(OriginalMeshComponent, FigureFrameCount, FigureCurrentFrame, bFigureHasHeadSection);
	}
}

void ASimCopterGroundAgent::ConfigureVehicleHeadlights(const FBox& VehicleLocalBounds)
{
	if (HeadlightLeft == nullptr || HeadlightRight == nullptr)
	{
		return;
	}

	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	// Positions are in VisualRoot space. The car's nose is the mesh's max-X; headlights sit just
	// above the ground at the front corners and aim forward (+X), slightly downward.
	const float FrontX = VehicleLocalBounds.Max.X;
	const float SideY = FMath::Max(VehicleLocalBounds.Max.Y, 10.0f) * 0.62f;
	const float BodyHeight = VehicleLocalBounds.Max.Z - VehicleLocalBounds.Min.Z;
	const float HeadlightZ = -HalfHeight + FMath::Clamp(BodyHeight * 0.25f, 8.0f, 30.0f);
	const FRotator BeamRotation(-9.0f, 0.0f, 0.0f);

	USpotLightComponent* Lights[2] = {HeadlightLeft, HeadlightRight};
	const float SideSigns[2] = {-1.0f, 1.0f};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		USpotLightComponent* Light = Lights[Index];
		Light->SetRelativeLocation(FVector(FrontX, SideSigns[Index] * SideY, HeadlightZ));
		Light->SetRelativeRotation(BeamRotation);
		Light->SetIntensity(HeadlightIntensity);
		Light->SetAttenuationRadius(HeadlightAttenuationRadiusCm);
		Light->SetInnerConeAngle(11.0f);
		Light->SetOuterConeAngle(26.0f);
		Light->SetLightColor(FLinearColor(HeadlightColor));
		Light->SetVisibility(bEnableVehicleHeadlights);
	}
}

void ASimCopterGroundAgent::DisableVehicleHeadlights()
{
	if (HeadlightLeft != nullptr)
	{
		HeadlightLeft->SetVisibility(false);
	}
	if (HeadlightRight != nullptr)
	{
		HeadlightRight->SetVisibility(false);
	}
}

void ASimCopterGroundAgent::SnapToGroundImmediate()
{
	FVector GroundedLocation;
	if (TraceGround(GroundedLocation))
	{
		SetActorLocation(GroundedLocation, false);
	}
}

void ASimCopterGroundAgent::SetMoveTarget(const FVector& NewTargetLocation)
{
	MoveTargetLocation = NewTargetLocation;
	GuidanceMoveTargetTimeRemainingSeconds = 0.0f;
	bHasMoveTarget = true;
}

void ASimCopterGroundAgent::ClearMoveTarget()
{
	bHasMoveTarget = false;
	CurrentVelocityCmPerSec = FVector::ZeroVector;
	AvoidancePathOffsetTimeRemainingSeconds = 0.0f;
	AvoidancePathOffsetSpeedMultiplier = 1.0f;
	GuidanceMoveTargetTimeRemainingSeconds = 0.0f;
}

bool ASimCopterGroundAgent::IsNearMoveTarget(float DistanceCm) const
{
	if (IsAvoidanceMoveActive())
	{
		return false;
	}

	if (!bHasMoveTarget)
	{
		return true;
	}

	const FVector EffectiveTarget = IsAvoidancePathOffsetActive() ? MoveTargetLocation + AvoidancePathOffset : MoveTargetLocation;
	const FVector Delta = EffectiveTarget - GetActorLocation();
	return FVector(Delta.X, Delta.Y, 0.0f).SizeSquared() <= FMath::Square(DistanceCm);
}

float ASimCopterGroundAgent::GetCollisionRadiusCm() const
{
	return CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleRadius() : 0.0f;
}

void ASimCopterGroundAgent::SetTrafficSpeedScale(float NewSpeedScale)
{
	TrafficSpeedScale = FMath::Clamp(NewSpeedScale, 0.0f, 1.75f);
}

void ASimCopterGroundAgent::LimitTrafficSpeedScale(float MaxSpeedScale)
{
	TrafficSpeedScale = FMath::Min(TrafficSpeedScale, FMath::Clamp(MaxSpeedScale, 0.0f, 1.75f));
}

void ASimCopterGroundAgent::ApplyTrafficBrake(float MaxSpeedScale, float DeltaSeconds, float BrakeRate)
{
	const float ClampedSpeedScale = FMath::Clamp(MaxSpeedScale, 0.0f, 1.75f);
	LimitTrafficSpeedScale(ClampedSpeedScale);

	const float BrakeAlpha = FMath::Clamp(1.0f - ClampedSpeedScale, 0.0f, 1.0f);
	const float EffectiveBrakeRate = FMath::Max(0.0f, BrakeRate) * BrakeAlpha;
	if (EffectiveBrakeRate <= 0.0f || DeltaSeconds <= 0.0f)
	{
		return;
	}

	CurrentVelocityCmPerSec = FMath::VInterpTo(CurrentVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, EffectiveBrakeRate);
	ExternalVelocityCmPerSec = FMath::VInterpTo(ExternalVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, EffectiveBrakeRate);
}

void ASimCopterGroundAgent::AddTrafficVelocityImpulse(const FVector& ImpulseCmPerSec)
{
	ExternalVelocityCmPerSec += FVector(ImpulseCmPerSec.X, ImpulseCmPerSec.Y, 0.0f);
}

void ASimCopterGroundAgent::MoveByTrafficSeparation(const FVector& WorldDelta)
{
	if (!WorldDelta.IsNearlyZero())
	{
		AddActorWorldOffset(WorldDelta, false);
		if (bSnapToGround)
		{
			UpdateGroundSnap();
		}
	}
}

void ASimCopterGroundAgent::SetAvoidanceMoveTarget(const FVector& NewTargetLocation, float DurationSeconds, float SpeedMultiplier)
{
	AvoidanceMoveTargetLocation = NewTargetLocation;
	AvoidanceMoveTimeRemainingSeconds = FMath::Max(0.0f, DurationSeconds);
	AvoidanceSpeedMultiplier = FMath::Clamp(SpeedMultiplier, 0.25f, 2.5f);
}

void ASimCopterGroundAgent::SetAvoidancePathOffset(const FVector& NewWorldOffset, float DurationSeconds, float SpeedMultiplier)
{
	AvoidancePathOffset = FVector(NewWorldOffset.X, NewWorldOffset.Y, 0.0f);
	AvoidancePathOffsetTimeRemainingSeconds = FMath::Max(0.0f, DurationSeconds);
	AvoidancePathOffsetSpeedMultiplier = FMath::Clamp(SpeedMultiplier, 0.25f, 2.5f);
}

void ASimCopterGroundAgent::SetGuidanceMoveTarget(const FVector& NewTargetLocation, float DurationSeconds)
{
	GuidanceMoveTargetLocation = NewTargetLocation;
	GuidanceMoveTargetTimeRemainingSeconds = FMath::Max(0.0f, DurationSeconds);
}

void ASimCopterGroundAgent::ApplyAgentShape()
{
	if (CollisionComponent == nullptr || ProxyMeshComponent == nullptr)
	{
		return;
	}

	if (AgentKind == ESimCopterGroundAgentKind::Vehicle)
	{
		CollisionComponent->SetCapsuleSize(VehicleCapsuleRadiusCm * PopulationWorldScale, VehicleCapsuleHalfHeightCm * PopulationWorldScale);
		ProxyMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, (-VehicleCapsuleHalfHeightCm + VehicleFallbackZCm) * PopulationWorldScale));
		ProxyMeshComponent->SetRelativeScale3D(FVector(2.8f, 1.25f, 0.55f) * PopulationWorldScale);
		MovementSpeedCmPerSec = FMath::Max(MovementSpeedCmPerSec, 520.0f);
	}
	else
	{
		CollisionComponent->SetCapsuleSize(PedestrianCapsuleRadiusCm * PopulationWorldScale, PedestrianCapsuleHalfHeightCm * PopulationWorldScale);
		ProxyMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, (-PedestrianCapsuleHalfHeightCm + PedestrianFallbackZCm) * PopulationWorldScale));
		ProxyMeshComponent->SetRelativeScale3D(FVector(0.28f, 0.18f, 1.76f) * PopulationWorldScale);
		MovementSpeedCmPerSec = FMath::Clamp(MovementSpeedCmPerSec, 130.0f, 520.0f);
	}
}

void ASimCopterGroundAgent::UpdateMovement(float DeltaSeconds)
{
	if (RootComponent == nullptr)
	{
		return;
	}

	// Behavior-VM pedestrians move with the original per-tick model: a constant velocity
	// renewed by each MoveStep, with no target seeking or arrival deceleration (the cause of
	// the old stop-start pulse). Yaw is set from the stored facing attribute, not steering.
	// Avoidance move targets (car dodges) still take over via the branch below.
	if (bBehaviorActive && AgentKind == ESimCopterGroundAgentKind::Pedestrian && !IsAvoidanceMoveActive())
	{
		if (BehaviorStepTimeRemainingSeconds > 0.0f)
		{
			BehaviorStepTimeRemainingSeconds -= DeltaSeconds;
			CurrentVelocityCmPerSec = BehaviorStepVelocityCmPerSec;
		}
		else
		{
			CurrentVelocityCmPerSec = FVector::ZeroVector;
		}
		const FVector Delta = (CurrentVelocityCmPerSec + ExternalVelocityCmPerSec) * DeltaSeconds;
		if (!Delta.IsNearlyZero())
		{
			RootComponent->MoveComponent(Delta, GetActorQuat(), false);
		}
		ExternalVelocityCmPerSec = FMath::VInterpTo(ExternalVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, 8.0f);
		return;
	}

	const bool bUsingAvoidanceTarget = IsAvoidanceMoveActive();
	if (!bHasMoveTarget && !bUsingAvoidanceTarget)
	{
		CurrentVelocityCmPerSec = FMath::VInterpTo(CurrentVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, 6.0f);
		ExternalVelocityCmPerSec = FMath::VInterpTo(ExternalVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, 8.0f);
		if (!ExternalVelocityCmPerSec.IsNearlyZero())
		{
			RootComponent->MoveComponent(ExternalVelocityCmPerSec * DeltaSeconds, GetActorQuat(), false);
		}
		return;
	}

	const FVector CurrentLocation = GetActorLocation();
	const bool bUsingAvoidancePathOffset = IsAvoidancePathOffsetActive();
	bool bUsingGuidanceTarget = IsGuidanceMoveTargetActive();
	FVector BaseMoveTarget = bUsingGuidanceTarget ? GuidanceMoveTargetLocation : MoveTargetLocation;
	FVector ActiveMoveTarget = bUsingAvoidanceTarget
		? AvoidanceMoveTargetLocation
		: BaseMoveTarget + (bUsingAvoidancePathOffset ? AvoidancePathOffset : FVector::ZeroVector);
	FVector ToTarget = ActiveMoveTarget - CurrentLocation;
	FVector FlatToTarget(ToTarget.X, ToTarget.Y, 0.0f);
	float DistanceToTarget = FlatToTarget.Size();
	if (bUsingGuidanceTarget && DistanceToTarget <= TargetStopDistanceCm)
	{
		GuidanceMoveTargetTimeRemainingSeconds = 0.0f;
		bUsingGuidanceTarget = false;
		BaseMoveTarget = MoveTargetLocation;
		ActiveMoveTarget = MoveTargetLocation + (bUsingAvoidancePathOffset ? AvoidancePathOffset : FVector::ZeroVector);
		ToTarget = ActiveMoveTarget - CurrentLocation;
		FlatToTarget = FVector(ToTarget.X, ToTarget.Y, 0.0f);
		DistanceToTarget = FlatToTarget.Size();
	}
	if (DistanceToTarget <= TargetStopDistanceCm)
	{
		if (bUsingAvoidanceTarget)
		{
			AvoidanceMoveTimeRemainingSeconds = 0.0f;
			AvoidanceSpeedMultiplier = 1.0f;
		}
		else
		{
			ClearMoveTarget();
		}
		return;
	}

	const FVector DesiredDirection = FlatToTarget / DistanceToTarget;
	const float EffectiveSpeedScale = TrafficSpeedScale * (bUsingAvoidanceTarget
		? AvoidanceSpeedMultiplier
		: (bUsingAvoidancePathOffset ? AvoidancePathOffsetSpeedMultiplier : 1.0f));
	const FVector DesiredVelocity = DesiredDirection * MovementSpeedCmPerSec * EffectiveSpeedScale;
	CurrentVelocityCmPerSec = FMath::VInterpTo(CurrentVelocityCmPerSec, DesiredVelocity, DeltaSeconds, AgentKind == ESimCopterGroundAgentKind::Vehicle ? 3.0f : 9.0f);

	const FVector Delta = (CurrentVelocityCmPerSec + ExternalVelocityCmPerSec) * DeltaSeconds;
	const FRotator CurrentRotation = GetActorRotation();
	const FRotator DesiredRotation(0.0f, DesiredDirection.Rotation().Yaw, 0.0f);
	const FRotator NewRotation = FMath::RInterpConstantTo(CurrentRotation, DesiredRotation, DeltaSeconds, TurnRateDegPerSec);

	// Move kinematically (no collision sweep). The traffic system handles road-following,
	// separation, and bump responses between agents; sweeping here would make capsules catch on
	// building corners and stall instead of continuing along the road graph.
	RootComponent->MoveComponent(Delta, NewRotation.Quaternion(), false);
	ExternalVelocityCmPerSec = FMath::VInterpTo(ExternalVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, 8.0f);
}

bool ASimCopterGroundAgent::TraceGround(FVector& OutGroundLocation) const
{
	if (GetWorld() == nullptr || CollisionComponent == nullptr)
	{
		return false;
	}

	const float HalfHeight = CollisionComponent->GetScaledCapsuleHalfHeight();
	const FVector CurrentLocation = GetActorLocation();

	// Trace well above to well below the agent so placement survives any gap between the traffic
	// spawner's terrain estimate and the city's actual rendered surface (the cause of the old
	// hovering). The Camera channel is blocked by the city terrain/mesh but ignored by every
	// pedestrian/vehicle/player capsule, so agents never snap onto one another.
	float StartZ = CurrentLocation.Z + GroundProbeUpCm;
	float TerrainFallbackZ = 0.0f;
	bool bHasTerrainClamp = false;

	// The original never derives a person's Z from the building model: people walk building
	// tiles at street level (Z = tile altitude + figure.twk feet adjust). A pedestrian's probe
	// therefore starts just above the tile's terrain altitude, below every roof, so it can pick
	// up sidewalk/road geometry but can never snap on top of a building. Vehicles keep the tall
	// probe (bridge decks sit far above the water tile's terrain altitude).
	if (AgentKind == ESimCopterGroundAgentKind::Pedestrian)
	{
		if (const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(GetOwner()))
		{
			float TerrainWorldZ = 0.0f;
			if (TrafficSystem->TryGetTerrainWorldZAtWorldLocation(CurrentLocation, TerrainWorldZ))
			{
				// Building tiles are flat by construction, so the walk surface is the tile
				// altitude itself and the probe start must stay below the lowest roof. Open
				// tiles can slope, so grant the extra headroom only there.
				const int32 TileClass = TrafficSystem->GetPeopleTileClassAtWorldLocation(CurrentLocation);
				const bool bBuildingTile = TileClass >= 10 && TileClass <= 13;
				StartZ = TerrainWorldZ + PedestrianGroundProbeStartAboveTerrainCm +
					(bBuildingTile ? 0.0f : PedestrianGroundProbeSlopeHeadroomCm);
				TerrainFallbackZ = TerrainWorldZ;
				bHasTerrainClamp = true;
			}
		}
	}

	const FVector Start(CurrentLocation.X, CurrentLocation.Y, StartZ);
	const float EndZ = FMath::Min(CurrentLocation.Z - HalfHeight, StartZ) - GroundProbeDistanceCm;
	const FVector End(CurrentLocation.X, CurrentLocation.Y, EndZ);
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterGroundAgentSnap), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Camera, QueryParams) && Hit.bBlockingHit)
	{
		OutGroundLocation = FVector(CurrentLocation.X, CurrentLocation.Y, Hit.ImpactPoint.Z + HalfHeight + 1.0f);
		return true;
	}

	if (bHasTerrainClamp)
	{
		// Clamped probe started inside/under building geometry with no ground quad beneath:
		// stand at the tile's terrain altitude, exactly like the original placement.
		OutGroundLocation = FVector(CurrentLocation.X, CurrentLocation.Y, TerrainFallbackZ + HalfHeight + 1.0f);
		return true;
	}

	return false;
}

void ASimCopterGroundAgent::UpdateGroundSnap()
{
	FVector GroundedLocation;
	if (TraceGround(GroundedLocation))
	{
		SetActorLocation(GroundedLocation, false);
	}
}

void ASimCopterGroundAgent::UpdateJankyAnimation(float DeltaSeconds)
{
	if (!bEnableJankyAnimation || VisualRoot == nullptr)
	{
		return;
	}

	AnimationTimeSeconds += DeltaSeconds;
	const float SpeedAlpha = FMath::Clamp(CurrentVelocityCmPerSec.Size() / FMath::Max(1.0f, MovementSpeedCmPerSec), 0.0f, 1.0f);
	const float Wave = FMath::Sin(AnimationTimeSeconds * JankyAnimationRate + AnimationPhase);

	if (AgentKind == ESimCopterGroundAgentKind::Vehicle)
	{
		const float Roll = Wave * 1.0f * SpeedAlpha;
		const float Pitch = FMath::Sin(AnimationTimeSeconds * JankyAnimationRate * 0.7f + AnimationPhase) * 0.65f * SpeedAlpha;
		VisualRoot->SetRelativeRotation(FRotator(Pitch, 0.0f, Roll));
		VisualRoot->SetRelativeLocation(FVector::ZeroVector);
	}
	else if (bUsingPedestrianFigure)
	{
		UpdateFigureAnimation(DeltaSeconds, SpeedAlpha);
	}
	else
	{
		const float Lean = Wave * 7.0f * SpeedAlpha; // degrees of roll (scale-independent)
		const float Bob = FMath::Abs(Wave) * 7.0f * SpeedAlpha * PopulationWorldScale;
		VisualRoot->SetRelativeRotation(FRotator(0.0f, 0.0f, Lean));
		VisualRoot->SetRelativeLocation(FVector(0.0f, 0.0f, Bob));

		if (bUsingPedestrianSprite)
		{
			const int32 DesiredSpriteRow = SpeedAlpha > 0.12f
				? FMath::FloorToInt(AnimationTimeSeconds * JankyAnimationRate) % FSimCopterPopulationSprite::People1Rows
				: 0;
			if (DesiredSpriteRow != PedestrianSpriteRow)
			{
				PedestrianSpriteRow = DesiredSpriteRow;
				FSimCopterPopulationSprite::BuildPeople1FrameQuad(OriginalMeshComponent, PedestrianSpriteColumn, PedestrianSpriteRow, PedestrianSpriteHeightCm);
				if (SpriteMaterialInstance != nullptr)
				{
					OriginalMeshComponent->SetMaterial(0, SpriteMaterialInstance);
				}
			}
		}
	}
}

void ASimCopterGroundAgent::ShowOriginalMesh(bool bUseOriginalMesh)
{
	bUsingOriginalMesh = bUseOriginalMesh;
	if (!bUseOriginalMesh)
	{
		bUsingPedestrianSprite = false;
		bUsingPedestrianBody = false;
		bUsingPedestrianFigure = false;
	}
	if (OriginalMeshComponent != nullptr)
	{
		OriginalMeshComponent->SetVisibility(bUseOriginalMesh, true);
	}
	if (ProxyMeshComponent != nullptr)
	{
		ProxyMeshComponent->SetVisibility(!bUseOriginalMesh, true);
	}
}

FString ASimCopterGroundAgent::ResolveOriginalGameRoot() const
{
	const FString ConfiguredPath = OriginalGameRoot.Path.TrimStartAndEnd();
	if (ConfiguredPath.IsEmpty())
	{
		return FString();
	}

	if (FPaths::IsRelative(ConfiguredPath))
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ConfiguredPath));
	}

	return FPaths::ConvertRelativePathToFull(ConfiguredPath);
}
