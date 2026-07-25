// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterInteraction.h"

namespace
{
// SCHOOK: PersonReactionTable 0x004c3010 (DAT_0058d728)
// Verified against the shipped X/people.df BHAV directory; names in the comments are the
// resource names, not guesses.
constexpr int32 InvalidReaction = INDEX_NONE;

const int32 PersonReactionTable[ESimCopterInteractionMode::Count] = {
	910,             //  0 Rxn: Debris stuff hit
	900,             //  1 Rioter react to spotlight (overridden by BHAV 950 in FUN_004c1050)
	901,             //  2 Rxn: Megaphone
	915,             //  3 Rxn: Missile/bullet
	908,             //  4 Rxn: Water
	907,             //  5 Rxn: Teargas
	913,             //  6 Rxn: Fire/sparks hit
	915,             //  7 Rxn: Missile/bullet
	911,             //  8 Rxn: boat hit
	912,             //  9 Rxn: Large fast vehicle hit
	912,             // 10 Rxn: Large fast vehicle hit
	912,             // 11 Rxn: Large fast vehicle hit
	912,             // 12 Rxn: Large fast vehicle hit
	914,             // 13 Rxn: Person--civil, neutral
	910,             // 14 Rxn: Debris stuff hit
	913,             // 15 Rxn: Fire/sparks hit
	909,             // 16 Rxn: Fall
	InvalidReaction, // 17 (0xffff)
	InvalidReaction, // 18 (0xffff)
	InvalidReaction, // 19 (0xffff)
};
}

namespace SimCopterInteraction
{
int32 GetPersonReactionProgram(int32 Mode)
{
	if (Mode < 0 || Mode >= ESimCopterInteractionMode::Count)
	{
		return InvalidReaction;
	}
	return PersonReactionTable[Mode];
}

bool IsHighPriorityReaction(int32 ProgramId)
{
	// FUN_004c1050: sVar2/sVar1 comparisons against 0x387, 0x393, 0x390, 0x38d.
	return ProgramId == 903 || ProgramId == 915 || ProgramId == 912 || ProgramId == 909;
}

bool ReactionCanInterrupt(int32 NewProgramId, int32 CurrentProgramId)
{
	if (NewProgramId == InvalidReaction)
	{
		return false;
	}
	if (CurrentProgramId == InvalidReaction)
	{
		// Nothing is running - FUN_004c1050's person[0x57] < 1 branch.
		return true;
	}
	if (NewProgramId == CurrentProgramId)
	{
		// The original refuses to restart the reaction that is already playing.
		return false;
	}
	// A new reaction only wins when it is one of the four priority ids and the running one
	// is not.
	return IsHighPriorityReaction(NewProgramId) && !IsHighPriorityReaction(CurrentProgramId);
}

int32 GetSpiralRingsForMode(int32 Mode)
{
	if (Mode == ESimCopterInteractionMode::Spotlight)
	{
		return SpotlightRings;
	}
	if (Mode == ESimCopterInteractionMode::Megaphone)
	{
		return MegaphoneRings;
	}
	return 0;
}

// SCHOOK: InteractionSpiralScan 0x0048ae70
void BuildSpiralTiles(const FIntPoint& CenterTile, int32 Rings, TArray<FIntPoint>& OutTiles)
{
	OutTiles.Reset();
	if (Rings <= 0)
	{
		return;
	}

	// Locals named after the decompile so the port can be diffed against it directly:
	// local_24 = run length, local_18 = leg index, local_20/local_1c = step vector.
	int32 RunLength = 0;
	int32 LegIndex = -1;
	int32 StepX = 0;
	int32 StepY = 0;
	int32 CursorX = CenterTile.X;
	int32 CursorY = CenterTile.Y;
	bool bFinalLeg = false;

	// The original has no iteration cap; the run length is strictly increasing so the loop
	// always reaches bFinalLeg. The guard here only protects against an absurd Rings value.
	for (int32 Guard = 0; Guard < 1024; ++Guard)
	{
		++LegIndex;
		switch (LegIndex)
		{
		case 0:
		case 4:
			++RunLength;
			LegIndex = 0;
			StepX = 0;
			StepY = -1;
			break;
		case 1:
			StepX = 1;
			StepY = 0;
			break;
		case 2:
			StepX = 0;
			StepY = 1;
			++RunLength;
			break;
		case 3:
			StepX = -1;
			StepY = 0;
			break;
		default:
			break;
		}

		if (RunLength == Rings)
		{
			bFinalLeg = true;
			--RunLength;
		}

		for (int32 Remaining = RunLength; Remaining > 0; --Remaining)
		{
			OutTiles.Add(FIntPoint(CursorX, CursorY));
			CursorX += StepX;
			CursorY += StepY;
		}

		if (bFinalLeg)
		{
			return;
		}
	}
}
}
