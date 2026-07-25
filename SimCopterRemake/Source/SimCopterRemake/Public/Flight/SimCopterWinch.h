// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// The single 20-node rope shared by the water bucket and the rescue harness, with the
// rope-end object exchanged when the winch pays out.
//
// Ported from (Docs/scratchpad/ghidra/heli_tools_models_decode_20260724.md section 3):
//   FUN_00485f50  actions 0x0b/0x0c (bucket) and 0x0e/0x0f (harness) -> heli[0x72] commands
//   FUN_00487bb0  winch step: node cursor, door/rope visibility, rope-end object swap
//   FUN_00483c20  construction of the rope (20 nodes) and both rope-end objects
//
// Polarity traps confirmed against the assert strings in FUN_004cccd0
// ("bucket not raised - can ignore"):
//   * heli[0x70] / heli[0x71] are STOWED flags: 1 = raised/stowed, 0 = deployed.
//   * heli[0x6f] counts DOWN from 0x11 (fully stowed) to 3 (fully lowered).
//   * command +1/-1 drive the bucket, +2/-2 drive the harness.

namespace SimCopterWinch
{
// heli[0x6f] limits.
constexpr int32 StowedNode = 0x11;    // 17
constexpr int32 LoweredNode = 3;
// The node the door opens / the rope-end object swaps at while paying out.
constexpr int32 SwapNode = 0x10;      // 16

// heli[0x72] command values.
constexpr int32 CommandIdle = 0;
constexpr int32 CommandRaiseBucket = 1;
constexpr int32 CommandRaiseHarness = 2;
constexpr int32 CommandLowerBucket = -1;
constexpr int32 CommandLowerHarness = -2;

// Which object the rope end renders.
enum class ERopeEnd : uint8
{
	Bucket,  // GEO 0x07b, heli[0x32]
	Harness, // GEO 0x16d, heli[0x33]
};

// The winch half of the helicopter state (heli[0x6f]..heli[0x72]).
struct SIMCOPTERREMAKE_API FWinchState
{
	int32 NodeCursor = StowedNode;   // heli[0x6f]
	bool bBucketStowed = true;       // heli[0x70] != 0
	bool bHarnessStowed = true;      // heli[0x71] != 0
	int32 Command = CommandIdle;     // heli[0x72]
	ERopeEnd RopeEnd = ERopeEnd::Bucket;

	bool IsFullyStowed() const { return bBucketStowed && bHarnessStowed; }
	bool IsAnythingDeployed() const { return !bBucketStowed || !bHarnessStowed; }
};

// SCHOOK: WinchLowerBucket 0x00485f50 (action 0x0b)
// Pressing "lower bucket" while the harness is out first raises the harness - this is the
// mechanism behind the help text's "only one attachment at a time".
SIMCOPTERREMAKE_API int32 ResolveLowerBucketCommand(const FWinchState& State);

// SCHOOK: WinchRaiseBucket 0x00485f50 (action 0x0c)
SIMCOPTERREMAKE_API int32 ResolveRaiseBucketCommand(const FWinchState& State);

// SCHOOK: WinchLowerHarness 0x00485f50 (action 0x0e)
SIMCOPTERREMAKE_API int32 ResolveLowerHarnessCommand(const FWinchState& State);

// SCHOOK: WinchRaiseHarness 0x00485f50 (action 0x0f)
SIMCOPTERREMAKE_API int32 ResolveRaiseHarnessCommand(const FWinchState& State);

// SCHOOK: WinchStep 0x00487bb0
// Advances one frame of the winch. Returns true when the rope-end object changed this frame
// (the caller swaps the visible mesh).
SIMCOPTERREMAKE_API bool StepWinch(FWinchState& InOutState);

// Rope length in nodes currently paid out (0 when stowed).
SIMCOPTERREMAKE_API int32 GetDeployedNodeCount(const FWinchState& State);
}
