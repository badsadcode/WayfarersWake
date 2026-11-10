// Port of Thornwake Game/Rewrite/GroundCover.Flora.cs (plant populations), Flora.cs (the species) and the grid helpers
// of GroundCover.cs. Single-precision steps are kept where Thornwake computes in float.
#include "WwFlora.h"
#include <functional>

namespace ww
{
namespace
{
enum
{
	ShortA = 0, ShortB = 1, MediumA = 2, MediumB = 3, TallA = 4, TallB = 5, TallC = 6, TallD = 7, Lush = 8,
	Seeded = 9, WheatYoung = 10, WheatGolden = 11,
	ReedsCommon = 12, ReedsRush = 13, Cattails = 14, Flowering = 15,
	LilyPads = 16, WaterLilies = 17, Duckweed = 18,
	Submerged = 19, Seagrass = 20, EelgrassTall = 21, Broadleaf = 22, Feathery = 23, Mixed = 24, GreenAlgae = 25, RedAlgae = 26, Kelp = 27,
};

const FFloraSpecies All[FFlora::SpeciesCount] = {
	{ "grass_short_a", .45f, .7f, EFloraForm::Upright }, { "grass_short_b", .45f, .7f, EFloraForm::Upright },
	{ "grass_medium_a", .6f, .9f, EFloraForm::Upright }, { "grass_medium_b", .6f, .9f, EFloraForm::Upright },
	{ "grass_tall_a", .8f, 1.15f, EFloraForm::Upright }, { "grass_tall_b", .8f, 1.15f, EFloraForm::Upright },
	{ "grass_tall_c", .85f, 1.2f, EFloraForm::Upright }, { "grass_tall_d", .85f, 1.25f, EFloraForm::Upright },
	{ "grass_lush", .7f, 1.05f, EFloraForm::Upright }, { "grass_seeded", .9f, 1.25f, EFloraForm::Upright },
	{ "wheat_young", .9f, 1.2f, EFloraForm::Upright }, { "wheat_golden", .95f, 1.25f, EFloraForm::Upright },
	{ "reeds_common", 1.9f, 2.6f, EFloraForm::Upright }, { "reeds_rush", 1.3f, 1.8f, EFloraForm::Upright },
	{ "cattails", 1.7f, 2.3f, EFloraForm::Upright }, { "flowering_aquatic", .7f, 1.05f, EFloraForm::Upright },
	{ "lily_pads", 1.f, 1.8f, EFloraForm::Floating }, { "water_lilies", 1.f, 1.6f, EFloraForm::Floating }, { "duckweed", 1.2f, 2.2f, EFloraForm::Floating },
	{ "submerged_plant", .5f, 1.1f, EFloraForm::Underwater }, { "seagrass", .5f, .9f, EFloraForm::Underwater },
	{ "eelgrass_tall", .8f, 1.6f, EFloraForm::Underwater }, { "broadleaf_aquatic", .6f, 1.1f, EFloraForm::Underwater },
	{ "feathery_aquatic", .6f, 1.2f, EFloraForm::Underwater }, { "mixed_aquatic", .6f, 1.1f, EFloraForm::Underwater },
	{ "green_algae", .4f, .8f, EFloraForm::Underwater }, { "red_algae", .5f, 1.f, EFloraForm::Underwater }, { "kelp", 2.5f, 5.5f, EFloraForm::Underwater },
};

struct FMix { int Species; float Weight; };
struct FCommunity { float Density, Scale; std::vector<FMix> Mix; };

// Land communities (indices used by the habitat weights below; species -1: wild wheat).
const FCommunity LandCommunities[6] = {
	{ .5f, .9f, { { ShortA, 3 }, { ShortB, 3 }, { MediumA, 1 } } },                                                  // sparse short grass
	{ .85f, 1.f, { { ShortA, 3 }, { ShortB, 2 }, { MediumA, 2 }, { MediumB, 1 }, { TallA, .5f }, { TallB, .5f } } }, // short, odd tall tufts
	{ 1.15f, 1.05f, { { Lush, 3 }, { MediumB, 3 }, { MediumA, 2 }, { TallC, 1 } } },                                 // thick lush grass
	{ 1.f, 1.f, { { ShortA, 2 }, { MediumA, 2 }, { -1, 2.5f }, { Seeded, 1 } } },                                    // grass with wild wheat
	{ 1.05f, 1.05f, { { TallA, 2 }, { TallB, 2 }, { TallC, 2 }, { TallD, 2 }, { Seeded, 1.5f }, { Lush, 1 } } },     // tall stand
	{ 1.15f, 1.05f, { { Lush, 3 }, { TallC, 2 }, { TallD, 1.5f }, { ReedsRush, .5f }, { Flowering, .25f } } },       // wet meadow
};
const FCommunity ShoreCommunities[4] = {
	{ 1.f, 1.f, { { ReedsCommon, 4 }, { ReedsRush, 1 } } },                      // reed bed
	{ 1.f, 1.f, { { Cattails, 4 }, { ReedsCommon, 1 } } },                       // cattail stand
	{ .9f, 1.f, { { ReedsRush, 3 }, { Flowering, 1.5f }, { Lush, 1 } } },        // rushes with flowers
	{ .45f, .9f, { { ReedsRush, 1 }, { Lush, 2 }, { TallC, 1 } } },              // sparse bank
};
const float ReferenceGrass[3] = { 0x56 / 255.f, 0x6b / 255.f, 0x3f / 255.f }; // temperate biome grass

float Unit(u64 h, int shift) { return (float)Hashing::Unit(Hashing::Mix(h + (u64)shift)); }
int ClampI(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
float ClampF(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
int Nearest(const FFloraGrid& g, double x, double z)
{
	int i = ClampI((int)std::nearbyint(x / g.Spacing), 0, g.Side - 1), j = ClampI((int)std::nearbyint(z / g.Spacing), 0, g.Side - 1);
	return j * g.Side + i;
}

/// Height, normal Y, forest and wetness at tile-local (x, z), matching the mesh triangles.
bool At(const FFloraGrid& g, double x, double z, float& height, float& up, float& forest, bool& wet)
{
	height = 0; up = 0; forest = 0; wet = false;
	double gx = x / g.Spacing, gz = z / g.Spacing;
	int i = (int)std::floor(gx), j = (int)std::floor(gz);
	if (i < 0 || j < 0 || i >= g.Side - 1 || j >= g.Side - 1) return false;
	double fx = gx - i, fz = gz - j;
	int n = g.Side, a = j * n + i, b = a + 1, c = a + n, d = c + 1;
	double ha = g.Height[a], hb = g.Height[b], hc = g.Height[c], hd = g.Height[d];
	height = (float)(fx + fz <= 1 ? ha + (hb - ha) * fx + (hc - ha) * fz : hd + (hc - hd) * (1 - fx) + (hb - hd) * (1 - fz));
	int nearest = (fz < .5 ? j : j + 1) * n + (fx < .5 ? i : i + 1);
	up = g.Up[nearest];
	forest = (float)(g.Forest[a] * (1 - fx) * (1 - fz) + g.Forest[b] * fx * (1 - fz) + g.Forest[c] * (1 - fx) * fz + g.Forest[d] * fx * fz);
	wet = g.Samples[a].Water != EWaterKind::None || g.Samples[b].Water != EWaterKind::None || g.Samples[c].Water != EWaterKind::None || g.Samples[d].Water != EWaterKind::None;
	return true;
}

float Bilinear(const FFloraGrid& g, const float* f, double x, double z)
{
	double gx = DetMath::Clamp(x / g.Spacing, 0, g.Side - 1.001), gz = DetMath::Clamp(z / g.Spacing, 0, g.Side - 1.001);
	int i = (int)gx, j = (int)gz; float fx = (float)(gx - i), fz = (float)(gz - j);
	int a = j * g.Side + i;
	return f[a] * (1 - fx) * (1 - fz) + f[a + 1] * fx * (1 - fz) + f[a + g.Side] * (1 - fx) * fz + f[a + g.Side + 1] * fx * fz;
}

/// Per-tile fields on the vertex grid (bilinear between vertices).
struct FField
{
	const FFloraGrid& G;
	std::vector<float> Density, Lush, WarpX, WarpZ, Coarse, NearWater, Reeds;
	FField(const FFloraGrid& g, u64 seed) : G(g)
	{
		int side = g.Side, n = side * side;
		Density.resize(n); Lush.resize(n); WarpX.resize(n); WarpZ.resize(n); Coarse.resize(n); NearWater.resize(n); Reeds.resize(n);
		for (int j = 0; j < side; j++)
			for (int i = 0; i < side; i++)
			{
				int k = j * side + i;
				double wx = g.X0 + i * g.Spacing, wz = g.Z0 + j * g.Spacing, lush, dx, dz, sel;
				Density[k] = (float)FFlora::Cover(wx, wz, seed, 0, lush, dx, dz, sel);
				Lush[k] = (float)lush; WarpX[k] = (float)dx; WarpZ[k] = (float)dz; Coarse[k] = (float)sel;
				// Reed and floating-plant patches along the water (their own clumps, ~12 m).
				Reeds[k] = (float)DetMath::Smooth((Noise::Fbm(wx + dx, wz + dz, 12, 2, 0, seed + 7) + .1) / .4);
			}
		// Nearness to water (within ~8 m), from the tile's own samples.
		int r = (int)std::nearbyint(8 / g.Spacing);
		if (r < 1) r = 1;
		for (int j = 0; j < side; j++)
			for (int i = 0; i < side; i++)
			{
				double best = 1e300;
				for (int b = j - r < 0 ? 0 : j - r; b <= (j + r > side - 1 ? side - 1 : j + r); b++)
					for (int a = i - r < 0 ? 0 : i - r; a <= (i + r > side - 1 ? side - 1 : i + r); a++)
					{
						EWaterKind w = g.Samples[b * side + a].Water;
						if (w == EWaterKind::None || w == EWaterKind::Ocean) continue;
						double d = (double)((a - i) * (a - i) + (b - j) * (b - j));
						if (d < best) best = d;
					}
				NearWater[j * side + i] = best == 1e300 ? 0 : (float)Mx(0, 1 - std::sqrt(best) * g.Spacing / 8);
			}
	}
	float At(const std::vector<float>& f, double x, double z) const { return Bilinear(G, f.data(), x, z); }
};

int Pick(const float* weights, int count, double u)
{
	double total = 0;
	for (int i = 0; i < count; i++) total += weights[i] > 0 ? weights[i] : 0;
	double t = u * total;
	for (int i = 0; i < count; i++) { t -= weights[i] > 0 ? weights[i] : 0; if (t <= 0) return i; }
	return count - 1;
}

int PickSpecies(const FCommunity& c, double u, bool golden)
{
	double total = 0;
	for (const FMix& m : c.Mix) total += m.Weight;
	double t = u * total;
	for (const FMix& m : c.Mix) { t -= m.Weight; if (t <= 0) return m.Species < 0 ? (golden ? WheatGolden : WheatYoung) : m.Species; }
	int last = c.Mix.back().Species;
	return last < 0 ? WheatYoung : last;
}

/// The patch cell a point belongs to: a warped grid of 8 m or 22 m cells (the scale varies by region).
u64 PatchCell(const FField& f, double x, double z, double wx, double wz, u64 salt)
{
	double px = wx + f.At(f.WarpX, x, z) * .8, pz = wz + f.At(f.WarpZ, x, z) * .8;
	u64 coarse = Hashing::Lattice((i64)std::floor(px / 22), (i64)std::floor(pz / 22), salt ^ 0xC0A5EULL);
	if (Hashing::Unit(coarse) < f.At(f.Coarse, x, z)) return coarse;
	return Hashing::Lattice((i64)std::floor(px / 8), (i64)std::floor(pz / 8), salt ^ 0xF1AEULL);
}

void GrassTint(const FFloraGrid& g, double x, double z, double dry, float jitter, FFloraInstance& out)
{
	float r = 1, gr = 1, b = 1;
	if (g.Grass)
	{
		int k = Nearest(g, x, z) * 3;
		r = ClampF(1 + (g.Grass[k] / ReferenceGrass[0] - 1) * .6f, .6f, 1.5f);
		gr = ClampF(1 + (g.Grass[k + 1] / ReferenceGrass[1] - 1) * .6f, .6f, 1.5f);
		b = ClampF(1 + (g.Grass[k + 2] / ReferenceGrass[2] - 1) * .6f, .6f, 1.5f);
	}
	// Fresh ... dry patches (sun-bleached, yellower).
	float d = (float)DetMath::Clamp(dry, 0, 1);
	r *= 1 + .08f * d; gr *= 1 + .01f * d; b *= 1 - .18f * d;
	out.R = r * jitter; out.G = gr * jitter; out.B = b * jitter;
}

void Place(u64 h, float size, float x, float y, float z, float maxTilt, FFloraInstance& out)
{
	out.Yaw = Unit(h, 13) * 6.2831855f;
	out.Tilt = (Unit(h, 14) - .5f) * 2 * maxTilt;
	out.Size = size; out.X = x; out.Y = y; out.Z = z;
}

/// Calls `emit` for each world-aligned cell of size `cell` inside the tile (tile-local x, z and the cell's hash).
void Cells(double minX, double minZ, double size, double cell, u64 salt, const std::function<void(double, double, u64)>& emit)
{
	i64 c0x = (i64)std::floor(minX / cell), c0z = (i64)std::floor(minZ / cell);
	i64 c1x = (i64)std::ceil((minX + size) / cell), c1z = (i64)std::ceil((minZ + size) / cell);
	for (i64 cz = c0z; cz < c1z; cz++)
		for (i64 cx = c0x; cx < c1x; cx++)
		{
			u64 h = Hashing::Lattice(cx, cz, salt);
			double x = (cx + Hashing::Unit(h)) * cell - minX, z = (cz + Hashing::Unit(Hashing::Mix(h))) * cell - minZ;
			if (x < 0 || z < 0 || x >= size || z >= size) continue;
			emit(x, z, Hashing::Mix(h ^ 0x5DEECE66DULL));
		}
}
}

const FFloraSpecies& FFlora::Species(int Index) { return All[Index]; }

u64 FFlora::CoverSeed(const FTerrainSampler& Sampler) { return Hashing::Mix((u64)(i64)Sampler.Plan->Manifest.Seed + 0xC0FFEEULL); }

double FFlora::Cover(double wx, double wz, u64 seed, double minWavelength, double& lush, double& dx, double& dz, double& sel)
{
	double macro = Noise::Fbm(wx, wz, 170, 3, minWavelength, seed + 1);
	lush = DetMath::Smooth((macro + .4) / .8);
	dx = 7 * Noise::Fbm(wx, wz, 30, 2, minWavelength, seed + 2); dz = 7 * Noise::Fbm(wx + 51.3, wz - 17.1, 30, 2, minWavelength, seed + 3);
	double fine = Noise::Fbm(wx + dx, wz + dz, 7, 2, minWavelength, seed + 4), coarse = Noise::Fbm(wx + dx, wz + dz, 24, 3, minWavelength, seed + 5);
	sel = DetMath::Smooth((Noise::Fbm(wx, wz, 90, 2, minWavelength, seed + 6) + .25) / .5);
	double p = fine + (coarse - fine) * sel;
	// Lush regions lower the threshold: bigger, joined-up patches; sparse ones leave islands.
	double thr = .15 - .45 * (lush - .5);
	double d = DetMath::Smooth((p - thr + .12) / .3);
	// Between patches: sparse to normal grass, never a bare lawn.
	return Mx(d, .2 + .2 * lush);
}

void FFlora::Land(const FFloraGrid& g, double tileSize, double sea, u64 seed, double amount, std::vector<FFloraInstance>& Out)
{
	FField field(g, seed ^ 0xF10AULL);
	float weights[6];
	// Candidates: a little denser than the classic tufts at the same setting; the fields thin them.
	Cells(g.X0, g.Z0, tileSize, 1.25 / std::sqrt(2 * amount) * .78, seed ^ 0x9A55ULL, [&](double x, double z, u64 h)
	{
		float height, up, forest; bool wet;
		if (!At(g, x, z, height, up, forest, wet) || wet) return;
		if (up < .82f || height - sea < 1.4) return;
		float alpine = g.bClimateLine ? 0 : (float)DetMath::Smooth((height - sea - 1700) / 500);
		float plants = g.Plants[Nearest(g, x, z)];
		float habitat = (1 - .7f * forest) * (1 - alpine) * (float)DetMath::Smooth((up - .82) / .08) * plants * plants;
		if (habitat <= .01f) return;
		float density = field.At(field.Density, x, z), lush = field.At(field.Lush, x, z), nearWater = field.At(field.NearWater, x, z);
		float wetness = g.Wetness[Nearest(g, x, z)];
		u64 cell = PatchCell(field, x, z, g.X0 + x, g.Z0 + z, seed);
		weights[0] = 2 + 4 * (1 - plants) + 2 * forest + 2 * (1 - lush);
		weights[1] = 4;
		weights[2] = 1 + 3 * lush;
		weights[3] = .8f * (1 - forest) * (1 - wetness);
		weights[4] = .8f + 2 * lush + 3 * nearWater;
		weights[5] = 6 * wetness + 4 * nearWater; // wetlands: sedge and rush meadows
		const FCommunity& c = LandCommunities[Pick(weights, 6, Hashing::Unit(cell))];
		if (Unit(h, 1) > density * c.Density * habitat) return;
		u64 cellH = Hashing::Mix(cell);
		int species = PickSpecies(c, Unit(h, 2), Hashing::Unit(cellH) < .3);
		const FFloraSpecies& sp = All[species];
		float size = (sp.MinHeight + (sp.MaxHeight - sp.MinHeight) * Unit(h, 3)) * c.Scale * (.85f + .3f * density) * (1 - .25f * forest);
		double dry = Hashing::Unit(Hashing::Mix(cellH)) * .45 + (1 - plants) * .4 - nearWater * .3;
		FFloraInstance out;
		out.Species = species;
		GrassTint(g, x, z, dry, .94f + .12f * Unit(h, 4), out);
		Place(h, size, (float)x, height - .03f, (float)z, .12f, out);
		Out.push_back(out);
	});
}

void FFlora::Water(const FFloraGrid& g, double tileSize, double sea, u64 seed, const FBiomeMap& biomes, double amount, std::vector<FFloraInstance>& Out)
{
	FField field(g, seed ^ 0xA0A7ULL);
	float weights[4];
	double thin = std::sqrt(Mx(.2, amount));
	// Reeds, cattails, rushes: the bank just above the waterline and the shallows (not sea beaches, not fast water).
	Cells(g.X0, g.Z0, tileSize, .7 / thin, seed ^ 0x2EEDULL, [&](double x, double z, u64 h)
	{
		float height, up, forest; bool wet;
		if (!At(g, x, z, height, up, forest, wet) || up < .8f) return;
		const FTerrainSample& t = g.Samples[Nearest(g, x, z)];
		float shore = Bilinear(g, g.Shore, x, z);
		float place;
		if (t.Water == EWaterKind::None)
		{
			if (shore < .22f || shore > .66f || height - sea < 1.2) return;
			place = 1 - std::fabs(shore - .47f) / .25f;
		}
		else
		{
			double depth = t.WaterLevel - t.Height;
			if (t.Water == EWaterKind::Ocean || depth > (t.Water == EWaterKind::River ? .25 : .55)) return;
			place = (float)(1 - DetMath::Smooth((depth - .15) / .4));
		}
		float wetness = g.Wetness[Nearest(g, x, z)], clump = field.At(field.Reeds, x, z);
		// Clumps along the bank, not a continuous hedge around every pool.
		if (Unit(h, 1) > place * clump * clump * (.45f + .55f * wetness) * 1.2f) return;
		u64 cell = PatchCell(field, x, z, g.X0 + x, g.Z0 + z, seed ^ 0x5EEDULL);
		weights[0] = 3; weights[1] = 1 + 1.5f * wetness + (t.Water == EWaterKind::Pond ? 1 : 0); weights[2] = 2.5f; weights[3] = 2.5f;
		const FCommunity& c = ShoreCommunities[Pick(weights, 4, Hashing::Unit(cell))];
		if (Unit(h, 5) > c.Density) return;
		int species = PickSpecies(c, Unit(h, 2), false);
		const FFloraSpecies& sp = All[species];
		float size = (sp.MinHeight + (sp.MaxHeight - sp.MinHeight) * Unit(h, 3)) * c.Scale * (.8f + .3f * clump);
		FFloraInstance out;
		out.Species = species;
		if (species == ShortA || species == Lush || species == TallC) GrassTint(g, x, z, .1, .95f + .1f * Unit(h, 4), out);
		else out.R = out.G = out.B = .92f + .14f * Unit(h, 4);
		Place(h, size, (float)x, height - .05f, (float)z, .08f, out);
		Out.push_back(out);
	});
	// Floating leaves on still water (ponds, lakes), in rafts.
	const std::vector<FBiome>& pool = biomes.Registry.Pool;
	Cells(g.X0, g.Z0, tileSize, 1.7 / thin, seed ^ 0xF10A7ULL, [&](double x, double z, u64 h)
	{
		const FTerrainSample& t = g.Samples[Nearest(g, x, z)];
		if (!(t.Water == EWaterKind::Pond || t.Water == EWaterKind::Lake)) return;
		double depth = t.WaterLevel - t.Height;
		float band = (float)(DetMath::Smooth((depth - .2) / .4) * (1 - DetMath::Smooth((depth - 1.8) / 1.4)));
		int ba, bb; double share;
		biomes.Sample(g.X0 + x, g.Z0 + z, ba, bb, share);
		const FBiome& v = Unit(h, 6) < share ? pool[ba] : pool[bb];
		if (v.bHyacinth) return; // bayou: water hyacinth (not ported)
		float floating = (float)v.FloatingPlants, least = t.Water == EWaterKind::Pond ? .35f : .2f;
		float abundance = floating > least ? floating : least;
		double raft = DetMath::Smooth((Noise::Fbm(g.X0 + x, g.Z0 + z, 18, 2, 0, seed ^ 0x4AF7ULL) - .02) / .3);
		if (Unit(h, 1) > band * raft * abundance * 1.6f) return;
		u64 cell = PatchCell(field, x, z, g.X0 + x, g.Z0 + z, seed ^ 0x11E5ULL);
		bool duck = Hashing::Unit(cell) < (t.Water == EWaterKind::Pond ? .4 : .15);
		int species = duck ? Duckweed : Unit(h, 2) < .22f ? WaterLilies : LilyPads;
		const FFloraSpecies& sp = All[species];
		FFloraInstance out;
		out.Species = species;
		out.Size = sp.MinHeight + (sp.MaxHeight - sp.MinHeight) * Unit(h, 3);
		out.Yaw = Unit(h, 4) * 6.2831855f;
		out.X = (float)x; out.Y = (float)t.WaterLevel + .015f + .004f * Unit(h, 5); out.Z = (float)z;
		out.R = out.G = out.B = .92f + .14f * Unit(h, 7);
		Out.push_back(out);
	});
	// Submerged plants: lake, pond and river beds; seagrass meadows, kelp and seaweed in the sea.
	Cells(g.X0, g.Z0, tileSize, 1.3 / thin, seed ^ 0x5EA6ULL, [&](double x, double z, u64 h)
	{
		const FTerrainSample& t = g.Samples[Nearest(g, x, z)];
		if (t.Water == EWaterKind::None) return;
		double depth = t.WaterLevel - t.Height;
		if (depth < .35) return;
		float bed, up, forest; bool wet;
		if (!At(g, x, z, bed, up, forest, wet)) return;
		bed = (float)Mn(bed, t.Height);
		bool rocky = up < .93f;
		double wx = g.X0 + x, wz = g.Z0 + z;
		double meadow = Noise::Fbm(wx, wz, 30, 2, 0, seed ^ 0x5EA9ULL);
		int species; float chance;
		if (t.Water == EWaterKind::Ocean)
		{
			float grassBand = (float)(DetMath::Smooth((depth - .5) / .6) * (1 - DetMath::Smooth((depth - 3.5) / 2.5)));
			float kelpBand = (float)(DetMath::Smooth((depth - 2.5) / 2) * (1 - DetMath::Smooth((depth - 14) / 6)));
			double forestPatch = DetMath::Smooth((Noise::Fbm(wx, wz, 45, 2, 0, seed ^ 0x6E1BULL) - .05) / .3);
			float seagrass = grassBand * (float)DetMath::Smooth((meadow + .05) / .35) * (rocky ? .3f : 1);
			float kelp = kelpBand * (float)forestPatch * (rocky ? 1.2f : .6f);
			float weed = (rocky ? .7f : .1f) * (float)(1 - DetMath::Smooth((depth - 6) / 4));
			float total = seagrass + kelp + weed;
			if (total <= .01f) return;
			double u = Unit(h, 2) * total;
			species = u < seagrass ? (Unit(h, 3) < .7f ? Seagrass : EelgrassTall)
				: u < seagrass + kelp ? Kelp
				: Unit(h, 3) < .5f ? RedAlgae : GreenAlgae;
			chance = (total < 1 ? total : 1) * .8f;
		}
		else
		{
			float shallow = (float)(DetMath::Smooth((depth - .35) / .4) * (1 - DetMath::Smooth((depth - 2) / 1.5)));
			float deep = (float)(DetMath::Smooth((depth - 1.5) / 1) * (1 - DetMath::Smooth((depth - 5) / 2.5)));
			float patch = (float)DetMath::Smooth((meadow + .1) / .4);
			float total = (shallow + deep) * patch;
			if (total <= .01f) return;
			double u = Unit(h, 2) * (shallow + deep);
			if (t.Water == EWaterKind::River)
				species = rocky ? GreenAlgae : Unit(h, 3) < .75f ? EelgrassTall : Submerged;
			else if (u < shallow)
			{
				float s = Unit(h, 3);
				species = s < .4f ? Submerged : s < .7f ? EelgrassTall : Feathery;
			}
			else
				species = rocky && Unit(h, 3) < .4f ? GreenAlgae : Unit(h, 3) < .45f ? Broadleaf : Unit(h, 3) < .75f ? Mixed : Feathery;
			chance = (total < 1 ? total : 1) * .7f;
		}
		if (Unit(h, 1) > chance) return;
		const FFloraSpecies& sp = All[species];
		float size = sp.MinHeight + (sp.MaxHeight - sp.MinHeight) * Unit(h, 4);
		// Below the surface with room to spare: at sea the wave troughs dip ~0.5 m; lakes have wind waves too.
		double room = t.Water == EWaterKind::Ocean ? (depth - .7) * .75 : t.Water == EWaterKind::Lake ? (depth - .5) * .85 : depth - .25;
		if (room < .3) return;
		size = (float)Mn(size, room);
		FFloraInstance out;
		out.Species = species;
		out.R = out.G = out.B = .9f + .15f * Unit(h, 5);
		Place(h, size, (float)x, bed - .05f, (float)z, .1f, out);
		Out.push_back(out);
	});
}
}
