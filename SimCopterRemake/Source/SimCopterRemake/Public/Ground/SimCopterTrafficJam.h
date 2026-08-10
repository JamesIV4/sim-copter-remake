// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Queuing behind a traffic jam, decoded from the original's vehicle move core.
//
// FUN_0049be50 is the per-tick "advance along the road" routine every vehicle in the original
// runs. The step it wants to take is offered to FUN_0049ee30, which answers with a result code and,
// when it refuses, the thing that is in the way. The whole of the original's traffic-jam behaviour
// falls out of two properties of that pair:
//
//   1. A BLOCKED CAR DOES NOT MOVE. Any result but 0 skips the advance entirely; FUN_0049be50
//      accumulates the held time at veh+0xaf and does nothing else until it reaches 20 s
//      (0x140001), when the car tries to reroute (FUN_0049ea70) or - for the emergency pools only -
//      rolls the 1-in-(0x40 >> difficulty) that raises a jam mission. There is no nudging, no lane
//      change and no backing up anywhere in it. Cars stack up nose to tail behind whatever stopped,
//      which is what a SimCopter jam looks like.
//
//   2. ONLY SAME-DIRECTION TRAFFIC BLOCKS. FUN_0049ee30 measures the two cars' headings against
//      each other (a dot product of their 16.16 direction vectors) and uses it twice: the effective
//      blocking radius is scaled by (1 + dot) / 2, and the forward probe additionally requires
//      `veh[0x127] * 0x2666 + 0x8000 <= dot`. A car coming the other way therefore has a blocking
//      radius of zero and is ignored outright - so a queue down one side of the street never stops
//      the other side, while two cars sharing a heading can never slip past one another.
//
// veh+0x127 is a yield counter: a car that defers to another at an intersection increments it, and
// because it sits on the left of that test, the longer a car has been giving way the stricter it
// gets about what counts as a blocker. That is the original's entire anti-deadlock mechanism, and
// it is a great deal gentler than steering around the queue.
//
// Functions:
//
//   FUN_0049be50  the per-tick vehicle update: the horn, the held-time accumulator at veh+0xaf,
//                 and the reroute / jam roll once it runs out.
//   FUN_0049ee30  the move core. The probe point tested for a blocker is
//                 pos + heading * (radius + 5 units); the tile lookup takes another 8 units.
//                 Result 0 = clear, 1 = an intersection another car holds, 4 = blocked by a
//                 vehicle, 3/5/6/7 = people and static objects, 0xc = a pass-through overlap.
//   FUN_004a22e0  the touch handler the current-position overlap calls, already ported as
//                 ASimCopterTrafficSystemActor::ApplyCollisionCarFireRoll.
namespace SimCopterTrafficJam
{
// FUN_0049ee30's probe: the point tested for a blocker is the car's own position advanced along its
// heading by its collision radius plus five units. A car therefore comes to rest with its centre
// one of its own radii, plus five units, plus the leader's radius behind the leader's centre.
constexpr float BlockProbeOriginalUnits = 5.0f;

// The same probe adds eight more units before it looks up which tile to search. Not used by the
// remake's queue (which searches agents, not tile lists) but it is the reason a car notices the
// next tile's occupants slightly before it reaches them.
constexpr float TileProbeOriginalUnits = 8.0f;

// The 0x8000 / 0x2666 pair in `veh[0x127] * 0x2666 + 0x8000 <= dot`, in 16.16.
constexpr float SameDirectionDot = 0.5f;
constexpr float SameDirectionDotPerYield = 0.15f;

// veh+0xaf thresholds in seconds. DAT_005039a0 is the frame delta in 16.16 seconds (clamped to
// 0xccc = 0.05 s), so the accumulator is plain seconds: 0x50000 and 0x140001.
constexpr float HornHeldSeconds = 5.0f;
constexpr float RerouteHeldSeconds = 20.0f;

// FUN_0049be50's two horn rolls. A car flying the jam flag 0x200 takes its own branch at the top of
// the function and rolls rand() & 0xf every update; anything else that has merely been held longer
// than HornHeldSeconds rolls rand() & 0x3f.
constexpr int32 JammedHornOneIn = 16;
constexpr int32 HeldHornOneIn = 64;

// (1 + dot) / 2, clamped at zero. A car on the same heading has to respect the leader's full
// radius, one crossing perpendicular respects half of it, and one coming the other way respects
// none of it.
SIMCOPTERREMAKE_API float GetHeadingBlockScale(float HeadingDot);

// `veh[0x127] * 0x2666 + 0x8000 <= dot`: is that car in front actually going my way? YieldCount is
// the original's veh+0x127, which only its intersection rule ever increments - a car queued in a
// jam never yields, so the remake's jam queue passes 0 and gets the plain 60-degree cone.
SIMCOPTERREMAKE_API bool IsSameDirectionBlocker(float HeadingDot, int32 YieldCount);

// Centre-to-centre distance a follower holds behind the car in front of it, from FUN_0049ee30's
// probe: my radius + 5 units + the leader's radius.
SIMCOPTERREMAKE_API float GetQueueHoldDistanceCm(
	float FollowerRadiusCm,
	float LeaderRadiusCm,
	float CmPerOriginalUnit);

// Speed scale for a follower whose leader's centre is ForwardDistanceCm ahead along its heading.
//
// DIVERGENCE, and a deliberate one: the original is physics-less and binary - the car either takes
// its whole step or takes none of it, so it stops dead the frame the probe reports a blocker. The
// remake's cars carry velocity, so a hard zero would be a visible snap and any braking at all
// overshoots a stop line placed exactly at the hold distance. This spends the approach band
// quadratically instead, which is already slow well before the hold distance and reaches zero at
// it, so the queue closes up smoothly and still parks at the original's spacing.
SIMCOPTERREMAKE_API float GetQueueSpeedScale(
	float ForwardDistanceCm,
	float HoldDistanceCm,
	float SlowDistanceCm);
}
