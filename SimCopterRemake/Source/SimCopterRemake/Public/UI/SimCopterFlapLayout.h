// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/SimCopterHelicopterRegistry.h"

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

// The four flaps in the order FUN_004127d0 builds them, which is also top-to-bottom order.
SIMCOPTERREMAKE_API TArrayView<const FFlap> GetFlaps();

// Human-readable name for a control, for tooltips.
SIMCOPTERREMAKE_API const TCHAR* GetActionName(EAction Action);
}
