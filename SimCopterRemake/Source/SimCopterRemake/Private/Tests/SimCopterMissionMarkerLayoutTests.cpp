// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UI/SimCopterMissionMarkerLayout.h"

namespace
{
float UiBoundaryMeasure(
	const FVector2D& Point,
	const FBox2D& Bounds,
	const FVector2D& MarkerSize,
	const float Padding)
{
	const FVector2D HalfExtents =
		Bounds.GetExtent() + MarkerSize * 0.5f + FVector2D(Padding, Padding);
	const FVector2D Local = Point - Bounds.GetCenter();
	return FMath::Max(
		FMath::Abs(Local.X) / HalfExtents.X,
		FMath::Abs(Local.Y) / HalfExtents.Y);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMissionMarkerUiAvoidanceTest,
	"SimCopter.Missions.MarkerUiAvoidance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionMarkerUiAvoidanceTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterMissionMarkerLayout;

	const FVector2D Viewport(1000.0f, 700.0f);
	const FVector2D MarkerSize(100.0f, 60.0f);
	constexpr float EdgePadding = 10.0f;
	constexpr float UiPadding = 12.0f;
	constexpr float AllowedOverlap = 0.5f;
	const FBox2D CenterPanelBounds(FVector2D(400.0f, 250.0f), FVector2D(600.0f, 450.0f));
	const TArray<FUiObstacle> CenterPanel = { FUiObstacle(CenterPanelBounds, UiPadding) };
	const FVector2D DesiredCenter(540.0f, 370.0f);

	bool bAdjusted = false;
	const FVector2D Result = ResolveMarkerCenter(
		DesiredCenter, MarkerSize, Viewport, EdgePadding,
		CenterPanel, TConstArrayView<FPlacedMarker>(), AllowedOverlap, bAdjusted);
	TestTrue(TEXT("A marker covered by UI is adjusted"), bAdjusted);
	TestTrue(TEXT("The marker reaches the exact padded UI outline"),
		FMath::IsNearlyEqual(
			UiBoundaryMeasure(Result, CenterPanelBounds, MarkerSize, UiPadding), 1.0f, 0.001f));
	TestTrue(TEXT("UI avoidance moves along one screen axis without radial projection"),
		FMath::IsNearlyEqual(Result.X, DesiredCenter.X, 0.001f) ||
		FMath::IsNearlyEqual(Result.Y, DesiredCenter.Y, 0.001f));

	const FBox2D BottomPanelBounds(FVector2D(600.0f, 500.0f), FVector2D(1000.0f, 700.0f));
	const TArray<FUiObstacle> BottomPanel = { FUiObstacle(BottomPanelBounds, UiPadding) };
	const FVector2D BottomDesired(700.0f, 650.0f);
	const FVector2D BottomResult = ResolveMarkerCenter(
		BottomDesired, MarkerSize, Viewport, EdgePadding,
		BottomPanel, TConstArrayView<FPlacedMarker>(), AllowedOverlap, bAdjusted);
	TestTrue(TEXT("A screen-edge panel sends the marker to a visible outline edge"),
		UiBoundaryMeasure(BottomResult, BottomPanelBounds, MarkerSize, UiPadding) >= 1.0f - 0.001f);
	TestTrue(TEXT("Screen-edge avoidance also moves along one axis"),
		FMath::IsNearlyEqual(BottomResult.X, BottomDesired.X, 0.001f) ||
		FMath::IsNearlyEqual(BottomResult.Y, BottomDesired.Y, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMissionMarkerOverlapTest,
	"SimCopter.Missions.MarkerOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionMarkerOverlapTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterMissionMarkerLayout;

	const FVector2D Viewport(1000.0f, 700.0f);
	const FVector2D MarkerSize(100.0f, 60.0f);
	constexpr float EdgePadding = 10.0f;
	constexpr float AllowedOverlap = 0.5f;
	const FVector2D ExistingCenter(500.0f, 350.0f);
	const FBox2D ExistingBounds(
		ExistingCenter - MarkerSize * 0.5f,
		ExistingCenter + MarkerSize * 0.5f);
	const TArray<FPlacedMarker> PlacedMarkers = { FPlacedMarker(ExistingBounds) };

	bool bAdjusted = false;
	FVector2D Result = ResolveMarkerCenter(
		FVector2D(400.0f, 300.0f), MarkerSize, Viewport,
		EdgePadding, TConstArrayView<FUiObstacle>(), TConstArrayView<FPlacedMarker>(),
		AllowedOverlap, bAdjusted);
	TestEqual(TEXT("A marker with no neighbours is unchanged"), Result, FVector2D(400.0f, 300.0f));
	TestFalse(TEXT("An unchanged marker is not marked adjusted"), bAdjusted);

	Result = ResolveMarkerCenter(
		ExistingCenter, MarkerSize, Viewport, EdgePadding,
		TConstArrayView<FUiObstacle>(), PlacedMarkers, AllowedOverlap, bAdjusted);
	const FVector2D Delta = Result - ExistingCenter;
	const bool bHasHorizontalClearance = FMath::IsNearlyEqual(FMath::Abs(Delta.X), MarkerSize.X * 0.5f, 0.001f);
	const bool bHasVerticalClearance = FMath::IsNearlyEqual(FMath::Abs(Delta.Y), MarkerSize.Y * 0.5f, 0.001f);
	TestTrue(TEXT("Coincident markers are separated"), bAdjusted);
	TestTrue(TEXT("Markers may retain exactly fifty-percent overlap on one axis"),
		bHasHorizontalClearance || bHasVerticalClearance);
	TestTrue(TEXT("Separation moves along one screen axis instead of a radial projection"),
		FMath::IsNearlyZero(Delta.X, 0.001f) || FMath::IsNearlyZero(Delta.Y, 0.001f));

	const FVector2D FiftyPercentHorizontalOverlap(
		ExistingCenter.X + MarkerSize.X * 0.5f,
		ExistingCenter.Y);
	Result = ResolveMarkerCenter(
		FiftyPercentHorizontalOverlap, MarkerSize, Viewport, EdgePadding,
		TConstArrayView<FUiObstacle>(), PlacedMarkers, AllowedOverlap, bAdjusted);
	TestEqual(TEXT("An existing fifty-percent overlap is allowed"), Result, FiftyPercentHorizontalOverlap);
	TestFalse(TEXT("An allowed overlap is not adjusted"), bAdjusted);

	Result = ResolveMarkerCenter(
		FVector2D(-100.0f, 900.0f), MarkerSize, Viewport,
		EdgePadding, TConstArrayView<FUiObstacle>(), TConstArrayView<FPlacedMarker>(),
		AllowedOverlap, bAdjusted);
	TestEqual(TEXT("The viewport edge remains padded"), Result, FVector2D(60.0f, 660.0f));
	return true;
}
