// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterEffectRasterizer.h"

#include "Engine/Texture2D.h"
#include "Ground/SimCopterEffectFX.h"

namespace
{
	struct FStencilDescriptor
	{
		const ANSICHAR* Pixels;
		uint8 Width;
		uint8 Height;
		uint8 SelectorAdvance;
	};

	constexpr uint8 SelectorTables[FSimCopterEffectRasterizer::SelectorFamilyCount]
		[FSimCopterEffectRasterizer::SelectorPhaseCount] = {
		{ 0x13, 0x17, 0x73, 0x7b, 0x64, 0x1d, 0x1f, 0x7f }, // 0x00504828: fire
		{ 0x3a, 0x3a, 0x3b, 0x3a, 0x3c, 0x3a, 0x39, 0x3a }, // 0x00504830: light smoke
		{ 0x31, 0x31, 0x30, 0x31, 0x33, 0x31, 0x32, 0x31 }, // 0x00504838: dark smoke
		{ 0x94, 0xa8, 0x96, 0xaa, 0x9a, 0xac, 0x92, 0xab }, // 0x00504840: water
	};

	// These are the literal framebuffer writes made by the ten normal-mode
	// radius cases at 0x004995eb..0x0049a405. A dot is an untouched pixel; a
	// digit is the DAT_00505c48 selector offset used for that write. Radius 8
	// intentionally preserves the executable's fall-through into radius 9.
	static constexpr ANSICHAR Stencil0[] =
		"0";
	static constexpr ANSICHAR Stencil1[] =
		"0.0"
		"..."
		"1.1";
	static constexpr ANSICHAR Stencil2[] =
		"0.0.0"
		"....."
		"0.1.1"
		"....."
		"1.2.2";
	static constexpr ANSICHAR Stencil3[] =
		"0.0.0.0"
		"......."
		"1.1.1.1"
		"......."
		"1.2.2.2"
		"......."
		"2.2.3.3";
	static constexpr ANSICHAR Stencil4[] =
		"0.0.0.0.1"
		"........."
		"1.1.1.1.2"
		"........."
		"2.2.2.3.3"
		"........."
		"3.3.4.4.4"
		"........."
		"4.4.4.4.4";
	static constexpr ANSICHAR Stencil5[] =
		"0.0.1.1.1.1"
		"..........."
		"2.2.2.2.2.2"
		"..........."
		"3.3.3.3.3.3"
		"..........."
		"4.4.4.4.4.4"
		"..........."
		"5.5.5.5.5.5"
		"..........."
		"6.6.6.6.6.6";
	static constexpr ANSICHAR Stencil6[] =
		"0.0.0.0.0.1.1"
		"............."
		"1.1.1.1.1.1.1"
		"............."
		"2.2.2.2.3.3.3"
		"............."
		"3.3.3.3.3.3.4"
		"............."
		"4.4.5.5.5.5.5"
		"............."
		"5.5.6.6.6.6.6"
		"............."
		"6.6.7.7.7.7.7";
	static constexpr ANSICHAR Stencil7[] =
		"0.0.1.1.1.1.1.1"
		"..............."
		"1.2.2.3.3.3.3.4"
		"..............."
		"4.4.4.4.4.4.4.4"
		"..............."
		"5.5.5.5.5.5.6.6"
		"..............."
		"6.6.6.6.6.7.7.7"
		"..............."
		"7.7.7.0.0.0.0.0"
		"..............."
		"0.1.1.2.2.2.2.2"
		"..............."
		"2.2.2.2.2.2.2.2";
	static constexpr ANSICHAR Stencil8[] =
		"0.0.0.0.0.0.1.1.1.................."
		"..................................."
		"1.1.1.1.2.3.3.3.3.................."
		"..................................."
		"3.3.3.3.3.3.4.4.4.................."
		"..................................."
		"4.4.4.4.4.4.5.5.5.................."
		"..................................."
		"5.5.5.5.5.5.5.5.5.................."
		"..................................."
		"5.6.6.6.6.6.6.7.7.................."
		"..................................."
		"7.7.7.7.7.0.0.0.0.................."
		"..................................."
		"0.0.0.0.0.0.1.1.1.................."
		"..................................."
		"1.1.1.1.2.2.2.2.3.3.3.3.3.3.3.3.3.4"
		"..................................."
		"................4.4.4.4.5.5.5.5.5.5"
		"..................................."
		"................6.6.6.6.6.6.7.7.7.7"
		"..................................."
		"................7.7.7.7.0.0.0.0.0.0"
		"..................................."
		"................0.0.0.0.0.0.0.0.0.1"
		"..................................."
		"................1.1.1.1.1.1.1.2.2.2"
		"..................................."
		"................2.2.3.3.3.4.4.4.4.4"
		"..................................."
		"................4.4.4.5.5.5.5.5.5.5"
		"..................................."
		"................5.5.5.5.6.6.6.6.6.6"
		"..................................."
		"................6.7.7.7.7.7.7.7.7.7";
	static constexpr ANSICHAR Stencil9[] =
		"0.0.0.0.0.0.0.0.0.1"
		"..................."
		"1.1.1.1.2.2.2.2.2.2"
		"..................."
		"3.3.3.3.3.3.4.4.4.4"
		"..................."
		"4.4.4.4.5.5.5.5.5.5"
		"..................."
		"5.5.5.5.5.5.5.5.5.6"
		"..................."
		"6.6.6.6.6.6.6.7.7.7"
		"..................."
		"7.7.0.0.0.1.1.1.1.1"
		"..................."
		"1.1.1.2.2.2.2.2.2.2"
		"..................."
		"2.2.2.2.3.3.3.3.3.3"
		"..................."
		"3.4.4.4.4.4.4.4.4.4";

	static constexpr FStencilDescriptor Stencils[] = {
		{ Stencil0, 1, 1, 1 },
		{ Stencil1, 3, 3, 2 },
		{ Stencil2, 5, 5, 3 },
		{ Stencil3, 7, 7, 4 },
		{ Stencil4, 9, 9, 5 },
		{ Stencil5, 11, 11, 7 },
		{ Stencil6, 13, 13, 8 },
		{ Stencil7, 15, 15, 11 },
		{ Stencil8, 35, 35, 24 },
		{ Stencil9, 19, 19, 13 },
	};
	static_assert(UE_ARRAY_COUNT(Stencils) == FSimCopterEffectRasterizer::RadiusCount);
	static_assert(sizeof(Stencil0) - 1 == 1 * 1);
	static_assert(sizeof(Stencil1) - 1 == 3 * 3);
	static_assert(sizeof(Stencil2) - 1 == 5 * 5);
	static_assert(sizeof(Stencil3) - 1 == 7 * 7);
	static_assert(sizeof(Stencil4) - 1 == 9 * 9);
	static_assert(sizeof(Stencil5) - 1 == 11 * 11);
	static_assert(sizeof(Stencil6) - 1 == 13 * 13);
	static_assert(sizeof(Stencil7) - 1 == 15 * 15);
	static_assert(sizeof(Stencil8) - 1 == 35 * 35);
	static_assert(sizeof(Stencil9) - 1 == 19 * 19);

	uint32 OriginalSelectorCursor = 0;

	int32 FixedMultiplyFloor(int32 Scale1616, int32 Factor)
	{
		return static_cast<int32>((static_cast<int64>(Scale1616) * Factor) >> 16);
	}
}

int32 FSimCopterEffectRasterizer::ComputeDepthScale1616(float CameraDepthCm)
{
	const int32 Depth1616 = FMath::Max(
		0,
		FMath::RoundToInt(CameraDepthCm / SimCopterEffectFX::OriginalUnitToCm * 65536.0f));
	if (Depth1616 >= FarDepth1616)
	{
		return 0;
	}
	return static_cast<int32>(
		(static_cast<int64>(FarDepth1616 - Depth1616) << 16) / FarDepth1616);
}

FSimCopterEffectKernelMetrics FSimCopterEffectRasterizer::ComputeKernelMetrics(
	uint8 EffectClass,
	int32 DepthScale1616)
{
	FSimCopterEffectKernelMetrics Result;
	if (DepthScale1616 <= 0 || (EffectClass & 0x7f) > 11)
	{
		return Result;
	}

	int32 MinFactor = 2;
	int32 MaxFactor = 7;
	int32 JitterFactor = 15;
	switch (EffectClass & 0x7f)
	{
	case 1:
	case 2:
	case 7:
	case 9:
		MinFactor = 8;
		MaxFactor = 9;
		JitterFactor = 20;
		break;
	case 3:
		MinFactor = 2;
		MaxFactor = 3;
		JitterFactor = 12;
		break;
	case 4:
	case 6:
	case 8:
		MinFactor = 2;
		MaxFactor = 5;
		JitterFactor = 10;
		break;
	case 5:
		MinFactor = 1;
		MaxFactor = 3;
		JitterFactor = 12;
		break;
	case 10:
	case 11:
		MinFactor = 3;
		MaxFactor = 6;
		JitterFactor = 15;
		break;
	default:
		break;
	}

	Result.MinRadius = FixedMultiplyFloor(DepthScale1616, MinFactor);
	const int32 MaxRadius = FixedMultiplyFloor(DepthScale1616, MaxFactor);
	Result.RadiusChoiceCount = FMath::Max(1, MaxRadius - Result.MinRadius + 1);
	Result.Iterations = FMath::Min(FixedMultiplyFloor(DepthScale1616, JitterFactor), 30);

	// DAT_004f9750 == 0x10 doubles the half extent and then doubles the modulo
	// span again at 0x00496f23..0x00496f29.
	Result.JitterHalfExtentPixels = Result.Iterations * 2;
	Result.JitterSpanPixels = Result.JitterHalfExtentPixels * 2;
	return Result;
}

int32 FSimCopterEffectRasterizer::GetSelectorFamily(uint8 EffectClass)
{
	switch (EffectClass & 0x7f)
	{
	case 2:
	case 5:
	case 10:
		return 0;
	case 8:
	case 9:
		return 1;
	case 6:
	case 7:
		return 2;
	case 1:
	case 4:
	case 11:
		return 1;
	default:
		return 3;
	}
}

uint8 FSimCopterEffectRasterizer::GetSelectorPaletteIndex(uint8 EffectClass, int32 Phase)
{
	const int32 Family = GetSelectorFamily(EffectClass);
	return SelectorTables[Family][Phase & 7];
}

uint32 FSimCopterEffectRasterizer::AdvanceRandom(uint32& State)
{
	State = State * 214013u + 2531011u;
	return (State >> 16) & 0x7fffu;
}

FSimCopterEffectStencilMetrics FSimCopterEffectRasterizer::GetStencilMetrics(int32 Radius)
{
	const FStencilDescriptor& Stencil = Stencils[FMath::Clamp(Radius, 0, RadiusCount - 1)];
	return {
		static_cast<int32>(Stencil.Width),
		static_cast<int32>(Stencil.Height),
		static_cast<int32>(Stencil.SelectorAdvance)
	};
}

int32 FSimCopterEffectRasterizer::GetStencilPhaseOffset(
	int32 Radius,
	int32 PixelX,
	int32 PixelY)
{
	const FStencilDescriptor& Stencil = Stencils[FMath::Clamp(Radius, 0, RadiusCount - 1)];
	if (PixelX < 0 || PixelY < 0 || PixelX >= Stencil.Width || PixelY >= Stencil.Height)
	{
		return INDEX_NONE;
	}
	const ANSICHAR Pixel = Stencil.Pixels[PixelY * Stencil.Width + PixelX];
	return Pixel == '.' ? INDEX_NONE : Pixel - '0';
}

int32 FSimCopterEffectRasterizer::ConsumeSelectorPhase(int32 Radius)
{
	const FStencilDescriptor& Stencil = Stencils[FMath::Clamp(Radius, 0, RadiusCount - 1)];
	const int32 Phase = static_cast<int32>(OriginalSelectorCursor & 7u);
	OriginalSelectorCursor += Stencil.SelectorAdvance;
	return Phase;
}

UTexture2D* FSimCopterEffectRasterizer::CreateSelectorAtlas(
	UObject* Outer,
	const TArray<FColor>& Palette)
{
	if (Palette.Num() < 256)
	{
		return nullptr;
	}

	constexpr int32 Width =
		RadiusCount * SelectorPhaseCount * KernelAtlasCellSize;
	constexpr int32 Height = SelectorFamilyCount * KernelAtlasCellSize;
	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (Texture == nullptr ||
		Texture->GetPlatformData() == nullptr ||
		Texture->GetPlatformData()->Mips.IsEmpty())
	{
		return nullptr;
	}
	if (Outer != nullptr)
	{
		Texture->Rename(
			*MakeUniqueObjectName(Outer, UTexture2D::StaticClass(), TEXT("OriginalEffectSelectors")).ToString(),
			Outer);
	}

	TArray<FColor> Pixels;
	Pixels.Init(FColor::Transparent, Width * Height);
	for (int32 Family = 0; Family < SelectorFamilyCount; ++Family)
	{
		for (int32 Radius = 0; Radius < RadiusCount; ++Radius)
		{
			const FStencilDescriptor& Stencil = Stencils[Radius];
			for (int32 Phase = 0; Phase < SelectorPhaseCount; ++Phase)
			{
				const int32 CellX =
					(Radius * SelectorPhaseCount + Phase) * KernelAtlasCellSize;
				const int32 CellY = Family * KernelAtlasCellSize;
				for (int32 Y = 0; Y < Stencil.Height; ++Y)
				{
					for (int32 X = 0; X < Stencil.Width; ++X)
					{
						const int32 PhaseOffset = GetStencilPhaseOffset(Radius, X, Y);
						if (PhaseOffset == INDEX_NONE)
						{
							continue;
						}
						const uint8 PaletteIndex =
							SelectorTables[Family][(Phase + PhaseOffset) & 7];
						Pixels[(CellY + Y) * Width + CellX + X] = Palette[PaletteIndex];
					}
				}
			}
		}
	}

	Texture->CompressionSettings = TC_VectorDisplacementmap;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->NeverStream = true;
	Texture->SRGB = true;
	Texture->Filter = TF_Nearest;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}

FVector2D FSimCopterEffectRasterizer::GetAtlasUV(
	uint8 EffectClass,
	int32 Phase,
	int32 Radius,
	int32 PixelX,
	int32 PixelY)
{
	constexpr float Width =
		static_cast<float>(RadiusCount * SelectorPhaseCount * KernelAtlasCellSize);
	constexpr float Height =
		static_cast<float>(SelectorFamilyCount * KernelAtlasCellSize);
	const int32 ClampedRadius = FMath::Clamp(Radius, 0, RadiusCount - 1);
	const int32 CellX =
		(ClampedRadius * SelectorPhaseCount + (Phase & 7)) * KernelAtlasCellSize;
	const int32 CellY = GetSelectorFamily(EffectClass) * KernelAtlasCellSize;
	return FVector2D(
		static_cast<float>(CellX + PixelX) / Width,
		static_cast<float>(CellY + PixelY) / Height);
}
