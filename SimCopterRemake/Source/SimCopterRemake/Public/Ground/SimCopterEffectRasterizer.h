// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture2D;

// Direct translation of the face-type 0x1a sizing state built by FUN_00496da0.
// Values are measured in pixels of the active original software-renderer viewport.
struct FSimCopterEffectKernelMetrics
{
	int32 Iterations = 0;
	int32 JitterHalfExtentPixels = 0;
	int32 JitterSpanPixels = 0;
	int32 MinRadius = 0;
	int32 RadiusChoiceCount = 0;
};

// Exact footprint of one normal-mode (DAT_004f9750 == 0x10) radius case in
// FUN_00496da0. Width/height include the untouched framebuffer pixels between
// writes; SelectorAdvance is the number of DAT_00505c48 increments.
struct FSimCopterEffectStencilMetrics
{
	int32 Width = 0;
	int32 Height = 0;
	int32 SelectorAdvance = 0;
};

// Shared, testable pieces of the original face-type 0x17/0x1a software renderer.
// Both handlers size their output in framebuffer pixels of the original gameplay
// viewport, so the Unreal components convert those pixel counts back through the
// original projection to get the world size the effect had in the 1996 renderer.
class SIMCOPTERREMAKE_API FSimCopterEffectRasterizer
{
public:
	// FUN_00479bb0 stores 0x10 in DAT_004f9750, so the shipped renderer is the
	// high-resolution one: a 560x400 gameplay viewport inside a 640x480, 8-bit
	// framebuffer. The 280x200 viewport in a 320x240 framebuffer is the alternate
	// mode 0x20, which FUN_00461350 stretch-blits 2x back up to 560x400 - both
	// modes therefore cover the same fraction of the gameplay view.
	static constexpr int32 OriginalViewportWidth = 560;
	static constexpr int32 OriginalViewportHeight = 400;
	static constexpr int32 OriginalFramebufferStride = 640;
	static constexpr int32 OriginalFramebufferHeight = 480;
	static constexpr int32 LowResolutionViewportWidth = 280;
	static constexpr int32 LowResolutionViewportHeight = 200;
	static constexpr int32 LowResolutionFramebufferStride = 320;
	static constexpr int32 LowResolutionFramebufferHeight = 240;

	// FUN_0046f2ca builds the projection from the active gameplay viewport:
	//   DAT_004faff0 = viewportWidth << 11            = half width, 20.12
	//   DAT_004faff8 = (DAT_004faff0 * 0x1bb6) >> 12  = focal length, 20.12
	// 0x1bb6/4096 is 1.7319336 (sqrt 3), so tan(hFov/2) = halfWidth/focal = 1/sqrt(3)
	// and the original gameplay view is a 60-degree horizontal frustum over 560x400.
	// FUN_0046f2b0(0x10000) sets the pixel aspect DAT_004fb014 to 1.0, so original
	// framebuffer pixels are square and one focal length serves both axes.
	static constexpr float OriginalProjectionRatio = 7094.0f / 4096.0f;
	static constexpr float OriginalFocalLengthPixels =
		OriginalViewportWidth * 0.5f * OriginalProjectionRatio; // 484.9414

	// World size that one original viewport pixel spans at CameraDepthCm - the exact
	// inverse of the original perspective divide. Sizing effect kernels with this
	// reproduces the size they had relative to the city, independent of the remake's
	// resolution and camera FOV. Deriving it from the live viewport instead makes the
	// effects grow with the remake's wider frustum.
	static constexpr float GetWorldSizePerViewportPixel(float CameraDepthCm)
	{
		return CameraDepthCm / OriginalFocalLengthPixels;
	}

	static constexpr int32 KernelAtlasCellSize = 35;
	static constexpr int32 SelectorFamilyCount = 4;
	static constexpr int32 SelectorPhaseCount = 8;
	static constexpr int32 RadiusCount = 10;
	static constexpr int32 FarDepth1616 = 0x05900000; // 0x590 = 1424 original world units

	// FUN_00496da0 computes (0x5900000 - projectedDepth) / 0x5900000.
	static int32 ComputeDepthScale1616(float CameraDepthCm);
	static FSimCopterEffectKernelMetrics ComputeKernelMetrics(uint8 EffectClass, int32 DepthScale1616);

	// Exact selector tables at 0x00504828..0x00504847.
	static int32 GetSelectorFamily(uint8 EffectClass);
	static uint8 GetSelectorPaletteIndex(uint8 EffectClass, int32 Phase);

	// Microsoft C runtime rand() recurrence used by the executable.
	static uint32 AdvanceRandom(uint32& State);

	// Exact sparse write stencil decoded from the 0x10-mode jump table at
	// 0x0049a49c. A negative phase means the original renderer left that
	// framebuffer pixel untouched.
	static FSimCopterEffectStencilMetrics GetStencilMetrics(int32 Radius);
	static int32 GetStencilPhaseOffset(int32 Radius, int32 PixelX, int32 PixelY);
	static int32 ConsumeSelectorPhase(int32 Radius);

	// Builds all four selector families, ten radius cases, and eight possible
	// DAT_00505c48 starting phases into a nearest-filtered transparent atlas.
	static UTexture2D* CreateSelectorAtlas(UObject* Outer, const TArray<FColor>& Palette);

	static FVector2D GetAtlasUV(
		uint8 EffectClass,
		int32 Phase,
		int32 Radius,
		int32 PixelX,
		int32 PixelY);
};
