// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterDayNightLength.h"

#include "City/SimCopterDayNightFog.h"
#include "DaySequenceActor.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterDayNightLength, Log, All);

namespace SimCopterDayNightLength
{
namespace
{
// Folds an hour that a ramp offset may have pushed below zero back onto the clock face.
float WrapHour(float Hour, float DayLengthHours)
{
	if (DayLengthHours <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float Wrapped = FMath::Fmod(Hour, DayLengthHours);
	return Wrapped < 0.0f ? Wrapped + DayLengthHours : Wrapped;
}
}

float ComputeDaylightHours(float SunriseHour, float SunsetHour, float DayLengthHours)
{
	// Same wrap the fog curve uses - "hours from sunrise forward to sunset" is exactly the age of
	// one event measured from the other - so it is shared rather than written twice.
	return SimCopterDayNightFog::HoursSince(SunsetHour, SunriseHour, DayLengthHours);
}

float ComputeNightSpeedAlpha(
	float CurrentHour,
	float SunriseHour,
	float SunsetHour,
	float TransitionHours,
	float DayLengthHours,
	bool bSmoothTransition)
{
	// The fog's curve already solves this shape: two opposing fades on a wrapping clock, most
	// recent event wins, which is what makes the midnight wrap fall out for free. The only
	// difference is the anchoring - the fog's fades both START on their hour, whereas the ease back
	// to day speed has to FINISH on sunrise - so the sunrise anchor is handed over shifted one
	// transition earlier and the identical maths applies.
	const float EaseBackStartHour = WrapHour(SunriseHour - TransitionHours, DayLengthHours);
	return SimCopterDayNightFog::ComputeNightAlpha(
		CurrentHour,
		EaseBackStartHour,
		SunsetHour,
		TransitionHours,
		DayLengthHours,
		bSmoothTransition);
}

float ComputePlayRate(float BandHours, float DayLengthHours, float TimePerCycleMinutes, float DesiredRealMinutes)
{
	// Any degenerate input leaves the cycle at its authored speed rather than stopping it dead or
	// dividing by zero - a play rate of 0 would freeze time of day entirely.
	if (BandHours <= KINDA_SMALL_NUMBER
		|| DayLengthHours <= KINDA_SMALL_NUMBER
		|| TimePerCycleMinutes <= KINDA_SMALL_NUMBER
		|| DesiredRealMinutes <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}

	const float BandFraction = BandHours / DayLengthHours;
	return (BandFraction * TimePerCycleMinutes) / DesiredRealMinutes;
}

float ComputeCurrentPlayRate(
	float CurrentHour,
	float SunriseHour,
	float SunsetHour,
	float TransitionHours,
	float DayLengthHours,
	float TimePerCycleMinutes,
	float DayRealMinutes,
	float NightRealMinutes,
	bool bSmoothTransition)
{
	const float DaylightHours = ComputeDaylightHours(SunriseHour, SunsetHour, DayLengthHours);
	const float NightHours = DayLengthHours - DaylightHours;

	const float DayRate = ComputePlayRate(DaylightHours, DayLengthHours, TimePerCycleMinutes, DayRealMinutes);
	const float NightRate = ComputePlayRate(NightHours, DayLengthHours, TimePerCycleMinutes, NightRealMinutes);

	const float Alpha = ComputeNightSpeedAlpha(
		CurrentHour, SunriseHour, SunsetHour, TransitionHours, DayLengthHours, bSmoothTransition);

	// The RATE is what eases, not the elapsed time: that is what "smooth speed up and slow down"
	// means on screen - the sun visibly accelerates rather than jumping to a new pace.
	return FMath::Lerp(DayRate, NightRate, Alpha);
}

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
	int32 SampleCount)
{
	const float SpanHours = SimCopterDayNightFog::HoursSince(EndHour, StartHour, DayLengthHours);
	if (SpanHours <= KINDA_SMALL_NUMBER || DayLengthHours <= KINDA_SMALL_NUMBER || SampleCount <= 0)
	{
		return 0.0f;
	}

	// Real minutes per game hour is (TimePerCycle / DayLength) / Rate, so the elapsed real time over
	// a band is the integral of the RECIPROCAL of the rate. Midpoint rule: the rate curve is
	// piecewise smooth with at most two ramps in it, so a few hundred samples is far more resolution
	// than a readout needs, and this only runs when a setting changes.
	const float StepHours = SpanHours / static_cast<float>(SampleCount);
	const float MinutesPerGameHourAtUnitRate = TimePerCycleMinutes / DayLengthHours;

	float TotalMinutes = 0.0f;
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const float SampleHour = WrapHour(StartHour + (static_cast<float>(SampleIndex) + 0.5f) * StepHours, DayLengthHours);
		const float Rate = ComputeCurrentPlayRate(
			SampleHour, SunriseHour, SunsetHour, TransitionHours, DayLengthHours,
			TimePerCycleMinutes, DayRealMinutes, NightRealMinutes, bSmoothTransition);

		if (Rate > KINDA_SMALL_NUMBER)
		{
			TotalMinutes += (MinutesPerGameHourAtUnitRate / Rate) * StepHours;
		}
	}

	return TotalMinutes;
}
}

USimCopterDayNightLengthComponent::USimCopterDayNightLengthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Deliberately NOT bTickInEditor for the play rate: the sequence player only exists in a game
	// world and GetPlayRate is documented as always 1 in editor worlds. The duration readouts are
	// refreshed from PostEditChangeProperty instead, so the panel still answers "what did that
	// actually buy me" without entering PIE.
}

void USimCopterDayNightLengthComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshEffectiveDurations();
	RefreshPlayRate();
}

void USimCopterDayNightLengthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshPlayRate();
}

void USimCopterDayNightLengthComponent::RefreshPlayRate()
{
	ADaySequenceActor* DaySequenceActor = Cast<ADaySequenceActor>(GetOwner());
	if (DaySequenceActor == nullptr)
	{
		if (!bWarnedMissingDaySequence)
		{
			bWarnedMissingDaySequence = true;
			UE_LOG(LogSimCopterDayNightLength, Warning,
				TEXT("%s is not on a DaySequenceActor, so there is no day cycle to pace."),
				*GetPathName());
		}
		return;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr || !World->IsGameWorld())
	{
		return;
	}

	const float DayLengthHours = ResolveDayLengthHours();
	const float CurrentHour = DaySequenceActor->GetTimeOfDay();

	NightSpeedAlpha = SimCopterDayNightLength::ComputeNightSpeedAlpha(
		CurrentHour, SunriseHour, SunsetHour, TransitionHours, DayLengthHours, bSmoothTransition);

	float DesiredPlayRate = 1.0f;
	if (bEnabled)
	{
		DesiredPlayRate = SimCopterDayNightLength::ComputeCurrentPlayRate(
			CurrentHour,
			SunriseHour,
			SunsetHour,
			TransitionHours,
			DayLengthHours,
			ResolveTimePerCycleMinutes(),
			DayRealMinutes,
			NightRealMinutes,
			bSmoothTransition);
	}

	// SetPlayRate is a reliable NetMulticast, so it is not something to fire every frame at full
	// float precision. Mid-ramp the rate moves continuously, so this deliberately quantises: the
	// threshold is small enough to be invisible and large enough to keep the ramp down to a few
	// dozen calls rather than one per frame.
	if (!FMath::IsNearlyEqual(AppliedPlayRate, DesiredPlayRate, 1e-3f))
	{
		DaySequenceActor->SetPlayRate(DesiredPlayRate);
		AppliedPlayRate = DesiredPlayRate;
	}
}

void USimCopterDayNightLengthComponent::RefreshEffectiveDurations()
{
	const float DayLengthHours = ResolveDayLengthHours();
	const float TimePerCycleMinutes = ResolveTimePerCycleMinutes();

	const auto Traverse = [&](float StartHour, float EndHour)
	{
		return SimCopterDayNightLength::ComputeTraversalRealMinutes(
			StartHour, EndHour, SunriseHour, SunsetHour, TransitionHours,
			DayLengthHours, TimePerCycleMinutes, DayRealMinutes, NightRealMinutes, bSmoothTransition);
	};

	EffectiveDayMinutes = Traverse(SunriseHour, SunsetHour);
	EffectiveNightMinutes = Traverse(SunsetHour, SunriseHour);
}

float USimCopterDayNightLengthComponent::ResolveDayLengthHours() const
{
	if (const ADaySequenceActor* DaySequenceActor = Cast<ADaySequenceActor>(GetOwner()))
	{
		const float DayLength = DaySequenceActor->GetDayLength();
		if (DayLength > KINDA_SMALL_NUMBER)
		{
			return DayLength;
		}
	}

	return SimCopterDayNightFog::DefaultDayLengthHours;
}

float USimCopterDayNightLengthComponent::ResolveTimePerCycleMinutes() const
{
	if (const ADaySequenceActor* DaySequenceActor = Cast<ADaySequenceActor>(GetOwner()))
	{
		// GetTimePerCycle answers in hours; everything user-facing here is in real minutes.
		const float TimePerCycleMinutes = DaySequenceActor->GetTimePerCycle() * 60.0f;
		if (TimePerCycleMinutes > KINDA_SMALL_NUMBER)
		{
			return TimePerCycleMinutes;
		}
	}

	// Only reached with no owner or a degenerate cycle. TimePerCycle cancels out of the rate maths,
	// so any positive stand-in gives the same play rates; it only scales the readouts.
	return 10.0f;
}

#if WITH_EDITOR
void USimCopterDayNightLengthComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Retuned mid-PIE, the new pacing should take effect on the spot rather than at the next
	// sunrise. Outside a game world RefreshPlayRate is a no-op, but the readouts still update, so
	// the cost of a longer ramp is visible while editing.
	RefreshEffectiveDurations();
	RefreshPlayRate();
}
#endif
