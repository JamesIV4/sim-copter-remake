// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Flight/SimCopterFlightModel.h"
#include "Game/SimCopterCheckup.h"
#include "Ground/SimCopterFlashingLights.h"
#include "Misc/AutomationTest.h"
#include "UI/SSimCopterCheckupMenu.h"
#include "UI/SSimCopterCheckupSlider.h"

namespace
{
// A Jet Ranger-ish state: 604 hit points, 91.2 gallons, $1 per point and $2 per gallon, parked at
// the airport with money in the bank.
FSimCopterCheckupState MakeState()
{
	FSimCopterCheckupState State;
	State.HitPoints = 604;
	State.MaxHitPoints = 604;
	State.DollarsPerHitPoint1616 = SimCopterFixed::FromFloat(1.0f);
	State.Fuel1616 = SimCopterFixed::FromFloat(91.2f);
	State.FuelCapacity1616 = SimCopterFixed::FromFloat(91.2f);
	State.DollarsPerGallon1616 = SimCopterFixed::FromFloat(2.0f);
	State.bTearGasFitted = false;
	State.TearGasRounds = 0;
	State.bAtAirport = true;
	State.Funds = 100000;
	return State;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCheckupSliderRangeTest,
	"SimCopter.Checkup.SliderRanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCheckupSliderRangeTest::RunTest(const FString& Parameters)
{
	FSimCopterCheckupState State = MakeState();

	// Nothing wrong with the aircraft: both dollar sliders are dead.
	TestEqual(TEXT("Undamaged damage slider"), FSimCopterCheckup::GetDamageSliderMaxDollars(State), 0);
	TestEqual(TEXT("Full tank fuel slider"), FSimCopterCheckup::GetFuelSliderMaxDollars(State), 0);

	// FUN_0048a380: (max - hitPoints) * dollarsPerHitPoint.
	State.HitPoints = 504;
	TestEqual(TEXT("100 points of damage at $1/point"), FSimCopterCheckup::GetDamageSliderMaxDollars(State), 100);

	// FUN_0048a480: (capacity - fuel) * dollarsPerGallon.
	State.Fuel1616 = SimCopterFixed::FromFloat(41.2f);
	TestEqual(TEXT("50 gallons short at $2/gallon"), FSimCopterCheckup::GetFuelSliderMaxDollars(State), 100);

	// Both maxima triple away from the airport.
	State.bAtAirport = false;
	TestEqual(TEXT("Damage off-airport surcharge"), FSimCopterCheckup::GetDamageSliderMaxDollars(State), 300);
	TestEqual(TEXT("Fuel off-airport surcharge"), FSimCopterCheckup::GetFuelSliderMaxDollars(State), 300);
	State.bAtAirport = true;

	// FUN_00444690 only enables the tear-gas slider when the launcher is fitted; FUN_0048a560
	// caps it at ten.
	TestEqual(TEXT("Tear gas slider without the launcher"), FSimCopterCheckup::GetTearGasSliderMaxRounds(State), 0);
	State.bTearGasFitted = true;
	TestEqual(TEXT("Tear gas slider, empty"), FSimCopterCheckup::GetTearGasSliderMaxRounds(State), 10);
	State.TearGasRounds = 7;
	TestEqual(TEXT("Tear gas slider, 7 aboard"), FSimCopterCheckup::GetTearGasSliderMaxRounds(State), 3);
	State.TearGasRounds = 10;
	TestEqual(TEXT("Tear gas slider, full"), FSimCopterCheckup::GetTearGasSliderMaxRounds(State), 0);

	// FUN_0048a570: $50 a canister, and the total is the plain sum (FUN_004447e0).
	TestEqual(TEXT("Tear gas price"), FSimCopterCheckup::GetTearGasCostDollars(4), 200);
	FSimCopterCheckupOrder Order;
	Order.DamageDollars = 100;
	Order.FuelDollars = 60;
	Order.TearGasRounds = 3;
	TestEqual(TEXT("Total cost"), FSimCopterCheckup::GetTotalCostDollars(Order), 100 + 60 + 150);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCheckupApplyTest,
	"SimCopter.Checkup.Apply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCheckupApplyTest::RunTest(const FString& Parameters)
{
	FSimCopterCheckupState State = MakeState();
	State.HitPoints = 504;
	State.Fuel1616 = SimCopterFixed::FromFloat(41.2f);

	// FUN_0048a3e0: dollars / dollarsPerHitPoint, added to the hit points.
	TestEqual(TEXT("Half the repair"), FSimCopterCheckup::ApplyDamageRepair(State, 50), 554);
	TestEqual(TEXT("Full repair"), FSimCopterCheckup::ApplyDamageRepair(State, 100), 604);
	// Overpaying cannot push past the maximum.
	TestEqual(TEXT("Repair clamps at max"), FSimCopterCheckup::ApplyDamageRepair(State, 400), 604);

	// FUN_0048a4e0: dollars / dollarsPerGallon gallons, clamped at the tank.
	TestEqual(
		TEXT("Half a tank"),
		FSimCopterCheckup::ApplyFuel(State, 50),
		SimCopterFixed::FromFloat(66.2f));
	TestEqual(
		TEXT("Fuel clamps at capacity"),
		FSimCopterCheckup::ApplyFuel(State, 1000),
		State.FuelCapacity1616);

	// The off-airport surcharge is taken back out of the repair, so a dollar buys a third as much
	// there - but NOT out of the fuel, which is the original's own asymmetry.
	State.bAtAirport = false;
	TestEqual(TEXT("Off-airport repair buys a third"), FSimCopterCheckup::ApplyDamageRepair(State, 150), 554);
	TestEqual(
		TEXT("Off-airport fuel is not divided"),
		FSimCopterCheckup::ApplyFuel(State, 50),
		SimCopterFixed::FromFloat(66.2f));
	State.bAtAirport = true;

	// FUN_0048a3e0's tail clamp: a repair leaving fewer than four hit points zeroes them.
	FSimCopterCheckupState Wrecked = MakeState();
	Wrecked.HitPoints = 1;
	TestEqual(TEXT("Under four hit points snaps to zero"), FSimCopterCheckup::ApplyDamageRepair(Wrecked, 0), 0);
	TestEqual(TEXT("Four hit points survives"), FSimCopterCheckup::ApplyDamageRepair(Wrecked, 3), 4);

	// FUN_0048b130: dollars / 50 canisters.
	TestEqual(TEXT("Tear gas rounds bought"), FSimCopterCheckup::ApplyTearGas(150, 2), 5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCheckupOfferTest,
	"SimCopter.Checkup.Offer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCheckupOfferTest::RunTest(const FString& Parameters)
{
	FSimCopterCheckupState State = MakeState();

	// FUN_00444750: away from the airport the panel is never offered.
	State.HitPoints = 0;
	State.Fuel1616 = 0;
	State.bAtAirport = false;
	TestFalse(TEXT("Never offered off-airport"), FSimCopterCheckup::ShouldOffer(State));

	// A pristine aircraft on the pad has nothing to sell.
	State = MakeState();
	TestFalse(TEXT("Not offered with nothing to do"), FSimCopterCheckup::ShouldOffer(State));

	// $21 is the threshold on either dollar slider.
	State.HitPoints = State.MaxHitPoints - 20;
	TestFalse(TEXT("$20 of damage is below the threshold"), FSimCopterCheckup::ShouldOffer(State));
	State.HitPoints = State.MaxHitPoints - 21;
	TestTrue(TEXT("$21 of damage offers"), FSimCopterCheckup::ShouldOffer(State));

	State = MakeState();
	State.Fuel1616 = State.FuelCapacity1616 - SimCopterFixed::FromFloat(10.5f); // $21 at $2/gallon
	TestTrue(TEXT("$21 of fuel offers"), FSimCopterCheckup::ShouldOffer(State));

	// Or fewer than five canisters aboard, with the launcher fitted.
	State = MakeState();
	State.bTearGasFitted = true;
	State.TearGasRounds = 5;
	TestFalse(TEXT("Five canisters is enough"), FSimCopterCheckup::ShouldOffer(State));
	State.TearGasRounds = 4;
	TestTrue(TEXT("Four canisters offers"), FSimCopterCheckup::ShouldOffer(State));
	State.bTearGasFitted = false;
	TestFalse(TEXT("No launcher, no tear gas offer"), FSimCopterCheckup::ShouldOffer(State));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCheckupClampOrderTest,
	"SimCopter.Checkup.ClampOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCheckupClampOrderTest::RunTest(const FString& Parameters)
{
	FSimCopterCheckupState State = MakeState();
	State.HitPoints = 504;                                   // $100 of repair
	State.Fuel1616 = SimCopterFixed::FromFloat(41.2f);        // $100 of fuel
	State.bTearGasFitted = true;                             // 10 canisters = $500

	// An order past every slider's ceiling comes back at the ceiling.
	FSimCopterCheckupOrder Greedy;
	Greedy.DamageDollars = 9999;
	Greedy.FuelDollars = 9999;
	Greedy.TearGasRounds = 99;
	FSimCopterCheckupOrder Clamped = FSimCopterCheckup::ClampOrder(State, Greedy);
	TestEqual(TEXT("Damage clamped to slider"), Clamped.DamageDollars, 100);
	TestEqual(TEXT("Fuel clamped to slider"), Clamped.FuelDollars, 100);
	TestEqual(TEXT("Tear gas clamped to ten"), Clamped.TearGasRounds, 10);

	// Funds are spent down in FUN_004385c0's order - damage, then fuel, then tear gas.
	State.Funds = 150;
	Clamped = FSimCopterCheckup::ClampOrder(State, Greedy);
	TestEqual(TEXT("Damage takes its full share first"), Clamped.DamageDollars, 100);
	TestEqual(TEXT("Fuel takes what is left"), Clamped.FuelDollars, 50);
	TestEqual(TEXT("Nothing left for tear gas"), Clamped.TearGasRounds, 0);
	TestTrue(
		TEXT("Total never exceeds funds"),
		FSimCopterCheckup::GetTotalCostDollars(Clamped) <= State.Funds);

	State.Funds = 0;
	Clamped = FSimCopterCheckup::ClampOrder(State, Greedy);
	TestEqual(TEXT("Broke buys nothing"), FSimCopterCheckup::GetTotalCostDollars(Clamped), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCheckupLayoutTest,
	"SimCopter.Checkup.Layout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCheckupLayoutTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterCheckupLayout;

	// Every control rectangle FUN_00443c20 pushes has to land inside CHECKUP.BMP, or it is being
	// read out of the listing wrongly - the frame shifts by four whenever a PUSH lands between
	// the stores, which is exactly how a plausible-looking but wrong number gets in.
	const auto TestOnPage = [this](const TCHAR* What, const FCheckupRect& Rect)
	{
		TestTrue(FString::Printf(TEXT("%s is on the page"), What),
			Rect.Left >= 0.0f && Rect.Top >= 0.0f
			&& Rect.Right <= PageWidth && Rect.Bottom <= PageHeight
			&& Rect.Width() > 0.0f && Rect.Height() > 0.0f);
	};

	TestOnPage(TEXT("Title"), TitleRect);
	TestOnPage(TEXT("Funds label"), FundsLabelRect);
	TestOnPage(TEXT("Funds value"), FundsValueRect);
	TestOnPage(TEXT("Total label"), TotalLabelRect);
	TestOnPage(TEXT("Total value"), TotalValueRect);
	for (int32 Index = 0; Index < SliderCount; ++Index)
	{
		TestOnPage(TEXT("Slider track"), SliderTrackRect[Index]);
		TestOnPage(TEXT("Slider label"), SliderLabelRect[Index]);
		TestOnPage(TEXT("Slider value"), SliderValueRect[Index]);
	}

	// The title is the one control the original builds at a different size (30 against 14).
	TestTrue(TEXT("Title font is the larger one"), TitleFontSize > BodyFontSize);

	// Both readouts share the top well's second line, each number immediately right of its label
	// rather than pushed to the panel edge - the Total Cost readout is NOT in the bottom recess,
	// which is the button tray.
	TestEqual(TEXT("Funds and Total share a line"), FundsLabelRect.Top, TotalLabelRect.Top);
	TestEqual(TEXT("Funds value shares its label's line"), FundsValueRect.Top, FundsLabelRect.Top);
	TestTrue(TEXT("Funds value follows its label"), FundsValueRect.Left >= FundsLabelRect.Right);
	TestTrue(TEXT("Total value follows its label"), TotalValueRect.Left >= TotalLabelRect.Right);
	TestTrue(TEXT("Readouts sit below the title"), FundsLabelRect.Top >= TitleRect.Bottom);
	TestTrue(TEXT("Readouts clear the button tray"), TotalValueRect.Bottom < ButtonY);

	// All three tracks are the same size, but the Fuel track is printed lower and left of the
	// panel's centre line. Placing it as "the middle of a symmetrical row" is the bug that left
	// its thumb off the printed groove.
	for (int32 Index = 1; Index < SliderCount; ++Index)
	{
		TestEqual(TEXT("Track width matches"), SliderTrackRect[Index].Width(), SliderTrackRect[0].Width());
		TestEqual(TEXT("Track height matches"), SliderTrackRect[Index].Height(), SliderTrackRect[0].Height());
	}
	TestTrue(TEXT("Fuel track is dropped"), SliderTrackRect[1].Top > SliderTrackRect[0].Top);
	TestTrue(
		TEXT("Fuel track is left of the panel centre"),
		SliderTrackRect[1].Left + SliderTrackRect[1].Width() * 0.5f < PageWidth * 0.5f);

	// The Fuel plate is above its track; the outer two are below theirs.
	TestTrue(TEXT("Damage plate is under its track"), SliderLabelRect[0].Top >= SliderTrackRect[0].Bottom);
	TestTrue(TEXT("Teargas plate is under its track"), SliderLabelRect[2].Top >= SliderTrackRect[2].Bottom);
	TestTrue(TEXT("Fuel plate is above its track"), SliderLabelRect[1].Bottom <= SliderTrackRect[1].Top);

	// Both buttons sit side by side on one line in the bottom tray.
	TestTrue(TEXT("Cancel follows OK"), CancelButtonX >= OkButtonX + ButtonWidth);
	TestTrue(TEXT("Buttons fit the page"), CancelButtonX + ButtonWidth <= PageWidth);
	TestTrue(TEXT("Buttons fit the page vertically"), ButtonY + ButtonHeight <= PageHeight);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCheckupSliderTravelTest,
	"SimCopter.Checkup.SliderTravel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCheckupSliderTravelTest::RunTest(const FString& Parameters)
{
	// A 26x202 control rect (FUN_00443c20) carrying SLIDERTV.BMP's 22x18 thumb.
	const FVector2f Track(26.0f, 202.0f);
	const FVector2f Thumb(22.0f, 18.0f);
	const float Travel = Track.Y - Thumb.Y;

	// The thumb is centred across the track: 22 wide in a 26 wide groove leaves 2 px either side.
	TestEqual(
		TEXT("Thumb is centred across the track"),
		SSimCopterCheckupSlider::GetThumbTopLeft(Track, Thumb, 0.5f).X,
		2.0f);

	// Zero is at the BOTTOM, and neither end hangs off the track.
	TestEqual(
		TEXT("Zero parks at the bottom"),
		SSimCopterCheckupSlider::GetThumbTopLeft(Track, Thumb, 0.0f).Y,
		Travel);
	TestEqual(
		TEXT("Full parks at the top"),
		SSimCopterCheckupSlider::GetThumbTopLeft(Track, Thumb, 1.0f).Y,
		0.0f);
	TestEqual(
		TEXT("Half sits mid-travel"),
		SSimCopterCheckupSlider::GetThumbTopLeft(Track, Thumb, 0.5f).Y,
		Travel * 0.5f);

	// Out-of-range values clamp rather than running the thumb off the end.
	TestEqual(
		TEXT("Below zero clamps to the bottom"),
		SSimCopterCheckupSlider::GetThumbTopLeft(Track, Thumb, -3.0f).Y,
		Travel);
	TestEqual(
		TEXT("Above one clamps to the top"),
		SSimCopterCheckupSlider::GetThumbTopLeft(Track, Thumb, 4.0f).Y,
		0.0f);

	// Clicking takes the cursor as the middle of the thumb, so a click at the very bottom of the
	// track still selects zero rather than something slightly above it.
	TestEqual(
		TEXT("Click at the bottom is zero"),
		SSimCopterCheckupSlider::GetValueAtLocalY(Track.Y, Thumb.Y, Track.Y),
		0.0f);
	TestEqual(
		TEXT("Click at the top is full"),
		SSimCopterCheckupSlider::GetValueAtLocalY(Track.Y, Thumb.Y, 0.0f),
		1.0f);
	TestEqual(
		TEXT("Click mid-track is a half"),
		SSimCopterCheckupSlider::GetValueAtLocalY(Track.Y, Thumb.Y, Track.Y * 0.5f),
		0.5f);

	// Round trip: the value a click selects puts the thumb's centre back under the cursor.
	for (const float ClickY : { 20.0f, 60.0f, 101.0f, 150.0f, 190.0f })
	{
		const float Value = SSimCopterCheckupSlider::GetValueAtLocalY(Track.Y, Thumb.Y, ClickY);
		const float ThumbCentre =
			SSimCopterCheckupSlider::GetThumbTopLeft(Track, Thumb, Value).Y + Thumb.Y * 0.5f;
		TestEqual(
			FString::Printf(TEXT("Thumb centre follows a click at %.0f"), ClickY),
			ThumbCentre,
			FMath::Clamp(ClickY, Thumb.Y * 0.5f, Track.Y - Thumb.Y * 0.5f),
			0.01f);
	}

	// A track shorter than its thumb has nowhere to move; it must not divide by zero.
	TestEqual(
		TEXT("Degenerate track reads zero"),
		SSimCopterCheckupSlider::GetValueAtLocalY(10.0f, 18.0f, 5.0f),
		0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterFlashingLightScheduleTest,
	"SimCopter.Lights.BlinkSchedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterFlashingLightScheduleTest::RunTest(const FString& Parameters)
{
	// FUN_00496c00's switch, colour by colour.
	TestEqual(TEXT("White phase"), FSimCopterFlashingLightSchedule::GetPhaseForPaletteIndex(0xf6), 0);
	TestEqual(TEXT("Red phase"), FSimCopterFlashingLightSchedule::GetPhaseForPaletteIndex(0xf9), 1);
	TestEqual(TEXT("Green phase"), FSimCopterFlashingLightSchedule::GetPhaseForPaletteIndex(0xfa), 2);
	TestEqual(TEXT("Yellow phase"), FSimCopterFlashingLightSchedule::GetPhaseForPaletteIndex(0xfb), 3);
	TestEqual(TEXT("Blue phase"), FSimCopterFlashingLightSchedule::GetPhaseForPaletteIndex(0xfc), 4);
	// Every unlisted colour shares the default arm.
	TestEqual(TEXT("Unlisted colour phase"), FSimCopterFlashingLightSchedule::GetPhaseForPaletteIndex(0x00), 5);
	TestEqual(TEXT("Unlisted colour phase (grey)"), FSimCopterFlashingLightSchedule::GetPhaseForPaletteIndex(0xf0), 5);

	// Exactly one colour is lit at a time, and phases 6 and 7 light nothing at all.
	const uint8 Colours[] = { 0xf6, 0xf9, 0xfa, 0xfb, 0xfc };
	for (int32 Phase = 0; Phase < FSimCopterFlashingLightSchedule::PhaseCount; ++Phase)
	{
		int32 LitCount = 0;
		for (const uint8 Colour : Colours)
		{
			LitCount += FSimCopterFlashingLightSchedule::IsLitAtPhase(Colour, Phase) ? 1 : 0;
		}
		const int32 Expected = Phase <= 4 ? 1 : 0;
		TestEqual(FString::Printf(TEXT("Lit colours at phase %d"), Phase), LitCount, Expected);
	}

	// The counter advances one phase per original frame and wraps at eight.
	TestEqual(TEXT("Phase at t=0"), FSimCopterFlashingLightSchedule::GetPhaseAtTime(0.0), 0);
	TestEqual(TEXT("Phase mid-frame"), FSimCopterFlashingLightSchedule::GetPhaseAtTime(0.04), 0);
	TestEqual(TEXT("Phase at one frame"), FSimCopterFlashingLightSchedule::GetPhaseAtTime(0.05), 1);
	TestEqual(TEXT("Phase at five frames"), FSimCopterFlashingLightSchedule::GetPhaseAtTime(0.25), 5);
	TestEqual(TEXT("Phase wraps after eight"), FSimCopterFlashingLightSchedule::GetPhaseAtTime(0.40), 0);
	TestEqual(TEXT("Phase wraps again"), FSimCopterFlashingLightSchedule::GetPhaseAtTime(0.45), 1);
	// Negative time has no original counterpart; it must not produce a negative index.
	TestEqual(TEXT("Negative time clamps"), FSimCopterFlashingLightSchedule::GetPhaseAtTime(-5.0), 0);

	return true;
}

#endif
