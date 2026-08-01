// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Fixed-point tear-gas rules, kept in the executable's 16.16 units so the canister's flight and
// the gas cloud's cadence can be diffed against the decompile without a unit conversion in the
// way. Ported from:
//
//   FUN_00484d20  the muzzle: heli[0x57] == 3 -> launch point, direction and speed
//   FUN_0048e0b0  emitter type 3: pool DAT_005d4bd0, ammo decrement, fuse, launch sound 0x17
//   FUN_0048ed00  the tear gas arm of the master emitter tick: drag, gravity, fuse, gas cloud
//   FUN_00490690  collision: class flag 0x8 reflects rather than despawning
//
// Evidence: Docs/scratchpad/agent-sessions/2026-07-31-teargas/ and
// Docs/scratchpad/ghidra/heli_tools_models_decode_20260724.md section 8.
namespace SimCopterTearGas
{
	// DAT_005d4bd0 is ten slots (FUN_0048db20).
	static constexpr int32 PoolSlots = 10;

	// FUN_0048e0b0 type 3: slot[1] = 0x50000, slot[2] = 0x8000, slot[0xe] = 0.
	static constexpr int32 FuseLife1616 = 0x50000;        // 5.0 s of flight before it pops
	static constexpr int32 TrailInterval1616 = 0x8000;    // 0.5 s between smoke-trail cards

	// FUN_0048ed00 on fuse expiry: slot[0xe] = 1, slot[1] = 0x1e0000, slot[2] = 0.
	static constexpr int32 CloudLife1616 = 0x1e0000;      // 30.0 s of gas
	static constexpr int32 CloudPuffInterval1616 = 0x4ccc; // 0.3 s between puffs

	// Shared with the debris pool: 1% speed lost per frame plus 40 units/s^2 of gravity.
	static constexpr int32 Drag1616 = 0x28f;
	static constexpr int32 Gravity1616 = 0x280000;

	// FUN_0048ed00 after FUN_00490690 reports a hit: speed = Mul(0xc20c, speed), and the bump
	// sound only plays while what is left is faster than 20.0 units/s.
	static constexpr int32 BounceDamping1616 = 0xc20c;    // ~0.7599
	static constexpr int32 BounceSoundSpeed1616 = 0x140000;

	// FUN_00484d20's heli[0x57] == 3 arm: launch speed is the airframe's own forward speed plus
	// 50.0 units/s, from a point 3.0 units above the body node.
	static constexpr int32 LaunchSpeedBonus1616 = 0x320000;
	static constexpr int32 LaunchHeight1616 = 0x30000;

	// (0x14 - rand() % 0x28) * 0x10000 on X and Z: a whole number of units in [-19, +20].
	static constexpr int32 CloudSpreadCenterUnits = 0x14;
	static constexpr int32 CloudSpreadRangeUnits = 0x28;

	// FUN_004af220 kinds. The kind doubles as the card's SIM3D palette index.
	static constexpr uint8 TrailPuffClass = 4;
	static constexpr uint8 CloudPuffClass = 9;
	static constexpr uint8 SplashPuffClass = 8;

	// The node's collision radius, written once by FUN_0048db20 (node + 0x10).
	static constexpr int32 CollisionRadius1616 = 0x30000;

	// One live slot's motion and timers - the 0x48-byte pool entry, minus the render node.
	struct SIMCOPTERREMAKE_API FCanisterState
	{
		// Unit direction and scalar speed, exactly as the pool stores them (slot[4..6], slot[3]).
		// Z is up here, where the original uses Y; the rest of the arithmetic is unchanged.
		FIntVector Direction1616 = FIntVector(0, 0, 0);
		int32 Speed1616 = 0;

		// slot[1] - the fuse while flying, then the cloud's 30 seconds.
		int32 Life1616 = FuseLife1616;

		// slot[2] - counts down to the next trail card or gas puff.
		int32 EffectTimer1616 = TrailInterval1616;

		// slot[0xe] - 0 while the canister is in the air, 1 once it has burst.
		bool bDetonated = false;
	};

	// What one step of the tear-gas arm of FUN_0048ed00 asks the caller to do.
	struct SIMCOPTERREMAKE_API FCanisterFrame
	{
		FIntVector Travel1616 = FIntVector::ZeroValue;
		bool bAlive = true;
		// The fuse ran out this step: play TGPOP and switch to the cloud.
		bool bDetonatedThisFrame = false;
		// Drop a kind-4 card at the canister.
		bool bEmitTrail = false;
		// Drop a kind-9 card at a random offset tile and gas everyone standing on it.
		bool bEmitCloudPuff = false;
	};

	// SCHOOK: TearGasLaunch 0x00484d20
	// ForwardSpeed1616 is heli[0x4e]; the caller supplies the airframe's forward axis.
	SIMCOPTERREMAKE_API FCanisterState MakeLaunchState(const FVector& Direction, int32 ForwardSpeed1616);

	// SCHOOK: TearGasCanisterUpdate 0x0048ed00
	// Runs the fuse, the detonation, the drag/gravity integration and the effect timer in the
	// original's order: the burst happens *before* this step's motion, and it zeroes the effect
	// timer so the first gas puff lands on the same step.
	SIMCOPTERREMAKE_API FCanisterFrame AdvanceCanisterFrame(FCanisterState& InOutState, int32 Delta1616);

	// SCHOOK: TearGasBounce 0x00490690 + 0x0048ed00
	// The canister's class flag 0x8 is in the reflect set (0x798), so an impact turns it around
	// instead of destroying it. Returns true when the speed left over is loud enough for SOFTBMP2.
	SIMCOPTERREMAKE_API bool ApplyBounce(FCanisterState& InOutState, const FVector& SurfaceNormal);

	// One axis of the gas cloud's scatter. RandomValue is a raw rand() draw.
	SIMCOPTERREMAKE_API int32 CloudOffsetAxis1616(int32 RandomValue);
}
