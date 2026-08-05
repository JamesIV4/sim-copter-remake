// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterDayNight.h"

#include "City/SimCopterDayNightLength.h"
#include "DaySequenceActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/SimCopterSettings.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterDayNight, Log, All);

const TCHAR* const SimCopterDayNight::ParameterCollectionPath =
	TEXT("/Game/Materials/MPC_SimCopterDayNight.MPC_SimCopterDayNight");
const TCHAR* const SimCopterDayNight::NightBlendParameterName = TEXT("NightBlend");
const TCHAR* const SimCopterDayNight::WindowSeedParameterName = TEXT("WindowSeed");
const TCHAR* const SimCopterDayNight::WindowLitFractionParameterName = TEXT("WindowLitFraction");
const TCHAR* const SimCopterDayNight::WindowRowLitFractionParameterName = TEXT("WindowRowLitFraction");
const TCHAR* const SimCopterDayNight::WindowGlowNitsParameterName = TEXT("WindowGlowNits");

namespace
{
// The day sequence actor is placed once and never moves, so a miss only needs re-testing slowly -
// the main menu has no day sequence at all and would otherwise iterate its actors every frame.
constexpr double DaySequenceRescanIntervalSeconds = 2.0;

// Below this the published blend is not worth a collection write. A collection set dirties every
// material instance that samples it, so this is not just a float comparison saved.
constexpr float NightBlendEpsilon = 1.0f / 512.0f;

// The window lights are on well before the blend finishes, so the re-roll edge is taken early in
// the sunset fade rather than at the halfway mark IsNight() uses - by the time a player can SEE a
// window lit, its number has to have been drawn already.
constexpr float WindowRerollNightAlpha = 0.02f;

float GSimCopterNightWindowLitFraction = SimCopterDayNight::DefaultWindowLitFraction;
static FAutoConsoleVariableRef CVarNightWindowLitFraction(
	TEXT("SimCopter.NightWindows.LitFraction"),
	GSimCopterNightWindowLitFraction,
	TEXT("Fraction of a building's windows lit at night, 0..1. Rolled per window from the current ")
	TEXT("seed, so changing this re-selects rather than adding to what is already lit."),
	ECVF_Default);

float GSimCopterNightWindowRowLitFraction = SimCopterDayNight::DefaultWindowRowLitFraction;
static FAutoConsoleVariableRef CVarNightWindowRowLitFraction(
	TEXT("SimCopter.NightWindows.RowLitFraction"),
	GSimCopterNightWindowRowLitFraction,
	TEXT("Chance that a whole floor of a building lights up at once, 0..1. Applied on top of the ")
	TEXT("per-window roll, so a lit row is fully lit."),
	ECVF_Default);

float GSimCopterNightWindowNits = SimCopterDayNight::DefaultWindowGlowNits;
static FAutoConsoleVariableRef CVarNightWindowNits(
	TEXT("SimCopter.NightWindows.Nits"),
	GSimCopterNightWindowNits,
	TEXT("How bright a lit window burns, in nits, before the Settings screen's Emissive Brightness ")
	TEXT("is applied. Raise for a brighter skyline; lower if the bloom halos merge."),
	ECVF_Default);
}

USimCopterDayNightSubsystem* USimCopterDayNightSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World != nullptr ? World->GetSubsystem<USimCopterDayNightSubsystem>() : nullptr;
}

void USimCopterDayNightSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Refresh();
}

void USimCopterDayNightSubsystem::Deinitialize()
{
	CachedDaySequenceActor.Reset();
	CachedParameterCollection = nullptr;
	Super::Deinitialize();
}

TStatId USimCopterDayNightSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USimCopterDayNightSubsystem, STATGROUP_Tickables);
}

void USimCopterDayNightSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
	ApplyTimeOfDaySettings();
	Refresh();
}

ADaySequenceActor* USimCopterDayNightSubsystem::GetDaySequenceActor() const
{
	return CachedDaySequenceActor.Get();
}

ADaySequenceActor* USimCopterDayNightSubsystem::ResolveDaySequenceActor()
{
	if (ADaySequenceActor* Cached = CachedDaySequenceActor.Get())
	{
		return Cached;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	const double Now = FPlatformTime::Seconds();
	if (LastActorScanSeconds >= 0.0 && (Now - LastActorScanSeconds) < DaySequenceRescanIntervalSeconds)
	{
		return nullptr;
	}
	LastActorScanSeconds = Now;

	// ACelestialVaultDaySequenceActor derives from ADaySequenceActor, so the base class finds the
	// shipped level's actor without this having to depend on the CelestialVault type.
	for (TActorIterator<ADaySequenceActor> It(World); It; ++It)
	{
		CachedDaySequenceActor = *It;
		return *It;
	}

	return nullptr;
}

UMaterialParameterCollection* USimCopterDayNightSubsystem::ResolveParameterCollection()
{
	if (CachedParameterCollection != nullptr)
	{
		return CachedParameterCollection;
	}

	CachedParameterCollection = LoadObject<UMaterialParameterCollection>(
		nullptr, SimCopterDayNight::ParameterCollectionPath);

	if (CachedParameterCollection == nullptr && !bWarnedMissingCollection)
	{
		bWarnedMissingCollection = true;
		// Not fatal: without the collection the atlas material's NightBlend keeps its authored
		// default (0), so the city simply stays on its day pages.
		UE_LOG(LogSimCopterDayNight, Warning,
			TEXT("'%s' is missing - run Tools/Unreal/CreateSimCopterMaterials.py. The city's night ")
			TEXT("window lights will not come on."),
			SimCopterDayNight::ParameterCollectionPath);
	}

	return CachedParameterCollection;
}

void USimCopterDayNightSubsystem::Refresh()
{
	const ADaySequenceActor* DaySequenceActor = ResolveDaySequenceActor();
	if (DaySequenceActor == nullptr)
	{
		return;
	}

	const float ActorDayLength = DaySequenceActor->GetDayLength();
	DayLengthHours = ActorDayLength > KINDA_SMALL_NUMBER
		? ActorDayLength
		: SimCopterDayNightFog::DefaultDayLengthHours;

	// GetTimeOfDay falls back to the editor's Time Of Day Preview outside a game world, which is
	// what makes scrubbing that slider move the window lights in the viewport.
	TimeOfDayHours = DaySequenceActor->GetTimeOfDay();

	NightAlpha = SimCopterDayNightFog::ComputeNightAlpha(
		TimeOfDayHours,
		SunriseHour,
		SunsetHour,
		FadeDurationHours,
		DayLengthHours,
		/*bSmoothFade=*/true);

	// The edge, not the state: the seed has to be drawn once as the sun goes down, not re-drawn
	// every frame it stays down (which would make the whole skyline flicker).
	const bool bIsNightNow = NightAlpha >= WindowRerollNightAlpha;
	if (bIsNightNow && !bWasNight)
	{
		RerollNightWindows();
	}
	bWasNight = bIsNightNow;

	PublishWindowTuning();

	if (FMath::IsNearlyEqual(NightAlpha, PublishedNightBlend, NightBlendEpsilon))
	{
		return;
	}

	PublishScalar(SimCopterDayNight::NightBlendParameterName, NightAlpha);
	PublishedNightBlend = NightAlpha;
}

void USimCopterDayNightSubsystem::PublishScalar(const TCHAR* ParameterName, const float Value)
{
	if (UMaterialParameterCollection* Collection = ResolveParameterCollection())
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, Collection, ParameterName, Value);
	}
}

void USimCopterDayNightSubsystem::PublishWindowTuning()
{
	const float LitFraction = FMath::Clamp(GSimCopterNightWindowLitFraction, 0.0f, 1.0f);
	const float RowLitFraction = FMath::Clamp(GSimCopterNightWindowRowLitFraction, 0.0f, 1.0f);

	// One brightness knob over everything emissive, so the windows keep their relationship to the
	// fire and the effect cards when the player pulls it down.
	float GlowNits = FMath::Max(GSimCopterNightWindowNits, 0.0f);
	if (const USimCopterSettings* Settings = USimCopterSettings::Get(this))
	{
		GlowNits *= Settings->GetEmissiveBrightness();
	}

	if (!FMath::IsNearlyEqual(LitFraction, PublishedLitFraction))
	{
		PublishScalar(SimCopterDayNight::WindowLitFractionParameterName, LitFraction);
		PublishedLitFraction = LitFraction;
	}
	if (!FMath::IsNearlyEqual(RowLitFraction, PublishedRowLitFraction))
	{
		PublishScalar(SimCopterDayNight::WindowRowLitFractionParameterName, RowLitFraction);
		PublishedRowLitFraction = RowLitFraction;
	}
	if (!FMath::IsNearlyEqual(GlowNits, PublishedGlowNits))
	{
		PublishScalar(SimCopterDayNight::WindowGlowNitsParameterName, GlowNits);
		PublishedGlowNits = GlowNits;
	}
}

void USimCopterDayNightSubsystem::RerollNightWindows()
{
	// Any value works - the material hashes it - but keeping it off zero and away from huge
	// magnitudes keeps the hash's sin() well conditioned.
	PublishScalar(SimCopterDayNight::WindowSeedParameterName, FMath::FRandRange(1.0f, 1000.0f));
}

bool USimCopterDayNightSubsystem::IsNightForWorld(const UObject* WorldContextObject)
{
	const USimCopterDayNightSubsystem* Subsystem = Get(WorldContextObject);
	return Subsystem != nullptr && Subsystem->IsNight();
}

void USimCopterDayNightSubsystem::ApplyTimeOfDaySettings()
{
	const UWorld* World = GetWorld();
	if (World == nullptr || !World->IsGameWorld())
	{
		// SetTimeOfDay/Pause need the sequence player, which only exists in a game world. The editor
		// preview keeps using Time Of Day Preview, which is the right behaviour while authoring.
		return;
	}

	const USimCopterSettings* Settings = USimCopterSettings::Get(this);
	if (Settings == nullptr)
	{
		return;
	}

	const ESimCopterTimeOfDayMode Mode = Settings->GetTimeOfDayMode();
	const float StaticHours = Settings->GetStaticTimeOfDayHours();
	const float DayMinutes = Settings->GetDayRealMinutes();
	const float NightMinutes = Settings->GetNightRealMinutes();
	const uint8 ModeValue = static_cast<uint8>(Mode);

	const bool bModeUnchanged = ModeValue == AppliedTimeOfDayMode;
	const bool bStaticHoursUnchanged =
		Mode == ESimCopterTimeOfDayMode::Dynamic || FMath::IsNearlyEqual(StaticHours, AppliedStaticTimeOfDayHours);
	const bool bLengthsUnchanged =
		FMath::IsNearlyEqual(DayMinutes, AppliedDayRealMinutes)
		&& FMath::IsNearlyEqual(NightMinutes, AppliedNightRealMinutes);
	if (bModeUnchanged && bStaticHoursUnchanged && bLengthsUnchanged)
	{
		return;
	}

	ADaySequenceActor* DaySequenceActor = ResolveDaySequenceActor();
	if (DaySequenceActor == nullptr)
	{
		return;
	}

	// The pacing component is what turns two real-minute figures into a play rate; it stays the
	// owner of the ramp maths and the Effective Day/Night Minutes readouts. The Settings screen only
	// moves its two inputs. Pushed in both modes so the values are already right when the player
	// switches back to Dynamic.
	if (USimCopterDayNightLengthComponent* Length =
		DaySequenceActor->FindComponentByClass<USimCopterDayNightLengthComponent>())
	{
		Length->DayRealMinutes = DayMinutes;
		Length->NightRealMinutes = NightMinutes;
		Length->RefreshEffectiveDurations();
		Length->RefreshPlayRate();
	}
	AppliedDayRealMinutes = DayMinutes;
	AppliedNightRealMinutes = NightMinutes;

	if (Mode == ESimCopterTimeOfDayMode::Static)
	{
		// Order matters: SetTimeOfDay scrubs with EUpdatePositionMethod::Play, so it RESUMES the
		// sequence. Pausing first and seeking second would leave the clock running.
		DaySequenceActor->SetRunDayCycle(false);
		DaySequenceActor->SetTimeOfDay(StaticHours);
		DaySequenceActor->Pause();
	}
	else
	{
		// Play() refuses outright while bRunDayCycle is false, so the flag has to go back first.
		DaySequenceActor->SetRunDayCycle(true);
		DaySequenceActor->Play();
	}

	AppliedTimeOfDayMode = ModeValue;
	AppliedStaticTimeOfDayHours = StaticHours;

	// A seek is a discontinuity, so the day->night edge Refresh() watches for cannot be trusted
	// across it: dragging the Static Time slider from noon to midnight arrives at night without ever
	// passing through a rising fade. Forget the previous state and let Refresh() re-detect it, which
	// is what makes "the user activates night from the options" roll a fresh set of windows.
	bWasNight = false;

	// The seek moved the clock; publish the new blend now rather than one frame late.
	Refresh();
}
