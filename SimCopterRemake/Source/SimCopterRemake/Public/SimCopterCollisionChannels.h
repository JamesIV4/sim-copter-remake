// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

// The project's one custom collision channel, declared in Config/DefaultEngine.ini as
// "SimCopterFoliage". Keep the two in step: the ini gives the channel its name and its default
// response, this gives the C++ side something readable to say.
namespace SimCopterCollision
{
	// Object type of the city's tree instances (XBLD 0x06-0x0C, TREE6..TREE12). Not the small
	// park (0x0D) - that is a flat authored slab people stand on, so it stays ECC_WorldStatic.
	//
	// Why a channel of its own: the helicopter must still collide with a tree, because
	// FUN_0048ad50 answers any object on the aircraft's tile with damage and a bounce, but
	// nothing on foot may. The airframe capsule and every pedestrian capsule are both ECC_Pawn,
	// so a response set on the tree cannot separate them - only the tree's own object type can,
	// with the walkers opting out of it.
	constexpr ECollisionChannel Foliage = ECollisionChannel::ECC_GameTraceChannel1;

	// A tree also has to leave the pedestrian probes, which are trace queries on ECC_Camera and
	// therefore answer to the tree's *response*, not its object type: the walk-surface line trace
	// (so nobody stands on a canopy), the wall sweep (so nobody is stopped by a trunk) and the
	// spawn-clearance trace. ECC_Camera is the channel walkable city geometry answers on - see
	// ASimCopterGroundAgent::TryGetWalkSurfaceZAt - so dropping trees out of it removes them from
	// all of those at once. The helicopter's own contact test is the swept capsule in
	// ApplyFlightModelToActor, which is unaffected.
	constexpr ECollisionChannel WalkableSurface = ECollisionChannel::ECC_Camera;
}
