// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Formats/MaxisProceduralMeshBuilder.h"

// Everything a live model switch needs, built off to the side so a failed load can never
// leave one model's tuning on another model's mesh (plan section 7).
//
// The original engine never had to solve this: FUN_00483c20 builds a fresh helicopter
// object per purchase and the old one is destroyed. The remake reuses one pawn, so the
// staged data is the substitute for that fresh construction.
struct FSimCopterPreparedHelicopterModel
{
	const FSimCopterHelicopterDefinition* Definition = nullptr;

	// Parsed heli.twk values for Definition->TweakSection.
	FSimCopterHelicopterTypeTuning HelicopterTuning;
	FSimCopterLandingTuning LandingTuning;
	FSimCopterRopeTuning RopeTuning;
	FSimCopterDamageTuning DamageTuning;
	bool bTuningLoaded = false;

	// Resolved geometry. Rotors keep their face-type-11 blur disc in a separate section so
	// the RPM >= 300 toggle still works after a switch.
	FMaxisMeshSection BodySection;
	FMaxisMeshSection MainRotorOpaqueSection;
	FMaxisMeshSection MainRotorDiscSection;
	FMaxisMeshSection TailRotorOpaqueSection;
	FMaxisMeshSection TailRotorDiscSection;
	FMaxisMeshSection BucketSection;
	FMaxisMeshSection HarnessSection;

	bool bHasMainRotor = false;
	bool bHasTailRotor = false;
	bool bHasBucket = false;
	bool bHasHarness = false;

	// DAT_005040e4 + 0x2c..0x34 converted to the pawn's centimetre space.
	FVector TailRotorOffsetCm = FVector::ZeroVector;

	// Every problem found while preparing; empty means a clean load.
	TArray<FString> Errors;

	bool HasBody() const { return Definition != nullptr && !BodySection.IsEmpty(); }

	FString DescribeErrors() const { return FString::Join(Errors, TEXT("; ")); }
};
