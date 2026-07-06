// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Authentic effect constants decoded from SimCopter.exe (see
// Docs/scratchpad/ghidra/out_effects_DECODED.md). Every original effect particle is a small
// palette-coloured sprite moved by velocity + gravity over a short lifetime - NOT a large
// alpha-blended quad. Colours below are the exact SIM3D palette entries (extracted from the
// sim3d1.max CMAP) that the original writes into each particle's colour field.
namespace SimCopterEffectFX
{
	// The sim world uses 16.16 fixed-point units where one 64-unit span is one tile. The remake's
	// tile is 400 cm, so one sim unit = 400/64 = 6.25 cm.
	static constexpr float OriginalUnitToCm = 6.25f;
	static constexpr float Fixed1616ToCm = OriginalUnitToCm / 65536.0f;

	// FUN_0048ed00 subtracts 0x280000 (= 40.0 units) * frameTime from each particle's Z velocity
	// every frame, i.e. a downward acceleration of 40 units/s^2. In cm that is 250 cm/s^2; we
	// render the arcs a touch heavier so short-lived specks visibly fall on-screen.
	static constexpr float GravityCmPerSec2 = 40.0f * OriginalUnitToCm * 2.0f; // 500

	// Convert a raw 8-bit SIM3D palette entry (sRGB bytes) to an unlit emissive colour with an
	// explicit dither/alpha weight. FLinearColor(FColor) sRGB-decodes the RGB so the unlit sprite
	// displays as the same colour the 8-bit framebuffer showed.
	inline FLinearColor Palette(uint8 R, uint8 G, uint8 B, float Alpha)
	{
		FLinearColor C(FColor(R, G, B, 255));
		C.A = Alpha;
		return C;
	}

	// Fire / kicked-dust ramp, palette indices 0x10..0x1F (dark-red -> orange -> amber). The
	// original animates fire embers and the impact/splash column through this run.
	inline FLinearColor FireRamp(float T, float Alpha = 1.0f)
	{
		static const uint8 Ramp[16][3] = {
			{0x8F,0x10,0x05},{0x90,0x1A,0x05},{0x9A,0x20,0x05},{0x9F,0x2F,0x05},
			{0xA5,0x35,0x05},{0xAA,0x3F,0x0A},{0xB0,0x45,0x0A},{0xB5,0x50,0x0A},
			{0xBF,0x5A,0x0A},{0xC0,0x60,0x0A},{0xCA,0x6F,0x0A},{0xCF,0x75,0x0A},
			{0xD0,0x7F,0x0A},{0xDA,0x85,0x0A},{0xDF,0x90,0x0A},{0xE5,0x9A,0x0A},
		};
		const int32 Index = FMath::Clamp(FMath::RoundToInt(T * 15.0f), 0, 15);
		return Palette(Ramp[Index][0], Ramp[Index][1], Ramp[Index][2], Alpha);
	}

	// Hot fire tips (palette 0x73 / 0x7B), used by the rising fire-trajectory sprites.
	inline FLinearColor FireTipBright(float Alpha = 1.0f) { return Palette(0xFF, 0xF0, 0x1F, Alpha); }
	inline FLinearColor FireTipPale(float Alpha = 1.0f)   { return Palette(0xFF, 0xDA, 0x6F, Alpha); }

	// Rotor-wash / spray colours (palette 0x08 pale green-white, 0x09 pale blue) - the water spray
	// look, "white and blue mixed".
	inline FLinearColor SprayWhite(float Alpha = 1.0f) { return Palette(0xC0, 0xDF, 0xC0, Alpha); }
	inline FLinearColor SprayBlue(float Alpha = 1.0f)  { return Palette(0x6E, 0xA6, 0xE6, Alpha); }
	inline FLinearColor WaterBlue(float Alpha = 1.0f)  { return Palette(0x3C, 0x7A, 0xD0, Alpha); }

	// Kicked-up dust over land - warm tan-brown.
	inline FLinearColor DustBrown(float Alpha = 1.0f)  { return Palette(0x9A, 0x78, 0x4E, Alpha); }
	inline FLinearColor DustDarkBrown(float Alpha = 1.0f) { return Palette(0x6E, 0x52, 0x30, Alpha); }

	// Sizes (original 16.16 sprite dimensions -> cm). These are the per-sprite full widths.
	static constexpr float WashPuffSizeCm  = 0x140000 * Fixed1616ToCm; // 125 (SMOKE puff)
	static constexpr float SmokeSizeCm     = 0x60000  * Fixed1616ToCm; // 37.5
	static constexpr float DebrisSizeCm    = 0x30000  * Fixed1616ToCm; // 18.75
	static constexpr float FireColumnSizeCm= 0x50000  * Fixed1616ToCm; // 31.25
	static constexpr float WashCardSizeCm  = 0x10000  * Fixed1616ToCm; // 6.25
}
