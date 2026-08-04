// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimCopterDayNightLength.generated.h"

// Maths for giving daytime and night-time their own real-world durations, kept free of the
// component so it can be tested without a world or a day sequence.
//
// A DaySequenceActor runs one clock at one speed: `TimePerCycle` real time covers the whole
// `DayLength`, so with a 24 hour day and a 10 minute cycle, day and night get 5 minutes each. The
// lever for changing that at runtime is `SetPlayRate`, which scales how fast the cycle advances.
// Ramping it between a day speed and a night speed gives each half its own duration.
namespace SimCopterDayNightLength
{
// Hours the daylight band spans - sunrise forward to sunset, wrapping midnight. Returns 0 for a
// degenerate band where the two hours are equal.
float ComputeDaylightHours(float SunriseHour, float SunsetHour, float DayLengthHours);

// 0 = running at the day speed, 1 = running at the night speed.
//
// Both ramps are anchored to sunset and sunrise, but from opposite ends, because that is how they
// read on a clock:
//   * the speed-up **starts** at sunset and takes TransitionHours to complete;
//   * the ease back down **finishes** at sunrise, so it starts TransitionHours before it.
// With the defaults - sunset 18:00, sunrise 06:00, one hour ramps - the clock is at full night
// speed from 19:00 to 05:00, and both ramps sit inside the night, so daylight is untouched.
float ComputeNightSpeedAlpha(
	float CurrentHour,
	float SunriseHour,
	float SunsetHour,
	float TransitionHours,
	float DayLengthHours,
	bool bSmoothTransition);

// The play rate that makes a band of BandHours take DesiredRealMinutes of wall-clock time.
//
// At rate R the whole DayLength takes TimePerCycle/R, so a band takes
// (BandHours / DayLengthHours) * (TimePerCycleMinutes / R). Solving for R makes TimePerCycle
// **cancel out**: whatever it is set to, the band lands on the requested real minutes. That is
// deliberate - it means the two durations below fully define the cycle and nothing has to be kept
// in sync with the actor's own Time Per Cycle field.
float ComputePlayRate(float BandHours, float DayLengthHours, float TimePerCycleMinutes, float DesiredRealMinutes);

// The play rate to run at right now: the day and night rates blended by ComputeNightSpeedAlpha.
float ComputeCurrentPlayRate(
	float CurrentHour,
	float SunriseHour,
	float SunsetHour,
	float TransitionHours,
	float DayLengthHours,
	float TimePerCycleMinutes,
	float DayRealMinutes,
	float NightRealMinutes,
	bool bSmoothTransition);

// Real-world minutes the clock actually takes to run from StartHour forward to EndHour, integrating
// the ramped rate rather than assuming a constant one.
//
// **The ramps are not free.** DayRealMinutes and NightRealMinutes set the speeds of the two
// PLATEAUS; the hours spent ramping between them run at an in-between speed, so a band containing a
// ramp takes a little longer than its plateau figure suggests. Both ramps sit inside the night by
// default, which is why night drifts past its 3 minutes while day stays exactly on 7. Feeding this
// a TransitionHours of 0 reproduces the plateau figures exactly, which is how the tests pin it.
float ComputeTraversalRealMinutes(
	float StartHour,
	float EndHour,
	float SunriseHour,
	float SunsetHour,
	float TransitionHours,
	float DayLengthHours,
	float TimePerCycleMinutes,
	float DayRealMinutes,
	float NightRealMinutes,
	bool bSmoothTransition,
	int32 SampleCount = 512);
}

/**
 * Gives daytime and night-time separate real-world durations - 7 minutes of day to 3 minutes of
 * night by default, instead of the even 5/5 a 10 minute cycle would otherwise give - easing between
 * the two speeds rather than stepping.
 *
 * Add it to the `CelestialVaultDaySequenceActor`. It watches the time of day and ramps the day
 * sequence's play rate around the configured sunrise and sunset.
 *
 * **Why not `DayInterpCurve`, which looks like it should do this without any code:** that curve is
 * applied in `ADaySequenceActor::WarpEvaluationRange`, which warps the range handed to the *sequence
 * player* for evaluation, and in `GetApparentTimeOfDay`. It does **not** move the player's actual
 * position, which is why `GetApparentTimeOfDay` has to re-apply the curve on top of `GetTimeOfDay`
 * rather than just reading it. The celestial vault places the sun, moon and stars from raw
 * `GetTimeOfDay()` in its own `UpdateBodiesMotion` - it deliberately does not drive the bodies from
 * a procedural sequence, as its header explains - so a curve leaves the sun moving at a constant
 * rate and only skews sequence-driven tracks. Changing the play rate moves the clock itself, which
 * everything reads.
 *
 * **Runtime only.** `GetPlayRate` is documented as always 1 in editor worlds, and the sequence
 * player only exists in a game world, so the pacing does nothing while scrubbing the editor
 * preview. The duration readouts below still update as values are edited.
 */
UCLASS(ClassGroup = (SimCopter), meta = (BlueprintSpawnableComponent, DisplayName = "SimCopter Day/Night Length"))
class SIMCOPTERREMAKE_API USimCopterDayNightLengthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimCopterDayNightLengthComponent();

	/** Off restores the day sequence's own even pacing (play rate 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Length")
	bool bEnabled = true;

	/** Real-world minutes the daylight plateau should take, ramps excluded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Length", meta = (ForceUnits = "Minutes", ClampMin = "0.01", UIMin = "0.5", UIMax = "60.0"))
	float DayRealMinutes = 7.0f;

	/** Real-world minutes the night plateau should take, ramps excluded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Length", meta = (ForceUnits = "Minutes", ClampMin = "0.01", UIMin = "0.5", UIMax = "60.0"))
	float NightRealMinutes = 3.0f;

	/** Start of daylight. The ease back to day speed FINISHES here, having begun one Transition earlier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Length", meta = (DisplayName = "Sunrise Hour", ForceUnits = "Hours", ClampMin = "0.0", ClampMax = "24.0"))
	float SunriseHour = 6.0f;

	/** End of daylight. The speed-up towards night speed STARTS here and completes one Transition later. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Length", meta = (DisplayName = "Sunset Hour", ForceUnits = "Hours", ClampMin = "0.0", ClampMax = "24.0"))
	float SunsetHour = 18.0f;

	/** How long each ramp takes, in time-of-day hours. Zero switches speed on the hour with no ramp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Length", meta = (DisplayName = "Transition Hours", ForceUnits = "Hours", ClampMin = "0.0", ClampMax = "12.0"))
	float TransitionHours = 1.0f;

	/** Eases both ends of each ramp instead of running it linearly, so the pace never changes abruptly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Length")
	bool bSmoothTransition = true;

	/** 0 while running at day speed, 1 at night speed, in between while ramping. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Day/Night Length")
	float NightSpeedAlpha = 0.0f;

	/** The play rate last pushed at the day sequence. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Day/Night Length")
	float AppliedPlayRate = 0.0f;

	/**
	 * What sunrise-to-sunset ACTUALLY takes with the current ramps, in real minutes. Matches
	 * Day Real Minutes exactly while both ramps sit inside the night, which they do by default.
	 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Day/Night Length", meta = (ForceUnits = "Minutes"))
	float EffectiveDayMinutes = 0.0f;

	/**
	 * What sunset-to-sunrise ACTUALLY takes, in real minutes. Reads a little above Night Real
	 * Minutes because the ramp hours run slower than the night plateau they are easing into.
	 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Day/Night Length", meta = (ForceUnits = "Minutes"))
	float EffectiveNightMinutes = 0.0f;

	/** Re-reads the time of day and applies the play rate for wherever it is on the ramp. */
	UFUNCTION(BlueprintCallable, Category = "Day/Night Length")
	void RefreshPlayRate();

	/** Recomputes the Effective Day/Night Minutes readouts. */
	UFUNCTION(BlueprintCallable, Category = "Day/Night Length")
	void RefreshEffectiveDurations();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** Day length in hours from the owning day sequence actor, or the 24 hour default. */
	float ResolveDayLengthHours() const;

	/** Cycle length in real minutes from the owning day sequence actor. */
	float ResolveTimePerCycleMinutes() const;

	/** Set once the owner has been checked and found not to be a day sequence actor. */
	bool bWarnedMissingDaySequence = false;
};
