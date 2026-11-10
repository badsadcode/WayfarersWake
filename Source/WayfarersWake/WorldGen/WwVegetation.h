// Deterministic forest rules (port of Thornwake Game/Rewrite/Vegetation.cs). Trees sit on world-aligned cells, so the
// same tree appears at the same place whatever tile or LOD level asks for it. Engine-free and thread-safe.
#pragma once

#include "WwWorld.h"

namespace ww
{
struct FLatticeTree
{
	i64 Cx = 0, Cz = 0;        // the lattice cell: the tree's identity (felling, saves)
	double X = 0, Y = 0, Z = 0; // metres; Y is the ground height at the trunk less 0.1 m
	int Species = 0;            // index into FVegetation::Species()
	float Scale = 1, Yaw = 0;
};

class FVegetation
{
public:
	/// Tree model names in Thornwake's order (the first 17 have art; the rest were procedural placeholders there).
	static const std::vector<std::string>& Species();
	static constexpr int SpeciesWithArt = 17;

	/// 0..1 forest cover at a point: broad patches of woodland and meadow, thinning toward the tree line; the biome
	/// sets how much forest there is.
	static double ForestDensity(const FTerrainSampler& Sampler, double x, double z, const FTerrainSample& t);
	/// 0..1: below the tree line (follows the climate with biomes on).
	static double Treeline(const FTerrainSampler& Sampler, double x, double z, double elevation, u64 seed);
	/// The lattice's cell size for this world (denser forests: smaller cells).
	static double LatticeCell(const FTerrainSampler& Sampler);
	/// The tree of lattice cell (cx, cz); false where none grows.
	static bool TreeAt(const FTerrainSampler& Sampler, i64 cx, i64 cz, FLatticeTree& Out);
	/// The trees standing in a square of ground (min corner x0, z0; side `size` metres).
	static void TreesIn(const FTerrainSampler& Sampler, double x0, double z0, double size, std::vector<FLatticeTree>& Out);
};
}
