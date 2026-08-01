// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SimCopterSegmentedBar.h"

namespace SimCopterSegmentedBar
{
FIntRect GetCellFrame(const ECell Cell, const int32 CellWidth, const int32 CellHeight)
{
	const int32 Column =
		Cell == ECell::Full ? 0 :
		Cell == ECell::LeadingEdge ? 1 : 2;
	return FIntRect(Column * CellWidth, 0, (Column + 1) * CellWidth, CellHeight);
}

int32 GetLevel(const int32 Amount, const int32 Capacity, const int32 CellCount)
{
	if (Capacity <= 0 || CellCount <= 0)
	{
		return 0;
	}
	return FMath::Clamp((FMath::Max(Amount, 0) * CellCount) / Capacity, 0, CellCount);
}

ECell GetCellState(const int32 Index, const int32 Level)
{
	// Loop one covers everything below the level, loop two lays down a single meniscus, loop
	// three fills the rest.
	if (Index < Level)
	{
		return ECell::Full;
	}
	return Index == Level ? ECell::LeadingEdge : ECell::Empty;
}
}
