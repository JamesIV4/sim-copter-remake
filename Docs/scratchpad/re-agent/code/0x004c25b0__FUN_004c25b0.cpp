#include <cstddef>
#include <cstdint>

/*
RE_AGENT_NOTE
purpose: Runs scripted ambient person spawns for special XBLD building ids during the city ambient scan: d1/d2 service pedestrians, d7 baseball batter/fielders, and db scripted building person.
use: Port in the FUN_004c2550 ambient tile path before generic FUN_004c2450 attempts; depends on XBLD tile lookup, FUN_004c1fb0 spawn gate, FUN_004c2260/FUN_004c20b0 spawn helpers, FUN_004c2ef0 free-slot probe, and FUN_004cea00 people RNG.
evidence: Function masks TileX/TileY to bytes, reads DAT_005910b0 or near-camera fallback tile 0xf6, switches on ids 0xd1/0xd2/0xd7/0xdb, calls baseball BHAV ids 0x4b5/0x4b6 at fixed 16.16 offsets, and calls db BHAV id 0x4b2 with flags 0xffffffff at offsets 0x80000/0x200000.
caveats: FUN_004c1fb0 and FUN_004c20b0 lack verified semantic names here; Ghidra renders immediate 0x00460000 as &DAT_00460000, but call sites use it as a 16.16 local offset. Off-grid fallback relies on DAT_005d91d0/DAT_005d91d4 camera/current tile globals.
*/

struct FSimCopterOriginalPerson
{
	std::byte Pad0000[0x160];
	int16_t FigureClothesOffset; // +0x160
};

static_assert(offsetof(FSimCopterOriginalPerson, FigureClothesOffset) == 0x160);

extern uint8_t* DAT_005910b0[0x80];
extern int16_t DAT_005d91d0;
extern int16_t DAT_005d91d4;
extern uint16_t DAT_0058dc3a;

extern int32_t FUN_004c1fb0(uint16_t TileX, uint16_t TileY);
extern uint16_t FUN_004cea00(uint32_t Bound);
extern void FUN_004c2260(int32_t BehaviorClass, int32_t InitialPersonState, uint16_t TileX, uint16_t TileY, uint32_t MissionEventId, int32_t Unknown6);
extern FSimCopterOriginalPerson* FUN_004c2ef0();
extern int16_t FUN_004c20b0(uint16_t BuildingId, uint16_t BhavId, uint32_t SpawnModeOrFlags, uint16_t TileX, uint16_t TileY, int32_t Unknown6, int32_t LocalX16_16, int32_t LocalZ16_16);

void FUN_004c25b0(uint16_t TileX, uint16_t TileY)
{
	uint16_t BuildingId;
	uint16_t TileXByte = TileX & 0x00ff;
	uint16_t TileYByte = TileY & 0x00ff;

	if ((TileXByte < 0x80) && (TileYByte < 0x80))
	{
		BuildingId = DAT_005910b0[static_cast<int16_t>(TileXByte)][static_cast<int16_t>(TileYByte)];
	}
	else
	{
		BuildingId = static_cast<uint16_t>(static_cast<int16_t>(TileXByte - DAT_005d91d0) >> 0x0f);
		if ((1 < static_cast<int16_t>((TileXByte - DAT_005d91d0 ^ BuildingId) - BuildingId)) ||
			(BuildingId = 0x00f6,
			 TileXByte = static_cast<uint16_t>(static_cast<int16_t>(TileYByte - DAT_005d91d4) >> 0x0f),
			 1 < static_cast<int16_t>((TileYByte - DAT_005d91d4 ^ TileXByte) - TileXByte)))
		{
			BuildingId = 0xffff;
		}
	}

	switch (BuildingId)
	{
	case 0x00d1:
		if (FUN_004c1fb0(TileX, TileY) == 0)
		{
			int16_t SpawnCount = 1;
			if (static_cast<int16_t>(FUN_004cea00(DAT_0058dc3a >> 2)) == 0)
			{
				SpawnCount = static_cast<int16_t>(FUN_004cea00(0x001e) + 1);
			}

			if (0 < SpawnCount)
			{
				do
				{
					FUN_004c2260(0x0c, 5, TileX, TileY, 0xffffffff, 0);
					SpawnCount = static_cast<int16_t>(SpawnCount - 1);
				} while (SpawnCount != 0);
				return;
			}
		}
		break;

	case 0x00d2:
		if (FUN_004c1fb0(TileX, TileY) == 0)
		{
			int16_t SpawnCount = 1;
			if (static_cast<int16_t>(FUN_004cea00(DAT_0058dc3a >> 2)) == 0)
			{
				SpawnCount = static_cast<int16_t>(FUN_004cea00(0x001e) + 1);
			}

			if (0 < SpawnCount)
			{
				do
				{
					FUN_004c2260(0x0e, 7, TileX, TileY, 0xffffffff, 0);
					SpawnCount = static_cast<int16_t>(SpawnCount - 1);
				} while (SpawnCount != 0);
				return;
			}
		}
		break;

	case 0x00d7:
		if (FUN_004c1fb0(TileX, TileY) == 0)
		{
			FSimCopterOriginalPerson* Person = FUN_004c2ef0();
			if ((Person != nullptr) && (FUN_004c20b0(BuildingId, 0x04b5, 4, TileX, TileY, 0, 0x00460000, -0x00460000) != 0))
			{
				Person->FigureClothesOffset = static_cast<int16_t>(FUN_004cea00(10));
			}

			const int16_t TeamClothesOffset = static_cast<int16_t>(FUN_004cea00(10));

			Person = FUN_004c2ef0();
			if ((Person != nullptr) && (FUN_004c20b0(BuildingId, 0x04b6, 4, TileX, TileY, 0, 0x00140000, -0x00460000) != 0))
			{
				Person->FigureClothesOffset = TeamClothesOffset;
			}

			Person = FUN_004c2ef0();
			if ((Person != nullptr) && (FUN_004c20b0(BuildingId, 0x04b6, 4, TileX, TileY, 0, 0x00140000, -0x00140000) != 0))
			{
				Person->FigureClothesOffset = TeamClothesOffset;
			}

			Person = FUN_004c2ef0();
			if ((Person != nullptr) && (FUN_004c20b0(BuildingId, 0x04b6, 4, TileX, TileY, 0, 0x00460000, -0x00140000) != 0))
			{
				Person->FigureClothesOffset = TeamClothesOffset;
			}

			Person = FUN_004c2ef0();
			if ((Person != nullptr) && (FUN_004c20b0(BuildingId, 0x04b6, 4, TileX, TileY, 0, 0x002d0000, -0x002d0000) != 0))
			{
				Person->FigureClothesOffset = TeamClothesOffset;
			}

			Person = FUN_004c2ef0();
			if ((Person != nullptr) && (FUN_004c20b0(BuildingId, 0x04b6, 4, TileX, TileY, 0, -0x00280000, -0x00280000) != 0))
			{
				Person->FigureClothesOffset = TeamClothesOffset;
			}

			Person = FUN_004c2ef0();
			if ((Person != nullptr) && (FUN_004c20b0(BuildingId, 0x04b6, 4, TileX, TileY, 0, 0x00280000, 0x00280000) != 0))
			{
				Person->FigureClothesOffset = TeamClothesOffset;
			}

			Person = FUN_004c2ef0();
			if ((Person != nullptr) && (FUN_004c20b0(BuildingId, 0x04b6, 4, TileX, TileY, 0, -0x00280000, 0x00280000) != 0))
			{
				Person->FigureClothesOffset = TeamClothesOffset;
				return;
			}
		}
		break;

	case 0x00db:
		if ((FUN_004c1fb0(TileX, TileY) == 0) && (FUN_004c2ef0() != nullptr))
		{
			FUN_004c20b0(BuildingId, 0x04b2, 0xffffffff, TileX, TileY, 0, 0x00080000, 0x00200000);
		}
		break;
	}

	return;
}