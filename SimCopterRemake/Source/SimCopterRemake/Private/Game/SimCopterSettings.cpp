// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterSettings.h"

#include "Audio/SimCopterAudioSubsystem.h"
#include "Audio/SimCopterRadio.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameUserSettings.h"

#if WITH_DLSS
#include "DLSSLibrary.h"
#endif

#if WITH_STREAMLINE
#include "StreamlineLibraryDLSSG.h"
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
#endif
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
#endif

	OnHudScaleChanged.Broadcast(HudScale);
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

#undef LOCTEXT_NAMESPACE
