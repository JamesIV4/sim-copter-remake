// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

// Diagnostic trace for riot missions - why a riot ends, and how fast.
//
// A riot completes when `dispersed + casualties + criminalsCaught + calmed >= RiotSize`
// (FUN_004a73e0), and every one of those four counters can be reached without the player doing
// anything: BHAV 311 retires anyone whose agitation falls below 3 and pays nothing when the
// helicopter is more than six tiles away, and a rioter run down by traffic posts a casualty that
// counts just the same. This category records each of those transitions with the agitation that
// caused it, so a fast finish can be attributed rather than guessed at.
//
// Everything here is gated on `SimCopter.Riot.Log` and writes nothing when it is 0.

SIMCOPTERREMAKE_API DECLARE_LOG_CATEGORY_EXTERN(LogSimCopterRiot, Log, All);

namespace SimCopterRiotLog
{
/** `SimCopter.Riot.Log` - the master switch. Default 1. */
SIMCOPTERREMAKE_API bool IsEnabled();

/** `SimCopter.Riot.LogAgitation` - per-rioter agitation deltas and reaction hits. Default 1. */
SIMCOPTERREMAKE_API bool IsAgitationTraceEnabled();

/** `SimCopter.Riot.LogCensusSeconds` - crowd census period; 0 disables it. Default 5. */
SIMCOPTERREMAKE_API float GetCensusIntervalSeconds();

/** Seconds since the first line was emitted, so a session reads as elapsed time. */
SIMCOPTERREMAKE_API double GetTraceSeconds();

/** Start the age clock for a riot record. Called when the record is created. */
SIMCOPTERREMAKE_API void NoteRiotStarted(int32 EventId);

/** How long this riot has been alive, or -1 when it was never registered. */
SIMCOPTERREMAKE_API double GetRiotAgeSeconds(int32 EventId);

/** Drop the age clock (and the census timer) for a finished riot. */
SIMCOPTERREMAKE_API void NoteRiotEnded(int32 EventId);

/**
 * True when this riot is due another census line, and stamps the timer if so. Keeps the
 * per-riot cadence out of the caller.
 */
SIMCOPTERREMAKE_API bool ShouldEmitCensus(int32 EventId);

SIMCOPTERREMAKE_API void Emit(const FString& Line);
}

// Always has a format string, so __VA_ARGS__ is never empty and this is portable to MSVC's
// conforming preprocessor.
#define SIMCOPTER_RIOT_LOG(...) \
	do \
	{ \
		if (SimCopterRiotLog::IsEnabled()) \
		{ \
			SimCopterRiotLog::Emit(FString::Printf(__VA_ARGS__)); \
		} \
	} while (0)

#define SIMCOPTER_RIOT_LOG_AGITATION(...) \
	do \
	{ \
		if (SimCopterRiotLog::IsEnabled() && SimCopterRiotLog::IsAgitationTraceEnabled()) \
		{ \
			SimCopterRiotLog::Emit(FString::Printf(__VA_ARGS__)); \
		} \
	} while (0)
