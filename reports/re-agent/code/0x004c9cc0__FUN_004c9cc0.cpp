#include <cstddef>
#include <cstdint>

struct FSimCopterOriginalPerson
{
	std::byte Pad0000[0x12e];
	int16_t FigureId; // +0x12e
	std::byte Pad0130[0x1cc - 0x130];
	int32_t WorldX; // +0x1cc
	int32_t WorldY; // +0x1d0
	int32_t WorldZ; // +0x1d4
};

static_assert(offsetof(FSimCopterOriginalPerson, FigureId) == 0x12e);
static_assert(offsetof(FSimCopterOriginalPerson, WorldX) == 0x1cc);
static_assert(offsetof(FSimCopterOriginalPerson, WorldY) == 0x1d0);
static_assert(offsetof(FSimCopterOriginalPerson, WorldZ) == 0x1d4);

struct FSimCopterSceneCell
{
	uint8_t Flags; // +0x00
};

extern int32_t DAT_0051ac58;
extern FSimCopterSceneCell* DAT_005d9200[0x10000];

extern int32_t FUN_0042de60(int32_t DebugInput);
extern int32_t FUN_004c92a0(uint16_t TileX, uint16_t TileY);
extern int32_t FUN_004ae7a0(int32_t WorldX, int32_t WorldZ, void* OutFlatFlag);
extern int32_t FUN_004c82c0(int32_t WorldX, int32_t CurrentWorldY, int32_t WorldZ);

int32_t FUN_004c9cc0(uint16_t TileX, uint16_t TileY, FSimCopterOriginalPerson* Person)
{
	constexpr int16_t PlayerFigureId = 32000;
	constexpr int32_t RoofHeightGate = 0x140000;
	constexpr uint8_t SceneCellBlocksAmbientPeople = 0x20;

	if ((((Person != nullptr) && (Person->FigureId == PlayerFigureId)) && (DAT_0051ac58 != 0)) &&
		(FUN_0042de60(1) != 0))
	{
		return 1;
	}

	const int16_t DensityAllowsAmbientPeople = static_cast<int16_t>(FUN_004c92a0(TileX, TileY));
	if (DensityAllowsAmbientPeople != 0)
	{
		if (Person != nullptr)
		{
			const int32_t TerrainY = FUN_004ae7a0(Person->WorldX, Person->WorldZ, nullptr);
			const int32_t WalkSurfaceY = FUN_004c82c0(Person->WorldX, Person->WorldY, Person->WorldZ);

			if ((RoofHeightGate < WalkSurfaceY - TerrainY) && (WalkSurfaceY <= Person->WorldY))
			{
				return 1;
			}
		}

		return 0;
	}

	if (Person != nullptr)
	{
		if (Person->FigureId == PlayerFigureId)
		{
			return 1;
		}

		if (Person != nullptr)
		{
			return 1;
		}
	}

	const uint32_t SceneCellIndex = (uint32_t(TileX & 0xff) * 0x100) + uint32_t(TileY & 0xff);
	if ((DAT_005d9200[SceneCellIndex]->Flags & SceneCellBlocksAmbientPeople) == 0)
	{
		return 1;
	}

	return 0;
}