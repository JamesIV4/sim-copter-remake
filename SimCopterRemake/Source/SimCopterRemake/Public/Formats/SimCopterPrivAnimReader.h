// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Pure parser for SimCopter's `X/privanim.df` articulated pedestrian figures.
//
// The format was fully reverse engineered from the decompiled reader in SimCopter.exe and is
// documented in `Docs/OriginalGameFileFormats.md` ("Exact Container Spec" + "The figure
// renderer"); the Python reference implementation is `Tools/privanim_extract.py`, and the decode
// was validated byte-for-byte against the running original game (`Tools/privanim_live_oracle.py`).
//
// Summary of the model:
//  * 21 named figures (pilot, Child, Kopp, Elvis, Nessie, ... Woman).
//  * Per figure, an ARCP "skeleton" of parts. Each part has a draw type (thick line / thin
//    line / dots / textured head sprite), a palette color index, an LOD bitmask, a
//    recolorable flag, and a parent link used for endpoint chaining.
//  * Per figure, an ARLU table mapping the 18 behavior animation mnemonics ("1Wal", "NoMo",
//    "Dead", "Wave", ...) to clip names ("101!".."495!").
//  * Per clip, an ARPP array of frames x parts 8-byte records; each record is the part's line
//    segment for that frame: two signed-byte (x, y, z) endpoints (z up). The 4th byte of the
//    first endpoint is the head sprite's roll angle for type-9 parts.
struct FPrivAnimEndpoint
{
	int8 X = 0;
	int8 Y = 0;
	int8 Z = 0; // up
	uint8 Extra = 0;
};

// One body part's line segment for one frame.
struct FPrivAnimSegment
{
	FPrivAnimEndpoint A;
	FPrivAnimEndpoint B;
};

// ARCP part draw types (dispatch in the original's FUN_004cf8f0).
namespace EPrivAnimPartType
{
	constexpr uint8 None = 0;
	constexpr uint8 DotStyle1 = 0x08;
	constexpr uint8 HeadSprite = 0x09; // rotated textured sprite from SIM3D.BMP
	constexpr uint8 ThinLine = 0x0A;
	constexpr uint8 ThickLine = 0x0B; // the fleshed limb/torso stroke
	constexpr uint8 Pixel = 0x0C;
	constexpr uint8 DotStyle2 = 0x0D;
	constexpr uint8 DotStyle0 = 0x0E;
}

struct FPrivAnimPart
{
	uint8 Type = 0;        // EPrivAnimPartType
	uint8 Ref = 0;
	uint8 Seq = 0;         // sequential transform-cache index in the original
	uint8 ColorIndex = 0;  // palette entry = 0x24 + color*0x10
	uint8 LodMask = 0;     // bit set per LOD band the part draws in (bit0 = nearest)
	uint8 FixedColor = 0;  // 0 = recolored per person (clothes variation)
	FString Name;          // 4-char node name ("New ", "Ne0 ", ...)
	FString Parent;        // 4-char parent node name (empty/zero for the root)
	int32 ParentIndex = INDEX_NONE;
	FVector3f Dims = FVector3f::ZeroVector; // authoring-tool dimensions (unused by the draw)
};

struct FPrivAnimClip
{
	FString Name;        // e.g. "101!"
	int32 FrameCount = 0;
	int32 PartCount = 0;
	// FrameCount * PartCount segments, frame-major.
	TArray<FPrivAnimSegment> Segments;

	const FPrivAnimSegment& Segment(int32 Frame, int32 Part) const
	{
		return Segments[Frame * PartCount + Part];
	}
};

struct FPrivAnimFigure
{
	FString Name; // e.g. "pilot", "Kopp", "2woman"
	TArray<FPrivAnimPart> Parts;
	// The 18 behavior anim mnemonics -> clip index into FPrivAnimModel::Clips.
	TMap<FString, int32> ClipIndexByMnemonic;
};

struct FPrivAnimModel
{
	TArray<FPrivAnimFigure> Figures;
	TArray<FPrivAnimClip> Clips;

	int32 FindFigureIndex(const FString& FigureName) const;
	const FPrivAnimClip* FindClip(const FPrivAnimFigure& Figure, const FString& Mnemonic) const;
};

class SIMCOPTERREMAKE_API FSimCopterPrivAnimReader
{
public:
	// Resolves `<OriginalGameRoot>/X/privanim.df`.
	static FString ResolvePrivAnimPath(const FString& OriginalGameRoot);

	static bool LoadFromFile(const FString& FilePath, FPrivAnimModel& OutModel, FString& OutError);
	static bool LoadFromBytes(const TArray<uint8>& FileData, FPrivAnimModel& OutModel, FString& OutError);
};
