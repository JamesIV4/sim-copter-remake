// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterSpotlight.h"

#include "Flight/SimCopterFlightModel.h"

namespace SimCopterSpotlight
{
// SCHOOK: SpotlightDistanceClamp 0x00489250
int32 ClampMarchDistance(int32 RawDistance1616)
{
	return RawDistance1616 > DistanceClampThreshold1616 ? MaxDistance1616 : RawDistance1616;
}

// SCHOOK: SpotlightDistanceSmoothing 0x00489250
int32 SmoothDistance(int32 PreviousDistance1616, int32 RawDistance1616, bool bFlying)
{
	// (previous * 7 + raw) >> 3 - only while flying; parked the raw value is used directly.
	return bFlying ? ((PreviousDistance1616 * 7 + RawDistance1616) >> 3) : RawDistance1616;
}

// SCHOOK: SpotlightBandSelect 0x00489250
int32 SelectBand(int32 Distance1616)
{
	if (Distance1616 < Band0Max1616 + 1)
	{
		return 0;
	}
	if (Distance1616 < Band1Max1616 + 1)
	{
		return 1;
	}
	return Distance1616 > Band2Max1616 ? 3 : 2;
}

// SCHOOK: SpotlightConeScale 0x00489250
int32 SelectNodeScale(int32 Distance1616)
{
	// Div(distance, 512.0) * 10, floored at 0.3.
	const int32 Scale = SimCopterFixed::Div(Distance1616, 0x2000000) * 10;
	return FMath::Max(Scale, MinNodeScale1616);
}

// SCHOOK: SpotlightAimClamp 0x00489730
int32 ClampAim(int32 Aim1616)
{
	if (Aim1616 > AimClamp1616)
	{
		return AimClamp1616;
	}
	if (Aim1616 < -AimClamp1616)
	{
		return -AimClamp1616;
	}
	return Aim1616;
}
}
