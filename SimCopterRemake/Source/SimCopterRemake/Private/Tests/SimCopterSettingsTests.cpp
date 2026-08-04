// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Game/SimCopterSettings.h"
#include "Misc/AutomationTest.h"
#include "UI/SSimCopterCheckupSlider.h"
#include "UI/SSimCopterCitySettings.h"
#include "UI/SSimCopterControlSettings.h"
#include "UI/SSimCopterSettingsMenu.h"
#include "UI/SSimCopterSoundSettings.h"

// The in-game Settings screen's decodable parts: the two-variant item list, the eight City
// Settings sliders and their ranges, the Sound dialog's volume mapping, and the horizontal slider
// the Game Volume fader needed. The pages themselves want artwork and a viewport, so these cover
// what can be wrong silently. Ground truth: Docs/scratchpad/settings-DECODED.md.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterSettingsMenuItemsTest,
	"SimCopter.Settings.MenuItems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterSettingsMenuItemsTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterSettingsMenuLayout;

	// FUN_00437d10's two descriptor variants. The command base is 0 with City Settings on the page
	// and 1 without, which is what keeps an item's id the same either way - so every row below the
	// first must map to the SAME command in both.
	TestEqual(TEXT("User game row 0 is City Settings"),
		SSimCopterSettingsMenu::GetItemForRow(0, /*bHasCitySettings=*/true), ESimCopterSettingsItem::CitySettings);
	TestEqual(TEXT("Career row 0 is Graphics"),
		SSimCopterSettingsMenu::GetItemForRow(0, /*bHasCitySettings=*/false), ESimCopterSettingsItem::Graphics);

	for (int32 Row = 0; Row < FullItemCount - 1; ++Row)
	{
		TestEqual(
			*FString::Printf(TEXT("Career row %d matches user-game row %d"), Row, Row + 1),
			SSimCopterSettingsMenu::GetItemForRow(Row, /*bHasCitySettings=*/false),
			SSimCopterSettingsMenu::GetItemForRow(Row + 1, /*bHasCitySettings=*/true));
	}

	// The last row is Continue in both.
	TestEqual(TEXT("User game last row"),
		SSimCopterSettingsMenu::GetItemForRow(FullItemCount - 1, true), ESimCopterSettingsItem::Continue);
	TestEqual(TEXT("Career last row"),
		SSimCopterSettingsMenu::GetItemForRow(FullItemCount - 2, false), ESimCopterSettingsItem::Continue);

	// Every item has a label; a gap would show as an empty plate.
	for (int32 Index = 0; Index < FullItemCount; ++Index)
	{
		TestFalse(
			*FString::Printf(TEXT("Item %d has a label"), Index),
			SSimCopterSettingsMenu::GetItemLabel(static_cast<ESimCopterSettingsItem>(Index)).IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterSettingsMenuLayoutTest,
	"SimCopter.Settings.MenuLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterSettingsMenuLayoutTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterSettingsMenuLayout;

	// Descriptor +0x34: 64 with eight items, 104 with seven. Both end on the same plate, which is
	// the whole point of the two variants - the page prints eight and the shorter list starts on
	// the second.
	TestEqual(TEXT("User game first row"), GetRowTop(0, true), FirstItemYWithCitySettings);
	TestEqual(TEXT("Career first row"), GetRowTop(0, false), FirstItemYWithoutCitySettings);
	TestEqual(TEXT("Career row 0 sits on user-game plate 1"), GetRowTop(0, false), GetRowTop(1, true));
	TestEqual(TEXT("Last plate is shared"),
		GetRowTop(FullItemCount - 1, true), GetRowTop(FullItemCount - 2, false));

	// Descriptor +0x3c: the stride.
	TestEqual(TEXT("Stride"), GetRowTop(1, true) - GetRowTop(0, true), ItemStride);

	// The eight plates have to fit on playmenu.bmp with the font's own band inside the page.
	TestTrue(TEXT("Last row fits the page"),
		GetRowTop(FullItemCount - 1, true) + static_cast<float>(ItemFontHeight) < PageHeight);

	// FUN_0045fc60's band: as tall as the font, not as tall as the 40 px plate.
	const FRect Hit = GetRowHitRect(0, true);
	TestEqual(TEXT("Hit band height is the font height"), Hit.Height(), static_cast<float>(ItemFontHeight));
	TestTrue(TEXT("Hit band is shorter than the plate"), Hit.Height() < ItemStride);
	TestTrue(TEXT("Hit band stays on the page"), Hit.Right <= PageWidth);
	TestTrue(TEXT("Hit band starts left of the text"), Hit.Left < ItemX);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCitySettingsTest,
	"SimCopter.Settings.CitySettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCitySettingsTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterCitySettingsLayout;

	// FUN_0040bb50 after each construction: slider 0 is Difficulty over 0..3, the seven rates run
	// 0..100. Getting this wrong makes Difficulty a 0..100 control that reads as 33x too coarse.
	TestEqual(TEXT("Difficulty range"), GetSliderMax(0), 3);
	for (int32 Index = 1; Index < SliderCount; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Weight %d range"), Index - 1), GetSliderMax(Index), 100);
	}

	// The eight troughs are evenly spaced across cityset.bmp and none runs off it.
	const float Gap = SliderX[1] - SliderX[0];
	for (int32 Index = 1; Index < SliderCount; ++Index)
	{
		TestTrue(
			*FString::Printf(TEXT("Slider %d is evenly spaced"), Index),
			FMath::IsNearlyEqual(SliderX[Index] - SliderX[Index - 1], Gap, 1.0f));
	}
	TestTrue(TEXT("Last slider fits the page"), SliderX[SliderCount - 1] + SliderWidth <= PageWidth);
	TestTrue(TEXT("Troughs fit the page"), SliderBottom < PageHeight);

	// slidcity.bmp is 26x202, which is exactly the rect - that is why the loose track art is drawn
	// here and the Check-up dialog's is not.
	TestEqual(TEXT("Trough width matches slidcity.bmp"), SliderWidth, 26.0f);
	TestEqual(TEXT("Trough height matches slidcity.bmp"), SliderBottom - SliderTop, 202.0f);

	// The labels straddle the troughs, four above and four below, and each is on its own side.
	int32 Above = 0;
	for (int32 Index = 0; Index < SliderCount; ++Index)
	{
		const FRect& Rect = Labels[Index].Rect;
		if (Rect.Top < SliderTop)
		{
			TestTrue(*FString::Printf(TEXT("Label %d clears the troughs above"), Index), Rect.Bottom <= SliderTop);
			++Above;
		}
		else
		{
			TestTrue(*FString::Printf(TEXT("Label %d clears the troughs below"), Index), Rect.Top >= SliderBottom);
		}
		TestEqual(
			*FString::Printf(TEXT("Label %d string id"), Index),
			Labels[Index].StringId, 333 + Index);
	}
	TestEqual(TEXT("Four labels above the troughs"), Above, 4);

	// Slider index <-> FSimCopterCareerCity, which is what pins each label to its slider: the
	// dialog's construction order IS the record's field order.
	FSimCopterCitySettingsValues Values;
	for (int32 Index = 0; Index < SliderCount; ++Index)
	{
		SSimCopterCitySettings::SetValueForSlider(Values, Index, GetSliderMax(Index));
	}
	TestEqual(TEXT("Difficulty round trip"), Values.Difficulty, 3);
	for (int32 Index = 0; Index < 7; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Weight %d round trip"), Index), Values.Weights[Index], 100.0f);
	}

	// Out of range is clamped, not wrapped - the original's FUN_0040ba90 clamps to [min, max].
	SSimCopterCitySettings::SetValueForSlider(Values, 0, 99);
	TestEqual(TEXT("Difficulty clamps at its own maximum"), Values.Difficulty, 3);
	SSimCopterCitySettings::SetValueForSlider(Values, 3, -5);
	TestEqual(TEXT("A rate clamps at zero"), Values.Weights[2], 0.0f);

	// Every slider has a label and every label a name.
	for (int32 Index = 0; Index < SliderCount; ++Index)
	{
		TestFalse(
			*FString::Printf(TEXT("Slider %d is named"), Index),
			SSimCopterCitySettings::GetLabel(Index).IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterSoundSettingsTest,
	"SimCopter.Settings.SoundSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterSoundSettingsTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterSoundSettingsLayout;

	// FUN_0040bb20 / FUN_0040bb50 on both volume sliders.
	TestEqual(TEXT("Volume minimum"), VolumeMin, 320);
	TestEqual(TEXT("Volume maximum"), VolumeMax, 10000);

	// The ends map exactly, or a fader dragged to the bottom would not be the original's quietest.
	TestEqual(TEXT("Alpha 0 is the minimum"), SSimCopterSoundSettings::AlphaToVolume(0.0f), VolumeMin);
	TestEqual(TEXT("Alpha 1 is the maximum"), SSimCopterSoundSettings::AlphaToVolume(1.0f), VolumeMax);
	TestTrue(TEXT("Alpha 0 round trips"),
		FMath::IsNearlyZero(SSimCopterSoundSettings::VolumeToAlpha(VolumeMin)));
	TestTrue(TEXT("Alpha 1 round trips"),
		FMath::IsNearlyEqual(SSimCopterSoundSettings::VolumeToAlpha(VolumeMax), 1.0f));

	// Out of range clamps rather than extrapolating.
	TestEqual(TEXT("Below zero clamps"), SSimCopterSoundSettings::AlphaToVolume(-1.0f), VolumeMin);
	TestEqual(TEXT("Above one clamps"), SSimCopterSoundSettings::AlphaToVolume(2.0f), VolumeMax);

	// A round trip through both directions has to land back where it started, within the one-step
	// rounding the integer index costs.
	for (int32 Volume = VolumeMin; Volume <= VolumeMax; Volume += 971)
	{
		const int32 Back = SSimCopterSoundSettings::AlphaToVolume(SSimCopterSoundSettings::VolumeToAlpha(Volume));
		TestTrue(
			*FString::Printf(TEXT("Volume %d round trips"), Volume),
			FMath::Abs(Back - Volume) <= 1);
	}

	// Every control lands inside sound.bmp.
	const FRect Rects[] = { GameVolumeRect, RadioVolumeRect, TunerRect, GameVolumeLabelRect,
		CommercialsLabelRect, DjLabelRect, AutoQuietLabelRect, VolLabelRect };
	for (const FRect& Rect : Rects)
	{
		TestTrue(TEXT("Control is on the page"),
			Rect.Left >= 0.0f && Rect.Top >= 0.0f && Rect.Right <= PageWidth && Rect.Bottom <= PageHeight);
	}

	// The one horizontal slider in the game: the Game Volume fader is wider than it is tall, and
	// the other two are the other way round.
	TestTrue(TEXT("Game Volume is horizontal"), GameVolumeRect.Width() > GameVolumeRect.Height());
	TestTrue(TEXT("Radio volume is vertical"), RadioVolumeRect.Height() > RadioVolumeRect.Width());
	TestTrue(TEXT("Tuner is vertical"), TunerRect.Height() > TunerRect.Width());

	// Its label sits under it, and the three toggles sit above theirs.
	TestTrue(TEXT("Game Volume label is below the fader"), GameVolumeLabelRect.Top >= GameVolumeRect.Bottom);
	TestTrue(TEXT("Commercials toggle is above its label"), CommercialsToggleRect.Bottom <= CommercialsLabelRect.Top);
	TestTrue(TEXT("DJ toggle is above its label"), DjToggleRect.Bottom <= DjLabelRect.Top);
	TestTrue(TEXT("Auto-Quiet toggle is above its label"), AutoQuietToggleRect.Bottom <= AutoQuietLabelRect.Top);

	// The two buttons stack in the same column, one button height apart.
	TestEqual(TEXT("Buttons share a column"), OkButtonY + SimCopterFrontEnd::ButtonHeight, CancelButtonY);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterHorizontalSliderTest,
	"SimCopter.Settings.HorizontalSlider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterHorizontalSliderTest::RunTest(const FString& Parameters)
{
	// The Game Volume fader is the one horizontal slider in the game, so its axis is worth pinning
	// down: zero is at the LEFT, where the vertical form puts zero at the bottom.
	const FVector2f Track(192.0f, 32.0f);
	const FVector2f Thumb(22.0f, 18.0f);
	const float Travel = Track.X - Thumb.X;

	TestEqual(TEXT("Zero parks at the left"),
		SSimCopterCheckupSlider::GetThumbTopLeft(Track, Thumb, 0.0f, Orient_Horizontal).X, 0.0f);
	TestEqual(TEXT("One parks a thumb short of the right"),
		SSimCopterCheckupSlider::GetThumbTopLeft(Track, Thumb, 1.0f, Orient_Horizontal).X, Travel);
	TestEqual(TEXT("Half is halfway"),
		SSimCopterCheckupSlider::GetThumbTopLeft(Track, Thumb, 0.5f, Orient_Horizontal).X, Travel * 0.5f);

	// Centred across the track, as the vertical form is centred along it.
	TestEqual(TEXT("Centred across the track"),
		SSimCopterCheckupSlider::GetThumbTopLeft(Track, Thumb, 0.5f, Orient_Horizontal).Y,
		(Track.Y - Thumb.Y) * 0.5f);

	// The vertical form is untouched: zero still at the bottom.
	TestEqual(TEXT("Vertical zero is still at the bottom"),
		SSimCopterCheckupSlider::GetThumbTopLeft(FVector2f(26.0f, 202.0f), Thumb, 0.0f).Y, 202.0f - 18.0f);

	// Click-to-value takes the cursor as the middle of the thumb, and clamps.
	TestEqual(TEXT("Click at the left end"),
		SSimCopterCheckupSlider::GetValueAtLocalX(Track.X, Thumb.X, 0.0f), 0.0f);
	TestEqual(TEXT("Click at the right end"),
		SSimCopterCheckupSlider::GetValueAtLocalX(Track.X, Thumb.X, Track.X), 1.0f);
	TestEqual(TEXT("Click in the middle"),
		SSimCopterCheckupSlider::GetValueAtLocalX(Track.X, Thumb.X, Track.X * 0.5f), 0.5f);

	// A track no wider than its thumb has nowhere to travel and must not divide by zero.
	TestEqual(TEXT("Degenerate track"),
		SSimCopterCheckupSlider::GetValueAtLocalX(Thumb.X, Thumb.X, 5.0f), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterControlSettingsTest,
	"SimCopter.Settings.ControlBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterControlSettingsTest::RunTest(const FString& Parameters)
{
	// The project prefix goes and the camel case is spaced, or the list reads as identifiers.
	TestEqual(TEXT("Action label"),
		SSimCopterControlSettings::MakeDisplayLabel(FName(TEXT("SimCopterEngineStart")), false, 1.0f).ToString(),
		FString(TEXT("Engine Start")));
	TestEqual(TEXT("Long action label"),
		SSimCopterControlSettings::MakeDisplayLabel(FName(TEXT("SimCopterControllerRightTrigger")), false, 1.0f).ToString(),
		FString(TEXT("Controller Right Trigger")));

	// An axis has one row per direction, so the sign has to be on the label or the two rows look
	// like duplicates of each other.
	const FString Positive =
		SSimCopterControlSettings::MakeDisplayLabel(FName(TEXT("SimCopterPitch")), true, 1.0f).ToString();
	const FString Negative =
		SSimCopterControlSettings::MakeDisplayLabel(FName(TEXT("SimCopterPitch")), true, -1.0f).ToString();
	TestTrue(TEXT("Positive axis is marked"), Positive.Contains(TEXT("+")));
	TestTrue(TEXT("Negative axis is marked"), Negative.Contains(TEXT("-")));
	TestNotEqual(TEXT("The two directions differ"), Positive, Negative);

	// DefaultInput.ini has to parse, or the Defaults button silently does nothing. This also
	// catches a malformed mapping line added to the ini by hand.
	TArray<FSimCopterBinding> Defaults;
	if (SSimCopterControlSettings::ReadDefaultBindings(Defaults))
	{
		TestTrue(TEXT("Defaults were parsed"), Defaults.Num() > 0);

		bool bFoundSettingsKey = false;
		bool bFoundAxis = false;
		for (const FSimCopterBinding& Binding : Defaults)
		{
			TestTrue(TEXT("Every default names a mapping"), !Binding.Name.IsNone());
			TestTrue(TEXT("Every default names a key"), Binding.Key.IsValid());
			bFoundSettingsKey |= Binding.Name == FName(TEXT("SimCopterSettingsMenu"));
			bFoundAxis |= Binding.bIsAxis;
		}

		// The Settings screen is unreachable without its binding, and an axis-free parse would
		// mean only half the ini was read.
		TestTrue(TEXT("The Settings key is bound"), bFoundSettingsKey);
		TestTrue(TEXT("Axis mappings were read too"), bFoundAxis);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterDisplayResolutionTest,
	"SimCopter.Settings.DisplayResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterDisplayResolutionTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterDisplay;

	const auto MakeMonitor = [](const FIntPoint Native, const FIntRect Rect, const bool bPrimary)
	{
		FMonitor Monitor;
		Monitor.NativeResolution = Native;
		Monitor.DisplayRect = Rect;
		Monitor.bIsPrimary = bPrimary;
		return Monitor;
	};

	// A 4K primary with a 1080p panel to its right - the layout that makes "which screen is the game
	// actually on" a question worth answering.
	const FMonitor Primary4K = MakeMonitor(FIntPoint(3840, 2160), FIntRect(0, 0, 3840, 2160), true);
	const FMonitor Secondary1080 = MakeMonitor(FIntPoint(1920, 1080), FIntRect(3840, 0, 5760, 1080), false);
	const TArray<FMonitor> TwoMonitors = { Primary4K, Secondary1080 };

	{
		TestEqual(TEXT("A window on the primary takes the primary's native size"),
			FindNativeResolutionForWindow(TwoMonitors, FIntRect(100, 100, 1380, 820)),
			FIntPoint(3840, 2160));

		TestEqual(TEXT("A window on the second monitor takes ITS native size, not the primary's"),
			FindNativeResolutionForWindow(TwoMonitors, FIntRect(4000, 100, 5280, 820)),
			FIntPoint(1920, 1080));
	}

	// Straddling is settled by overlap area, not by the window's origin: the monitor showing most of
	// the window is the one the game is being watched on.
	{
		// 200px wide on the 4K, 1080px on the 1080p.
		TestEqual(TEXT("A straddling window follows the larger overlap"),
			FindNativeResolutionForWindow(TwoMonitors, FIntRect(3640, 100, 4920, 820)),
			FIntPoint(1920, 1080));

		// The same window shifted so most of it is back on the 4K.
		TestEqual(TEXT("...and follows it back the other way"),
			FindNativeResolutionForWindow(TwoMonitors, FIntRect(2700, 100, 3980, 820)),
			FIntPoint(3840, 2160));
	}

	// An unshown window reports a zero rect, which overlaps nothing. That must not come back empty,
	// or a first run would decline to seed and open at UGameUserSettings' 1280x720 instead.
	{
		TestEqual(TEXT("A zero-size window falls back to the primary"),
			FindNativeResolutionForWindow(TwoMonitors, FIntRect(0, 0, 0, 0)),
			FIntPoint(3840, 2160));

		TestEqual(TEXT("An off-screen window falls back to the primary"),
			FindNativeResolutionForWindow(TwoMonitors, FIntRect(-4000, -4000, -3000, -3000)),
			FIntPoint(3840, 2160));

		const TArray<FMonitor> NoPrimary = { Secondary1080 };
		TestEqual(TEXT("With no monitor flagged primary, the first usable one wins"),
			FindNativeResolutionForWindow(NoPrimary, FIntRect(0, 0, 0, 0)),
			FIntPoint(1920, 1080));
	}

	// Native resolution is NOT the display rect: Windows scaling makes a 4K panel report a 2560x1440
	// desktop rect, and seeding from the rect would open the game at the scaled size.
	{
		const TArray<FMonitor> Scaled = {
			MakeMonitor(FIntPoint(3840, 2160), FIntRect(0, 0, 2560, 1440), true)
		};
		TestEqual(TEXT("A DPI-scaled monitor still seeds its real panel size"),
			FindNativeResolutionForWindow(Scaled, FIntRect(0, 0, 1280, 720)),
			FIntPoint(3840, 2160));
	}

	// Degenerate inputs answer zero rather than a made-up mode, so the caller can decline to seed.
	{
		TestEqual(TEXT("No monitors at all"),
			FindNativeResolutionForWindow(TArray<FMonitor>(), FIntRect(0, 0, 1280, 720)),
			FIntPoint::ZeroValue);

		const TArray<FMonitor> Unreported = {
			MakeMonitor(FIntPoint::ZeroValue, FIntRect(0, 0, 1920, 1080), true)
		};
		TestEqual(TEXT("A monitor that reports no native size is not usable"),
			FindNativeResolutionForWindow(Unreported, FIntRect(0, 0, 1280, 720)),
			FIntPoint::ZeroValue);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterTimeOfDayFormatTest,
	"SimCopter.Settings.TimeOfDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterTimeOfDayFormatTest::RunTest(const FString& Parameters)
{
	const auto Formatted = [](const float Hours)
	{
		return USimCopterSettings::FormatTimeOfDay(Hours).ToString();
	};

	TestEqual(TEXT("Midnight"), Formatted(0.0f), TEXT("00:00"));
	TestEqual(TEXT("Noon"), Formatted(12.0f), TEXT("12:00"));
	TestEqual(TEXT("Half past reads as 30 minutes, not 0.5"), Formatted(13.5f), TEXT("13:30"));
	TestEqual(TEXT("A quarter past"), Formatted(6.25f), TEXT("06:15"));
	TestEqual(TEXT("Just before midnight"), Formatted(23.99f), TEXT("23:59"));

	// The Static Time slider's top end is exactly 24, which is midnight again - "24:00" would be the
	// one reading on the clock that does not exist.
	TestEqual(TEXT("The slider's top end wraps to midnight"), Formatted(24.0f), TEXT("00:00"));

	// The setter clamps, but the formatter is a public static and gets whatever a caller has.
	TestEqual(TEXT("Negative hours clamp to midnight"), Formatted(-3.0f), TEXT("00:00"));
	TestEqual(TEXT("Hours past a full day wrap"), Formatted(26.5f), TEXT("02:30"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
