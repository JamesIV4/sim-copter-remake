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

// SCHOOK: CitySettingsDialog 0x00440370
//
// The Settings screen's "City Settings" sub-dialog - control id 0x7d8, drawn on cityset.bmp, with
// slidcity.bmp as the track under each of its eight vertical sliders. Reached from Settings item 0,
// which only exists in a user game (FUN_00407bb0 hands a career the city's fixed record instead of
// the editable block).
//
// Decode with citations: Docs/scratchpad/settings-DECODED.md. The rects come from
// Docs/scratchpad/asm-00440370-cityset.txt via parse_dialog_rects.py, never from the decompile,
// and were drawn back over the page art (Docs/scratchpad/overlay_settings_rects.py).
namespace SimCopterCitySettingsLayout
{
using SimCopterFrontEnd::FRect;

// FUN_004383c0 gives the dialog a degenerate (0,0,1,1) rect, so cityset.bmp lands at its own size.
constexpr float PageWidth = 594.0f;
constexpr float PageHeight = 435.0f;

constexpr int32 SliderCount = 8;

// All eight are 26x202 - exactly slidcity.bmp's size - at y 96..298, evenly spaced.
constexpr float SliderTop = 96.0f;
constexpr float SliderBottom = 298.0f;
constexpr float SliderWidth = 26.0f;
constexpr float SliderX[SliderCount] = { 42.0f, 111.0f, 179.0f, 248.0f, 316.0f, 385.0f, 454.0f, 522.0f };

// The command ids the sliders are built with, 3..10; also their order in the eight consecutive
// dwords FUN_00440e40 / FUN_00440ec0 copy to and from.
constexpr int32 FirstSliderCommandId = 3;

// FUN_0040bb20 / FUN_0040bb50 right after each construction. Difficulty is the odd one out.
constexpr int32 DifficultyMax = 3;
constexpr int32 WeightMax = 100;

constexpr int32 GetSliderMax(const int32 Index) { return Index == 0 ? DifficultyMax : WeightMax; }

// Labels, font height 18, justify 1 (centre). Four sit in the wells above the troughs and four in
// the wells below; the label-to-slider pairing is construction order, which the geometry confirms
// (an even slider's label shares its LEFT edge, an odd one's its RIGHT edge).
struct FLabel
{
	int32 StringId;
	FRect Rect;
};

constexpr FLabel Labels[SliderCount] = {
	{ 333, { 42.0f, 47.0f, 128.0f, 70.0f } },   // Difficulty
	{ 334, { 52.0f, 327.0f, 136.0f, 350.0f } }, // Fire
	{ 335, { 179.0f, 327.0f, 265.0f, 350.0f } },// Crime
	{ 336, { 187.0f, 47.0f, 273.0f, 69.0f } },  // Rescue
	{ 337, { 314.0f, 47.0f, 400.0f, 69.0f } },  // Riot
	{ 338, { 323.0f, 327.0f, 408.0f, 350.0f } },// Traffic
	{ 339, { 452.0f, 327.0f, 536.0f, 350.0f } },// Medical
	{ 340, { 461.0f, 47.0f, 544.0f, 70.0f } },  // Transport
};

constexpr int32 LabelFontHeight = 18;
constexpr int32 ButtonFontHeight = 16;

// Both degenerate 1x1, so button.bmp's own 100x28 applies.
constexpr float ButtonY = 376.0f;
constexpr float OkButtonX = 130.0f;
constexpr float CancelButtonX = 364.0f;

// The remake shows each slider's number, which the original does not: its tracks are printed with
// detent dots and nothing else, and a 0..100 weight is unreadable from a thumb position alone.
constexpr int32 ValueFontHeight = 15;
}

// The eight values, in the order FUN_00440ec0 writes them - which is FSimCopterCareerCity's own
// order: Difficulty then Weights[7] = Fire, Crime, Rescue, Riot, Traffic, MedEvac, Transport.
struct FSimCopterCitySettingsValues
{
	int32 Difficulty = 0;
	float Weights[7] = { 0, 0, 0, 0, 0, 0, 0 };
};

DECLARE_DELEGATE_OneParam(FOnSimCopterCitySettingsAccepted, const FSimCopterCitySettingsValues&);

class SSimCopterCitySettings : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterCitySettings) {}
		SLATE_ARGUMENT(TObjectPtr<USimCopterHangarArt>, Art)
		SLATE_ARGUMENT(FSimCopterCitySettingsValues, Values)
		SLATE_EVENT(FOnSimCopterCitySettingsAccepted, OnAccepted)
		SLATE_EVENT(FSimpleDelegate, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Slider index -> its stored value, and back. Split out so the mapping can be tested. */
	static int32 GetValueForSlider(const FSimCopterCitySettingsValues& Values, int32 Index);
	static void SetValueForSlider(FSimCopterCitySettingsValues& Values, int32 Index, int32 Value);

	static const FText& GetLabel(int32 Index);

private:
	TObjectPtr<USimCopterHangarArt> Art;
	FSimCopterCitySettingsValues Values;
	FOnSimCopterCitySettingsAccepted OnAccepted;
	FSimpleDelegate OnCancelled;

	TSharedPtr<SSimCopterCheckupSlider> Sliders[SimCopterCitySettingsLayout::SliderCount];
	TSharedPtr<STextBlock> Readouts[SimCopterCitySettingsLayout::SliderCount];
	TArray<TSharedRef<FButtonStyle>> ButtonStyles;

	void RefreshReadouts();
	/** FUN_00438440's `param_3 != 0` branch: only OK writes the sliders back. */
	void Accept();
};
