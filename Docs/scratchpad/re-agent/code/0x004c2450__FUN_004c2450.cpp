/*
RE_AGENT_NOTE
purpose: Attempts one generic ambient spawn for a packed city/tile argument after two placement/class gates pass.
use: Port at the single call site identified for FUN_004c2450; caller identity and ordering are not established by this excerpt. The routine depends on FUN_004c9cc0/FUN_004c9220/FUN_004c9dc0 gates, FUN_004cea00/FUN_004c7190 class selection, DAT_0058ec00 class membership, and FUN_004c3eb0 creation.
evidence: Masks param_2 with 0xffff00ff, builds uVar7 from param_1 and the high word of incoming unaff_ESI before the same mask, rejects zero low-16 gate results, tries five classes, maps rolls 0/1 to classes 10/17, scans ten DAT_0058ec00 entries, and returns the full 32-bit FUN_004c3eb0 result.
caveats: unaff_ESI is a Ghidra recovered incoming-register artifact, not a proven formal parameter; RE_004c2450_UnresolvedIncomingEsi is a porting placeholder until caller/calling-convention disassembly identifies the real high-word source. Packed argument layout, DAT_0058ec00 meaning, and FUN_004c3eb0 parameter semantics remain provisional.
*/

#include <cstdint>

extern int32_t DAT_0058ec00[22 * 10];

// Porting placeholder for Ghidra's unaff_ESI recovered input at 0x004c2450.
extern uint32_t RE_004c2450_UnresolvedIncomingEsi;

extern int16_t FUN_004c9cc0(uint32_t PackedArg1, uint32_t PackedArg2, int32_t Arg3);
extern int32_t FUN_004c9220(uint32_t PackedArg1, uint32_t PackedArg2);
extern int16_t FUN_004c9dc0(int32_t ClassValue, int32_t Arg2);
extern int16_t FUN_004cea00(uint32_t Bound);
extern int32_t FUN_004c7190();
extern uint32_t FUN_004c3eb0(
	int32_t ClassValue,
	int32_t Arg2,
	uint32_t PackedArg3,
	uint32_t PackedArg4,
	uint32_t Arg5,
	int32_t Arg6,
	int32_t Arg7);

uint32_t FUN_004c2450(uint16_t Param1, uint32_t Param2)
{
	Param2 = Param2 & 0xffff00ff;

	const uint32_t PackedArg1 =
		(((static_cast<uint32_t>(static_cast<uint16_t>(RE_004c2450_UnresolvedIncomingEsi >> 0x10))) << 16) | Param1) &
		0xffff00ff;

	int16_t GateResult = FUN_004c9cc0(PackedArg1, Param2, 0);
	if (GateResult == 0)
	{
		return 0xffff;
	}

	const int32_t ClassValue = FUN_004c9220(PackedArg1, Param2);

	GateResult = FUN_004c9dc0(ClassValue, 0);
	if (GateResult == 0)
	{
		return 0xffff;
	}

	int16_t Attempt = 0;
	bool bFoundClass = false;
	int32_t ClassChoice = 0;

	do
	{
		const int16_t Roll = FUN_004cea00(0x14);
		if (Roll == 0)
		{
			ClassChoice = 10;
		}
		else if (Roll == 1)
		{
			ClassChoice = 0x11;
		}
		else
		{
			ClassChoice = FUN_004c7190();
		}

		int16_t Index = 0;
		do
		{
			if (DAT_0058ec00[static_cast<int32_t>(Index) + ClassChoice * 10] == ClassValue)
			{
				bFoundClass = true;
				goto FoundClass;
			}

			Index = static_cast<int16_t>(Index + 1);
		}
		while (Index < 10);

		Attempt = static_cast<int16_t>(Attempt + 1);
	}
	while (Attempt < 5);

FoundClass:
	if (bFoundClass)
	{
		return FUN_004c3eb0(ClassChoice, 0, PackedArg1, Param2, 0xffffffff, 0, 0);
	}

	return 0xffff;
}

// REVERSED_FUNCTION: ::FUN_004c2450 (0x004c2450)