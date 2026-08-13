// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Replay/SimCopterReplayTypes.h"

/**
 * Switchable trace for one person's behaviour program - what it selected, where it tried to walk,
 * and what refused it.
 *
 * Written for the arrest: a policeman and an arrested robber both stop dead on their way back to
 * the police car, and every mechanism that could do that is invisible from outside. Their programs
 * (BHAV 1060, BHAV 1150) do the same three things - probe for a police car within ten tiles, count
 * a hundred ticks of `goto object`, then either arrive or fall into an idle loop - so the trace
 * follows exactly that: selections, goto-object steps with the move result that stopped them,
 * reaction pushes, outcomes and the despawn.
 *
 * Off by default and costs nothing when off. Turn it on around the thing you want to watch:
 *
 *     SimCopter.People.Trace 1            master switch
 *     SimCopter.People.TraceStates 10     only the robber (default is the criminals and both cops)
 *     SimCopter.People.TraceStates *      everybody, including the ambient crowd - very chatty
 *     SimCopter.People.TraceOpcodes 1     every VM record as it executes, with its result
 *
 * Person states worth naming: 0 ambient, 2 rooftop victim, 3 rioter, 4 transport fare, 5 hospital
 * worker, 6 medevac patient, 7 aerial cop, 8 foot cop, 10 robber, 11 arsonist, 12 mugger,
 * 13 burglar.
 */

SIMCOPTERREMAKE_API DECLARE_LOG_CATEGORY_EXTERN(LogSimCopterPeople, Log, All);

namespace SimCopterPeopleTrace
{
/** `SimCopter.People.Trace` - the master switch. Default 0. */
SIMCOPTERREMAKE_API bool IsEnabled();

/**
 * Whether this person's state is in `SimCopter.People.TraceStates`. The default list is
 * "8,10,11,12,13" - the two criminals' pursuers and the four criminals - and `*` means everyone.
 */
SIMCOPTERREMAKE_API bool ShouldTraceState(int32 PersonState);

/** `SimCopter.People.TraceOpcodes` - one line per VM record executed. Default 0. */
SIMCOPTERREMAKE_API bool AreOpcodesTraced();

/** Seconds since the first line, so a session reads as elapsed time rather than wall clock. */
SIMCOPTERREMAKE_API double GetTraceSeconds();

SIMCOPTERREMAKE_API void Emit(const FString& Line);

/** Human-readable person state, for the log prefix. */
SIMCOPTERREMAKE_API const TCHAR* GetPersonStateName(int32 PersonState);
}

/**
 * These two macros are ALSO the replay clip's decision feed.
 *
 * Every shipped behaviour-VM decision site already calls one of them, so teeing them into
 * `SimCopterReplay::RecordEvent` gives a replay clip the "what did this actor decide" track without
 * a second call site anywhere. The two conditions are independent: the log answers to
 * `SimCopter.People.Trace` and its state filter, while the replay answers only to "is a clip
 * recording" - a clip should carry the whole city's decisions, not just the states someone happened
 * to be debugging. The line is formatted once and shared when both want it.
 *
 * `ASimCopterGroundAgent::Tick` opens a `SimCopterReplay::FScopedEventSource` around its behaviour
 * update, which is what attributes a line printed six frames deep in the VM to the right track.
 *
 * Nothing is formatted when neither wants it, so the cost with the trace off and no clip recording
 * is two loads and a branch - the same as before.
 */

// Always has a format string, so __VA_ARGS__ is never empty (MSVC's conforming preprocessor).
#define SIMCOPTER_PEOPLE_TRACE(PersonState, ...) \
	do \
	{ \
		const bool bSimCopterTraceWanted = \
			SimCopterPeopleTrace::IsEnabled() && SimCopterPeopleTrace::ShouldTraceState(PersonState); \
		const bool bSimCopterReplayWanted = SimCopterReplay::IsRecordingEvents(); \
		if (bSimCopterTraceWanted || bSimCopterReplayWanted) \
		{ \
			const FString SimCopterTraceLine = FString::Printf(__VA_ARGS__); \
			if (bSimCopterTraceWanted) \
			{ \
				SimCopterPeopleTrace::Emit(SimCopterTraceLine); \
			} \
			if (bSimCopterReplayWanted) \
			{ \
				SimCopterReplay::RecordEvent( \
					SimCopterReplay::EReplayEventKind::PersonDecision, \
					SimCopterTraceLine, \
					nullptr, \
					(PersonState)); \
			} \
		} \
	} while (0)

// The opcode trace fires once per VM record - thousands a second across a city - so the replay half
// is behind its own switch (`SimCopter.Replay.RecordOpcodes`, off by default) rather than riding on
// "a clip is recording" the way the decision trace does.
#define SIMCOPTER_PEOPLE_TRACE_OP(PersonState, ...) \
	do \
	{ \
		const bool bSimCopterTraceWanted = \
			SimCopterPeopleTrace::IsEnabled() && \
			SimCopterPeopleTrace::AreOpcodesTraced() && \
			SimCopterPeopleTrace::ShouldTraceState(PersonState); \
		const bool bSimCopterReplayWanted = SimCopterReplay::IsRecordingOpcodeEvents(); \
		if (bSimCopterTraceWanted || bSimCopterReplayWanted) \
		{ \
			const FString SimCopterTraceLine = FString::Printf(__VA_ARGS__); \
			if (bSimCopterTraceWanted) \
			{ \
				SimCopterPeopleTrace::Emit(SimCopterTraceLine); \
			} \
			if (bSimCopterReplayWanted) \
			{ \
				SimCopterReplay::RecordEvent( \
					SimCopterReplay::EReplayEventKind::PersonOpcode, \
					SimCopterTraceLine, \
					nullptr, \
					(PersonState)); \
			} \
		} \
	} while (0)
