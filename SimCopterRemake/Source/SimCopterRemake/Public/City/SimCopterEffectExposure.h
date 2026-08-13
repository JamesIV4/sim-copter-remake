// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SimCopterEffectExposure.generated.h"

class FSimCopterExposureViewExtension;
class UDirectionalLightComponent;
class UMaterialInstanceDynamic;

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

// **No floor.** Not everything on the unlit sprite material is a light source: the pedestrian
// sprites and the privanim figure heads are ordinary surfaces that happen to be drawn as unlit
// cards, and a floor is precisely what would make them glow after dark. They get the sun's own
// luminance and nothing else, so they go dark when it does.
constexpr float SurfaceMinimumEmissiveNits = 0.0f;

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

// The sun's illuminance at noon, and the ceiling on the measured floor below. Also the value the
// no-world fallbacks use.
constexpr float NoonIlluminanceLux = 120000.0f;

/**
 * The measured half of the compensation: an effect card is never dimmer than the luminance the
 * auto exposure is actually metering.
 *
 * The sun model above is `Intensity * max(-Direction.Z, 0)` - direct sun on FLAT GROUND - and that
 * cosine collapses to nothing while the sun is still lighting the sky and the city's vertical
 * faces. Through the dawn/dusk window it therefore reports near-darkness for a scene that is still
 * bright, the emissive falls to its 1.5-nit floor, and fire, spray, smoke and dust tonemap to black
 * against everything around them. The compensation was never wrong at noon or at midnight; it had
 * a hole in the hour between.
 *
 * `FSceneViewStateInterface::GetLastAverageSceneLuminance()` is that hole measured directly: the
 * GPU readback of what the exposure metered last frame, in nits, converging immediately rather than
 * following the exposure's own smoothing. Feeding it in as a floor makes the transition continuous
 * by construction - at the crossover the two agree, so there is no step - and needs no constant to
 * calibrate, because it is the same physical quantity the sun model was estimating.
 *
 * Capped at the noon value so the loop cannot run away: the cards contribute to the frame the
 * exposure meters, and a screenful of fire raising its own floor would ring.
 */
SIMCOPTERREMAKE_API float ComputeMeasuredFloorNits(
	float AverageSceneLuminanceNits,
	float EffectBrightness = DefaultEffectBrightness);
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
	// The view extension is the only public way to the exposure readback: ULocalPlayer keeps its
	// view states private, and FSceneView's accessors are ENGINE_API but only reachable from a view.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Nits for an unlit effect card this frame. Cheap to call repeatedly; recomputed once a frame. */
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Effects")
	float GetEffectEmissiveNits();

	/** Ground illuminance from the world's key light, in lux. Zero if there is no directional light. */
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Effects")
	float GetKeyIlluminanceLux();

	/**
	 * Average scene luminance the auto exposure metered last frame, in nits, or 0 when no view state
	 * has reported one yet (a headless run, a scene capture, the first frames of a level).
	 */
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Effects")
	float GetMeasuredSceneLuminanceNits();

	/** Convenience for call sites that only have a world; null-safe, returns the daylight default. */
	static float GetEffectEmissiveNitsForWorld(const UWorld* World);

	/**
	 * Nits for a card standing in for an ordinary SURFACE rather than a light source - the pedestrian
	 * sprites and the privanim figure heads. Same derivation, no minimum, so they track the sun all
	 * the way down to black instead of glowing at midnight.
	 */
	static float GetSurfaceEmissiveNitsForWorld(const UWorld* World);

	/**
	 * Writes the right EmissiveNits onto a dynamic instance of the shared unlit sprite material.
	 *
	 * **Every consumer of `M_SimCopterSpriteTexture` has to call this.** That material bakes a
	 * daylight default (`EMISSIVE_NITS_DEFAULT`, 26000) so a card is never black with nothing driving
	 * it - which means a consumer that creates a MID and forgets the parameter renders at 26000 nits
	 * forever, day and night. That is exactly how the fire, the pedestrian sprites, the figure heads
	 * and the mission markers ended up glowing: six MID sites, one of them writing the parameter.
	 */
	static void ApplyEmissiveNits(
		UMaterialInstanceDynamic* MaterialInstance,
		const UWorld* World,
		bool bIsLightSource);

	/** The parameter the effect materials expose, written by the drawing components. */
	static FName GetEmissiveNitsParameterName() { return FName(TEXT("EmissiveNits")); }

	/**
	 * The player's Emissive Brightness setting, folded into every value above.
	 *
	 * Read through here rather than off `USimCopterSettings` directly so the subsystem does not have
	 * to exist in a world with no game instance (the automation tests, the material preview), and so
	 * the `SimCopter.Effects.Brightness` console variable can still multiply it for live tuning.
	 */
	static float GetBrightnessScale(const UWorld* World);

private:
	/** Re-resolves the key light if the cached one has gone away. */
	UDirectionalLightComponent* ResolveKeyLight();

	/** Last average scene luminance the view extension saw, in nits; 0 when it has seen nothing. */
	float ReadAverageSceneLuminanceNits() const;

	/** Registered for this world's lifetime; records the exposure readback off each frame's view. */
	TSharedPtr<FSimCopterExposureViewExtension, ESPMode::ThreadSafe> ExposureViewExtension;

	/** Brightest directional light in the world, cached across frames. */
	TWeakObjectPtr<UDirectionalLightComponent> KeyLight;

	/** Frame the cached value was computed on, so one scan serves every effect component. */
	uint64 CachedFrame = 0;
	float CachedIlluminanceLux = 0.0f;
	float CachedSceneLuminanceNits = 0.0f;
	float CachedEmissiveNits = 0.0f;
	bool bHasCachedValue = false;

	/** Time of the last full scan, so a world with no directional light does not rescan every frame. */
	double LastScanTimeSeconds = -1.0;
};
