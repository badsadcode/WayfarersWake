// Port of Thornwake Game/WorldGen: TerrainSampler.cs, TerrainSampler.Water.cs, BiomeLandforms.cs, TerrainEdits.cs
// (terrain revisions 8..16). The height pipeline, one stage per method, water last so it carves through everything:
// coast -> sea | continent -> detail -> biome terrain -> rivers -> lakes -> wetland pools -> player edits.
#include "WwLandforms.h"

namespace ww
{
// ------------------------------------------------------------------------------------------------ player edits
static i64 EditChunk(i64 vertex) { return (i64)std::floor(vertex / (double)FTerrainEdits::ChunkVertices); }

float FTerrainEdits::Vertex(i64 x, i64 z) const
{
	i64 cx = EditChunk(x), cz = EditChunk(z);
	auto it = Chunks.find({ cx, cz });
	if (it == Chunks.end()) return 0;
	return (*it->second)[(size_t)((z - cz * ChunkVertices) * ChunkVertices + (x - cx * ChunkVertices))];
}

double FTerrainEdits::Delta(double x, double z) const
{
	if (bEmpty) return 0;
	std::lock_guard<std::mutex> Guard(Lock);
	double fx = std::floor(x), fz = std::floor(z);
	i64 ix = (i64)fx, iz = (i64)fz;
	double tx = x - fx, tz = z - fz;
	float a = Vertex(ix, iz), b = Vertex(ix + 1, iz), c = Vertex(ix, iz + 1), d = Vertex(ix + 1, iz + 1);
	if (a == 0 && b == 0 && c == 0 && d == 0) return 0;
	return a * (1 - tx) * (1 - tz) + b * tx * (1 - tz) + c * (1 - tx) * tz + d * tx * tz;
}

void FTerrainEdits::Apply(double x, double z, double radius, double amount, double limit, double& MinX, double& MinZ, double& MaxX, double& MaxZ)
{
	i64 x0 = (i64)std::floor(x - radius), x1 = (i64)std::ceil(x + radius);
	i64 z0 = (i64)std::floor(z - radius), z1 = (i64)std::ceil(z + radius);
	std::lock_guard<std::mutex> Guard(Lock);
	for (i64 cz = EditChunk(z0); cz <= EditChunk(z1); cz++)
		for (i64 cx = EditChunk(x0); cx <= EditChunk(x1); cx++)
		{
			auto it = Chunks.find({ cx, cz });
			auto next = std::make_shared<std::vector<float>>(it != Chunks.end() ? *it->second : std::vector<float>(ChunkVertices * ChunkVertices, 0.f));
			bool changed = false;
			for (int j = 0; j < ChunkVertices; j++)
				for (int i = 0; i < ChunkVertices; i++)
				{
					double vx = (double)(cx * ChunkVertices + i), vz = (double)(cz * ChunkVertices + j);
					double r = std::sqrt((vx - x) * (vx - x) + (vz - z) * (vz - z)) / radius;
					if (r >= 1) continue;
					double falloff = DetMath::Smooth(1 - r);
					int k = j * ChunkVertices + i;
					(*next)[k] = (float)DetMath::Clamp((*next)[k] + amount * falloff, -limit, limit);
					changed = true;
				}
			if (changed) { Chunks[{ cx, cz }] = next; bEmpty = false; }
		}
	VersionCounter++;
	MinX = (double)x0; MinZ = (double)z0; MaxX = (double)x1; MaxZ = (double)z1;
}

std::vector<std::pair<std::pair<i64, i64>, FTerrainEdits::FChunk>> FTerrainEdits::Snapshot() const
{
	std::lock_guard<std::mutex> Guard(Lock);
	return std::vector<std::pair<std::pair<i64, i64>, FChunk>>(Chunks.begin(), Chunks.end());
}

void FTerrainEdits::SetChunk(i64 cx, i64 cz, const std::vector<float>* Offsets)
{
	std::lock_guard<std::mutex> Guard(Lock);
	bool allZero = true;
	if (Offsets && Offsets->size() == (size_t)ChunkVertices * ChunkVertices) for (float v : *Offsets) if (v != 0) { allZero = false; break; }
	if (allZero) Chunks.erase({ cx, cz });
	else Chunks[{ cx, cz }] = std::make_shared<std::vector<float>>(*Offsets);
	bEmpty = Chunks.empty();
	VersionCounter++;
}

void FTerrainEdits::Clear()
{
	std::lock_guard<std::mutex> Guard(Lock);
	Chunks.clear(); bEmpty = true; VersionCounter++;
}

// ------------------------------------------------------------------------------------------------ sampler
static void BuildQuantiles(const std::function<double(double, double)>& noise, std::vector<double>& sorted, double& stdOut)
{
	const int N = 6000;
	sorted.resize(N);
	double sum = 0, sq = 0;
	for (int i = 0; i < N; i++)
	{
		// Spread over ~80 x 80 noise wavelengths (scale-free: the noise is evaluated at unit size).
		double u = Hashing::Unit(Hashing::Mix((u64)i * 2 + 11)) * 80, w = Hashing::Unit(Hashing::Mix((u64)i * 2 + 12)) * 80;
		sorted[i] = noise(u, w); sum += sorted[i]; sq += sorted[i] * sorted[i];
	}
	double mean = sum / N, stdv = std::sqrt(Mx(1e-12, sq / N - mean * mean));
	std::sort(sorted.begin(), sorted.end());
	for (int i = 0; i < N; i++) sorted[i] /= stdv;
	stdOut = stdv;
}

/// The value exceeded by a share `cover` of the samples.
static double Quantile(const std::vector<double>& sorted, double cover)
{
	double f = DetMath::Clamp(1 - cover, 0, 1) * (sorted.size() - 1);
	int i = (int)f; if (i >= (int)sorted.size() - 1) return sorted.back();
	return sorted[i] + (sorted[i + 1] - sorted[i]) * (f - i);
}

FTerrainSampler::FTerrainSampler(std::shared_ptr<const FWorldPlan> InPlan)
	: Plan(InPlan), P(InPlan->Manifest.Profile), Style(InPlan->Style), Macro(InPlan->Macro), Hydro(*InPlan->Hydrology)
{
	const FWorldManifest& m = Plan->Manifest;
	Ox = m.OriginX; Oz = m.OriginZ;
	auto SeedOf = [&](const char* layer) { return Hashing::FeatureSeed(m.Seed, 100 + FWorldManifest::CurrentGenerator, m.ProfileDigest, layer); };
	DetailSeed = SeedOf("detail"); RockSeed = SeedOf("rock"); CoastSeed = SeedOf("coast"); RegionSeed = SeedOf("region"); BankSeed = SeedOf("banks");
	PoolSeed = SeedOf("pools");
	Canyons = Plan->Canyons && Plan->Canyons->Count() > 0 ? Plan->Canyons.get() : nullptr;
	Cirques = Plan->Cirques && Plan->Cirques->Count() > 0 ? Plan->Cirques.get() : nullptr;
	Troughs = Plan->Troughs && Plan->Troughs->Count() > 0 ? Plan->Troughs.get() : nullptr; // (as the source: fjords alone do not count)
	Volcanoes = Plan->Volcanoes && Plan->Volcanoes->Count() > 0 ? Plan->Volcanoes : nullptr;
	LandformSeed = SeedOf("landforms");
	DuneAngle = Hashing::Unit(Hashing::Mix(LandformSeed)) * 6.283185307179586; // prevailing sand-moving wind of this world
	BuildQuantiles([this](double u, double v) { return PoolNoise(u, v, 1, 0); }, PoolQuantiles, PoolStd);
	BuildQuantiles([this](double u, double v) { return ChannelNoise(u, v, 1, 0); }, ChannelQuantiles, ChannelStd);
	// Distance along each sea-bound reach where it crosses the (raster) coastline; reaches continue out to sea.
	CoastAlongCache.assign(Hydro.Reaches.size(), 0.0);
	for (size_t index = 0; index < Hydro.Reaches.size(); index++)
	{
		const FRiverReach& reach = Hydro.Reaches[index];
		if (!reach.bEndsInSea) continue;
		int k = (int)reach.Points.size() - 1;
		while (k > 0)
		{
			double land, h, mo;
			Macro.Sample(reach.Points[k].X, reach.Points[k].Z, FWorldPlan::LandChannel, FWorldPlan::HeightChannel, FWorldPlan::MountainChannel, land, h, mo);
			if (land > 0) break;
			k--;
		}
		CoastAlongCache[index] = reach.Along[k];
	}
}

double FTerrainSampler::CoastAlong(int index) const { return CoastAlongCache[index]; }

FTerrainSample FTerrainSampler::Sample(double x, double z, double minWavelength) const
{
	double lx = x - Ox, lz = z - Oz;
	double land, macroHeight, mountainShare, rasterLand, coastDistance;
	Coast(x, z, lx, lz, minWavelength, land, macroHeight, mountainShare, rasterLand, coastDistance);
	if (land <= 0) return Sea(x, z, land, coastDistance);
	double baseHeight = Continent(land, rasterLand, macroHeight, coastDistance, mountainShare);
	double canyon = Canyons ? Canyons->Uplift(x, z) : 0; // (revision 21: raised land a river has cut through)
	baseHeight += canyon;
	// Revision 26: a fjord's mountain walls, raised like a canyon's land (rivers crossing them cut gorges).
	if (Troughs && Troughs->HasFjords())
	{
		double rocky;
		double lift = Troughs->Raise(x, z, coastDistance, rocky);
		baseHeight += lift; canyon += lift;
		if (rocky > 0) mountainShare = Mx(mountainShare, .5 * rocky * rocky); // (rugged near the walls, easing out)
	}
	// Revision 24: glacial troughs, the ground taken down to a flat floor between steep walls.
	double troughFlat = 0, troughRock = 0, scree = 0; bool fjord = false;
	if (Troughs) baseHeight = Troughs->Carve(x, z, baseHeight, troughFlat, troughRock, fjord, scree);
	double inland, detail, roughness;
	Detail(lx, lz, minWavelength, land, mountainShare, inland, detail, roughness);
	if (troughFlat > 0) detail *= 1 - .85 * troughFlat;
	// Revision 26: a fjord's floor below the sea is the sea (the walk ends here, as at the coast).
	if (fjord && baseHeight + detail < 0)
	{
		FTerrainSample sea;
		sea.Height = P.SeaLevel + baseHeight + detail; sea.Land = land; sea.Mountain = Mx(mountainShare, troughRock); sea.Detail = detail;
		sea.Water = EWaterKind::Ocean; sea.WaterLevel = P.SeaLevel; sea.WaterBody = -1;
		return sea;
	}

	double bare = 0, pointRock = 0;
	if (!Plan->PlanLandforms.empty()) { double planScree; detail += PlanGround(x, z, minWavelength, bare, planScree); scree = Mx(scree, planScree); }
	bare = Mx(Mx(bare, troughRock), pointRock);
	scree = Mx(scree, pointRock); // (a point's bare rock draws as loose rock)
	double beforeBiome = detail;

	// Stage 3, biome terrain: wetland flattening and pool settings, then the biome landforms (dunes, mesas, spires).
	bool wetland = false;
	double poolCover = 0, poolDepth = 0, poolSize = 16, poolChannels = 0, flatten = 0;
	FBiomeTerrainMods mods;
	if (Style.Revision >= 11 && inland > 0 && Plan->Biomes)
	{
		wetland = Plan->Biomes->TerrainAt(x, z, mods, Style.Revision >= 12);
		poolCover = mods.Pools; poolDepth = mods.PoolDepth; poolSize = mods.PoolSize; poolChannels = mods.PoolChannels; flatten = mods.Flatten;
		// Where there are pools the ground is flatter still (to ~8 % of the relief), so their surfaces stay level.
		if (poolCover > 0) flatten = Mx(flatten, Mn(.92, flatten + poolCover));
		if (wetland) detail *= 1 - flatten;
	}
	if (Style.Revision >= 12 && wetland && mods.Dunes + mods.Mesas + mods.Spires > 0)
		detail = BiomeLandforms(x, z, lx, lz, detail, inland, minWavelength, mods);
	// Revision 34: a glacial trough's floor is scoured flat of the biome's landforms too.
	if (troughFlat > 0 && Style.Revision >= 34) detail = beforeBiome + (detail - beforeBiome) * (1 - .85 * troughFlat);

	// Revision 23: cirques, carved after the biome's landforms so their tarns win.
	if (Cirques) detail += Cirques->Carve(x, z, baseHeight + detail, detail);

	EWaterKind water = EWaterKind::None; double level = NaNValue(); int body = -1; double riverDamp = 1;
	double height = Rivers(x, z, minWavelength, baseHeight, inland, detail, riverDamp, water, level, body, scree, FCanyons::Wall(canyon));
	// Revision 33: a gorge's talus is bare ground as the escarpments' debris slopes are (scree, not cliff rock).
	bare = Mx(bare, .65 * scree);
	double lakePlume = 0, lakeCells = Inf;
	LakesStage(x, z, height, water, level, body, lakePlume, lakeCells);
	// Revision 23: a cirque's tarn, still water of its own (drawn as a pond), where nothing else is water.
	if (Cirques && water == EWaterKind::None)
	{
		double tarn;
		if (Cirques->Tarn(x, z, height, tarn)) { water = EWaterKind::Pond; level = tarn; }
	}
	// Revision 29: a caldera's lake fills all of its floor below the water.
	if (Volcanoes && water == EWaterKind::None)
	{
		double cx, cz;
		if (Volcanoes->InCaldera(x, z, cx, cz))
		{
			int caldera = Hydro.LakeNear(cx, cz);
			if (caldera >= 0 && height < Hydro.Lakes[caldera].Level - P.SeaLevel) { water = EWaterKind::Lake; level = Hydro.Lakes[caldera].Level; body = caldera; }
		}
	}
	// Revision 31: an active volcano's lava lake, a level surface of molten rock.
	if (Volcanoes && water == EWaterKind::None) { double lava; if (Volcanoes->LavaLake(x, z, height, lava)) height = lava; }
	// Revision 28: a quiet volcano's crater lake.
	if (Volcanoes && water == EWaterKind::None) { double crater; if (Volcanoes->CraterLake(x, z, height, crater)) { water = EWaterKind::Pond; level = crater; } }

	// Stage 6, wetland pools (revision 11): standing water in pool biomes where nothing else is water.
	// Revision 22: none where a river has cut the ground down by over 2 m (valley sides, canyon and gorge walls).
	if (wetland && poolCover > 0 && water == EWaterKind::None && (Style.Revision < 22 || baseHeight + detail * riverDamp - height < 2))
	{
		double floor = .2 * inland + .05;
		// Pool water level: the ground without its short relief (the long waves only, flattened like the rest).
		const double PoolLevelWavelength = 500;
		double low = Noise::ErodedFbm(lx, lz, Style.DetailWavelength, 11, Mx(minWavelength, PoolLevelWavelength), DetailSeed, Style.DetailGain)
			* Style.LowlandDetail * roughness * (1 + 2 * mountainShare) * inland * (1 - flatten);
		double smooth = Mx(baseHeight + low, floor);
		double gx, gz;
		Macro.SampleGradient(FWorldPlan::HeightChannel, x, z, gx, gz);
		// No pools at lake shores (they would dig below the lake's surface where its reach ends).
		double cover = poolCover * riverDamp * DetMath::Smooth(land / .015) * (1 - DetMath::Smooth(mountainShare / .3)) * DetMath::Smooth((lakeCells - 1.6) / (Style.Revision >= 12 ? .38 : .9))
			* (1 - DetMath::Smooth((std::sqrt(gx * gx + gz * gz) - .015) / .03));
		if (cover > .005) height = Pool(lx, lz, height, smooth, Mn(cover, .85), poolDepth, poolSize, poolChannels, minWavelength, water, level);
	}

	// Stage 7, player edits: dug and raised ground on top; ground raised out of water is dry.
	double edit = Edits.Delta(x, z);
	if (edit != 0)
	{
		height += edit;
		if (water != EWaterKind::None && P.SeaLevel + height >= level - .02) { water = EWaterKind::None; level = NaNValue(); body = -1; }
	}
	double lavaCover = Volcanoes && water == EWaterKind::None ? Volcanoes->LavaFlow(x, z).Cover : 0;
	FTerrainSample out;
	if (water != EWaterKind::None) scree = 0;
	out.Lava = lavaCover;
	out.Height = P.SeaLevel + height; out.Land = land; out.Mountain = bare > 0 ? Mx(mountainShare, bare) : mountainShare; out.Detail = detail;
	out.Scree = scree;
	out.Water = water; out.WaterLevel = level; out.WaterBody = body; out.Plume = lakePlume;
	return out;
}

/// The big landforms' walking-scale relief (revision 18). Ground they lower is faded at lake shores.
double FTerrainSampler::PlanGround(double x, double z, double minWavelength, double& bare, double& scree) const
{
	double g = 0;
	bare = 0; scree = 0;
	for (const auto& Form : Plan->PlanLandforms) { double rock, s; g += Form->Ground(x, z, minWavelength, rock, s); bare = Mx(bare, rock); scree = Mx(scree, s); }
	if (g >= 0) return g;
	double cells;
	int lake = Hydro.LakeDistance(x, z, cells);
	return lake >= 0 ? g * DetMath::Smooth((cells - 1.6) / .38) : g;
}

/// Stage 1a, coast: the macro raster (land, eroded height, mountain share) and the sub-raster coastline wiggle.
void FTerrainSampler::Coast(double x, double z, double lx, double lz, double minWavelength, double& land, double& macroHeight, double& mountainShare,
	double& rasterLand, double& coastDistance) const
{
	Macro.Sample(x, z, FWorldPlan::LandChannel, FWorldPlan::HeightChannel, FWorldPlan::MountainChannel, land, macroHeight, mountainShare);
	rasterLand = land;
	coastDistance = Inf; // metres from the detailed coastline (estimate)
	if (land > -CoastBand && land < CoastBand)
	{
		double gx, gz;
		Macro.SampleGradient(FWorldPlan::LandChannel, x, z, gx, gz);
		double gradient = std::sqrt(gx * gx + gz * gz);
		double shift = Style.CoastDetailShift * Noise::Fbm(lx, lz, Macro.Cell * 2.5, 7, minWavelength, CoastSeed, .55);
		land += shift * gradient * DetMath::Smooth(1 - std::fabs(land) / CoastBand); // fades out: no step at the band edge
		coastDistance = std::fabs(land) / Mx(gradient, 1e-12);
	}
}

/// Stage 1b, the sea: the sea floor (deepening off the coast, river mouths' channels) - the walk ends here.
FTerrainSample FTerrainSampler::Sea(double x, double z, double land, double coastDistance) const
{
	double shore = Style.ShoreSlope;
	double depth = P.OceanDepth * DetMath::Smooth(-land / .6);
	if (shore > 0 && coastDistance < 1e7) depth = Mx(depth, 25 * (1 - DetMath::Exp(-coastDistance * .035 * shore / 25)));
	double seabed = P.SeaLevel - depth, plume = 0;
	// Revision 27: islands and skerries off a fjord coast.
	double isle = Troughs && Troughs->HasCoasts() ? Troughs->Skerries(x, z, land) : 0;
	seabed += isle;
	if (Style.MouthBlend > 0) seabed = MouthChannel(x, z, seabed, -1, plume);
	seabed += Edits.Delta(x, z);
	FTerrainSample out;
	out.Height = seabed; out.Land = land;
	// Revision 16: a sea pocket inside a lake's reach is part of the lake.
	if (Style.Revision >= 16)
	{
		double cells;
		int lake = Hydro.LakeDistance(x, z, cells);
		if (lake >= 0 && cells < 1.6 && Hydro.Lakes[lake].Level > P.SeaLevel + .05 && seabed < Hydro.Lakes[lake].Level)
		{
			out.Water = EWaterKind::Lake; out.WaterLevel = Hydro.Lakes[lake].Level; out.WaterBody = lake;
			return out;
		}
	}
	if (seabed < P.SeaLevel) { out.Water = EWaterKind::Ocean; out.WaterLevel = P.SeaLevel; out.Plume = plume; }
	else { out.Water = EWaterKind::None; out.WaterLevel = NaNValue(); out.Mountain = isle > 0 ? .5 : 0; }
	return out;
}

/// Stage 1c, continent: the land's base height from the eroded macro height and the shore ramp.
double FTerrainSampler::Continent(double land, double rasterLand, double macroHeight, double coastDistance, double& mountainShare) const
{
	double shore = Style.ShoreSlope;
	double baseHeight = macroHeight + P.LowlandHeight * (DetMath::Smooth(land / .4) - DetMath::Smooth(rasterLand / .4));
	baseHeight = Mx(P.LowlandHeight * DetMath::Smooth(land / .08) * .05, baseHeight);
	if (shore > 0 && coastDistance < 1e7) baseHeight = Mx(baseHeight, 3.5 * (1 - DetMath::Exp(-coastDistance * .025 * shore / 3.5)));
	mountainShare = DetMath::Clamp(mountainShare, 0, 1);
	return baseHeight;
}

/// Stage 2, detail: walking-scale relief (rolling hills by regional character, rock ridges in the mountains).
void FTerrainSampler::Detail(double lx, double lz, double minWavelength, double land, double mountainShare, double& inland, double& detail, double& roughness) const
{
	inland = DetMath::Smooth(land / .03);
	detail = 0; roughness = 1;
	if (inland > 0)
	{
		double character = DetMath::Smooth(.5 + .9 * Noise::Fbm(lx, lz, Style.RegionWavelength, 3, minWavelength, RegionSeed));
		roughness = Style.PlainsRoughness + (1 - Style.PlainsRoughness) * character;
		double rolling = Noise::ErodedFbm(lx, lz, Style.DetailWavelength, 11, minWavelength, DetailSeed, Style.DetailGain);
		detail = rolling * Style.LowlandDetail * roughness * (1 + 2 * mountainShare);
		if (mountainShare > .02)
		{
			double ridges = Noise::Ridged(lx, lz, 900, 8, minWavelength, RockSeed, .5) - .3;
			detail += ridges * Style.MountainDetail * P.MountainHeight * DetMath::Smooth(mountainShare / .6);
		}
		detail *= inland;
	}
}

void FTerrainSampler::HeightGrid(double x0, double z0, double spacing, int nx, int nz, double* output, double minWavelength) const
{
	double mw = minWavelength < 0 ? spacing * 2 : minWavelength;
	for (int j = 0; j < nz; j++)
	{
		double z = z0 + j * spacing;
		for (int i = 0; i < nx; i++) output[j * nx + i] = Sample(x0 + i * spacing, z, mw).Height;
	}
}

// ------------------------------------------------------------------------------------------------ biome landforms
double FTerrainSampler::BiomeLandforms(double x, double z, double lx, double lz, double detail, double inland, double minWavelength, const FBiomeTerrainMods& mods) const
{
	double cells;
	int lake = Hydro.LakeDistance(x, z, cells);
	double fade = inland * (lake >= 0 ? DetMath::Smooth((cells - 1.6) / .38) : 1);
	if (fade <= 0) return detail;
	if (mods.Dunes > 0)
	{
		double s = Mn(1, mods.Dunes) * fade;
		detail = detail * (1 - .6 * s) + DuneHeight(lx, lz, minWavelength) * s;
	}
	// Spire height fades out across the edge of spire biomes.
	if (mods.Spires > 0)
		detail += SpireHeight(lx, lz, minWavelength, Mn(1.5, mods.Spires) * 1) * fade * DetMath::Smooth(mods.SpirePresence / .6);
	if (mods.Mesas > 0)
	{
		double s = Mn(1, mods.Mesas) * fade;
		double up = MesaUplift(lx, lz, minWavelength);
		if (Style.Revision >= 19)
		{
			// Revision 19: the biome's strength lowers the uplift itself, and only uplift above ~15 m is tiered.
			up *= s;
			if (up > 0) detail += up + (Mx(0, MesaTerrace(up, lx, lz, minWavelength)) - up) * DetMath::Smooth((up - 15) / 15);
		}
		// (x a ramp over the first 4 m: the terrace of a near-zero uplift is not exactly zero; no step at the foot.)
		else if (up > 0) detail += Mx(0, MesaTerrace(up, lx, lz, minWavelength)) * DetMath::Smooth(up / 4) * s;
	}
	return detail;
}

double FTerrainSampler::DuneHeight(double lx, double lz, double minWavelength) const
{
	const double spacing = 90;
	double lod = 1 - DetMath::Smooth((minWavelength - spacing * .35) / (spacing * .25));
	if (lod <= 0) return 0;
	u64 seed = LandformSeed + 100;
	double ux = std::cos(DuneAngle), uz = std::sin(DuneAngle);
	double along = lx * ux + lz * uz, across = -lx * uz + lz * ux;
	double phase = along / spacing + 6 * Noise::Fbm(lx, lz, 6000, 2, minWavelength, seed)
		+ .9 * Noise::Fbm(across, along * .25, spacing * 5, 2, minWavelength, seed + 1)
		+ .25 * Noise::Fbm(lx, lz, spacing * 1.6, 2, minWavelength, seed + 2);
	double f = phase - std::floor(phase);
	const double windward = .68;
	double profile = f < windward ? DetMath::Smooth(f / windward) : 1 - DetMath::Smooth((f - windward) / (1 - windward));
	double field = DetMath::Smooth(.55 + 1.1 * Noise::Fbm(lx, lz, 4000, 2, minWavelength, seed + 3));
	double crest = .45 + .55 * DetMath::Smooth(.5 + 1.6 * Noise::Fbm(across, along, spacing * 3.5, 2, minWavelength, seed + 4));
	return (4 + 8 * field) * crest * profile * lod * 1;
}

double FTerrainSampler::MesaUplift(double lx, double lz, double minWavelength) const
{
	minWavelength = Mn(minWavelength, 150);
	u64 seed = LandformSeed + 210;
	double n = Noise::Fbm(lx, lz, 1400, 3, minWavelength, seed, .5);
	double height = (40 + 70 * DetMath::Smooth(.5 + Noise::Fbm(lx, lz, 6000, 2, minWavelength, seed + 1))) * 1;
	return height * DetMath::Smooth((n - .02) / .07);
}

double FTerrainSampler::MesaTerrace(double h, double lx, double lz, double minWavelength) const
{
	minWavelength = Mn(minWavelength, 150); // tiers stay put at every detail level
	u64 seed = LandformSeed + 200;
	double step = (12 + 9 * DetMath::Smooth(.5 + Noise::Fbm(lx, lz, 5000, 2, minWavelength, seed))) * 1;
	double offset = step * .35 * Noise::Fbm(lx, lz, 700, 3, minWavelength, seed + 1); // tier heights wander
	if (h <= 0) return h;
	double t = (h + offset) / step, k = std::floor(t), f = t - k;
	const double riser = .18;
	double g = .15 * f + .85 * DetMath::Smooth((f - (1 - riser)) / riser);
	return (k + g) * step - offset;
}

double FTerrainSampler::SpireHeight(double lx, double lz, double minWavelength, double density) const
{
	const double cell = 36;
	u64 seed = LandformSeed + 300;
	i64 ci = (i64)std::floor(lx / cell), cj = (i64)std::floor(lz / cell);
	double best = 0;
	for (i64 dj = -1; dj <= 1; dj++)
		for (i64 di = -1; di <= 1; di++)
		{
			u64 h = Hashing::Lattice(ci + di, cj + dj, seed);
			if (Hashing::Unit(h) > .45 * density) continue;
			double cx = (ci + di + .2 + .6 * Hashing::Unit(Hashing::Mix(h + 1))) * cell, cz = (cj + dj + .2 + .6 * Hashing::Unit(Hashing::Mix(h + 2))) * cell;
			double r = (3 + 6 * Hashing::Unit(Hashing::Mix(h + 3))) * 1;
			double u = Hashing::Unit(Hashing::Mix(h + 4)), height = Mx((12 + 43 * u * u) * 1, r * 4.2); // needles, not stumps
			double re = Mx(r, minWavelength * .6);
			double d = std::sqrt((lx - cx) * (lx - cx) + (lz - cz) * (lz - cz));
			if (d > re * 1.8) continue;
			double side = DetMath::Smooth((re - d) / (re * .2 + .6));        // near-vertical wall
			double talus = DetMath::Smooth((re * 1.8 - d) / (re * .8)) * .12; // a small flare of scree at the foot
			double top = .94 + .06 * DetMath::Smooth(1 - d / re);            // slightly rounded cap
			best = Mx(best, height * Mx(side * top, talus));
		}
	return best;
}

// ------------------------------------------------------------------------------------------------ rivers
/// The talus the last GorgeWall calls on this thread found (revision 33), for the river stage to report as scree.
extern thread_local double TTalus;

// Walking-scale detail fades over a wide corridor around a river (valley floors are smoother than hillsides).
static double DetailDamp(const FRiverHit& r)
{
	return DetMath::Smooth((r.Distance - r.Width * .5) / (FHydrology::Bank(r.Width) + FHydrology::MaxFloodplain(r.Width)));
}

/// Stage 4, rivers: channel, banks and slope-limited valley sides replace the terrain near a river.
double FTerrainSampler::Rivers(double x, double z, double minWavelength, double baseHeight, double inland, double detail, double& riverDamp,
	EWaterKind& water, double& level, int& body, double& scree, double wall) const
{
	double floor = .2 * inland + .05, height;
	FRiverHit rivers[32];
	int nearby = Hydro.NearbyRivers(x, z, rivers, 32);
	if (nearby > 0)
	{
		// One smooth profile per reach: true nearest distance, with water level/width/depth softly averaged over the
		// reach's nearby segments. Reaches then combine: deepest carve, highest bank.
		FRiverHit reaches[16];
		int count = 0;
		for (int i = 0; i < nearby && count < 16; i++)
		{
			int reach = rivers[i].Reach;
			bool seen = false;
			for (int j = 0; j < count; j++) if (reaches[j].Reach == reach) { seen = true; break; }
			if (seen) continue;
			double dmin = Inf, along = 0; int side = 0;
			for (int j = i; j < nearby; j++)
				if (rivers[j].Reach == reach && rivers[j].Distance < dmin) { dmin = rivers[j].Distance; side = rivers[j].Side; along = rivers[j].Along; }
			double tau = 6 + .45 * dmin, wsum = 0, surface = 0, width = 0, depth = 0, catchment = 0, alongSum = 0, alongWeight = 0;
			for (int j = i; j < nearby; j++)
			{
				if (rivers[j].Reach != reach) continue;
				double q = (rivers[j].Distance - dmin) / tau, w = 1 / (1 + q * q * (1 + q));
				wsum += w; surface += w * rivers[j].Surface; width += w * rivers[j].Width; depth += w * rivers[j].Depth; catchment += w * rivers[j].Catchment;
				double wa = 1 - DetMath::Smooth((rivers[j].Distance - dmin) / (3 + .2 * dmin));
				alongSum += wa * rivers[j].Along; alongWeight += wa;
			}
			// Revision 9: along is blended like the other attributes.
			if (Style.Revision >= 9 && alongWeight > 0) along = alongSum / alongWeight;
			FRiverHit& o = reaches[count++];
			o.Reach = reach; o.Distance = dmin; o.Surface = surface / wsum; o.Width = width / wsum; o.Depth = depth / wsum;
			o.Catchment = catchment / wsum; o.Along = along; o.Side = side;
		}
		double rise = Mx(baseHeight, floor), damp = 1;
		for (int i = 0; i < count; i++) damp = Mn(damp, DetailDamp(reaches[i]));
		riverDamp = damp;
		double terrain = Mx(baseHeight + detail * damp, floor);
		double weight, inside;
		// Revision 13: a river crossing a bigger river's lower valley floor flows at that floor, a little below it.
		if (Style.Revision >= 13 && count > 1)
		{
			double deltas[16];
			for (int i = 0; i < count; i++) deltas[i] = Profile(reaches[i], terrain, rise, minWavelength * .5, weight, inside, x, z, wall) - terrain;
			for (int i = 0; i < count; i++)
			{
				double other = 0; int deepest = -1;
				for (int j = 0; j < count; j++) if (j != i && deltas[j] < other) { other = deltas[j]; deepest = j; }
				if (deepest < 0) continue;
				// Never below the other river's own water (inside its channel the "floor" is its bed).
				double cap = Mx(P.SeaLevel + terrain + other - .3, reaches[deepest].Surface), excess = reaches[i].Surface - cap;
				if (excess <= 0) continue;
				reaches[i].Surface = reaches[i].Surface - excess * DetMath::Smooth(-other / .5);
			}
		}
		double carve = 0, raise = 0, wetInside = 0;
		for (int i = 0; i < count; i++)
		{
			TTalus = 0;
			double delta = Profile(reaches[i], terrain, rise, minWavelength * .5, weight, inside, x, z, wall) - terrain;
			if (delta > 0 && Style.MouthBlend > 0) delta *= LeveeFactor(reaches[i]);
			if (delta < carve) scree = Mx(scree, TTalus); // (the talus of the wall that wins: the deepest carve)
			carve = Mn(carve, delta);
			raise = Mx(raise, delta);
			if (inside > wetInside) { wetInside = inside; water = EWaterKind::River; level = reaches[i].Surface; body = reaches[i].Reach; }
		}
		// Inside a lake basin (ground below the lake surface) rivers carve but never raise banks.
		int basin = Style.BankVariation > 0 ? Hydro.LakeNear(x, z) : -1; // off in the build-5 terrain
		if (basin >= 0)
		{
			double below = Hydro.Lakes[basin].Level - P.SeaLevel - terrain;
			raise *= DetMath::Smooth((-.05 - below) / .4); // none once the ground is under the lake, full 0.35 m above it
		}
		// Inside a channel, another river's raised bank never lifts the bed above this water.
		height = wetInside > 0 ? Mn(terrain + carve, level - P.SeaLevel - .05) : terrain + carve + raise;
	}
	else height = Mx(baseHeight + detail, floor); // generated land never dips below the sea
	return height;
}

/// Stage 5, lakes: a lake fills every point below its surface within its reach; rivers through a lake are the lake.
void FTerrainSampler::LakesStage(double x, double z, double& height, EWaterKind& water, double& level, int& body, double& lakePlume, double& lakeCells) const
{
	// Revision 15: a river running through a lake below the lake's surface is the lake there.
	if (Style.Revision >= 15 && water == EWaterKind::River)
	{
		double cells;
		int lake = Hydro.LakeDistance(x, z, cells);
		if (lake >= 0 && cells < 1.6 && Hydro.Lakes[lake].Level > level + .05 && P.SeaLevel + height < Hydro.Lakes[lake].Level)
		{
			water = EWaterKind::Lake; level = Hydro.Lakes[lake].Level; body = lake; lakeCells = cells;
		}
	}
	if (water == EWaterKind::None && Style.Revision >= 10)
	{
		// Revision 10: the lake fills every point below its surface within ~1 cell of its cells (round reach);
		// between 1 and 1.6 cells the ground rises to a low shore just above the water.
		double cells;
		int lake = Hydro.LakeDistance(x, z, cells);
		if (lake >= 0) lakeCells = cells;
		if (lake >= 0 && cells < 1.6)
		{
			double surface = Hydro.Lakes[lake].Level - P.SeaLevel;
			if (cells > 1) height = Mx(height, height + (surface + .4 - height) * DetMath::Smooth((cells - 1) / .6));
			if (height < surface)
			{
				water = EWaterKind::Lake; level = Hydro.Lakes[lake].Level; body = lake;
				if (Style.MouthBlend > 0) height = MouthChannel(x, z, height, lake, lakePlume);
			}
		}
	}
	else if (water == EWaterKind::None)
	{
		int lake = Hydro.LakeNear(x, z);
		if (lake >= 0 && P.SeaLevel + height < Hydro.Lakes[lake].Level)
		{
			water = EWaterKind::Lake; level = Hydro.Lakes[lake].Level; body = lake;
			if (Style.MouthBlend > 0) height = MouthChannel(x, z, height, lake, lakePlume);
		}
	}
}

// ------------------------------------------------------------------------------------------------ wetland pools
double FTerrainSampler::PoolNoise(double x, double z, double size, double minWavelength) const
{
	return Noise::Fbm(x, z, size * 2.2, 3, minWavelength, PoolSeed, .42);
}

// Channels: lines where a domain-warped noise crosses zero (high = near the line), winding like fen and bayou leads.
double FTerrainSampler::ChannelNoise(double x, double z, double size, double minWavelength) const
{
	double wx = x + size * 3 * Noise::Fbm(x, z, size * 12, 2, minWavelength, PoolSeed + 8, .5);
	double wz = z + size * 3 * Noise::Fbm(x + 71.3, z - 19.7, size * 12, 2, minWavelength, PoolSeed + 9, .5);
	return -std::fabs(Noise::Fbm(wx, wz, size * 9, 3, minWavelength, PoolSeed + 7, .42));
}

double FTerrainSampler::Excess(double lx, double lz, double size, double minW, double cover, double channels) const
{
	double e = -Inf;
	if (channels < .999) e = PoolNoise(lx, lz, size, minW) / PoolStd - Quantile(PoolQuantiles, cover * (1 - channels));
	if (channels > .001) e = Mx(e, ChannelNoise(lx, lz, size, minW) / ChannelStd - Quantile(ChannelQuantiles, cover * channels));
	return e;
}

/// Standing-water pools (wetland biomes, revision 11): see TerrainSampler.Water.cs Pool.
double FTerrainSampler::Pool(double lx, double lz, double height, double smooth, double cover, double depth, double size, double channels,
	double minWavelength, EWaterKind& water, double& level) const
{
	double minW = Mn(minWavelength, size * .5); // at least the pools themselves, even on coarse distant tiles
	double coverIn = cover;
	cover = Mn(.9, cover * (Style.Revision >= 14 ? 1.15 : 1.3));
	double e;
	if (Style.Revision >= 14)
	{
		// Revision 14: evaluate at fixed sizes on a ladder of half octaves and blend the two around the wanted size.
		double ls = std::log2(size) * 2, f = std::floor(ls), t = ls - f;
		double s0 = std::pow(2.0, f / 2), s1 = std::pow(2.0, (f + 1) / 2);
		e = Excess(lx, lz, s0, Mn(minWavelength, s0 * .5), cover, channels) * (1 - t)
			+ Excess(lx, lz, s1, Mn(minWavelength, s1 * .5), cover, channels) * t;
	}
	else e = Excess(lx, lz, size, minW, cover, channels);
	const double soft = .4, bank = .25;
	double dev = Style.Revision >= 12 ? std::fabs(height - smooth) * 1.5 : 0;
	double below = Mx(0, smooth + .05 - height), drop = Style.Revision >= 12 ? below * 1.5 : 0;
	double nearRange = soft * 3 + dev, floorRange = soft * 2 + drop;
	if (-e > Mx(soft + nearRange, soft * 2 + floorRange)) return height;
	double fine = DetMath::Clamp(.3 * (height - smooth), -.8 * bank, .8 * bank);
	double profile = e >= 0 ? -depth * DetMath::Smooth(e / (soft * 5)) : bank * DetMath::Smooth(-e / soft);
	if (Style.Revision >= 14)
	{
		fine *= DetMath::Smooth(std::fabs(e) / (soft * 2));
		if (e >= 0) profile = -(.12 + (depth - .12) * DetMath::Smooth(e / (soft * 5)));
	}
	double nearW = 1 - DetMath::Smooth((-e - soft) / nearRange);
	double shaped = height + (smooth + profile + fine - height) * nearW;
	double grow = Style.Revision >= 12 ? DetMath::Smooth((coverIn - .005) / .035) : 1;
	if (e > -soft)
	{
		if (shaped < smooth && grow >= 1) { water = EWaterKind::Pond; level = P.SeaLevel + smooth; }
		else if (Style.Revision >= 12) shaped = Mx(shaped, smooth + .04);
	}
	else
	{
		shaped = Mx(shaped, smooth + .05 - below * DetMath::Smooth((-e - 2 * soft) / floorRange));
	}
	if (grow < 1) shaped = height + (shaped - height) * grow;
	return shaped;
}

// ------------------------------------------------------------------------------------------------ river profile
double FTerrainSampler::LeveeFactor(const FRiverHit& r) const
{
	const FRiverReach& reach = Hydro.Reaches[r.Reach];
	if (!reach.bEndsInSea && reach.EndLake < 0) return 1;
	double stretch = (reach.bEndsInSea ? 880 : 0) + r.Width * 6 + 80;
	return DetMath::Smooth((reach.Length() - r.Along - stretch * .6) / (stretch * .4));
}

/// Where rivers flow into the sea or a lake (lake -1 = sea): the channel continues as a shallowing groove into the bed.
double FTerrainSampler::MouthChannel(double x, double z, double bed, int lake, double& plume) const
{
	plume = 0;
	FRiverHit hits[16];
	int n = Hydro.NearbyRivers(x, z, hits, 16);
	double result = bed;
	for (int i = 0; i < n; i++)
	{
		const FRiverHit& r = hits[i];
		const FRiverReach& reach = Hydro.Reaches[r.Reach];
		if (lake < 0 ? !reach.bEndsInSea : reach.EndLake != lake) continue;
		double half = r.Width * .5, bank = FHydrology::Bank(r.Width), surface = r.Surface - P.SeaLevel;
		double tail = lake < 0 ? 880 : r.Width * 3 + 60;
		double fade = DetMath::Smooth((reach.Length() - r.Along) / tail);
		double groove;
		if (Style.Revision >= 9)
		{
			half *= 1.7; bank *= 3;
			double u = r.Distance / half;
			groove = r.Distance < half ? surface - r.Depth * fade * (1 - u * u)
				: surface + (bed - surface) * DetMath::Smooth((r.Distance - half) / bank);
		}
		else
		{
			double edge = surface - .3 * r.Depth * fade;
			groove = r.Distance < half
				? surface - r.Depth * fade * (1 - (r.Distance / half) * (r.Distance / half) * .7)
				: edge + (bed - edge) * DetMath::Smooth((r.Distance - half) / bank);
		}
		result = Mn(result, Mx(groove, bed - 50));
		plume = Mx(plume, (1 - DetMath::Smooth((r.Distance - half) / (r.Width * 1.5 + 60))) * (.4 + .6 * fade));
	}
	return result;
}

double FTerrainSampler::BeachWidth(const FRiverHit& r) const
{
	if (Style.BankVariation <= 0 || r.Side == 0 || r.Reach < 0) return 0;
	const FRiverReach& reach = Hydro.Reaches[r.Reach];
	double margin = r.Width * 2 + 25;
	double ends = DetMath::Smooth(r.Along / margin) * DetMath::Smooth((reach.Length() - r.Along) / margin);
	if (ends <= 0) return 0;
	u64 seed = BankSeed ^ Hashing::Mix((u64)r.Reach * 2 + (r.Side > 0 ? 1ULL : 0ULL) + 0xBEAC4ULL);
	double w = reach.Width[reach.Width.size() / 2];
	double n = 2.5 * Noise::Gradient(r.Along / Mx(60, w * 6) + 2.3, 8.1, seed)
		+ .5 * 2.5 * Noise::Gradient(r.Along / Mx(15, w * 1.5) + .7, 3.3, seed + 1);
	double amount = DetMath::Smooth((n + .1) / .9);
	return amount * FHydrology::Bank(r.Width) * .6 * Mn(1, Style.BankVariation) * ends;
}

double FTerrainSampler::WaterHalfWidth(const FRiverHit& r, double& shelf, double& slow) const
{
	double half = r.Width * .5;
	shelf = 0; slow = 0;
	if (Style.BankVariation <= 0 || r.Side == 0 || r.Reach < 0) return half;
	const FRiverReach& reach = Hydro.Reaches[r.Reach];
	double margin = r.Width * 2 + 25;
	double ends = DetMath::Smooth(r.Along / margin) * DetMath::Smooth((reach.Length() - r.Along) / margin);
	if (ends <= 0) return half;
	u64 seed = BankSeed ^ Hashing::Mix((u64)r.Reach * 2 + (r.Side > 0 ? 1ULL : 0ULL));
	double s = r.Along;
	double w = reach.Width[reach.Width.size() / 2];
	double broad = 2.5 * Noise::Gradient(s / Mx(300, w * 40) + 3.1, 1.7, seed) * .3;
	double wobble = 2.5 * Noise::Gradient(s / Mx(20, w * 2) + 5.3, 2.9, seed + 1) * (.4 + .09 * r.Width)
		+ 2.5 * Noise::Gradient(s / Mx(7, w * .7) + 1.9, 6.1, seed + 3) * (.12 + .03 * r.Width);
	double b = 2.5 * Noise::Gradient(s / Mx(40, w * 4.5) + 7.7, 4.4, seed + 2);
	double sign = b > 0 ? 1 : b < 0 ? -1 : 0;
	double bump = sign * DetMath::Smooth((std::fabs(b) - .35) / .6) * (b > 0 ? .7 : .35);
	double factor = 1 + (broad + wobble / half + bump) * Style.BankVariation * ends;
	factor = DetMath::Clamp(factor, .5, 2.1);
	slow = DetMath::Clamp(broad * Style.BankVariation * ends, -.5, 1.1) * half;
	shelf = DetMath::Smooth((factor - 1.1) / .35);
	return half * factor;
}

/// 0..1 over a river's last stretch before the sea or a lake (revision 9): the estuary.
double FTerrainSampler::Estuary(const FRiverHit& r) const
{
	if (Style.Revision < 9 || r.Reach < 0) return 0;
	const FRiverReach& reach = Hydro.Reaches[r.Reach];
	if (!reach.bEndsInSea && reach.EndLake < 0) return 0;
	double toMouth = (reach.bEndsInSea ? CoastAlong(r.Reach) : reach.Length()) - r.Along;
	double stretch = reach.Width.back() * 8 + 200; // (single precision, as the source)
	return 1 - DetMath::Smooth(toMouth / stretch);
}

double FTerrainSampler::Profile(const FRiverHit& r0, double terrain, double rise, double spacing, double& weight, double& inside, double x, double z, double wall) const
{
	bool rev9 = Style.Revision >= 9;
	double estuary = Estuary(r0);
	double shelf, slow;
	double nominal = r0.Width * .5 * (1 + .7 * estuary), shift = WaterHalfWidth(r0, shelf, slow) - r0.Width * .5;
	double bankWidth = FHydrology::Bank(r0.Width) * (1 + 2 * estuary);
	// Revision 9: only the slow widening and the cuts move the bank; bulges are a shallow ledge carved into the lower bank.
	double bulge = 0;
	if (rev9)
	{
		double fast = DetMath::Clamp(shift - slow, -bankWidth * .6, bankWidth);
		bulge = Mx(fast, 0);
		shift = slow + Mn(fast, 0);
	}
	// Revision 20: no bank irregularity in the gorge below a waterfall (each small shift became a step on a high wall).
	if (Style.Revision >= 20)
	{
		double inGorge = Mx(Hydro.Gorge(r0.Reach, r0.Along), wall);
		if (inGorge > 0) { shift *= 1 - inGorge; bulge *= 1 - inGorge; }
	}
	double fade = bankWidth * 1.5 + (rev9 ? 2 * std::fabs(shift) : 0);
	double carry = 1 - DetMath::Smooth((r0.Distance - nominal - Mx(shift, 0) - bankWidth) / fade);
	FRiverHit r = r0;
	r.Distance = Mx(0, r0.Distance - shift * carry);
	double half = nominal;
	double beach = BeachWidth(r0);
	if (spacing <= 2.5 || spacing > 40)
	{
		double h = ChannelProfile(r, half, terrain, rise, weight, inside, -1, beach, estuary, x, z, wall);
		if (!rev9 && shelf > 0 && inside > 0 && shift > 0) h = Shelf(h, r0, nominal, nominal + shift, shelf);
		if (bulge > 0) h = Ledge(h, r, half, bulge, inside);
		return h;
	}
	// Coarse LOD tiles: the channel edge is a straight ramp in distance so a coarse mesh draws the water edge where
	// it really is; channels narrower than the spacing are drawn at least that wide.
	half = Mx(half, spacing * .5);
	double band = spacing * 1.5, d = r.Distance, surface = r.Surface - P.SeaLevel;
	double outer = ChannelProfile(r, half, terrain, rise, weight, inside, half + band, beach, estuary, x, z, wall);
	inside = (half - d) / half;
	double unusedWeight, unusedInside;
	if (std::fabs(d - half) >= band) return ChannelProfile(r, half, terrain, rise, weight, unusedInside, -1, beach, estuary, x, z, wall);
	double g = Mx(.05, (outer - surface) / band);
	if (d >= half) return surface + g * (d - half);
	double bed = ChannelProfile(r, half, terrain, rise, unusedWeight, unusedInside, -1, 0, 0, x, z, wall);
	return Mx(bed, surface - g * (half - d));
}

/// A bulge of the water's edge (revision 9): the lower bank sinks to a shallow ledge and is water.
double FTerrainSampler::Ledge(double h, const FRiverHit& r, double half, double bulge, double& inside) const
{
	double surface = r.Surface - P.SeaLevel;
	double m = 1 - DetMath::Smooth((r.Distance - half - bulge) / 3 + .5);
	if (m <= 0) return h;
	double ledge = surface - (.2 + .08 * r.Depth) * DetMath::Smooth((half + bulge - r.Distance) / 2 + .3);
	double result = h - m * Mx(0, h - ledge);
	if (result < surface - .02 && inside <= 0) inside = .02 * m;
	return result;
}

/// A bulging bank's extra width is a shallow shelf (wading depth), not deep channel.
double FTerrainSampler::Shelf(double bed, const FRiverHit& r, double nominal, double half, double shelf) const
{
	double surface = r.Surface - P.SeaLevel, d = r.Distance;
	double ledge = surface - (.22 + .12 * r.Depth) * (1 - DetMath::Smooth((d - half * .85) / (half * .15 + .5)));
	double onShelf = DetMath::Smooth((d - nominal * .8) / Mx(1, nominal * .25)) * shelf;
	return DetMath::Lerp(bed, Mx(bed, ledge), onShelf);
}

double FTerrainSampler::ChannelProfile(const FRiverHit& r, double half, double terrain, double rise, double& weight, double& inside, double distance,
	double beach, double estuary, double x, double z, double wall) const
{
	double bank = FHydrology::Bank(r.Width) * (1 + 2 * estuary), d = distance >= 0 ? distance : r.Distance, surface = r.Surface - P.SeaLevel;
	double bankTop = surface + .6 + .15 * r.Depth; // (kept in this exact form: earlier terrain versions are bit-exact)
	if (estuary > 0) bankTop -= (.6 + .15 * r.Depth) * .6 * estuary;
	double extra = estuary > 0 ? Mx(0, half - r.Width * .5) + (bank - FHydrology::Bank(r.Width)) : 0; // estuary widening
	double flood = Mn(FHydrology::Floodplain(r.Width, rise - bankTop), FHydrology::MaxFloodplain(r.Width) - beach - extra);
	// Revision 20: below a waterfall the valley sides are a gorge's steep walls (about 70 degrees), not a floodplain.
	double gorge = Style.Revision >= 20 ? Mx(Hydro.Gorge(r.Reach, r.Along), wall) : 0; // (wall: canyons, revision 21)
	if (gorge > 0) flood += (Mx(bank, (rise - bankTop) * .35 + 4) - flood) * gorge;
	// A beach: a low, gently sloping strip before the bank rises; everything beyond the waterline moves outward.
	double lift = 0;
	if (beach > 0 && d > half)
	{
		lift = .3 * DetMath::Smooth((d - half) / beach);
		double take = Style.Revision >= 9 ? beach * (1 - DetMath::Smooth((d - half - beach - bank) / (bank * 1.5 + 2 * beach))) : beach;
		d = Mx(half, d - take);
		if (d == half) { weight = 1; inside = 0; return surface + lift; }
		bankTop = Mx(bankTop, surface + lift + .05);
	}
	weight = 1 - DetMath::Smooth((d - half - bank) / flood);
	inside = (half - d) / half;
	if (d < half) { double u = d / half; return surface - r.Depth * (1 - u * u); }
	// Bank: rises to just above the water, quickly where the ground beside is high, as a gentle levee ramp where low.
	if (d < half + bank)
	{
		double gentle = DetMath::Smooth((d - half) / bank), quick = DetMath::Smooth((d - half) / Mn(bank, 2.5));
		double high = DetMath::Clamp((Mn(terrain, bankTop) - surface) / (bankTop - surface), 0, 1) * (1 - estuary);
		double from = surface + lift;
		return from + (bankTop - from) * DetMath::Lerp(gentle, quick, high);
	}
	double ramp = DetMath::Lerp(Mn(flood, bank * 2 + 10), flood, DetMath::Smooth(terrain - bankTop + .5));
	if (gorge > 0 && Style.Revision >= 21) return GorgeWall(bankTop, terrain, d - half - bank, ramp, gorge, x, z);
	return bankTop + (terrain - bankTop) * DetMath::Smooth((d - half - bank) / ramp);
}
}
