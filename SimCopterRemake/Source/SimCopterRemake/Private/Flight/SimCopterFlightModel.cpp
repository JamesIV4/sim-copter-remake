// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterFlightModel.h"

namespace SimCopterFixed
{
namespace
{
// Quarter-wave sine table matching the original's layout at 0x0046b530:
// 901 16.16 entries for 0.0 .. 90.0 degrees in tenth-degree steps. The
// original ships the table as data; regenerating it from sinf reproduces the
// same values to within one LSB, which is below the simulation's noise floor.
struct FQuarterSineTable
{
	int32 Values[901];

	FQuarterSineTable()
	{
		for (int32 Index = 0; Index <= 900; ++Index)
		{
			const double Radians = (static_cast<double>(Index) / 10.0) * PI / 180.0;
			Values[Index] = static_cast<int32>(FMath::RoundToInt64(FMath::Sin(Radians) * 65536.0));
		}
	}
};

const FQuarterSineTable& QuarterSine()
{
	static const FQuarterSineTable Table;
	return Table;
}
}

int32 WrapAngle(int32 AngleTenthDeg)
{
	// FUN_0046c4dc wraps by repeated add/subtract of the full turn.
	while (AngleTenthDeg < 0)
	{
		AngleTenthDeg += FullTurnTenthDeg;
	}
	while (AngleTenthDeg > FullTurnTenthDeg)
	{
		AngleTenthDeg -= FullTurnTenthDeg;
	}
	return AngleTenthDeg;
}

void SinCos(int32 AngleTenthDeg, int32& OutSin, int32& OutCos)
{
	// FUN_0046c4dc: quadrant fold over the 901-entry quarter table. The index
	// is the integer part of the angle in tenth-degrees.
	const FQuarterSineTable& Table = QuarterSine();
	uint32 Index = static_cast<uint32>(WrapAngle(AngleTenthDeg)) >> 16;
	int32 Sin;
	int32 Cos;
	if (Index < 0x709)
	{
		if (Index < 0x385)
		{
			Sin = Table.Values[Index];
			Cos = Table.Values[900 - Index];
		}
		else
		{
			Sin = Table.Values[0x708 - Index];
			Cos = -Table.Values[900 - (0x708 - Index)];
		}
	}
	else
	{
		Index = 0xe10 - Index;
		if (Index < 0x385)
		{
			Sin = -Table.Values[Index];
			Cos = Table.Values[900 - Index];
		}
		else
		{
			Sin = -Table.Values[0x708 - Index];
			Cos = -Table.Values[900 - (0x708 - Index)];
		}
	}
	OutSin = Sin;
	OutCos = Cos;
}
}

namespace
{
using namespace SimCopterFixed;

// The original clamps the frame delta to at least 0x0ccc (0.05 s) when
// computing the attitude smoothing window, capping the effective frame rate at
// 20 fps so the lag feel does not tighten on fast machines.
constexpr int32 MinSmoothingDt = 0x0ccc;

// FUN_00486e90: slide velocity weight (32000/65536 = 0.488) and the
// units-per-second scale for position integration (40000/65536 = 0.610).
constexpr int32 SlideVelocityScale = 32000;
constexpr int32 PositionScale = 40000;

// FUN_00486a30: bank is never allowed to exceed |pitch| + 30 degrees.
constexpr int32 BankOverPitchAllowance = 0x12c0000; // 300.0 tenth-deg

// FUN_00486a30: ground-proximity band that adds up to MaxPitch/8 of extra
// pitch authority in the lowest 150 units.
constexpr int32 GroundPitchBand = 0x960000; // 150.0 units

int32 SignedKick(int32 Random)
{
	// Master tick bounce kicks: (1 - rand() % 3) in {-1, 0, 1}.
	return 1 - (Random % 3);
}
}

int32 FSimCopterFlightModel::NextRand()
{
	// MSVC rand(): the exe links the CRT's LCG.
	RandState = RandState * 214013u + 2531011u;
	return static_cast<int32>((RandState >> 16) & 0x7fff);
}

void FSimCopterFlightModel::ResetOnSurface(int32 InPosX, int32 InPosZ, int32 SurfaceHeight)
{
	State = ESimCopterFlightState::Parked;
	PosX = InPosX;
	PosZ = InPosZ;
	Altitude = SurfaceHeight + 0x13333; // landing settle offset (1.2 units)
	PitchTarget = PitchSmoothed = 0;
	BankTarget = BankSmoothed = 0;
	SlideTarget = SlideSmoothed = 0;
	YawRateTarget = YawRateSmoothed = 0;
	ForwardSpeed = 0;
	ClimbSpeed = 0;
	SpeedDelta = 0;
	HorizontalSpeed = 0;
	VelX = VelZ = 0;
	DeltaX = DeltaZ = 0;
	RotorSpeed = 0;
	bRotorBlurDisc = false;
	BounceTimer = 0;
	DyingTimer = 0;
	HitPoints = Tuning.MaxDamage;
	Fuel = Tuning.FuelGallons;
	FlightSeconds = 0;
	FMemory::Memzero(TurbPitchSamples, sizeof(TurbPitchSamples));
	FMemory::Memzero(TurbSlideSamples, sizeof(TurbSlideSamples));
	FMemory::Memzero(TurbYawSamples, sizeof(TurbYawSamples));
	TurbPitch = TurbSlide = TurbYaw = 0;
}

void FSimCopterFlightModel::Step(float DeltaSeconds, const FSimCopterFlightInputs& Inputs, const FSimCopterFlightEnvironment& Env, FSimCopterFlightEvents& OutEvents)
{
	OutEvents = FSimCopterFlightEvents();

	const int32 Dt = FMath::Clamp(FromFloat(DeltaSeconds), 1, One);

	// Master tick preamble (FUN_00484d20): load factor from the weight budget
	// of seats*120 lb + max cargo + 30 lb versus what is actually aboard.
	const int32 CapacityPounds = Tuning.PassengerSeats * 120 + Tuning.MaxLoadPounds + 30;
	const int32 CarriedPounds = Passengers * 120 + LoadPounds;
	LoadFactor = Div((CapacityPounds - CarriedPounds) * One, CapacityPounds * One);

	if (State == ESimCopterFlightState::Dead)
	{
		return;
	}

	if (State == ESimCopterFlightState::Dying)
	{
		// FUN_00486a30/FUN_00486e90 state 5: the wreck tumbles and falls until
		// it reaches the ground (the original drives this from a spawned crash
		// effect; the remake integrates an equivalent spiral directly).
		Heading = WrapAngle(Heading + Mul((NextRand() % 900 + 900) * One, Dt));
		PitchTarget = FMath::Min(PitchTarget + Mul(0x1900000, Dt), Tuning.MaxPitch * 2);
		BankSmoothed = WrapAngle(BankSmoothed + Mul(0xc80000, Dt)) % 0x1680000;
		ClimbSpeed = -Tuning.MaxDescentRate * 4;
		Altitude += Mul(ClimbSpeed, Dt);
		RotorSpeed = FMath::Max(0, RotorSpeed - Mul(0xc80000, Dt)); // -200/s (FUN_00487740 state 5)
		if (Altitude <= Env.SurfaceHeight)
		{
			Altitude = Env.SurfaceHeight;
			State = ESimCopterFlightState::Dead;
			OutEvents.bCrashed = true;
		}
		return;
	}

	// FUN_00485f50 skips all control input while the bounce timer runs.
	if (BounceTimer <= 0)
	{
		StepControls(Dt, Inputs);
	}
	StepTurbulence(Env, OutEvents);
	StepAttitude(Dt, Env);
	StepVelocity(Dt, Inputs);
	StepVertical(Dt, Inputs, Env, OutEvents);
	StepRotor(Dt, Inputs);
	StepFuelAndDamage(Dt, OutEvents);
	StepGroundImpact(Dt, Env, OutEvents);
}

void FSimCopterFlightModel::StepControls(int32 Dt, const FSimCopterFlightInputs& Inputs)
{
	// FUN_00485f50. Keyboard controls RAMP the attitude targets (classic
	// SimCopter trim feel); analog axes SEEK a deflection-proportional target.
	// All ramp rates scale with the load factor - a heavy helicopter is slower
	// on the controls.

	// Pitch and slide keys both ramp at the SlideRate control (the PitchRate
	// control only shapes the attitude lag below). Confirmed against the
	// decompile: FUN_00485f50 reads DAT_00504108 (Ctrl6) for both.
	const int32 PitchRampRate = Mul(Tuning.SlideRate, LoadFactor);

	if (Inputs.bPitchForwardKey)
	{
		PitchTarget += Mul(Dt, PitchRampRate);
	}
	else if (Inputs.bPitchBackKey)
	{
		PitchTarget -= Mul(Dt, PitchRampRate);
	}
	else if (Inputs.PitchAxis != 0)
	{
		// Joystick: target = -axis% * 3.0 tenth-deg, approached at 2/s * load.
		const int32 Target = Mul(Inputs.PitchAxis * -One, 0x30000);
		PitchTarget += Mul(Mul(Target - PitchTarget, Dt * 2), LoadFactor);
	}
	else
	{
		// No input: decay toward level at (1 - 2*dt) per frame.
		PitchTarget = Mul(PitchTarget, (0x8000 - Dt) * 2);
	}

	// Coordinated turn: bank and yaw-rate move together.
	const int32 YawRampRate = Mul(Tuning.YawRate, LoadFactor);
	if (Inputs.bTurnRightKey && !Inputs.bSlideModifier)
	{
		BankTarget -= Mul(Dt, Tuning.RollRate);
		YawRateTarget += Mul(Dt, YawRampRate);
	}
	else if (Inputs.bTurnLeftKey && !Inputs.bSlideModifier)
	{
		BankTarget += Mul(Dt, Tuning.RollRate);
		YawRateTarget -= Mul(Dt, YawRampRate);
	}
	else if (Inputs.TurnAxis != 0)
	{
		// Joystick: bank seeks -axis% * 6.0; yaw rate = MaxYawRate * axis% / 100.
		const int32 BankSeek = Mul(Inputs.TurnAxis * -One, 0x60000);
		BankTarget += Mul(BankSeek - BankTarget, Dt);
		const int32 AxisYawRate = static_cast<int32>(static_cast<int64>(Tuning.MaxYawRate) * Inputs.TurnAxis / 100);
		YawRateTarget = Mul(AxisYawRate, LoadFactor);
	}
	else
	{
		// Decay both at (1 - 4*dt) per frame.
		const int32 Decay = (0x4000 - Dt) * 4;
		BankTarget = Mul(BankTarget, Decay);
		YawRateTarget = Mul(YawRateTarget, Decay);
	}

	// Slide: dedicated keys, or the turn keys while the modifier is held.
	const int32 SlideRampRate = Mul(Tuning.SlideRate, LoadFactor);
	const bool bSlideRight = Inputs.bSlideRightKey || (Inputs.bSlideModifier && Inputs.bTurnRightKey);
	const bool bSlideLeft = Inputs.bSlideLeftKey || (Inputs.bSlideModifier && Inputs.bTurnLeftKey);
	if (bSlideRight)
	{
		SlideTarget -= Mul(Dt, SlideRampRate);
	}
	else if (bSlideLeft)
	{
		SlideTarget += Mul(Dt, SlideRampRate);
	}
	else if (Inputs.SlideAxis != 0)
	{
		// Joystick: target = -axis% * 2.0, approached at 1/s * load.
		const int32 Target = Mul(Inputs.SlideAxis * -One, 0x20000);
		SlideTarget += Mul(Mul(Target - SlideTarget, Dt), LoadFactor);
	}
	else
	{
		SlideTarget = Mul(SlideTarget, (0x8000 - Dt) * 2);
	}

	// Out of fuel forces the collective down.
	// (ClimbCommand itself is consumed by StepVertical.)
}

void FSimCopterFlightModel::StepTurbulence(const FSimCopterFlightEnvironment& Env, FSimCopterFlightEvents& OutEvents)
{
	// FUN_00489800: every frame push one random kick per axis into a 9-sample
	// ring buffer and expose the running averages; the attitude integrator adds
	// them to the targets. Amplitude 3 when healthy and clear of fires,
	// otherwise it grows with accumulated damage and fire proximity.
	const int32 FireDelta = Env.FireHeightDelta;

	int32 Amplitude;
	if (HitPoints == Tuning.MaxDamage && (FireDelta == 0 || FireDelta > 0xfa0000))
	{
		Amplitude = 3;
	}
	else
	{
		Amplitude = 0;
		if (FireDelta != 0)
		{
			// Flying inside the fire band burns hit points...
			if (Tuning.MinFireAlt <= FireDelta && FireDelta <= Tuning.MaxFireAlt)
			{
				ApplyDamage((Tuning.MaxFireAlt >> 16) - (FireDelta >> 16), OutEvents);
			}
			// ...and anything within 250 units shakes harder the closer it is.
			if (FireDelta >= -0x300000 && FireDelta <= 0xfa0000)
			{
				Amplitude += 0xfa - (FireDelta >> 16);
			}
		}
		Amplitude += (Tuning.MaxDamage - HitPoints) / 0x14;
		Amplitude = FMath::Max(Amplitude, 1);
	}

	auto PushSample = [this, Amplitude](int32* Samples) -> int32
	{
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Samples[Index] = Samples[Index + 1];
		}
		int32 Value = NextRand() % Amplitude;
		if (NextRand() & 1)
		{
			Value = -Value;
		}
		Samples[8] = Value << 16;
		// The original averages slots 1..9 of a 10-slot window; with the shift
		// above that is the 9 stored samples.
		int32 Sum = 0;
		for (int32 Index = 0; Index < 9; ++Index)
		{
			Sum += Samples[Index];
		}
		return Sum / 9;
	};

	TurbSlide = PushSample(TurbSlideSamples);
	TurbYaw = PushSample(TurbYawSamples);
	TurbPitch = PushSample(TurbPitchSamples);
}

int32 FSimCopterFlightModel::SmoothingFrames(int32 /*RateTenthDeg*/, int32 Dt) const
{
	// FUN_00486a30: N = ((1000.0 - PitchRate) / 500) * fps, fps capped at 20 by
	// the 0.05 s minimum delta. All four axes share the PitchRate-derived N.
	int32 ClampedDt = FMath::Max(Dt, MinSmoothingDt);
	const int32 Fps = Div(One, ClampedDt);
	int32 Frames = (static_cast<int64>(0x3e80000 - Tuning.PitchRate) / 500) * (Fps >> 16) >> 16;
	return FMath::Max(Frames, 1);
}

void FSimCopterFlightModel::StepAttitude(int32 Dt, const FSimCopterFlightEnvironment& Env)
{
	// FUN_00486a30 (state 0 zeroes everything; states 5/6 handled in Step).
	if (State == ESimCopterFlightState::Parked)
	{
		PitchTarget = 0;
		BankTarget = 0;
		YawRateTarget = 0;
		SlideTarget = 0;
		return;
	}

	// Ground-bounce pitch derating: while ClimbSpeed carries a bounce impulse
	// the pitch clamp shrinks (down to half) so the helicopter cannot dig in.
	int32 PitchClamp = Tuning.MaxPitch;
	if (ClimbSpeed > 0)
	{
		int32 Fraction = Div(Tuning.ClimbRate * 4 - ClimbSpeed, Tuning.ClimbRate * 4);
		Fraction = FMath::Max(Fraction, 0x8000);
		PitchClamp = Mul(Fraction, Tuning.MaxPitch);
	}

	// Turbulence feeds straight into the targets (player helicopters only).
	PitchTarget += TurbPitch;

	// Within 150 units of the ground a proximity bonus of up to MaxPitch/8
	// loosens the clamp as height increases.
	int32 Bonus = 0;
	const int32 Proximity = (Env.TerrainHeight - Altitude) + GroundPitchBand;
	if (Proximity > 0 && Proximity < GroundPitchBand)
	{
		Bonus = Mul(PitchClamp >> 3, Div(GroundPitchBand - Proximity, GroundPitchBand));
	}
	const int32 PitchLimit = PitchClamp + Bonus; // easy model: (PitchClamp + Bonus) / 2
	PitchTarget = FMath::Clamp(PitchTarget, -PitchLimit, PitchLimit);

	// First-order lag shared by all four smoothed values.
	const int32 Frames = SmoothingFrames(Tuning.PitchRate, Dt);
	PitchSmoothed = (static_cast<int64>(PitchSmoothed) * (Frames - 1) + PitchTarget) / Frames;

	// Bank: clamp to MaxBank, then to |smoothed pitch| + 30 degrees.
	BankTarget = FMath::Clamp(BankTarget, -Tuning.MaxBank, Tuning.MaxBank);
	const int32 BankLimit = FMath::Abs(PitchSmoothed) + BankOverPitchAllowance;
	BankTarget = FMath::Clamp(BankTarget, -BankLimit, BankLimit);
	BankSmoothed = (static_cast<int64>(BankSmoothed) * (Frames - 1) + BankTarget) / Frames;

	// Slide.
	SlideTarget += TurbSlide;
	SlideTarget = FMath::Clamp(SlideTarget, -Tuning.MaxSlide, Tuning.MaxSlide);
	SlideSmoothed = (static_cast<int64>(SlideSmoothed) * (Frames - 1) + SlideTarget) / Frames;

	// Yaw rate.
	YawRateTarget += TurbYaw;
	YawRateTarget = FMath::Clamp(YawRateTarget, -Tuning.MaxYawRate, Tuning.MaxYawRate);
	YawRateSmoothed = (static_cast<int64>(YawRateSmoothed) * (Frames - 1) + YawRateTarget) / Frames;

	// Heading integrates the smoothed yaw rate at x15 (tenth-deg per second).
	Heading = WrapAngle(Heading + Mul(YawRateSmoothed, Mul(Dt, 0xf0000)));

	// Display quirk: when sliding harder than banking, the bank state itself
	// inherits the slide so the fuselage leans into the slide.
	if (FMath::Abs(BankSmoothed) < FMath::Abs(SlideSmoothed))
	{
		BankSmoothed = SlideSmoothed;
	}
}

void FSimCopterFlightModel::StepVelocity(int32 Dt, const FSimCopterFlightInputs& Inputs)
{
	// FUN_00486e90. The forward speed chases the smoothed pitch angle: the
	// original literally uses tenth-degrees of pitch as units/s of airspeed.
	const int32 OldSpeed = ForwardSpeed;

	if (BounceTimer > 0)
	{
		BounceTimer -= Dt;
		ForwardSpeed = PitchTarget >> 3;
	}
	else
	{
		const int32 Target = PitchSmoothed; // easy model: PitchSmoothed * 2
		if (ForwardSpeed < Target)
		{
			ForwardSpeed += (Target - ForwardSpeed) >> 5;
		}
		if (ForwardSpeed > Target)
		{
			ForwardSpeed -= (ForwardSpeed - Target) >> 5;
		}
	}

	if (State == ESimCopterFlightState::Parked)
	{
		ForwardSpeed = 0;
		SlideSmoothed = 0;
		YawRateSmoothed = 0;
		BankSmoothed = 0;
	}

	SpeedDelta = ForwardSpeed - OldSpeed;

	int32 Speed = ForwardSpeed;
	if (Inputs.bTurbo)
	{
		Speed = Mul(Speed, 0xa0000);
	}

	// Forward vector from the heading (the original re-derives a unit XZ
	// vector via atan2 after transforming local +Z by the orientation matrix).
	int32 FwdX;
	int32 FwdZ;
	SinCos(Heading, FwdX, FwdZ);

	const int32 SlideVel = Mul(SlideSmoothed, SlideVelocityScale);
	VelX = Mul(FwdX, Speed) - Mul(FwdZ, SlideVel);
	VelZ = Mul(FwdZ, Speed) + Mul(FwdX, SlideVel);

	// FUN_004679d0 computes the magnitude through floating point.
	const float VelXf = ToFloat(VelX);
	const float VelZf = ToFloat(VelZ);
	HorizontalSpeed = FromFloat(FMath::Sqrt(VelXf * VelXf + VelZf * VelZf));

	const int32 Scale = Mul(Dt, PositionScale);
	DeltaX = Mul(VelX, Scale);
	DeltaZ = Mul(VelZ, Scale);
	PosX += DeltaX;
	PosZ += DeltaZ;
}

void FSimCopterFlightModel::StepVertical(int32 Dt, const FSimCopterFlightInputs& Inputs, const FSimCopterFlightEnvironment& Env, FSimCopterFlightEvents& OutEvents)
{
	// FUN_00487160.
	const bool bOutOfFuel = Fuel < 1;
	int32 ClimbCommand = Inputs.ClimbCommand;
	if (bOutOfFuel)
	{
		ClimbCommand = -1; // FUN_00485f50 forces descend with no fuel
	}

	if (ClimbCommand < 0)
	{
		// Descend: ramp at 2x MaxDescentRate per second, floor -4x.
		ClimbSpeed -= Mul(Tuning.MaxDescentRate, Dt * 2);
		ClimbSpeed = FMath::Max(ClimbSpeed, Tuning.MaxDescentRate * -4);
		// Autorotation: with no fuel, trading forward speed arrests the fall.
		if (bOutOfFuel && ForwardSpeed > 0 && SpeedDelta < 0)
		{
			ClimbSpeed = FMath::Min(ClimbSpeed + SpeedDelta * -2, 0);
		}
		if (State == ESimCopterFlightState::Parked)
		{
			ClimbSpeed = 0;
		}
	}
	else if (ClimbCommand == 0)
	{
		// Neutral: per-frame exponential decay, 5% up / 10% down.
		if (ClimbSpeed > 0)
		{
			ClimbSpeed -= Mul(0x0ccc, ClimbSpeed);
			ClimbSpeed = FMath::Max(ClimbSpeed, 0);
		}
		else if (ClimbSpeed < 0)
		{
			ClimbSpeed -= Mul(0x1999, ClimbSpeed);
			ClimbSpeed = FMath::Min(ClimbSpeed, 0);
		}
		// Resting on a surface with lifting rotor pushes up at 4x ClimbRate
		// (this is what makes an idle-collective helicopter hop off a slam).
		if ((Altitude <= Env.SurfaceHeight || Altitude <= Env.TerrainHeight) && Env.bTerrainFlat)
		{
			ClimbSpeed = Tuning.ClimbRate * 4;
		}
		if (State == ESimCopterFlightState::Parked)
		{
			ClimbSpeed = 0;
		}
		if (Altitude - Env.TerrainHeight > CeilingAboveTerrain)
		{
			ClimbSpeed = -Tuning.MaxDescentRate; // above the ceiling: sink
		}
	}
	else
	{
		// Climb: cap at 4x ClimbRate scaled by the load factor.
		const int32 ClimbCap = Mul(Tuning.ClimbRate * 4, LoadFactor);
		ClimbSpeed = FMath::Min(ClimbSpeed, ClimbCap);

		if (RotorSpeed < RotorLiftGate)
		{
			// Rotor still spooling: no lift yet (about three seconds at 100/s).
			RotorSpeed += Mul(0x640000, Dt);
			ClimbSpeed = 0;
		}
		else if (Altitude - Env.TerrainHeight <= CeilingAboveTerrain)
		{
			ClimbSpeed += Mul(Tuning.ClimbRate, Dt * 2);
			ClimbSpeed = FMath::Min(ClimbSpeed, ClimbCap);
		}
		else
		{
			ClimbSpeed = -Tuning.MaxDescentRate; // ceiling
		}
	}

	Altitude += Mul(ClimbSpeed, Dt);
	AboveGround = Altitude - Env.TerrainHeight;

	// State transitions.
	if (State == ESimCopterFlightState::Parked)
	{
		if (ClimbCommand > 0 && RotorSpeed > RotorLiftGate)
		{
			PitchSmoothed = 0;
			SlideSmoothed = 0;
			BankSmoothed = 0;
			YawRateSmoothed = 0;
			State = ESimCopterFlightState::Flying;
			OutEvents.bLiftedOff = true;
		}
		return;
	}

	// Landing (both on flat terrain and on an elevated pad). The original
	// compares the *targets*: pitch, climb speed, slide and forward speed all
	// inside the Heli Landing limits, with the flat-terrain flag required.
	const bool bAttitudeOk =
		FMath::Abs(PitchTarget) <= Tuning.LandMaxPitch &&
		FMath::Abs(ClimbSpeed) <= Tuning.LandMaxYSpeed &&
		FMath::Abs(SlideTarget) <= Tuning.LandMaxSlide &&
		FMath::Abs(ForwardSpeed) <= Tuning.LandMaxSpeed;

	if (Env.bTerrainFlat && !Env.bHostileSurface && bAttitudeOk && ClimbCommand <= 0)
	{
		const bool bOnTerrain = Altitude < Env.TerrainHeight + One;
		const bool bOnPad = Altitude < Env.SurfaceHeight + One && Altitude > Env.SurfaceHeight - One && BounceTimer <= 0;
		if (bOnTerrain || bOnPad)
		{
			Altitude = (bOnTerrain ? Env.TerrainHeight : Env.SurfaceHeight) + 0x13333;
			State = ESimCopterFlightState::Parked;
			ClimbSpeed = 0;
			OutEvents.bTouchedDown = true;
		}
	}
}

void FSimCopterFlightModel::StepRotor(int32 Dt, const FSimCopterFlightInputs& Inputs)
{
	// FUN_00487740.
	if (State == ESimCopterFlightState::Parked)
	{
		if (Inputs.ClimbCommand < 1)
		{
			RotorSpeed -= Mul(0x320000, Dt); // spool down at 50/s
		}
	}
	else if (RotorSpeed < RotorTopSpeed)
	{
		RotorSpeed += Mul(0x640000, Dt); // spool to 360 at 100/s in flight
	}
	RotorSpeed = FMath::Max(RotorSpeed, 0);

	// Blade step: proportional while slow, then a constant 39.1 degrees per
	// frame - the original's deliberate blur strobe.
	int32 BladeStep = 0x1870000; // 391.0 tenth-deg
	if (RotorSpeed <= 0xfa0000)
	{
		BladeStep = Mul(RotorSpeed, Dt << 5);
	}
	MainRotorAngle = WrapAngle(MainRotorAngle + BladeStep);
	TailRotorAngle = WrapAngle(TailRotorAngle + BladeStep);

	// Face-type-11 blur faces toggle on at lift RPM.
	bRotorBlurDisc = RotorSpeed >= RotorLiftGate;
}

void FSimCopterFlightModel::StepFuelAndDamage(int32 Dt, FSimCopterFlightEvents& OutEvents)
{
	// Master tick, state 1: fuel burn = FuelRate * dt * (17/65536) which works
	// out to gallons-per-hour within ~7%; plus the flight timer.
	if (State == ESimCopterFlightState::Flying)
	{
		if (Fuel > 0)
		{
			Fuel = FMath::Max(0, Fuel - Mul(0x11, Mul(Tuning.FuelRateGalPerHour, Dt)));
		}
		FlightSeconds += Dt;
	}

	if (HitPoints < 0 && State != ESimCopterFlightState::Dying)
	{
		State = ESimCopterFlightState::Dying;
		OutEvents.bStartedDying = true;
	}
}

void FSimCopterFlightModel::ApplyDamage(int32 Amount, FSimCopterFlightEvents& OutEvents)
{
	if (Amount > 0)
	{
		HitPoints -= Amount;
		OutEvents.DamageTaken += Amount;
	}
}

void FSimCopterFlightModel::StepGroundImpact(int32 Dt, const FSimCopterFlightEnvironment& Env, FSimCopterFlightEvents& OutEvents)
{
	// Master tick tail (FUN_00484d20 after the sub-steps). Parked and dying
	// helicopters skip the impact logic.
	if (State != ESimCopterFlightState::Flying && State != ESimCopterFlightState::FlyingAI)
	{
		return;
	}

	const bool bOutOfFuel = Fuel < 1;

	if (Altitude < Env.TerrainHeight)
	{
		if (Env.bHostileSurface)
		{
			// Water/wilderness: splash and bounce at 2x ClimbRate.
			Altitude = Env.TerrainHeight;
			ApplyDamage(4 + (bOutOfFuel ? Tuning.CollisionSubtract * 4 : 0), OutEvents);
			PitchTarget = SignedKick(NextRand()) * (Tuning.MaxPitch >> 2);
			SlideTarget = SignedKick(NextRand()) * (Tuning.MaxSlide >> 2);
			ClimbSpeed = Tuning.ClimbRate * 2;
			OutEvents.bSplashBounce = true;
			return;
		}
		if (!Env.bTerrainFlat)
		{
			// Rough/sloped ground: dust bounce with random attitude kicks.
			Altitude = Env.TerrainHeight;
			ApplyDamage(4 + (bOutOfFuel ? Tuning.CollisionSubtract * 4 : 0), OutEvents);
			PitchTarget = SignedKick(NextRand()) * (Tuning.MaxPitch >> 2);
			SlideTarget = SignedKick(NextRand()) * (Tuning.MaxSlide >> 2);
			ClimbSpeed = bOutOfFuel ? Tuning.ClimbRate : Tuning.ClimbRate * 4;
			OutEvents.bGroundBounce = true;
			return;
		}
	}

	// Sank below an elevated surface (building roof/pad edge): bounce away
	// along the motion direction and cut the controls for 0.2 s.
	if (Altitude < Env.SurfaceHeight && Env.SurfaceHeight > Env.TerrainHeight && BounceTimer <= 0)
	{
		ApplyDamage(Tuning.CollisionSubtract + (bOutOfFuel ? Tuning.CollisionSubtract * 4 : 0), OutEvents);
		Altitude = FMath::Max(Altitude, Env.TerrainHeight);
		NotifyWallImpact(OutEvents);
	}
}

void FSimCopterFlightModel::NotifyObjectCollision(FSimCopterFlightEvents& OutEvents)
{
	if (State != ESimCopterFlightState::Flying && State != ESimCopterFlightState::FlyingAI)
	{
		return;
	}
	ApplyDamage(4 + (Fuel < 1 ? Tuning.CollisionSubtract * 4 : 0), OutEvents);
	NotifyWallImpact(OutEvents);
}

void FSimCopterFlightModel::NotifyWallImpact(FSimCopterFlightEvents& OutEvents)
{
	// Master tick elevated-surface response: transform the motion direction
	// into the helicopter frame and kick pitch/slide away from the impact,
	// bounce up at 4x ClimbRate and start the 0.2 s control cut.
	int32 FwdX;
	int32 FwdZ;
	SimCopterFixed::SinCos(Heading, FwdX, FwdZ);

	// Normalised motion direction.
	const float VX = SimCopterFixed::ToFloat(VelX);
	const float VZ = SimCopterFixed::ToFloat(VelZ);
	const float Len = FMath::Sqrt(VX * VX + VZ * VZ);
	int32 LocalForward = SimCopterFixed::One;
	int32 LocalRight = 0;
	if (Len > KINDA_SMALL_NUMBER)
	{
		const float DirX = VX / Len;
		const float DirZ = VZ / Len;
		LocalForward = SimCopterFixed::FromFloat(DirX * SimCopterFixed::ToFloat(FwdX) + DirZ * SimCopterFixed::ToFloat(FwdZ));
		LocalRight = SimCopterFixed::FromFloat(DirX * SimCopterFixed::ToFloat(FwdZ) - DirZ * SimCopterFixed::ToFloat(FwdX));
	}

	PitchTarget = -SimCopterFixed::Mul(Tuning.MaxPitch, LocalForward);
	SlideTarget = SimCopterFixed::Mul(Tuning.MaxSlide, LocalRight);
	ClimbSpeed = Tuning.ClimbRate * 4;
	BounceTimer = 0x3333; // 0.2 s
	OutEvents.bPadBounce = true;
}
