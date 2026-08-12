// Copyright Epic Games, Inc. All Rights Reserved.

#include "Missions/SimCopterRiotLog.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY(LogSimCopterRiot);

namespace
{
TAutoConsoleVariable<int32> CVarRiotLog(
	TEXT("SimCopter.Riot.Log"),
	1,
	TEXT("Trace riot mission lifecycle to LogSimCopterRiot: creation, every counter that moves\n")
	TEXT("toward completion, and the agitation behind it. 0 disables all riot logging."),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarRiotLogAgitation(
	TEXT("SimCopter.Riot.LogAgitation"),
	1,
	TEXT("Include per-rioter agitation changes and tool hits. Chatty while gassing a crowd;\n")
	TEXT("set to 0 to keep only the lifecycle lines."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarRiotLogCensusSeconds(
	TEXT("SimCopter.Riot.LogCensusSeconds"),
	5.0f,
	TEXT("How often to log the live rioter census (head count and agitation histogram).\n")
	TEXT("0 disables the census."),
	ECVF_Default);

double GTraceStartSeconds = 0.0;

struct FRiotTraceState
{
	double StartSeconds = 0.0;
	double LastCensusSeconds = 0.0;
};

// Diagnostic-only, so it lives here rather than as a member of the serialized mission system.
TMap<int32, FRiotTraceState> GRiotTraceStates;
}

namespace SimCopterRiotLog
{
bool IsEnabled()
{
	return CVarRiotLog.GetValueOnAnyThread() != 0;
}

bool IsAgitationTraceEnabled()
{
	return CVarRiotLogAgitation.GetValueOnAnyThread() != 0;
}

float GetCensusIntervalSeconds()
{
	return CVarRiotLogCensusSeconds.GetValueOnAnyThread();
}

double GetTraceSeconds()
{
	const double Now = FPlatformTime::Seconds();
	if (GTraceStartSeconds == 0.0)
	{
		GTraceStartSeconds = Now;
	}
	return Now - GTraceStartSeconds;
}

void NoteRiotStarted(int32 EventId)
{
	FRiotTraceState& State = GRiotTraceStates.FindOrAdd(EventId);
	State.StartSeconds = GetTraceSeconds();
	State.LastCensusSeconds = State.StartSeconds;
}

double GetRiotAgeSeconds(int32 EventId)
{
	const FRiotTraceState* State = GRiotTraceStates.Find(EventId);
	return State != nullptr ? GetTraceSeconds() - State->StartSeconds : -1.0;
}

void NoteRiotEnded(int32 EventId)
{
	GRiotTraceStates.Remove(EventId);
}

bool ShouldEmitCensus(int32 EventId)
{
	const float Interval = GetCensusIntervalSeconds();
	if (!IsEnabled() || Interval <= 0.0f)
	{
		return false;
	}
	FRiotTraceState* State = GRiotTraceStates.Find(EventId);
	if (State == nullptr)
	{
		// A riot restored from a save never saw its creation call; start its clocks now.
		NoteRiotStarted(EventId);
		return true;
	}
	const double Now = GetTraceSeconds();
	if (Now - State->LastCensusSeconds < double(Interval))
	{
		return false;
	}
	State->LastCensusSeconds = Now;
	return true;
}

void Emit(const FString& Line)
{
	// The engine already stamps wall clock and frame number; this relative clock is what makes
	// "the riot ended four seconds after it started" readable at a glance.
	UE_LOG(LogSimCopterRiot, Log, TEXT("[%8.2fs] %s"), GetTraceSeconds(), *Line);
}
}
