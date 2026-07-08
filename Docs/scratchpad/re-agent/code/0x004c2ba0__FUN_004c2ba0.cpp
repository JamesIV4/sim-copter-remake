/*
RE_AGENT_NOTE
purpose: Ambient people population driver; when the reference/default person enters a new tile, scans directional paths around that tile and asks FUN_004c2550 to run per-tile spawn work.
use: Port in the original figure/people frame update at the existing FUN_004c2ba0 call site; FUN_004c2550 performs the delegated per-tile work. The __ftol() result is used as the attempt/count argument and is clamped to at least 1.
evidence: Early-outs when DAT_0058dc1c exceeds DAT_0058dc2a or cached tile DAT_0058dc5e/60 matches DAT_00506444 +0x12a/+0x12c; signed DAT_0061a664/DAT_0061a66c magnitudes select directional scan paths; every selected tile delegates to FUN_004c2550 and may stop early if the population cap is reached.
caveats: DAT_0061a664/DAT_0061a66c are inferred as recent tile movement/direction deltas. The x87 value converted by __ftol() is not visible in the provided decompile. unaff_EDI/unaff_ESI high halves are Ghidra calling-convention artifacts; keep them as unknown incoming high halves unless the caller/callee ABI is narrowed further.
*/

#include <cstddef>
#include <cstdint>

struct FSimCopterOriginalPerson
{
	std::byte Pad0000[0x12a];
	int16_t TileX; // +0x12a
	int16_t TileY; // +0x12c
};

static_assert(offsetof(FSimCopterOriginalPerson, TileX) == 0x12a);
static_assert(offsetof(FSimCopterOriginalPerson, TileY) == 0x12c);

extern FSimCopterOriginalPerson* DAT_00506444;
extern int32_t DAT_0058dc1c;
extern int32_t DAT_0058dc2a;
extern int16_t DAT_0058dc2e;
extern int16_t DAT_0058dc5e;
extern int16_t DAT_0058dc60;
extern int32_t DAT_0061a664;
extern int32_t DAT_0061a66c;

// Ghidra artifacts from preserved high halves of incoming registers.
extern int16_t unaff_EDI_hi_004c2ba0;
extern int16_t unaff_ESI_hi_004c2ba0;

extern "C" int32_t __ftol();
extern void FUN_004c2550(int32_t TileXExpression, int32_t TileYExpression, int16_t SpawnAttemptCount);

static int32_t CONCAT22_004c2ba0(int16_t Hi, int16_t Lo)
{
	return static_cast<int32_t>(
		(static_cast<uint32_t>(static_cast<uint16_t>(Hi)) << 16) |
		static_cast<uint16_t>(Lo));
}

static uint32_t NegU32_004c2ba0(int32_t Value)
{
	return 0u - static_cast<uint32_t>(Value);
}

static uint32_t NegU32_004c2ba0(uint32_t Value)
{
	return 0u - Value;
}

static int32_t AddU32AsI32_004c2ba0(int32_t Lhs, uint32_t Rhs)
{
	return static_cast<int32_t>(static_cast<uint32_t>(Lhs) + Rhs);
}

static uint32_t AbsU32_004c2ba0(int32_t Value)
{
	const uint32_t SignMask = 0u - (static_cast<uint32_t>(Value) >> 31);
	return (static_cast<uint32_t>(Value) ^ SignMask) - SignMask;
}

void FUN_004c2ba0()
{
	int16_t sVar1 = DAT_0058dc2e;
	if (DAT_0058dc2a < DAT_0058dc1c)
	{
		return;
	}

	int16_t sVar7 = DAT_00506444->TileX;
	if ((DAT_0058dc5e == sVar7) && (DAT_00506444->TileY == DAT_0058dc60))
	{
		return;
	}

	int16_t sVar13 = sVar7 - DAT_0058dc2e;
	int32_t iVar14 = CONCAT22_004c2ba0(unaff_EDI_hi_004c2ba0, sVar13);
	sVar7 = DAT_0058dc2e + sVar7;
	int16_t sVar2 = DAT_00506444->TileY + DAT_0058dc2e;
	int16_t sVar10 = DAT_00506444->TileY - DAT_0058dc2e;
	int32_t iVar12 = CONCAT22_004c2ba0(unaff_ESI_hi_004c2ba0, sVar10);
	int16_t sVar11 = DAT_0058dc2e * 2;
	int16_t sVar8 = DAT_0058dc2e << 2;

	int16_t sVar3 = static_cast<int16_t>(__ftol());
	if (sVar3 < 1)
	{
		sVar3 = 1;
	}

	uint32_t uVar5 = AbsU32_004c2ba0(DAT_0061a664);
	int32_t iVar6 = static_cast<int32_t>(AbsU32_004c2ba0(DAT_0061a66c));
	uint32_t uVar9 = uVar5;

	if (DAT_0061a664 < 0)
	{
		if ((DAT_0061a66c <= static_cast<int32_t>(uVar5)) && (static_cast<int32_t>(NegU32_004c2ba0(uVar5)) < DAT_0061a66c))
		{
			uVar5 = uVar5 & 0xffff0000;
			if (0 < sVar11)
			{
				do
				{
					sVar8 = static_cast<int16_t>(uVar5);
					sVar13 = sVar8 + 1;
					uVar5 = static_cast<uint32_t>(CONCAT22_004c2ba0(static_cast<int16_t>(uVar5 >> 16), sVar13));
					FUN_004c2550(iVar14, sVar8 + sVar10, sVar3);
				}
				while (sVar13 < sVar11);
			}

			uVar5 = uVar5 & 0xffff0000;
			if (0 < sVar1)
			{
				do
				{
					FUN_004c2550(AddU32AsI32_004c2ba0(iVar14, uVar5), iVar12, sVar3);
					FUN_004c2550(AddU32AsI32_004c2ba0(iVar14, uVar5), CONCAT22_004c2ba0(sVar7, sVar2), sVar3);
					if (DAT_0058dc2a < DAT_0058dc1c)
					{
						break;
					}

					sVar8 = static_cast<int16_t>(uVar5) + 1;
					uVar5 = static_cast<uint32_t>(CONCAT22_004c2ba0(static_cast<int16_t>(uVar5 >> 16), sVar8));
				}
				while (sVar8 < sVar1);
			}

			goto UpdateCachedTile;
		}

		if (-1 < DAT_0061a664)
		{
			goto ScanPositiveX;
		}
	}
	else
	{
ScanPositiveX:
		if ((DAT_0061a66c < static_cast<int32_t>(uVar5)) &&
			(uVar9 = NegU32_004c2ba0(uVar5), (NegU32_004c2ba0(DAT_0061a66c) == uVar5) || (static_cast<int32_t>(uVar9) < DAT_0061a66c)))
		{
			sVar10 = 0;
			if (0 < sVar11)
			{
				do
				{
					sVar13 = sVar2 - sVar10;
					sVar10 = sVar10 + 1;
					FUN_004c2550(CONCAT22_004c2ba0(sVar8, sVar7), sVar13, sVar3);
				}
				while (sVar10 < sVar11);
			}

			sVar8 = 0;
			if (0 < sVar1)
			{
				do
				{
					uVar9 = static_cast<uint32_t>(CONCAT22_004c2ba0(static_cast<int16_t>(uVar9 >> 16), sVar7 - sVar8));
					FUN_004c2550(static_cast<int32_t>(uVar9), iVar12, sVar3);
					FUN_004c2550(static_cast<int32_t>(uVar9), CONCAT22_004c2ba0(sVar7, sVar2), sVar3);
					if (DAT_0058dc2a < DAT_0058dc1c)
					{
						break;
					}

					sVar8 = sVar8 + 1;
				}
				while (sVar8 < sVar1);
			}

			goto UpdateCachedTile;
		}
	}

	if (DAT_0061a66c < 0)
	{
		if (((static_cast<int32_t>(NegU32_004c2ba0(DAT_0061a664)) == iVar6) || (static_cast<int32_t>(NegU32_004c2ba0(iVar6)) < DAT_0061a664)) && (DAT_0061a664 < iVar6))
		{
			sVar10 = 0;
			if (0 < sVar11)
			{
				do
				{
					int16_t sVar4 = sVar10 + sVar13;
					sVar10 = sVar10 + 1;
					FUN_004c2550(CONCAT22_004c2ba0(sVar7, sVar4), CONCAT22_004c2ba0(sVar7, sVar2), sVar3);
				}
				while (sVar10 < sVar11);
			}

			sVar11 = 0;
			if (0 < sVar1)
			{
				do
				{
					uVar9 = static_cast<uint32_t>(CONCAT22_004c2ba0(static_cast<int16_t>(uVar9 >> 16), sVar2 - sVar11));
					FUN_004c2550(CONCAT22_004c2ba0(sVar8, sVar7), static_cast<int32_t>(uVar9), sVar3);
					FUN_004c2550(iVar14, static_cast<int32_t>(uVar9), sVar3);
					if (DAT_0058dc2a < DAT_0058dc1c)
					{
						break;
					}

					sVar11 = sVar11 + 1;
				}
				while (sVar11 < sVar1);
			}

			goto UpdateCachedTile;
		}

		if (DAT_0061a66c < 0)
		{
			goto UpdateCachedTile;
		}
	}

	if ((static_cast<int32_t>(NegU32_004c2ba0(iVar6)) < DAT_0061a664) && (DAT_0061a664 <= iVar6))
	{
		uVar9 = uVar9 & 0xffff0000;
		if (0 < sVar11)
		{
			do
			{
				sVar2 = static_cast<int16_t>(uVar9);
				sVar10 = sVar2 + 1;
				uVar9 = static_cast<uint32_t>(CONCAT22_004c2ba0(static_cast<int16_t>(uVar9 >> 16), sVar10));
				FUN_004c2550(sVar7 - sVar2, iVar12, sVar3);
			}
			while (sVar10 < sVar11);
		}

		uVar9 = uVar9 & 0xffff0000;
		if (0 < sVar1)
		{
			do
			{
				FUN_004c2550(CONCAT22_004c2ba0(sVar8, sVar7), AddU32AsI32_004c2ba0(iVar12, uVar9), sVar3);
				FUN_004c2550(iVar14, AddU32AsI32_004c2ba0(iVar12, uVar9), sVar3);
				if (DAT_0058dc2a < DAT_0058dc1c)
				{
					break;
				}

				sVar11 = static_cast<int16_t>(uVar9) + 1;
				uVar9 = static_cast<uint32_t>(CONCAT22_004c2ba0(static_cast<int16_t>(uVar9 >> 16), sVar11));
			}
			while (sVar11 < sVar1);
		}
	}

UpdateCachedTile:
	DAT_0058dc60 = DAT_00506444->TileY;
	DAT_0058dc5e = DAT_00506444->TileX;
}

// REVERSED_FUNCTION: ::FUN_004c2ba0 (0x004c2ba0)