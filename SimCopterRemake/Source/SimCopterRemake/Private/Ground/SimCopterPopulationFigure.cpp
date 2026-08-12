// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterPopulationFigure.h"

#include "Formats/MaxisMeshReader.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"

namespace
{
// Per-primitive sizing constants lifted straight out of the original's part dispatch
// (FUN_004cf8f0). There the products are screen pixels; here the same numbers are model units,
// which the calibration converts to centimetres.
//
//   0x0b thick line : width = Dims.Y
//   0x0a thin line  : width = Dims.Y at the start, narrowing towards the end (see below)
//   0x08/0x0d/0x0e  : filled disc, diameter = Dims.X * 1.8      (double at 0x004f5110)
//   0x09 head       : ellipse, vertical radius Dims.X, horizontal radius 0.75x
//   0x0c            : a single pixel
constexpr float DiscDiameterPerDim = 1.8f;
// The thin-line blitter (FUN_004d0f50) shrinks the stroke by two pixels every
// length/(taper+1) steps, with taper = width * (1/3) * Dims.Z, so the far end lands at
// width * (1 - 2/3 * Dims.Z).
constexpr float ThinLineTaperPerDim = 2.0f / 3.0f;
// The head blitter (FUN_004d0b70) walks rows out to +/-radius but takes its row half-widths
// from (0.75 * radius)^2, so the head silhouette is an upright oval, not a circle.
constexpr float HeadWidthRatio = 0.75f;

// Tessellation of the ball primitives. The original drew them as small filled discs, so this
// only has to read as round; keeping it coarse matters because a figure can carry dozens of
// them across every animation frame (the dog and Nessie are made of almost nothing else).
constexpr int32 BallSegments = 8; // around
constexpr int32 BallRings = 5;    // pole to pole

// Model axes (from the decompiled renderer): byte0 = stride/forward, byte1 = lateral,
// byte2 = the vertical in *screen* space, which increases DOWNWARD (1996 software rasterizer),
// so it is negated for Unreal's +Z-up (figures rendered upside down before the flip).
FVector ToLocal(const FPrivAnimEndpoint& P, const FSimCopterPopulationFigure::FCalibration& Cal)
{
	return FVector(
		float(P.X) * Cal.ScaleCmPerUnit,
		float(P.Y) * Cal.ScaleCmPerUnit,
		-float(P.Z) * Cal.ScaleCmPerUnit + Cal.FeetOffsetCm);
}

struct FMeshArrays
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
};

// Unreal shows the face a triangle winds *clockwise* around, i.e. the side its index order's
// right-hand-rule normal points away from - the same convention the city terrain builder relies
// on. Emit the triangle so it is visible from `Normal`'s side.
void AppendTriangleIndices(FMeshArrays& M, int32 Base, int32 I0, int32 I1, int32 I2, bool bFlip)
{
	M.Triangles.Add(Base + I0);
	M.Triangles.Add(Base + (bFlip ? I2 : I1));
	M.Triangles.Add(Base + (bFlip ? I1 : I2));
}

// One quad. Corners are given in ring order; the face ends up visible from `Normal`'s side
// whichever way round the ring turns. `CornerNormals`, when supplied, gives smooth shading
// (used by the balls) while `Normal` still decides the winding.
void AppendQuad(
	FMeshArrays& M,
	const FVector Corners[4],
	const FVector& Normal,
	const FLinearColor& Color,
	bool bDoubleSided,
	const FVector2D* CornerUVs = nullptr,
	const FVector* CornerNormals = nullptr)
{
	const int32 Base = M.Vertices.Num();
	const FVector Tangent = (Corners[1] - Corners[0]).GetSafeNormal();
	static const FVector2D DefaultUVs[4] = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)};
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		M.Vertices.Add(Corners[Corner]);
		M.Normals.Add(CornerNormals != nullptr ? CornerNormals[Corner] : Normal);
		M.UVs.Add(CornerUVs != nullptr ? CornerUVs[Corner] : DefaultUVs[Corner]);
		M.VertexColors.Add(Color);
		M.Tangents.Add(FProcMeshTangent(Tangent, false));
	}

	const FVector RingNormal = FVector::CrossProduct(Corners[1] - Corners[0], Corners[3] - Corners[0]);
	const bool bFlip = FVector::DotProduct(RingNormal, Normal) > 0.0f;
	AppendTriangleIndices(M, Base, 0, 1, 2, bFlip);
	AppendTriangleIndices(M, Base, 0, 2, 3, bFlip);
	if (bDoubleSided)
	{
		AppendTriangleIndices(M, Base, 0, 1, 2, !bFlip);
		AppendTriangleIndices(M, Base, 0, 2, 3, !bFlip);
	}
}

// As AppendQuad, for the triangles that close a ball's poles.
void AppendTriangle(
	FMeshArrays& M,
	const FVector Corners[3],
	const FVector& Normal,
	const FLinearColor& Color,
	const FVector2D* CornerUVs,
	const FVector* CornerNormals)
{
	const int32 Base = M.Vertices.Num();
	const FVector Tangent = (Corners[1] - Corners[0]).GetSafeNormal();
	for (int32 Corner = 0; Corner < 3; ++Corner)
	{
		M.Vertices.Add(Corners[Corner]);
		M.Normals.Add(CornerNormals != nullptr ? CornerNormals[Corner] : Normal);
		M.UVs.Add(CornerUVs != nullptr ? CornerUVs[Corner] : FVector2D::ZeroVector);
		M.VertexColors.Add(Color);
		M.Tangents.Add(FProcMeshTangent(Tangent, false));
	}

	const FVector RingNormal = FVector::CrossProduct(Corners[1] - Corners[0], Corners[2] - Corners[0]);
	AppendTriangleIndices(M, Base, 0, 1, 2, FVector::DotProduct(RingNormal, Normal) > 0.0f);
}

// Oriented box from A to B, optionally tapering from one half width to the other (the 3D
// stand-in for the original's constant-width and shrinking screen strokes). `ForwardDepthRatio`
// squashes the cross-section along the figure's forward axis, since the original's strokes have
// width but no depth. Degenerate segments become a cube.
void AppendStroke(
	FMeshArrays& M,
	const FVector& A,
	const FVector& B,
	float HalfWidthA,
	float HalfWidthB,
	const FLinearColor& Color,
	float ForwardDepthRatio = 1.0f)
{
	FVector Axis = B - A;
	float Length = Axis.Size();
	FVector Dir = Length > KINDA_SMALL_NUMBER ? Axis / Length : FVector::UpVector;

	// Extend the stroke by its half width on both ends so chained segments overlap the way the
	// original's round-capped thick lines did (otherwise joints show gaps).
	const FVector Start = A - Dir * HalfWidthA;
	const FVector End = B + Dir * HalfWidthB;

	FVector U = FVector::CrossProduct(Dir, FVector::UpVector);
	if (!U.Normalize())
	{
		U = FVector::CrossProduct(Dir, FVector::ForwardVector).GetSafeNormal();
	}
	FVector V = FVector::CrossProduct(Dir, U);

	// Only the cross-section is squashed - never the endpoints or the length - so the pose and
	// the figure's real forward reach are untouched and only the invented depth shrinks. A
	// stroke already pointing along forward has no forward component in its cross-section, so
	// this correctly leaves outstretched arms at full thickness.
	U.X *= ForwardDepthRatio;
	V.X *= ForwardDepthRatio;

	const FVector StartU = U * HalfWidthA;
	const FVector StartV = V * HalfWidthA;
	const FVector EndU = U * HalfWidthB;
	const FVector EndV = V * HalfWidthB;

	// 8 corners: the ring at Start, then the ring at End.
	const FVector C[8] = {
		Start - StartU - StartV, Start + StartU - StartV, Start + StartU + StartV, Start - StartU + StartV,
		End - EndU - EndV, End + EndU - EndV, End + EndU + EndV, End - EndU + EndV};

	auto Face = [&M, &Color](const FVector& V0, const FVector& V1, const FVector& V2, const FVector& V3)
	{
		const FVector Corners[4] = {V0, V1, V2, V3};
		// The corner rings below all turn the same way around the outside of the box, so the
		// ring normal is the outward one.
		const FVector Normal = FVector::CrossProduct(V1 - V0, V3 - V0).GetSafeNormal();
		AppendQuad(M, Corners, Normal, Color, false);
	};

	Face(C[0], C[3], C[2], C[1]); // start cap
	Face(C[4], C[5], C[6], C[7]); // end cap
	Face(C[0], C[1], C[5], C[4]);
	Face(C[1], C[2], C[6], C[5]);
	Face(C[2], C[3], C[7], C[6]);
	Face(C[3], C[0], C[4], C[7]);
}

void AppendCube(FMeshArrays& M, const FVector& Center, float HalfSize, const FLinearColor& Color)
{
	AppendStroke(
		M,
		Center - FVector(0, 0, HalfSize * 0.001f),
		Center + FVector(0, 0, HalfSize * 0.001f),
		HalfSize,
		HalfSize,
		Color);
}

// A low-poly ball: the 3D stand-in for the original's filled-disc primitives, which were
// rasterized in screen space and so looked round from every camera angle.
//
// With `PanoramaU` set the ball is skinned with the head sprite instead of vertex colours: the
// original's 52x25 SIM3D.BMP head image is a panorama the blitter wraps around the head as it
// turns, so U runs once around the ball (offset so the face fronts +X) and V runs top to bottom.
void AppendBall(
	FMeshArrays& M,
	const FVector& Center,
	float RadiusXY,
	float RadiusZ,
	const FLinearColor& Color,
	const float* PanoramaU = nullptr)
{
	if (RadiusXY <= KINDA_SMALL_NUMBER || RadiusZ <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Ellipsoid surface normal, so the ball shades smoothly instead of showing its facets.
	auto NormalAt = [RadiusXY, RadiusZ, &Center](const FVector& Point)
	{
		const FVector Local = Point - Center;
		return FVector(
			Local.X / (RadiusXY * RadiusXY),
			Local.Y / (RadiusXY * RadiusXY),
			Local.Z / (RadiusZ * RadiusZ)).GetSafeNormal();
	};
	auto PointAt = [RadiusXY, RadiusZ, &Center](int32 Ring, int32 Segment)
	{
		const float Polar = UE_PI * float(Ring) / float(BallRings);
		const float Azimuth = UE_TWO_PI * float(Segment) / float(BallSegments);
		return Center + FVector(
			FMath::Cos(Azimuth) * FMath::Sin(Polar) * RadiusXY,
			FMath::Sin(Azimuth) * FMath::Sin(Polar) * RadiusXY,
			FMath::Cos(Polar) * RadiusZ);
	};
	// Keep every U inside 0..1 (the last column runs up to exactly 1.0) so the panorama needs no
	// wrap addressing on the texture.
	auto UAt = [PanoramaU](int32 Segment)
	{
		return FMath::Fmod(*PanoramaU + float(Segment) / float(BallSegments), 1.0f);
	};
	const FLinearColor SkinColor = PanoramaU != nullptr ? FLinearColor::White : Color;

	for (int32 Ring = 0; Ring < BallRings; ++Ring)
	{
		const float V0 = float(Ring) / float(BallRings);
		const float V1 = float(Ring + 1) / float(BallRings);
		for (int32 Segment = 0; Segment < BallSegments; ++Segment)
		{
			const FVector TopLeft = PointAt(Ring, Segment);
			const FVector TopRight = PointAt(Ring, Segment + 1);
			const FVector BottomRight = PointAt(Ring + 1, Segment + 1);
			const FVector BottomLeft = PointAt(Ring + 1, Segment);
			const float U0 = PanoramaU != nullptr ? UAt(Segment) : 0.0f;
			const float U1 = PanoramaU != nullptr ? U0 + 1.0f / float(BallSegments) : 1.0f;

			if (Ring == 0 || Ring == BallRings - 1)
			{
				// The rings touching a pole collapse to a triangle fan.
				const bool bTopPole = Ring == 0;
				const FVector Corners[3] = {
					bTopPole ? TopLeft : BottomLeft,
					bTopPole ? BottomLeft : TopLeft,
					bTopPole ? BottomRight : TopRight};
				const FVector Normals[3] = {NormalAt(Corners[0]), NormalAt(Corners[1]), NormalAt(Corners[2])};
				const FVector2D UVs[3] = {
					FVector2D(U0, bTopPole ? V0 : V1),
					FVector2D(U0, bTopPole ? V1 : V0),
					FVector2D(U1, bTopPole ? V1 : V0)};
				const FVector FaceNormal = (Normals[0] + Normals[1] + Normals[2]).GetSafeNormal();
				AppendTriangle(M, Corners, FaceNormal, SkinColor, UVs, Normals);
				continue;
			}

			const FVector Corners[4] = {TopLeft, TopRight, BottomRight, BottomLeft};
			const FVector Normals[4] = {
				NormalAt(TopLeft), NormalAt(TopRight), NormalAt(BottomRight), NormalAt(BottomLeft)};
			const FVector2D UVs[4] = {
				FVector2D(U0, V0), FVector2D(U1, V0), FVector2D(U1, V1), FVector2D(U0, V1)};
			const FVector FaceNormal = (Normals[0] + Normals[1] + Normals[2] + Normals[3]).GetSafeNormal();
			AppendQuad(M, Corners, FaceNormal, SkinColor, false, UVs, Normals);
		}
	}
}
} // namespace

TSharedPtr<FSimCopterPrivAnimShared> FSimCopterPopulationFigure::GetShared(const FString& OriginalGameRoot, FString& OutError)
{
	// One cache entry per original-game root; loaded data is immutable and shared by all agents.
	static TMap<FString, TSharedPtr<FSimCopterPrivAnimShared>> Cache;
	static TSet<FString> FailedRoots;

	const FString Key = FPaths::ConvertRelativePathToFull(OriginalGameRoot);
	if (const TSharedPtr<FSimCopterPrivAnimShared>* Found = Cache.Find(Key))
	{
		return *Found;
	}
	if (FailedRoots.Contains(Key))
	{
		OutError = TEXT("privanim data previously failed to load for this root.");
		return nullptr;
	}

	TSharedPtr<FSimCopterPrivAnimShared> Shared = MakeShared<FSimCopterPrivAnimShared>();

	const FString PrivAnimPath = FSimCopterPrivAnimReader::ResolvePrivAnimPath(OriginalGameRoot);
	if (PrivAnimPath.IsEmpty())
	{
		OutError = FString::Printf(TEXT("privanim.df not found under '%s/X'."), *OriginalGameRoot);
		FailedRoots.Add(Key);
		return nullptr;
	}
	if (!FSimCopterPrivAnimReader::LoadFromFile(PrivAnimPath, Shared->Model, OutError))
	{
		FailedRoots.Add(Key);
		return nullptr;
	}

	// The game's global palette is the (identical) CMAP in each GEO pack.
	const TCHAR* GeoNames[] = {TEXT("sim3d1.max"), TEXT("SIM3D1.MAX"), TEXT("SIM3D2.MAX"), TEXT("SIM3D3.MAX")};
	for (const TCHAR* GeoName : GeoNames)
	{
		const FString GeoPath = FPaths::Combine(OriginalGameRoot, TEXT("GEO"), GeoName);
		if (!FPaths::FileExists(GeoPath))
		{
			continue;
		}
		FMaxisMeshFile MeshFile;
		FString MeshError;
		if (FMaxisMeshReader::LoadMeshFileFromFile(GeoPath, MeshFile, MeshError) && MeshFile.ColorMap.Num() > 0)
		{
			Shared->Palette = MeshFile.ColorMap;
			break;
		}
	}
	if (Shared->Palette.Num() == 0)
	{
		OutError = FString::Printf(TEXT("No GEO CMAP palette found under '%s/GEO'."), *OriginalGameRoot);
		FailedRoots.Add(Key);
		return nullptr;
	}

	// Head sprites come from SIM3D.BMP (optional: figures degrade to colored-cube heads).
	const TCHAR* BitmapNames[] = {TEXT("BMP/SIM3D.BMP"), TEXT("BMP/sim3d.bmp")};
	for (const TCHAR* BitmapName : BitmapNames)
	{
		const FString BitmapPath = FPaths::Combine(OriginalGameRoot, BitmapName);
		if (!FPaths::FileExists(BitmapPath))
		{
			continue;
		}
		FMaxisCompositeBitmap Bitmap;
		FString BitmapError;
		if (FMaxisTextureReader::LoadCompositeBitmapFromFile(BitmapPath, Shared->Palette, Bitmap, BitmapError, true))
		{
			for (const int32 ImageId : GetHeadImageTable())
			{
				if (const FMaxisTextureImage* Image = Bitmap.FindImage(ImageId))
				{
					Shared->HeadImages.Add(ImageId, *Image);
				}
			}
		}
		break;
	}

	Cache.Add(Key, Shared);
	return Shared;
}

const TArray<int32>& FSimCopterPopulationFigure::GetHeadImageTable()
{
	// DAT_0058f0e0, written by the privanim loader FUN_004ceab0: SIM3D.BMP image ids for the
	// pedestrian head sprites, indexed by the per-person head selector (person+0x18e).
	static const TArray<int32> Table = {4, 5, 0x2c, 0x2d, 0x2e, 0x41, 0x2f, 0x42, 0x30, 0x31, 0x43};
	return Table;
}

FLinearColor FSimCopterPopulationFigure::ResolvePartColor(const TArray<FColor>& Palette, const FPrivAnimPart& Part, int32 ClothesOffset)
{
	// FUN_004cf8f0: palette entry = 0x24 + color*0x10; recolorable parts (+5 == 0) shift the
	// color index by the person's clothes offset modulo 14.
	int32 ColorIndex = Part.ColorIndex;
	if (Part.FixedColor == 0 && ClothesOffset != 0)
	{
		ColorIndex = (ColorIndex + ClothesOffset) % 14;
	}
	const int32 PaletteEntry = 0x24 + ColorIndex * 0x10;
	if (Palette.IsValidIndex(PaletteEntry))
	{
		return FLinearColor::FromSRGBColor(Palette[PaletteEntry]);
	}
	return FLinearColor(0.6f, 0.55f, 0.5f);
}

FSimCopterPopulationFigure::FCalibration FSimCopterPopulationFigure::Calibrate(const FPrivAnimClip& StandingClip, float HeightCm)
{
	FCalibration Calibration;
	int32 MinZ = 127;
	int32 MaxZ = -128;
	const int32 Frame0Count = FMath::Min(StandingClip.PartCount, StandingClip.Segments.Num());
	for (int32 Index = 0; Index < Frame0Count; ++Index)
	{
		const FPrivAnimSegment& Segment = StandingClip.Segments[Index];
		MinZ = FMath::Min(MinZ, FMath::Min<int32>(Segment.A.Z, Segment.B.Z));
		MaxZ = FMath::Max(MaxZ, FMath::Max<int32>(Segment.A.Z, Segment.B.Z));
	}
	const float Range = float(MaxZ - MinZ);
	Calibration.ScaleCmPerUnit = Range > 1.0f ? HeightCm / Range : 1.0f;
	// Model vertical is negated in ToLocal (screen-space y-down), so the feet are at MaxZ.
	Calibration.FeetOffsetCm = float(MaxZ) * Calibration.ScaleCmPerUnit;
	return Calibration;
}

float FSimCopterPopulationFigure::ComputeClipDropBelowFeetCm(
	const FPrivAnimClip& Clip,
	const FCalibration& Calibration)
{
	// The model vertical is screen-space y-down and ToLocal negates it, so the LOWEST point of the
	// pose is its LARGEST model Z. Every frame, not just frame 0: a clip can settle further down as
	// it plays.
	int32 MaxZ = -128;
	for (const FPrivAnimSegment& Segment : Clip.Segments)
	{
		MaxZ = FMath::Max(MaxZ, FMath::Max<int32>(Segment.A.Z, Segment.B.Z));
	}
	if (MaxZ <= -128)
	{
		return 0.0f;
	}

	// ToLocal's Z, at the lowest point in the clip. Negative means it is below the feet plane.
	const float LowestLocalZCm = -static_cast<float>(MaxZ) * Calibration.ScaleCmPerUnit + Calibration.FeetOffsetCm;
	return FMath::Max(0.0f, -LowestLocalZCm);
}

float FSimCopterPopulationFigure::ComputeClipGroundLiftCm(
	const FPrivAnimClip& Clip,
	const FPrivAnimClip& CalibrationClip,
	const FCalibration& Calibration)
{
	// Against the walking figure's own lowest point rather than against the nominal feet plane.
	// Calibrate reads frame 0, and every shipped walk cycle dips a couple of centimetres below it
	// mid-stride - that dip is where a pedestrian visibly meets the pavement today, so measuring
	// from zero instead would lift the entire population off the ground to fix the lying poses.
	const float ClipDropCm = ComputeClipDropBelowFeetCm(Clip, Calibration);
	const float CalibrationDropCm = ComputeClipDropBelowFeetCm(CalibrationClip, Calibration);
	return FMath::Max(0.0f, ClipDropCm - CalibrationDropCm);
}

bool FSimCopterPopulationFigure::BuildClipSections(
	UProceduralMeshComponent* MeshComponent,
	const FPrivAnimFigure& Figure,
	const FPrivAnimClip& Clip,
	const TArray<FColor>& Palette,
	const FBuildParams& Params,
	const FCalibration& Calibration,
	bool& bOutHasHeadSection)
{
	bOutHasHeadSection = false;
	if (MeshComponent == nullptr || Clip.PartCount != Figure.Parts.Num() || Clip.FrameCount <= 0)
	{
		return false;
	}

	MeshComponent->ClearAllMeshSections();

	// Model units -> world, with the global chunkiness dial folded in. Every primitive below is
	// sized from the part's own ARCP dimensions through this.
	const float SizeScale = Calibration.ScaleCmPerUnit * FMath::Max(Params.PartSizeScale, 0.0f);
	const float MinSizeUnits = FMath::Max(Params.MinPartSizeUnits, 0.0f);
	const float DepthRatio = FMath::Clamp(Params.StrokeForwardDepthRatio, 0.05f, 1.0f);
	// Spread the painter bias over however many parts this figure has, so the total stays put
	// whether the figure is Nessie's 29 parts or the cow's 88.
	const float PainterBiasStep = Clip.PartCount > 0
		? Params.HeightCm * FMath::Max(Params.PainterBiasFraction, 0.0f) / float(Clip.PartCount)
		: 0.0f;

	for (int32 Frame = 0; Frame < Clip.FrameCount; ++Frame)
	{
		FMeshArrays Body;
		FMeshArrays Head;

		for (int32 PartIndex = 0; PartIndex < Clip.PartCount; ++PartIndex)
		{
			const FPrivAnimPart& Part = Figure.Parts[PartIndex];
			if (Part.Type == EPrivAnimPartType::None || (Part.LodMask & Params.LodBit) == 0)
			{
				continue;
			}

			const FPrivAnimSegment& Segment = Clip.Segment(Frame, PartIndex);
			const FVector A = ToLocal(Segment.A, Calibration);
			const FVector B = ToLocal(Segment.B, Calibration);
			const FLinearColor Color = ResolvePartColor(Palette, Part, Params.ClothesOffset);

			// Dims.X sizes the round primitives (disc, head), Dims.Y the strokes, Dims.Z is the
			// thin stroke's taper. The dot primitives only ever use endpoint A: the original
			// does not even transform B for them (FUN_004cfb30 skips types 8/9/0xc/0xd/0xe).
			// The painter bias stands in for the original's back-to-front draw order.
			const float PainterBias = float(PartIndex) * PainterBiasStep;
			const float RoundUnits = FMath::Max(Part.Dims.X, MinSizeUnits);
			const float StrokeHalf = FMath::Max(Part.Dims.Y, MinSizeUnits) * 0.5f * SizeScale + PainterBias;

			switch (Part.Type)
			{
			case EPrivAnimPartType::ThickLine:
				AppendStroke(Body, A, B, StrokeHalf, StrokeHalf, Color, DepthRatio);
				break;
			case EPrivAnimPartType::ThinLine:
			{
				const float TaperedHalf =
					StrokeHalf * FMath::Max(1.0f - ThinLineTaperPerDim * Part.Dims.Z, 0.0f);
				AppendStroke(Body, A, B, StrokeHalf, TaperedHalf, Color, DepthRatio);
				break;
			}
			case EPrivAnimPartType::HeadSprite:
			{
				const float HeadRadiusZ = RoundUnits * SizeScale + PainterBias;
				const float HeadRadiusXY = HeadRadiusZ * HeadWidthRatio;
				if (!Params.bTexturedHead)
				{
					AppendBall(Body, A, HeadRadiusXY, HeadRadiusZ, Color);
					break;
				}
				AppendBall(Head, A, HeadRadiusXY, HeadRadiusZ, FLinearColor::White, &Params.HeadFaceU);
				bOutHasHeadSection = true;
				break;
			}
			case EPrivAnimPartType::DotStyle0:
			case EPrivAnimPartType::DotStyle1:
			case EPrivAnimPartType::DotStyle2:
			{
				const float DiscRadius = RoundUnits * DiscDiameterPerDim * 0.5f * SizeScale + PainterBias;
				AppendBall(Body, A, DiscRadius, DiscRadius, Color);
				break;
			}
			case EPrivAnimPartType::Pixel:
				// One screen pixel in the original; a speck here. These are nearly all
				// far-LOD stand-ins, so they rarely survive the LOD gate above anyway.
				AppendCube(Body, A, MinSizeUnits * 0.5f * SizeScale + PainterBias, Color);
				break;
			default:
				// Unknown draw type - keep the segment visible as a stroke.
				AppendStroke(Body, A, B, StrokeHalf, StrokeHalf, Color, DepthRatio);
				break;
			}
		}

		const bool bVisible = Frame == 0;
		MeshComponent->CreateMeshSection_LinearColor(
			Frame * 2, Body.Vertices, Body.Triangles, Body.Normals, Body.UVs, Body.VertexColors, Body.Tangents, false);
		MeshComponent->SetMeshSectionVisible(Frame * 2, bVisible);
		MeshComponent->CreateMeshSection_LinearColor(
			Frame * 2 + 1, Head.Vertices, Head.Triangles, Head.Normals, Head.UVs, Head.VertexColors, Head.Tangents, false);
		MeshComponent->SetMeshSectionVisible(Frame * 2 + 1, bVisible);
	}

	return true;
}

void FSimCopterPopulationFigure::ShowFrame(UProceduralMeshComponent* MeshComponent, int32 FrameCount, int32 FrameIndex, bool bHasHeadSection)
{
	if (MeshComponent == nullptr || FrameCount <= 0)
	{
		return;
	}
	const int32 Wrapped = ((FrameIndex % FrameCount) + FrameCount) % FrameCount;
	for (int32 Frame = 0; Frame < FrameCount; ++Frame)
	{
		const bool bVisible = Frame == Wrapped;
		MeshComponent->SetMeshSectionVisible(Frame * 2, bVisible);
		if (bHasHeadSection)
		{
			MeshComponent->SetMeshSectionVisible(Frame * 2 + 1, bVisible);
		}
	}
}
