// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterTearGas.h"

#include "Flight/SimCopterWaterGameplay.h"

namespace SimCopterTearGas
{
	using SimCopterWaterGameplay::FixedMul;
	using SimCopterWaterGameplay::Normalize1616;
	using SimCopterWaterGameplay::DirectionToFixed;
	using SimCopterWaterGameplay::DirectionToFloat;

	FCanisterState MakeLaunchState(const FVector& Direction, const int32 ForwardSpeed1616)
	{
		FCanisterState State;
		State.Direction1616 = DirectionToFixed(Direction);
		// FUN_00484d20 passes heli[0x4e] + 0x320000 as the emitter's speed: the canister inherits
		// the airframe's forward speed, so a fast run throws it further than a hover does.
		State.Speed1616 = ForwardSpeed1616 + LaunchSpeedBonus1616;
		State.Life1616 = FuseLife1616;
		State.EffectTimer1616 = TrailInterval1616;
		State.bDetonated = false;
		return State;
	}

	FCanisterFrame AdvanceCanisterFrame(FCanisterState& InOutState, const int32 Delta1616)
	{
		FCanisterFrame Result;

		// The fuse is spent before the step moves anything, and a slot that has already burst is
		// simply freed - the original unlinks its node and clears the active bit here.
		InOutState.Life1616 -= Delta1616;
		if (InOutState.Life1616 < 1)
		{
			if (InOutState.bDetonated)
			{
				Result.bAlive = false;
				return Result;
			}
			InOutState.bDetonated = true;
			InOutState.Life1616 = CloudLife1616;
			// Zeroed, not reloaded: the timer below goes negative on this same step, so the first
			// puff of gas comes out with the pop rather than 0.3 s later.
			InOutState.EffectTimer1616 = 0;
			Result.bDetonatedThisFrame = true;
		}

		// Shared with the debris pool: one per cent of the speed goes every frame, gravity is a
		// per-second acceleration applied to the *scaled* direction, and renormalising the result
		// is what turns the vector back into a direction plus a new speed.
		InOutState.Speed1616 -= FixedMul(Drag1616, InOutState.Speed1616);

		FIntVector Velocity1616(
			FixedMul(InOutState.Direction1616.X, InOutState.Speed1616),
			FixedMul(InOutState.Direction1616.Y, InOutState.Speed1616),
			FixedMul(InOutState.Direction1616.Z, InOutState.Speed1616));
		Velocity1616.Z -= FixedMul(Gravity1616, Delta1616);

		InOutState.Direction1616 = Normalize1616(Velocity1616, InOutState.Speed1616);

		const int32 Distance1616 = FixedMul(InOutState.Speed1616, Delta1616);
		Result.Travel1616 = FIntVector(
			FixedMul(InOutState.Direction1616.X, Distance1616),
			FixedMul(InOutState.Direction1616.Y, Distance1616),
			FixedMul(InOutState.Direction1616.Z, Distance1616));

		InOutState.EffectTimer1616 -= Delta1616;
		if (InOutState.EffectTimer1616 < 0)
		{
			if (InOutState.bDetonated)
			{
				Result.bEmitCloudPuff = true;
				InOutState.EffectTimer1616 = CloudPuffInterval1616;
			}
			else
			{
				Result.bEmitTrail = true;
				InOutState.EffectTimer1616 = TrailInterval1616;
			}
		}

		return Result;
	}

	bool ApplyBounce(FCanisterState& InOutState, const FVector& SurfaceNormal)
	{
		const FVector Normal = SurfaceNormal.GetSafeNormal(SMALL_NUMBER, FVector::UpVector);
		const FVector Incoming = DirectionToFloat(InOutState.Direction1616);
		const FVector Reflected = Incoming - 2.0f * FVector::DotProduct(Incoming, Normal) * Normal;
		InOutState.Direction1616 = DirectionToFixed(Reflected);

		InOutState.Speed1616 = FixedMul(BounceDamping1616, InOutState.Speed1616);
		return InOutState.Speed1616 > BounceSoundSpeed1616;
	}

	int32 CloudOffsetAxis1616(const int32 RandomValue)
	{
		// The decompile's sign gymnastics reduce to a modulo of a non-negative rand() draw, so the
		// scatter is a whole number of units in [-19, +20] - never a fraction.
		const int32 Units = CloudSpreadCenterUnits - (FMath::Abs(RandomValue) % CloudSpreadRangeUnits);
		return Units * 0x10000;
	}
}
