#pragma once

#include "Formats/MaxisMeshReader.h"

namespace SimCopterPowerLinePlacement
{
inline void NormalizeTerrainRelativePole(FMaxisMeshObject& Mesh)
{
	// FUN_0047c0c0 selects WR16..19 (0x57..0x5a) for slope tiles.
	// Their shaft reaches 64 original units versus WR14/15's 32; the high
	// crossbar and wire anchors likewise carry a 32-unit terrain allowance.
	// Remake placement already starts at the terrain surface. Remove that
	// allowance, retaining the shaft base, crossbar thickness and low wire ends.
	if (Mesh.Header.Id < 0x57 || Mesh.Header.Id > 0x5a) return;
	constexpr int32 TerrainAllowance = 32 * 65536;
	for (FMaxisMeshVertex& Vertex : Mesh.Vertices)
	{
		if (Vertex.Y > TerrainAllowance) Vertex.Y -= TerrainAllowance;
	}
}
}
