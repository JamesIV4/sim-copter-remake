// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "UI/SimCopterMapRaster.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace SimCopterMap;

namespace
{
// A flat inland city: every tile open ground on terrain class 0x30 at altitude shade 4, which
// paints the whole view one colour and makes anything drawn over it obvious.
FSimCopterMapCity MakeFlatCity()
{
	FSimCopterMapCity City;
	City.Xbld.SetNumZeroed(MapTiles * MapTiles);
	City.TerrainClass.Init(0x30, MapTiles * MapTiles);
	City.AltitudeShade.Init(4, MapTiles * MapTiles);
	City.OnFire.SetNumZeroed(MapTiles * MapTiles);
	return City;
}

void SetTile(FSimCopterMapCity& City, const int32 X, const int32 Y, const uint8 Xbld)
{
	City.Xbld[Y * MapTiles + X] = Xbld;
}

FSimCopterMapIconSheet MakeIconSheet(const int32 CellSize, const int32 CellCount, const uint8 Fill)
{
	FSimCopterMapIconSheet Sheet;
	Sheet.CellSize = CellSize;
	Sheet.Stride = CellSize * CellCount;
	Sheet.CellCount = CellCount;
	Sheet.Pixels.Init(Fill, Sheet.Stride * CellSize);
	return Sheet;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMapLayoutTest,
	"SimCopter.Map.Layout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMapLayoutTest::RunTest(const FString&)
{
	// dash5.bmp is 185x148 and its buffer 124x98 at (54,13), so the tile view lands at (64,22) -
	// the rect FUN_004a2740 writes to _DAT_00505eb8.
	TestEqual(TEXT("Panel width"), PanelWidth, 185);
	TestEqual(TEXT("Panel height"), PanelHeight, 148);
	TestEqual(TEXT("Buffer right edge"), BufferPanelX + BufferWidth, 178);
	TestEqual(TEXT("Buffer bottom edge"), BufferPanelY + BufferHeight, 111);
	TestEqual(TEXT("Tile view panel X"), BufferPanelX + ViewOriginX, 64);
	TestEqual(TEXT("Tile view panel Y"), BufferPanelY + ViewOriginY, 22);
	TestEqual(TEXT("Tile view right"), BufferPanelX + ViewOriginX + ViewTilesX, 168);
	TestEqual(TEXT("Tile view bottom"), BufferPanelY + ViewOriginY + ViewTilesY, 102);

	// The drawn band is 104 px at every zoom, which is why the row advance is a constant +20.
	for (int32 Zoom = 0; Zoom <= MaxZoom; ++Zoom)
	{
		TestEqual(
			FString::Printf(TEXT("Zoom %d covers the view width"), Zoom),
			(ViewTilesX >> Zoom) << Zoom,
			ViewTilesX);
		TestEqual(
			FString::Printf(TEXT("Zoom %d covers the view height"), Zoom),
			(ViewTilesY >> Zoom) << Zoom,
			ViewTilesY);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMapButtonsTest,
	"SimCopter.Map.Buttons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMapButtonsTest::RunTest(const FString&)
{
	// Two columns 18 px apart and three rows 19 apart, all 15x15 click rects.
	const int32 ExpectedLeft[ButtonCount] = { 9, 27, 9, 27, 9, 27 };
	const int32 ExpectedTop[ButtonCount] = { 54, 54, 73, 73, 92, 92 };
	for (int32 Index = 0; Index < ButtonCount; ++Index)
	{
		const FIntRect Rect = GetButtonRect(Index);
		TestEqual(FString::Printf(TEXT("Button %d left"), Index), Rect.Min.X, ExpectedLeft[Index]);
		TestEqual(FString::Printf(TEXT("Button %d top"), Index), Rect.Min.Y, ExpectedTop[Index]);
		TestEqual(FString::Printf(TEXT("Button %d width"), Index), Rect.Width(), 15);
		TestEqual(FString::Printf(TEXT("Button %d height"), Index), Rect.Height(), 15);
	}

	// mapbttn.bmp is four 16-px columns: left released, right released, left pressed, right
	// pressed. So a right-hand button's pressed cell is the fourth column, not the second.
	TestEqual(TEXT("Button 0 released cell"), GetButtonSourceRect(0, false), FIntRect(0, 0, 16, 16));
	TestEqual(TEXT("Button 0 pressed cell"), GetButtonSourceRect(0, true), FIntRect(32, 0, 48, 16));
	TestEqual(TEXT("Button 1 released cell"), GetButtonSourceRect(1, false), FIntRect(16, 0, 32, 16));
	TestEqual(TEXT("Button 1 pressed cell"), GetButtonSourceRect(1, true), FIntRect(48, 0, 64, 16));
	TestEqual(TEXT("Button 5 released cell"), GetButtonSourceRect(5, false), FIntRect(16, 32, 32, 48));
	TestEqual(TEXT("Button 5 pressed cell"), GetButtonSourceRect(5, true), FIntRect(48, 32, 64, 48));

	// The face is blitted at the click rect's own corner.
	for (int32 Index = 0; Index < ButtonCount; ++Index)
	{
		TestEqual(
			FString::Printf(TEXT("Button %d draws at its rect"), Index),
			GetButtonDrawOrigin(Index),
			GetButtonRect(Index).Min);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMapTileColorsTest,
	"SimCopter.Map.TileColors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMapTileColorsTest::RunTest(const FString&)
{
	FSimCopterMapCity City = MakeFlatCity();
	City.AirportOrigin = FIntPoint(20, 20);

	// The three service buildings are named colours; everything else falls through the network
	// and building bands.
	SetTile(City, 1, 1, 0xd1);
	SetTile(City, 2, 1, 0xd2);
	SetTile(City, 3, 1, 0xd3);
	SetTile(City, 4, 1, 0xd5);
	SetTile(City, 5, 1, 0x07);   // in the 0x06-0x0d park band
	SetTile(City, 6, 1, 0x1d);   // first network id
	SetTile(City, 7, 1, 0x6f);   // last network id
	SetTile(City, 8, 1, 0x70);   // first building id
	SetTile(City, 9, 1, 0x1c);   // still "nothing built": shows terrain

	TestEqual(TEXT("Hospital"), FSimCopterMapRaster::ResolveTileColor(City, 1, 1, 0), Color::Hospital);
	TestEqual(TEXT("Police station"), FSimCopterMapRaster::ResolveTileColor(City, 2, 1, 0), Color::PoliceStation);
	TestEqual(TEXT("Fire station"), FSimCopterMapRaster::ResolveTileColor(City, 3, 1, 0), Color::FireStation);
	TestEqual(TEXT("0xd5 is park"), FSimCopterMapRaster::ResolveTileColor(City, 4, 1, 0), Color::Park);
	TestEqual(TEXT("Park band"), FSimCopterMapRaster::ResolveTileColor(City, 5, 1, 0), Color::Park);
	TestEqual(TEXT("Network low"), FSimCopterMapRaster::ResolveTileColor(City, 6, 1, 0), Color::Network);
	TestEqual(TEXT("Network high"), FSimCopterMapRaster::ResolveTileColor(City, 7, 1, 0), Color::Network);
	TestEqual(TEXT("Building"), FSimCopterMapRaster::ResolveTileColor(City, 8, 1, 0), Color::Building);
	TestEqual(
		TEXT("0x1c still shows ground"),
		FSimCopterMapRaster::ResolveTileColor(City, 9, 1, 0),
		static_cast<uint8>(Color::GroundRampFirst + 4));

	// Terrain shading: class under 10 is water at a flat colour, 0x20-0x2f is the green ramp, and
	// everything else the ground ramp - both shaded by altitude.
	City.TerrainClass[1 * MapTiles + 10] = 0x05;
	City.TerrainClass[1 * MapTiles + 11] = 0x25;
	TestEqual(TEXT("Water"), FSimCopterMapRaster::ResolveTileColor(City, 10, 1, 0), Color::Water);
	TestEqual(
		TEXT("Green ramp shaded"),
		FSimCopterMapRaster::ResolveTileColor(City, 11, 1, 0),
		static_cast<uint8>(Color::LandRampFirst + 4));

	// Off the 128x128 map the original reads zeroed terrain, which is the surrounding ocean.
	TestEqual(TEXT("Off-map is ocean"), FSimCopterMapRaster::ResolveTileColor(City, -5, 40, 0), Color::Water);
	TestEqual(TEXT("Past the edge is ocean"), FSimCopterMapRaster::ResolveTileColor(City, 200, 40, 0), Color::Water);

	// The airport's 4x4 block outranks the network and terrain rules but not the three service
	// buildings, and not a fire.
	TestEqual(TEXT("Airport corner"), FSimCopterMapRaster::ResolveTileColor(City, 20, 20, 0), Color::Airport);
	TestEqual(TEXT("Airport far corner"), FSimCopterMapRaster::ResolveTileColor(City, 23, 23, 0), Color::Airport);
	TestNotEqual(TEXT("Airport is only 4 wide"), FSimCopterMapRaster::ResolveTileColor(City, 24, 20, 0), Color::Airport);

	// A burning cell walks the 16-entry ramp and beats everything, including a building.
	City.OnFire[1 * MapTiles + 8] = 1;
	TestEqual(
		TEXT("Fire step 0"),
		FSimCopterMapRaster::ResolveTileColor(City, 8, 1, 0),
		static_cast<uint8>(Color::FireRampFirst));
	TestEqual(
		TEXT("Fire step 15"),
		FSimCopterMapRaster::ResolveTileColor(City, 8, 1, 15),
		static_cast<uint8>(Color::FireRampFirst + 15));
	TestEqual(
		TEXT("Fire ramp wraps at 16"),
		FSimCopterMapRaster::ResolveTileColor(City, 8, 1, 16),
		static_cast<uint8>(Color::FireRampFirst));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMapZoomTest,
	"SimCopter.Map.Zoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMapZoomTest::RunTest(const FString&)
{
	FSimCopterMapCity City = MakeFlatCity();
	// The block probes use a building four tiles north of the helicopter (68, 64), not the tile under it:
	// the heading needle always starts on the buffer's centre pixel.
	SetTile(City, 68, 64, 0x80);
	const uint8 Ground = static_cast<uint8>(Color::GroundRampFirst + 4);

	FSimCopterMapFrame Frame;
	Frame.City = &City;
	Frame.CentreTile = FIntPoint(64, 64);
	// Point the needle along +X (East) so it only ever occupies the centre row.
	Frame.HeadingX1616 = 1 << 16;
	Frame.HeadingZ1616 = 0;

	FSimCopterMapSettings Settings;
	FSimCopterMapRaster Raster;

	// Zoom 0: one pixel per tile, no grid, no centre marker.
	Settings.Zoom = 0;
	Raster.Render(Frame, Settings);
	TestEqual(TEXT("Zoom 0 origin tile"), Raster.GetViewOriginTile(), FIntPoint(64 + 40, 64 + 52));

	// Zoom 1: a 2x2 block, still no grid and no centre marker.
	Settings.Zoom = 1;
	Raster.Render(Frame, Settings);
	TestEqual(TEXT("Zoom 1 origin tile"), Raster.GetViewOriginTile(), FIntPoint(64 + 20, 64 + 26));

	// Zoom 2: a 4x4 block
	Settings.Zoom = 2;
	Raster.Render(Frame, Settings);
	TestEqual(TEXT("Zoom 2 origin tile"), Raster.GetViewOriginTile(), FIntPoint(64 + 10, 64 + 13));

	// Zoom 3: an 8x8 block
	Settings.Zoom = 3;
	Raster.Render(Frame, Settings);
	TestEqual(TEXT("Zoom 3 origin tile"), Raster.GetViewOriginTile(), FIntPoint(64 + 5, 64 + 6));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMapOverlaysTest,
	"SimCopter.Map.Overlays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMapOverlaysTest::RunTest(const FString&)
{
	FSimCopterMapCity City = MakeFlatCity();
	const FSimCopterMapIconSheet MissionIcons = MakeIconSheet(14, 8, 0x11);
	const FSimCopterMapIconSheet ServiceIcons = MakeIconSheet(10, 3, 0x22);

	FSimCopterMapFrame Frame;
	Frame.City = &City;
	Frame.CentreTile = FIntPoint(64, 64);
	Frame.MissionIcons = &MissionIcons;
	Frame.ServiceIcons = &ServiceIcons;
	// Facing straight along +X (East), so the needle runs right from the centre.
	Frame.HeadingX1616 = 1 << 16;
	Frame.HeadingZ1616 = 0;

	FSimCopterMapSettings Settings;
	Settings.Zoom = 0;

	FSimCopterMapRaster Raster;
	Raster.Render(Frame, Settings);

	// Twenty steps of the facing vector, starting on the centre pixel itself.
	TestEqual(TEXT("Needle at the centre"), Raster.GetPixel(CentreX, CentreY), Color::Heading);
	TestEqual(TEXT("Needle tip"), Raster.GetPixel(CentreX + 19, CentreY), Color::Heading);
	TestNotEqual(TEXT("Needle stops at 20 steps"), Raster.GetPixel(CentreX + 20, CentreY), Color::Heading);

	// A live mission draws two fading rays from the centre: a grey one to where the work is and a
	// red one to where it has to be delivered.
	FSimCopterMapMission Mission;
	Mission.Name = TEXT("Fire #1");
	Mission.EventId = 7;
	Mission.TypeMask = 0x20;   // medevac: icons 1 and 4
	Mission.bActive = true;
	Mission.Tile = FIntPoint(64, 44);   // 20 tiles East
	Mission.Tertiary = FIntPoint(44, 64); // 20 tiles South
	Frame.Missions.Add(Mission);
	Frame.CurrentMission = 0;
	Frame.HeadingX1616 = 0;    // needle out of the way of both rays

	Raster.Render(Frame, Settings);
	TestEqual(TEXT("Primary ray shade"), Raster.GetPixel(CentreX + 10, CentreY), static_cast<uint8>(0x3e));
	TestEqual(TEXT("Secondary ray shade"), Raster.GetPixel(CentreX, CentreY + 10), static_cast<uint8>(0x6a));

	// Test Transport mission line targeting before and after pickup (bBegun false vs true)
	FSimCopterMapMission TransportMission;
	TransportMission.Name = TEXT("Transport #1");
	TransportMission.EventId = 8;
	TransportMission.TypeMask = 0x40;  // Transport
	TransportMission.bActive = true;
	TransportMission.bBegun = false;
	TransportMission.Tile = FIntPoint(64, 44);      // Passenger at 20 tiles East
	TransportMission.Secondary = FIntPoint(44, 64); // Dropoff at 20 tiles South

	FSimCopterMapFrame TransportFrame = Frame;
	TransportFrame.Missions.Reset();
	TransportFrame.Missions.Add(TransportMission);
	TransportFrame.CurrentMission = 0;

	// Before pickup (bBegun = false): Primary line goes to Passenger (East), Secondary line to Dropoff (South)
	FSimCopterMapRaster TransportRaster;
	TransportRaster.Render(TransportFrame, Settings);
	TestEqual(TEXT("Unbegun transport primary ray goes to passenger (East)"), TransportRaster.GetPixel(CentreX + 10, CentreY), static_cast<uint8>(0x3e));
	TestEqual(TEXT("Unbegun transport secondary ray goes to dropoff (South)"), TransportRaster.GetPixel(CentreX, CentreY + 10), static_cast<uint8>(0x6a));

	// After pickup (bBegun = true): Primary line switches to Dropoff (South)
	TransportFrame.Missions[0].bBegun = true;
	TransportRaster.Render(TransportFrame, Settings);
	TestEqual(TEXT("Begun transport primary ray goes to dropoff (South)"), TransportRaster.GetPixel(CentreX, CentreY + 10), static_cast<uint8>(0x3e));

	// FUN_004a4000's table, keyed on the whole type mask rather than its bits.
	int32 Primary = INDEX_NONE;
	int32 Secondary = INDEX_NONE;
	FSimCopterMapRaster::GetMissionIcons(0x20, Primary, Secondary);
	TestEqual(TEXT("Medevac primary icon"), Primary, 1);
	TestEqual(TEXT("Medevac secondary icon"), Secondary, 4);
	FSimCopterMapRaster::GetMissionIcons(0x90, Primary, Secondary);
	TestEqual(TEXT("Boat rescue is its own row"), Primary, 4);
	TestEqual(TEXT("Boat rescue has one icon"), Secondary, INDEX_NONE);
	FSimCopterMapRaster::GetMissionIcons(0x800, Primary, Secondary);
	TestEqual(TEXT("Traffic jam uses one icon twice"), Primary, 5);
	TestEqual(TEXT("Traffic jam secondary"), Secondary, 5);
	FSimCopterMapRaster::GetMissionIcons(0x2, Primary, Secondary);
	TestEqual(TEXT("Speeder bit alone has no icon"), Primary, INDEX_NONE);
	TestEqual(TEXT("Speeder bit alone has no second icon"), Secondary, INDEX_NONE);

	// A service vehicle in view gets a clickable rect; one outside the view does not.
	FSimCopterMapServiceBlip InView;
	InView.Id = 42;
	InView.IconIndex = 1;
	InView.Tile = FIntPoint(64, 64);
	InView.EndTile = FIntPoint(64, 58);
	FSimCopterMapServiceBlip OutOfView;
	OutOfView.Id = 43;
	OutOfView.IconIndex = 0;
	OutOfView.Tile = FIntPoint(126, 126);
	OutOfView.EndTile = FIntPoint(126, 126);
	Frame.ServiceBlips.Add(InView);
	Frame.ServiceBlips.Add(OutOfView);

	Raster.Render(Frame, Settings);
	TestEqual(TEXT("One blip is in view"), Raster.GetServiceHits().Num(), 1);
	if (Raster.GetServiceHits().Num() == 1)
	{
		const FIntRect& Rect = Raster.GetServiceHits()[0].Rect;
		TestEqual(TEXT("Blip hit"), Raster.HitTestServiceBlip(Rect.Min.X + 2, Rect.Min.Y + 2), 42);
		TestEqual(TEXT("Blip miss"), Raster.HitTestServiceBlip(Rect.Min.X - 2, Rect.Min.Y - 2), INDEX_NONE);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMapMissionCycleTest,
	"SimCopter.Map.MissionCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMapMissionCycleTest::RunTest(const FString&)
{
	TArray<FSimCopterMapMission> Missions;
	Missions.SetNum(6);
	auto MakeLive = [&Missions](const int32 Index, const bool bLive, const int32 Category)
	{
		Missions[Index].bActive = bLive;
		Missions[Index].Category = Category;
		Missions[Index].EventId = Index;
	};
	MakeLive(0, true, 0);
	MakeLive(1, false, 0);
	MakeLive(2, true, 2);   // finished: skipped even though it is still flagged active
	MakeLive(3, true, 0);
	MakeLive(4, false, 0);
	MakeLive(5, true, 0);

	TestEqual(TEXT("Next from 0"), FSimCopterMapRaster::FindNextMission(Missions, 0), 3);
	TestEqual(TEXT("Next from 3"), FSimCopterMapRaster::FindNextMission(Missions, 3), 5);
	TestEqual(TEXT("Next from 5 wraps"), FSimCopterMapRaster::FindNextMission(Missions, 5), 0);

	TestEqual(TEXT("Previous from 5"), FSimCopterMapRaster::FindPreviousMission(Missions, 5), 3);
	TestEqual(TEXT("Previous from 3"), FSimCopterMapRaster::FindPreviousMission(Missions, 3), 0);
	TestEqual(TEXT("Previous from 0 wraps"), FSimCopterMapRaster::FindPreviousMission(Missions, 0), 5);

	TArray<FSimCopterMapMission> Lonely;
	Lonely.SetNum(3);
	Lonely[1].bActive = true;
	Lonely[1].EventId = 1;
	TestEqual(TEXT("Next with no other live mission"), FSimCopterMapRaster::FindNextMission(Lonely, 1), 1);
	TestEqual(TEXT("Previous with no other live mission"), FSimCopterMapRaster::FindPreviousMission(Lonely, 1), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMapHeadingOrientationTest,
	"SimCopter.Map.HeadingOrientation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMapHeadingOrientationTest::RunTest(const FString&)
{
	FSimCopterMapCity City = MakeFlatCity();

	auto TestCardinalNeedle = [&](const FVector2D& TileDirection, const FIntPoint& ExpectedTipOffset, const TCHAR* Name)
	{
		FSimCopterMapFrame Frame;
		Frame.City = &City;
		Frame.CentreTile = FIntPoint(64, 64);
		Frame.HeadingX1616 = -FMath::RoundToInt(TileDirection.Y * 65536.0f);
		Frame.HeadingZ1616 = FMath::RoundToInt(TileDirection.X * 65536.0f);

		FSimCopterMapSettings Settings;
		Settings.Zoom = 0;

		FSimCopterMapRaster Raster;
		Raster.Render(Frame, Settings);

		const int32 TipX = CentreX + ExpectedTipOffset.X;
		const int32 TipY = CentreY + ExpectedTipOffset.Y;
		TestEqual(FString::Printf(TEXT("%s needle tip"), Name), Raster.GetPixel(TipX, TipY), Color::Heading);
	};

	// North (LocalDirection = (1, 0), TileDirection = (1, 0)): needle points UP (-Y screen)
	TestCardinalNeedle(FVector2D(1.0f, 0.0f), FIntPoint(0, -19), TEXT("North"));

	// East (LocalDirection = (0, 1), TileDirection = (0, -1)): needle points RIGHT (+X screen)
	TestCardinalNeedle(FVector2D(0.0f, -1.0f), FIntPoint(19, 0), TEXT("East"));

	// South (LocalDirection = (-1, 0), TileDirection = (-1, 0)): needle points DOWN (+Y screen)
	TestCardinalNeedle(FVector2D(-1.0f, 0.0f), FIntPoint(0, 19), TEXT("South"));

	// West (LocalDirection = (0, -1), TileDirection = (0, 1)): needle points LEFT (-X screen)
	TestCardinalNeedle(FVector2D(0.0f, 1.0f), FIntPoint(-19, 0), TEXT("West"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

