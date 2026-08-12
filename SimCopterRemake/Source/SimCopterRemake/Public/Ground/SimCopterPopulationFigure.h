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
// The original drew each figure per frame as depth-sorted 2D primitives: a constant-width
// stroke per limb, a tapered stroke for hair/tails, a filled disc for rounded masses (hands,
// chests, hats, and every part of the dog and Nessie), and a rotated photo-head sprite from
// SIM3D.BMP (see Docs/OriginalGameFileFormats.md "The figure renderer"). The remake rebuilds
// the same data in 3D: an oriented box per line segment, a tapered box for thin lines, a ball
// for the disc primitives, and a textured ellipsoid for the head. Each animation frame is
// emitted as its own pair of mesh sections (body + head) so playback just toggles section
// visibility.
//
// Every primitive's size comes from the part's own ARCP dimension floats, exactly as the
// original's per-part dispatch reads them (FUN_004cf8f0); those numbers are screen pixels
// there and model units here, so the proportions come out the same.
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
		// Uniform fudge on every primitive's thickness. Part sizes are data-driven, so this is
		// only a global "chunkiness" dial; 1.0 is the original's proportions.
		float PartSizeScale = 1.0f;
		// Floor on a primitive's model-space size, so a part authored with zero dimensions
		// still shows up instead of collapsing to nothing.
		float MinPartSizeUnits = 0.75f;
		// The original's strokes are screen-space lines with no depth at all. A 3D box has to
		// pick one, and a square cross-section makes a 7-unit-wide torso 7 units deep, so bodies
		// bulge front and back in a way the original never did (its skeletons are close to
		// planar, so pedestrians really are thin side-on). Squash the stroke cross-section along
		// the figure's forward axis to bring the proportions back. Round primitives are exempt:
		// they were screen-space discs, so they have to read round from every angle.
		float StrokeForwardDepthRatio = 0.6f;
		// The original had no depth buffer: parts were sorted back-to-front and simply
		// overpainted each other, so figures carry parts that lie exactly on top of one another
		// (the fill and outline strokes down a torso, the trim on a sleeve). Against a depth
		// buffer those coincide and z-fight. Growing each part by a hair in ARCP order restores
		// a deterministic winner - later, detail parts sit fractionally proud of the earlier,
		// bulk ones - without a runtime sort. Total spread across all parts, as a fraction of
		// HeightCm; small enough to be invisible, large enough to beat depth precision at the
		// range a pedestrian is actually legible.
		float PainterBiasFraction = 0.004f;
		// U offset of the face within the panorama strip (tune so the face looks forward).
		float HeadFaceU = 0.5f;
		// Draw parts whose LOD bitmask contains this bit. The original picks the mask from the
		// figure's screen depth (FUN_004c7f10): 1 nearest, 2 and 4 progressively coarser, so
		// bit0 is the full-detail set and bit2-only parts are far-away stand-ins.
		uint8 LodBit = 1;
		// When false (no head texture available) the head part is drawn as a colored ball in
		// the body section instead of a textured head section.
		bool bTexturedHead = true;
	};

	// Model-units -> world mapping derived from a standing clip (so lying-down clips keep scale).
	struct FCalibration
	{
		float ScaleCmPerUnit = 1.0f;
		float FeetOffsetCm = 0.0f; // added to Z after scaling so the feet rest at local Z=0
	};

	static FCalibration Calibrate(const FPrivAnimClip& StandingClip, float HeightCm);

	/**
	 * How far below the standing feet plane this clip's art reaches, in cm (0 when none of it does).
	 *
	 * The calibration above pins local Z=0 to the feet **of the standing clip**, because scale has to
	 * come from one pose or every clip would be a different size. Nothing then holds any *other*
	 * pose above that plane, and a pose drawn lower on the 1996 screen maps straight down through the
	 * floor: `ToLocal` negates the model's screen-space Y-down vertical, so a vertex further down the
	 * screen than the standing clip's feet lands at a negative local Z. The lying poses ("Dead",
	 * "Inju", "Slum") are all drawn low - a body on the ground is at the bottom of its cell - which is
	 * why a casualty, or anyone knocked over by a car, was buried to the shoulders in the pavement
	 * with only their head showing.
	 *
	 * Measured over every frame of the clip. Note that this is NOT zero for the calibration clip
	 * itself: `Calibrate` reads frame 0 only, and a walk cycle swings its feet 2-3 cm below that
	 * frame's plane at population scale. That swing is the ground a walking figure already visibly
	 * stands on, which is why the lift below is measured against it rather than against zero.
	 *
	 * Endpoint-based: the primitive drawn AT the lowest endpoint still extends below it by its own
	 * radius, a couple of millimetres at population scale.
	 */
	static float ComputeClipDropBelowFeetCm(const FPrivAnimClip& Clip, const FCalibration& Calibration);

	/**
	 * How far to raise this pose so it rests on the same ground the walking figure already does:
	 * how much deeper it reaches than the clip the calibration came from, and never negative.
	 *
	 * Zero for the calibration clip itself and for any pose drawn no lower than it, so binding an
	 * ordinary walk or idle moves a pedestrian by nothing at all. The lying poses are what this is
	 * for.
	 */
	static float ComputeClipGroundLiftCm(
		const FPrivAnimClip& Clip,
		const FPrivAnimClip& CalibrationClip,
		const FCalibration& Calibration);

	// Emits two mesh sections per frame into the component:
	//   section Frame*2     = body primitives (vertex-colored)
	//   section Frame*2 + 1 = head ellipsoid (textured; empty if the figure has no head part)
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
