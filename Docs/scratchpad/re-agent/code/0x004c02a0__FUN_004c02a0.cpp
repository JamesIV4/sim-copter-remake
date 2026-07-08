/*
RE_AGENT_NOTE
purpose: Samples up to two fixed-point local X/Z offsets inside a scene-cell footprint and accepts a candidate according to the original collision and surface-height placement gates.
use: Port as the original placement validator around person/spawn setup code; important callees are FUN_0046c4bf, FUN_004cea00, FUN_004c9000, and FUN_004c82c0.
evidence: Reads param_1 +0x1c0/+0x1c4 for radius setup, reads param_2 +0/+2/+4/+6/+8 for scene-cell flags and tile position/footprint, probes collision at (CellY + 3) << 16, and has distinct acceptance gates for placement modes 0/1/2/3.
caveats: ECX/param_1 is modeled as a Person pointer for the __thiscall input. Case 2 references Ghidra's unaff_EDI artifact; the original callable interface has no sixth formal parameter, so UnaffEDI is kept as a local artifact model and its incoming high half needs assembly/caller review.
*/

#include <cstddef>
#include <cstdint>

struct FSimCopterOriginalPerson
{
	std::byte Pad0000[0x1c0];
	uint16_t SpawnFlags; // +0x1c0
	std::byte Pad01c2[0x02];
	int32_t WalkSpeed; // +0x1c4
};

struct FSimCopterOriginalSceneCell
{
	uint8_t Flags; // +0x00
	std::byte Pad0001[0x01];
	int16_t CellX; // +0x02
	int16_t CellY; // +0x04
	int16_t CellZ; // +0x06
	int16_t FootprintSize; // +0x08
};

static_assert(offsetof(FSimCopterOriginalPerson, SpawnFlags) == 0x1c0);
static_assert(offsetof(FSimCopterOriginalPerson, WalkSpeed) == 0x1c4);
static_assert(offsetof(FSimCopterOriginalSceneCell, Flags) == 0x00);
static_assert(offsetof(FSimCopterOriginalSceneCell, CellX) == 0x02);
static_assert(offsetof(FSimCopterOriginalSceneCell, CellY) == 0x04);
static_assert(offsetof(FSimCopterOriginalSceneCell, CellZ) == 0x06);
static_assert(offsetof(FSimCopterOriginalSceneCell, FootprintSize) == 0x08);

extern int32_t DAT_00506aec;

extern int32_t FUN_0046c4bf(int32_t Value, int32_t Scale);
extern uint16_t FUN_004cea00(uint32_t Bound);
extern int32_t FUN_004c9000(int32_t Unknown1, int32_t WorldX, int32_t WorldY, int32_t WorldZ, int32_t Radius, int32_t Unknown6, int32_t Unknown7);
extern int32_t FUN_004c82c0(int32_t WorldX, int32_t CurrentWorldY, int32_t WorldZ);

static constexpr uint32_t CONCAT22(uint16_t High, uint16_t Low)
{
	return (uint32_t(High) << 16) | uint32_t(Low);
}

static constexpr int32_t Fixed16Mul(int32_t Value)
{
	return int32_t(uint32_t(Value) * 0x10000u);
}

int16_t FUN_004c02a0(
	FSimCopterOriginalPerson* Person,
	FSimCopterOriginalSceneCell* SceneCell,
	int32_t* OutLocalX,
	int32_t* OutLocalZ,
	int32_t PlacementMode)
{
	const int16_t Extent = int16_t(uint16_t(SceneCell->FootprintSize) << 5);
	int32_t UnaffEDI = 0;

	int32_t Radius;
	if ((Person->SpawnFlags & 0x80) == 0)
	{
		Radius = DAT_00506aec;
		if ((Person->SpawnFlags & 0x200) != 0)
		{
			Radius = DAT_00506aec * 2;
		}
		Radius = FUN_0046c4bf(Person->WalkSpeed, Radius);
	}
	else
	{
		Radius = 0x80000;
	}
	Radius = Radius * 2;

	switch (PlacementMode)
	{
	case 0:
	case 1:
	{
		int16_t Attempt = 0;
		int32_t IVar3 = PlacementMode;
		do
		{
			const uint16_t UVar6 = uint16_t(uint32_t(IVar3) >> 16);

			if (PlacementMode == 0)
			{
				if (Extent == 0)
				{
					*OutLocalZ = 0;
					*OutLocalX = 0;
				}
				else
				{
					const int32_t Extent32 = int32_t(Extent);
					IVar3 = Extent32 + -1;
					const uint16_t Roll = FUN_004cea00(CONCAT22(UVar6, uint16_t(Extent)) * 2u + uint32_t(-2));
					const int32_t Variable = Fixed16Mul((Extent32 - int32_t(Roll)) + -1);

					if (FUN_004cea00(2) == 0)
					{
						IVar3 = 1 - Extent32;
					}

					if (FUN_004cea00(2) == 0)
					{
						*OutLocalZ = Variable;
						*OutLocalX = int32_t(uint32_t(IVar3) << 16);
					}
					else
					{
						*OutLocalX = Variable;
						*OutLocalZ = int32_t(uint32_t(IVar3) << 16);
					}
				}
			}
			else if (Extent == 0)
			{
				*OutLocalZ = 0;
				*OutLocalX = 0;
			}
			else
			{
				IVar3 = int32_t(CONCAT22(UVar6, uint16_t(Extent)) * 2u + uint32_t(-2));
				uint16_t Roll = FUN_004cea00(uint32_t(IVar3));
				*OutLocalX = Fixed16Mul((int32_t(Extent) - int32_t(Roll)) + -1);
				Roll = FUN_004cea00(uint32_t(IVar3));
				*OutLocalZ = Fixed16Mul((int32_t(Extent) - int32_t(Roll)) + -1);
			}

			const int32_t WorldX = Fixed16Mul(int32_t(SceneCell->CellX)) + *OutLocalX;
			const int32_t WorldY = Fixed16Mul(int32_t(int16_t(SceneCell->CellY + 3)));
			const int32_t WorldZ = Fixed16Mul(int32_t(SceneCell->CellZ)) + *OutLocalZ;

			IVar3 = FUN_004c9000(0, WorldX, WorldY, WorldZ, Radius, 0, 0);
			if ((IVar3 == 0) &&
				(((SceneCell->Flags & 1) == 0) ||
				 (IVar3 = FUN_004c82c0(WorldX, WorldY, WorldZ),
				  IVar3 <= Fixed16Mul(int32_t(int16_t(SceneCell->CellY + 10))))))
			{
				return 1;
			}

			Attempt = int16_t(Attempt + 1);
		}
		while (Attempt < 2);
		break;
	}

	case 2:
	{
		int16_t Attempt = 0;
		do
		{
			if (Extent == 0)
			{
				*OutLocalZ = 0;
				*OutLocalX = 0;
			}
			else
			{
				const int16_t HalfExtent = int16_t(Extent >> 1);
				UnaffEDI = int32_t(CONCAT22(uint16_t(uint32_t(UnaffEDI) >> 16), uint16_t(HalfExtent)));

				uint16_t Roll = FUN_004cea00(uint32_t(UnaffEDI) * 2u + uint32_t(-2));
				*OutLocalX = Fixed16Mul((int32_t(HalfExtent) - int32_t(Roll)) + -1);
				Roll = FUN_004cea00(uint32_t(UnaffEDI) * 2u + uint32_t(-2));
				*OutLocalZ = Fixed16Mul((int32_t(HalfExtent) - int32_t(Roll)) + -1);
			}

			const int32_t WorldX = Fixed16Mul(int32_t(SceneCell->CellX)) + *OutLocalX;
			const int32_t WorldY = Fixed16Mul(int32_t(int16_t(SceneCell->CellY + 3)));
			const int32_t WorldZ = Fixed16Mul(int32_t(SceneCell->CellZ)) + *OutLocalZ;

			const int32_t Hit = FUN_004c9000(0, WorldX, WorldY, WorldZ, Radius, 0, 0);
			if (((Hit == 0) && ((SceneCell->Flags & 1) != 0)) &&
				(Fixed16Mul(int32_t(int16_t(SceneCell->CellY + 10))) < FUN_004c82c0(WorldX, WorldY, WorldZ)))
			{
				return 1;
			}

			Attempt = int16_t(Attempt + 1);
		}
		while (Attempt < 2);
		break;
	}

	case 3:
	{
		int16_t Attempt = 0;
		do
		{
			if (Extent == 0)
			{
				*OutLocalZ = 0;
				*OutLocalX = 0;
			}
			else
			{
				const int32_t Bound = int32_t(CONCAT22(uint16_t(uint32_t(PlacementMode) >> 16), uint16_t(Extent)) * 2u + uint32_t(-2));
				uint16_t Roll = FUN_004cea00(uint32_t(Bound));
				*OutLocalX = Fixed16Mul((int32_t(Extent) - int32_t(Roll)) + -1);
				Roll = FUN_004cea00(uint32_t(Bound));
				*OutLocalZ = Fixed16Mul((int32_t(Extent) - int32_t(Roll)) + -1);
			}

			PlacementMode = FUN_004c9000(
				0,
				Fixed16Mul(int32_t(SceneCell->CellX)) + *OutLocalX,
				Fixed16Mul(int32_t(int16_t(SceneCell->CellY + 3))),
				Fixed16Mul(int32_t(SceneCell->CellZ)) + *OutLocalZ,
				Radius,
				0,
				0);

			if ((PlacementMode == 0) && ((SceneCell->Flags & 1) != 0))
			{
				return 1;
			}

			Attempt = int16_t(Attempt + 1);
		}
		while (Attempt < 2);
		break;
	}
	}

	return 0;
}

// REVERSED_FUNCTION: ::FUN_004c02a0 (0x004c02a0)