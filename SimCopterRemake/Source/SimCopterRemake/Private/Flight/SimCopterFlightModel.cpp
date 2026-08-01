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

// Several of the original's rules are written per *frame* with no delta term at
// all - the collective's neutral decay, the forward-speed chase, the attitude
// EMA, the fire burn, the rotor strobe. They were tuned at its ~20 fps frame, and
// the executable half-admits that by clamping the smoothing window's frame delta
// to 0.05 s. Running them unchanged at the remake's substep would make the
// simulation a function of the display rate: three times too fast at 60 fps,
// seven at 144. Each one is therefore converted against
// FSimCopterFlightModel::OriginalFrameSeconds.
//
// Returns the fraction of a value a per-frame decay should remove over Dt: a
// removal of `PerFrameRemoved` once per original frame is 1 - (1 - r)^(dt/0.05).
// The subtraction is done in float because for short substeps the result is a
// small difference of two near-equal numbers, and 16.16 has no precision to spare
// there.
int32 DecayOverSubstep(int32 PerFrameRemoved, int32 Dt, int32 FramePeriod)
{
	const float Retained = 1.0f - ToFloat(PerFrameRemoved);
	if (Retained <= 0.0f)
	{
		return One;
	}
	const float Exponent = ToFloat(Dt) / ToFloat(FMath::Max(FramePeriod, 1));
	return FromFloat(1.0f - FMath::Pow(Retained, Exponent));
}

// The substep's share of one reference frame, for rules that add a fixed amount per
// frame rather than decaying.
int32 SubstepFrameFraction(int32 Dt, int32 FramePeriod)
{
	return Div(Dt, FMath::Max(FramePeriod, 1));
}

// FUN_00487740's blade step: a flat 39.1 degrees per frame once the rotor passes 250.
// As a rate that is only 782 deg/s at 20 fps, which reads as slow motion on a modern
// display; the remake had been drawing 39.1 x the display rate instead (~2300 deg/s
// at 60 fps, ~5600 at 144). Scaled by RotorVisualMultiplier against this fixed
// period, so the blades turn at the same apparent speed on every machine.
constexpr int32 RotorVisualFramePeriod = 0x0ccc; // 0.05 s
constexpr int32 RotorStrobeStep = 0x1870000;     // 391.0 tenth-deg per frame

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
	TurbPitchPrev = TurbSlidePrev = TurbYawPrev = 0;
	TurbPitchNext = TurbSlideNext = TurbYawNext = 0;
	TurbulenceClock = 0;
	FireDamageAccrued = 0;
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

	// FUN_00485f50's very last statement is `if (heli[0xcc] < 1) heli[3] = -1;` - the no-fuel
	// override is written into the collective field ITSELF, so every reader later in the same
	// tick sees it. Both do read it: FUN_00487160 for the descent, and FUN_00487740 to decide
	// whether the parked rotor winds down. Deriving the override privately inside StepVertical
	// left StepRotor looking at the player's raw input, so a dry helicopter with the collective
	// held kept its rotor frozen at whatever RPM it had instead of spooling down.
	FSimCopterFlightInputs Effective = Inputs;
	if (Fuel < 1)
	{
		Effective.ClimbCommand = -1;
	}

	StepTurbulence(Dt, Env, OutEvents);
	StepAttitude(Dt, Env);
	StepVelocity(Dt, Effective);
	StepVertical(Dt, Effective, Env, OutEvents);
	StepRotor(Dt, Effective);
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
	//
	// The easy model halves this, but only here: FUN_00485f50 computes the rate
	// twice, applies `iVar2 >> 1` to the copy the pitch keys use, and re-reads
	// Ctrl6 unhalved for the slide keys below. Halving both flies wrong.
	int32 PitchRampRate = Mul(Tuning.SlideRate, LoadFactor);
	if (bEasyFlightModel)
	{
		PitchRampRate >>= 1;
	}

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
		// No input: decay toward level at (1 - 2*dt) per frame. The easy model
		// uses (1 - dt) instead, so a trimmed nose attitude persists.
		PitchTarget = Mul(PitchTarget, bEasyFlightModel ? (One - Dt) : ((0x8000 - Dt) * 2));
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

void FSimCopterFlightModel::StepTurbulence(int32 Dt, const FSimCopterFlightEnvironment& Env, FSimCopterFlightEvents& OutEvents)
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
			// Flying inside the fire band burns hit points. The original spends this
			// many per frame; a substep owes a fraction of one, so it accrues and is
			// paid in whole points - otherwise truncation either burns nothing at all
			// or, unscaled, makes a fire several times more lethal than the original's.
			if (Tuning.MinFireAlt <= FireDelta && FireDelta <= Tuning.MaxFireAlt)
			{
				// Charged against the original's own 20 Hz frame, NOT ReferenceFrameSeconds. That
				// knob is a feel setting the debug panel dials in - the pawn ships it at 1/60 for
				// the climb decay and the EMA window - and pointing a *damage* rule at it made a
				// fire burn three times as fast as the executable does. How much a fire costs is
				// fidelity, not taste, so it stays pinned.
				const int32 PerFrame = (Tuning.MaxFireAlt >> 16) - (FireDelta >> 16);
				FireDamageAccrued += Mul(PerFrame * One, SubstepFrameFraction(Dt, OriginalFrameSeconds));
				const int32 WholePoints = FireDamageAccrued >> 16;
				if (WholePoints > 0)
				{
					ApplyDamage(WholePoints, OutEvents);
					FireDamageAccrued -= WholePoints << 16;
				}
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

	// The ring advances on the original's 20 Hz frame boundary, not on every
	// substep, so the sequence of kicks is the one the executable would have
	// generated. The order of the three pulls is the original's (slide, yaw,
	// pitch) because they consume the shared rand() stream.
	TurbulenceClock += Dt;
	while (TurbulenceClock >= TurbulenceFrameSeconds)
	{
		TurbulenceClock -= TurbulenceFrameSeconds;
		TurbSlidePrev = TurbSlideNext;
		TurbYawPrev = TurbYawNext;
		TurbPitchPrev = TurbPitchNext;
		TurbSlideNext = PushSample(TurbSlideSamples);
		TurbYawNext = PushSample(TurbYawSamples);
		TurbPitchNext = PushSample(TurbPitchSamples);
	}

	// Lerp across the interval so a 60 Hz substep does not see a 20 Hz staircase.
	const int32 Phase = Div(TurbulenceClock, TurbulenceFrameSeconds);
	TurbSlide = TurbSlidePrev + Mul(TurbSlideNext - TurbSlidePrev, Phase);
	TurbYaw = TurbYawPrev + Mul(TurbYawNext - TurbYawPrev, Phase);
	TurbPitch = TurbPitchPrev + Mul(TurbPitchNext - TurbPitchPrev, Phase);
}

int32 FSimCopterFlightModel::SmoothingFrames(int32 /*RateTenthDeg*/, int32 Dt) const
{
	// FUN_00486a30: N = ((1000.0 - PitchRate) / 500) * fps, where fps comes from the
	// RAW frame delta DAT_005039a0 floored at 0.05 s - so 20 fps is the most the term
	// can ever be. 21 frames for the Jet Ranger. All four axes share this N.
	int32 ClampedDt = FMath::Max(Dt, ReferenceFrameSeconds);
	const int32 Fps = Div(One, ClampedDt);
	int32 Frames = (static_cast<int64>(0x3e80000 - Tuning.PitchRate) / 500) * (Fps >> 16) >> 16;
	return FMath::Max(Frames, 1);
}

int32 FSimCopterFlightModel::SmoothingAlpha(int32 Dt) const
{
	// N above is a count of frames and the filter removes 1/N of the gap per frame,
	// so N/fps - the lag in seconds - has the fps terms cancel: the original's
	// attitude lag is *already* frame-rate independent by construction, and the
	// `* fps` is its delta-time compensation written the long way round. What breaks
	// it is its own floor on the delta, which pins the fps term at 20 above 20 fps
	// while the filter keeps being applied at the real rate. That is exactly the
	// regime the remake lives in - the pawn's substep is at most 1/60 s.
	//
	// Reproduce the filter the original actually ran, rather than the one the
	// un-floored formula describes: retaining (N-1)/N per 0.05 s frame. Those differ
	// - N is floored from 21.89 to 21, which is a 1.02 s lag where the algebra says
	// 1.09 - and 20 fps is the only frame rate the executable itself ever names, so
	// its behaviour there is the thing worth matching.
	const int32 Frames = SmoothingFrames(Tuning.PitchRate, Dt);
	const int32 PerFrameAlpha = Div(One, Frames * One);
	return DecayOverSubstep(PerFrameAlpha, FMath::Min(Dt, ReferenceFrameSeconds), ReferenceFrameSeconds);
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

	// Turbulence feeds straight into the targets (player helicopters only). The
	// original adds the whole ring average once per rendered frame; scaling by the
	// substep's share of a 20 Hz frame keeps the amount injected per second - and
	// so the size of the random walk the shake produces - the same at any rate.
	// This is 1.0 at dt = 1/20, where it reduces to the original's arithmetic.
	const int32 TurbulenceInjection = SubstepFrameFraction(Dt, TurbulenceFrameSeconds);
	PitchTarget += Mul(TurbPitch, TurbulenceInjection);

	// Within 150 units of the ground a proximity bonus of up to MaxPitch/8
	// loosens the clamp as height increases.
	int32 Bonus = 0;
	const int32 Proximity = (Env.TerrainHeight - Altitude) + GroundPitchBand;
	if (Proximity > 0 && Proximity < GroundPitchBand)
	{
		Bonus = Mul(PitchClamp >> 3, Div(GroundPitchBand - Proximity, GroundPitchBand));
	}
	// The easy model halves each term separately - (Bonus >> 1) + (PitchClamp >> 1),
	// which is how FUN_00486a30 writes it - so the nose can only reach half the
	// attitude, and with it half the standard model's top speed per degree.
	const int32 PitchLimit = bEasyFlightModel ? ((Bonus >> 1) + (PitchClamp >> 1)) : (PitchClamp + Bonus);
	PitchTarget = FMath::Clamp(PitchTarget, -PitchLimit, PitchLimit);

	// First-order lag shared by all four smoothed values.
	const int32 Alpha = SmoothingAlpha(Dt);
	PitchSmoothed += Mul(PitchTarget - PitchSmoothed, Alpha);

	// Bank: clamp to MaxBank, then to |smoothed pitch| + 30 degrees.
	BankTarget = FMath::Clamp(BankTarget, -Tuning.MaxBank, Tuning.MaxBank);
	const int32 BankLimit = FMath::Abs(PitchSmoothed) + BankOverPitchAllowance;
	BankTarget = FMath::Clamp(BankTarget, -BankLimit, BankLimit);
	BankSmoothed += Mul(BankTarget - BankSmoothed, Alpha);

	// Slide.
	SlideTarget += Mul(TurbSlide, TurbulenceInjection);
	SlideTarget = FMath::Clamp(SlideTarget, -Tuning.MaxSlide, Tuning.MaxSlide);
	SlideSmoothed += Mul(SlideTarget - SlideSmoothed, Alpha);

	// Yaw rate.
	YawRateTarget += Mul(TurbYaw, TurbulenceInjection);
	YawRateTarget = FMath::Clamp(YawRateTarget, -Tuning.MaxYawRate, Tuning.MaxYawRate);
	YawRateSmoothed += Mul(YawRateTarget - YawRateSmoothed, Alpha);

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
		// The easy model doubles the airspeed a degree of pitch is worth, which
		// cancels its halved pitch clamp at the top end and leaves it faster
		// everywhere below - and it sheds speed at 1/16 per frame rather than
		// 1/32, so backing off the nose actually slows it down.
		// The original closes 1/32 of the gap per frame (1/16 shedding speed under
		// the easy model), written as a shift; converted here so the same fraction
		// is closed per unit time whatever the substep is, against
		// SpeedChaseFramePeriod rather than the 20 fps reference the other rules use.
		const int32 Target = bEasyFlightModel ? PitchSmoothed * 2 : PitchSmoothed;
		if (ForwardSpeed < Target)
		{
			ForwardSpeed += Mul(Target - ForwardSpeed, DecayOverSubstep(One >> 5, Dt, SpeedChaseFrameSeconds));
		}
		if (ForwardSpeed > Target)
		{
			const int32 PerFrame = bEasyFlightModel ? (One >> 4) : (One >> 5);
			ForwardSpeed -= Mul(ForwardSpeed - Target, DecayOverSubstep(PerFrame, Dt, SpeedChaseFrameSeconds));
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
	// FUN_00487160. The no-fuel override already landed on Inputs.ClimbCommand in Step(), where
	// FUN_00485f50 puts it, so this reads the same value the rotor step does.
	const bool bOutOfFuel = Fuel < 1;
	const int32 ClimbCommand = Inputs.ClimbCommand;

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
		// Neutral: exponential decay of 5% per frame climbing, 10% descending,
		// converted to the substep. This one decides how far the helicopter coasts
		// after the collective is released, which is the whole feel of a landing
		// flare - unconverted it arrests three times too quickly at 60 fps.
		if (ClimbSpeed > 0)
		{
			ClimbSpeed -= Mul(DecayOverSubstep(0x0ccc, Dt, ReferenceFrameSeconds), ClimbSpeed);
			ClimbSpeed = FMath::Max(ClimbSpeed, 0);
		}
		else if (ClimbSpeed < 0)
		{
			ClimbSpeed -= Mul(DecayOverSubstep(0x1999, Dt, ReferenceFrameSeconds), ClimbSpeed);
			ClimbSpeed = FMath::Min(ClimbSpeed, 0);
		}
		// Touching down on ground the helicopter cannot land on pushes it back up at
		// 4x ClimbRate. FUN_00487160 gates this on `param_1[0x53] == 0` - the terrain
		// flat flag being CLEAR, i.e. rough or hostile ground - so that settling onto
		// flat, landable ground is never interrupted. Testing bTerrainFlat instead
		// inverts it and shoves the helicopter off exactly the ground it is trying to
		// land on, which is what made a shaking airframe impossible to put down.
		if ((Altitude <= Env.SurfaceHeight || Altitude <= Env.TerrainHeight) && !Env.bTerrainFlat)
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
		// FUN_00487160 tests `0x12bffff < RotorSpeed`, so exactly 300.0 is
		// sufficient for takeoff. Requiring strictly more can strand the fixed-point
		// rotor at the gate forever: the spool branch only advances values below it.
		if (ClimbCommand > 0 && RotorSpeed >= RotorLiftGate)
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

	// Blade step: proportional while spooling, then a constant 39.1 degrees per frame -
	// the original's deliberate blur strobe. Both branches are per-frame rates scaled
	// to the substep and then up by RotorVisualSpeedUp, so the blades turn at the same
	// apparent speed on every machine. See the constant for why it is not 1.
	int32 BladeStep = Mul(Mul(RotorStrobeStep, RotorVisualMultiplier), Div(Dt, RotorVisualFramePeriod));
	if (RotorSpeed <= 0xfa0000)
	{
		BladeStep = Mul(Mul(RotorSpeed, Dt << 5), RotorVisualMultiplier);
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





