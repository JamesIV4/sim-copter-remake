// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimCopterFrontEndPage.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SSimCopterCheckupSlider;
class STextBlock;
class SVerticalBox;
class USimCopterHangarArt;
struct FButtonStyle;

// The Settings screen's "Graphics" sub-dialog - control id 0x7d5, drawn on render.bmp.
//
// REMAKE DIVERGENCE, and the only page in the port whose *contents* are deliberately not the
// original's. FUN_0043df80 builds three checkboxes (building textures, ground textures, sky and
// clouds), one fog-closeness slider and a three-way display-resolution radio group - every one of
// them a 1996 concession to hardware that could not keep up. Nothing in this project needs them:
// the whole city is a handful of runtime static meshes built from GEO models a few hundred
// triangles each, and the textures are 8-bit palette rasters measured in kilobytes. Porting the
// switches would give the player five controls that all do nothing.
//
// So the page keeps the original's furniture, its OK/Cancel plates at the decoded positions, and
// its place in the menu, and carries Unreal's own graphics settings instead. The original's decoded
// layout is recorded anyway, in Docs/scratchpad/settings-DECODED.md.
namespace SimCopterGraphicsSettingsLayout
{
using SimCopterFrontEnd::FRect;

// FUN_004380a0 gives the dialog a degenerate (0,0,1,1) rect, so render.bmp lands at its own size.
constexpr float PageWidth = 594.0f;
constexpr float PageHeight = 435.0f;

// The page interior, inside the printed metal frame. The original divides this into five wells;
// the remake lays one list over it on its own backing panel, because there are eighteen rows to
// place and the printed wells hold three.
constexpr FRect ListRect{ 56.0f, 44.0f, 538.0f, 302.0f };
constexpr float RowHeight = 26.0f;
constexpr float RowLabelWidth = 200.0f;

// FUN_0043df80's two buttons, commands 1 and 2, strings 81 and 82, font height 16. Both rects are
// degenerate 3x3, so button.bmp's own 100x28 applies. These are the original's positions.
constexpr float ButtonY = 318.0f;
constexpr float OkButtonX = 328.0f;
constexpr float CancelButtonX = 432.0f;
constexpr int32 ButtonFontHeight = 16;

constexpr int32 RowFontHeight = 17;
constexpr int32 HeadingFontHeight = 18;
}

DECLARE_DELEGATE(FOnSimCopterGraphicsSettingsClosed);

class SSimCopterGraphicsSettings : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterGraphicsSettings) {}
		SLATE_ARGUMENT(TObjectPtr<USimCopterHangarArt>, Art)
		/** OK: keep and save. */
		SLATE_EVENT(FOnSimCopterGraphicsSettingsClosed, OnAccepted)
		/** Cancel: put back what was there on entry, as FUN_00438150's non-OK branch does. */
		SLATE_EVENT(FOnSimCopterGraphicsSettingsClosed, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Scalability 0..4 -> Low/Medium/High/Epic/Cinematic, and -1 -> Custom. */
	static FText GetQualityLevelLabel(int32 Level);

	/** The frame-rate ladder the limit row steps through; 0 is "Unlimited". */
	static void GetFrameRateOptions(TArray<float>& OutRates);

private:
	TObjectPtr<USimCopterHangarArt> Art;
	FOnSimCopterGraphicsSettingsClosed OnAccepted;
	FOnSimCopterGraphicsSettingsClosed OnCancelled;

	TArray<TSharedRef<FButtonStyle>> ButtonStyles;

	/** SComboBox holds a raw pointer to its options array, so each row's has to be kept alive. */
	TArray<TSharedRef<TArray<TSharedPtr<int32>>>> ComboOptionSources;

	/** Screen resolutions offered by the display, deduplicated and sorted. */
	TArray<FIntPoint> Resolutions;

	/**
	 * Everything the page can change, snapshotted on entry so Cancel can put it back.
	 *
	 * Deliberately field by field rather than UGameUserSettings::LoadSettings + ApplySettings:
	 * re-reading the ini reverts to the last SAVED state, which is not the state on entry when
	 * anything overrode it - `-ResX`/`-ResY` on the command line is the obvious case, and
	 * cancelling would then resize the window even though the player never touched the resolution.
	 */
	struct FEnteredState
	{
		bool bLowPowerMode = false;
		bool bDlssEnabled = false;
		uint8 DlssQuality = 0;
		uint8 FrameGenMode = 0;
		int32 FrameGenMultiple = 2;
		uint8 ReflexMode = 0;
		uint8 LumenMode = 0;
		uint8 AntiAliasingMethod = 4; // ESimCopterAntiAliasingMethod::Tsr
		bool bVolumetricFog = true;
		float EmissiveBrightness = 1.0f;
		uint8 TimeOfDayMode = 0;
		float StaticTimeOfDayHours = 12.0f;
		float DayRealMinutes = 7.0f;
		float NightRealMinutes = 3.0f;
		float HudScale = 1.0f;

		FIntPoint Resolution = FIntPoint::ZeroValue;
		int32 WindowMode = 0;
		bool bVSync = false;
		float FrameRateLimit = 0.0f;
		float ResolutionScale = 1.0f;
		/** View distance, AA, post, shadow, GI, reflection, texture, effects, foliage, shading. */
		int32 Quality[10] = {};
	};
	FEnteredState Entered;

	void CaptureEnteredState();
	void RestoreEnteredState();

	/**
	 * One list row: a label, a value readout, and a pair of steppers that walk an integer index.
	 * Rows whose choice set is a real enumeration also get a dropdown, which is what the DLSS and
	 * frame-generation rows use.
	 */
	struct FRowBinding
	{
		TFunction<int32()> GetIndex;
		TFunction<void(int32)> SetIndex;
		TFunction<int32()> GetCount;
		TFunction<FText(int32)> GetOptionLabel;
		TFunction<bool()> IsEnabled;
	};

	TSharedRef<SWidget> BuildHeading(const FText& Text);
	TSharedRef<SWidget> BuildDropdownRow(const FText& Label, FRowBinding Binding);
	TSharedRef<SWidget> BuildSliderRow(
		const FText& Label,
		TFunction<float()> GetAlpha,
		TFunction<void(float)> SetAlpha,
		TFunction<FText()> GetText,
		TFunction<bool()> IsEnabled = TFunction<bool()>());
	TSharedRef<SWidget> BuildCheckboxRow(
		const FText& Label,
		TFunction<bool()> IsChecked,
		TFunction<void(bool)> SetChecked,
		TFunction<bool()> IsEnabled = TFunction<bool()>());

	/** A wrapped line of explanatory text under a row, for the one setting that needs explaining. */
	TSharedRef<SWidget> BuildNote(const FText& Text);

	void PopulateRows(const TSharedRef<SVerticalBox>& Rows);

	/**
	 * Pushes the Time of Day rows at the level's day sequence through
	 * `USimCopterDayNightSubsystem`, so the sun moves while the page is still open. Every other row
	 * here applies through UGameUserSettings or a console variable, which is why this one needs a
	 * helper of its own.
	 */
	void ApplyTimeOfDay();

	void Accept();
	void Cancel();
};
