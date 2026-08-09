// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterDayNight.h"

#include "City/SimCopterDayNightLength.h"
#include "DaySequenceActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/SimCopterLowPowerMode.h"
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
const TCHAR* const SimCopterDayNight::LowPowerParameterName = TEXT("LowPower");

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

// --- surface shading ---------------------------------------------------------------------------
// The collection scalars these publish into. The same nine names are declared on the Python side in
// Tools/Unreal/CreateSimCopterMaterials.py (SURFACE_SHADING_FAMILIES); they have to agree, or a
// material silently compiles the parameter to a constant zero and the surface turns black.
const TCHAR* const SurfaceShadingParameterNames[] = {
	TEXT("TreeMaxBrightness"),    TEXT("TreeRoughness"),    TEXT("TreeSpecular"),
	TEXT("TerrainMaxBrightness"), TEXT("TerrainRoughness"), TEXT("TerrainSpecular"),
	TEXT("CityMaxBrightness"),    TEXT("CityRoughness"),    TEXT("CitySpecular"),
	TEXT("CityWindowRoughness"), TEXT("CityWindowSpecular"),
	TEXT("WaterMaxBrightness"),   TEXT("WaterRoughness"),   TEXT("WaterSpecular"),
	TEXT("WaterShoreRoughness"), TEXT("WaterShoreSpecular"), TEXT("WaterShoreFadeWidth"),
	TEXT("WaterShoreEdgeNoiseStrength"), TEXT("WaterShoreEdgeNoiseScale"),
	TEXT("WaterShoreEdgeNoiseStrength2"), TEXT("WaterShoreEdgeNoiseScale2"),
	TEXT("WaterShoreEdgeNoiseSpeed"), TEXT("WaterShoreEdgeNoiseSpeed2"),
	TEXT("WaterDetailNormalStrength"), TEXT("WaterDetailNormalScale"),
};

float GSimCopterTreeMaxBrightness = SimCopterDayNight::DefaultTreeMaxBrightness;
static FAutoConsoleVariableRef CVarTreeMaxBrightness(
	TEXT("SimCopter.Shading.TreeMaxBrightness"),
	GSimCopterTreeMaxBrightness,
	TEXT("Albedo ceiling for tree and sign cards, 0..1. Hue preserving: the texel is scaled down ")
	TEXT("until its brightest channel reaches this, so bright palette entries stop clipping to ")
	TEXT("white at noon. Real foliage is around 0.15-0.25."),
	ECVF_Default);

float GSimCopterTreeRoughness = SimCopterDayNight::DefaultTreeRoughness;
static FAutoConsoleVariableRef CVarTreeRoughness(
	TEXT("SimCopter.Shading.TreeRoughness"),
	GSimCopterTreeRoughness,
	TEXT("Roughness of tree and sign cards, 0..1. Near 1 spreads the highlight out; low values ")
	TEXT("give the canopy a wet sheen, which the world-up card normal makes worse."),
	ECVF_Default);

float GSimCopterTreeSpecular = SimCopterDayNight::DefaultTreeSpecular;
static FAutoConsoleVariableRef CVarTreeSpecular(
	TEXT("SimCopter.Shading.TreeSpecular"),
	GSimCopterTreeSpecular,
	TEXT("Reflectivity of tree and sign cards, 0..1 (UE maps this to F0 0..0.08). Leaves are ")
	TEXT("almost non-specular, so this wants to be near zero."),
	ECVF_Default);

float GSimCopterTerrainMaxBrightness = SimCopterDayNight::DefaultTerrainMaxBrightness;
static FAutoConsoleVariableRef CVarTerrainMaxBrightness(
	TEXT("SimCopter.Shading.TerrainMaxBrightness"),
	GSimCopterTerrainMaxBrightness,
	TEXT("Albedo ceiling for the ground, 0..1. Same hue-preserving clamp as the trees."),
	ECVF_Default);

float GSimCopterTerrainRoughness = SimCopterDayNight::DefaultTerrainRoughness;
static FAutoConsoleVariableRef CVarTerrainRoughness(
	TEXT("SimCopter.Shading.TerrainRoughness"),
	GSimCopterTerrainRoughness,
	TEXT("Roughness of the ground, 0..1. Earth and grass are matte."),
	ECVF_Default);

float GSimCopterTerrainSpecular = SimCopterDayNight::DefaultTerrainSpecular;
static FAutoConsoleVariableRef CVarTerrainSpecular(
	TEXT("SimCopter.Shading.TerrainSpecular"),
	GSimCopterTerrainSpecular,
	TEXT("Reflectivity of the ground, 0..1. Raise it and a whole map of dirt reads as damp stone."),
	ECVF_Default);

float GSimCopterCityMaxBrightness = SimCopterDayNight::DefaultCityMaxBrightness;
static FAutoConsoleVariableRef CVarCityMaxBrightness(
	TEXT("SimCopter.Shading.CityMaxBrightness"),
	GSimCopterCityMaxBrightness,
	TEXT("Albedo ceiling for buildings and roads, 0..1 - the atlas pages, the direct-image faces ")
	TEXT("and the flat palette-coloured ones. The vehicles share the last of those materials, so ")
	TEXT("this caps them too."),
	ECVF_Default);

float GSimCopterCityRoughness = SimCopterDayNight::DefaultCityRoughness;
static FAutoConsoleVariableRef CVarCityRoughness(
	TEXT("SimCopter.Shading.CityRoughness"),
	GSimCopterCityRoughness,
	TEXT("Roughness of buildings and roads, 0..1. Starts at the ground's value so the city does ")
	TEXT("not stand out beside it; raise the specular instead if glass wants a highlight."),
	ECVF_Default);

float GSimCopterCitySpecular = SimCopterDayNight::DefaultCitySpecular;
static FAutoConsoleVariableRef CVarCitySpecular(
	TEXT("SimCopter.Shading.CitySpecular"),
	GSimCopterCitySpecular,
	TEXT("Reflectivity of buildings and roads, 0..1. This is the shine that made the city read as ")
	TEXT("wet plastic next to matte ground."),
	ECVF_Default);

float GSimCopterCityWindowRoughness = SimCopterDayNight::DefaultCityWindowRoughness;
static FAutoConsoleVariableRef CVarCityWindowRoughness(
	TEXT("SimCopter.Shading.CityWindowRoughness"),
	GSimCopterCityWindowRoughness,
	TEXT("Roughness of the WINDOWS, 0..1, where the hand-painted mask says there is a pane. Low, ")
	TEXT("because glass is the one part of a building that should reflect. Applies by day as well ")
	TEXT("as by night - a window is glass whether or not the light behind it is on."),
	ECVF_Default);

float GSimCopterCityWindowSpecular = SimCopterDayNight::DefaultCityWindowSpecular;
static FAutoConsoleVariableRef CVarCityWindowSpecular(
	TEXT("SimCopter.Shading.CityWindowSpecular"),
	GSimCopterCityWindowSpecular,
	TEXT("Reflectivity of the windows, 0..1. Set it to CitySpecular to turn the glass back off."),
	ECVF_Default);

float GSimCopterWaterMaxBrightness = SimCopterDayNight::DefaultWaterMaxBrightness;
static FAutoConsoleVariableRef CVarWaterMaxBrightness(
	TEXT("SimCopter.Shading.WaterMaxBrightness"),
	GSimCopterWaterMaxBrightness,
	TEXT("Albedo ceiling for water, 0..1. Higher than the land's: water carries its brightness in ")
	TEXT("the specular, and clamping the albedo hard just makes it muddy."),
	ECVF_Default);

float GSimCopterWaterRoughness = SimCopterDayNight::DefaultWaterRoughness;
static FAutoConsoleVariableRef CVarWaterRoughness(
	TEXT("SimCopter.Shading.WaterRoughness"),
	GSimCopterWaterRoughness,
	TEXT("Roughness of water, 0..1. This is the sun-glint knob - low is a mirror, high is haze."),
	ECVF_Default);

float GSimCopterWaterSpecular = SimCopterDayNight::DefaultWaterSpecular;
static FAutoConsoleVariableRef CVarWaterSpecular(
	TEXT("SimCopter.Shading.WaterSpecular"),
	GSimCopterWaterSpecular,
	TEXT("Reflectivity of water, 0..1 (UE maps this to F0 0..0.08). Water is the one surface in ")
	TEXT("the city that should be near the dielectric maximum."),
	ECVF_Default);

float GSimCopterWaterShoreRoughness = SimCopterDayNight::DefaultWaterShoreRoughness;
static FAutoConsoleVariableRef CVarWaterShoreRoughness(
	TEXT("SimCopter.Shading.WaterShoreRoughness"),
	GSimCopterWaterShoreRoughness,
	TEXT("Roughness at the welded shoreline, 0..1, easing out to WaterRoughness offshore. Matte, ")
	TEXT("so the reflection ends as wet sand instead of on a hard stair-stepped line."),
	ECVF_Default);

float GSimCopterWaterShoreSpecular = SimCopterDayNight::DefaultWaterShoreSpecular;
static FAutoConsoleVariableRef CVarWaterShoreSpecular(
	TEXT("SimCopter.Shading.WaterShoreSpecular"),
	GSimCopterWaterShoreSpecular,
	TEXT("Reflectivity at the shoreline, 0..1, easing out to WaterSpecular offshore."),
	ECVF_Default);

float GSimCopterWaterShoreFadeWidth = SimCopterDayNight::DefaultWaterShoreFadeWidth;
static FAutoConsoleVariableRef CVarWaterShoreFadeWidth(
	TEXT("SimCopter.Shading.WaterShoreFadeWidth"),
	GSimCopterWaterShoreFadeWidth,
	TEXT("How far the shoreline fade reaches, as a fraction 0..1 of the water mesh's wave-weight ")
	TEXT("ramp. Larger pushes the reflection further offshore; 0 restores the hard edge."),
	ECVF_Default);

float GSimCopterWaterShoreEdgeNoiseStrength = SimCopterDayNight::DefaultWaterShoreEdgeNoiseStrength;
static FAutoConsoleVariableRef CVarWaterShoreEdgeNoiseStrength(
	TEXT("SimCopter.Shading.WaterShoreEdgeNoiseStrength"),
	GSimCopterWaterShoreEdgeNoiseStrength,
	TEXT("How far the shoreline fade's contour wanders off the tile grid, in weight units. This is ")
	TEXT("the knob that stops the fade tracing tile edges and quad diagonals; 0 puts it back on ")
	TEXT("the grid. How far it moves on screen depends on how fast the weight ramps, so a shallow ")
	TEXT("coast wanders more than a steep one."),
	ECVF_Default);

float GSimCopterWaterShoreEdgeNoiseScale = SimCopterDayNight::DefaultWaterShoreEdgeNoiseScale;
static FAutoConsoleVariableRef CVarWaterShoreEdgeNoiseScale(
	TEXT("SimCopter.Shading.WaterShoreEdgeNoiseScale"),
	GSimCopterWaterShoreEdgeNoiseScale,
	TEXT("Wavelength of that wander in centimetres. Wants to be SEVERAL tiles - at tile scale it ")
	TEXT("adds fizz to the same stair-step instead of hiding it."),
	ECVF_Default);

float GSimCopterWaterShoreEdgeNoiseStrength2 = SimCopterDayNight::DefaultWaterShoreEdgeNoiseStrength2;
static FAutoConsoleVariableRef CVarWaterShoreEdgeNoiseStrength2(
	TEXT("SimCopter.Shading.WaterShoreEdgeNoiseStrength2"),
	GSimCopterWaterShoreEdgeNoiseStrength2,
	TEXT("Second, independent shoreline warp layer - its own amplitude, added on top of the first ")
	TEXT("rather than chained off it. 0 turns it off and leaves layer 1 alone."),
	ECVF_Default);

float GSimCopterWaterShoreEdgeNoiseScale2 = SimCopterDayNight::DefaultWaterShoreEdgeNoiseScale2;
static FAutoConsoleVariableRef CVarWaterShoreEdgeNoiseScale2(
	TEXT("SimCopter.Shading.WaterShoreEdgeNoiseScale2"),
	GSimCopterWaterShoreEdgeNoiseScale2,
	TEXT("Wavelength of the second layer in centimetres. Unlike layer 1 this one may be FINER than ")
	TEXT("a tile: layer 1 has already hidden the 400 cm step, so this is only making the resulting ")
	TEXT("curve less regular."),
	ECVF_Default);

float GSimCopterWaterShoreEdgeNoiseSpeed = SimCopterDayNight::DefaultWaterShoreEdgeNoiseSpeed;
static FAutoConsoleVariableRef CVarWaterShoreEdgeNoiseSpeed(
	TEXT("SimCopter.Shading.WaterShoreEdgeNoiseSpeed"),
	GSimCopterWaterShoreEdgeNoiseSpeed,
	TEXT("How fast layer 1's field changes, in noise FRAMES PER SECOND - it is re-rolled at this ")
	TEXT("rate and smoothly eased between rolls, so the pattern churns in place rather than ")
	TEXT("scrolling across the water. 0 freezes the layer."),
	ECVF_Default);

float GSimCopterWaterShoreEdgeNoiseSpeed2 = SimCopterDayNight::DefaultWaterShoreEdgeNoiseSpeed2;
static FAutoConsoleVariableRef CVarWaterShoreEdgeNoiseSpeed2(
	TEXT("SimCopter.Shading.WaterShoreEdgeNoiseSpeed2"),
	GSimCopterWaterShoreEdgeNoiseSpeed2,
	TEXT("The same for layer 2, in frames per second. This is the fine layer, so it is where ")
	TEXT("shimmer comes from: detail that small is near sub-pixel from altitude, and animating ")
	TEXT("sub-pixel detail sparkles. Bring it down if the water fizzes from high up."),
	ECVF_Default);

float GSimCopterWaterDetailNormalStrength = SimCopterDayNight::DefaultWaterDetailNormalStrength;
static FAutoConsoleVariableRef CVarWaterDetailNormalStrength(
	TEXT("SimCopter.Shading.WaterDetailNormalStrength"),
	GSimCopterWaterDetailNormalStrength,
	TEXT("Strength of the fine normal-only ripple that keeps open water off being one flat mirror. ")
	TEXT("0 turns it off. It never touches the wave geometry or the welded shoreline."),
	ECVF_Default);

float GSimCopterWaterDetailNormalScale = SimCopterDayNight::DefaultWaterDetailNormalScale;
static FAutoConsoleVariableRef CVarWaterDetailNormalScale(
	TEXT("SimCopter.Shading.WaterDetailNormalScale"),
	GSimCopterWaterDetailNormalScale,
	TEXT("Wavelength of that ripple in centimetres. Around a third of the 400 cm tile is what ")
	TEXT("breaks the quad seams up rather than lining up with them."),
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
	PublishLowPower();
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
	PublishLowPower();
	// From Tick and not from Refresh, for the same reason PublishLowPower is: these have to reach
	// the materials in a level with no day sequence too (the editor preview, the main menu), and
	// Refresh gives up early there.
	PublishSurfaceShading();
	ApplyTimeOfDaySettings();
	ApplyPendingSavedTimeOfDay();
	Refresh();
}

ADaySequenceActor* USimCopterDayNightSubsystem::GetDaySequenceActor() const
{
	return CachedDaySequenceActor.Get();
}

bool USimCopterDayNightSubsystem::TryGetLiveTimeOfDayHours(float& OutHours)
{
	if (const ADaySequenceActor* DaySequenceActor = ResolveDaySequenceActor())
	{
		OutHours = DaySequenceActor->GetTimeOfDay();
		return FMath::IsFinite(OutHours);
	}

	OutHours = 0.0f;
	return false;
}

void USimCopterDayNightSubsystem::RestoreSavedTimeOfDay(const float Hours)
{
	PendingSavedTimeOfDayHours = FMath::Clamp(Hours, 0.0f, 24.0f);
	ApplyTimeOfDaySettings();
	ApplyPendingSavedTimeOfDay();
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

void USimCopterDayNightSubsystem::PublishSurfaceShading()
{
	const float Values[SurfaceShadingSlotCount] = {
		FMath::Clamp(GSimCopterTreeMaxBrightness, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterTreeRoughness, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterTreeSpecular, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterTerrainMaxBrightness, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterTerrainRoughness, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterTerrainSpecular, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterCityMaxBrightness, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterCityRoughness, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterCitySpecular, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterCityWindowRoughness, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterCityWindowSpecular, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterWaterMaxBrightness, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterWaterRoughness, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterWaterSpecular, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterWaterShoreRoughness, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterWaterShoreSpecular, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterWaterShoreFadeWidth, 0.0f, 1.0f),
		FMath::Clamp(GSimCopterWaterShoreEdgeNoiseStrength, 0.0f, 1.0f),
		// A wavelength in centimetres, not a 0..1 ratio.
		FMath::Max(GSimCopterWaterShoreEdgeNoiseScale, 1.0f),
		FMath::Clamp(GSimCopterWaterShoreEdgeNoiseStrength2, 0.0f, 1.0f),
		FMath::Max(GSimCopterWaterShoreEdgeNoiseScale2, 1.0f),
		// Frames per second, so no upper bound worth imposing - only "not backwards in time".
		FMath::Max(GSimCopterWaterShoreEdgeNoiseSpeed, 0.0f),
		FMath::Max(GSimCopterWaterShoreEdgeNoiseSpeed2, 0.0f),
		FMath::Max(GSimCopterWaterDetailNormalStrength, 0.0f),
		// A wavelength, not a 0..1 ratio - the shader floors it at 1 cm anyway, but keep it sane.
		FMath::Max(GSimCopterWaterDetailNormalScale, 1.0f),
	};

	static_assert(UE_ARRAY_COUNT(SurfaceShadingParameterNames) == SurfaceShadingSlotCount,
		"Every SimCopter.Shading.* knob needs a collection parameter to publish into.");

	for (int32 Slot = 0; Slot < SurfaceShadingSlotCount; ++Slot)
	{
		if (FMath::IsNearlyEqual(Values[Slot], PublishedSurfaceShading[Slot]))
		{
			continue;
		}
		PublishScalar(SurfaceShadingParameterNames[Slot], Values[Slot]);
		PublishedSurfaceShading[Slot] = Values[Slot];
	}
}

void USimCopterDayNightSubsystem::PublishLowPower()
{
	const float LowPower = SimCopterLowPower::IsEnabled() ? 1.0f : 0.0f;
	if (FMath::IsNearlyEqual(LowPower, PublishedLowPower))
	{
		return;
	}

	PublishScalar(SimCopterDayNight::LowPowerParameterName, LowPower);
	PublishedLowPower = LowPower;
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

void USimCopterDayNightSubsystem::ApplyPendingSavedTimeOfDay()
{
	if (PendingSavedTimeOfDayHours < 0.0f)
	{
		return;
	}

	ADaySequenceActor* DaySequenceActor = ResolveDaySequenceActor();
	const USimCopterSettings* Settings = USimCopterSettings::Get(this);
	if (DaySequenceActor == nullptr || Settings == nullptr)
	{
		return;
	}

	const float RestoredHours = PendingSavedTimeOfDayHours;
	PendingSavedTimeOfDayHours = -1.0f;

	// SetTimeOfDay scrubs with Play and therefore resumes the player. Reassert the saved mode after
	// the seek so a Static save remains pinned while a Dynamic save continues from the saved hour.
	if (Settings->GetTimeOfDayMode() == ESimCopterTimeOfDayMode::Static)
	{
		DaySequenceActor->SetRunDayCycle(false);
		DaySequenceActor->SetTimeOfDay(RestoredHours);
		DaySequenceActor->Pause();
	}
	else
	{
		DaySequenceActor->SetRunDayCycle(true);
		DaySequenceActor->SetTimeOfDay(RestoredHours);
		DaySequenceActor->Play();
	}

	bWasNight = false;
	Refresh();
}
