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
// The Unreal components render a low-resolution virtual effect layer and scale
// its cards, dither dots, and gaps together to the modern viewport.
class SIMCOPTERREMAKE_API FSimCopterEffectRasterizer
{
public:
	// DAT_004f9750 == 0x10 selects the shipped high-resolution renderer. Its gameplay
	// viewport is 560x400 inside a 640x480, 8-bit framebuffer. The 280x200 viewport
	// and 320x240 framebuffer are the alternate low-resolution mode.
	static constexpr int32 OriginalViewportWidth = 560;
	static constexpr int32 OriginalViewportHeight = 400;
	static constexpr int32 OriginalFramebufferStride = 640;
	static constexpr int32 OriginalFramebufferHeight = 480;
	static constexpr int32 LowResolutionViewportWidth = 280;
	static constexpr int32 LowResolutionViewportHeight = 200;
	static constexpr int32 LowResolutionFramebufferStride = 320;
	static constexpr int32 LowResolutionFramebufferHeight = 240;
	// Modern presentation intentionally simulates face-0x1a on the original
	// low-resolution gameplay viewport, then scales that virtual layer as a whole.
	static constexpr int32 DitherSimulationViewportWidth = LowResolutionViewportWidth;
	static constexpr int32 DitherSimulationViewportHeight = LowResolutionViewportHeight;
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
