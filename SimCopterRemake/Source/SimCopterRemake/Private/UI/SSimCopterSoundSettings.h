// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimCopterFrontEndPage.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SSimCopterCheckupSlider;
class STextBlock;
class USimCopterHangarArt;
struct FButtonStyle;

// SCHOOK: SoundDialog 0x0043f7c0
//
// The Settings screen's "Sound" sub-dialog - control id 0x7d6, drawn on sound.bmp. Despite the
// name the page is the cockpit radio's head unit: a speaker grille, the game volume fader along
// the bottom, and the tuner dial with its own volume slider down the right.
//
// The five strings the page does NOT use (131 Dispatch, 132 Sound Effects, 133 Vehicle Sounds,
// 134 Classical, 135 Rock, 136 Techno) are leftovers from an earlier design - FUN_0043f7c0 builds
// five text controls and they are 130, 138, 137, 139 and 140.
//
// Decode with citations: Docs/scratchpad/settings-DECODED.md. Rects from
// Docs/scratchpad/asm-0043f7c0-sound.txt via parse_dialog_rects.py, overlaid on the page art by
// Docs/scratchpad/overlay_settings_rects.py.
namespace SimCopterSoundSettingsLayout
{
using SimCopterFrontEnd::FRect;

// FUN_00438200 gives the dialog a degenerate (0,0,1,1) rect, so sound.bmp lands at its own size.
constexpr float PageWidth = 550.0f;
constexpr float PageHeight = 434.0f;

// FUN_0040bb20 / FUN_0040bb50 after each slider: both volumes run 320..10000 and the tuner 0..2.
constexpr int32 VolumeMin = 320;
constexpr int32 VolumeMax = 10000;
constexpr int32 TunerMax = 2;

// Command 6. The one horizontal slider in the game.
constexpr FRect GameVolumeRect{ 120.0f, 334.0f, 312.0f, 366.0f };
// Command 11.
constexpr FRect RadioVolumeRect{ 350.0f, 78.0f, 382.0f, 270.0f };
// Command 10 - the printed frequency scale, which the needle travels down.
constexpr FRect TunerRect{ 393.0f, 91.0f, 439.0f, 279.0f };

// Commands 4, 3 and 5, each a degenerate 1x1 that sizes itself from its own sprite. The remake
// draws them as lamps on the printed sub-panels, so it needs a real size: the plates below them
// are ~76 px wide and the lamp wells on the page are square.
constexpr float ToggleSize = 22.0f;
constexpr FRect CommercialsToggleRect{ 108.0f, 253.0f, 108.0f + ToggleSize, 253.0f + ToggleSize };
constexpr FRect DjToggleRect{ 196.0f, 253.0f, 196.0f + ToggleSize, 253.0f + ToggleSize };
constexpr FRect AutoQuietToggleRect{ 286.0f, 253.0f, 286.0f + ToggleSize, 253.0f + ToggleSize };

// The five text controls, font height 14.
constexpr FRect GameVolumeLabelRect{ 150.0f, 368.0f, 280.0f, 382.0f };  // 130, centred
constexpr FRect CommercialsLabelRect{ 110.0f, 287.0f, 186.0f, 301.0f }; // 138, centred
constexpr FRect DjLabelRect{ 192.0f, 287.0f, 230.0f, 301.0f };          // 137, centred
constexpr FRect AutoQuietLabelRect{ 238.0f, 287.0f, 316.0f, 301.0f };   // 139, right-justified
constexpr FRect VolLabelRect{ 348.0f, 287.0f, 382.0f, 300.0f };         // 140, centred
constexpr int32 LabelFontHeight = 14;

// Commands 1 and 2, strings 141 and 142, font height 16. Both degenerate, so button.bmp's 100x28.
constexpr float ButtonX = 334.0f;
constexpr float OkButtonY = 331.0f;
constexpr float CancelButtonY = 359.0f;
constexpr int32 ButtonFontHeight = 16;

// The remake's own readouts: the call sign under the dial and the volume as a percentage. The
// original prints neither, but its dial is a photographic frequency scale that means nothing when
// the stations are globbed off disk rather than fixed.
constexpr FRect StationLabelRect{ 389.0f, 282.0f, 443.0f, 300.0f };
constexpr int32 ReadoutFontHeight = 14;
}

/** What the dialog edits, mirroring the six controls FUN_00440070 pushes into the page. */
struct FSimCopterSoundSettingsValues
{
	int32 GameVolume = SimCopterSoundSettingsLayout::VolumeMax;
	int32 RadioVolume = SimCopterSoundSettingsLayout::VolumeMax;
	int32 RadioStation = 0;
	bool bCommercials = true;
	bool bDj = true;
	bool bAutoQuiet = true;
};

DECLARE_DELEGATE_OneParam(FOnSimCopterSoundSettingsAccepted, const FSimCopterSoundSettingsValues&);

class SSimCopterSoundSettings : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterSoundSettings)
		: _StationCount(0)
	{}
		SLATE_ARGUMENT(TObjectPtr<USimCopterHangarArt>, Art)
		SLATE_ARGUMENT(FSimCopterSoundSettingsValues, Values)
		/** Stations actually discovered on disk; the tuner is clamped to them, not to the fixed 0..2. */
		SLATE_ARGUMENT(int32, StationCount)
		SLATE_ARGUMENT(TArray<FString>, StationCallSigns)
		/** Fires on every change so the mixer can be heard moving, as the original's does. */
		SLATE_EVENT(FOnSimCopterSoundSettingsAccepted, OnPreviewChanged)
		SLATE_EVENT(FOnSimCopterSoundSettingsAccepted, OnAccepted)
		SLATE_EVENT(FSimpleDelegate, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Slider alpha <-> the original's 320..10000 volume index. */
	static int32 AlphaToVolume(float Alpha);
	static float VolumeToAlpha(int32 Volume);

private:
	TObjectPtr<USimCopterHangarArt> Art;
	FSimCopterSoundSettingsValues Values;
	FSimCopterSoundSettingsValues Entered;
	int32 StationCount = 0;
	TArray<FString> StationCallSigns;

	FOnSimCopterSoundSettingsAccepted OnPreviewChanged;
	FOnSimCopterSoundSettingsAccepted OnAccepted;
	FSimpleDelegate OnCancelled;

	TSharedPtr<SSimCopterCheckupSlider> GameVolumeSlider;
	TSharedPtr<SSimCopterCheckupSlider> RadioVolumeSlider;
	TSharedPtr<SSimCopterCheckupSlider> TunerSlider;
	TSharedPtr<STextBlock> StationLabel;
	TSharedPtr<STextBlock> GameVolumeLabel;
	TArray<TSharedRef<FButtonStyle>> ButtonStyles;

	int32 GetTunerMax() const;
	void RefreshReadouts();
	void Preview();
	void Accept();
	/** FUN_00438320's Cancel path: the page closes without FUN_00440130 ever running. */
	void Cancel();

	TSharedRef<SWidget> BuildToggle(const FText& Label, bool* Flag);
};
