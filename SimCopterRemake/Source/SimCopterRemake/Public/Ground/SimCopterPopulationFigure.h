// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Formats/MaxisTextureReader.h"
#include "Formats/SimCopterPrivAnimReader.h"

class UProceduralMeshComponent;

// Process-wide shared original-asset data for figure building: the parsed privanim model, the
// GEO CMAP palette (the game's global 256-color palette), and the decoded SIM3D.BMP head
// images. Loaded once per original-game root and shared by every pedestrian.
struct FSimCopterPrivAnimShared
{
	FPrivAnimModel Model;
	TArray<FColor> Palette;
	TMap<int32, FMaxisTextureImage> HeadImages; // keyed by SIM3D.BMP image id
};

// Builds SimCopter's original `privanim.df` pedestrian figures as animated procedural meshes.
//
// The original drew each figure per frame as depth-sorted 2D strokes: a thick palette-colored
// line per body part, dots for hands/feet details, and a rotated photo-head sprite from
// SIM3D.BMP (see Docs/OriginalGameFileFormats.md "The figure renderer"). The remake rebuilds
// the same data in 3D: an oriented box per line segment (thick/thin per part type), small
// cubes for dot parts, and a forward-facing textured card for the head part. Each animation
// frame is emitted as its own pair of mesh sections (body + head) so playback just toggles
// section visibility.
//
// Colors reproduce the original palette math: entry `0x24 + color*0x10` in the shared GEO
// CMAP palette, with recolorable parts offset by the per-person clothes index modulo 14.
class SIMCOPTERREMAKE_API FSimCopterPopulationFigure
{
public:
	// Loads (or returns the cached) shared figure data for an original-game root. Returns null
	// with OutError set when privanim.df or the GEO palette is unavailable.
	static TSharedPtr<FSimCopterPrivAnimShared> GetShared(const FString& OriginalGameRoot, FString& OutError);

	// SIM3D.BMP image ids for the pedestrian head sprites (the original's DAT_0058f0e0 table,
	// indexed by the per-person head selector).
	static const TArray<int32>& GetHeadImageTable();

	// Palette ramp math from the original per-part primitive dispatch (FUN_004cf8f0).
	static FLinearColor ResolvePartColor(const TArray<FColor>& Palette, const FPrivAnimPart& Part, int32 ClothesOffset);

	struct FBuildParams
	{
		// World height of a standing figure (already includes any population scale).
		float HeightCm = 44.0f;
		// Per-person clothes color offset, 0..13.
		int32 ClothesOffset = 0;
		// Stroke sizes as fractions of HeightCm (the original used screen-space pixel widths;
		// these defaults match its chunky proportions and are tunable).
		float ThickWidthFraction = 0.085f;
		float ThinWidthFraction = 0.04f;
		float DotSizeFraction = 0.065f;
		float HeadSizeFraction = 0.26f;
		// Draw parts whose LOD bitmask contains this bit (bit0 = the original's nearest band).
		uint8 LodBit = 1;
		// When false (no head texture available) the head part is drawn as a colored cube in
		// the body section instead of a textured card section.
		bool bTexturedHead = true;
	};

	// Model-units -> world mapping derived from a standing clip (so lying-down clips keep scale).
	struct FCalibration
	{
		float ScaleCmPerUnit = 1.0f;
		float FeetOffsetCm = 0.0f; // added to Z after scaling so the feet rest at local Z=0
	};

	static FCalibration Calibrate(const FPrivAnimClip& StandingClip, float HeightCm);

	// Emits two mesh sections per frame into the component:
	//   section Frame*2     = body strokes (vertex-colored)
	//   section Frame*2 + 1 = head card (textured; empty if the figure has no head part)
	// Only frame 0 is left visible. Returns false when the clip/figure mismatch.
	static bool BuildClipSections(
		UProceduralMeshComponent* MeshComponent,
		const FPrivAnimFigure& Figure,
		const FPrivAnimClip& Clip,
		const TArray<FColor>& Palette,
		const FBuildParams& Params,
		const FCalibration& Calibration,
		bool& bOutHasHeadSection);

	// Shows exactly one frame of a clip built by BuildClipSections.
	static void ShowFrame(UProceduralMeshComponent* MeshComponent, int32 FrameCount, int32 FrameIndex, bool bHasHeadSection);
};
