// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SimCopterStreetLights.generated.h"

class USpotLightComponent;

// The real light under a LAMP35..38 street light.
//
// DIVERGENCE, and an intended one. The original has no dynamic lighting at all: a street light's
// "beam" is eighteen face-type-11 quads plus a painted pool on the pavement, drawn whatever the
// hour. The remake keeps that art and hangs an actual downward spot light off the same apex, so a
// lit street reads as lit at night instead of as a glowing decal on unlit tarmac.
//
// Every value that positions the light is measured off the model rather than invented -
// SimCopterRoadDecorations::TryGetStreetLightEmitter reads the apex, throw and spread straight out
// of those cards. Only the intensity and colour are the remake's, and they follow the conventions
// the car headlights already set (unitless, InverseExposureBlend 1, no shadows, off in Low Power).
UCLASS(ClassGroup = (SimCopter), meta = (BlueprintSpawnableComponent))
class SIMCOPTERREMAKE_API USimCopterStreetLightsComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USimCopterStreetLightsComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// One placed lamp, in this component's space.
	struct FPlacement
	{
		FVector Location = FVector::ZeroVector;
		float ConeLengthCm = 0.0f;
		float ConeHalfAngleDegrees = 0.0f;
	};

	// Rebuilds the pool. Called once per city build.
	void SetStreetLights(TArray<FPlacement> InPlacements);
	void ClearStreetLights();
	int32 GetStreetLightCount() const { return Placements.Num(); }

	UPROPERTY(EditAnywhere, Category = "SimCopter|Street Lights")
	bool bEnabled = true;

	// Same base as the car headlights: 9,000 unitless is roughly 14 candelas once the engine
	// converts it, which is the scale a gameplay light has to be on next to a 120,000 lux sun.
	// See Docs/memory/simcopter-exposure-scale.md.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Street Lights", meta = (ClampMin = "0.0"))
	float Intensity = 9000.0f;

	// Warm sodium-vapour white. The lamp's own cards are palette-coloured art and do not carry a
	// light colour, so this is the remake's, chosen to sit beside the headlights' 255,244,214.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Street Lights")
	FColor Color = FColor(255, 226, 170);

	// The cone reaches the pavement at ConeLengthCm, so an attenuation radius of exactly that clips
	// the falloff off at the kerb and the pool ends in a hard ring. Give it room past the painted
	// cone's own footprint instead.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Street Lights", meta = (ClampMin = "1.0"))
	float AttenuationRadiusScale = 2.5f;

	// Unlike the car headlights and the building beacons, street lights are NOT dropped by Low Power
	// Graphics by default. The reason those two are cut is volume: dozens of moving cars carrying
	// two spotlights each, and hundreds of blink markers, are exactly what the standard deferred
	// light loop is worst at. A city's street lights are a static handful by comparison - one per
	// fourth T junction - and they are the only thing lighting the roads at night, so cutting them
	// makes a Low Power night unnavigable rather than merely plainer. Set this if the profile says
	// otherwise on a given machine.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Street Lights")
	bool bDisableInLowPower = false;

	// Softens the edge: the inner cone is this fraction of the measured half angle.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Street Lights", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float InnerConeFraction = 0.55f;

	// A street light is one of the few lights in the city with a genuinely static shadow caster
	// under it (the pole, the kerb), but there are hundreds of them and MegaLights pays per
	// shadowed light. Off by default; the painted pool already grounds the lamp.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Street Lights")
	bool bCastShadows = false;

	// Street lights come on at dusk with everything else. 0 leaves them fully off in daylight,
	// which is what the day/night pass wants; raise it to keep a trace of them lit at noon.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Street Lights", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DaytimeIntensityScale = 0.0f;

	// Below this the components are hidden outright rather than left on at a nearly-zero intensity,
	// so a daylit city pays nothing for them at all.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Street Lights", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinimumVisibleScale = 0.02f;

private:
	// Pushes bEnabled / Low Power / the current night alpha at the pool. Cheap and idempotent:
	// it early-outs unless the resulting scale actually moved.
	void RefreshStreetLights();

	UPROPERTY(Transient)
	TArray<TObjectPtr<USpotLightComponent>> SpotLights;

	TArray<FPlacement> Placements;

	FDelegateHandle LowPowerChangedHandle;
	float AppliedIntensityScale = -1.0f;
};
