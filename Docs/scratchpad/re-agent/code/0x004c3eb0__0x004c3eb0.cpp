/*
RE_AGENT_NOTE
purpose: Probes for an available original person slot, runs FUN_004c4190 with the seven forwarded arguments, and returns the selected slot's +0x12e field on nonzero success; otherwise returns 0xffff.
use: Port as a small spawn/allocation wrapper around FUN_004c2ef0 and FUN_004c4190. Callers should treat 0xffff as the failure sentinel unless later caller analysis narrows the return type.
evidence: Decompile initializes uVar3 to 0xffff, calls FUN_004c2ef0, skips all remaining work when it returns null, calls FUN_004c4190(param_1..param_7), and only when that result is nonzero reads *(undefined2 *)(iVar2 + 0x12e).
caveats: Ghidra shows no visible iVar2 argument to FUN_004c4190 here; this implementation models the call exactly as a free function, not as a proven hidden-this/member call. Parameter names and signedness are intentionally generic pending caller/callee review.
*/

#include <cstddef>
#include <cstdint>

struct FSimCopterOriginalPerson
{
	std::byte Pad0000[0x12e];
	uint16_t FigureId; // +0x12e
};

static_assert(offsetof(FSimCopterOriginalPerson, FigureId) == 0x12e);

extern FSimCopterOriginalPerson* FUN_004c2ef0();
extern int16_t FUN_004c4190(
	int32_t Param1,
	int32_t Param2,
	uint16_t Param3,
	uint16_t Param4,
	int32_t Param5,
	int32_t Param6,
	int32_t Param7);

uint16_t FUN_004c3eb0(
	int32_t Param1,
	int32_t Param2,
	uint16_t Param3,
	uint16_t Param4,
	int32_t Param5,
	int32_t Param6,
	int32_t Param7)
{
	uint16_t Result = 0xffff;

	FSimCopterOriginalPerson* Person = FUN_004c2ef0();
	if (Person != nullptr)
	{
		const int16_t SpawnResult = FUN_004c4190(Param1, Param2, Param3, Param4, Param5, Param6, Param7);
		if (SpawnResult != 0)
		{
			Result = Person->FigureId;
		}
	}

	return Result;
}

// REVERSED_FUNCTION: ::0x004c3eb0 (0x004c3eb0)