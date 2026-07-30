// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ASimCopterHelicopterPawn;
class SBox;

// Controller-only interaction presentation. Mouse/keyboard cockpit art remains untouched; this
// layer appears only while a held radial, passenger action, or pause state needs an explicit
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

private:
	TWeakObjectPtr<ASimCopterHelicopterPawn> Pawn;
	TSharedPtr<SBox> DispatchWheelHost;
	TSharedPtr<SBox> ToolWheelHost;

	TSharedRef<SWidget> BuildDispatchWheel() const;
	TSharedRef<SWidget> BuildToolWheel() const;
	TSharedRef<SWidget> BuildRadialWheel(
		const TArray<FString>& Labels,
		const FText& Title,
		const FText& Instructions) const;

	EVisibility GetDispatchWheelVisibility() const;
	EVisibility GetToolWheelVisibility() const;
	EVisibility GetPassengerVisibility() const;
	EVisibility GetPauseVisibility() const;
	FText GetPassengerTitle() const;
	FText GetPassengerBody() const;
	FText GetPauseText() const;
};
