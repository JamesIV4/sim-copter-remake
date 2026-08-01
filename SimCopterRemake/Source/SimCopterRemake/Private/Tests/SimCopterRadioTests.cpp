// Radio tests. Everything here is the pure part of the station player: the ported RNG, the
// shuffle-bag behaviour that distinguishes it from a random draw, the scheduler's probability
// table, and the dial mapping the dash tuner uses.

#include "Audio/SimCopterRadio.h"
#include "Misc/AutomationTest.h"

using FRng = USimCopterRadioSubsystem::FLaggedFibonacci;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterRadioRandomTest,
	"SimCopter.Radio.Random",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterRadioRandomTest::RunTest(const FString& Parameters)
{
	// SCHOOK: RadioRandom 0x00455d70 - a subtractive lagged-Fibonacci, not MSVC rand and not the
	// people LFSR. The radio is the game's third generator.
	FRng Rng;
	Rng.Seed(12345);

	// Range: every value must land in [0, n).
	for (int32 Trial = 0; Trial < 4000; ++Trial)
	{
		const uint32 Value = Rng.Next(7);
		if (Value >= 7)
		{
			AddError(FString::Printf(TEXT("value %u out of range on trial %d"), Value, Trial));
			return false;
		}
	}

	// Modulo 0 must not divide by zero - the shuffle calls this with a count that can be 0.
	TestEqual(TEXT("modulo zero is safe"), Rng.Next(0), 0u);
	TestEqual(TEXT("modulo one is always zero"), Rng.Next(1), 0u);

	// Determinism: the same seed replays the same stream, which is what makes the shuffle
	// reproducible in a test at all.
	FRng A, B;
	A.Seed(999);
	B.Seed(999);
	bool bSame = true;
	for (int32 Index = 0; Index < 200; ++Index)
	{
		bSame &= (A.Next(1000) == B.Next(1000));
	}
	TestTrue(TEXT("same seed replays the same stream"), bSame);

	// Different seeds must not collapse onto the same stream.
	FRng C;
	C.Seed(1000);
	FRng D;
	D.Seed(1001);
	int32 Differences = 0;
	for (int32 Index = 0; Index < 200; ++Index)
	{
		Differences += (C.Next(1000) != D.Next(1000)) ? 1 : 0;
	}
	TestTrue(TEXT("different seeds diverge"), Differences > 150);

	// Spread: over many draws every bucket should be hit. A generator stuck on a lattice would
	// leave holes here, which is the failure mode worth guarding.
	FRng E;
	E.Seed(4242);
	int32 Buckets[10] = {};
	for (int32 Index = 0; Index < 20000; ++Index)
	{
		Buckets[E.Next(10)]++;
	}
	for (int32 Bucket = 0; Bucket < 10; ++Bucket)
	{
		TestTrue(
			*FString::Printf(TEXT("bucket %d is populated (%d)"), Bucket, Buckets[Bucket]),
			Buckets[Bucket] > 1000);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterRadioShuffleTest,
	"SimCopter.Radio.ShuffleBag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterRadioShuffleTest::RunTest(const FString& Parameters)
{
	FRng Rng;
	Rng.Seed(7);

	// A shuffle is a permutation: nothing gained, nothing lost. This is the property that makes
	// it a shuffle BAG - every track plays once per cycle before any repeats - as opposed to
	// drawing at random each time, which is what the radio is usually assumed to do.
	for (int32 Count = 1; Count <= 32; ++Count)
	{
		TArray<int32> Items;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Items.Add(Index);
		}
		USimCopterRadioSubsystem::ShuffleWithAntiRepeat(Items, Rng, INDEX_NONE);

		TestEqual(*FString::Printf(TEXT("count %d preserved"), Count), Items.Num(), Count);
		TArray<int32> Sorted = Items;
		Sorted.Sort();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Sorted[Index] != Index)
			{
				AddError(FString::Printf(TEXT("count %d is not a permutation"), Count));
				return false;
			}
		}
	}

	// SCHOOK: RadioShuffle 0x00430070's tail - if the new first element repeats whatever ended
	// the previous cycle, first and last are swapped. Without it the seam between cycles can
	// play the same category twice running.
	for (int32 Trial = 0; Trial < 200; ++Trial)
	{
		TArray<int32> Items = { 0, 1, 2, 3, 4, 5 };
		const int32 PreviousLast = 3;
		USimCopterRadioSubsystem::ShuffleWithAntiRepeat(Items, Rng, PreviousLast);
		if (Items[0] == PreviousLast)
		{
			AddError(FString::Printf(
				TEXT("trial %d left the repeated value first"), Trial));
			return false;
		}
	}

	// A single-element list cannot honour the rule and must not be corrupted trying.
	TArray<int32> One = { 4 };
	USimCopterRadioSubsystem::ShuffleWithAntiRepeat(One, Rng, 4);
	TestEqual(TEXT("single element survives"), One.Num(), 1);
	TestEqual(TEXT("single element unchanged"), One[0], 4);

	TArray<int32> Empty;
	USimCopterRadioSubsystem::ShuffleWithAntiRepeat(Empty, Rng, INDEX_NONE);
	TestEqual(TEXT("empty list stays empty"), Empty.Num(), 0);

	// The shuffle must actually shuffle - a no-op would satisfy every check above.
	int32 Moved = 0;
	for (int32 Trial = 0; Trial < 50; ++Trial)
	{
		TArray<int32> Items;
		for (int32 Index = 0; Index < 12; ++Index)
		{
			Items.Add(Index);
		}
		USimCopterRadioSubsystem::ShuffleWithAntiRepeat(Items, Rng, INDEX_NONE);
		for (int32 Index = 0; Index < 12; ++Index)
		{
			Moved += (Items[Index] != Index) ? 1 : 0;
		}
	}
	TestTrue(TEXT("shuffle displaces most elements"), Moved > 50 * 12 / 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterRadioScheduleTest,
	"SimCopter.Radio.Schedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterRadioScheduleTest::RunTest(const FString& Parameters)
{
	// FUN_0042f160's four branches, in the units the original wrote them: music unconditional,
	// then rand() % 100 against 0x14 / 0x5a / 0x14.
	TestEqual(TEXT("music always plays"),
		USimCopterRadioSubsystem::GetSlotChancePercent(ESimCopterRadioSlot::Music), 100);
	TestEqual(TEXT("dj is 20% (< 0x14)"),
		USimCopterRadioSubsystem::GetSlotChancePercent(ESimCopterRadioSlot::Dj), 0x14);
	TestEqual(TEXT("commercial is 90% (< 0x5a)"),
		USimCopterRadioSubsystem::GetSlotChancePercent(ESimCopterRadioSlot::Commercial), 0x5a);
	TestEqual(TEXT("jingle is 20% (< 0x14)"),
		USimCopterRadioSubsystem::GetSlotChancePercent(ESimCopterRadioSlot::Jingle), 0x14);

	// The gap is 4000 of the scheduler's timer units, which are milliseconds.
	TestEqual(TEXT("inter-item gap is four seconds"), USimCopterRadioSubsystem::GapSeconds, 4.0f);
	TestEqual(TEXT("back-to-back music is one in ten"),
		USimCopterRadioSubsystem::BackToBackMusicPercent, 10);

	// The slot codes are the pattern array's own values and must not be reordered: the loader
	// fills the four lists in this order (music, dj, commercl, jingle).
	TestEqual(TEXT("music is slot 0"), static_cast<int32>(ESimCopterRadioSlot::Music), 0);
	TestEqual(TEXT("dj is slot 1"), static_cast<int32>(ESimCopterRadioSlot::Dj), 1);
	TestEqual(TEXT("commercial is slot 2"), static_cast<int32>(ESimCopterRadioSlot::Commercial), 2);
	TestEqual(TEXT("jingle is slot 3"), static_cast<int32>(ESimCopterRadioSlot::Jingle), 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterRadioPossessionGateTest,
	"SimCopter.Radio.PossessionGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterRadioPossessionGateTest::RunTest(const FString& Parameters)
{
	USimCopterRadioSubsystem* Radio = NewObject<USimCopterRadioSubsystem>();
	if (!TestNotNull(TEXT("subsystem constructs"), Radio))
	{
		return false;
	}

	TestFalse(TEXT("radio starts outside the helicopter"), Radio->IsPlayerInHelicopter());
	TestTrue(TEXT("radio power setting still defaults on"), Radio->IsPowered());

	Radio->SetPlayerInHelicopter(true);
	TestTrue(TEXT("possession enables radio playback"), Radio->IsPlayerInHelicopter());

	Radio->SetPowered(false);
	Radio->SetPlayerInHelicopter(false);
	TestFalse(TEXT("leaving disables radio playback"), Radio->IsPlayerInHelicopter());
	Radio->SetPlayerInHelicopter(true);
	TestFalse(TEXT("re-entering preserves the player's off setting"), Radio->IsPowered());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterRadioDialTest,
	"SimCopter.Radio.Dial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterRadioDialTest::RunTest(const FString& Parameters)
{
	// The dial mapping is the remake's, so the thing worth pinning is that it round-trips: the
	// needle position a station produces must tune back to that same station when clicked.
	// Anything else and click-to-tune lands one station off at the ends.
	USimCopterRadioSubsystem* Radio = NewObject<USimCopterRadioSubsystem>();
	if (!TestNotNull(TEXT("subsystem constructs"), Radio))
	{
		return false;
	}

	// With no stations discovered the dial must be inert rather than divide by zero.
	TestEqual(TEXT("no stations gives alpha 0"), Radio->GetDialAlpha(), 0.0f);
	TestEqual(TEXT("no stations tunes to nothing"), Radio->GetStationForDialAlpha(0.5f), INDEX_NONE);

	// The dial spans the two outermost printed labels: alpha 0 is the first station and 1 the
	// last, evenly divided. Mirrors GetDialAlpha, which the subsystem cannot be asked for here
	// without a discovered station list.
	auto AlphaFor = [](int32 Index, int32 Count)
	{
		return Count == 1 ? 0.5f : static_cast<float>(Index) / static_cast<float>(Count - 1);
	};
	auto StationFor = [](float Alpha, int32 Count)
	{
		const float Position = FMath::Clamp(Alpha, 0.0f, 1.0f) * static_cast<float>(Count - 1);
		return FMath::Clamp(FMath::RoundToInt(Position), 0, Count - 1);
	};

	// The round-trip is what click-to-tune depends on: the needle position a station produces
	// must tune back to that station. Five is the shipped count; 1 and 2 are the edge cases.
	for (int32 Count : { 1, 2, 3, 5, 9 })
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Alpha = AlphaFor(Index, Count);
			TestTrue(
				*FString::Printf(TEXT("count %d station %d alpha is on the dial"), Count, Index),
				Alpha >= 0.0f && Alpha <= 1.0f);
			if (Count > 1)
			{
				TestEqual(
					*FString::Printf(TEXT("count %d station %d round-trips"), Count, Index),
					StationFor(Alpha, Count),
					Index);
			}
		}
		if (Count > 1)
		{
			TestEqual(*FString::Printf(TEXT("count %d: first is at 0"), Count), AlphaFor(0, Count), 0.0f);
			TestEqual(*FString::Printf(TEXT("count %d: last is at 1"), Count), AlphaFor(Count - 1, Count), 1.0f);
		}
	}

	// Past either end of the scale must clamp onto a real station, never run off the array.
	for (int32 Count : { 1, 2, 5 })
	{
		for (float Alpha : { -1.0f, 0.0f, 0.5f, 1.0f, 2.0f })
		{
			const int32 Station = StationFor(Alpha, Count);
			TestTrue(
				*FString::Printf(TEXT("count %d alpha %.1f clamps"), Count, Alpha),
				Station >= 0 && Station < Count);
		}
	}

	// Clicking the far left and far right must select the end stations, not the neighbours.
	TestEqual(TEXT("far left tunes the first station"), StationFor(0.0f, 5), 0);
	TestEqual(TEXT("far right tunes the last station"), StationFor(1.0f, 5), 4);
	TestEqual(TEXT("centre tunes the middle station"), StationFor(0.5f, 5), 2);

	return true;
}
