// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SimCopterMissionCatalog.h"

#include "Missions/SimCopterMissionSystem.h"

using namespace SimCopterMissions;

namespace
{
const FSimCopterMissionCatalogEntry GMissionCatalog[] =
{
	{ TYPE_BuildingFire, TEXT("Fire"),      TEXT("ignites a building; flames climb it storey by storey"), true },
	{ TYPE_CarFireEvent, TEXT("Fire"),      TEXT("burning ambient car (mask 0x408 = car fire + debris bit)"), true },
	{ TYPE_PlaneCrash,   TEXT("Fire"),      TEXT("tier 2+ only; plane object hook not ported"), false },
	{ TYPE_TrainCrash,   TEXT("Fire"),      TEXT("tier 3-4 only; train object hook not ported"), false },

	{ TYPE_CriminalA,    TEXT("Crime"),     TEXT("one criminal on foot (spawn mode 10, state 9)"), true },
	{ TYPE_SpeederEvent, TEXT("Crime"),     TEXT("one speeder on foot (spawn mode 0xb, state 9)"), true },
	{ TYPE_CriminalC,    TEXT("Crime"),     TEXT("tier 3+ criminal variant (spawn mode 0xc, state 9)"), true },
	{ TYPE_CriminalCar,  TEXT("Crime"),     TEXT("tier 3+ speeder car; car hook not ported"), false },

	{ TYPE_FireRescue,   TEXT("Rescue"),    TEXT("people trapped at a mission building (spawn mode 2)"), true },
	{ TYPE_BoatRescue,   TEXT("Rescue"),    TEXT("tier 2+ sinking boat; boat hook not ported"), false },
	{ TYPE_TrainRescue,  TEXT("Rescue"),    TEXT("tier 3+ train wreck; train hook not ported"), false },

	{ TYPE_Riot,         TEXT("Riot"),      TEXT("16+ rioters; needs 11 to place or it aborts"), true },
	{ TYPE_TrafficJam,   TEXT("Traffic"),   TEXT("jams an ambient car; clear it with the megaphone"), true },
	{ TYPE_Medevac,      TEXT("MedEvac"),   TEXT("1..tier injured people; deliver to the nearest hospital"), true },
	{ TYPE_Transport,    TEXT("Transport"), TEXT("building crowd pickup (spawn mode 4)"), true },

	{ TYPE_Ufo,          TEXT("(none)"),    TEXT("scheduler never rolls it; record only, no world object"), true },
};
}

TArrayView<const FSimCopterMissionCatalogEntry> GetSimCopterMissionCatalog()
{
	return MakeArrayView(GMissionCatalog, UE_ARRAY_COUNT(GMissionCatalog));
}
