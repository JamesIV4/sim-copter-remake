/*
RE_AGENT_NOTE
purpose: Checks a byte from the 256x256 DAT_005bde80 tile-indexed table and returns whether it passes the original numeric gate.
use: Port as a leaf helper for callers that need this DAT_005bde80 tile-byte predicate; supplied xref data reports two callers but does not identify them.
evidence: Function masks both parameters to 8-bit tile coordinates, indexes DAT_005bde80 as (TileX & 0xff) * 0x100 + (TileY & 0xff), performs no calls, returns 0 for the original blocked condition and 1 otherwise.
caveats: The gameplay meaning of DAT_005bde80 and the exact caller roles are inferred only from surrounding project context, not from this decompile. The unsigned `(TerrainOrDensityType < 5)` subcondition is unreachable when paired with `4 < TerrainOrDensityType`, but is preserved to mirror the emitted branch logic.
*/

#include <cstdint>

extern uint8_t DAT_005bde80[0x10000];

int32_t FUN_004c92a0(uint16_t TileX, uint16_t TileY)
{
	const uint8_t TerrainOrDensityType =
		DAT_005bde80[uint32_t(TileY & 0xff) + uint32_t(TileX & 0xff) * 0x100];

	if ((4 < TerrainOrDensityType) && ((TerrainOrDensityType < 5) || (9 < TerrainOrDensityType)))
	{
		return 0;
	}

	return 1;
}

// REVERSED_FUNCTION: ::FUN_004c92a0 (0x004c92a0)