// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SimCopterMissionCatalog.h"

#include "Missions/SimCopterMissionSystem.h"

using namespace SimCopterMissions;

namespace
{
const FSimCopterMissionCatalogEntry GMissionCatalog[] =
{
	{ TYPE_BuildingFire, TEXT("Building Fire"), TEXT("Fire"),      TEXT("ignites a building; flames climb it storey by storey"), true },
	{ TYPE_CarFireEvent, TEXT("Car Fire"),     TEXT("Fire"),      TEXT("burning ambient car (mask 0x408 = car fire + debris bit)"), true },
	{ TYPE_PlaneCrash,   TEXT("Plane Crash"),  TEXT("Fire"),      TEXT("tier 2+; PLANE1 dives - a fire, a boat rescue over water, or the crash itself"), true },
	{ TYPE_TrainCrash,   TEXT("Train Crash"),  TEXT("Fire"),      TEXT("tier 3-4; the train derails, slides for two seconds and blows up"), true },

	{ TYPE_CriminalA,    TEXT("Criminal"),     TEXT("Crime"),     TEXT("one criminal on foot (spawn mode 10, state 9)"), true },
	{ TYPE_SpeederEvent, TEXT("Speeder"),      TEXT("Crime"),     TEXT("one speeder on foot (spawn mode 0xb, state 9)"), true },
	{ TYPE_CriminalC,    TEXT("Criminal C"),   TEXT("Crime"),     TEXT("tier 3+ criminal variant (spawn mode 0xc, state 9)"), true },
	{ TYPE_CriminalCar,  TEXT("Speeder Car"),  TEXT("Crime"),     TEXT("tier 3+ speeder car; light it with the searchlight, then send police (F4/F5) to stop it"), true },

	{ TYPE_FireRescue,   TEXT("Rescue"),       TEXT("Rescue"),    TEXT("people trapped at a mission building (spawn mode 2)"), true },
	{ TYPE_BoatRescue,   TEXT("Boat Rescue"),  TEXT("Rescue"),    TEXT("tier 2+; CAPBOAT1 in open water with 3-5 survivors (spawn mode 1)"), true },
	{ TYPE_TrainRescue,  TEXT("Train Rescue"), TEXT("Rescue"),    TEXT("tier 3+; 1-3 passengers trapped by the train (spawn mode 0x13)"), true },

	{ TYPE_Riot,         TEXT("Riot"),         TEXT("Riot"),      TEXT("16+ rioters; needs 11 to place or it aborts"), true },
	{ TYPE_TrafficJam,   TEXT("Traffic Jam"),  TEXT("Traffic"),   TEXT("jams an ambient car; clear it with the megaphone"), true },
	{ TYPE_Medevac,      TEXT("MedEvac"),      TEXT("MedEvac"),   TEXT("1..tier injured people; deliver to the nearest hospital"), true },
	{ TYPE_Transport,    TEXT("Transport"),    TEXT("Transport"), TEXT("building crowd pickup (spawn mode 4)"), true },

	// The scheduler has no UFO bucket: the original's UFO is plane slot 1 (GEO 0x17c), which pays
	// [General Miss] UFO Money/Points from FUN_004b2910 when it goes down. This entry only creates
	// the bare record.
	{ TYPE_Ufo,          TEXT("UFO"),          TEXT("(none)"),    TEXT("scheduler never rolls it; the flying UFO is plane slot 1, not this record"), true },
};
}

TArrayView<const FSimCopterMissionCatalogEntry> GetSimCopterMissionCatalog()
{
	return MakeArrayView(GMissionCatalog, UE_ARRAY_COUNT(GMissionCatalog));
}
