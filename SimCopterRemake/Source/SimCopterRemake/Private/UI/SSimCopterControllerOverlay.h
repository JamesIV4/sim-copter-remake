// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ASimCopterHelicopterPawn;
class SBox;
class SSimCopterRadialWheel;

// Controller-only interaction presentation. Mouse/keyboard cockpit art remains untouched; this
// layer appears while a radial, passenger action or pause needs an explicit
// controller-visible affordance.
class SSimCopterControllerOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterControllerOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ASimCopterHelicopterPawn>, Pawn)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Equipment can change when the player buys a tool or changes helicopter. Rebuild the small
	// set of labels rather than keeping unavailable entries in the wheel.
	void RefreshRadials();
	void ReleaseRadial(bool bDispatch, int32 ActivatedIndex);

	// Adds only the currently visible controller panels, never the full-screen overlay host.
	void AppendMissionMarkerAvoidanceWidgets(TArray<TSharedPtr<SWidget>>& OutWidgets) const;

private:
	TWeakObjectPtr<ASimCopterHelicopterPawn> Pawn;
	TSharedPtr<SBox> DispatchWheelHost;
	TSharedPtr<SBox> ToolWheelHost;
	TSharedPtr<SWidget> PassengerPanel;
	TSharedPtr<SSimCopterRadialWheel> DispatchWheel;
	TSharedPtr<SSimCopterRadialWheel> ToolWheel;

	TSharedRef<SWidget> BuildDispatchWheel();
	TSharedRef<SWidget> BuildToolWheel();
	TSharedRef<SWidget> BuildRadialWheel(
		const TArray<FString>& Labels,
		const FText& Title,
		const FText& Instructions, TSharedPtr<SSimCopterRadialWheel>& Wheel);

	EVisibility GetDispatchWheelVisibility() const;
	EVisibility GetToolWheelVisibility() const;
	EVisibility GetPassengerVisibility() const;
	EVisibility GetPauseVisibility() const;
	FText GetPassengerTitle() const;
	FText GetPassengerBody() const;
	FText GetPauseText() const;
};
