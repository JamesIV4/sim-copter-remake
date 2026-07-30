// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterCheckup.h"

#include "Flight/SimCopterFlightModel.h"

namespace
{
// The original tests the airport tile inside each max function and multiplies there.
int32 ApplyLocationSurcharge(const FSimCopterCheckupState& State, int32 Dollars)
{
	return State.bAtAirport ? Dollars : Dollars * FSimCopterCheckup::OffAirportPriceMultiplier;
}
}

int32 FSimCopterCheckup::GetDamageSliderMaxDollars(const FSimCopterCheckupState& State)
{
	// FUN_0048a380: Mul1616((maxHitPoints - hitPoints) << 16, dollarsPerHitPoint) >> 16.
	const int32 MissingHitPoints = State.MaxHitPoints - State.HitPoints;
	if (MissingHitPoints <= 0)
	{
		return 0;
	}

	const int32 Dollars =
		SimCopterFixed::Mul(MissingHitPoints * SimCopterFixed::One, State.DollarsPerHitPoint1616) >> 16;
	return FMath::Max(ApplyLocationSurcharge(State, Dollars), 0);
}

int32 FSimCopterCheckup::GetFuelSliderMaxDollars(const FSimCopterCheckupState& State)
{
	// FUN_0048a480. The subtraction is already 16.16 here - fuel is stored in gallons 16.16, so
	// unlike the damage path it is NOT shifted up first.
	const int32 MissingFuel1616 = State.FuelCapacity1616 - State.Fuel1616;
	if (MissingFuel1616 <= 0)
	{
		return 0;
	}

	const int32 Dollars = SimCopterFixed::Mul(MissingFuel1616, State.DollarsPerGallon1616) >> 16;
	return FMath::Max(ApplyLocationSurcharge(State, Dollars), 0);
}

int32 FSimCopterCheckup::GetTearGasSliderMaxRounds(const FSimCopterCheckupState& State)
{
	// FUN_00444690 only enables the slider when the launcher is fitted (tools[0x48] & 8), with
	// FUN_0048a560's flat cap of ten minus what is already aboard.
	if (!State.bTearGasFitted)
	{
		return 0;
	}

	return FMath::Max(MaxTearGasRounds - State.TearGasRounds, 0);
}

int32 FSimCopterCheckup::GetTotalCostDollars(const FSimCopterCheckupOrder& Order)
{
	// FUN_004447e0 sums the two dollar sliders and the converted tear-gas slider.
	return Order.DamageDollars + Order.FuelDollars + GetTearGasCostDollars(Order.TearGasRounds);
}

FSimCopterCheckupOrder FSimCopterCheckup::ClampOrder(
	const FSimCopterCheckupState& State,
	FSimCopterCheckupOrder Order)
{
	Order.DamageDollars = FMath::Clamp(Order.DamageDollars, 0, GetDamageSliderMaxDollars(State));
	Order.FuelDollars = FMath::Clamp(Order.FuelDollars, 0, GetFuelSliderMaxDollars(State));
	Order.TearGasRounds = FMath::Clamp(Order.TearGasRounds, 0, GetTearGasSliderMaxRounds(State));

	// Spend down the available funds in FUN_004385c0's order: damage, fuel, then tear gas.
	int32 Remaining = FMath::Max(State.Funds, 0);
	Order.DamageDollars = FMath::Min(Order.DamageDollars, Remaining);
	Remaining -= Order.DamageDollars;
	Order.FuelDollars = FMath::Min(Order.FuelDollars, Remaining);
	Remaining -= Order.FuelDollars;
	Order.TearGasRounds = FMath::Min(Order.TearGasRounds, Remaining / TearGasDollarsPerRound);

	return Order;
}

int32 FSimCopterCheckup::ApplyDamageRepair(const FSimCopterCheckupState& State, int32 Dollars)
{
	// FUN_0048a3e0. The off-airport surcharge added to the slider ceiling is taken back out here,
	// so a dollar spent at a remote pad buys a third of the repair it would at the airport.
	if (!State.bAtAirport)
	{
		Dollars /= OffAirportPriceMultiplier;
	}

	const int32 GainedHitPoints =
		SimCopterFixed::Div(Dollars * SimCopterFixed::One, State.DollarsPerHitPoint1616) >> 16;
	int32 HitPoints = State.HitPoints + GainedHitPoints;
	if (HitPoints > State.MaxHitPoints)
	{
		HitPoints = State.MaxHitPoints;
	}
	if (HitPoints < MinimumHitPoints)
	{
		HitPoints = 0;
	}

	return HitPoints;
}

int32 FSimCopterCheckup::ApplyFuel(const FSimCopterCheckupState& State, int32 Dollars)
{
	// FUN_0048a4e0 - deliberately no off-airport division; see the header note.
	const int32 GainedFuel1616 =
		SimCopterFixed::Div(Dollars * SimCopterFixed::One, State.DollarsPerGallon1616);
	int32 Fuel1616 = State.Fuel1616 + GainedFuel1616;
	if (Fuel1616 > State.FuelCapacity1616)
	{
		Fuel1616 = State.FuelCapacity1616;
	}

	return Fuel1616;
}

int32 FSimCopterCheckup::ApplyTearGas(int32 Dollars, int32 CurrentRounds)
{
	// FUN_0048b130. The original does not clamp here - the slider's range already did.
	return CurrentRounds + Dollars / TearGasDollarsPerRound;
}

bool FSimCopterCheckup::ShouldOffer(const FSimCopterCheckupState& State)
{
	// FUN_00444750, minus its view-mode gate: the caller decides whether the player is in a state
	// where a panel can be opened at all.
	if (!State.bAtAirport)
	{
		return false;
	}

	if (GetDamageSliderMaxDollars(State) >= OfferCostThresholdDollars ||
		GetFuelSliderMaxDollars(State) >= OfferCostThresholdDollars)
	{
		return true;
	}

	return State.bTearGasFitted && State.TearGasRounds < OfferTearGasThreshold;
}
