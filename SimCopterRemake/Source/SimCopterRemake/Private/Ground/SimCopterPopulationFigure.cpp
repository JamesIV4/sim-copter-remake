// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterPopulationFigure.h"

#include "Formats/MaxisMeshReader.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"

namespace
{
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

void AppendQuad(
	FMeshArrays& M,
	const FVector Corners[4],
	const FVector& Normal,
	const FLinearColor& Color,
	bool bDoubleSided,
	const FVector2D* CornerUVs = nullptr)
{
	const int32 Base = M.Vertices.Num();
	const FVector Tangent = (Corners[1] - Corners[0]).GetSafeNormal();
	static const FVector2D DefaultUVs[4] = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)};
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		M.Vertices.Add(Corners[Corner]);
		M.Normals.Add(Normal);
		M.UVs.Add(CornerUVs != nullptr ? CornerUVs[Corner] : DefaultUVs[Corner]);
		M.VertexColors.Add(Color);
		M.Tangents.Add(FProcMeshTangent(Tangent, false));
	}
	M.Triangles.Add(Base + 0); M.Triangles.Add(Base + 1); M.Triangles.Add(Base + 2);
	M.Triangles.Add(Base + 0); M.Triangles.Add(Base + 2); M.Triangles.Add(Base + 3);
	if (bDoubleSided)
	{
		M.Triangles.Add(Base + 0); M.Triangles.Add(Base + 2); M.Triangles.Add(Base + 1);
		M.Triangles.Add(Base + 0); M.Triangles.Add(Base + 3); M.Triangles.Add(Base + 2);
	}
}

// Oriented box from A to B with a square cross-section (the 3D stand-in for the original's
// constant-width screen stroke). Degenerate segments become a cube.
void AppendStroke(FMeshArrays& M, const FVector& A, const FVector& B, float HalfWidth, const FLinearColor& Color)
{
	FVector Axis = B - A;
	float Length = Axis.Size();
	FVector Dir = Length > KINDA_SMALL_NUMBER ? Axis / Length : FVector::UpVector;

	// Extend the stroke by its half width on both ends so chained segments overlap the way the
	// original's round-capped thick lines did (otherwise joints show gaps).
	const FVector Start = A - Dir * HalfWidth;
	const FVector End = B + Dir * HalfWidth;

	FVector U = FVector::CrossProduct(Dir, FVector::UpVector);
	if (!U.Normalize())
	{
		U = FVector::CrossProduct(Dir, FVector::ForwardVector).GetSafeNormal();
	}
	const FVector V = FVector::CrossProduct(Dir, U);

	const FVector HalfU = U * HalfWidth;
	const FVector HalfV = V * HalfWidth;

	// 8 corners: bottom (at Start) then top (at End).
	const FVector C[8] = {
		Start - HalfU - HalfV, Start + HalfU - HalfV, Start + HalfU + HalfV, Start - HalfU + HalfV,
		End - HalfU - HalfV, End + HalfU - HalfV, End + HalfU + HalfV, End - HalfU + HalfV};

	auto Face = [&M, &Color](const FVector& V0, const FVector& V1, const FVector& V2, const FVector& V3)
	{
		const FVector Corners[4] = {V0, V1, V2, V3};
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
	AppendStroke(M, Center - FVector(0, 0, HalfSize * 0.001f), Center + FVector(0, 0, HalfSize * 0.001f), HalfSize, Color);
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

	const float ThickHalf = Params.HeightCm * Params.ThickWidthFraction * 0.5f;
	const float ThinHalf = Params.HeightCm * Params.ThinWidthFraction * 0.5f;
	const float DotHalf = Params.HeightCm * Params.DotSizeFraction * 0.5f;
	const float HeadHalf = Params.HeightCm * Params.HeadSizeFraction * 0.5f;

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

			switch (Part.Type)
			{
			case EPrivAnimPartType::ThickLine:
				AppendStroke(Body, A, B, ThickHalf, Color);
				break;
			case EPrivAnimPartType::ThinLine:
				AppendStroke(Body, A, B, ThinHalf, Color);
				break;
			case EPrivAnimPartType::HeadSprite:
			{
				if (!Params.bTexturedHead)
				{
					AppendCube(Body, A, HeadHalf * 0.7f, Color);
					break;
				}
				// Forward-facing card; the original blits a rotated SIM3D.BMP head sprite here.
				// Card top V=0 (texture rows are stored top-down after decode).
				const FVector Center = A + FVector(ThinHalf, 0.0f, 0.0f);
				const FVector Corners[4] = {
					Center + FVector(0, -HeadHalf, HeadHalf),
					Center + FVector(0, HeadHalf, HeadHalf),
					Center + FVector(0, HeadHalf, -HeadHalf),
					Center + FVector(0, -HeadHalf, -HeadHalf)};
				AppendQuad(Head, Corners, FVector::ForwardVector, FLinearColor::White, true);
				bOutHasHeadSection = true;
				break;
			}
			case EPrivAnimPartType::DotStyle0:
			case EPrivAnimPartType::DotStyle1:
			case EPrivAnimPartType::DotStyle2:
			case EPrivAnimPartType::Pixel:
				AppendCube(Body, A, DotHalf, Color);
				break;
			default:
				// Unknown draw type - keep the segment visible as a thin stroke.
				AppendStroke(Body, A, B, ThinHalf, Color);
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
