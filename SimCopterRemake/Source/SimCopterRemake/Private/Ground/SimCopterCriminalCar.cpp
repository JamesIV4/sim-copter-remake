// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterCriminalCar.h"

#include "Flight/SimCopterSpotlight.h"

namespace SimCopterCriminalCar
{
namespace
{
// FUN_004a01f0's three early-outs, as 16.16 upper bounds on the horizontal distance from the
// beam's ground point. The cone widens with the beam's length, so a longer shot marks a wider
// patch of road.
constexpr int32 BandMarkRadius1616[] = {
	0x300000, // band 0: 48.0
	0x480000, // band 1: 72.0
	0x600000, // band 2: 96.0
};

// FUN_0049d980's speed multipliers, 16.16.
constexpr int32 BandSpeedMultiplier1616[] = {
	0x10de1, // band 0: 1.0542
	0x150d7, // band 1: 1.3158
	0x18590, // band 2: 1.5217
};

// The multiplier a fleeing car runs at when the beam is not on it.
constexpr int32 UnmarkedFleeingMultiplier1616 = 0x1c000; // 1.75

constexpr float Fixed1616ToFloat = 1.0f / 65536.0f;
}

bool IsPursuitTarget(const int32 MessageId, const bool bFleeing)
{
	// FUN_0049dab0: obj[0x14] == 0x11e || (obj[5] & 8).
	return MessageId == CriminalCarMessageId || bFleeing;
}

float GetSpotlightMarkRadiusOriginalUnits(const int32 Band)
{
	if (Band < 0 || Band >= UE_ARRAY_COUNT(BandMarkRadius1616))
	{
		// FUN_004a01f0 returns without marking for any band it does not recognise, which is
		// band 3 - the beam is too long to mark anything.
		return 0.0f;
	}
	return static_cast<float>(BandMarkRadius1616[Band]) * Fixed1616ToFloat;
}

int32 AccumulateSpotlightMark(const int32 Current, const bool bLit, const bool bSpotlightActive)
{
	if (!bSpotlightActive)
	{
		// FUN_004a01f0's DAT_00503aa0 == 3 branch zeroes the counter outright. Taking the beam
		// off a car un-marks it completely rather than letting it fade.
		return 0;
	}
	if (!bLit || Current >= SpotlightMarkMax)
	{
		return Current;
	}
	return Current + SpotlightMarkStep;
}

float GetFleeingSpeedMultiplier(const bool bFleeing, const int32 SpotlightMark, const int32 Band)
{
	if (!bFleeing)
	{
		// FUN_0049d980 leaves the base speed alone unless obj[5] & 8 is set.
		return 1.0f;
	}
	if (SpotlightMark > 0 && Band >= 0 && Band < UE_ARRAY_COUNT(BandSpeedMultiplier1616))
	{
		return static_cast<float>(BandSpeedMultiplier1616[Band]) * Fixed1616ToFloat;
	}
	return static_cast<float>(UnmarkedFleeingMultiplier1616) * Fixed1616ToFloat;
}

bool AcceptsStopOrder(
	const EState State,
	const int32 SpotlightMark,
	const int32 CallerMessageId,
	const bool bAlreadyStopping)
{
	// FUN_004b89a0, in its own order.
	if (State == EState::Arrested || State == EState::Leaving)
	{
		return false;
	}

	// A police caller overrides the state machine entirely - but only against a car the player
	// has actually lit up. This single test is what makes the spotlight the pursuit mechanic.
	if (CallerMessageId == PoliceCarMessageId && SpotlightMark != 0)
	{
		return true;
	}

	if (State != EState::Stopping && State != EState::Idling)
	{
		return false;
	}
	// veh[4] & 0x30: already decelerating, or already at rest.
	return !bAlreadyStopping;
}

int32 GetOfficerPersonState(const bool bHasTarget, const bool bTargetFleeing)
{
	return (bHasTarget && bTargetFleeing) ? OfficerStateAgainstFleeing : OfficerStateDefault;
}

int32 GetTileStepDistance(const FIntPoint& A, const FIntPoint& B)
{
	// FUN_0049b000: octile, larger axis plus half the smaller.
	const int32 DeltaX = FMath::Abs(A.X - B.X);
	const int32 DeltaY = FMath::Abs(A.Y - B.Y);
	return DeltaY < DeltaX ? (DeltaY >> 1) + DeltaX : DeltaY + (DeltaX >> 1);
}
}
