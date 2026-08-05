// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterSettings.h"

#include "Audio/SimCopterAudioSubsystem.h"
#include "Audio/SimCopterRadio.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "GenericPlatform/GenericApplication.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "Widgets/SWindow.h"

#if WITH_DLSS
#include "DLSSLibrary.h"
#endif

#if WITH_STREAMLINE
#include "StreamlineLibraryDLSSG.h"
#include "StreamlineLibraryReflex.h"
#endif

#define LOCTEXT_NAMESPACE "SimCopterSettings"

namespace
{
/** The radio's own volume rides on top of the mixer master, so it is a 0..1 scale. */
float VolumeIndexToScale(const int32 Index)
{
	return FMath::Clamp(static_cast<float>(Index) / static_cast<float>(USimCopterSettings::VolumeMax), 0.0f, 1.0f);
}

/** How far Auto-Quiet pulls the radio down. The original ducks it under dispatch speech. */
constexpr float AutoQuietScale = 0.6f;

#if WITH_DLSS
UDLSSMode ToDlssPluginMode(const ESimCopterDlssQuality Quality)
{
	switch (Quality)
	{
	case ESimCopterDlssQuality::Dlaa:             return UDLSSMode::DLAA;
	case ESimCopterDlssQuality::UltraQuality:     return UDLSSMode::UltraQuality;
	case ESimCopterDlssQuality::Quality:          return UDLSSMode::Quality;
	case ESimCopterDlssQuality::Balanced:         return UDLSSMode::Balanced;
	case ESimCopterDlssQuality::Performance:      return UDLSSMode::Performance;
	case ESimCopterDlssQuality::UltraPerformance: return UDLSSMode::UltraPerformance;
	default:                                      return UDLSSMode::Auto;
	}
}

bool FromDlssPluginMode(const UDLSSMode Mode, ESimCopterDlssQuality& OutQuality)
{
	switch (Mode)
	{
	case UDLSSMode::Auto:             OutQuality = ESimCopterDlssQuality::Auto; return true;
	case UDLSSMode::DLAA:             OutQuality = ESimCopterDlssQuality::Dlaa; return true;
	case UDLSSMode::UltraQuality:     OutQuality = ESimCopterDlssQuality::UltraQuality; return true;
	case UDLSSMode::Quality:          OutQuality = ESimCopterDlssQuality::Quality; return true;
	case UDLSSMode::Balanced:         OutQuality = ESimCopterDlssQuality::Balanced; return true;
	case UDLSSMode::Performance:      OutQuality = ESimCopterDlssQuality::Performance; return true;
	case UDLSSMode::UltraPerformance: OutQuality = ESimCopterDlssQuality::UltraPerformance; return true;
	default:                          return false;   // Off is the enable flag's job, not a quality
	}
}
#endif

#if WITH_STREAMLINE
/** Streamline folds "is it on" and "how many frames" into one enum; the page keeps them apart. */
EStreamlineDLSSGMode ToFrameGenPluginMode(const ESimCopterFrameGenMode Mode, const int32 Multiple)
{
	if (Mode == ESimCopterFrameGenMode::Auto)
	{
		return EStreamlineDLSSGMode::Auto;
	}
	if (Mode == ESimCopterFrameGenMode::Off)
	{
		return EStreamlineDLSSGMode::Off;
	}

	switch (Multiple)
	{
	case 3:  return EStreamlineDLSSGMode::On3X;
	case 4:  return EStreamlineDLSSGMode::On4X;
	case 5:  return EStreamlineDLSSGMode::On5X;
	case 6:  return EStreamlineDLSSGMode::On6X;
	default: return EStreamlineDLSSGMode::On2X;
	}
}

/** INDEX_NONE for the modes that carry no fixed multiple (Off, Auto, Dynamic). */
int32 FrameGenPluginModeToMultiple(const EStreamlineDLSSGMode Mode)
{
	switch (Mode)
	{
	case EStreamlineDLSSGMode::On2X: return 2;
	case EStreamlineDLSSGMode::On3X: return 3;
	case EStreamlineDLSSGMode::On4X: return 4;
	case EStreamlineDLSSGMode::On5X: return 5;
	case EStreamlineDLSSGMode::On6X: return 6;
	default:                         return INDEX_NONE;
	}
}

/** Streamline's Reflex enum is sparse - Boost is 3 - so this cannot be a cast. */
EStreamlineReflexMode ToReflexPluginMode(const ESimCopterReflexMode Mode)
{
	switch (Mode)
	{
	case ESimCopterReflexMode::On:      return EStreamlineReflexMode::Enabled;
	case ESimCopterReflexMode::OnBoost: return EStreamlineReflexMode::Boost;
	default:                            return EStreamlineReflexMode::Off;
	}
}
#endif

/**
 * Sets a console variable by name if it exists, so a renamed CVar cannot crash the Settings page.
 *
 * **`ECVF_SetByGameOverride`, not `ECVF_SetByGameSetting`.** The SetBy flags are a priority ladder
 * and a `Set` at or below the current owner's priority is silently dropped. Every variable this
 * function drives - `r.DynamicGlobalIlluminationMethod`, `r.ReflectionMethod`,
 * `r.Lumen.HardwareRayTracing`, `r.VolumetricFog` - is written by `[/Script/Engine.RendererSettings]`
 * in DefaultEngine.ini, which lands at `ECVF_SetByProjectSetting` (0x04) - ABOVE
 * `ECVF_SetByGameSetting` (0x03), despite the name. Game settings are meant to be defaults a project
 * may enforce over. `ECVF_SetByGameOverride` (0x09) is the one the engine documents for exactly this
 * ("GameUserSettings fields that need to override device specific settings"), and still loses to
 * consolevariables.ini, the command line and the interactive console.
 */
void SetRenderCVar(const TCHAR* Name, const int32 Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		Variable->Set(Value, ECVF_SetByGameOverride);
	}
}
}

FIntPoint SimCopterDisplay::FindNativeResolutionForWindow(
	const TArray<FMonitor>& Monitors,
	const FIntRect& WindowRect)
{
	const FMonitor* Best = nullptr;
	int64 BestOverlap = 0;

	for (const FMonitor& Monitor : Monitors)
	{
		if (Monitor.NativeResolution.X <= 0 || Monitor.NativeResolution.Y <= 0)
		{
			continue;
		}

		const int64 OverlapWidth = FMath::Max(
			0, FMath::Min(WindowRect.Max.X, Monitor.DisplayRect.Max.X) - FMath::Max(WindowRect.Min.X, Monitor.DisplayRect.Min.X));
		const int64 OverlapHeight = FMath::Max(
			0, FMath::Min(WindowRect.Max.Y, Monitor.DisplayRect.Max.Y) - FMath::Max(WindowRect.Min.Y, Monitor.DisplayRect.Min.Y));
		const int64 Overlap = OverlapWidth * OverlapHeight;

		if (Overlap > BestOverlap)
		{
			BestOverlap = Overlap;
			Best = &Monitor;
		}
	}

	if (Best != nullptr)
	{
		return Best->NativeResolution;
	}

	// No overlap at all - an off-screen or zero-size window, which is what an unshown one reports.
	for (const FMonitor& Monitor : Monitors)
	{
		if (Monitor.bIsPrimary && Monitor.NativeResolution.X > 0 && Monitor.NativeResolution.Y > 0)
		{
			return Monitor.NativeResolution;
		}
	}

	for (const FMonitor& Monitor : Monitors)
	{
		if (Monitor.NativeResolution.X > 0 && Monitor.NativeResolution.Y > 0)
		{
			return Monitor.NativeResolution;
		}
	}

	return FIntPoint::ZeroValue;
}

USimCopterSettings* USimCopterSettings::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<USimCopterSettings>() : nullptr;
}

void USimCopterSettings::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LoadConfig();

	GameVolume = FMath::Clamp(GameVolume, VolumeMin, VolumeMax);
	RadioVolume = FMath::Clamp(RadioVolume, VolumeMin, VolumeMax);
	RadioStation = FMath::Max(RadioStation, 0);
	HudScale = FMath::Clamp(HudScale, HudScaleMin, HudScaleMax);
	FrameGenMultiple = FMath::Clamp(FrameGenMultiple, FrameGenMultipleMin, FrameGenMultipleMax);
	StaticTimeOfDayHours = FMath::Clamp(StaticTimeOfDayHours, StaticTimeOfDayMinHours, StaticTimeOfDayMaxHours);
	DayRealMinutes = FMath::Clamp(DayRealMinutes, CycleLengthMinMinutes, CycleLengthMaxMinutes);
	NightRealMinutes = FMath::Clamp(NightRealMinutes, CycleLengthMinMinutes, CycleLengthMaxMinutes);
	EmissiveBrightness = FMath::Clamp(EmissiveBrightness, EmissiveBrightnessMin, EmissiveBrightnessMax);

	// A stored mode the current GPU cannot do would otherwise leave the page showing something
	// the renderer quietly ignored - the ini travels between machines.
	if (bDlssEnabled && !IsDlssAvailable())
	{
		bDlssEnabled = false;
	}
	if (FrameGenMode != ESimCopterFrameGenMode::Off && !IsFrameGenAvailable())
	{
		FrameGenMode = ESimCopterFrameGenMode::Off;
	}

	TArray<int32> Multiples;
	GetAvailableFrameGenMultiples(Multiples);
	if (Multiples.Num() > 0 && !Multiples.Contains(FrameGenMultiple))
	{
		FrameGenMultiple = Multiples.Last();
	}

	if (ReflexMode != ESimCopterReflexMode::Off && !IsReflexModeAvailable(ReflexMode))
	{
		ReflexMode = IsReflexModeAvailable(ESimCopterReflexMode::On)
			? ESimCopterReflexMode::On
			: ESimCopterReflexMode::Off;
	}

	// Hardware Lumen on a machine with no ray tracing is not an error - the renderer just falls back
	// to software traces - but the row would then show "Hardware" while software is what is running.
	if (LumenMode == ESimCopterLumenMode::HardwareRayTracing && !IsHardwareRayTracingAvailable())
	{
		LumenMode = ESimCopterLumenMode::Software;
	}

	SeedResolutionFromDisplay();
}

void USimCopterSettings::SeedResolutionFromDisplay()
{
	if (bResolutionSeededFromDisplay)
	{
		return;
	}

	UGameUserSettings* UserSettings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (UserSettings == nullptr || !FApp::CanEverRender() || !FSlateApplication::IsInitialized())
	{
		// Headless (the automation tests, a cook): leave the flag clear so a real run still seeds.
		return;
	}

	FDisplayMetrics DisplayMetrics;
	FDisplayMetrics::RebuildDisplayMetrics(DisplayMetrics);

	TArray<SimCopterDisplay::FMonitor> Monitors;
	Monitors.Reserve(DisplayMetrics.MonitorInfo.Num());
	for (const FMonitorInfo& Info : DisplayMetrics.MonitorInfo)
	{
		SimCopterDisplay::FMonitor Monitor;
		Monitor.NativeResolution = FIntPoint(Info.NativeWidth, Info.NativeHeight);
		Monitor.DisplayRect = FIntRect(
			Info.DisplayRect.Left, Info.DisplayRect.Top, Info.DisplayRect.Right, Info.DisplayRect.Bottom);
		Monitor.bIsPrimary = Info.bIsPrimary;
		Monitors.Add(Monitor);
	}

	// Where the game window actually is, so the right panel is measured on a multi-monitor desk.
	FIntRect WindowRect(0, 0, 0, 0);
	if (GEngine->GameViewport != nullptr)
	{
		if (const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow())
		{
			const FVector2D Position = Window->GetPositionInScreen();
			const FVector2D Size = Window->GetSizeInScreen();
			WindowRect = FIntRect(
				FMath::RoundToInt(Position.X),
				FMath::RoundToInt(Position.Y),
				FMath::RoundToInt(Position.X + Size.X),
				FMath::RoundToInt(Position.Y + Size.Y));
		}
	}

	const FIntPoint Native = SimCopterDisplay::FindNativeResolutionForWindow(Monitors, WindowRect);
	if (Native.X <= 0 || Native.Y <= 0)
	{
		return;
	}

	bResolutionSeededFromDisplay = true;

	// `-ResX=` on the command line is a deliberate override and outranks the display. The flag is
	// still set either way, so this only ever gets one shot.
	int32 CommandLineResX = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("ResX="), CommandLineResX) && CommandLineResX > 0)
	{
		SaveConfig();
		return;
	}

	UserSettings->SetScreenResolution(Native);
	UserSettings->ApplyResolutionSettings(/*bCheckForCommandLineOverrides=*/false);
	UserSettings->ConfirmVideoMode();
	UserSettings->SaveSettings();
	SaveConfig();
}

void USimCopterSettings::ApplyAll(const UObject* WorldContextObject)
{
	ApplySound(WorldContextObject);
	ApplyGraphics(WorldContextObject);
}

void USimCopterSettings::Save()
{
	SaveConfig();

	if (UGameUserSettings* UserSettings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr)
	{
		UserSettings->ApplySettings(/*bCheckForCommandLineOverrides=*/false);
	}
}

// ---------------------------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------------------------

void USimCopterSettings::ApplySound(const UObject* WorldContextObject)
{
	if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(WorldContextObject))
	{
		Audio->SetMasterVolume(GameVolume);
	}

	if (USimCopterRadioSubsystem* Radio = USimCopterRadioSubsystem::Get(WorldContextObject))
	{
		Radio->SetVolume(VolumeIndexToScale(RadioVolume) * (bAutoQuiet ? AutoQuietScale : 1.0f));
		Radio->SetSlotEnabled(ESimCopterRadioSlot::Dj, bDjEnabled);
		Radio->SetSlotEnabled(ESimCopterRadioSlot::Commercial, bCommercialsEnabled);
		if (Radio->GetStationCount() > 0)
		{
			Radio->SetStationIndex(FMath::Clamp(RadioStation, 0, Radio->GetStationCount() - 1));
		}
	}
}

void USimCopterSettings::SetGameVolume(const int32 Value)
{
	GameVolume = FMath::Clamp(Value, VolumeMin, VolumeMax);
}

void USimCopterSettings::SetRadioVolume(const int32 Value)
{
	RadioVolume = FMath::Clamp(Value, VolumeMin, VolumeMax);
}

void USimCopterSettings::SetRadioStation(const int32 Index)
{
	RadioStation = FMath::Max(Index, 0);
}

void USimCopterSettings::SetDjEnabled(const bool bEnabled)
{
	bDjEnabled = bEnabled;
}

void USimCopterSettings::SetCommercialsEnabled(const bool bEnabled)
{
	bCommercialsEnabled = bEnabled;
}

void USimCopterSettings::SetAutoQuietEnabled(const bool bEnabled)
{
	bAutoQuiet = bEnabled;
}

// ---------------------------------------------------------------------------------------------
// Graphics
// ---------------------------------------------------------------------------------------------

void USimCopterSettings::ApplyGraphics(const UObject* WorldContextObject)
{
#if WITH_DLSS
	if (UDLSSLibrary::IsDLSSSupported())
	{
		UDLSSLibrary::EnableDLSS(bDlssEnabled);
		if (bDlssEnabled)
		{
			UDLSSLibrary::SetDLSSMode(const_cast<UObject*>(WorldContextObject), ToDlssPluginMode(DlssQuality));
		}
	}
#endif

#if WITH_STREAMLINE
	if (UStreamlineLibraryDLSSG::IsDLSSGSupported())
	{
		UStreamlineLibraryDLSSG::SetDLSSGMode(ToFrameGenPluginMode(FrameGenMode, FrameGenMultiple));
	}

	if (UStreamlineLibraryReflex::IsReflexSupported())
	{
		UStreamlineLibraryReflex::SetReflexMode(ToReflexPluginMode(ReflexMode));
	}
#endif

	// Lumen and volumetric fog are console variables rather than UGameUserSettings properties, so
	// they are driven directly. Hardware and Software are the same GI method; only the tracing
	// changes, which is why Off is the only case that also moves the reflection method.
	switch (LumenMode)
	{
	case ESimCopterLumenMode::HardwareRayTracing:
		SetRenderCVar(TEXT("r.DynamicGlobalIlluminationMethod"), 1);
		SetRenderCVar(TEXT("r.ReflectionMethod"), 1);
		SetRenderCVar(TEXT("r.Lumen.HardwareRayTracing"), 1);
		break;
	case ESimCopterLumenMode::Software:
		SetRenderCVar(TEXT("r.DynamicGlobalIlluminationMethod"), 1);
		SetRenderCVar(TEXT("r.ReflectionMethod"), 1);
		SetRenderCVar(TEXT("r.Lumen.HardwareRayTracing"), 0);
		break;
	default:
		// Screen-space reflections rather than none: with Lumen off the city would otherwise lose
		// every reflection it has, and SSR costs almost nothing on geometry this simple.
		SetRenderCVar(TEXT("r.DynamicGlobalIlluminationMethod"), 0);
		SetRenderCVar(TEXT("r.ReflectionMethod"), 2);
		break;
	}

	SetRenderCVar(TEXT("r.VolumetricFog"), bVolumetricFog ? 1 : 0);

	OnHudScaleChanged.Broadcast(HudScale);
}

void USimCopterSettings::SetStaticTimeOfDayHours(const float Hours)
{
	StaticTimeOfDayHours = FMath::Clamp(Hours, StaticTimeOfDayMinHours, StaticTimeOfDayMaxHours);
}

void USimCopterSettings::SetEmissiveBrightness(const float Scale)
{
	EmissiveBrightness = FMath::Clamp(Scale, EmissiveBrightnessMin, EmissiveBrightnessMax);
}

void USimCopterSettings::SetDayRealMinutes(const float Minutes)
{
	DayRealMinutes = FMath::Clamp(Minutes, CycleLengthMinMinutes, CycleLengthMaxMinutes);
}

void USimCopterSettings::SetNightRealMinutes(const float Minutes)
{
	NightRealMinutes = FMath::Clamp(Minutes, CycleLengthMinMinutes, CycleLengthMaxMinutes);
}

FText USimCopterSettings::FormatMinutes(const float Minutes)
{
	// One decimal: the slider's own step is finer than a whole minute, and rounding the readout to
	// integers would make two visibly different positions both read "7 min".
	return FText::FromString(FString::Printf(TEXT("%.1f min"), Minutes));
}

FText USimCopterSettings::FormatTimeOfDay(const float Hours)
{
	// Wrapped, because 24:00 is 00:00 and the slider's top end lands exactly there.
	const float Wrapped = FMath::Fmod(FMath::Max(Hours, 0.0f), 24.0f);
	const int32 Hour = FMath::Clamp(FMath::FloorToInt(Wrapped), 0, 23);
	const int32 Minute = FMath::Clamp(FMath::FloorToInt((Wrapped - Hour) * 60.0f), 0, 59);
	return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Hour, Minute));
}

void USimCopterSettings::SetFrameGenMultiple(const int32 Multiple)
{
	FrameGenMultiple = FMath::Clamp(Multiple, FrameGenMultipleMin, FrameGenMultipleMax);
}

void USimCopterSettings::SetHudScale(const float Scale)
{
	const float Clamped = FMath::Clamp(Scale, HudScaleMin, HudScaleMax);
	if (FMath::IsNearlyEqual(Clamped, HudScale))
	{
		return;
	}
	HudScale = Clamped;
	OnHudScaleChanged.Broadcast(HudScale);
}

// ---------------------------------------------------------------------------------------------
// Availability
// ---------------------------------------------------------------------------------------------

bool USimCopterSettings::IsDlssAvailable()
{
#if WITH_DLSS
	return UDLSSLibrary::IsDLSSSupported();
#else
	return false;
#endif
}

bool USimCopterSettings::IsFrameGenAvailable()
{
#if WITH_STREAMLINE
	return UStreamlineLibraryDLSSG::IsDLSSGSupported();
#else
	return false;
#endif
}

bool USimCopterSettings::IsReflexAvailable()
{
#if WITH_STREAMLINE
	return UStreamlineLibraryReflex::IsReflexSupported();
#else
	return false;
#endif
}

bool USimCopterSettings::IsReflexModeAvailable(const ESimCopterReflexMode Mode)
{
#if WITH_STREAMLINE
	return UStreamlineLibraryReflex::IsReflexSupported()
		&& UStreamlineLibraryReflex::IsReflexModeSupported(ToReflexPluginMode(Mode));
#else
	return Mode == ESimCopterReflexMode::Off;
#endif
}

bool USimCopterSettings::IsHardwareRayTracingAvailable()
{
	// GRHISupportsRayTracing alone is not enough: the project also has to have been cooked with ray
	// tracing shaders, which is what IsRayTracingEnabled folds in.
	return IsRayTracingEnabled();
}

void USimCopterSettings::GetAvailableDlssQualities(TArray<ESimCopterDlssQuality>& OutQualities)
{
	OutQualities.Reset();

#if WITH_DLSS
	if (!UDLSSLibrary::IsDLSSSupported())
	{
		return;
	}

	// The plugin reports what this GPU and driver actually offer, so the dropdown never lists a
	// mode that would silently do nothing.
	for (const UDLSSMode Mode : UDLSSLibrary::GetSupportedDLSSModes())
	{
		ESimCopterDlssQuality Ours = ESimCopterDlssQuality::Auto;
		if (FromDlssPluginMode(Mode, Ours))
		{
			OutQualities.AddUnique(Ours);
		}
	}
#endif
}

void USimCopterSettings::GetAvailableFrameGenMultiples(TArray<int32>& OutMultiples)
{
	OutMultiples.Reset();

#if WITH_STREAMLINE
	if (!UStreamlineLibraryDLSSG::IsDLSSGSupported())
	{
		return;
	}

	for (const EStreamlineDLSSGMode Mode : UStreamlineLibraryDLSSG::GetSupportedDLSSGModes())
	{
		const int32 Multiple = FrameGenPluginModeToMultiple(Mode);
		if (Multiple != INDEX_NONE)
		{
			OutMultiples.AddUnique(Multiple);
		}
	}
	OutMultiples.Sort();
#endif
}

FText USimCopterSettings::GetDlssQualityLabel(const ESimCopterDlssQuality Quality)
{
	switch (Quality)
	{
	case ESimCopterDlssQuality::Dlaa:             return LOCTEXT("DlssDlaa", "DLAA");
	case ESimCopterDlssQuality::UltraQuality:     return LOCTEXT("DlssUltraQuality", "Ultra Quality");
	case ESimCopterDlssQuality::Quality:          return LOCTEXT("DlssQuality", "Quality");
	case ESimCopterDlssQuality::Balanced:         return LOCTEXT("DlssBalanced", "Balanced");
	case ESimCopterDlssQuality::Performance:      return LOCTEXT("DlssPerformance", "Performance");
	case ESimCopterDlssQuality::UltraPerformance: return LOCTEXT("DlssUltraPerformance", "Ultra Performance");
	default:                                      return LOCTEXT("DlssAuto", "Auto");
	}
}

FText USimCopterSettings::GetFrameGenModeLabel(const ESimCopterFrameGenMode Mode)
{
	switch (Mode)
	{
	case ESimCopterFrameGenMode::On:   return LOCTEXT("FrameGenOn", "On");
	case ESimCopterFrameGenMode::Auto: return LOCTEXT("FrameGenAuto", "Auto");
	default:                           return LOCTEXT("FrameGenOff", "Off");
	}
}

FText USimCopterSettings::GetFrameGenMultipleLabel(const int32 Multiple)
{
	// Streamline's own wording: NX presents N frames per rendered frame, so N-1 are generated.
	return FText::Format(
		LOCTEXT("FrameGenMultipleFormat", "{0}x  ({1} generated)"),
		FText::AsNumber(Multiple),
		FText::AsNumber(FMath::Max(Multiple - 1, 0)));
}

FText USimCopterSettings::GetReflexModeLabel(const ESimCopterReflexMode Mode)
{
	switch (Mode)
	{
	case ESimCopterReflexMode::On:      return LOCTEXT("ReflexOn", "On");
	case ESimCopterReflexMode::OnBoost: return LOCTEXT("ReflexBoost", "On (Boost)");
	default:                            return LOCTEXT("ReflexOff", "Off");
	}
}

FText USimCopterSettings::GetLumenModeLabel(const ESimCopterLumenMode Mode)
{
	switch (Mode)
	{
	case ESimCopterLumenMode::HardwareRayTracing: return LOCTEXT("LumenHardware", "On (Hardware Lumen)");
	case ESimCopterLumenMode::Software:           return LOCTEXT("LumenSoftware", "On (Software Lumen)");
	default:                                      return LOCTEXT("LumenOff", "Off");
	}
}

FText USimCopterSettings::GetTimeOfDayModeLabel(const ESimCopterTimeOfDayMode Mode)
{
	return Mode == ESimCopterTimeOfDayMode::Static
		? LOCTEXT("TimeOfDayStatic", "Static")
		: LOCTEXT("TimeOfDayDynamic", "Dynamic");
}

#undef LOCTEXT_NAMESPACE
