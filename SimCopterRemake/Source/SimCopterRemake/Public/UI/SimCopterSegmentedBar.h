// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// The original draws every one of its cockpit bar readouts with the same routine, twice over:
//
//   0x00455700  flap0's water meter   eleven cells from watergge.bmp  (15x10 = three 5x10 cells)
//   0x004535??  dash6's points bar    fifteen cells from managge.bmp  (15x13 = three 5x13 cells)
//
// In both, the bitmap is a strip of exactly three states side by side - full, leading edge,
// empty - and the repaint walks one cursor along the row blitting:
//
//   loop 1   `level` cells of the FULL sprite
//   loop 2   exactly ONE leading-edge sprite, skipped when the bar is already full
//   loop 3   the remainder as EMPTY
//
// so the row is always the same number of cells and the leading edge marks the boundary. The
// level itself is `amount * cellCount / capacity` through a truncating idiv.
//
// Evidence: Docs/scratchpad/agent-sessions/2026-07-31-teargas/teargas-decode.md section 6 and
// Docs/scratchpad/agent-sessions/2026-08-01-dash-and-collision/dash-points-bar.md.
namespace SimCopterSegmentedBar
{
// The three sprites, in the left-to-right order they sit in the strip.
enum class ECell : uint8
{
	Full,
	LeadingEdge,
	Empty,
};

// Source rect of one state in a strip whose cells are CellWidth x CellHeight.
SIMCOPTERREMAKE_API FIntRect GetCellFrame(ECell Cell, int32 CellWidth, int32 CellHeight);

// `amount * cellCount / capacity`, truncating the way the original's idiv does, and clamped to
// the row. A capacity of zero reads empty rather than dividing by it - the original never sees
// one, but both capacities here come from data that can arrive half-loaded.
SIMCOPTERREMAKE_API int32 GetLevel(int32 Amount, int32 Capacity, int32 CellCount);

// Which sprite cell Index gets. Index == Level is the leading edge, which is why a full bar has
// no meniscus: Level then equals CellCount and no index can match it.
SIMCOPTERREMAKE_API ECell GetCellState(int32 Index, int32 Level);
}
