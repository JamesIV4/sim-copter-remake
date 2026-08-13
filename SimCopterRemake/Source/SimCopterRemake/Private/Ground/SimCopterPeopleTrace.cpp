// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterPeopleTrace.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY(LogSimCopterPeople);

namespace
{
TAutoConsoleVariable<int32> CVarPeopleTrace(
	TEXT("SimCopter.People.Trace"),
	0,
	TEXT("Trace people behaviour programs to LogSimCopterPeople: object selections, goto-object\n")
	TEXT("steps and what refused them, reaction pushes, mission outcomes and despawns.\n")
	TEXT("Off by default and free when off. Narrow it with SimCopter.People.TraceStates."),
	ECVF_Default);

TAutoConsoleVariable<FString> CVarPeopleTraceStates(
	TEXT("SimCopter.People.TraceStates"),
	TEXT("8,10,11,12,13"),
	TEXT("Which person states to trace, comma separated; * for all.\n")
	TEXT("0 ambient, 2 rooftop victim, 3 rioter, 4 transport fare, 5 hospital worker,\n")
	TEXT("6 medevac patient, 7 aerial cop, 8 foot cop, 10 robber, 11 arsonist, 12 mugger,\n")
	TEXT("13 burglar. The default is the four criminals plus the foot cop that arrests them."),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarPeopleTraceOpcodes(
	TEXT("SimCopter.People.TraceOpcodes"),
	0,
	TEXT("Also log every VM record as it executes, with its opcode and result. Chatty - one or two\n")
	TEXT("lines per person per behaviour tick - but it is the only way to see which edge a program\n")
	TEXT("actually took."),
	ECVF_Default);

double GTraceStartSeconds = 0.0;

// Parsed from the CVar string and rebuilt only when it changes, because the test runs per traced
// line and the string does not.
FString GCachedStateFilter;
TArray<int32> GTracedStates;
bool bGTraceAllStates = false;

void RefreshStateFilter()
{
	const FString Value = CVarPeopleTraceStates.GetValueOnAnyThread();
	if (Value == GCachedStateFilter && (!GTracedStates.IsEmpty() || bGTraceAllStates))
	{
		return;
	}

	GCachedStateFilter = Value;
	GTracedStates.Reset();
	bGTraceAllStates = false;

	FString Trimmed = Value.TrimStartAndEnd();
	if (Trimmed.IsEmpty() || Trimmed == TEXT("*"))
	{
		bGTraceAllStates = true;
		return;
	}

	TArray<FString> Parts;
	Trimmed.ParseIntoArray(Parts, TEXT(","), true);
	for (const FString& Part : Parts)
	{
		const FString One = Part.TrimStartAndEnd();
		if (One.IsNumeric())
		{
			GTracedStates.AddUnique(FCString::Atoi(*One));
		}
	}
	// A filter that parsed to nothing would silently trace nobody, which reads exactly like the
	// trace being broken. Treat it as "everyone" instead.
	bGTraceAllStates = GTracedStates.IsEmpty();
}
}

namespace SimCopterPeopleTrace
{
bool IsEnabled()
{
	return CVarPeopleTrace.GetValueOnAnyThread() != 0;
}

bool ShouldTraceState(const int32 PersonState)
{
	RefreshStateFilter();
	return bGTraceAllStates || GTracedStates.Contains(PersonState);
}

bool AreOpcodesTraced()
{
	return CVarPeopleTraceOpcodes.GetValueOnAnyThread() != 0;
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

const TCHAR* GetPersonStateName(const int32 PersonState)
{
	// DAT_0058de80's states, by the program each one starts on.
	switch (PersonState)
	{
	case 0:  return TEXT("ambient");
	case 1:  return TEXT("rescue");
	case 2:  return TEXT("roof-victim");
	case 3:  return TEXT("rioter");
	case 4:  return TEXT("transport");
	case 5:  return TEXT("hospital");
	case 6:  return TEXT("medevac");
	case 7:  return TEXT("cop-air");
	case 8:  return TEXT("cop-foot");
	case 9:  return TEXT("fireman");
	case 10: return TEXT("robber");
	case 11: return TEXT("arsonist");
	case 12: return TEXT("mugger");
	case 13: return TEXT("burglar");
	case 17: return TEXT("band-leader");
	case 18: return TEXT("band-member");
	default: return TEXT("state");
	}
}

void Emit(const FString& Line)
{
	// The engine stamps wall clock and frame; this relative clock is what makes "they stopped six
	// seconds after the arrest" readable without arithmetic.
	UE_LOG(LogSimCopterPeople, Log, TEXT("[%8.2fs] %s"), GetTraceSeconds(), *Line);
}
}
