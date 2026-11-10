// The water surface of a terrain tile (port of Thornwake TileBuilder.BuildWater and Flow). One sheet on the tile's
// vertex grid: water vertices at their own level, bank vertices beside them carried under the ground at their wet
// neighbours' level (so the shoreline is where the sheet meets the ground, not a row of squares), the sea reaching
// ~14 m under the beach. Per vertex: depth, the kind of water, how much river water is in it (plume), an edge fade,
// and the current. Engine-free and thread-safe.
// Not ported: dug ground and the local water simulation, shore data for
// sea waves, obstacle wakes.
#pragma once

#include "WwWorld.h"

namespace ww
{
struct FWaterSheet
{
	/// Kind codes as Thornwake's mesh colour: ordered by what meets what, so values blended across a triangle never
	/// pass through a third kind.
	static constexpr float SeaCode = 0, RiverCode = 1 / 3.f, LakeCode = 2 / 3.f, PondCode = 1;

	bool bAny = false;
	std::vector<float> Level;    // per vertex, metres
	std::vector<float> Depth;    // per vertex: (level - ground) / 20, clamped 0..1
	std::vector<float> Kind;     // per vertex: kind code
	std::vector<float> Plume;    // per vertex: share of river water
	std::vector<float> Edge;     // per vertex: 1 on water, fading over the sheet's extension
	std::vector<float> FlowX, FlowZ; // per vertex: current, m/s (east, south)
	std::vector<int> Indices;    // triangles (a, b, c / b, d, c per square), vertex = j * Side + i

	/// Samples and Heights: Side x Side, row by row. X0, Z0: the tile's corner in the world.
	/// Falls: where given, a waterfall's face is left to the falling water (the sheet fades out and is cut there).
	void Build(const FTerrainSample* Samples, const double* Heights, int Side, double Spacing, double SeaLevel, const FHydrology& Rivers, double X0, double Z0,
		const class FFallSet* Falls = nullptr);
};
}
