// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimCopterStars.generated.h"

class UMaterialInstanceDynamic;

/**
 * Adjusts how bright the celestial vault's stars render.
 *
 * The stars material is energy conservative: each instance's luminance is derived from its real
 * catalogue magnitude, so there is no plain "intensity" multiplier. The control it does expose is
 * `MagnitudeOffset`, which shifts every star's apparent magnitude before that conversion.
 *
 * **Magnitude runs backwards** - it is a logarithmic scale where smaller means brighter - so a
 * NEGATIVE offset brightens the sky and a positive one dims it. The material's own note is blunt
 * about it: "<0 gives more brightness". One step is a factor of ~2.5 in luminance.
 *
 * Add it to the `CelestialVaultDaySequenceActor`; it finds that actor's own Stars component.
 *
 * It takes the material from the component's slot 0 rather than assuming the plugin default,
 * because the level overrides it (`MI_Stars_EnergyConservative` in CityRender, where the class
 * default is `MI_StarsStar_EnergyConservative`). Whatever is on the component is what gets the
 * override, so swapping the stars material does not silently disable this.
 */
UCLASS(ClassGroup = (SimCopter), meta = (BlueprintSpawnableComponent, DisplayName = "SimCopter Stars"))
class SIMCOPTERREMAKE_API USimCopterStarsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimCopterStarsComponent();

	/** Off leaves the stars on whatever their material authored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stars")
	bool bOverrideMagnitudeOffset = true;

	/**
	 * Shifts every star's apparent magnitude. **Negative is BRIGHTER, positive is dimmer**, and one
	 * whole step is roughly a 2.5x change in luminance, so useful values are small. The material
	 * ships at 0, which is true catalogue magnitudes, and that is what this defaults to.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stars", meta = (DisplayName = "Magnitude Offset (negative = brighter)", EditCondition = "bOverrideMagnitudeOffset", ClampMin = "-10.0", ClampMax = "10.0", UIMin = "-5.0", UIMax = "5.0"))
	float MagnitudeOffset = 0.0f;

	/** The value last pushed at the stars material, for reading off the details panel. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Stars")
	float AppliedMagnitudeOffset = 0.0f;

	/** Pushes MagnitudeOffset at the stars' material instance. */
	UFUNCTION(BlueprintCallable, Category = "Stars")
	void ApplyMagnitudeOffset();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** The owning celestial actor's stars material, creating the MID if it is not one yet. */
	UMaterialInstanceDynamic* ResolveStarsMaterial();

	/** Set once the owner has been checked and found not to be a celestial vault actor. */
	bool bWarnedMissingActor = false;

	/** Set once the stars material has been checked and found to have no MagnitudeOffset. */
	bool bWarnedMissingParameter = false;
};
