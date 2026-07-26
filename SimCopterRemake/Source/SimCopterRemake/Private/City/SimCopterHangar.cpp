// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterHangar.h"

#include "City/SimCity2000CityActor.h"
#include "City/SimCopterAirport.h"
#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Game/SimCopterCareerSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Ground/SimCopterOnFootPawn.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "ProceduralMeshComponent.h"
#include "UI/SimCopterHangarArt.h"
#include "UObject/ConstructorHelpers.h"

#include "../UI/SSimCopterHangarMenu.h"

namespace
{
// Flat, low-colour paint that sits next to the original's building palette rather than fighting
// it: pale green corrugated walls, a darker roof, a dark interior.
const FLinearColor WallColor(0.52f, 0.56f, 0.50f, 1.0f);
const FLinearColor WallShadeColor(0.40f, 0.44f, 0.39f, 1.0f);
const FLinearColor RoofColor(0.33f, 0.35f, 0.36f, 1.0f);
const FLinearColor TrimColor(0.72f, 0.55f, 0.16f, 1.0f);
const FLinearColor FloorColor(0.22f, 0.22f, 0.23f, 1.0f);

constexpr const TCHAR* VertexColorMaterialPath =
	TEXT("/Game/Materials/M_SimCopterLitVertexColor.M_SimCopterLitVertexColor");

// One quad, wound both ways: the player walks inside the shell, so every face has to be visible
// from both sides. (SimCopterPopulationBody's box builder does the same for the same reason.)
void AppendQuad(
	const FVector& A,
	const FVector& B,
	const FVector& C,
	const FVector& D,
	const FLinearColor& Color,
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals,
	TArray<FVector2D>& UVs,
	TArray<FLinearColor>& VertexColors,
	TArray<FProcMeshTangent>& Tangents)
{
	const FVector Normal = FVector::CrossProduct(B - A, D - A).GetSafeNormal();
	const FVector Tangent = (B - A).GetSafeNormal();

	const int32 Base = Vertices.Num();
	Vertices.Add(A);
	Vertices.Add(B);
	Vertices.Add(C);
	Vertices.Add(D);

	const FVector2D CornerUVs[4] = { FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) };
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		Normals.Add(Normal);
		UVs.Add(CornerUVs[Corner]);
		VertexColors.Add(Color);
		Tangents.Add(FProcMeshTangent(Tangent, false));
	}

	Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
	Triangles.Append({ Base, Base + 2, Base + 1, Base, Base + 3, Base + 2 });
}

void AppendTriangle(
	const FVector& A,
	const FVector& B,
	const FVector& C,
	const FLinearColor& Color,
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals,
	TArray<FVector2D>& UVs,
	TArray<FLinearColor>& VertexColors,
	TArray<FProcMeshTangent>& Tangents)
{
	const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
	const FVector Tangent = (B - A).GetSafeNormal();

	const int32 Base = Vertices.Num();
	Vertices.Add(A);
	Vertices.Add(B);
	Vertices.Add(C);

	const FVector2D CornerUVs[3] = { FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1) };
	for (int32 Corner = 0; Corner < 3; ++Corner)
	{
		Normals.Add(Normal);
		UVs.Add(CornerUVs[Corner]);
		VertexColors.Add(Color);
		Tangents.Add(FProcMeshTangent(Tangent, false));
	}

	Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 1 });
}
}

namespace SimCopterHangarPlacement
{
FVector2D GetSideAnchorTile(const FIntPoint& AirportOrigin, const ESide Side)
{
	// The block covers tile offsets 0..3 from its origin, so its outer edge is at -0.5 and 3.5
	// and its centre at 1.5.
	constexpr float BlockCentre = (SimCopterAirport::BlockSpan - 1) * 0.5f;
	constexpr float NearEdge = -0.5f - StandoffTiles;
	constexpr float FarEdge = (SimCopterAirport::BlockSpan - 1) + 0.5f + StandoffTiles;

	FVector2D Offset(BlockCentre, BlockCentre);
	switch (Side)
	{
	case ESide::North: Offset.Y = NearEdge; break;
	case ESide::South: Offset.Y = FarEdge; break;
	case ESide::West:  Offset.X = NearEdge; break;
	case ESide::East:  Offset.X = FarEdge; break;
	default: break;
	}

	return FVector2D(AirportOrigin.X + Offset.X, AirportOrigin.Y + Offset.Y);
}

ESide GetNearestSide(const FIntPoint& AirportOrigin, const FVector2D& TargetTile)
{
	constexpr float BlockCentre = (SimCopterAirport::BlockSpan - 1) * 0.5f;
	const FVector2D Delta(
		TargetTile.X - (AirportOrigin.X + BlockCentre),
		TargetTile.Y - (AirportOrigin.Y + BlockCentre));

	// A point inside the block resolves to whichever edge it is closest to leaving through.
	if (FMath::Abs(Delta.X) >= FMath::Abs(Delta.Y))
	{
		return Delta.X >= 0.0f ? ESide::East : ESide::West;
	}
	return Delta.Y >= 0.0f ? ESide::South : ESide::North;
}

void GetSidesByDistance(const FIntPoint& AirportOrigin, const FVector2D& TargetTile, TArray<ESide>& OutSides)
{
	OutSides.Reset();
	for (int32 Index = 0; Index < static_cast<int32>(ESide::Count); ++Index)
	{
		OutSides.Add(static_cast<ESide>(Index));
	}

	OutSides.Sort([&AirportOrigin, &TargetTile](const ESide Left, const ESide Right)
	{
		const double LeftDistance = (GetSideAnchorTile(AirportOrigin, Left) - TargetTile).SizeSquared();
		const double RightDistance = (GetSideAnchorTile(AirportOrigin, Right) - TargetTile).SizeSquared();
		return LeftDistance < RightDistance;
	});
}

void GetFootprintTiles(const FVector2D& AnchorTile, TArray<FIntPoint>& OutTiles)
{
	OutTiles.Reset();

	const int32 MinX = FMath::FloorToInt(AnchorTile.X - DepthTiles * 0.5f);
	const int32 MaxX = FMath::FloorToInt(AnchorTile.X + DepthTiles * 0.5f);
	const int32 MinY = FMath::FloorToInt(AnchorTile.Y - WidthTiles * 0.5f);
	const int32 MaxY = FMath::FloorToInt(AnchorTile.Y + WidthTiles * 0.5f);

	// The footprint is square-ish and the yaw only ever turns it a quarter at a time, so the same
	// span is used on both axes - whichever way the doors end up facing.
	const int32 Min = FMath::Min(MinX, MinY);
	const int32 Max = FMath::Max(MaxX, MaxY);
	const int32 CentreX = FMath::FloorToInt(AnchorTile.X);
	const int32 CentreY = FMath::FloorToInt(AnchorTile.Y);
	const int32 Radius = FMath::Max(CentreX - Min, Max - CentreX);

	for (int32 OffsetY = -Radius; OffsetY <= Radius; ++OffsetY)
	{
		for (int32 OffsetX = -Radius; OffsetX <= Radius; ++OffsetX)
		{
			OutTiles.Emplace(CentreX + OffsetX, CentreY + OffsetY);
		}
	}
}

float GetSnappedFacingYawDegrees(const FVector& From, const FVector& Target)
{
	const FVector Delta = Target - From;
	if (Delta.SizeSquared2D() < KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
	return FMath::UnwindDegrees(FMath::RoundToFloat(Yaw / 90.0f) * 90.0f);
}
}

ASimCopterHangar::ASimCopterHangar()
{
	// The overlap event does the work; the tick only re-checks, so it runs a few times a second.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.15f;

	ShellMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ShellMesh"));
	SetRootComponent(ShellMesh);
	ShellMesh->bUseAsyncCooking = true;
	ShellMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ShellMesh->SetCollisionObjectType(ECC_WorldStatic);
	ShellMesh->SetCollisionResponseToAllChannels(ECR_Block);
	ShellMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	ShellMesh->SetCanEverAffectNavigation(false);

	EntryTrigger = CreateDefaultSubobject<UBoxComponent>(TEXT("EntryTrigger"));
	EntryTrigger->SetupAttachment(ShellMesh);
	EntryTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	EntryTrigger->SetCollisionObjectType(ECC_WorldDynamic);
	EntryTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	EntryTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	EntryTrigger->SetGenerateOverlapEvents(true);
	EntryTrigger->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> VertexColorMaterialFinder(VertexColorMaterialPath);
	if (VertexColorMaterialFinder.Succeeded())
	{
		ShellMaterial = VertexColorMaterialFinder.Object;
	}
}

void ASimCopterHangar::BeginPlay()
{
	Super::BeginPlay();

	BuildShellMesh();
	EntryTrigger->OnComponentBeginOverlap.AddDynamic(this, &ASimCopterHangar::HandleTriggerBeginOverlap);
}

void ASimCopterHangar::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bShellOpen)
	{
		CloseShell();
	}
	Super::EndPlay(EndPlayReason);
}

void ASimCopterHangar::BuildShellMesh()
{
	using namespace SimCopterHangarPlacement;

	const float HalfWidth = WidthTiles * TileSizeCm * 0.5f;
	const float HalfDepth = DepthTiles * TileSizeCm * 0.5f;
	const float Eaves = EavesHeightTiles * TileSizeCm;
	const float Apex = ApexHeightTiles * TileSizeCm;
	const float HalfDoor = FMath::Min(DoorWidthTiles * TileSizeCm * 0.5f, HalfWidth - 1.0f);
	const float DoorTop = FMath::Min(DoorHeightTiles * TileSizeCm, Eaves - 1.0f);

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	auto Quad = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FLinearColor& Color)
	{
		AppendQuad(A, B, C, D, Color, Vertices, Triangles, Normals, UVs, VertexColors, Tangents);
	};
	auto Tri = [&](const FVector& A, const FVector& B, const FVector& C, const FLinearColor& Color)
	{
		AppendTriangle(A, B, C, Color, Vertices, Triangles, Normals, UVs, VertexColors, Tangents);
	};

	// The doorway faces +X, which is the actor's forward axis - PlaceAtAirport yaws the whole
	// actor so that axis points at the player's start.
	const float Front = HalfDepth;
	const float Back = -HalfDepth;

	// Floor slab.
	Quad(
		FVector(Back, -HalfWidth, 0.0f),
		FVector(Front, -HalfWidth, 0.0f),
		FVector(Front, HalfWidth, 0.0f),
		FVector(Back, HalfWidth, 0.0f),
		FloorColor);

	// Side walls up to the eaves.
	for (const float Side : { -HalfWidth, HalfWidth })
	{
		Quad(
			FVector(Back, Side, 0.0f),
			FVector(Front, Side, 0.0f),
			FVector(Front, Side, Eaves),
			FVector(Back, Side, Eaves),
			Side < 0.0f ? WallColor : WallShadeColor);
	}

	// Back wall plus its gable.
	Quad(
		FVector(Back, -HalfWidth, 0.0f),
		FVector(Back, HalfWidth, 0.0f),
		FVector(Back, HalfWidth, Eaves),
		FVector(Back, -HalfWidth, Eaves),
		WallShadeColor);
	Tri(
		FVector(Back, -HalfWidth, Eaves),
		FVector(Back, HalfWidth, Eaves),
		FVector(Back, 0.0f, Apex),
		WallShadeColor);

	// Front wall: two jambs, a lintel, and the gable above them - the hole between is the door.
	Quad(
		FVector(Front, -HalfWidth, 0.0f),
		FVector(Front, -HalfDoor, 0.0f),
		FVector(Front, -HalfDoor, Eaves),
		FVector(Front, -HalfWidth, Eaves),
		WallColor);
	Quad(
		FVector(Front, HalfDoor, 0.0f),
		FVector(Front, HalfWidth, 0.0f),
		FVector(Front, HalfWidth, Eaves),
		FVector(Front, HalfDoor, Eaves),
		WallColor);
	Quad(
		FVector(Front, -HalfDoor, DoorTop),
		FVector(Front, HalfDoor, DoorTop),
		FVector(Front, HalfDoor, Eaves),
		FVector(Front, -HalfDoor, Eaves),
		WallColor);
	Tri(
		FVector(Front, -HalfWidth, Eaves),
		FVector(Front, HalfWidth, Eaves),
		FVector(Front, 0.0f, Apex),
		TrimColor);

	// Gable roof: two slopes from the eaves to the ridge.
	Quad(
		FVector(Back, -HalfWidth, Eaves),
		FVector(Front, -HalfWidth, Eaves),
		FVector(Front, 0.0f, Apex),
		FVector(Back, 0.0f, Apex),
		RoofColor);
	Quad(
		FVector(Back, 0.0f, Apex),
		FVector(Front, 0.0f, Apex),
		FVector(Front, HalfWidth, Eaves),
		FVector(Back, HalfWidth, Eaves),
		RoofColor);

	// A band over the doorway, so the building reads as the way in from the air as well.
	const float BandZ = DoorTop + (Eaves - DoorTop) * 0.35f;
	const float BandHeight = FMath::Max(6.0f, (Eaves - DoorTop) * 0.3f);
	Quad(
		FVector(Front + 2.0f, -HalfDoor, BandZ),
		FVector(Front + 2.0f, HalfDoor, BandZ),
		FVector(Front + 2.0f, HalfDoor, BandZ + BandHeight),
		FVector(Front + 2.0f, -HalfDoor, BandZ + BandHeight),
		TrimColor);

	ShellMesh->ClearAllMeshSections();
	ShellMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, /*bCreateCollision=*/true);
	if (ShellMaterial != nullptr)
	{
		ShellMesh->SetMaterial(0, ShellMaterial);
	}

	// The trigger fills the interior but stops short of the walls and the doorway, so the shell
	// only opens once the player is properly inside.
	const float Inset = FMath::Min(TileSizeCm * 0.2f, HalfWidth * 0.4f);
	EntryTrigger->SetBoxExtent(FVector(
		FMath::Max(10.0f, HalfDepth - Inset),
		FMath::Max(10.0f, HalfWidth - Inset),
		FMath::Max(10.0f, Eaves * 0.5f)));
	EntryTrigger->SetRelativeLocation(FVector(-Inset * 0.5f, 0.0f, Eaves * 0.5f));
}

bool ASimCopterHangar::PlaceAtAirport(ASimCopterTrafficSystemActor* Traffic, const FVector& FacingTarget)
{
	using namespace SimCopterHangarPlacement;

	if (Traffic == nullptr)
	{
		return false;
	}

	ASimCity2000CityActor* CityActor = Traffic->GetCityActor();
	if (CityActor != nullptr)
	{
		TileSizeCm = CityActor->GetTileSize();
	}

	// The pad table gives an exact tile -> world basis that works for the fallback airport too:
	// pad 8 is the block's (0,0) tile, pad 9 its (3,0) and pad 3 its (0,3).
	FVector PadOrigin, PadX, PadY;
	if (!Traffic->TryGetAirportPadWorldLocation(8, PadOrigin) ||
		!Traffic->TryGetAirportPadWorldLocation(9, PadX) ||
		!Traffic->TryGetAirportPadWorldLocation(3, PadY))
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter hangar: the airport has no pads to anchor to."));
		return false;
	}

	const FVector AxisX = (PadX - PadOrigin) / 3.0f;
	const FVector AxisY = (PadY - PadOrigin) / 3.0f;
	if (AxisX.SizeSquared() < KINDA_SMALL_NUMBER || AxisY.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// Where the player starts, in the block's own tile offsets.
	const FVector TargetDelta = FacingTarget - PadOrigin;
	const FVector2D TargetTileOffset(
		FVector::DotProduct(TargetDelta, AxisX) / AxisX.SizeSquared(),
		FVector::DotProduct(TargetDelta, AxisY) / AxisY.SizeSquared());

	const FIntPoint Origin = Traffic->GetAirportOriginTile();
	const FVector2D TargetTile(Origin.X + TargetTileOffset.X, Origin.Y + TargetTileOffset.Y);

	// Prefer the side the player starts on, but a hangar dropped on top of a standing city block
	// looks like a bug, so walk out to the next-nearest side until the ground is clear. If every
	// side is built on, take the nearest one anyway and knock down what is in the way.
	TArray<ESide> Sides;
	GetSidesByDistance(Origin, TargetTile, Sides);

	ESide Side = Sides.Num() > 0 ? Sides[0] : ESide::South;
	TArray<FIntPoint> Footprint;
	bool bFoundClearSide = false;

	for (const ESide Candidate : Sides)
	{
		GetFootprintTiles(GetSideAnchorTile(Origin, Candidate), Footprint);

		bool bClear = true;
		for (const FIntPoint& Tile : Footprint)
		{
			const bool bBuilt = CityActor != nullptr && CityActor->HasStandingBuildingAtTile(Tile.X, Tile.Y);
			if (bBuilt || Traffic->IsWaterTile(Tile.X, Tile.Y))
			{
				bClear = false;
				break;
			}
		}

		if (bClear)
		{
			Side = Candidate;
			bFoundClearSide = true;
			break;
		}
	}

	if (!bFoundClearSide && CityActor != nullptr)
	{
		GetFootprintTiles(GetSideAnchorTile(Origin, Side), Footprint);
		TArray<FIntPoint> Cleared;
		for (const FIntPoint& Tile : Footprint)
		{
			if (CityActor->DemolishBuildingAtTile(Tile.X, Tile.Y, Cleared, /*bLeaveRubble=*/false))
			{
				Traffic->ClearXbldTiles(Cleared);
			}
		}
		UE_LOG(LogTemp, Display,
			TEXT("SimCopter hangar: every side of the airport was built on; cleared the nearest one."));
	}

	const FVector2D AnchorTile = GetSideAnchorTile(Origin, Side);
	const FVector2D AnchorOffset(AnchorTile.X - Origin.X, AnchorTile.Y - Origin.Y);

	FVector Location = PadOrigin + AxisX * AnchorOffset.X + AxisY * AnchorOffset.Y;

	// Seat the building on whatever the ground does out there; the airport's flattened patch
	// stops at the block edge, so the hangar is usually just off it.
	const UWorld* World = GetWorld();
	if (World != nullptr && CityActor != nullptr)
	{
		const float HalfWidth = WidthTiles * TileSizeCm * 0.5f;
		const float HalfDepth = DepthTiles * TileSizeCm * 0.5f;
		const FVector Samples[5] = {
			Location,
			Location + FVector(HalfDepth, HalfWidth, 0.0f),
			Location + FVector(HalfDepth, -HalfWidth, 0.0f),
			Location + FVector(-HalfDepth, HalfWidth, 0.0f),
			Location + FVector(-HalfDepth, -HalfWidth, 0.0f),
		};

		float HighestZ = -BIG_NUMBER;
		for (const FVector& Sample : Samples)
		{
			FCollisionObjectQueryParams ObjectParams;
			ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
			FCollisionQueryParams Params(FName(TEXT("SimCopterHangarGround")), /*bTraceComplex=*/false, this);

			TArray<FHitResult> Hits;
			const FVector Start(Sample.X, Sample.Y, Location.Z + 6000.0f);
			const FVector End(Sample.X, Sample.Y, Location.Z - 6000.0f);
			if (!World->LineTraceMultiByObjectType(Hits, Start, End, ObjectParams, Params))
			{
				continue;
			}

			for (const FHitResult& Hit : Hits)
			{
				if (CityActor->IsTerrainCollisionComponent(Hit.GetComponent()))
				{
					HighestZ = FMath::Max(HighestZ, Hit.ImpactPoint.Z);
					break;
				}
			}
		}

		if (HighestZ > -BIG_NUMBER * 0.5f)
		{
			Location.Z = HighestZ;
		}
	}

	// Turn the doorway toward the player's start, square with the city grid.
	const float Yaw = GetSnappedFacingYawDegrees(Location, FacingTarget);
	SetActorLocationAndRotation(Location, FRotator(0.0f, Yaw, 0.0f), /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	// TileSizeCm may have changed since the constructor built the shell.
	BuildShellMesh();

	UE_LOG(LogTemp, Display,
		TEXT("SimCopter hangar: placed on the %s side of the airport block (%d, %d) at %s facing %.0f deg (site %s)."),
		Side == ESide::North ? TEXT("north") : Side == ESide::South ? TEXT("south") : Side == ESide::West ? TEXT("west") : TEXT("east"),
		Origin.X,
		Origin.Y,
		*Location.ToCompactString(),
		Yaw,
		bFoundClearSide ? TEXT("was clear") : TEXT("was cleared"));

	return true;
}

FVector ASimCopterHangar::GetTagWorldLocation() const
{
	using namespace SimCopterHangarPlacement;
	return GetActorLocation() + FVector(0.0f, 0.0f, ApexHeightTiles * TileSizeCm + TileSizeCm * 0.25f);
}

bool ASimCopterHangar::IsAnyShellOpen(const UWorld* World)
{
	if (World == nullptr)
	{
		return false;
	}

	for (TActorIterator<ASimCopterHangar> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->IsShellOpen())
		{
			return true;
		}
	}

	return false;
}

void ASimCopterHangar::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bShellOpen)
	{
		return;
	}

	// The overlap event covers walking in. This covers arriving any other way - a teleport, a
	// respawn, or a frame where the trigger's overlap state was rebuilt underneath the pawn.
	const APlayerController* Controller = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	if (Pawn != nullptr && IsInsideEntryVolume(Pawn->GetActorLocation()))
	{
		TryOpenForPawn(Pawn);
	}
}

bool ASimCopterHangar::IsInsideEntryVolume(const FVector& WorldLocation) const
{
	if (EntryTrigger == nullptr)
	{
		return false;
	}

	const FVector Local = EntryTrigger->GetComponentTransform().InverseTransformPosition(WorldLocation);
	const FVector Extent = EntryTrigger->GetScaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

void ASimCopterHangar::TryOpenForPawn(AActor* Candidate)
{
	if (bShellOpen || Candidate == nullptr)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (World != nullptr && World->GetTimeSeconds() < ReopenAllowedTimeSeconds)
	{
		return;
	}

	// Only the player on foot walks into the hangar; a helicopter that clips the volume, or an
	// NPC wandering past, must not open the shop.
	const ASimCopterOnFootPawn* OnFoot = Cast<ASimCopterOnFootPawn>(Candidate);
	if (OnFoot == nullptr || OnFoot->GetController() == nullptr)
	{
		return;
	}

	OpenShell(Cast<APlayerController>(OnFoot->GetController()));
}

void ASimCopterHangar::HandleTriggerBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	TryOpenForPawn(OtherActor);
}

FString ASimCopterHangar::ResolveOriginalGameRoot() const
{
	if (!OriginalGameRoot.Path.IsEmpty())
	{
		return OriginalGameRoot.Path;
	}

	TArray<FString, TInlineAllocator<3>> Candidates;
	Candidates.Add(FPaths::ProjectContentDir() / TEXT("OriginalGame"));
	Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Reference/SimCopterOriginalGame")));
	Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame")));

	for (FString Candidate : Candidates)
	{
		Candidate = FPaths::ConvertRelativePathToFull(Candidate);
		FPaths::NormalizeDirectoryName(Candidate);
		if (FPaths::DirectoryExists(Candidate))
		{
			return Candidate;
		}
	}

	return FString();
}

void ASimCopterHangar::OpenShell(APlayerController* PlayerController)
{
	if (bShellOpen || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	if (Art == nullptr)
	{
		Art = NewObject<USimCopterHangarArt>(this, TEXT("HangarArt"));
	}
	Art->SetOriginalGameRoot(ResolveOriginalGameRoot());

	SimCopterHangarShop::FContext Context;
	Context.Missions = Cast<ASimCopterMissionSystemActor>(
		UGameplayStatics::GetActorOfClass(World, ASimCopterMissionSystemActor::StaticClass()));
	Context.Helicopter = Cast<ASimCopterHelicopterPawn>(
		UGameplayStatics::GetActorOfClass(World, ASimCopterHelicopterPawn::StaticClass()));

	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		USimCopterCareerSubsystem* Career = GameInstance->GetSubsystem<USimCopterCareerSubsystem>();
		if (Career != nullptr)
		{
			// A city entered directly (PIE, or -game with no menu) never ran BeginSession, so the
			// books would otherwise be empty and every catalog row unpriced.
			Career->EnsurePricesLoaded(ResolveOriginalGameRoot());
			if (!Career->IsCareerOpen())
			{
				Career->BeginCareer();
			}

			// Whatever the player is actually flying is on the books by definition.
			if (const ASimCopterHelicopterPawn* Helicopter = Context.Helicopter.Get())
			{
				Career->SetHelicopterOwned(Helicopter->GetHelicopterTypeIndex(), true);
			}
		}
		Context.Career = Career;
	}

	ShellWidget = SNew(SSimCopterHangarMenu)
		.Art(Art)
		.Shop(Context)
		.OnDoneRequested(FSimpleDelegate::CreateUObject(this, &ASimCopterHangar::CloseShell));

	GEngine->GameViewport->AddViewportWidgetContent(ShellWidget.ToSharedRef(), 200);

	ShellController = PlayerController != nullptr ? PlayerController : UGameplayStatics::GetPlayerController(World, 0);
	if (APlayerController* Controller = ShellController.Get())
	{
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Controller->SetInputMode(InputMode);
		Controller->bShowMouseCursor = true;
	}

	bShellOpen = true;
	UE_LOG(LogTemp, Display, TEXT("SimCopter hangar: shell opened."));
}

void ASimCopterHangar::CloseShell()
{
	if (!bShellOpen)
	{
		return;
	}

	if (GEngine != nullptr && GEngine->GameViewport != nullptr && ShellWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ShellWidget.ToSharedRef());
	}
	ShellWidget.Reset();

	if (APlayerController* Controller = ShellController.Get())
	{
		Controller->SetInputMode(FInputModeGameOnly());
		Controller->bShowMouseCursor = false;
	}
	ShellController.Reset();

	bShellOpen = false;

	PushPlayerOutOfDoorway();

	if (const UWorld* World = GetWorld())
	{
		ReopenAllowedTimeSeconds = World->GetTimeSeconds() + ReopenCooldownSeconds;
	}
}

void ASimCopterHangar::PushPlayerOutOfDoorway()
{
	UWorld* World = GetWorld();
	APlayerController* Controller = UGameplayStatics::GetPlayerController(World, 0);
	APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	if (Pawn == nullptr)
	{
		return;
	}

	// Forward is the doorway, so stepping the player along it takes them out of the building and
	// out of the trigger they were standing in. The distance is measured from the front wall,
	// which is DepthTiles/2 out from the actor's own origin.
	const FVector Forward = GetActorForwardVector();
	const FVector Current = Pawn->GetActorLocation();
	const float Push = (SimCopterHangarPlacement::DepthTiles * 0.5f + ExitPushTiles) * TileSizeCm;
	FVector Target = GetActorLocation() + Forward * Push;
	Target.Z = Current.Z;

	if (Pawn->TeleportTo(Target, Forward.Rotation(), /*bIsATest=*/false, /*bNoCheck=*/true))
	{
		return;
	}

	// Blocked (something parked in the doorway): settle for shoving them straight out from where
	// they stand, which still clears the trigger.
	Pawn->TeleportTo(Current + Forward * Push, Forward.Rotation(), /*bIsATest=*/false, /*bNoCheck=*/true);
}
