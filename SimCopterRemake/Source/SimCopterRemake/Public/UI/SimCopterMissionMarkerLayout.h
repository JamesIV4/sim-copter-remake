// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Box2D.h"

namespace SimCopterMissionMarkerLayout
{
struct SIMCOPTERREMAKE_API FUiObstacle
{
	FBox2D Bounds;
	float Padding = 0.0f;

	FUiObstacle() = default;
	FUiObstacle(const FBox2D& InBounds, const float InPadding)
		: Bounds(InBounds)
		, Padding(InPadding)
	{
	}
};

struct SIMCOPTERREMAKE_API FPlacedMarker
{
	FBox2D Bounds;

	FPlacedMarker() = default;
	explicit FPlacedMarker(const FBox2D& InBounds)
		: Bounds(InBounds)
	{
	}
};

/**
 * Finds the closest viewport-safe marker centre outside every padded UI rectangle and whose
 * overlap with every placed marker is no greater than AllowedOverlapFraction on at least one
 * screen axis.
 *
 * This is deliberately an axis-aligned deconfliction pass, not a radial projection. Candidate
 * centres are selected from the nearest horizontal/vertical separation boundaries.
 */
SIMCOPTERREMAKE_API FVector2D ResolveMarkerCenter(
	const FVector2D& DesiredCenter,
	const FVector2D& MarkerSize,
	const FVector2D& ViewportSize,
	float ViewportEdgePadding,
	TConstArrayView<FUiObstacle> UiObstacles,
	TConstArrayView<FPlacedMarker> PlacedMarkers,
	float AllowedOverlapFraction,
	bool& bOutAdjusted);
}
