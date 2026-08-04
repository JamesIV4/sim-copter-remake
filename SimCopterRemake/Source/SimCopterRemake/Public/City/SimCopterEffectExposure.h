// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SimCopterEffectExposure.generated.h"

class UDirectionalLightComponent;

/**
 * How bright an unlit effect card has to be to read against the sun that is lighting the city.
 *
 * The original had no lighting model: an effect was a SIM3D palette index stamped straight into the
 * frame buffer, so the remake's fire, water spray, dust, tear gas and blinking marker cards are all
 * unlit materials whose colour goes into Emissive. Emissive is a number of NITS, and how bright a
 * given number of nits looks depends entirely on what the auto exposure is metering.
 *
 * The level's `CelestialVaultDaySequenceActor` runs a physically scaled sun - 120,000 lux at noon -
 * where the `SimCopterDayNightCycleActor` it replaced ran 4 lux with a hand-set exposure. Everything
 * here was authored against the old scale, so under the new one the cards sit four orders of
 * magnitude below the exposure and tonemap to black.
 *
 * Rather than pick a constant and retune it whenever the sky changes, the emissive is derived from
 * the sun itself: a card is drawn EffectBrightness times as bright as white ground under the same
 * light. That ratio is what the original's flat palette colours effectively had, and it holds at
 * noon, at dusk and at midnight on its own.
 */
namespace SimCopterEffectExposure
{
// How bright an effect card is relative to a white diffuse surface under the same light.
//
// **One, not 1.4.** The effect materials already multiply their palette colour by a hard 1.4 of
// their own, which is the pop the cards were authored with; this supplies the white-ground
// luminance that boost is applied to. Setting 1.4 here as well double counts it and makes every
// effect twice as hot as it was before the day sequence went in.
constexpr float DefaultEffectBrightness = 1.0f;

// A floor, in nits, so an effect is never *nothing*. Deep night is ~0.1 lux of moonlight, which
// works out to hundredths of a nit - below the exposure's own floor, so fire at midnight would
// disappear instead of being the brightest thing in the frame. This is roughly a candle at arm's
// length, and at night the exposure makes it glow, which is what a fire should do.
constexpr float DefaultMinimumEmissiveNits = 1.5f;

// Illuminance in lux on flat ground from a directional light of IntensityLux pointing along
// LightDirection. Lambert's cosine law with an up-facing surface, so a light at the horizon (or
// below it) contributes nothing and one straight overhead contributes all of it.
SIMCOPTERREMAKE_API float ComputeGroundIlluminanceLux(float IntensityLux, const FVector& LightDirection);

// Nits an effect card must emit to sit EffectBrightness times as bright as white ground under
// KeyIlluminanceLux. A Lambertian white surface at illuminance E has luminance E/PI, so this is
// E * Brightness / PI, floored at MinimumNits.
SIMCOPTERREMAKE_API float ComputeEffectEmissiveNits(
	float KeyIlluminanceLux,
	float EffectBrightness = DefaultEffectBrightness,
	float MinimumNits = DefaultMinimumEmissiveNits);
}

/**
 * Publishes the current effect emissive value for a world, resolved from that world's key light.
 *
 * The drawing components (`USimCopterParticleFXComponent`, `USimCopterFlashingLightsComponent`) ask
 * for it when they rebuild and push it into their card material's `EmissiveNits` parameter. It is
 * cached per frame, because every one of them asks and the answer cannot change within a frame.
 *
 * The key light is found by scanning for the brightest directional light component in the world -
 * the celestial vault's sun is a component on the day sequence actor, not an `ADirectionalLight`,
 * so an actor-class search finds nothing. The result is cached and only re-scanned when it goes
 * stale, which is what keeps this off the per-frame cost.
 */
UCLASS()
class SIMCOPTERREMAKE_API USimCopterEffectExposureSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Nits for an unlit effect card this frame. Cheap to call repeatedly; recomputed once a frame. */
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Effects")
	float GetEffectEmissiveNits();

	/** Ground illuminance from the world's key light, in lux. Zero if there is no directional light. */
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Effects")
	float GetKeyIlluminanceLux();

	/** Convenience for call sites that only have a world; null-safe, returns the daylight default. */
	static float GetEffectEmissiveNitsForWorld(const UWorld* World);

	/** The parameter the effect materials expose, written by the drawing components. */
	static FName GetEmissiveNitsParameterName() { return FName(TEXT("EmissiveNits")); }

private:
	/** Re-resolves the key light if the cached one has gone away. */
	UDirectionalLightComponent* ResolveKeyLight();

	/** Brightest directional light in the world, cached across frames. */
	TWeakObjectPtr<UDirectionalLightComponent> KeyLight;

	/** Frame the cached value was computed on, so one scan serves every effect component. */
	uint64 CachedFrame = 0;
	float CachedIlluminanceLux = 0.0f;
	float CachedEmissiveNits = 0.0f;
	bool bHasCachedValue = false;

	/** Time of the last full scan, so a world with no directional light does not rescan every frame. */
	double LastScanTimeSeconds = -1.0;
};
