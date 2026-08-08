// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Decompiled SimCopter helicopter flight model, ported from SimCopter.exe.
//
// This is an exact 16.16 fixed-point port of the original per-frame helicopter
// simulation, decoded from these executable functions (see
// Docs/scratchpad/ghidra/out_heli_*.txt and Docs/DecompilationWorkflow.md):
//
//   FUN_00484d20  master tick (weight/load factor, fuel burn, ground impact + bounce)
//   FUN_00485f50  control reader (key ramps, analog seek targets, collective command)
//   FUN_00489800  turbulence/shake generator (damage + fire proximity, 9-frame average)
//   FUN_00486a30  attitude integrator (clamps, first-order lag, heading integration)
//   FUN_00486e90  velocity integrator (speed chases smoothed pitch, slide at x0.488)
//   FUN_00487160  vertical/ground logic (climb ramps, rotor spool gate, ceiling,
//                 neutral decay, landing rules, state transitions)
//   FUN_00487740  rotor animation (spool rates, blade step, blur disc at RPM >= 300)
//
// Units are the original's: positions/speeds in world units (64 units per city
// tile, 16.16 fixed point), angles in tenths of degrees (full turn = 3600.0),
// time in seconds (16.16). Both of the original's handling models are here: the
// standard one and the "easy" variant it selected from the view mode - see
// FSimCopterFlightModel::bEasyFlightModel.

namespace SimCopterFixed
{
constexpr int32 One = 0x10000;
constexpr int32 FullTurnTenthDeg = 0xe100000; // 3600.0 tenth-degrees

// FUN_0046c49d: 16.16 multiply through a 64-bit intermediate.
inline int32 Mul(int32 A, int32 B)
{
	return static_cast<int32>((static_cast<int64>(A) * static_cast<int64>(B)) >> 16);
}

// FUN_0046c4bf: 16.16 divide; returns the numerator unchanged when B == 0.
inline int32 Div(int32 A, int32 B)
{
	if (B == 0)
	{
		return A;
	}
	return static_cast<int32>((static_cast<int64>(A) << 16) / static_cast<int64>(B));
}

inline int32 FromFloat(float Value)
{
	return static_cast<int32>(FMath::RoundToInt64(static_cast<double>(Value) * 65536.0));
}

inline float ToFloat(int32 Value)
{
	return static_cast<float>(Value) / 65536.0f;
}

// FUN_0046c4dc: sine/cosine of an angle in tenth-degrees via the original's
// quarter-wave lookup tables (0x46b530). Wraps the angle into [0, 3600).
SIMCOPTERREMAKE_API int32 WrapAngle(int32 AngleTenthDeg);
SIMCOPTERREMAKE_API void SinCos(int32 AngleTenthDeg, int32& OutSin, int32& OutCos);
}

// Per-frame pilot input, mirroring the original's virtual-control array
// (FUN_0041c2a0 key-held bytes / FUN_0041c2e0 analog axes read by FUN_00485f50).
struct SIMCOPTERREMAKE_API FSimCopterFlightInputs
{
	// Controls 3/4: coordinated turn keys (bank + yaw together).
	bool bTurnLeftKey = false;
	bool bTurnRightKey = false;

	// Control 0x14 modifier: while held, the turn keys slide instead of turning.
	bool bSlideModifier = false;

	// Controls 5/6: pitch keys. Forward accumulates nose-down (accelerate).
	bool bPitchForwardKey = false;
	bool bPitchBackKey = false;

	// Controls 7/8: dedicated slide keys.
	bool bSlideLeftKey = false;
	bool bSlideRightKey = false;

	// Controls 9/10: collective. +1 climb, -1 descend, 0 neutral.
	int32 ClimbCommand = 0;

	// Analog axes in the original's percentage range (-100..100, 0 = centred).
	// When non-zero these take the joystick paths (target-seeking) instead of
	// the keyboard ramps.
	int32 TurnAxis = 0;
	int32 PitchAxis = 0;
	int32 SlideAxis = 0;

	// DAT_0051ac58 debug turbo (speed/climb x10). Off in normal play.
	bool bTurbo = false;
};

// What the simulation needs to know about the city under the helicopter.
// The original sampled its heightfield (FUN_004ae7a0), the tallest city object
// below (FUN_00488850) and the tile density/class grid (DAT_005bde80); the
// remake backs these with traces against the city actor.
struct SIMCOPTERREMAKE_API FSimCopterFlightEnvironment
{
	// [0x58]: terrain height (16.16 world units) at the helicopter's X/Z.
	int32 TerrainHeight = 0;

	// [0x53]: terrain is locally flat (original: triangle corners within 9.0
	// units); required for landing on the ground.
	bool bTerrainFlat = true;

	// [0x59]: top of the landing surface below (building roof/helipad), falling
	// back to the terrain height when nothing is built there.
	int32 SurfaceHeight = 0;

	// [0x61]: tile class < 10 with nothing built on it - water/wilderness. The
	// original clears the flat flag (no landing) and bounces with a splash and
	// a stronger kick.
	bool bHostileSurface = false;

	// FUN_004a5c10: helicopter height above a fire on the current tile
	// (16.16 units), 0 when no fire below. Between MinFireAlt and MaxFireAlt the
	// helicopter takes damage and shakes.
	int32 FireHeightDelta = 0;
};

// One-frame event flags for the pawn (sounds/effects in the original engine).
struct SIMCOPTERREMAKE_API FSimCopterFlightEvents
{
	bool bTouchedDown = false;      // gentle landing completed (state -> Parked)
	bool bLiftedOff = false;        // spool complete, state -> Flying
	bool bGroundBounce = false;     // slammed terrain, bounced (effect 1 + sound 1)
	bool bSplashBounce = false;     // slammed water/wilderness (effect 9 + sound 0xf)
	bool bPadBounce = false;        // hit an elevated surface / wall bounce
	bool bStartedDying = false;     // hit points below zero, state -> Dying
	bool bCrashed = false;          // dying helicopter reached the ground
	int32 DamageTaken = 0;          // hit points lost this frame
};

// Per-type tuning in original units. The fxpt tweak values map 1:1: angles in
// tenth-degrees (10 = 1 degree), rates in tenth-degrees/second, climb/landing
// speeds in world units/second (64 units per tile).
struct SIMCOPTERREMAKE_API FSimCopterFlightTuning
{
	// heli.twk per-type controls (block at DAT_005040e4 + type*0x5c).
	int32 MaxBank = SimCopterFixed::FromFloat(426.7f);        // +0x08 Ctrl0
	int32 MaxSlide = SimCopterFixed::FromFloat(140.2f);       // +0x0c Ctrl1
	int32 MaxPitch = SimCopterFixed::FromFloat(192.3f);       // +0x10 Ctrl2
	int32 PitchRate = SimCopterFixed::FromFloat(452.7f);      // +0x18 Ctrl3 (lag shaping)
	int32 YawRate = SimCopterFixed::FromFloat(105.5f);        // +0x1c Ctrl4 (key ramp)
	int32 RollRate = SimCopterFixed::FromFloat(209.7f);       // +0x20 Ctrl5 (key ramp)
	int32 SlideRate = SimCopterFixed::FromFloat(209.7f);      // +0x24 Ctrl6 (pitch/slide key ramp)
	int32 ClimbRate = SimCopterFixed::FromFloat(7.1f);        // +0x28 Ctrl7 (units/s basis)
	int32 MaxLoadPounds = 1548;                               // +0x04 Ctrl8 (plain int)
	int32 MaxYawRate = SimCopterFixed::FromFloat(58.9f);      // +0x14 Ctrl9
	int32 FuelRateGalPerHour = SimCopterFixed::FromFloat(230.8f); // +0x40 Ctrl10
	int32 MaxDamage = 604;                                    // +0x48 Ctrl12 (plain int)
	int32 FuelGallons = SimCopterFixed::FromFloat(91.2f);     // +0x3c Ctrl13
	int32 PassengerSeats = 4;                                 // +0x00 (static, not tweak-bound)
	bool bNoTailRotor = false;                                // +0x38 (static; MDEXPLORER/MD520)

	// [Heli Landing] (DAT_00504018..28). Landing tests compare the *targets*
	// (pitch target, slide target, climb speed, forward speed).
	int32 LandMaxPitch = SimCopterFixed::FromFloat(51.6f);    // Ctrl0 tenth-deg
	int32 LandMaxSlide = SimCopterFixed::FromFloat(43.3f);    // Ctrl1 tenth-deg
	int32 LandMaxSpeed = SimCopterFixed::FromFloat(42.1f);    // Ctrl2 units/s
	int32 LandMaxYSpeed = SimCopterFixed::FromFloat(20.1f);   // Ctrl3 units/s
	int32 MaxDescentRate = SimCopterFixed::FromFloat(19.4f);  // Ctrl4 units/s

	// [Heli Damage] (DAT_00504044..50).
	int32 MinFireAlt = SimCopterFixed::FromFloat(-48.0f);     // Ctrl0 units
	int32 MaxFireAlt = SimCopterFixed::FromFloat(61.1f);      // Ctrl1 units
	int32 CollisionSubtract = 27;                             // Ctrl3 (plain int)
};

// Original state codes (helicopter +0x04).
enum class ESimCopterFlightState : uint8
{
	Parked = 0,
	Flying = 1,
	FlyingAI = 4,
	Dying = 5,
	Dead = 6,
};

// The helicopter flight simulation. Field comments give the original's
// int-array offsets (param_1[N] in the decompiled functions).
struct SIMCOPTERREMAKE_API FSimCopterFlightModel
{
	FSimCopterFlightTuning Tuning;

	// The original's second handling model, selected by the view-mode global
	// DAT_00503aa0: mode 0 is the external chase view and flies the standard
	// model, while the interior views (1 and 2, cycled by input action 0x15)
	// switch every `DAT_00503aa0 != 0` branch below to the easy one. Flying from
	// inside the cabin you cannot read your own attitude, so the game trades
	// authority for reach - four divergences, all cited at their use sites:
	//
	//   FUN_00485f50  pitch key ramp halved; pitch decays at (1 - dt) not
	//                 (1 - 2*dt) per frame, so the nose holds its trim longer.
	//                 The slide ramp re-reads Ctrl6 and is NOT halved.
	//   FUN_00486a30  the pitch clamp and its ground-proximity bonus are each
	//                 halved before the clamp.
	//   FUN_00486e90  the airspeed a given pitch buys is doubled, and speed
	//                 bleeds off at 1/16 per frame instead of 1/32.
	//
	// The remake decouples this from the camera: it is a handling option the
	// developer panel toggles, so either model can be flown from any view.
	bool bEasyFlightModel = false;

	ESimCopterFlightState State = ESimCopterFlightState::Parked; // [1]

	// Position in world units (16.16). Altitude is the original node +0x1c "Y".
	int32 PosX = 0;
	int32 Altitude = 0;
	int32 PosZ = 0;

	// Attitude. Targets accumulate stick input; smoothed values follow with a
	// first-order lag; the heading integrates the smoothed yaw rate.
	int32 Heading = 0;          // [0x43] tenth-deg, wrapped to [0, 3600)
	int32 PitchTarget = 0;      // [0x47] tenth-deg, + = nose down (accelerate)
	int32 PitchSmoothed = 0;    // [0x48] drives the forward speed
	int32 BankTarget = 0;       // [0x45] tenth-deg
	int32 BankSmoothed = 0;     // [0x4b] display roll (inherits slide, see quirk)
	int32 SlideTarget = 0;      // [0x46] tenth-deg, + = slide left
	int32 SlideSmoothed = 0;    // [0x49]
	int32 YawRateTarget = 0;    // [0x4c]
	int32 YawRateSmoothed = 0;  // [0x4a]

	// Motion.
	int32 ForwardSpeed = 0;     // [0x4e] chases PitchSmoothed
	int32 ClimbSpeed = 0;       // [0x4d] units/s, + = up
	int32 SpeedDelta = 0;       // [0x4f] speed change this frame (autorotation)
	int32 HorizontalSpeed = 0;  // [0x37] |velocity| for HUD
	int32 VelX = 0;             // [0x38] world units/s
	int32 VelZ = 0;             // [0x3a]
	int32 DeltaX = 0;           // [0x50] applied this frame (16.16 units)
	int32 DeltaZ = 0;           // [0x51]
	int32 AboveGround = 0;      // [0x52] altitude - terrain

	// Rotor. RotorSpeed gates lift at 300.0 and the blur disc; the blade angle
	// steps min(RotorSpeed * 32 * dt, 391.0) tenth-degrees per frame.
	int32 RotorSpeed = 0;       // [0x56]
	int32 MainRotorAngle = 0;   // accumulated blade angle, tenth-deg
	int32 TailRotorAngle = 0;
	bool bRotorBlurDisc = false; // [0x55] face-type-11 blur faces visible

	// Status.
	int32 HitPoints = 604;      // [0x34] starts at MaxDamage
	int32 Fuel = SimCopterFixed::FromFloat(91.2f); // [0xcc] gallons 16.16
	int32 FlightSeconds = 0;    // [0xcd] 16.16
	int32 LoadPounds = 0;       // [0x74] cargo/bucket water weight
	int32 Passengers = 0;       // [0x77] each weighs 120 lb
	int32 LoadFactor = SimCopterFixed::One; // [0xce] scales rates/climb
	int32 BounceTimer = 0;      // [0x44] 16.16 s; controls cut while active
	int32 DyingTimer = 0;       // remake helper for the Dying -> Dead sequence

	// Turbulence (FUN_00489800): 9-sample ring buffers of random kicks whose
	// averages are added to the attitude targets every frame.
	//
	// The original pushes one sample per rendered frame and adds the average to
	// the targets once per rendered frame, so the shake it produces is a function
	// of the frame rate: the excursion grows as 1/sqrt(dt), and at the remake's
	// 60 Hz substep a damaged airframe shook about 1.8x as hard as the original's
	// did at the ~20 fps its constants were tuned for. (The original concedes that
	// design rate itself by capping the attitude EMA at 20 fps - see
	// MinSmoothingDt.) The remake therefore pins turbulence to a fixed 20 Hz clock
	// and interpolates between ticks, which needs both halves to work:
	//
	//   * the ring buffer advances on OriginalFrameSeconds boundaries only, so the
	//     noise *sequence* is the original's rather than 3x as many samples, and
	//     TurbPitch/Slide/Yaw lerp from the previous tick's average to the current
	//     one so nothing steps at 60 Hz;
	//   * StepAttitude scales each injection by dt/OriginalFrameSeconds, so the amount
	//     added per second is rate-independent. Interpolating alone would have made
	//     things *worse* - a smoothly held value is more correlated frame to frame,
	//     which pushes the random walk toward the 3x constant-bias case.
	//
	// At dt = 1/20 this reduces to the original's arithmetic exactly, one tick of
	// lag aside (interpolation cannot reach a value it has not generated yet).
	int32 TurbPitchSamples[9] = {};
	int32 TurbSlideSamples[9] = {};
	int32 TurbYawSamples[9] = {};
	int32 TurbPitch = 0;        // DAT_0057f2e0, interpolated
	int32 TurbSlide = 0;        // DAT_0057f2b8, interpolated
	int32 TurbYaw = 0;          // DAT_0057f240, interpolated

	// Ring averages either side of the current 20 Hz interval, and the 16.16
	// seconds accumulated into it.
	int32 TurbPitchPrev = 0;
	int32 TurbSlidePrev = 0;
	int32 TurbYawPrev = 0;
	int32 TurbPitchNext = 0;
	int32 TurbSlideNext = 0;
	int32 TurbYawNext = 0;
	int32 TurbulenceClock = 0;

	// 0.05 s: the frame period the original's per-frame rules were written against.
	// It is the only frame rate the executable ever names - the floor its attitude EMA
	// puts on the frame delta (FUN_00486a30) - but it is an inference, not a fact: the
	// original's delta is raw GetTickCount with no fixed timestep (FUN_00449850), so
	// the helicopter genuinely shook and accelerated harder on a faster machine.
	static constexpr int32 OriginalFrameSeconds = 0x0ccc;

	// --- Frame-rate assumption, live-tunable from the helicopter debug panel ---
	//
	// Every rule the executable applied "once per frame" is converted against these,
	// so the simulation behaves identically at 20, 60, 144 or 240 fps whatever they
	// are set to. What they change is *which* machine's feel is being reproduced.
	//
	// The reference is split three ways because a single knob fights itself: raising it
	// makes the helicopter accelerate harder (good) but also shake harder, arrest its
	// descent faster and burn quicker in a fire (all bad). Acceleration and the shake
	// are the two that wanted to move in opposite directions, so each has its own.
	//
	// These default to the executable's own figures, so a default-constructed model
	// reproduces the port and the automation tests assert fidelity rather than
	// whatever the panel is currently dialled to. The playable starting values live on
	// the pawn (ASimCopterHelicopterPawn::BeginPlay) and are overridden by the ini.
	//
	// TurbulenceFrameSeconds drives the shake alone - the ring buffer's advance and the
	// per-substep injection into the attitude targets.
	int32 TurbulenceFrameSeconds = OriginalFrameSeconds;

	// ReferenceFrameSeconds drives the rest of the per-frame rules: the
	// neutral-collective climb decay, the fire burn and the attitude EMA's window. Note
	// the attitude EMA barely moves with it - FUN_00486a30 already carries its own fps
	// compensation, so only the integer flooring of N shifts.
	int32 ReferenceFrameSeconds = OriginalFrameSeconds;

	// SpeedChaseFrameSeconds drives FUN_00486e90's 1/32-of-the-gap airspeed chase, the
	// dominant term in how long the helicopter takes to get moving.
	int32 SpeedChaseFrameSeconds = OriginalFrameSeconds;

	// Pure presentation: how much faster than the original's 39.1-degrees-per-0.05 s
	// strobe the blades are drawn (16.16 multiplier). Nothing in the simulation reads
	// the blade angle, and this is deliberately independent of the two above so that
	// revisiting a fidelity assumption never changes how fast the rotor looks.
	int32 RotorVisualMultiplier = SimCopterFixed::One;

	// Fire burns whole hit points per original frame; a substep's share is fractional,
	// so it accrues here and is spent as whole points (16.16).
	int32 FireDamageAccrued = 0;

	// MSVC rand() (the original's _rand), kept internal so tests are deterministic.
	uint32 RandState = 1;

	void ResetOnSurface(int32 InPosX, int32 InPosZ, int32 SurfaceHeight);

	// Runs one frame of the original per-helicopter tick.
	void Step(float DeltaSeconds, const FSimCopterFlightInputs& Inputs, const FSimCopterFlightEnvironment& Env, FSimCopterFlightEvents& OutEvents);

	// Original object-collision response (FUN_0048ad50 hit + master-tick bounce):
	// kicks the attitude away from the impact using the helicopter-local motion
	// direction, bounces up and starts the control-cut bounce timer.
	void NotifyWallImpact(FSimCopterFlightEvents& OutEvents);

	// Full response for a discrete swept city-object hit. The original charged 4 hit points
	// per overlapping 20 Hz frame; the remake packets that sustained contact with the shared
	// CollisionSubtract value because its detector emits one rate-limited impact.
	void NotifyObjectCollision(FSimCopterFlightEvents& OutEvents);

	// The same thing for a wreck already in its death spiral. Movement and effects only: the
	// spiral owns the attitude, so nothing kicks pitch or bank. Without it a wreck that comes
	// down against a building hangs there spinning forever - the Dying branch integrates no
	// horizontal motion at all, so nothing else can carry it clear.
	void NotifyWreckCollision(int32 PushX1616, int32 PushZ1616, FSimCopterFlightEvents& OutEvents);

	// How far a wreck impact lifts the airframe, so a shove along a wall also clears the ledge
	// it is resting on. 4.0 original units.
	static constexpr int32 WreckImpactLift = 0x40000;

	// How hard that shove is, in 16.16 original units - a little over the collision capsule's
	// own radius so one impact always leaves the obstacle rather than re-hitting it next frame.
	static constexpr int32 WreckImpactPush = 0x110000;

	// Display attitude for the scene node (FUN_00486a30 tail: matrix built from
	// heading, pitch *target* and the smoothed bank after the slide quirk).
	int32 DisplayPitchTenthDeg() const { return PitchTarget; }
	int32 DisplayBankTenthDeg() const { return BankSmoothed; }

	bool IsRotorLifting() const { return RotorSpeed >= RotorLiftGate; }

	static constexpr int32 RotorLiftGate = 0x12c0000;   // 300.0: lift + blur disc
	static constexpr int32 RotorTopSpeed = 0x1680000;   // 360.0 in flight
	static constexpr int32 CeilingAboveTerrain = 0x3200000; // DAT_0050404c: 800.0 units

	int32 NextRand(); // MSVC LCG, returns 0..0x7fff

private:
	void StepControls(int32 Dt, const FSimCopterFlightInputs& Inputs);
	void StepTurbulence(int32 Dt, const FSimCopterFlightEnvironment& Env, FSimCopterFlightEvents& OutEvents);
	void StepAttitude(int32 Dt, const FSimCopterFlightEnvironment& Env);
	void StepVelocity(int32 Dt, const FSimCopterFlightInputs& Inputs);
	void StepVertical(int32 Dt, const FSimCopterFlightInputs& Inputs, const FSimCopterFlightEnvironment& Env, FSimCopterFlightEvents& OutEvents);
	void StepRotor(int32 Dt, const FSimCopterFlightInputs& Inputs);
	void StepGroundImpact(int32 Dt, const FSimCopterFlightEnvironment& Env, FSimCopterFlightEvents& OutEvents);
	void StepFuelAndDamage(int32 Dt, FSimCopterFlightEvents& OutEvents);
	void ApplyDamage(int32 Amount, FSimCopterFlightEvents& OutEvents);
	int32 SmoothingFrames(int32 RateTenthDeg, int32 Dt) const;
	int32 SmoothingAlpha(int32 Dt) const;
};
