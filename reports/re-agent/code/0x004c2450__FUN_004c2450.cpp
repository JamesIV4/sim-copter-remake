#include <cstdint>

struct FSimCopterOriginalPerson;

extern int32_t DAT_0058ec00[22 * 10];

extern int16_t FUN_004c9cc0(uint32_t TileX, uint32_t TileY, FSimCopterOriginalPerson* Person);
extern int32_t FUN_004c9220(uint32_t TileX, uint32_t TileY);
extern int16_t FUN_004c9dc0(int32_t TileClass, FSimCopterOriginalPerson* Person);
extern int16_t FUN_004cea00(uint16_t Bound);
extern int32_t FUN_004c7190();
extern uint32_t FUN_004c3eb0(
	int32_t BehaviorClass,
	int32_t InitialPersonState,
	uint32_t TileX,
	uint32_t TileY,
	uint32_t MissionEventId,
	int32_t Unknown6,
	void* SpawnPosition);

// `unaff_ESI` is not a formal parameter and is not produced by a call here.
// Ghidra is preserving the incoming ESI register value observed by this function.
extern uint32_t unaff_ESI;

uint32_t FUN_004c2450(uint16_t param_1, uint32_t param_2)
{
	param_2 = param_2 & 0xffff00ff;
	const uint32_t uVar7 =
		((static_cast<uint32_t>(static_cast<uint16_t>(unaff_ESI >> 16)) << 16) | param_1) & 0xffff00ff;

	int16_t sVar2 = FUN_004c9cc0(uVar7, param_2, nullptr);
	if (sVar2 == 0)
	{
		return 0xffff;
	}

	const int32_t iVar4 = FUN_004c9220(uVar7, param_2);
	sVar2 = FUN_004c9dc0(iVar4, nullptr);
	if (sVar2 == 0)
	{
		return 0xffff;
	}

	sVar2 = 0;
	bool bVar1 = false;
	int32_t iVar5 = 0;

	do
	{
		int16_t sVar3 = FUN_004cea00(0x14);
		if (sVar3 == 0)
		{
			iVar5 = 10;
		}
		else if (sVar3 == 1)
		{
			iVar5 = 0x11;
		}
		else
		{
			iVar5 = FUN_004c7190();
		}

		sVar3 = 0;
		do
		{
			if (DAT_0058ec00[static_cast<int32_t>(sVar3) + iVar5 * 10] == iVar4)
			{
				bVar1 = true;
				goto LAB_004c2514;
			}

			sVar3 = sVar3 + 1;
		} while (sVar3 < 10);

		sVar2 = sVar2 + 1;
	} while (sVar2 < 5);

LAB_004c2514:
	if (bVar1)
	{
		const uint32_t uVar6 = FUN_004c3eb0(iVar5, 0, uVar7, param_2, 0xffffffff, 0, nullptr);
		return uVar6;
	}

	return 0xffff;
}