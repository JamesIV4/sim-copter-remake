// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "UI/SimCopterSegmentedBar.h"

// The cockpit's tool control flaps, decoded from the original.
//
// Each tool the career owns adds one flap to the cockpit - "Each tool has its own control panel,
// and is automatically installed in your helicopter when you buy the tool. When you sell a tool,
// the control flap is removed from the cockpit." (help 31ref.htm).
//
// FUN_004127d0 builds them. It reads the equipment mask at career + 0x48 and, for each bit,
// constructs a flap from one 138x58 bitmap keyed on palette index 254:
//
//   flap0.bmp  mask 0x11  water bucket (0x01) or water cannon (0x10) - the pair share a flap
//   flap1.bmp  mask 0x02  megaphone
//   flap2.bmp  mask 0x04  rescue harness
//   flap3.bmp  mask 0x08  tear gas launcher
//
// The original pins them to fixed screen rectangles down the right edge of its 640x480 display -
// (502,24), (502,90), (502,156), (502,222), a 66px stride - so a tool the player does not own
// leaves a hole. The remake stacks the flaps it has instead (see SSimCopterToolFlaps), which is
// the one deliberate departure here.
//
// FUN_00454ee0 is the flap constructor, and it is where the per-flap click boxes live: it
// switches on the flap index and writes up to four rectangles at this+0x1d. Every rectangle below
// is that table, and every art origin was confirmed by matching the button sprite against the
// page bitmap pixel for pixel.
namespace SimCopterFlapLayout
{
// Every flap page is this size, and every rectangle below is in page space.
constexpr int32 PageWidth = 138;
constexpr int32 PageHeight = 58;

// Where FUN_004127d0 puts the flaps on the original's 640x480 screen. Unused by the remake's
// stacked layout; kept because it is the measurement the page size and stride come from.
constexpr int32 OriginalScreenLeft = 502;
constexpr int32 OriginalScreenTop = 24;
constexpr int32 OriginalScreenStride = 66;

// What a flap's button does. The water flap carries the bucket and the cannon together, so the
// action - not the flap - is what identifies a control.
enum class EAction : uint8
{
	// flap0
	CannonFire,
	BucketDump,
	BucketRaise,
	BucketLower,
	// flap1
	MegaphoneBroadcast,
	// flap2
	HarnessRaise,
	HarnessLower,
	// flap3
	TearGasFire,
};

// The three button strips. Frames are packed left to right at unequal widths:
//
//   flapbtn0.bmp  74x29  rocker 17x29 x2, then the water drop 20x29 x2
//   flapbtn1.bmp  38x24  the plain octagon 17x24 x2, then a 4x24 sliver the flap prints itself
//   flapbtn2.bmp  34x29  the rocker again - byte for byte the first two frames of flapbtn0
//
// Only the pressed frame is ever drawn: the unpressed button is already painted on the page.
struct FButtonArt
{
	const TCHAR* FileName;
	FIntRect PressedFrame;
};

// One control on a flap.
struct FButton
{
	EAction Action;

	// The click box FUN_00454ee0 writes, right and bottom exclusive.
	FIntRect Hit;

	// Where the pressed sprite goes. It is not Hit.Min: the click boxes are padded by a pixel
	// or two, and a rocker's two halves share one full-height sprite.
	FIntPoint ArtOrigin;

	FButtonArt Art;

	// The tool that has to be aboard for this control to do anything. The water flap appears
	// when either water tool is owned, so its four buttons are gated one at a time.
	ESimCopterHelicopterTool Tool;
};

// One flap.
struct FFlap
{
	const TCHAR* PageFileName;

	// The bits FUN_004127d0 tests before it builds the flap at all.
	int32 EquipmentMask;

	TArrayView<const FButton> Buttons;
};

// The ten canisters printed across flap3 are the tear gas round counter, and the flap keeps them
// up to date itself. The tick at vtable +0xd0 (0x00455300, one of the methods Ghidra folded into
// the gap after FUN_00454ee0 - disassemble it, `decompile` fails) runs every fourth call: flap
// index 3 reads career + 0x54, and when it differs from the count it last drew it calls
// 0x00455790 to repaint the row.
//
// 0x00455790 blits a 4x4 dot per canister from flapbtn1.bmp's right-hand sliver:
//
//   full  (34,0)-(38,4)   pale grey     drawn for the last `rounds` lamps
//   empty (34,4)-(38,8)   near black    drawn for the first `10 - rounds` lamps
//
// so the row empties from the left. flap3.bmp already prints all ten lamps full - the pixels at
// (18,12) are byte for byte the full sprite - which is why the original only ever paints the dark
// ones, and why the remake does the same instead of drawing both states.
namespace CanisterCounter
{
constexpr int32 LampCount = 10;
constexpr int32 LampColumns = 5;
constexpr int32 LampSize = 4;

// x = 18 + 12 * (i % 5), y = 12 + 13 * (i / 5), straight out of the blit loop.
constexpr int32 LampOriginX = 18;
constexpr int32 LampOriginY = 12;
constexpr int32 LampPitchX = 12;
constexpr int32 LampPitchY = 13;

SIMCOPTERREMAKE_API FIntPoint GetLampOrigin(int32 Index);

// The sliver both states come from. flapbtn1 is the megaphone/tear gas button sheet, so this art
// is already loaded by the time the counter needs it.
SIMCOPTERREMAKE_API const TCHAR* GetLampFileName();
SIMCOPTERREMAKE_API FIntRect GetLampFullFrame();
SIMCOPTERREMAKE_API FIntRect GetLampEmptyFrame();

// True when lamp Index should be painted over with the dark sprite.
inline bool IsLampEmpty(const int32 Index, const int32 Rounds)
{
	return Index < LampCount - FMath::Clamp(Rounds, 0, LampCount);
}
}

// flap0's water meter, the counter's sibling. The same tick (0x00455300) reads the bucket load
// every fourth frame - `heli[0x74] * 11 / maxLoad[type]` - and on a change calls 0x00455700 to
// repaint eleven 5x10 cells starting at (16, 0x2b), each blitted from watergge.bmp:
//
//   full  (0,0)-(5,10)     pale blue water    for the first `level` cells
//   edge  (5,0)-(10,10)    the meniscus       for exactly one cell, unless the tank is full
//   empty (10,0)-(15,10)   flat near-black    for the rest
//
// watergge.bmp is a fifth bitmap only flap0 loads (0x00455170 puts it on this+0xbc, the button
// sheet on this+0xc0). Unlike the canister lamps, all three states have to be drawn: flap0.bmp
// ships the *empty* gauge - the meniscus is printed at x 16 and the rest is the empty cell,
// byte for byte - so a partly full tank has to cover the printed meniscus as well.
// The three-state strip and the fill rule are shared with dash6's points bar; only the geometry
// below is flap0's own. See UI/SimCopterSegmentedBar.h.
namespace WaterGauge
{
constexpr int32 CellCount = 11;
constexpr int32 CellWidth = 5;
constexpr int32 CellHeight = 10;
constexpr int32 OriginX = 16;
constexpr int32 OriginY = 43; // 0x2b

using ECell = SimCopterSegmentedBar::ECell;

SIMCOPTERREMAKE_API const TCHAR* GetGaugeFileName();

inline FIntRect GetCellFrame(const ECell Cell)
{
	return SimCopterSegmentedBar::GetCellFrame(Cell, CellWidth, CellHeight);
}

// The tick's `load * 11 / maxLoad`.
inline int32 GetLevel(const int32 WaterPounds, const int32 MaxLoadPounds)
{
	return SimCopterSegmentedBar::GetLevel(WaterPounds, MaxLoadPounds, CellCount);
}

inline ECell GetCellState(const int32 Index, const int32 Level)
{
	return SimCopterSegmentedBar::GetCellState(Index, Level);
}

inline FIntPoint GetCellOrigin(const int32 Index)
{
	return FIntPoint(OriginX + CellWidth * FMath::Clamp(Index, 0, CellCount - 1), OriginY);
}
}

// The four flaps in the order FUN_004127d0 builds them, which is also top-to-bottom order.
SIMCOPTERREMAKE_API TArrayView<const FFlap> GetFlaps();

// Human-readable name for a control, for tooltips.
SIMCOPTERREMAKE_API const TCHAR* GetActionName(EAction Action);
}
