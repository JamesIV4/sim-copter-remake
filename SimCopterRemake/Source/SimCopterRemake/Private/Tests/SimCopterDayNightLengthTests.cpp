// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterDayNightLength.h"
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterDayNightLengthTest,
	"SimCopter.City.DayNightLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterDayNightLengthTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterDayNightLength;

	// The shipped configuration: sunrise 06:00, sunset 18:00, one hour ramps, over a 24 hour day
	// whose authored cycle is 10 real minutes. 7 minutes of day, 3 of night.
	constexpr float Sunrise = 6.0f;
	constexpr float Sunset = 18.0f;
	constexpr float Transition = 1.0f;
	constexpr float Day = 24.0f;
	constexpr float CycleMinutes = 10.0f;
	constexpr float DayMinutes = 7.0f;
	constexpr float NightMinutes = 3.0f;

	// How long a band takes at a CONSTANT rate. The inverse of ComputePlayRate, written out
	// independently so the test cannot just restate the implementation.
	const auto RealMinutesForBand = [](float BandHours, float DayLengthHours, float TimePerCycleMinutes, float PlayRate)
	{
		return (BandHours / DayLengthHours) * (TimePerCycleMinutes / PlayRate);
	};

	const auto AlphaAt = [&](float Hour, float TransitionHours, bool bSmooth)
	{
		return ComputeNightSpeedAlpha(Hour, Sunrise, Sunset, TransitionHours, Day, bSmooth);
	};

	{
		TestEqual(TEXT("Sunrise 6 to sunset 18 is twelve daylight hours"),
			ComputeDaylightHours(Sunrise, Sunset, Day), 12.0f);
		TestEqual(TEXT("A late sunrise still measures forward to sunset"),
			ComputeDaylightHours(9.0f, 17.0f, Day), 8.0f);
		TestEqual(TEXT("Equal hours is a degenerate band, not a whole day"),
			ComputeDaylightHours(6.0f, 6.0f, Day), 0.0f);
	}

	// The two ramps anchor to sunset and sunrise from OPPOSITE ends: the speed-up starts on sunset,
	// the ease back finishes on sunrise. That asymmetry is the whole request, so it is pinned at
	// every boundary.
	{
		TestEqual(TEXT("Midday runs at day speed"), AlphaAt(12.0f, Transition, false), 0.0f);
		TestEqual(TEXT("The speed-up has not begun at 17:59"), AlphaAt(17.99f, Transition, false), 0.0f);
		TestEqual(TEXT("The speed-up BEGINS exactly at sunset"), AlphaAt(18.0f, Transition, false), 0.0f);
		TestEqual(TEXT("...is halfway by 18:30"), AlphaAt(18.5f, Transition, false), 0.5f);
		TestEqual(TEXT("...and complete by 19:00"), AlphaAt(19.0f, Transition, false), 1.0f);

		TestEqual(TEXT("Midnight is full night speed"), AlphaAt(0.0f, Transition, false), 1.0f);
		TestEqual(TEXT("Still full night speed at 04:59"), AlphaAt(4.99f, Transition, false), 1.0f);
		TestEqual(TEXT("The ease back BEGINS at 05:00, one transition before sunrise"),
			AlphaAt(5.0f, Transition, false), 1.0f);
		TestEqual(TEXT("...is halfway by 05:30"), AlphaAt(5.5f, Transition, false), 0.5f);
		TestEqual(TEXT("...and FINISHES exactly on sunrise"), AlphaAt(6.0f, Transition, false), 0.0f);
		TestEqual(TEXT("Well after sunrise is still day speed"), AlphaAt(9.0f, Transition, false), 0.0f);
	}

	// A longer ramp moves the ease-back's start, not its end - it always lands on sunrise.
	{
		constexpr float LongTransition = 3.0f;
		TestEqual(TEXT("A 3 hour ease back still finishes on sunrise"),
			AlphaAt(6.0f, LongTransition, false), 0.0f);
		TestEqual(TEXT("...and is halfway 1.5 hours earlier"),
			AlphaAt(4.5f, LongTransition, false), 0.5f);
		TestEqual(TEXT("...having begun at 03:00"), AlphaAt(3.0f, LongTransition, false), 1.0f);
		TestEqual(TEXT("A 3 hour speed-up still begins on sunset"),
			AlphaAt(18.0f, LongTransition, false), 0.0f);
		TestEqual(TEXT("...and completes 3 hours later"), AlphaAt(21.0f, LongTransition, false), 1.0f);
	}

	// Zero transition is a hard switch on the hour, and must reproduce the plateau figures EXACTLY.
	{
		TestEqual(TEXT("No ramp: night speed the instant sunset ticks over"),
			AlphaAt(18.0f, 0.0f, false), 1.0f);
		TestEqual(TEXT("No ramp: day speed on sunrise"), AlphaAt(6.0f, 0.0f, false), 0.0f);

		TestEqual(TEXT("With no ramp, daylight is exactly 7 real minutes"),
			ComputeTraversalRealMinutes(Sunrise, Sunset, Sunrise, Sunset, 0.0f,
				Day, CycleMinutes, DayMinutes, NightMinutes, false),
			DayMinutes, 1e-2f);
		TestEqual(TEXT("With no ramp, night is exactly 3 real minutes"),
			ComputeTraversalRealMinutes(Sunset, Sunrise, Sunrise, Sunset, 0.0f,
				Day, CycleMinutes, DayMinutes, NightMinutes, false),
			NightMinutes, 1e-2f);
	}

	// The plateau rates themselves, checked by converting back to real minutes.
	{
		const float DayRate = ComputePlayRate(12.0f, Day, CycleMinutes, DayMinutes);
		const float NightRate = ComputePlayRate(12.0f, Day, CycleMinutes, NightMinutes);

		TestTrue(TEXT("Day is stretched, so it runs slower than even pacing"), DayRate < 1.0f);
		TestTrue(TEXT("Night is compressed, so it runs faster"), NightRate > 1.0f);
		TestEqual(TEXT("Daylight plateau lands on 7 real minutes"),
			RealMinutesForBand(12.0f, Day, CycleMinutes, DayRate), DayMinutes, 1e-3f);
		TestEqual(TEXT("Night plateau lands on 3 real minutes"),
			RealMinutesForBand(12.0f, Day, CycleMinutes, NightRate), NightMinutes, 1e-3f);

		// The blended rate must sit between the two ends and hit them exactly at the anchors.
		TestEqual(TEXT("Rate at midday is the day rate"),
			ComputeCurrentPlayRate(12.0f, Sunrise, Sunset, Transition, Day, CycleMinutes, DayMinutes, NightMinutes, false),
			DayRate, 1e-4f);
		TestEqual(TEXT("Rate at midnight is the night rate"),
			ComputeCurrentPlayRate(0.0f, Sunrise, Sunset, Transition, Day, CycleMinutes, DayMinutes, NightMinutes, false),
			NightRate, 1e-4f);
		const float MidRampRate = ComputeCurrentPlayRate(18.5f, Sunrise, Sunset, Transition, Day, CycleMinutes, DayMinutes, NightMinutes, false);
		TestTrue(TEXT("Mid-ramp sits strictly between the two rates"), MidRampRate > DayRate && MidRampRate < NightRate);
	}

	// With ramps, day is untouched (both ramps sit inside the night) and night pays for them.
	{
		const float RampedDay = ComputeTraversalRealMinutes(Sunrise, Sunset, Sunrise, Sunset,
			Transition, Day, CycleMinutes, DayMinutes, NightMinutes, true);
		const float RampedNight = ComputeTraversalRealMinutes(Sunset, Sunrise, Sunrise, Sunset,
			Transition, Day, CycleMinutes, DayMinutes, NightMinutes, true);

		TestEqual(TEXT("Daylight is still exactly 7 minutes - no ramp falls inside it"),
			RampedDay, DayMinutes, 1e-2f);
		TestTrue(TEXT("Night costs MORE than its plateau figure because of the ramps"),
			RampedNight > NightMinutes);
		TestTrue(TEXT("...but not unboundedly so - under a minute of overhead for two ramp hours"),
			RampedNight < NightMinutes + 1.0f);
	}

	// Smoothing changes the shape but not the anchors.
	{
		TestEqual(TEXT("Smoothed speed-up still starts at day speed"), AlphaAt(18.0f, Transition, true), 0.0f);
		TestEqual(TEXT("Smoothed speed-up still ends at night speed"), AlphaAt(19.0f, Transition, true), 1.0f);
		TestEqual(TEXT("Smoothed speed-up still crosses halfway at the midpoint"),
			AlphaAt(18.5f, Transition, true), 0.5f);
		TestTrue(TEXT("Smoothing eases IN, so a quarter through is behind linear"),
			AlphaAt(18.25f, Transition, true) < AlphaAt(18.25f, Transition, false));
	}

	// Moving sunrise and sunset moves both ramps with them - they are the anchors, not fixed hours.
	{
		constexpr float LateSunrise = 8.0f;
		constexpr float EarlySunset = 16.0f;
		const auto MovedAlpha = [&](float Hour)
		{
			return ComputeNightSpeedAlpha(Hour, LateSunrise, EarlySunset, Transition, Day, false);
		};

		TestEqual(TEXT("Speed-up now begins on the moved sunset"), MovedAlpha(16.0f), 0.0f);
		TestEqual(TEXT("...completing an hour after it"), MovedAlpha(17.0f), 1.0f);
		TestEqual(TEXT("Ease back finishes on the moved sunrise"), MovedAlpha(8.0f), 0.0f);
		TestEqual(TEXT("...having begun an hour before it"), MovedAlpha(7.0f), 1.0f);
		TestEqual(TEXT("The old sunset hour is now deep in the night"), MovedAlpha(18.0f), 1.0f);

		// An 8 hour daylight window still fills exactly the requested minutes with no ramp in it.
		TestEqual(TEXT("A moved, 8 hour daylight band still takes 7 real minutes"),
			ComputeTraversalRealMinutes(LateSunrise, EarlySunset, LateSunrise, EarlySunset, 0.0f,
				Day, CycleMinutes, DayMinutes, NightMinutes, false),
			DayMinutes, 1e-2f);
	}

	// Time Per Cycle cancels out of the rate maths, so the requested durations hold whatever the
	// actor's own cycle length is. This is what stops the two having to be kept in sync.
	{
		for (float Cycle : { 1.0f, 10.0f, 45.0f })
		{
			const float DayRate = ComputePlayRate(12.0f, Day, Cycle, DayMinutes);
			TestEqual(
				*FString::Printf(TEXT("Daylight is 7 real minutes with a %.0f minute cycle"), Cycle),
				RealMinutesForBand(12.0f, Day, Cycle, DayRate), DayMinutes, 1e-3f);
		}
	}

	// Degenerate inputs fall back to the authored speed. A rate of 0 would freeze time of day, which
	// is a far worse failure than simply not applying the override.
	{
		TestEqual(TEXT("Zero band hours"), ComputePlayRate(0.0f, Day, CycleMinutes, DayMinutes), 1.0f);
		TestEqual(TEXT("Zero day length"), ComputePlayRate(12.0f, 0.0f, CycleMinutes, DayMinutes), 1.0f);
		TestEqual(TEXT("Zero cycle"), ComputePlayRate(12.0f, Day, 0.0f, DayMinutes), 1.0f);
		TestEqual(TEXT("Zero requested minutes"), ComputePlayRate(12.0f, Day, CycleMinutes, 0.0f), 1.0f);
		TestEqual(TEXT("Negative requested minutes"), ComputePlayRate(12.0f, Day, CycleMinutes, -5.0f), 1.0f);
		TestEqual(TEXT("A zero-span traversal is zero minutes, not a divide by zero"),
			ComputeTraversalRealMinutes(6.0f, 6.0f, Sunrise, Sunset, Transition, Day,
				CycleMinutes, DayMinutes, NightMinutes, true),
			0.0f);
	}

	return true;
}
