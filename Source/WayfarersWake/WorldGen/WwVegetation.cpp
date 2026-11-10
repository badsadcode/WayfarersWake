// Port of Thornwake Game/Rewrite/Vegetation.cs (forest cover, tree line, the tree lattice and species choice).
#include "WwVegetation.h"

namespace ww
{
const std::vector<std::string>& FVegetation::Species()
{
	static const std::vector<std::string> Names = {
		"Pine_A", "Pine_B", "Pine_C", "Spruce_A", "Spruce_B", "Oak_A", "Oak_B", "Oak_C", "Birch_Green_A", "Birch_Green_B",
		"Birch_Green_C", "Maple_Autumn_A", "Poplar_A", "Willow_A", "Sparse_A", "Birch_Autumn_A", "Birch_Autumn_B",
		// Placeholder plants in Thornwake (procedural models for biomes without art yet).
		"Palm_A", "Palm_B", "Cactus_A", "Cactus_B", "Dead_A", "Dead_B", "Acacia_A", "Jungle_A", "Cypress_A", "Shrub_Dry",
		"Bald_Cypress_A", "Bald_Cypress_B",
	};
	return Names;
}

namespace
{
const double TreeCell = 7;
const int Broadleaf[] = { 5, 6, 7, 8, 9, 10, 11, 12, 15, 16, 0, 1 };
const int Mixed[] = { 0, 1, 2, 3, 4, 8, 9, 5, 6, 15 };
const int Conifer[] = { 0, 1, 2, 3, 4, 3, 4, 14 };

u64 ForestSeed(const FTerrainSampler& s) { return Hashing::Mix((u64)(i64)s.Plan->Manifest.Seed * 0x2545F4914F6CDD1DULL + 77); }
u64 TreeSeed(const FTerrainSampler& s) { return Hashing::Mix((u64)(i64)s.Plan->Manifest.Seed + 0x7EE5ULL); }

void Candidate(i64 cx, i64 cz, double cell, u64 seed, double& x, double& z, u64& h)
{
	h = Hashing::Lattice(cx, cz, seed);
	x = (cx + .15 + .7 * Hashing::Unit(h)) * cell;
	z = (cz + .15 + .7 * Hashing::Unit(Hashing::Mix(h))) * cell;
}
}

double FVegetation::Treeline(const FTerrainSampler& Sampler, double x, double z, double elevation, u64 seed)
{
	const FClimate& climate = *Sampler.Plan->Climate;
	if (climate.Settings.Biomes)
	{
		double temperature, rain;
		climate.At(x, z, elevation, temperature, rain);
		double t = temperature + .8 * Noise::Gradient(x / 9000, z / 9000, seed + 5);
		return DetMath::Smooth((t + 2.5) / 4); // trees thin from +1.5 C and stop at -2.5 C
	}
	double treeline = 1750 + 350 * Noise::Gradient(x / 9000, z / 9000, seed + 5);
	return DetMath::Smooth((treeline - elevation) / 220);
}

double FVegetation::ForestDensity(const FTerrainSampler& Sampler, double x, double z, const FTerrainSample& t)
{
	const FWorldPlan& plan = *Sampler.Plan;
	double coverSetting = plan.Style.ForestCover;
	if (coverSetting <= 0 || !(t.Water == EWaterKind::None || t.Water == EWaterKind::Pond) || t.Lava > .02) return 0; // (no trees in lava)
	const std::vector<FBiome>& pool = plan.Biomes->Registry.Pool;
	int a, b; double share;
	plan.Biomes->Sample(x, z, a, b, share);
	double inWater = 1;
	if (t.Water == EWaterKind::Pond)
	{
		// Bayou cypress stand in the pools (not too deep); other trees keep out of the water.
		inWater = (pool[a].WaterTrees * share + pool[b].WaterTrees * (1 - share)) * (1 - DetMath::Smooth((t.WaterLevel - t.Height - 1.2) / .6));
		if (inWater <= .001) return 0;
	}
	double elevation = (t.Water == EWaterKind::Pond ? t.WaterLevel : t.Height) - plan.Profile().SeaLevel;
	if (elevation < 2.5) return 0;
	double biomeForest = pool[a].TreeDensity * share + pool[b].TreeDensity * (1 - share);
	biomeForest *= inWater;
	if (biomeForest <= .001) return 0;
	// Denser biomes grow bigger forests; sparse ones (steppe, savanna, tundra) only scattered trees.
	double sparse = Mn(1, biomeForest);
	coverSetting *= DetMath::Clamp(biomeForest, 1, 1.8);
	u64 seed = ForestSeed(Sampler);
	// Steep enough that land is mostly either forest or meadow, with short edges.
	double patches = Noise::Fbm(x, z, 2600, 4, 60, seed, .55) * 3.4 + .45 + (coverSetting - 1) * 1.1;
	double cover = DetMath::Smooth(patches) * Treeline(Sampler, x, z, elevation, seed) * DetMath::Smooth((elevation - 2.5) / 4);
	return cover * (1 - .7 * DetMath::Smooth((t.Mountain - .45) / .35)) * sparse;
}

double FVegetation::LatticeCell(const FTerrainSampler& Sampler)
{
	return TreeCell / std::sqrt(Mx(1, Sampler.Plan->Style.ForestDensity));
}

bool FVegetation::TreeAt(const FTerrainSampler& Sampler, i64 cx, i64 cz, FLatticeTree& Out)
{
	const FWorldPlan& plan = *Sampler.Plan;
	double cell = LatticeCell(Sampler), keep = Mn(1, plan.Style.ForestDensity), sea = plan.Profile().SeaLevel;
	double x, z; u64 h;
	Candidate(cx, cz, cell, TreeSeed(Sampler), x, z, h);
	double chance = Hashing::Unit(Hashing::Mix(h ^ 0xABCDEFULL));
	if (chance > .95) return false; // never a perfectly closed canopy
	FTerrainSample t = Sampler.Sample(x, z, 2);
	double density = ForestDensity(Sampler, x, z, t);
	if (chance > density * .92 * keep) return false;
	// Avoid steep ground: compare with a neighbour sample.
	double slope = std::fabs(Sampler.Height(x + 2, z, 4) - t.Height) / 2;
	if (slope > .7) return false;
	double elevation = t.Height - sea;
	u64 pick = Hashing::Mix(h + 99);
	int species;
	double sizeMin = .8, sizeMax = 1.25;
	FRiverHit river;
	if (plan.Hydrology->NearestRiver(x, z, 0, river) && river.Distance < river.Width * .5 + 18 && (pick & 3) != 0) species = 13; // willows by rivers
	else
	{
		// A tree from the biome's tree sets (classic look: species by elevation).
		const std::vector<FBiome>& pool = plan.Biomes->Registry.Pool;
		int a, b; double share;
		plan.Biomes->Sample(x, z, a, b, share);
		const FBiome& biome = Hashing::Unit(Hashing::Mix(pick + 7)) < share ? pool[a] : pool[b];
		if (biome.bClassic)
		{
			if (elevation < 550) species = Broadleaf[pick % 12];
			else if (elevation < 1250) species = Mixed[pick % 10];
			else species = Conifer[pick % 8];
		}
		else
		{
			species = 0;
			if (!biome.Trees.empty())
			{
				double total = biome.Trees.back().second, u = Hashing::Unit(Hashing::Mix(pick + 3)) * total;
				const std::string* name = &biome.Trees.back().first;
				for (const auto& entry : biome.Trees) if (u < entry.second) { name = &entry.first; break; }
				const std::vector<std::string>& names = Species();
				for (int i = 0; i < (int)names.size(); i++) if (names[i] == *name) { species = i; break; }
			}
		}
		sizeMin = biome.TreeSizeMin; sizeMax = biome.TreeSizeMax;
	}
	Out.Cx = cx; Out.Cz = cz; Out.X = x; Out.Z = z;
	Out.Y = (float)t.Height - .1f;
	Out.Species = species;
	Out.Scale = (float)(sizeMin + (sizeMax - sizeMin) * Hashing::Unit(Hashing::Mix(pick)));
	Out.Yaw = (float)(Hashing::Unit(Hashing::Mix(pick + 1)) * 3.141592653589793 * 2);
	return true;
}

void FVegetation::TreesIn(const FTerrainSampler& Sampler, double x0, double z0, double size, std::vector<FLatticeTree>& Out)
{
	double cell = LatticeCell(Sampler);
	i64 c0x = (i64)std::floor(x0 / cell), c0z = (i64)std::floor(z0 / cell);
	i64 c1x = (i64)std::floor((x0 + size) / cell), c1z = (i64)std::floor((z0 + size) / cell);
	u64 seed = TreeSeed(Sampler);
	// Inclusive: the cell straddling the far edge may put its tree inside this square.
	for (i64 cz = c0z; cz <= c1z; cz++)
		for (i64 cx = c0x; cx <= c1x; cx++)
		{
			double x, z; u64 h;
			Candidate(cx, cz, cell, seed, x, z, h);
			if (x < x0 || z < z0 || x >= x0 + size || z >= z0 + size) continue;
			FLatticeTree tree;
			if (TreeAt(Sampler, cx, cz, tree)) Out.push_back(tree);
		}
}
}
