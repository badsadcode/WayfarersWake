// Grass and small plants as populations (port of Thornwake Game/Rewrite/GroundCover.Flora.cs, Flora.cs and the parts of
// GroundCover.cs they use). Plants are not independent random dots: after the hard rules (no grass in water, on rock,
// on steep slopes), a candidate's chance comes from three scales of world-space noise: macro regions that are sparse
// or lush, warped patches a few metres across, and the candidate's own random values. Each patch also has an identity,
// a community ("short grass with odd tall tufts", "wet meadow", ...). Water plants follow the same idea with fuzzy
// depth bands. Engine-free and thread-safe; a tile's plants depend only on the world and the tile.
#pragma once

#include "WwWorld.h"

namespace ww
{
enum class EFloraForm : uint8_t { Upright, Floating, Underwater };

/// A plant sprite: file name in Thornwake's Assets/Flora, height range in metres, how it is drawn.
struct FFloraSpecies { const char* File; float MinHeight, MaxHeight; EFloraForm Form; };

/// One plant: tile-local position in metres (Y up), turn about the vertical, lean, size (the sprite is 1 m, scaled),
/// colour multiplier.
struct FFloraInstance
{
	int Species = 0;
	float X = 0, Y = 0, Z = 0, Yaw = 0, Tilt = 0, Size = 1;
	float R = 1, G = 1, B = 1;
};

/// A level-0 tile as the plant rules see it: the vertex grid (Side x Side, Spacing metres apart) with what the terrain
/// mesh carries at each vertex.
struct FFloraGrid
{
	int Side = 0;
	double Spacing = 1, X0 = 0, Z0 = 0;
	const float* Height = nullptr;        // ground height
	const float* Up = nullptr;            // vertical part of the surface normal
	const double* Forest = nullptr;       // forest cover
	const FTerrainSample* Samples = nullptr;
	const float* Shore = nullptr;         // 0.5 + depth below the nearest water surface / 2 m (0.5 = waterline)
	const float* Plants = nullptr;        // biome plant cover
	const float* Wetness = nullptr;       // biome wetness
	const float* Grass = nullptr;         // biome grass colour, three floats per vertex (display values)
	bool bClimateLine = false;            // the plant cover already carries the grass line (worlds with biomes)
};

class FFlora
{
public:
	static constexpr int SpeciesCount = 28;
	static const FFloraSpecies& Species(int Index);

	static u64 CoverSeed(const FTerrainSampler& Sampler);
	/// Grass cover 0..1 at a world point: macro lushness and the patch field.
	static double Cover(double wx, double wz, u64 seed, double minWavelength, double& lush, double& dx, double& dz, double& sel);
	/// Land plants of a level-0 tile: grass, tall grass, wild wheat, wet-meadow plants. `amount` is Thornwake's grass
	/// density setting (1 = its default; Tom plays with 3).
	static void Land(const FFloraGrid& Grid, double size, double sea, u64 seed, double amount, std::vector<FFloraInstance>& Out);
	/// Water plants of a level-0 tile: reeds, cattails and rushes along still and slow water, floating leaves on ponds
	/// and lakes, submerged plants on beds, seagrass, kelp and seaweed in the sea.
	static void Water(const FFloraGrid& Grid, double size, double sea, u64 seed, const FBiomeMap& biomes, double amount, std::vector<FFloraInstance>& Out);
};
}
