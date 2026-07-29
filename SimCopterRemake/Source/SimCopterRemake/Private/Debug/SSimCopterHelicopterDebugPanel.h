// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ASimCopterHelicopterPawn;

// Non-shipping developer panel for cycling helicopter models and tools during play.
//
// It is owned by the pawn, works in a free-flight map with no ASimCopterMissionSystemActor,
// and only calls the pawn's public API - never mission internals. Every action it exposes is
// session state: debug grants and refills never touch career ownership, money, ammunition, or
// the saved active helicopter (plan section 6).
class SSimCopterHelicopterDebugPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterHelicopterDebugPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ASimCopterHelicopterPawn>, Pawn)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Rebinds when possession changes so the panel follows the controlled helicopter.
	void SetPawn(TWeakObjectPtr<ASimCopterHelicopterPawn> InPawn) { Pawn = InPawn; }

private:
	TWeakObjectPtr<ASimCopterHelicopterPawn> Pawn;

	ASimCopterHelicopterPawn* GetPawn() const { return Pawn.Get(); }

	FText GetModelLineText() const;
	FText GetModelDetailText() const;
	FText GetModelStatusText() const;
	FText GetCameraModeText() const;
	FText GetToolLineText() const;
	FText GetToolAvailabilityText() const;
	FText GetToolContextText() const;
	FText GetGrantButtonText() const;
	FText GetDispatchServiceText() const;
	FText GetDispatchStatusText() const;
	FText GetMissionStatusText() const;

	// One button per shared-catalog mission type, wrapped to the panel width.
	TSharedRef<SWidget> BuildMissionButtons();

	// All Slate handlers return Handled so a click on the panel can never also fire the
	// world's primary action (plan section 5.2).
	FReply HandleModelPrev();
	FReply HandleModelNext();
	FReply HandleResetCameraOffset();
	FReply HandleToolPrev();
	FReply HandleToolNext();
	FReply HandleUsePressed();
	FReply HandleUseReleased();
	FReply HandleToggleGrant();
	FReply HandleMessagePrev();
	FReply HandleMessageNext();
	FReply HandleRefillTearGas();
	FReply HandleRopeToggle();
	FReply HandleDispatchServicePrev();
	FReply HandleDispatchServiceNext();
	FReply HandleDispatch();
	FReply HandleDispatchChase();
	FReply HandleDispatchClear();
	// One per entry in the shared mission catalog.
	FReply HandleStartMission(int32 TypeMask);

	EVisibility GetMegaphoneRowVisibility() const;
	EVisibility GetTearGasRowVisibility() const;
	EVisibility GetGrantButtonVisibility() const;

	TOptional<float> GetCameraTranslationX() const;
	TOptional<float> GetCameraTranslationY() const;
	TOptional<float> GetCameraTranslationZ() const;
	TOptional<float> GetCameraRotationPitch() const;
	TOptional<float> GetCameraRotationYaw() const;
	TOptional<float> GetCameraRotationRoll() const;
	TOptional<float> GetCameraZoomVerticalFramingStrength() const;
	TOptional<float> GetCameraMaxZoomDistance() const;
	void HandleCameraTranslationXChanged(float Value);
	void HandleCameraTranslationYChanged(float Value);
	void HandleCameraTranslationZChanged(float Value);
	void HandleCameraRotationPitchChanged(float Value);
	void HandleCameraRotationYawChanged(float Value);
	void HandleCameraRotationRollChanged(float Value);
	void HandleCameraZoomVerticalFramingStrengthChanged(float Value);
	void HandleCameraMaxZoomDistanceChanged(float Value);
};
