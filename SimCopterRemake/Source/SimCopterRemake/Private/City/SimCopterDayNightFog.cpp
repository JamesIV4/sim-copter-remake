// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterDayNightFog.h"

#include "Components/ExponentialHeightFogComponent.h"
#include "DaySequenceActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterDayNightFog, Log, All);

namespace SimCopterDayNightFog
{
float HoursSince(float CurrentHour, float EventHour, float DayLengthHours)
{
	if (DayLengthHours <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	// Fmod alone keeps the sign of the dividend, so a time of day behind the event hour would come
	// back negative and read as "this event has not happened yet" instead of "it happened almost a
	// whole day ago". The extra add-and-wrap folds it into [0, DayLengthHours).
	const float Delta = FMath::Fmod(CurrentHour - EventHour, DayLengthHours);
	return Delta < 0.0f ? Delta + DayLengthHours : Delta;
}

float ComputeNightAlpha(
	float CurrentHour,
	float SunriseHour,
	float SunsetHour,
	float FadeDurationHours,
	float DayLengthHours,
	bool bSmoothFade)
{
	const float SinceSunset = HoursSince(CurrentHour, SunsetHour, DayLengthHours);
	const float SinceSunrise = HoursSince(CurrentHour, SunriseHour, DayLengthHours);

	// A zero fade is a hard switch: the most recent event simply wins outright.
	const float Fade = FMath::Max(FadeDurationHours, 0.0f);
	const bool bHeadingIntoNight = SinceSunset <= SinceSunrise;
	const float SinceEvent = bHeadingIntoNight ? SinceSunset : SinceSunrise;
	const float Progress = Fade <= KINDA_SMALL_NUMBER ? 1.0f : FMath::Clamp(SinceEvent / Fade, 0.0f, 1.0f);

	const float Eased = bSmoothFade ? FMath::SmoothStep(0.0f, 1.0f, Progress) : Progress;
	return bHeadingIntoNight ? Eased : 1.0f - Eased;
}

float ComputeFogDensity(
	float CurrentHour,
	float SunriseHour,
	float SunsetHour,
	float FadeDurationHours,
	float DayFogDensity,
	float NightFogDensity,
	float DayLengthHours,
	bool bSmoothFade)
{
	const float NightAlpha = ComputeNightAlpha(CurrentHour, SunriseHour, SunsetHour, FadeDurationHours, DayLengthHours, bSmoothFade);
	return FMath::Lerp(DayFogDensity, NightFogDensity, NightAlpha);
}

const FLinearColor DefaultDayInscatteringLuminance(0.01f, 0.01f, 0.01f, 1.0f);
const FLinearColor DefaultNightInscatteringLuminance(0.008f, 0.008f, 0.01f, 1.0f);

FLinearColor ComputeFogInscatteringLuminance(
	float CurrentHour,
	float SunriseHour,
	float SunsetHour,
	float FadeDurationHours,
	const FLinearColor& DayLuminance,
	const FLinearColor& NightLuminance,
	float DayLengthHours,
	bool bSmoothFade)
{
	const float NightAlpha = ComputeNightAlpha(CurrentHour, SunriseHour, SunsetHour, FadeDurationHours, DayLengthHours, bSmoothFade);

	// The same alpha the density uses, deliberately: that is what "changes at the same time as the
	// fog" means, and it is why both live behind one ComputeNightAlpha rather than two curves that
	// could be tuned apart by accident.
	//
	// Lerped in LINEAR space, which is the space the values are already in - these come out of the
	// picker's R/G/B fields, not its sRGB Hex field. Converting to sRGB to interpolate would bend
	// the ramp for no reason.
	return FMath::Lerp(DayLuminance, NightLuminance, NightAlpha);
}
}

USimCopterDayNightFogComponent::USimCopterDayNightFogComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// The day sequence publishes no runtime "time changed" delegate - the celestial actor moves its
	// own bodies from an override of SequencePlayerUpdated, which is not reachable without
	// subclassing it - so the fog is polled instead. It is one float compare per frame.
	//
	// bTickInEditor matters as much as the runtime tick: GetTimeOfDay() falls back to the editor's
	// Time Of Day Preview outside a game world, so with this set, dragging that slider moves the
	// viewport fog live and the settings below can be tuned without entering PIE.
	bTickInEditor = true;
}

void USimCopterDayNightFogComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshFromDaySequence();
}

void USimCopterDayNightFogComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshFromDaySequence();
}

void USimCopterDayNightFogComponent::RefreshFromDaySequence()
{
	const ADaySequenceActor* DaySequenceActor = Cast<ADaySequenceActor>(GetOwner());
	if (DaySequenceActor == nullptr)
	{
		if (!bWarnedMissingDaySequence)
		{
			bWarnedMissingDaySequence = true;
			UE_LOG(LogSimCopterDayNightFog, Warning,
				TEXT("%s is not on a DaySequenceActor, so there is no time of day to follow. Add it to the CelestialVaultDaySequenceActor."),
				*GetPathName());
		}
		return;
	}

	// The celestial actor places the sun from GetTimeOfDay() (see its UpdateBodiesMotion), not from
	// the apparent time, so the fog follows the same clock and cannot drift from the visible sky.
	ApplyFogForTimeOfDay(DaySequenceActor->GetTimeOfDay());
}

void USimCopterDayNightFogComponent::ApplyFogForTimeOfDay(float TimeOfDayHours)
{
	if (!bEnabled)
	{
		return;
	}

	UExponentialHeightFogComponent* FogComponent = ResolveFogComponent();
	if (FogComponent == nullptr)
	{
		return;
	}

	const float DayLengthHours = ResolveDayLengthHours();

	CurrentTimeOfDay = TimeOfDayHours;
	CurrentNightAlpha = SimCopterDayNightFog::ComputeNightAlpha(
		TimeOfDayHours, SunriseHour, SunsetHour, FadeDurationHours, DayLengthHours, bSmoothFade);

	if (bDriveFogDensity)
	{
		CurrentFogDensity = SimCopterDayNightFog::ComputeFogDensity(
			TimeOfDayHours,
			SunriseHour,
			SunsetHour,
			FadeDurationHours,
			DayFogDensity,
			NightFogDensity,
			DayLengthHours,
			bSmoothFade);

		// SetFogDensity marks the render state dirty every call, so skip the ones that would not
		// change a pixel. A 24 hour day at a ten minute cycle moves the density by ~1e-4 per frame
		// mid-fade, which is well clear of this threshold; the flat day and night stretches sit
		// under it and cost nothing.
		if (!FMath::IsNearlyEqual(FogComponent->FogDensity, CurrentFogDensity, 1e-5f))
		{
			FogComponent->SetFogDensity(CurrentFogDensity);
		}
	}

	if (bDriveInscatteringLuminance)
	{
		CurrentInscatteringLuminance = SimCopterDayNightFog::ComputeFogInscatteringLuminance(
			TimeOfDayHours,
			SunriseHour,
			SunsetHour,
			FadeDurationHours,
			DayInscatteringLuminance,
			NightInscatteringLuminance,
			DayLengthHours,
			bSmoothFade);

		// A much tighter epsilon than the density's: the whole colour range in play here is
		// 0.008..0.01, so 1e-5 would swallow the entire fade and the fog would never turn blue.
		// SetFogInscatteringColor early-outs on an exact match anyway, so this only saves the
		// float compare.
		if (!FogComponent->FogInscatteringLuminance.Equals(CurrentInscatteringLuminance, 1e-7f))
		{
			FogComponent->SetFogInscatteringColor(CurrentInscatteringLuminance);
		}
	}
}

UExponentialHeightFogComponent* USimCopterDayNightFogComponent::ResolveFogComponent()
{
	if (CachedFogComponent != nullptr)
	{
		return CachedFogComponent;
	}

	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return nullptr;
	}

	// Found by class rather than by the celestial actor's named ExponentialHeightFogComponent
	// property, so this module does not have to link the CelestialVault plugin - any day sequence
	// actor carrying a height fog works.
	CachedFogComponent = Owner->FindComponentByClass<UExponentialHeightFogComponent>();
	if (CachedFogComponent == nullptr && !bWarnedMissingFog)
	{
		bWarnedMissingFog = true;
		UE_LOG(LogSimCopterDayNightFog, Warning,
			TEXT("%s found no ExponentialHeightFogComponent on %s; fog density is not being driven."),
			*GetPathName(), *Owner->GetName());
	}

	return CachedFogComponent;
}

float USimCopterDayNightFogComponent::ResolveDayLengthHours() const
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

#if WITH_EDITOR
void USimCopterDayNightFogComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Typing a new hour or density in the details panel should show up in the viewport on the spot,
	// without waiting for the next editor tick or nudging the preview slider.
	RefreshFromDaySequence();
}
#endif
