#include <cstdint>

/*
RE_AGENT_NOTE
purpose: Per-tile ambient people spawn wrapper; runs scripted building spawns for the tile, then conditionally performs generic ambient spawn attempts while under the ambient cap.
use: Port in the ambient tile-scan path; delegates to FUN_004c25b0 for scripted/building spawns, FUN_004cea00 for the random gate, and FUN_004c2450 for generic ambient spawn attempts.
evidence: Decompile calls FUN_004c25b0 unconditionally, checks DAT_0058dc1c < DAT_0058dc22, passes CONCAT22(DAT_0058dc1c >> 0xf, 0x0d - _DAT_0058dc18) to FUN_004cea00, accepts rolls < 3, then loops FUN_004c2450(param_1, param_2) until param_3 reaches zero.
caveats: _DAT_0058dc18 is an overlapping Ghidra global symbol; exact signedness/storage should be verified against neighboring globals. The high half of the FUN_004cea00 argument may be a calling/decompiler artifact only if separate callee analysis proves it.
*/

extern int32_t DAT_0058dc1c;
extern int32_t DAT_0058dc22;
extern int16_t _DAT_0058dc18;

extern void FUN_004c25b0(uint16_t TileX, uint16_t TileY);
extern uint16_t FUN_004cea00(uint32_t Bound);
extern uint32_t FUN_004c2450(uint16_t TileX, uint16_t TileY);

void FUN_004c2550(uint16_t TileX, uint16_t TileY, int16_t SpawnAttemptCount)
{
	FUN_004c25b0(TileX, TileY);

	if (DAT_0058dc1c < DAT_0058dc22)
	{
		const uint32_t PackedRandomBound =
			(static_cast<uint32_t>(static_cast<uint16_t>(DAT_0058dc1c >> 0xf)) << 16) |
			static_cast<uint16_t>(0x0d - _DAT_0058dc18);

		const uint16_t Roll = FUN_004cea00(PackedRandomBound);
		if ((Roll < 3) && (0 < SpawnAttemptCount))
		{
			do
			{
				FUN_004c2450(TileX, TileY);
				SpawnAttemptCount = SpawnAttemptCount - 1;
			}
			while (SpawnAttemptCount != 0);
		}
	}
}

// REVERSED_FUNCTION: ::0x004c2550 (0x004c2550)