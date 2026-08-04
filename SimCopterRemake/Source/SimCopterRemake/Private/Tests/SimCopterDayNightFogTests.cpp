// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterDayNightFog.h"
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterDayNightFogCurveTest,
	"SimCopter.City.DayNightFog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterDayNightFogCurveTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterDayNightFog;

	// The shipped configuration: sunup 06:00, sunset 18:00, a one hour fade, over a 24 hour day.
	constexpr float Sunrise = 6.0f;
	constexpr float Sunset = 18.0f;
	constexpr float Fade = 1.0f;
	constexpr float Day = 24.0f;
	constexpr float DayDensity = 0.1f;
	constexpr float NightDensity = 0.25f;

	const auto NightAlphaLinear = [&](float Hour)
	{
		return ComputeNightAlpha(Hour, Sunrise, Sunset, Fade, Day, /*bSmoothFade*/ false);
	};
	const auto DensityLinear = [&](float Hour)
	{
		return ComputeFogDensity(Hour, Sunrise, Sunset, Fade, DayDensity, NightDensity, Day, /*bSmoothFade*/ false);
	};

	// Wrapping is the whole reason HoursSince exists: a raw Fmod would report a negative age for any
	// time of day that sits before the event hour, which reads as "not yet happened".
	{
		TestEqual(TEXT("An hour after the event"), HoursSince(19.0f, 18.0f, Day), 1.0f);
		TestEqual(TEXT("An hour BEFORE the event is almost a whole day after it"), HoursSince(17.0f, 18.0f, Day), 23.0f);
		TestEqual(TEXT("On the hour"), HoursSince(18.0f, 18.0f, Day), 0.0f);
		TestEqual(TEXT("Midnight against an evening event"), HoursSince(0.0f, 18.0f, Day), 6.0f);
	}

	// The flat stretches: full day between the fades, full night between the fades.
	{
		TestEqual(TEXT("Midday is full day density"), DensityLinear(12.0f), DayDensity);
		TestEqual(TEXT("Just before sunset is still full day"), DensityLinear(17.99f), DayDensity);
		TestEqual(TEXT("Midnight is full night density"), DensityLinear(0.0f), NightDensity);
		TestEqual(TEXT("Small hours are still full night"), DensityLinear(3.0f), NightDensity);
		TestEqual(TEXT("Just before sunup is still full night"), DensityLinear(5.99f), NightDensity);
	}

	// The fades themselves START on the configured hour and finish a fade length later.
	{
		TestEqual(TEXT("Sunset begins at the day value"), NightAlphaLinear(18.0f), 0.0f);
		TestEqual(TEXT("Half an hour into a one hour sunset is halfway"), NightAlphaLinear(18.5f), 0.5f);
		TestEqual(TEXT("The sunset fade is complete an hour later"), NightAlphaLinear(19.0f), 1.0f);
		TestEqual(TEXT("Sunup begins at the night value"), NightAlphaLinear(6.0f), 1.0f);
		TestEqual(TEXT("Half an hour into sunup is halfway back"), NightAlphaLinear(6.5f), 0.5f);
		TestEqual(TEXT("The sunup fade is complete an hour later"), NightAlphaLinear(7.0f), 0.0f);

		TestEqual(TEXT("Mid-sunset density is the midpoint of the two values"),
			DensityLinear(18.5f), (DayDensity + NightDensity) * 0.5f);
	}

	// Night spans midnight, which is the case the "most recent event wins" rule exists to make free.
	// Walking the clock right through it must never dip back towards the day value.
	{
		float PreviousAlpha = NightAlphaLinear(19.0f);
		for (float Hour = 19.0f; Hour < 30.0f; Hour += 0.25f)
		{
			const float WrappedHour = FMath::Fmod(Hour, Day);
			const float Alpha = NightAlphaLinear(WrappedHour);
			TestTrue(
				*FString::Printf(TEXT("Night holds at %.2f h (alpha %.3f)"), WrappedHour, Alpha),
				FMath::IsNearlyEqual(Alpha, 1.0f));
			PreviousAlpha = Alpha;
		}
		TestEqual(TEXT("Night is unbroken right up to sunup"), PreviousAlpha, 1.0f);
	}

	// A zero fade is a hard switch on the hour rather than a divide by zero.
	{
		TestEqual(TEXT("Sunset with no fade is night immediately"),
			ComputeNightAlpha(18.0f, Sunrise, Sunset, 0.0f, Day, false), 1.0f);
		TestEqual(TEXT("A moment before sunset with no fade is still day"),
			ComputeNightAlpha(17.99f, Sunrise, Sunset, 0.0f, Day, false), 0.0f);
	}

	// A fade longer than the night it sits in cannot leave the alpha outside 0..1 or stall: the
	// newer event just takes the fade over from wherever the other one had reached.
	{
		constexpr float LongFade = 10.0f;
		constexpr float LateSunrise = 2.0f; // only 8 hours of night, fed a 10 hour fade
		for (float Hour = 0.0f; Hour < Day; Hour += 0.5f)
		{
			const float Alpha = ComputeNightAlpha(Hour, LateSunrise, Sunset, LongFade, Day, false);
			TestTrue(
				*FString::Printf(TEXT("An over-long fade stays in range at %.1f h (alpha %.3f)"), Hour, Alpha),
				Alpha >= 0.0f && Alpha <= 1.0f);
		}
	}

	// Smoothing changes the shape of the fade but not its endpoints or its midpoint.
	{
		TestEqual(TEXT("Smoothed sunset still starts at day"),
			ComputeNightAlpha(18.0f, Sunrise, Sunset, Fade, Day, true), 0.0f);
		TestEqual(TEXT("Smoothed sunset still ends at night"),
			ComputeNightAlpha(19.0f, Sunrise, Sunset, Fade, Day, true), 1.0f);
		TestEqual(TEXT("Smoothed sunset still crosses the midpoint halfway"),
			ComputeNightAlpha(18.5f, Sunrise, Sunset, Fade, Day, true), 0.5f);
		TestTrue(TEXT("Smoothing eases IN, so a quarter through is behind linear"),
			ComputeNightAlpha(18.25f, Sunrise, Sunset, Fade, Day, true) < NightAlphaLinear(18.25f));
	}

	// A shortened day cycle still lands the fades on the configured hours, because everything wraps
	// on the day length the sequence reports rather than on a hard-coded 24.
	{
		constexpr float ShortDay = 12.0f;
		TestEqual(TEXT("A 12 hour day wraps on 12"), HoursSince(1.0f, 11.0f, ShortDay), 2.0f);
		TestEqual(TEXT("Sunset still starts the fade on a short day"),
			ComputeNightAlpha(9.0f, 3.0f, 9.0f, Fade, ShortDay, false), 0.0f);
		TestEqual(TEXT("...and completes it a fade length later"),
			ComputeNightAlpha(10.0f, 3.0f, 9.0f, Fade, ShortDay, false), 1.0f);
	}

	return true;
}
