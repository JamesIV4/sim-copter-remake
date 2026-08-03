// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSimCopterMapPanel.h"

#include "City/SimCity2000CityActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "Missions/SimCopterMissionSystem.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "UI/SimCopterMapArt.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

using namespace SimCopterMap;

namespace
{
const TCHAR* const OriginalPanelBitmapFile = TEXT("DASH5.BMP");
const TCHAR* const UpscaledPanelFile = TEXT("DASH5-upscaled.png");
const TCHAR* const ButtonBitmapFile = TEXT("MAPBTTN.BMP");
const TCHAR* const RasterTextureKey = TEXT("SimCopterMapBuffer");

// FUN_004547a0 rasterises on every other frame of the original's 0.05 s loop.
constexpr double RasterIntervalSeconds = 0.1;

// Slate's font ascender leaves the visible capitals low in the reconstructed display well.
// Lift the label without changing FUN_00460e30's decoded 145x13 text rectangle.
constexpr float MissionLabelYOffset = -2.0f;

}

void SSimCopterMapPanel::Construct(const FArguments& InArgs)
{
	Pawn = InArgs._Pawn;
	Art = InArgs._Art;
	Scale = FMath::Max(0.5f, InArgs._Scale);

	LoadArt();

	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

	// The rasterised buffer, drawn over the panel art's window at (54,13).
	RasterBrush = MakeShared<FSlateBrush>();
	RasterBrush->ImageSize = FVector2D(BufferWidth, BufferHeight);
	RasterBrush->DrawAs = ESlateBrushDrawType::Image;
	Canvas->AddSlot()
		.Offset(FMargin(BufferPanelX * Scale, BufferPanelY * Scale, BufferWidth * Scale, BufferHeight * Scale))
		.Alignment(FVector2D::ZeroVector)
		[
			SAssignNew(RasterImage, SImage).Image(RasterBrush.Get())
		];

	for (int32 ButtonIndex = 0; ButtonIndex < ButtonCount; ++ButtonIndex)
	{
		const FIntPoint Origin = GetButtonDrawOrigin(ButtonIndex);
		const FIntRect Source = GetButtonSourceRect(ButtonIndex, /*bPressed=*/false);
		Canvas->AddSlot()
			.Offset(FMargin(
				Origin.X * Scale,
				Origin.Y * Scale,
				Source.Width() * Scale,
				Source.Height() * Scale))
			.Alignment(FVector2D::ZeroVector)
			[
				SAssignNew(ButtonImages[ButtonIndex], SImage)
				.Image(GetButtonBrush(ButtonIndex, false))
				.Visibility(EVisibility::Visible)
				.ToolTipText(GetButtonToolTipText(ButtonIndex))
			];
	}

	// The mission name band the original clears and prints into every eight frames.
	Canvas->AddSlot()
		.Offset(FMargin(
			LabelLeft * Scale,
			(LabelTop + MissionLabelYOffset) * Scale,
			(LabelRight - LabelLeft) * Scale,
			(LabelBottom - LabelTop) * Scale))
		.Alignment(FVector2D::ZeroVector)
		[
			SAssignNew(MissionLabel, STextBlock)
			// The original prints a 12-pixel bitmap font into a 13-pixel band. A Slate point size
			// renders taller than its number, so the band height is what the size is fitted to
			// rather than FUN_00460e30's 12.
			.Font(FCoreStyle::GetDefaultFontStyle(
				TEXT("Bold"),
				FMath::RoundToInt((LabelBottom - LabelTop) * Scale * 0.7f)))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.94f, 0.94f, 0.94f)))
			.Justification(ETextJustify::Center)
			.Text(FText::GetEmpty())
		];

	const FSlateBrush* PanelBrush = nullptr;
	if (USimCopterHangarArt* ArtObject = Art.Get())
	{
		PanelBrush = ArtObject->GetBundledSlateImage(UpscaledPanelFile);
		if (PanelBrush == nullptr)
		{
			// Keep the original available as a fallback. Keying index 254 makes its cyan top-left
			// corner transparent so the cockpit shows through the panel's cut corner.
			PanelBrush = ArtObject->GetBitmap(OriginalPanelBitmapFile, /*bColorKeyed=*/true);
		}
	}

	// The viewport slot anchors this panel directly to the bottom-left corner.
	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(PanelWidth * Scale)
		.HeightOverride(PanelHeight * Scale)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				// The 626x500 replacement fills DASH5's original 185x148 page-space rectangle.
				// All live raster, label and button layers therefore retain their decoded positions.
				PanelBrush != nullptr ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(PanelBrush)) : SNullWidget::NullWidget
			]
			+ SOverlay::Slot()
			[
				Canvas
			]
		]
	];

	RefreshButtonBrushes();
}

void SSimCopterMapPanel::SetPawn(TWeakObjectPtr<ASimCopterHelicopterPawn> InPawn)
{
	Pawn = InPawn;
}

ASimCopterMissionSystemActor* SSimCopterMapPanel::GetMissionSystem() const
{
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	if (Helicopter == nullptr || Helicopter->GetWorld() == nullptr)
	{
		return nullptr;
	}
	for (TActorIterator<ASimCopterMissionSystemActor> It(Helicopter->GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

ASimCopterTrafficSystemActor* SSimCopterMapPanel::GetTrafficSystem() const
{
	const ASimCopterHelicopterPawn* Helicopter = GetPawn();
	if (Helicopter == nullptr || Helicopter->GetWorld() == nullptr)
	{
		return nullptr;
	}
	for (TActorIterator<ASimCopterTrafficSystemActor> It(Helicopter->GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void SSimCopterMapPanel::LoadArt()
{
	if (bArtLoaded)
	{
		return;
	}

	USimCopterHangarArt* ArtObject = Art.Get();
	if (ArtObject == nullptr)
	{
		return;
	}

	const FString Root = ArtObject->GetOriginalGameRoot();
	if (!FSimCopterMapArt::LoadPalette(Root, Palette))
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter map: could not read the game palette from DASH5.BMP."));
		return;
	}

	FSimCopterMapArt::LoadIconSheet(
		Root, FSimCopterMapArt::MissionIconPage, FSimCopterMapArt::MissionIconCells, MissionIcons);
	FSimCopterMapArt::LoadIconSheet(
		Root, FSimCopterMapArt::ServiceIconPage, FSimCopterMapArt::ServiceIconCells, ServiceIcons);

	bArtLoaded = true;
}

void SSimCopterMapPanel::EnsureCityData()
{
	ASimCopterTrafficSystemActor* TrafficSystem = GetTrafficSystem();
	if (TrafficSystem == nullptr)
	{
		return;
	}

	ASimCity2000CityActor* CityActor = TrafficSystem->GetCityActor();
	if (CityActor == nullptr)
	{
		return;
	}
	if (CityActor == CachedCityActor.Get() && CityData.IsValid())
	{
		return;
	}

	TArray<uint8> TerrainClasses;
	TArray<uint8> AltitudeShades;
	if (!CityActor->TryGetMapTerrainGrids(TerrainClasses, AltitudeShades))
	{
		return;
	}

	CityData.TerrainClass = MoveTemp(TerrainClasses);
	CityData.AltitudeShade = MoveTemp(AltitudeShades);
	CityData.Xbld.SetNumZeroed(MapTiles * MapTiles);
	for (int32 TileY = 0; TileY < MapTiles; ++TileY)
	{
		for (int32 TileX = 0; TileX < MapTiles; ++TileX)
		{
			CityData.Xbld[TileY * MapTiles + TileX] =
				static_cast<uint8>(TrafficSystem->GetXbldTileId(TileX, TileY));
		}
	}
	CityData.OnFire.SetNumZeroed(MapTiles * MapTiles);
	CityData.AirportOrigin = TrafficSystem->GetAirportOriginTile();

	CachedCityActor = CityActor;
}

void SSimCopterMapPanel::RefreshFireTiles()
{
	if (CityData.OnFire.Num() != MapTiles * MapTiles)
	{
		return;
	}
	FMemory::Memzero(CityData.OnFire.GetData(), CityData.OnFire.Num());

	ASimCopterMissionSystemActor* MissionSystem = GetMissionSystem();
	if (MissionSystem == nullptr)
	{
		return;
	}

	// The original reads bit 0x20 off each tile's scene record, which the fire code sets when it
	// puts a flame on the cell. The remake's flame list is the same set.
	TArray<TPair<FIntPoint, int32>> FlameTiles;
	MissionSystem->GetActiveFlameTiles(FlameTiles);
	for (const TPair<FIntPoint, int32>& Entry : FlameTiles)
	{
		const FIntPoint& Tile = Entry.Key;
		if (Tile.X >= 0 && Tile.Y >= 0 && Tile.X < MapTiles && Tile.Y < MapTiles)
		{
			CityData.OnFire[Tile.Y * MapTiles + Tile.X] = 1;
		}
	}
}

bool SSimCopterMapPanel::BuildFrame(FSimCopterMapFrame& OutFrame)
{
	ASimCopterHelicopterPawn* Helicopter = GetPawn();
	ASimCopterTrafficSystemActor* TrafficSystem = GetTrafficSystem();
	if (Helicopter == nullptr || TrafficSystem == nullptr || !CityData.IsValid())
	{
		return false;
	}

	OutFrame.City = &CityData;
	OutFrame.MissionIcons = MissionIcons.IsValid() ? &MissionIcons : nullptr;
	OutFrame.ServiceIcons = ServiceIcons.IsValid() ? &ServiceIcons : nullptr;

	int32 TileX = 0;
	int32 TileY = 0;
	if (TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(Helicopter->GetActorLocation(), TileX, TileY))
	{
		OutFrame.CentreTile = FIntPoint(TileX, TileY);
	}

	// The needle steps a 16.16 unit vector out from the centre; in North-Up orientation,
	// Screen X (Right, East) = -TileDirection.Y, Screen -Y (Up, North) = +TileDirection.X.
	FVector2D TileDirection = FVector2D::ZeroVector;
	if (TrafficSystem->TryGetPeopleTileDirection(Helicopter->GetActorForwardVector(), TileDirection))
	{
		OutFrame.HeadingX1616 = -FMath::RoundToInt(TileDirection.Y * 65536.0f);
		OutFrame.HeadingZ1616 = FMath::RoundToInt(TileDirection.X * 65536.0f);
	}

	if (const ASimCopterMissionSystemActor* MissionSystem = GetMissionSystem())
	{
		const TArray<SimCopterMissions::FSimCopterMissionRecord>& Records = MissionSystem->GetMissionRecords();
		OutFrame.Missions.Reserve(Records.Num());
		for (const SimCopterMissions::FSimCopterMissionRecord& Record : Records)
		{
			FSimCopterMapMission Mission;
			Mission.Name = Record.Name;
			Mission.EventId = Record.EventId;
			Mission.TypeMask = Record.TypeMask;
			Mission.Category = Record.Category;
			Mission.bActive = Record.bActive;
			Mission.bBegun = MissionSystem->IsMissionBegun(Record);
			Mission.Tile = FIntPoint(Record.TileX, Record.TileY);
			Mission.Secondary = FIntPoint(Record.SecondaryX, Record.SecondaryY);
			Mission.Tertiary = FIntPoint(Record.TertiaryX, Record.TertiaryY);
			OutFrame.Missions.Add(MoveTemp(Mission));
		}
	}

	// Re-resolve the selection by event id, so a record moving slots keeps it, and adopt the
	// first live mission when there is none - which is the state the original's mission layer
	// leaves DAT_0057f9d8 in as soon as a job is announced.
	OutFrame.CurrentMission = INDEX_NONE;
	for (int32 Index = 0; Index < OutFrame.Missions.Num(); ++Index)
	{
		if (OutFrame.Missions[Index].EventId == CurrentMissionEventId && OutFrame.Missions[Index].IsSelectable())
		{
			OutFrame.CurrentMission = Index;
			break;
		}
	}
	if (OutFrame.CurrentMission == INDEX_NONE)
	{
		for (int32 Index = 0; Index < OutFrame.Missions.Num(); ++Index)
		{
			if (OutFrame.Missions[Index].IsSelectable())
			{
				OutFrame.CurrentMission = Index;
				break;
			}
		}
	}
	CurrentMissionEventId = OutFrame.Missions.IsValidIndex(OutFrame.CurrentMission)
		? OutFrame.Missions[OutFrame.CurrentMission].EventId
		: INDEX_NONE;

	TArray<ASimCopterTrafficSystemActor::FServiceVehicleView> Vehicles;
	TrafficSystem->GetActiveServiceVehicles(Vehicles);
	OutFrame.ServiceBlips.Reserve(Vehicles.Num());
	for (const ASimCopterTrafficSystemActor::FServiceVehicleView& Vehicle : Vehicles)
	{
		FSimCopterMapServiceBlip Blip;
		// Service and slot pack into one id the way the original's table keys on the vehicle's
		// object id.
		Blip.Id = Vehicle.Service * 100 + Vehicle.SlotIndex;
		Blip.IconIndex = Vehicle.Service;
		Blip.Tile = Vehicle.Tile;
		Blip.EndTile = Vehicle.DestinationTile;
		OutFrame.ServiceBlips.Add(Blip);
	}

	return true;
}

void SSimCopterMapPanel::RunRasterPass()
{
	EnsureCityData();
	if (!CityData.IsValid() || Palette.Num() < 256)
	{
		return;
	}
	RefreshFireTiles();

	FSimCopterMapFrame Frame;
	if (!BuildFrame(Frame))
	{
		return;
	}

	Raster.Render(Frame, Settings);
	RefreshMissionLabel(Frame);

	USimCopterHangarArt* ArtObject = Art.Get();
	if (ArtObject == nullptr)
	{
		return;
	}

	UTexture2D* Texture = FSimCopterMapArt::UpdateRasterTexture(
		ArtObject,
		ArtObject->FindRuntimeTexture(RasterTextureKey),
		Raster.GetPixels(),
		Palette);
	if (Texture == nullptr)
	{
		return;
	}
	ArtObject->RegisterRuntimeTexture(RasterTextureKey, Texture);

	if (RasterBrush.IsValid() && RasterBrush->GetResourceObject() != Texture)
	{
		RasterBrush->SetResourceObject(Texture);
		if (RasterImage.IsValid())
		{
			RasterImage->SetImage(RasterBrush.Get());
		}
	}
}

void SSimCopterMapPanel::RefreshMissionLabel(const FSimCopterMapFrame& Frame)
{
	if (!MissionLabel.IsValid())
	{
		return;
	}
	const FString Name = Frame.Missions.IsValidIndex(Frame.CurrentMission)
		? Frame.Missions[Frame.CurrentMission].Name
		: FString();
	MissionLabel->SetText(FText::FromString(Name));
}

void SSimCopterMapPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	LoadArt();

	if (InCurrentTime >= NextRasterTime)
	{
		NextRasterTime = InCurrentTime + RasterIntervalSeconds;
		RunRasterPass();
	}
}

// --- input ---------------------------------------------------------------------------------------

bool SSimCopterMapPanel::TryGetPanelPoint(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent,
	FIntPoint& OutPoint) const
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FIntPoint Point(
		FMath::FloorToInt(static_cast<float>(Local.X) / Scale),
		FMath::FloorToInt(static_cast<float>(Local.Y) / Scale));
	OutPoint = Point;
	return Point.X >= 0 && Point.Y >= 0 && Point.X < PanelWidth && Point.Y < PanelHeight;
}

FReply SSimCopterMapPanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	FIntPoint Point;
	if (!TryGetPanelPoint(MyGeometry, MouseEvent, Point))
	{
		return FReply::Unhandled();
	}

	// FUN_00454ad0 tests the map window first, then the two toggles, then the four momentary
	// buttons - in that order, so a click in the window never reaches a button.
	if (Point.X >= BufferPanelX && Point.X <= BufferPanelX + BufferWidth &&
		Point.Y >= BufferPanelY && Point.Y <= BufferPanelY + BufferHeight)
	{
		if (Settings.bShowServiceBlips)
		{
			Raster.HitTestServiceBlip(Point.X - BufferPanelX, Point.Y - BufferPanelY);
		}
		return FReply::Handled();
	}

	// The two toggles act on the press, not the release.
	for (int32 ButtonIndex = static_cast<int32>(EButton::ToggleMissionBlips); ButtonIndex < ButtonCount; ++ButtonIndex)
	{
		const FIntRect Rect = GetButtonRect(ButtonIndex);
		if (Point.X >= Rect.Min.X && Point.Y >= Rect.Min.Y && Point.X < Rect.Max.X && Point.Y < Rect.Max.Y)
		{
			PressButton(ButtonIndex);
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
	}

	for (int32 ButtonIndex = 0; ButtonIndex < static_cast<int32>(EButton::ToggleMissionBlips); ++ButtonIndex)
	{
		const FIntRect Rect = GetButtonRect(ButtonIndex);
		if (Point.X >= Rect.Min.X && Point.Y >= Rect.Min.Y && Point.X < Rect.Max.X && Point.Y < Rect.Max.Y)
		{
			PressButton(ButtonIndex);
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
	}

	return FReply::Unhandled();
}

FReply SSimCopterMapPanel::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || PressedButton == INDEX_NONE)
	{
		return FReply::Unhandled();
	}

	FIntPoint Point;
	TryGetPanelPoint(MyGeometry, MouseEvent, Point);
	const FIntRect Rect = GetButtonRect(PressedButton);
	const bool bInside =
		Point.X >= Rect.Min.X && Point.Y >= Rect.Min.Y && Point.X < Rect.Max.X && Point.Y < Rect.Max.Y;

	ReleaseButton(PressedButton, bInside);
	return FReply::Handled().ReleaseMouseCapture();
}

void SSimCopterMapPanel::PressButton(const int32 ButtonIndex)
{
	// The toggles flip on the way down and stay latched; the other four only show as held.
	if (ButtonIndex == static_cast<int32>(EButton::ToggleMissionBlips))
	{
		Settings.bShowMissionBlips = !Settings.bShowMissionBlips;
	}
	else if (ButtonIndex == static_cast<int32>(EButton::ToggleServiceBlips))
	{
		Settings.bShowServiceBlips = !Settings.bShowServiceBlips;
	}
	else
	{
		PressedButton = ButtonIndex;
	}

	RefreshButtonBrushes();
}

void SSimCopterMapPanel::ReleaseButton(const int32 ButtonIndex, const bool bInsideRect)
{
	PressedButton = INDEX_NONE;

	// FUN_00454c40 only fires the action when the release lands back inside the rect the press
	// started in, so dragging off a button cancels it.
	if (bInsideRect)
	{
		switch (static_cast<EButton>(ButtonIndex))
		{
		case EButton::ZoomOut:
			ZoomOut();
			break;
		case EButton::ZoomIn:
			ZoomIn();
			break;
		case EButton::PreviousMission:
			SelectPreviousMission();
			break;
		case EButton::NextMission:
			SelectNextMission();
			break;
		default:
			break;
		}
	}

	RefreshButtonBrushes();
}

void SSimCopterMapPanel::ZoomIn()
{
	Settings.Zoom = FMath::Min(Settings.Zoom + 1, MaxZoom);
}

void SSimCopterMapPanel::ZoomOut()
{
	Settings.Zoom = FMath::Max(Settings.Zoom - 1, 0);
}

void SSimCopterMapPanel::SelectPreviousMission()
{
	FSimCopterMapFrame Frame;
	if (!BuildFrame(Frame))
	{
		return;
	}
	const int32 Selected = FSimCopterMapRaster::FindPreviousMission(Frame.Missions, Frame.CurrentMission);
	if (Frame.Missions.IsValidIndex(Selected))
	{
		CurrentMissionEventId = Frame.Missions[Selected].EventId;
	}
}

void SSimCopterMapPanel::SelectNextMission()
{
	FSimCopterMapFrame Frame;
	if (!BuildFrame(Frame))
	{
		return;
	}
	const int32 Selected = FSimCopterMapRaster::FindNextMission(Frame.Missions, Frame.CurrentMission);
	if (Frame.Missions.IsValidIndex(Selected))
	{
		CurrentMissionEventId = Frame.Missions[Selected].EventId;
	}
}

bool SSimCopterMapPanel::IsButtonLatched(const int32 ButtonIndex) const
{
	// A toggle draws its pressed face while it is on, which is how the original leaves both of
	// them looking held at startup - it starts with both switched on.
	if (ButtonIndex == static_cast<int32>(EButton::ToggleMissionBlips))
	{
		return Settings.bShowMissionBlips;
	}
	if (ButtonIndex == static_cast<int32>(EButton::ToggleServiceBlips))
	{
		return Settings.bShowServiceBlips;
	}
	return false;
}

const FSlateBrush* SSimCopterMapPanel::GetButtonBrush(const int32 ButtonIndex, const bool bPressed) const
{
	USimCopterHangarArt* ArtObject = Art.Get();
	if (ArtObject == nullptr)
	{
		return nullptr;
	}
	return ArtObject->GetSubImage(ButtonBitmapFile, GetButtonSourceRect(ButtonIndex, bPressed), /*bColorKeyed=*/true);
}

void SSimCopterMapPanel::RefreshButtonBrushes()
{
	for (int32 ButtonIndex = 0; ButtonIndex < ButtonCount; ++ButtonIndex)
	{
		if (!ButtonImages[ButtonIndex].IsValid())
		{
			continue;
		}
		const bool bPressed = (PressedButton == ButtonIndex) || IsButtonLatched(ButtonIndex);
		if (const FSlateBrush* Brush = GetButtonBrush(ButtonIndex, bPressed))
		{
			ButtonImages[ButtonIndex]->SetImage(Brush);
		}
	}
}
