// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterTrafficJam.h"

namespace SimCopterTrafficJam
{
float GetHeadingBlockScale(const float HeadingDot)
{
	// FUN_0049ee30: iVar11 = FixMul(otherRadius, (dot + 0x10000) >> 1).
	return FMath::Max(0.0f, (1.0f + FMath::Clamp(HeadingDot, -1.0f, 1.0f)) * 0.5f);
}

bool IsSameDirectionBlocker(const float HeadingDot, const int32 YieldCount)
{
	// FUN_0049ee30: `*(int *)(param_1 + 0x127) * 0x2666 + 0x8000 <= iVar13`.
	const float Threshold = SameDirectionDot +
		SameDirectionDotPerYield * static_cast<float>(FMath::Max(0, YieldCount));
	return HeadingDot >= Threshold;
}

float GetQueueHoldDistanceCm(
	const float FollowerRadiusCm,
	const float LeaderRadiusCm,
	const float CmPerOriginalUnit)
{
	return FMath::Max(0.0f, FollowerRadiusCm) +
		FMath::Max(0.0f, LeaderRadiusCm) +
		BlockProbeOriginalUnits * FMath::Max(0.0f, CmPerOriginalUnit);
}

float GetQueueSpeedScale(
	const float ForwardDistanceCm,
	const float HoldDistanceCm,
	const float SlowDistanceCm)
{
	if (ForwardDistanceCm <= HoldDistanceCm)
	{
		return 0.0f;
	}

	const float Band = FMath::Max(1.0f, SlowDistanceCm);
	const float Alpha = FMath::Clamp((ForwardDistanceCm - HoldDistanceCm) / Band, 0.0f, 1.0f);
	return Alpha * Alpha;
}
}
