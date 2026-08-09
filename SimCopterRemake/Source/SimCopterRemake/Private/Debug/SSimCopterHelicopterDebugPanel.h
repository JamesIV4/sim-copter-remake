// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ASimCopterHelicopterPawn;

// Non-shipping developer panel for cycling helicopter models, tools, and calibrating element layout during play.
class SSimCopterHelicopterDebugPanel : public SCompoundWidget
{
public:
	enum class ETab : uint8
	{
		General,
		Calibration
	};

	SLATE_BEGIN_ARGS(SSimCopterHelicopterDebugPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ASimCopterHelicopterPawn>, Pawn)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Rebinds when possession changes so the panel follows the controlled helicopter.
	void SetPawn(TWeakObjectPtr<ASimCopterHelicopterPawn> InPawn) { Pawn = InPawn; }

	void SelectTab(ETab Tab);
	ETab GetActiveTab() const { return ActiveTab; }

private:
	TWeakObjectPtr<ASimCopterHelicopterPawn> Pawn;
	ETab ActiveTab = ETab::General;

	ASimCopterHelicopterPawn* GetPawn() const { return Pawn.Get(); }

	TSharedRef<SWidget> BuildTabHeader();
	TSharedRef<SWidget> BuildGeneralTabContent();
	TSharedRef<SWidget> BuildCalibrationTabContent();

	FText GetModelLineText() const;
	FText GetModelDetailText() const;
	FText GetModelStatusText() const;
	FText GetCameraModeText() const;
	FText GetFlightModelText() const;
	FText GetFlightModelButtonText() const;
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
	FReply HandleToggleEasyFlightModel();
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
	FReply HandleStartSpeeder();

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

	TOptional<float> GetCockpitAttitudeFollowStrength() const;
	TOptional<float> GetCockpitAttitudeLerpSpeed() const;
	void HandleCockpitAttitudeFollowStrengthChanged(float Value);
	void HandleCockpitAttitudeLerpSpeedChanged(float Value);
	TOptional<float> GetCockpitCannonOffsetX() const;
	TOptional<float> GetCockpitCannonOffsetY() const;
	TOptional<float> GetCockpitCannonOffsetZ() const;
	void HandleCockpitCannonOffsetXChanged(float Value);
	void HandleCockpitCannonOffsetYChanged(float Value);
	void HandleCockpitCannonOffsetZChanged(float Value);

	TOptional<float> GetTurbulenceReferenceFps() const;
	void HandleTurbulenceReferenceFpsChanged(float Value);
	TOptional<float> GetFlightReferenceFps() const;
	TOptional<float> GetSpeedChaseReferenceFps() const;
	TOptional<float> GetRotorVisualMultiplier() const;
	TOptional<float> GetVehicleMetallic() const;
	TOptional<float> GetFlashingLightIntensityScale() const;
	TOptional<float> GetWaterTextureFramesPerSecond() const;
	TOptional<float> GetCameraGroundLiftHeightCm() const;
	TOptional<float> GetCameraGroundLiftProbeRangeCm() const;
	TOptional<float> GetCameraGroundLiftFullDistanceCm() const;
	FText GetCameraGroundLiftStatusText() const;
	void HandleFlightReferenceFpsChanged(float Value);
	void HandleSpeedChaseReferenceFpsChanged(float Value);
	void HandleRotorVisualMultiplierChanged(float Value);
	void HandleVehicleMetallicChanged(float Value);
	void HandleFlashingLightIntensityScaleChanged(float Value);
	void HandleWaterTextureFramesPerSecondChanged(float Value);
	void HandleCameraGroundLiftHeightCmChanged(float Value);
	void HandleCameraGroundLiftProbeRangeCmChanged(float Value);
	void HandleCameraGroundLiftFullDistanceCmChanged(float Value);

	TOptional<float> GetRotorDiscOpacity() const;
	void HandleRotorDiscOpacityChanged(float Value);
	TOptional<float> GetRotorDiscColorR() const;
	TOptional<float> GetRotorDiscColorG() const;
	TOptional<float> GetRotorDiscColorB() const;
	void HandleRotorDiscColorRChanged(float Value);
	void HandleRotorDiscColorGChanged(float Value);
	void HandleRotorDiscColorBChanged(float Value);
};
