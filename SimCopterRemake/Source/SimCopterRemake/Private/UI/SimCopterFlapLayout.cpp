// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SimCopterFlapLayout.h"

namespace SimCopterFlapLayout
{
namespace
{
using ETool = ESimCopterHelicopterTool;

// --- flap0: water bucket and cannon ---
//
// FUN_00454ee0 case 0 writes four rectangles, in this order:
//   (102,10)-(119,39) split in half at y 24 - the rocker, top and bottom
//   (77,4)-(98,26)                          - the drop button beside the bucket
//   (10,14)-(32,36)                         - the drop button beside the cannon
//
// The page prints, left to right: a drop button, the cannon, the bucket, a second drop button,
// then the rocker. Each drop button therefore sits on the outside of the tool it fires, and the
// rocker sits with the bucket, whose cable it winds - "The water bucket can be filled or dumped
// only when lowered to some degree" (help 32ref.htm).
const FButton WaterButtons[] = {
	// The rocker is one 17x29 sprite carrying both arrowheads, and the click boxes split it at
	// its 14th row. Each half therefore takes half the pressed sprite, so only the arrow under
	// the cursor lights.
	{ EAction::BucketRaise, FIntRect(102, 10, 119, 24), FIntPoint(102, 10), { TEXT("FLAPBTN2.BMP"), FIntRect(17, 0, 34, 14) }, ETool::WaterBucket },
	{ EAction::BucketLower, FIntRect(102, 24, 119, 39), FIntPoint(102, 24), { TEXT("FLAPBTN2.BMP"), FIntRect(17, 14, 34, 29) }, ETool::WaterBucket },
	{ EAction::BucketDump,  FIntRect(77, 4, 98, 26),    FIntPoint(77, 5),   { TEXT("FLAPBTN0.BMP"), FIntRect(54, 0, 74, 29) }, ETool::WaterBucket },
	{ EAction::CannonFire,  FIntRect(10, 14, 32, 36),   FIntPoint(11, 15),  { TEXT("FLAPBTN0.BMP"), FIntRect(54, 0, 74, 29) }, ETool::WaterCannon },
};

// --- flap1: megaphone ---
//
// One rectangle, (77,9)-(102,40), over the octagon printed at (81,12).
const FButton MegaphoneButtons[] = {
	{ EAction::MegaphoneBroadcast, FIntRect(77, 9, 102, 40), FIntPoint(81, 12), { TEXT("FLAPBTN1.BMP"), FIntRect(17, 0, 34, 24) }, ETool::Megaphone },
};

// --- flap2: rescue harness ---
//
// (80,10)-(99,41), split at y 25, over the rocker printed at (81,11).
const FButton HarnessButtons[] = {
	{ EAction::HarnessRaise, FIntRect(80, 10, 99, 25), FIntPoint(81, 11), { TEXT("FLAPBTN2.BMP"), FIntRect(17, 0, 34, 14) }, ETool::RescueHarness },
	{ EAction::HarnessLower, FIntRect(80, 25, 99, 41), FIntPoint(81, 25), { TEXT("FLAPBTN2.BMP"), FIntRect(17, 14, 34, 29) }, ETool::RescueHarness },
};

// --- flap3: tear gas launcher ---
//
// The same single rectangle as the megaphone. The ten canisters printed down the left of the page
// are the round counter - "The tear gas launcher holds ten canisters" (help 35ref.htm).
const FButton TearGasButtons[] = {
	{ EAction::TearGasFire, FIntRect(77, 9, 102, 40), FIntPoint(81, 12), { TEXT("FLAPBTN1.BMP"), FIntRect(17, 0, 34, 24) }, ETool::TearGas },
};

const FFlap Flaps[] = {
	{ TEXT("FLAP0.BMP"), 0x11, WaterButtons },
	{ TEXT("FLAP1.BMP"), 0x02, MegaphoneButtons },
	{ TEXT("FLAP2.BMP"), 0x04, HarnessButtons },
	{ TEXT("FLAP3.BMP"), 0x08, TearGasButtons },
};
}

namespace CanisterCounter
{
FIntPoint GetLampOrigin(const int32 Index)
{
	const int32 Clamped = FMath::Clamp(Index, 0, LampCount - 1);
	return FIntPoint(
		LampOriginX + LampPitchX * (Clamped % LampColumns),
		LampOriginY + LampPitchY * (Clamped / LampColumns));
}

const TCHAR* GetLampFileName()
{
	return TEXT("FLAPBTN1.BMP");
}

FIntRect GetLampFullFrame()
{
	return FIntRect(34, 0, 34 + LampSize, LampSize);
}

FIntRect GetLampEmptyFrame()
{
	return FIntRect(34, LampSize, 34 + LampSize, LampSize * 2);
}
}

namespace WaterGauge
{
const TCHAR* GetGaugeFileName()
{
	// Exactly 15x10 - three 5x10 cells.
	return TEXT("WATERGGE.BMP");
}
}

TArrayView<const FFlap> GetFlaps()
{
	return MakeArrayView(Flaps);
}

const TCHAR* GetActionName(const EAction Action)
{
	switch (Action)
	{
	case EAction::CannonFire: return TEXT("Water cannon");
	case EAction::BucketDump: return TEXT("Dump bucket");
	case EAction::BucketRaise: return TEXT("Raise bucket");
	case EAction::BucketLower: return TEXT("Lower bucket");
	case EAction::MegaphoneBroadcast: return TEXT("Megaphone");
	case EAction::HarnessRaise: return TEXT("Raise harness");
	case EAction::HarnessLower: return TEXT("Lower harness");
	case EAction::TearGasFire: return TEXT("Fire tear gas");
	default: return TEXT("?");
	}
}
}
