// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// SCHOOK: CheckupDialog 0x00443c20
//
// The original's landing-pad service menu - title string 590, "Check-up". Three sliders (Damage,
// Fuel, Teargas), a running Total Cost against the player's Funds, and OK/Cancel. Everything in
// this header is transcribed from the decompile; the working notes are in
// `Docs/scratchpad/checkup-menu-DECODED.md`, which cites each function.
//
// Two things about it are easy to get wrong:
//
//  * The Damage and Fuel sliders are denominated in DOLLARS, not in hit points or gallons. Their
//    maxima are the cost of a full repair / full tank, and the appliers convert dollars back into
//    hit points and gallons. Only the Teargas slider counts units (canisters, at $50 each).
//
//  * `heli[0x34]` is HIT POINTS, not accumulated damage - it starts at MaxDamage and falls. The
//    cost to repair is (MaxDamage - HitPoints) * dollarsPerPoint, and repairing ADDS hit points.
//
// Servicing away from the airport costs triple (FUN_0048a380 / FUN_0048a480).

// Everything the menu needs to price itself, pulled off the pawn so the arithmetic stays testable.
// Fixed-point fields are 16.16, matching the original's fields directly.
struct SIMCOPTERREMAKE_API FSimCopterCheckupState
{
	// heli[0x34] and its per-type maximum (DAT_0050412c).
	int32 HitPoints = 0;
	int32 MaxHitPoints = 0;
	// heli.twk "Repair Rate" (DAT_00504130): dollars per hit point, 16.16.
	int32 DollarsPerHitPoint1616 = 0;

	// heli[0xcc] and the tank capacity (DAT_00504120), both 16.16 gallons.
	int32 Fuel1616 = 0;
	int32 FuelCapacity1616 = 0;
	// heli.twk "Fuel Cost" (DAT_00504134): dollars per gallon, 16.16.
	int32 DollarsPerGallon1616 = 0;

	// tools[0x48] & 8 and tools[0x54].
	bool bTearGasFitted = false;
	int32 TearGasRounds = 0;

	// FUN_004823a0(heli.x, heli.y, 0xf6, 2): standing on an airport tile.
	bool bAtAirport = false;

	int32 Funds = 0;
};

// What the player dialled in on the three sliders. Damage/Fuel are dollars, TearGas is canisters.
struct SIMCOPTERREMAKE_API FSimCopterCheckupOrder
{
	int32 DamageDollars = 0;
	int32 FuelDollars = 0;
	int32 TearGasRounds = 0;
};

class SIMCOPTERREMAKE_API FSimCopterCheckup
{
public:
	// FUN_0048a380 / FUN_0048a480: the slider maxima are tripled anywhere but the airport.
	static constexpr int32 OffAirportPriceMultiplier = 3;

	// FUN_0048a570: n * 0x32.
	static constexpr int32 TearGasDollarsPerRound = 50;

	// FUN_0048a560: a flat cap of ten canisters.
	static constexpr int32 MaxTearGasRounds = 10;

	// FUN_0048a3e0's final clamp: a repair that leaves fewer than four hit points zeroes them.
	static constexpr int32 MinimumHitPoints = 4;

	// FUN_00444750's thresholds: $21 of work, or fewer than five canisters aboard.
	static constexpr int32 OfferCostThresholdDollars = 0x15;
	static constexpr int32 OfferTearGasThreshold = 5;

	// --- slider ranges (FUN_00444690) ---

	static int32 GetDamageSliderMaxDollars(const FSimCopterCheckupState& State);
	static int32 GetFuelSliderMaxDollars(const FSimCopterCheckupState& State);
	static int32 GetTearGasSliderMaxRounds(const FSimCopterCheckupState& State);

	// --- totals (FUN_00444640 / FUN_004447e0) ---

	static int32 GetTearGasCostDollars(int32 Rounds) { return Rounds * TearGasDollarsPerRound; }
	static int32 GetTotalCostDollars(const FSimCopterCheckupOrder& Order);

	// Clamps an order to the slider maxima and to what the player can actually pay for, in the
	// original's own order (damage, then fuel, then tear gas). The original cannot overspend
	// because the sliders are bounded and the total is shown live; the remake clamps explicitly so
	// a programmatic order can never drive funds negative.
	static FSimCopterCheckupOrder ClampOrder(const FSimCopterCheckupState& State, FSimCopterCheckupOrder Order);

	// --- appliers ---

	// FUN_0048a3e0. Returns the new hit-point total.
	static int32 ApplyDamageRepair(const FSimCopterCheckupState& State, int32 Dollars);

	// FUN_0048a4e0. Returns the new 16.16 fuel level.
	//
	// Note the asymmetry with the repair path: this one has NO off-airport division, so a dollar
	// buys the same gallon wherever you are even though the slider's ceiling was tripled. That is
	// the original's behaviour, not a porting slip - off-airport the extra range is simply unused.
	static int32 ApplyFuel(const FSimCopterCheckupState& State, int32 Dollars);

	// FUN_0048b130: dollars / 50 canisters added to the career record.
	static int32 ApplyTearGas(int32 Dollars, int32 CurrentRounds);

	// --- when the menu opens (FUN_00444750) ---

	// True when the original would put the Check-up panel up: standing on the airport with at
	// least $21 of repair or fuel outstanding, or with the launcher fitted and fewer than five
	// canisters left.
	static bool ShouldOffer(const FSimCopterCheckupState& State);
};
