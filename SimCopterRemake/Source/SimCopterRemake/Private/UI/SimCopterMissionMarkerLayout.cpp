// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SimCopterMissionMarkerLayout.h"

namespace SimCopterMissionMarkerLayout
{
namespace
{
constexpr float SeparationTolerance = 0.001f;

FVector2D GetMinimumCenterDistance(
	const FPlacedMarker& PlacedMarker,
	const FVector2D& MarkerHalfSize,
	const float AllowedOverlapFraction)
{
	const float RequiredFraction = 1.0f - AllowedOverlapFraction;
	return (PlacedMarker.Bounds.GetExtent() + MarkerHalfSize) * RequiredFraction;
}

FVector2D GetMinimumCenterDistance(
	const FUiObstacle& UiObstacle,
	const FVector2D& MarkerHalfSize)
{
	const float Padding = FMath::Max(0.0f, UiObstacle.Padding);
	return UiObstacle.Bounds.GetExtent() + MarkerHalfSize + FVector2D(Padding, Padding);
}

bool ConflictsWithUi(
	const FVector2D& Point,
	const FVector2D& MarkerHalfSize,
	const FUiObstacle& UiObstacle)
{
	if (!UiObstacle.Bounds.bIsValid)
	{
		return false;
	}

	const FVector2D MinimumDistance = GetMinimumCenterDistance(UiObstacle, MarkerHalfSize);
	const FVector2D Delta = Point - UiObstacle.Bounds.GetCenter();
	return FMath::Abs(Delta.X) < MinimumDistance.X - SeparationTolerance &&
		FMath::Abs(Delta.Y) < MinimumDistance.Y - SeparationTolerance;
}

bool ConflictsWithMarker(
	const FVector2D& Point,
	const FVector2D& MarkerHalfSize,
	const FPlacedMarker& PlacedMarker,
	const float AllowedOverlapFraction)
{
	if (!PlacedMarker.Bounds.bIsValid)
	{
		return false;
	}

	const FVector2D MinimumDistance = GetMinimumCenterDistance(
		PlacedMarker, MarkerHalfSize, AllowedOverlapFraction);
	const FVector2D Delta = Point - PlacedMarker.Bounds.GetCenter();
	return FMath::Abs(Delta.X) < MinimumDistance.X - SeparationTolerance &&
		FMath::Abs(Delta.Y) < MinimumDistance.Y - SeparationTolerance;
}

bool IsLegal(
	const FVector2D& Point,
	const FVector2D& MarkerHalfSize,
	const TConstArrayView<FUiObstacle> UiObstacles,
	const TConstArrayView<FPlacedMarker> PlacedMarkers,
	const float AllowedOverlapFraction)
{
	for (const FUiObstacle& UiObstacle : UiObstacles)
	{
		if (ConflictsWithUi(Point, MarkerHalfSize, UiObstacle))
		{
			return false;
		}
	}
	for (const FPlacedMarker& PlacedMarker : PlacedMarkers)
	{
		if (ConflictsWithMarker(
			Point, MarkerHalfSize, PlacedMarker, AllowedOverlapFraction))
		{
			return false;
		}
	}
	return true;
}

float GetConflictPenalty(
	const FVector2D& Point,
	const FVector2D& MarkerHalfSize,
	const TConstArrayView<FUiObstacle> UiObstacles,
	const TConstArrayView<FPlacedMarker> PlacedMarkers,
	const float AllowedOverlapFraction)
{
	float Penalty = 0.0f;
	for (const FUiObstacle& UiObstacle : UiObstacles)
	{
		if (!UiObstacle.Bounds.bIsValid)
		{
			continue;
		}

		const FVector2D MinimumDistance = GetMinimumCenterDistance(UiObstacle, MarkerHalfSize);
		const FVector2D Delta = Point - UiObstacle.Bounds.GetCenter();
		const float HorizontalPenetration = FMath::Max(
			0.0f, MinimumDistance.X - FMath::Abs(Delta.X));
		const float VerticalPenetration = FMath::Max(
			0.0f, MinimumDistance.Y - FMath::Abs(Delta.Y));
		if (HorizontalPenetration > 0.0f && VerticalPenetration > 0.0f)
		{
			Penalty += FMath::Min(HorizontalPenetration, VerticalPenetration);
		}
	}
	for (const FPlacedMarker& PlacedMarker : PlacedMarkers)
	{
		if (!PlacedMarker.Bounds.bIsValid)
		{
			continue;
		}

		const FVector2D MinimumDistance = GetMinimumCenterDistance(
			PlacedMarker, MarkerHalfSize, AllowedOverlapFraction);
		const FVector2D Delta = Point - PlacedMarker.Bounds.GetCenter();
		const float HorizontalPenetration = FMath::Max(
			0.0f, MinimumDistance.X - FMath::Abs(Delta.X));
		const float VerticalPenetration = FMath::Max(
			0.0f, MinimumDistance.Y - FMath::Abs(Delta.Y));
		if (HorizontalPenetration > 0.0f && VerticalPenetration > 0.0f)
		{
			Penalty += FMath::Min(HorizontalPenetration, VerticalPenetration);
		}
	}
	return Penalty;
}

void AddCandidateCoordinate(TArray<float>& Coordinates, const float Coordinate)
{
	for (const float Existing : Coordinates)
	{
		if (FMath::IsNearlyEqual(Existing, Coordinate, SeparationTolerance))
		{
			return;
		}
	}
	Coordinates.Add(Coordinate);
}
}

FVector2D ResolveMarkerCenter(
	const FVector2D& DesiredCenter,
	const FVector2D& MarkerSize,
	const FVector2D& ViewportSize,
	const float ViewportEdgePadding,
	const TConstArrayView<FUiObstacle> UiObstacles,
	const TConstArrayView<FPlacedMarker> PlacedMarkers,
	const float AllowedOverlapFraction,
	bool& bOutAdjusted)
{
	const FVector2D HalfSize(
		FMath::Max(0.0f, MarkerSize.X) * 0.5f,
		FMath::Max(0.0f, MarkerSize.Y) * 0.5f);
	const float EdgePadding = FMath::Max(0.0f, ViewportEdgePadding);
	FVector2D SafeMin = HalfSize + FVector2D(EdgePadding, EdgePadding);
	FVector2D SafeMax = ViewportSize - SafeMin;
	if (SafeMax.X < SafeMin.X)
	{
		SafeMin.X = SafeMax.X = ViewportSize.X * 0.5f;
	}
	if (SafeMax.Y < SafeMin.Y)
	{
		SafeMin.Y = SafeMax.Y = ViewportSize.Y * 0.5f;
	}

	const FVector2D ClampedDesired(
		FMath::Clamp(DesiredCenter.X, SafeMin.X, SafeMax.X),
		FMath::Clamp(DesiredCenter.Y, SafeMin.Y, SafeMax.Y));
	const float ClampedOverlap = FMath::Clamp(AllowedOverlapFraction, 0.0f, 1.0f);
	if (IsLegal(ClampedDesired, HalfSize, UiObstacles, PlacedMarkers, ClampedOverlap))
	{
		bOutAdjusted = FVector2D::Distance(DesiredCenter, ClampedDesired) > 0.5f;
		return ClampedDesired;
	}

	// The nearest legal position for axis-aligned rectangles must lie on the desired coordinate,
	// a viewport edge, or one of another marker's horizontal/vertical separation boundaries.
	TArray<float> CandidateX;
	TArray<float> CandidateY;
	AddCandidateCoordinate(CandidateX, ClampedDesired.X);
	AddCandidateCoordinate(CandidateX, SafeMin.X);
	AddCandidateCoordinate(CandidateX, SafeMax.X);
	AddCandidateCoordinate(CandidateY, ClampedDesired.Y);
	AddCandidateCoordinate(CandidateY, SafeMin.Y);
	AddCandidateCoordinate(CandidateY, SafeMax.Y);

	for (const FUiObstacle& UiObstacle : UiObstacles)
	{
		if (!UiObstacle.Bounds.bIsValid)
		{
			continue;
		}

		const FVector2D MinimumDistance = GetMinimumCenterDistance(UiObstacle, HalfSize);
		const FVector2D Center = UiObstacle.Bounds.GetCenter();
		AddCandidateCoordinate(CandidateX, FMath::Clamp(Center.X - MinimumDistance.X, SafeMin.X, SafeMax.X));
		AddCandidateCoordinate(CandidateX, FMath::Clamp(Center.X + MinimumDistance.X, SafeMin.X, SafeMax.X));
		AddCandidateCoordinate(CandidateY, FMath::Clamp(Center.Y - MinimumDistance.Y, SafeMin.Y, SafeMax.Y));
		AddCandidateCoordinate(CandidateY, FMath::Clamp(Center.Y + MinimumDistance.Y, SafeMin.Y, SafeMax.Y));
	}

	for (const FPlacedMarker& PlacedMarker : PlacedMarkers)
	{
		if (!PlacedMarker.Bounds.bIsValid)
		{
			continue;
		}

		const FVector2D MinimumDistance = GetMinimumCenterDistance(
			PlacedMarker, HalfSize, ClampedOverlap);
		const FVector2D Center = PlacedMarker.Bounds.GetCenter();
		AddCandidateCoordinate(CandidateX, FMath::Clamp(Center.X - MinimumDistance.X, SafeMin.X, SafeMax.X));
		AddCandidateCoordinate(CandidateX, FMath::Clamp(Center.X + MinimumDistance.X, SafeMin.X, SafeMax.X));
		AddCandidateCoordinate(CandidateY, FMath::Clamp(Center.Y - MinimumDistance.Y, SafeMin.Y, SafeMax.Y));
		AddCandidateCoordinate(CandidateY, FMath::Clamp(Center.Y + MinimumDistance.Y, SafeMin.Y, SafeMax.Y));
	}

	FVector2D BestPoint = ClampedDesired;
	bool bFoundLegal = false;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	float BestPenalty = TNumericLimits<float>::Max();
	for (const float X : CandidateX)
	{
		for (const float Y : CandidateY)
		{
			const FVector2D Candidate(X, Y);
			const bool bLegal = IsLegal(
				Candidate, HalfSize, UiObstacles, PlacedMarkers, ClampedOverlap);
			const float DistanceSquared = FVector2D::DistSquared(ClampedDesired, Candidate);
			if (bLegal)
			{
				if (!bFoundLegal || DistanceSquared < BestDistanceSquared)
				{
					bFoundLegal = true;
					BestDistanceSquared = DistanceSquared;
					BestPoint = Candidate;
				}
				continue;
			}

			if (!bFoundLegal)
			{
				const float Penalty = GetConflictPenalty(
					Candidate, HalfSize, UiObstacles, PlacedMarkers, ClampedOverlap);
				if (Penalty < BestPenalty - SeparationTolerance ||
					(FMath::IsNearlyEqual(Penalty, BestPenalty, SeparationTolerance) &&
						DistanceSquared < BestDistanceSquared))
				{
					BestPenalty = Penalty;
					BestDistanceSquared = DistanceSquared;
					BestPoint = Candidate;
				}
			}
		}
	}

	bOutAdjusted = FVector2D::Distance(DesiredCenter, BestPoint) > 0.5f;
	return BestPoint;
}
}
