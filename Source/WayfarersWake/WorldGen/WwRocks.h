// Stones, rocks, boulders, cliff slabs and the small props of the ground (port of Thornwake Game/Rewrite/GroundCover.cs:
// Rocks, Props, RockyShore; the shapes of RockMeshes.cs, PropCatalog.cs and Visuals.Stump). Placement comes from
// world-aligned cells hashed with the world's seed, so the same stone lies in the same place for everyone.
// Engine-free and thread-safe.
// Not ported here: greenery on cliff rocks, picked-up stones and berries (removals), collision.
#pragma once

#include "WwFlora.h"

namespace ww
{
/// One rock or prop: kind, its basis (three axes, Thornwake's: x, up, z) and place (tile-local metres, y up), colour
/// multiplier and the rock look (texture layer).
struct FRockInstance
{
	int Kind = 0;
	float Ax[3] = { 1, 0, 0 }, Ay[3] = { 0, 1, 0 }, Az[3] = { 0, 0, 1 };
	float X = 0, Y = 0, Z = 0;
	float R = 1, G = 1, B = 1, Look = 0;
};

/// A distant tree, drawn as a card: its foot (tile-local metres, y up), height, crown colour (linear) and whether it is
/// a conifer.
struct FFarTree { float X = 0, Y = 0, Z = 0, Height = 0, R = 0, G = 0, B = 0; bool bConifer = false; };

/// What the rock and prop rules read beyond the plant rules' grid (per vertex).
struct FRockGrid
{
	FFloraGrid Base;
	const float* Normal = nullptr;    // surface normal: x, up, z
	const float* Rock = nullptr;      // biome rock colour, three floats (display values)
	const float* PropRocks = nullptr; // biome multiplier for rocks
	const float* PropLogs = nullptr;  // biome multiplier for bushes, stumps and logs
	const float* RockLook = nullptr;  // rock look index of the nearer biome
	const float* Cold = nullptr;      // share of cold (glacial) biomes
};

class FRocks
{
public:
	/// Kinds as in Thornwake: the five small shapes; then six shapes of worn cliff block, near, coarse (middle distance)
	/// and for the lip of a cliff; then the formations of the rock kit (columns, slabs, tiered, tor; four variants each).
	enum { Stone = 0, Rock = 1, Boulder = 2, CliffSlab = 3, BigBoulder = 4, CliffShapes = 6, CliffRock = 5, CliffCoarse = 11, CliffLip = 17, Formation = 23, RockKinds = 39 };
	enum { Bush = 0, BerryBush = 1, Stump = 2, Log = 3, PropKinds = 4 };

	/// 0..1: a rocky stretch of shore here (patches along the sea and lakes; bare rock instead of sand, blocky rocks).
	static float RockyShore(double worldX, double worldZ, double aboveWater);
	/// Rocks of a tile (levels 0..2; fewer kinds on coarser levels). `amount` is the world's rock amount.
	static void Scatter(const FRockGrid& Grid, int Level, double size, double sea, u64 seed, double amount, std::vector<FRockInstance>& Out);
	/// Distant tree cards of a tile (Thornwake GroundCover.FarTrees): beyond the full trees, sparser but larger with
	/// distance, so cards cover the ground about as much as the trees they stand for. Reads the grid's heights, slopes,
	/// forest cover and water.
	static void FarTrees(const FFloraGrid& Grid, int Level, double size, double sea, u64 seed, double density, std::vector<FFarTree>& Out);
	/// Bushes, stumps and logs of a tile (levels 0..1).
	static void Props(const FRockGrid& Grid, double size, double sea, u64 seed, double amount, std::vector<FRockInstance>& Out);
};

/// A mesh as a triangle soup: three vertices a triangle (counter-clockwise seen from outside), a normal and a colour
/// (display values) each. Axes: x, up, z; unit size before the instance's basis.
struct FSoup
{
	std::vector<float> Position, Normal, Colour; // three floats a vertex
	int Count() const { return (int)Position.size() / 3; }
};

class FPropShapes
{
public:
	/// A rock kind's mesh: a jittered, flattened icosphere, stone-coloured with a hint of moss on top, auto-smoothed.
	static FSoup Rock(int Kind);
	/// A block of worn cliff rock (unit size, centred): 0-1 blocks, 2 stepped in ledges, 3 wider at the top, 4 split by a
	/// cleft, 5 rounded; coarse for the middle distance.
	static FSoup CliffBlock(int Shape, bool bCoarse);
	/// A rock kit formation (metres): kind 0 columns, 1 slab stack, 2 tiered pile, 3 tor; four variants each.
	static FSoup Formation(int Kind, int Variant);
	/// A distant tree card's shape: two upright quads crossed and a level one at crown height (seen from above a forest is a
	/// carpet of crowns). One unit tall and wide; the colour carries the picture's place (r, g) and "from above" (b).
	static FSoup Card();
	/// A prop's mesh: bush, berry bush, stump, log.
	static FSoup Prop(int Kind);
};
}
