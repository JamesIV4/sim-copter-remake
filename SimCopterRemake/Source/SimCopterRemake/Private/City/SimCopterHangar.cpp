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
#include "Engine/Texture2D.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Formats/MaxisMeshReader.h"
#include "Formats/MaxisTextureReader.h"
#include "Game/SimCopterCareerSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Ground/SimCopterPopulationSprite.h"
#include "Materials/MaterialInstanceDynamic.h"
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

// The same lit-texture material the city builder skins its runtime-textured meshes with; its one
// texture parameter is named "Texture".
constexpr const TCHAR* TexturedMaterialPath =
	TEXT("/Game/Materials/M_SimCopterLitTexture.M_SimCopterLitTexture");

// One mesh section's worth of geometry. The shell is split by surface - walls, roof, floor and
// the painted trim - because each takes a different atlas cell (or none at all).
struct FShellSection
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	bool IsEmpty() const { return Triangles.Num() == 0; }

	// One quad, wound both ways: the player walks inside the shell, so every face has to be
	// visible from both sides. (SimCopterPopulationBody's box builder does the same for the same
	// reason.) UVs are given per corner so a wall can tile along its length and stretch over its
	// height without a second mapping pass.
	void AddQuad(
		const FVector& A, const FVector2D& UVA,
		const FVector& B, const FVector2D& UVB,
		const FVector& C, const FVector2D& UVC,
		const FVector& D, const FVector2D& UVD,
		const FLinearColor& Color)
	{
		const FVector Normal = FVector::CrossProduct(B - A, D - A).GetSafeNormal();
		const FVector Tangent = (B - A).GetSafeNormal();

		const int32 Base = Vertices.Num();
		Vertices.Append({ A, B, C, D });
		UVs.Append({ UVA, UVB, UVC, UVD });
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			Normals.Add(Normal);
			VertexColors.Add(Color);
			Tangents.Add(FProcMeshTangent(Tangent, false));
		}

		Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
		Triangles.Append({ Base, Base + 2, Base + 1, Base, Base + 3, Base + 2 });
	}

	void AddTriangle(
		const FVector& A, const FVector2D& UVA,
		const FVector& B, const FVector2D& UVB,
		const FVector& C, const FVector2D& UVC,
		const FLinearColor& Color)
	{
		const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		const FVector Tangent = (B - A).GetSafeNormal();

		const int32 Base = Vertices.Num();
		Vertices.Append({ A, B, C });
		UVs.Append({ UVA, UVB, UVC });
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			Normals.Add(Normal);
			VertexColors.Add(Color);
			Tangents.Add(FProcMeshTangent(Tangent, false));
		}

		Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 1 });
	}
};
}

namespace SimCopterHangarPlacement
{
FIntPoint GetPlotOriginTile(const FIntPoint& AirportOrigin)
{
	return SimCopterAirport::GetTerminalTile(AirportOrigin);
}

FVector2D GetPlotCentreTile(const FIntPoint& AirportOrigin)
{
	// The 2x2 spans tile offsets 1 and 2, so its centre in tile-centre coordinates is +1.5.
	const FIntPoint PlotOrigin = GetPlotOriginTile(AirportOrigin);
	return FVector2D(PlotOrigin.X + 0.5f, PlotOrigin.Y + 0.5f);
}

void GetPlotTiles(const FIntPoint& AirportOrigin, TArray<FIntPoint>& OutTiles)
{
	OutTiles.Reset();
	const FIntPoint PlotOrigin = GetPlotOriginTile(AirportOrigin);
	for (int32 OffsetY = 0; OffsetY < 2; ++OffsetY)
	{
		for (int32 OffsetX = 0; OffsetX < 2; ++OffsetX)
		{
			OutTiles.Emplace(PlotOrigin.X + OffsetX, PlotOrigin.Y + OffsetY);
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

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TexturedMaterialFinder(TexturedMaterialPath);
	if (TexturedMaterialFinder.Succeeded())
	{
		TexturedMaterial = TexturedMaterialFinder.Object;
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

	// The doorway faces +X, which is the actor's forward axis - PlaceAtAirport yaws the whole
	// actor so that axis points at the player's start.
	const float Front = HalfDepth;
	const float Back = -HalfDepth;

	// Four surfaces, four skins: walls, roof, floor, and the painted band over the door.
	FShellSection Walls;
	FShellSection Roof;
	FShellSection Floor;
	FShellSection Trim;

	// Every wall shares one mapping so the texture runs continuously round the building: U is the
	// distance travelled along the wall in texture repeats, V is 1 at the ground and 0 at the
	// eaves - which puts the cell's dark eaves band where the eaves are.
	const float Repeat = FMath::Max(1.0f, TextureRepeatCm);
	auto WallV = [Eaves](const float Z) { return 1.0f - Z / FMath::Max(1.0f, Eaves); };

	// Floor slab, lifted clear of the apron it sits on.
	{
		const float FloorZ = FloorLiftCm;
		const float UDepth = (HalfDepth * 2.0f) / Repeat;
		const float VWidth = (HalfWidth * 2.0f) / Repeat;
		Floor.AddQuad(
			FVector(Back, -HalfWidth, FloorZ), FVector2D(0.0f, 0.0f),
			FVector(Front, -HalfWidth, FloorZ), FVector2D(UDepth, 0.0f),
			FVector(Front, HalfWidth, FloorZ), FVector2D(UDepth, VWidth),
			FVector(Back, HalfWidth, FloorZ), FVector2D(0.0f, VWidth),
			FLinearColor::White);
	}

	// Side walls up to the eaves.
	{
		const float UDepth = (HalfDepth * 2.0f) / Repeat;
		for (const float Side : { -HalfWidth, HalfWidth })
		{
			Walls.AddQuad(
				FVector(Back, Side, 0.0f), FVector2D(0.0f, WallV(0.0f)),
				FVector(Front, Side, 0.0f), FVector2D(UDepth, WallV(0.0f)),
				FVector(Front, Side, Eaves), FVector2D(UDepth, WallV(Eaves)),
				FVector(Back, Side, Eaves), FVector2D(0.0f, WallV(Eaves)),
				FLinearColor::White);
		}
	}

	// Back wall plus its gable.
	{
		const float UWidth = (HalfWidth * 2.0f) / Repeat;
		Walls.AddQuad(
			FVector(Back, -HalfWidth, 0.0f), FVector2D(0.0f, WallV(0.0f)),
			FVector(Back, HalfWidth, 0.0f), FVector2D(UWidth, WallV(0.0f)),
			FVector(Back, HalfWidth, Eaves), FVector2D(UWidth, WallV(Eaves)),
			FVector(Back, -HalfWidth, Eaves), FVector2D(0.0f, WallV(Eaves)),
			FLinearColor::White);
		Walls.AddTriangle(
			FVector(Back, -HalfWidth, Eaves), FVector2D(0.0f, WallV(Eaves)),
			FVector(Back, HalfWidth, Eaves), FVector2D(UWidth, WallV(Eaves)),
			FVector(Back, 0.0f, Apex), FVector2D(UWidth * 0.5f, WallV(Apex)),
			FLinearColor::White);
	}

	// Front wall: two jambs, a lintel, and the gable above them - the hole between is the door.
	{
		const float JambWidth = HalfWidth - HalfDoor;
		const float UJamb = JambWidth / Repeat;
		const float UDoor = (HalfDoor * 2.0f) / Repeat;
		const float UWidth = (HalfWidth * 2.0f) / Repeat;

		Walls.AddQuad(
			FVector(Front, -HalfWidth, 0.0f), FVector2D(0.0f, WallV(0.0f)),
			FVector(Front, -HalfDoor, 0.0f), FVector2D(UJamb, WallV(0.0f)),
			FVector(Front, -HalfDoor, Eaves), FVector2D(UJamb, WallV(Eaves)),
			FVector(Front, -HalfWidth, Eaves), FVector2D(0.0f, WallV(Eaves)),
			FLinearColor::White);
		Walls.AddQuad(
			FVector(Front, HalfDoor, 0.0f), FVector2D(UJamb + UDoor, WallV(0.0f)),
			FVector(Front, HalfWidth, 0.0f), FVector2D(UWidth, WallV(0.0f)),
			FVector(Front, HalfWidth, Eaves), FVector2D(UWidth, WallV(Eaves)),
			FVector(Front, HalfDoor, Eaves), FVector2D(UJamb + UDoor, WallV(Eaves)),
			FLinearColor::White);
		Walls.AddQuad(
			FVector(Front, -HalfDoor, DoorTop), FVector2D(UJamb, WallV(DoorTop)),
			FVector(Front, HalfDoor, DoorTop), FVector2D(UJamb + UDoor, WallV(DoorTop)),
			FVector(Front, HalfDoor, Eaves), FVector2D(UJamb + UDoor, WallV(Eaves)),
			FVector(Front, -HalfDoor, Eaves), FVector2D(UJamb, WallV(Eaves)),
			FLinearColor::White);
		Walls.AddTriangle(
			FVector(Front, -HalfWidth, Eaves), FVector2D(0.0f, WallV(Eaves)),
			FVector(Front, HalfWidth, Eaves), FVector2D(UWidth, WallV(Eaves)),
			FVector(Front, 0.0f, Apex), FVector2D(UWidth * 0.5f, WallV(Apex)),
			FLinearColor::White);
	}

	// Gable roof: two slopes from the eaves to the ridge.
	{
		const float UDepth = (HalfDepth * 2.0f) / Repeat;
		const float SlopeLength = FMath::Sqrt(HalfWidth * HalfWidth + (Apex - Eaves) * (Apex - Eaves));
		const float VSlope = SlopeLength / Repeat;

		Roof.AddQuad(
			FVector(Back, -HalfWidth, Eaves), FVector2D(0.0f, 0.0f),
			FVector(Front, -HalfWidth, Eaves), FVector2D(UDepth, 0.0f),
			FVector(Front, 0.0f, Apex), FVector2D(UDepth, VSlope),
			FVector(Back, 0.0f, Apex), FVector2D(0.0f, VSlope),
			FLinearColor::White);
		Roof.AddQuad(
			FVector(Back, 0.0f, Apex), FVector2D(0.0f, 0.0f),
			FVector(Front, 0.0f, Apex), FVector2D(UDepth, 0.0f),
			FVector(Front, HalfWidth, Eaves), FVector2D(UDepth, VSlope),
			FVector(Back, HalfWidth, Eaves), FVector2D(0.0f, VSlope),
			FLinearColor::White);
	}

	// A band over the doorway, so the building reads as the way in from the air as well. This one
	// stays painted rather than skinned - it is the only part that is not the original's.
	{
		const float BandZ = DoorTop + (Eaves - DoorTop) * 0.35f;
		const float BandHeight = FMath::Max(6.0f, (Eaves - DoorTop) * 0.3f);
		Trim.AddQuad(
			FVector(Front + 2.0f, -HalfDoor, BandZ), FVector2D(0.0f, 0.0f),
			FVector(Front + 2.0f, HalfDoor, BandZ), FVector2D(1.0f, 0.0f),
			FVector(Front + 2.0f, HalfDoor, BandZ + BandHeight), FVector2D(1.0f, 1.0f),
			FVector(Front + 2.0f, -HalfDoor, BandZ + BandHeight), FVector2D(0.0f, 1.0f),
			TrimColor);
	}

	// Without the original artwork the skinned sections fall back to flat paint, which is what the
	// shell looked like before the textures were wired up.
	UMaterialInterface* WallMaterial = ResolveCellMaterial(WallTextureCell);
	UMaterialInterface* RoofMaterial = ResolveCellMaterial(RoofTextureCell);
	UMaterialInterface* FloorMaterial = ResolveCellMaterial(FloorTextureCell);
	if (WallMaterial == nullptr)
	{
		for (FLinearColor& Color : Walls.VertexColors) { Color = WallColor; }
	}
	if (RoofMaterial == nullptr)
	{
		for (FLinearColor& Color : Roof.VertexColors) { Color = RoofColor; }
	}
	if (FloorMaterial == nullptr)
	{
		for (FLinearColor& Color : Floor.VertexColors) { Color = FloorColor; }
	}

	ShellMesh->ClearAllMeshSections();

	const TPair<FShellSection*, UMaterialInterface*> SectionPlan[] = {
		{ &Walls, WallMaterial },
		{ &Roof, RoofMaterial },
		{ &Floor, FloorMaterial },
		{ &Trim, nullptr },
	};

	int32 SectionIndex = 0;
	for (const TPair<FShellSection*, UMaterialInterface*>& Entry : SectionPlan)
	{
		FShellSection& Section = *Entry.Key;
		if (Section.IsEmpty())
		{
			continue;
		}

		// Only the shell proper carries collision; the painted band is a decal on the front wall
		// and would otherwise put an invisible ledge across the doorway.
		const bool bCollides = &Section != &Trim;
		ShellMesh->CreateMeshSection_LinearColor(
			SectionIndex,
			Section.Vertices,
			Section.Triangles,
			Section.Normals,
			Section.UVs,
			Section.VertexColors,
			Section.Tangents,
			bCollides);

		UMaterialInterface* Material = Entry.Value != nullptr ? Entry.Value : ShellMaterial.Get();
		if (Material != nullptr)
		{
			ShellMesh->SetMaterial(SectionIndex, Material);
		}
		++SectionIndex;
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

	// The hangar takes the terminal's plot, so the offset is fixed - the middle of the block.
	const FVector2D PlotCentre = GetPlotCentreTile(Origin);
	const FVector2D PlotOffset(PlotCentre.X - Origin.X, PlotCentre.Y - Origin.Y);

	// Height comes straight off the pad table. FUN_004829f0 flattens the whole 4x4 to the terminal
	// tile's one height-map sample, so the plot is level with every pad by construction and there
	// is nothing to trace for - tracing here would only find the terminal's own roof.
	const FVector Location = PadOrigin + AxisX * PlotOffset.X + AxisY * PlotOffset.Y;

	// Take the original's airport building off the plot first. Its base (object 0x165) is a flat
	// 2x2 slab sitting exactly on the apron, so leaving it there z-fights with the hangar floor -
	// and its walls would stand inside ours. No rubble: this is a site being built on, not a fire.
	if (CityActor != nullptr)
	{
		TArray<FIntPoint> PlotTiles;
		GetPlotTiles(Origin, PlotTiles);

		TArray<FIntPoint> Cleared;
		int32 DemolishedCount = 0;
		for (const FIntPoint& Tile : PlotTiles)
		{
			if (CityActor->DemolishBuildingAtTile(Tile.X, Tile.Y, Cleared, /*bLeaveRubble=*/false))
			{
				Traffic->ClearXbldTiles(Cleared);
				++DemolishedCount;
			}
		}

		UE_LOG(LogTemp, Display,
			TEXT("SimCopter hangar: cleared %d building(s) off the terminal plot at (%d, %d)."),
			DemolishedCount,
			GetPlotOriginTile(Origin).X,
			GetPlotOriginTile(Origin).Y);
	}

	// Turn the doorway toward the player's start, square with the city grid so the doors open onto
	// a pad row rather than across a corner of the ring.
	const float Yaw = GetSnappedFacingYawDegrees(Location, FacingTarget);
	SetActorLocationAndRotation(Location, FRotator(0.0f, Yaw, 0.0f), /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	// TileSizeCm may have changed since the constructor built the shell.
	BuildShellMesh();

	UE_LOG(LogTemp, Display,
		TEXT("SimCopter hangar: standing on the airport terminal plot of block (%d, %d) at %s facing %.0f deg."),
		Origin.X,
		Origin.Y,
		*Location.ToCompactString(),
		Yaw);

	return true;
}

UMaterialInterface* ASimCopterHangar::ResolveCellMaterial(const int32 AtlasCell)
{
	using namespace SimCopterHangarPlacement;

	if (!bUseOriginalTextures || TexturedMaterial == nullptr)
	{
		return nullptr;
	}

	if (const TObjectPtr<UMaterialInterface>* Existing = CellMaterials.Find(AtlasCell))
	{
		return Existing->Get();
	}

	// A cached miss is stored so a missing install is only looked for once per cell.
	CellMaterials.Add(AtlasCell, nullptr);

	const FString RootPath = ResolveOriginalGameRoot();
	if (RootPath.IsEmpty())
	{
		return nullptr;
	}

	// The composite pages are palette-indexed, and the palette that decodes them is the GEO pack's
	// own CMAP - the same pairing the city builder uses (sim3d1.max + BMP/SIM3D.BMP).
	FMaxisMeshFile MeshFile;
	FString Error;
	if (!FMaxisMeshReader::LoadMeshFileFromFile(FPaths::Combine(RootPath, TEXT("GEO/sim3d1.max")), MeshFile, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter hangar: no palette for the wall textures - %s"), *Error);
		return nullptr;
	}

	FMaxisCompositeBitmap Pages;
	if (!FMaxisTextureReader::LoadCompositeBitmapFromFile(
			FPaths::Combine(RootPath, TEXT("BMP/SIM3D.BMP")),
			MeshFile.ColorMap,
			Pages,
			Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter hangar: could not read the texture pages - %s"), *Error);
		return nullptr;
	}

	const FMaxisTextureImage* Page = Pages.FindImage(TexturePage);
	if (Page == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter hangar: SIM3D.BMP has no page %d."), TexturePage);
		return nullptr;
	}

	FMaxisTextureImage CellImage;
	if (!FMaxisTextureReader::ExtractAtlasTile(*Page, AtlasCell, CellImage, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter hangar: %s"), *Error);
		return nullptr;
	}

	UTexture2D* Texture = FSimCopterPopulationSprite::CreateTextureFromImage(this, CellImage, TEXT("SimCopterHangarSkin"));
	if (Texture == nullptr)
	{
		return nullptr;
	}

	// A 32x32 cell tiled across a wall: wrap both ways, and keep the sprite path's nearest filter
	// so the hangar stays as crunchy as the buildings around it.
	Texture->AddressX = TA_Wrap;
	Texture->AddressY = TA_Wrap;
	Texture->UpdateResource();

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(TexturedMaterial, this);
	if (Material == nullptr)
	{
		return nullptr;
	}
	Material->SetTextureParameterValue(TEXT("Texture"), Texture);

	CellMaterials.Add(AtlasCell, Material);
	return Material;
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

	TSharedPtr<SSimCopterHangarMenu> HangarMenu;
	ShellWidget = SAssignNew(HangarMenu, SSimCopterHangarMenu)
		.Art(Art)
		.Shop(Context)
		.OnDoneRequested(FSimpleDelegate::CreateUObject(this, &ASimCopterHangar::CloseShell));

	GEngine->GameViewport->AddViewportWidgetContent(ShellWidget.ToSharedRef(), 200);

	ShellController = PlayerController != nullptr ? PlayerController : UGameplayStatics::GetPlayerController(World, 0);
	if (APlayerController* Controller = ShellController.Get())
	{
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetWidgetToFocus(HangarMenu.IsValid() ? HangarMenu->GetInitialFocusWidget() : nullptr);
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
