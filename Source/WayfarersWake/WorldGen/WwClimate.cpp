// Port of Thornwake Game/WorldGen: Climate.cs, BiomeMap.cs, BiomeRegistry.cs, WorldPlan.cs (revision 16 paths).
#include "WwLandforms.h"
#include <chrono>

namespace ww
{
// ------------------------------------------------------------------------------------------------ registry
static FColor3 HexColor(const std::string& c)
{
	FColor3 o;
	if (c.size() != 6) return o;
	auto H = [&](int at) { return (float)std::stoi(c.substr(at, 2), nullptr, 16) / 255.f; };
	o.R = H(0); o.G = H(2); o.B = H(4);
	return o;
}

static bool ReadRange(const FJson& o, const char* Name, double* Out)
{
	const FJson* r = o.Find(Name);
	if (!r || r->Type != FJson::Array || r->Items.size() != 2) return false;
	Out[0] = r->Items[0].Num; Out[1] = r->Items[1].Num;
	return true;
}

static void ReadStrings(const FJson& o, const char* Name, std::vector<std::string>& Out)
{
	const FJson* a = o.Find(Name);
	if (a && a->Type == FJson::Array) for (const FJson& v : a->Items) Out.push_back(v.Str);
}

bool FBiomeRegistry::Parse(const std::string& BiomesJson, const std::string& SetsJson, FBiomeRegistry& Out, std::string& Error)
{
	FJson Biomes, Sets;
	if (!FJson::Parse(BiomesJson, Biomes, Error) || !FJson::Parse(SetsJson, Sets, Error)) return false;
	if (Biomes.Type != FJson::Array || Sets.Type != FJson::Array) { Error = "Damaged biome data."; return false; }
	auto FindSet = [&](const std::string& Id) -> const FJson* { const FJson* f = nullptr; for (const FJson& s : Sets.Items) if (s.StringOr("Id", "") == Id) f = &s; return f; };
	static const FJson Empty = [] { FJson e; e.Type = FJson::Object; return e; }();
	for (const FJson& b : Biomes.Items)
	{
		if (!b.BoolOr("InPool", true)) continue;
		FBiome o;
		o.Id = b.StringOr("Id", ""); o.Name = b.StringOr("Name", "");
		if (o.Id.empty()) { Error = "A biome needs an id and a name."; return false; }
		const FJson& pl = b.Find("Placement") ? *b.Find("Placement") : Empty;
		FBiomePlacement& p = o.Placement;
		p.Weight = pl.NumberOr("Weight", 1);
		p.bTemperature = ReadRange(pl, "Temperature", p.Temperature); p.bMoisture = ReadRange(pl, "Moisture", p.Moisture);
		p.bElevation = ReadRange(pl, "Elevation", p.Elevation); p.bSlope = ReadRange(pl, "Slope", p.Slope);
		p.bMountain = ReadRange(pl, "Mountain", p.Mountain); p.bLatitude = ReadRange(pl, "Latitude", p.Latitude);
		p.Coast = pl.NumberOr("Coast", 0); p.River = pl.NumberOr("River", 0);
		ReadRange(pl, "RegionSize", p.RegionSize);
		if (const FJson* patch = pl.Find("Patch")) { p.bPatch = true; ReadStrings(*patch, "Within", p.PatchWithin); p.PatchCoverage = patch->NumberOr("Coverage", .15); }
		ReadStrings(pl, "PreferNeighbours", p.PreferNeighbours); ReadStrings(pl, "ForbiddenNeighbours", p.ForbiddenNeighbours);

		const FJson& terrain = b.Find("Terrain") ? *b.Find("Terrain") : Empty;
		o.Flatten = terrain.NumberOr("Flatten", 0);
		if (const FJson* features = terrain.Find("Features"))
		{
			o.Dunes = Mx(0, features->NumberOr("dunes", 0)); o.Mesas = Mx(0, features->NumberOr("mesas", 0)); o.Spires = Mx(0, features->NumberOr("spires", 0));
		}
		const FJson& water = b.Find("Water") ? *b.Find("Water") : Empty;
		o.Pools = water.NumberOr("Pools", 0); o.PoolDepth = water.NumberOr("PoolDepth", 1); o.PoolSize = water.NumberOr("PoolSize", 16);
		o.PoolChannels = water.NumberOr("PoolChannels", 0); o.Wetness = water.NumberOr("Wetness", 0);

		const FJson& look = b.Find("Appearance") ? *b.Find("Appearance") : Empty;
		o.RockExposure = look.NumberOr("RockExposure", 0); o.Darkness = look.NumberOr("Darkness", 0);
		o.Map = HexColor(look.StringOr("MapColor", "6e9a4e"));
		std::string groundId = look.StringOr("Ground", "TemperateGround");
		const FJson* ground = FindSet(groundId);
		if (!ground) { Error = "Biome '" + o.Id + "': unknown ground set '" + groundId + "'."; return false; }
		o.Grass = HexColor(ground->StringOr("Grass", "566b3f")); o.Dry = HexColor(ground->StringOr("Dry", "7b8059"));
		o.Soil = HexColor(ground->StringOr("Soil", "5c4a35")); o.ForestFloor = HexColor(ground->StringOr("ForestFloor", "3c4b2b"));
		o.Rock = HexColor(ground->StringOr("Rock", "77766a")); o.Snow = ground->NumberOr("Snow", 0);

		const FJson& veg = b.Find("Vegetation") ? *b.Find("Vegetation") : Empty;
		o.TreeDensity = veg.NumberOr("TreeDensity", 1); o.GrassAmount = veg.NumberOr("Grass", 1);
		std::vector<std::pair<std::string, double>> treeSets;
		if (const FJson* trees = veg.Find("Trees")) for (const auto& m : trees->Members) treeSets.emplace_back(m.first, m.second.Num);
		else treeSets.emplace_back("BroadleafTrees", 1.0);
		double cumulative = 0;
		for (const auto& ts : treeSets)
		{
			const FJson* set = FindSet(ts.first);
			const FJson* items = set ? set->Find("Items") : nullptr;
			if (!items) { Error = "Biome '" + o.Id + "': unknown tree set '" + ts.first + "'."; return false; }
			double sum = 0;
			for (const auto& m : items->Members) sum += m.second.Num;
			for (const auto& m : items->Members) { cumulative += ts.second * m.second.Num / Mx(1e-9, sum); o.Trees.emplace_back(m.first, cumulative); }
		}
		// A later definition with the same id replaces the earlier one in place (custom biomes).
		bool replaced = false;
		for (FBiome& existing : Out.Pool) if (existing.Id == o.Id) { o.Index = existing.Index; existing = o; replaced = true; }
		if (!replaced) { o.Index = (int)Out.Pool.size(); Out.Pool.push_back(o); }
	}
	if (Out.Pool.empty()) { Error = "No biome is in the generation pool."; return false; }
	return true;
}

FBiomeRegistry FBiomeRegistry::ClassicOnly()
{
	FBiomeRegistry r;
	FBiome b;
	b.Id = "classic"; b.Name = "Classic temperate";
	b.Grass = HexColor("566b3f"); b.Dry = HexColor("7b8059"); b.Soil = HexColor("5c4a35"); b.ForestFloor = HexColor("3c4b2b"); b.Rock = HexColor("77766a");
	b.Map = HexColor("6e9a4e");
	b.Trees.emplace_back("Oak_A", 1.0);
	r.Pool.push_back(b);
	return r;
}

// ------------------------------------------------------------------------------------------------ climate
static void BlurClimate(std::vector<float>& a, int n, int r)
{
	std::vector<float> tmp(a.size());
	for (int pass = 0; pass < 2; pass++)
	{
		ParallelFor(n, [&](int j)
		{
			double sum = 0; int count = 0;
			for (int i = -r; i <= r; i++) if (i >= 0 && i < n) { sum += a[j * n + i]; count++; }
			for (int i = 0; i < n; i++)
			{
				tmp[j * n + i] = (float)(sum / count);
				int add = i + r + 1, rem = i - r;
				if (add < n) { sum += a[j * n + add]; count++; }
				if (rem >= 0) { sum -= a[j * n + rem]; count--; }
			}
		});
		ParallelFor(n, [&](int i)
		{
			double sum = 0; int count = 0;
			for (int j = -r; j <= r; j++) if (j >= 0 && j < n) { sum += tmp[j * n + i]; count++; }
			for (int j = 0; j < n; j++)
			{
				a[j * n + i] = (float)(sum / count);
				int add = j + r + 1, rem = j - r;
				if (add < n) { sum += tmp[add * n + i]; count++; }
				if (rem >= 0) { sum -= tmp[rem * n + i]; count--; }
			}
		});
	}
}

FClimate::FClimate(const FWorldPlan& plan, const FClimateSettings& InSettings) : Settings(InSettings)
{
	const FMacroRaster& macro = plan.Macro;
	const FWorldProfile& p = plan.Profile();
	World = p.WorldSize; OriginZ = plan.Manifest.OriginZ;
	double span = Settings.Span(World);
	North = Settings.CentreLatitude + span / 2; South = Settings.CentreLatitude - span / 2;
	int n = MxI(128, macro.Size / 2);
	double cell = World / n;
	Raster = FMacroRaster(n, macro.MinX, macro.MinZ, cell, 2);
	std::vector<float>& temperature = Raster.Channel(TemperatureChannel);
	std::vector<float>& precipitation = Raster.Channel(PrecipitationChannel);
	std::vector<double> height((size_t)n * n);
	std::vector<uint8_t> land((size_t)n * n);
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++)
		{
			double x = Raster.CellX(i), z = Raster.CellZ(j), l, h, m;
			macro.Sample(x, z, FWorldPlan::LandChannel, FWorldPlan::HeightChannel, FWorldPlan::MountainChannel, l, h, m);
			int k = j * n + i;
			land[k] = l > 0;
			height[k] = Mx(0, h);
			temperature[k] = (float)(SeaLevelTemperature(Latitude(z)) - .0065 * height[k] + Settings.TemperatureOffset);
		}

	// Moisture carried by the wind along each row (east-west winds; the sweep direction follows the latitude band).
	double perKm = cell / 1000;
	ParallelFor(n, [&](int j)
	{
		double lat = Latitude(Raster.CellZ(j));
		bool fromWest = std::fabs(lat) >= 30 && std::fabs(lat) < 60;
		double air = 1;
		for (int s = 0; s < n; s++)
		{
			int i = fromWest ? s : n - 1 - s, k = j * n + i;
			int prev = fromWest ? MxI(0, i - 1) : MnI(n - 1, i + 1);
			if (!land[k])
			{
				air += (1 - air) * Mn(1, .03 * perKm); // the sea refills the air over ~30 km
				precipitation[k] = (float)(900 * air);
				continue;
			}
			double rise = Mx(0, height[k] - height[j * n + prev]) / cell; // slope the wind climbs
			double capacity = DetMath::Clamp((temperature[k] + 18) / 38, .2, 1); // cold air holds less (float arithmetic, as the source)
			double rain = air * Mn(.5, (.0028 + rise * 1.4) * perKm);
			air -= rain;
			precipitation[k] = (float)((250 + 2400 * air * capacity + rain / perKm * 900) * Settings.Moisture);
		}
	});
	BlurClimate(precipitation, n, MxI(1, (int)std::nearbyint(15000 / cell)));
}

double FClimate::Latitude(double z) const
{
	double t = DetMath::Clamp((z - OriginZ) / World + .5, 0, 1); // 0 north edge … 1 south edge
	return North + (South - North) * t;
}

void FClimate::At(double x, double z, double elevation, double& Temperature, double& Precipitation) const
{
	Precipitation = Raster.Sample(PrecipitationChannel, x, z);
	Temperature = SeaLevelTemperature(Latitude(z)) + Settings.TemperatureOffset - .0065 * Mx(0, elevation);
}

double FClimate::Snow(double groundSnow, double temperature, double elevation) const
{
	return Settings.Biomes ? Mx(groundSnow, DetMath::Smooth((-4 - temperature) / 5)) : DetMath::Smooth((elevation - 2450) / 400);
}

// ------------------------------------------------------------------------------------------------ biome map
static double Member(double v, bool has, const double* range, double soft)
{
	return !has ? 1 : DetMath::Smooth((v - range[0]) / soft + .5) * DetMath::Smooth((range[1] - v) / soft + .5);
}

static double MemberLog(double v, bool has, const double* range, double soft)
{
	if (!has) return 1;
	double lr[2] = { std::log(Mx(1, range[0])), std::log(Mx(1, range[1])) };
	return Member(std::log(Mx(1, v)), true, lr, soft);
}

/// Chamfer distance (m) to the nearest cell where Target holds.
static std::vector<float> ChamferDistance(int n, const std::vector<uint8_t>& target, double cell)
{
	std::vector<float> d((size_t)n * n);
	const float far = 1e9f;
	for (int k = 0; k < n * n; k++) d[k] = target[k] ? 0 : far;
	float a = (float)cell, b = (float)(cell * std::sqrt(2.0));
	auto MinF = [](float u, float v) { return u < v ? u : v; };
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++)
		{
			int k = j * n + i; float v = d[k];
			if (i > 0) v = MinF(v, d[k - 1] + a);
			if (j > 0) { v = MinF(v, d[k - n] + a); if (i > 0) v = MinF(v, d[k - n - 1] + b); if (i < n - 1) v = MinF(v, d[k - n + 1] + b); }
			d[k] = v;
		}
	for (int j = n - 1; j >= 0; j--)
		for (int i = n - 1; i >= 0; i--)
		{
			int k = j * n + i; float v = d[k];
			if (i < n - 1) v = MinF(v, d[k + 1] + a);
			if (j < n - 1) { v = MinF(v, d[k + n] + a); if (i < n - 1) v = MinF(v, d[k + n + 1] + b); if (i > 0) v = MinF(v, d[k + n - 1] + b); }
			d[k] = v;
		}
	return d;
}

static void BlurSerial(std::vector<float>& a, int n, int r)
{
	std::vector<float> tmp(a.size());
	for (int j = 0; j < n; j++)
	{
		double sum = 0; int count = 0;
		for (int i = -r; i <= r; i++) if (i >= 0 && i < n) { sum += a[j * n + i]; count++; }
		for (int i = 0; i < n; i++)
		{
			tmp[j * n + i] = (float)(sum / count);
			int add = i + r + 1, rem = i - r;
			if (add < n) { sum += a[j * n + add]; count++; }
			if (rem >= 0) { sum -= a[j * n + rem]; count--; }
		}
	}
	for (int i = 0; i < n; i++)
	{
		double sum = 0; int count = 0;
		for (int j = -r; j <= r; j++) if (j >= 0 && j < n) { sum += tmp[j * n + i]; count++; }
		for (int j = 0; j < n; j++)
		{
			a[j * n + i] = (float)(sum / count);
			int add = j + r + 1, rem = j - r;
			if (add < n) { sum += tmp[add * n + i]; count++; }
			if (rem >= 0) { sum -= tmp[rem * n + i]; count--; }
		}
	}
}

static int NearestClimateBiome(int k, const std::vector<float>& t, const std::vector<float>& p, const std::vector<float>& elevation,
	const std::vector<float>& slope, const std::vector<float>& mountain, const std::vector<float>& latitude, const std::vector<FBiome>& pool,
	const std::vector<int>& climateBiomes)
{
	auto Gap = [](double v, bool has, const double* r, double soft) { return !has ? 0 : Mx(0, Mx(r[0] - v, v - r[1])) / soft; };
	int best = !climateBiomes.empty() ? climateBiomes[0] : 0; double bestD = std::numeric_limits<double>::max();
	for (int b : climateBiomes)
	{
		const FBiomePlacement& pl = pool[b].Placement;
		double lp = std::log(Mx(1, p[k]));
		double lr[2] = { 0, 0 };
		if (pl.bMoisture) { lr[0] = std::log(Mx(1, pl.Moisture[0])); lr[1] = std::log(Mx(1, pl.Moisture[1])); }
		double g = Sq(Gap(t[k], pl.bTemperature, pl.Temperature, 3)) + Sq(Gap(lp, pl.bMoisture, lr, .25)) + Sq(Gap(elevation[k], pl.bElevation, pl.Elevation, 150))
			+ Sq(Gap(slope[k], pl.bSlope, pl.Slope, .03)) + Sq(Gap(mountain[k], pl.bMountain, pl.Mountain, .1)) + Sq(Gap(latitude[k], pl.bLatitude, pl.Latitude, 4));
		g -= .01 * std::log(Mx(1e-3, pl.Weight)); // ties: the more common biome
		if (g < bestD) { bestD = g; best = b; }
	}
	return best;
}

FBiomeMap::FBiomeMap(const FWorldPlan& plan, const FClimate& climate, const FBiomeRegistry& InRegistry) : Registry(InRegistry)
{
	const std::vector<FBiome>& pool = Registry.Pool;
	int count = (int)pool.size();
	const FClimateSettings& settings = climate.Settings;
	std::vector<uint8_t> excluded(count, 0);
	for (int b = 0; b < count; b++)
		excluded[b] = std::find(settings.ExcludedBiomes.begin(), settings.ExcludedBiomes.end(), pool[b].Id) != settings.ExcludedBiomes.end();
	if (std::all_of(excluded.begin(), excluded.end(), [](uint8_t e) { return e != 0; })) std::fill(excluded.begin(), excluded.end(), (uint8_t)0); // never an empty world
	bool patchwork = settings.BiomePlacement == 1;
	Grid = &climate.Raster;
	int n = N = Grid->Size;
	double cell = Grid->Cell;
	Seed = Hashing::FeatureSeed(plan.Manifest.Seed, 6, plan.Manifest.ProfileDigest, "biomes");
	Warp = cell * 1.3;
	size_t cells = (size_t)n * n;
	Ids.assign(cells * 3, 0);
	W.assign(cells * 3, 0.f);
	if (count == 1) { for (size_t k = 0; k < cells; k++) W[k * 3] = 1; return; }

	// ---- per-cell geography and climate
	std::vector<uint8_t> land(cells);
	std::vector<float> elevation(cells), slope(cells), mountain(cells), temperature(cells), moisture(cells), latitude(cells);
	ParallelFor(n, [&](int j)
	{
		for (int i = 0; i < n; i++)
		{
			double x = Grid->CellX(i), z = Grid->CellZ(j), l, h, m, gx, gz, t, p;
			int k = j * n + i;
			plan.Macro.Sample(x, z, FWorldPlan::LandChannel, FWorldPlan::HeightChannel, FWorldPlan::MountainChannel, l, h, m);
			land[k] = l > 0;
			elevation[k] = (float)Mx(0, h);
			mountain[k] = (float)DetMath::Clamp(m, 0, 1);
			plan.Macro.SampleGradient(FWorldPlan::HeightChannel, x, z, gx, gz);
			slope[k] = (float)std::sqrt(gx * gx + gz * gz);
			climate.At(x, z, Mx(0, h), t, p);
			temperature[k] = (float)t; moisture[k] = (float)p;
			latitude[k] = (float)climate.Latitude(z);
		}
	});
	LandCells = land;
	std::vector<uint8_t> sea(cells);
	for (size_t k = 0; k < cells; k++) sea[k] = !land[k];
	std::vector<float> coast = ChamferDistance(n, sea, cell); // metres to the sea
	std::vector<uint8_t> wet(cells, 0);
	for (const FRiverReach& r : plan.Hydrology->Reaches)
		for (const FPoint2& pt : r.Points)
		{
			int i = (int)((pt.X - Grid->MinX) / cell), j = (int)((pt.Z - Grid->MinZ) / cell);
			if (i >= 0 && j >= 0 && i < n && j < n) wet[j * n + i] = 1;
		}
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++)
			if (plan.Hydrology->LakeAt(Grid->CellX(i), Grid->CellZ(j)) >= 0) wet[j * n + i] = 1;
	std::vector<float> river = ChamferDistance(n, wet, cell); // metres to a river or lake

	// ---- fitness per biome
	std::vector<std::vector<float>> fit(count);
	std::vector<int> climateBiomes;
	for (int b = 0; b < count; b++) if (!pool[b].Placement.bPatch && !excluded[b]) climateBiomes.push_back(b);
	if (climateBiomes.empty()) for (int b = 0; b < count; b++) if (!excluded[b]) climateBiomes.push_back(b);
	ParallelFor(count, [&](int b)
	{
		const FBiomePlacement& p = pool[b].Placement;
		std::vector<float>& f = fit[b];
		f.assign(cells, 0.f);
		double scale = std::sqrt(p.RegionSize[0] * p.RegionSize[1]) * 1000 * .5;
		u64 seed = Seed ^ Hashing::Mix((u64)b * 977 + 13);
		for (int j = 0; j < n; j++)
			for (int i = 0; i < n; i++)
			{
				int k = j * n + i;
				if (!land[k]) continue;
				double v = Mx(0, p.Weight)
					* Member(temperature[k], p.bTemperature, p.Temperature, 3) * MemberLog(moisture[k], p.bMoisture, p.Moisture, .25)
					* Member(elevation[k], p.bElevation, p.Elevation, 150) * Member(slope[k], p.bSlope, p.Slope, .03)
					* Member(mountain[k], p.bMountain, p.Mountain, .1) * Member(latitude[k], p.bLatitude, p.Latitude, 4);
				if (v <= 0) continue;
				double nearCoast = 1 - DetMath::Smooth(coast[k] / 30000), nearRiver = 1 - DetMath::Smooth(river[k] / 5000);
				v *= std::exp(p.Coast * 1.2 * nearCoast) * std::exp(p.River * 1.2 * nearRiver);
				// Region noise at the biome's own scale: irregular regions of roughly its size, not climate bands.
				v *= std::exp(.55 * Noise::Fbm(Grid->CellX(i), Grid->CellZ(j), scale, 3, cell * 2, seed, .5));
				f[k] = (float)v;
			}
	});

	// ---- patch biomes: take over their hosts inside noise patches covering the requested share
	if (patchwork) Patchwork(fit, land, elevation, mountain, excluded, settings.PatchSize > 0 ? settings.PatchSize * 1000 : 12000, n);
	for (int b = 0; b < count; b++) if (excluded[b]) std::fill(fit[b].begin(), fit[b].end(), 0.f);
	if (!patchwork)
		for (int b = 0; b < count; b++)
		{
			if (!pool[b].Placement.bPatch || excluded[b]) continue;
			const FBiomePlacement& p = pool[b].Placement;
			std::vector<int> hosts;
			if (p.PatchWithin.empty()) hosts = climateBiomes;
			else for (int h : climateBiomes) if (std::find(p.PatchWithin.begin(), p.PatchWithin.end(), pool[h].Id) != p.PatchWithin.end()) hosts.push_back(h);
			double scale = std::sqrt(p.RegionSize[0] * p.RegionSize[1]) * 1000 * .6;
			u64 seed = Seed ^ Hashing::Mix((u64)b * 31337 + 7);
			std::vector<float> mask(cells, 0.f), hostFit(cells, 0.f), values;
			for (int k = 0; k < n * n; k++)
			{
				if (!land[k]) continue;
				float best = 0, bestAll = 0;
				for (int h : hosts) if (fit[h][k] > best) best = fit[h][k];
				for (int h : climateBiomes) if (fit[h][k] > bestAll) bestAll = fit[h][k];
				// Host present here (its fitness near the best) and this biome's own preferences allow it.
				float hostShare = bestAll > 0 ? best / bestAll : 0;
				if (hostShare < .6f || fit[b][k] <= 0) continue;
				int i = k % n, j = k / n;
				mask[k] = (float)Noise::Fbm(Grid->CellX(i), Grid->CellZ(j), scale, 3, cell * 2, seed, .55);
				hostFit[k] = bestAll;
				values.push_back(mask[k]);
			}
			if (values.empty()) { std::fill(fit[b].begin(), fit[b].end(), 0.f); continue; }
			std::sort(values.begin(), values.end());
			float threshold = values[ClampI((int)((1 - p.PatchCoverage) * values.size()), 0, (int)values.size() - 1)];
			std::vector<float>& f = fit[b];
			for (int k = 0; k < n * n; k++)
				f[k] = hostFit[k] > 0 ? (float)(hostFit[k] * 2.2 * DetMath::Smooth((mask[k] - threshold) / .06 + .5)) : 0;
		}

	// ---- revision 28: the volcanic biome on the volcanoes. Revision 34: the land's own biome climbs a volcano's lower
	// slopes; the volcanic biome keeps an active cone to three quarters of the way down, a quiet cone's upper 40 %,
	// a caldera's inner half.
	if (plan.Volcanoes && plan.Volcanoes->Count() > 0)
	{
		int volcanic = -1;
		for (const FBiome& r : pool) if (r.Id == "volcanic" && !excluded[r.Index]) { volcanic = r.Index; break; }
		bool climb = plan.Style.Revision >= 34;
		if (volcanic >= 0)
			plan.Volcanoes->Cones([&](double vx, double vz, double vr, bool caldera, bool active)
			{
				for (int k = 0; k < n * n; k++)
				{
					if (!land[k]) continue;
					double d = std::sqrt(Sq(Grid->CellX(k % n) - vx) + Sq(Grid->CellZ(k / n) - vz));
					double nearCone = climb ? 1 - DetMath::Smooth((d - vr * (active ? .75 : caldera ? .5 : .4)) / (vr * .15))
						: 1 - DetMath::Smooth((d - vr * .9) / (vr * .3));
					if (nearCone <= 0) continue;
					float best = 0;
					for (int b = 0; b < count; b++) if (fit[b][k] > best) best = fit[b][k];
					float value = (float)((best > .1f ? best : .1f) * 2.5 * nearCone);
					if (value > fit[volcanic][k]) fit[volcanic][k] = value;
				}
			});
	}

	// ---- neighbour rules (one soft pass): suppressed near forbidden neighbours, boosted near preferred ones
	std::vector<int> top(cells, -1);
	for (int k = 0; k < n * n; k++)
	{
		if (!land[k]) continue;
		float best = 0;
		for (int b = 0; b < count; b++) if (fit[b][k] > best) { best = fit[b][k]; top[k] = b; }
	}
	int radius = MxI(1, (int)std::nearbyint(8000 / cell));
	bool any = false;
	if (!patchwork) for (const FBiome& r : pool) if (r.Placement.ForbiddenNeighbours.size() + r.Placement.PreferNeighbours.size() > 0) any = true;
	if (any)
	{
		std::vector<std::vector<float>> presence(count);
		for (int b = 0; b < count; b++)
		{
			std::vector<float>& m = presence[b];
			m.assign(cells, 0.f);
			for (int k = 0; k < n * n; k++) if (top[k] == b) m[k] = 1;
			BlurSerial(m, n, radius);
		}
		auto IndexOf = [&](const std::string& id) { for (const FBiome& r : pool) if (r.Id == id) return r.Index; return -1; };
		for (int b = 0; b < count; b++)
		{
			const FBiomePlacement& p = pool[b].Placement;
			std::vector<int> forbid, prefer;
			for (const std::string& id : p.ForbiddenNeighbours) { int i = IndexOf(id); if (i >= 0) forbid.push_back(i); }
			for (const std::string& id : p.PreferNeighbours) { int i = IndexOf(id); if (i >= 0) prefer.push_back(i); }
			if (forbid.size() + prefer.size() == 0) continue;
			for (int k = 0; k < n * n; k++)
			{
				double factor = 1;
				for (int o : forbid) { float pr = presence[o][k] * 3; factor *= 1 - (pr < 1 ? pr : 1.f); }
				for (int o : prefer) factor *= 1 + .35 * presence[o][k];
				fit[b][k] = (float)(fit[b][k] * factor);
			}
		}
	}

	// ---- keep the three best per cell, sharpened so regions are decisive with soft edges
	for (int k = 0; k < n * n; k++)
	{
		int a = 0, b2 = 0, c = 0; float fa = -1, fb = -1, fc = -1;
		for (int b = 0; b < count; b++)
		{
			float v = fit[b][k];
			if (v > fa) { c = b2; fc = fb; b2 = a; fb = fa; a = b; fa = v; }
			else if (v > fb) { c = b2; fc = fb; b2 = b; fb = v; }
			else if (v > fc) { c = b; fc = v; }
		}
		// Nothing fits (or sea): the nearest climate biome, weak at sea so coastal land decides the blend there.
		if (fa <= 0) { a = b2 = c = NearestClimateBiome(k, temperature, moisture, elevation, slope, mountain, latitude, pool, climateBiomes); fa = land[k] ? 1 : .03f; fb = fc = 0; }
		float fb0 = fb > 0 ? fb : 0, fc0 = fc > 0 ? fc : 0;
		double wa = fa * fa, wb = fb0 * fb0, wc = fc0 * fc0, sum = wa + wb + wc;
		Ids[k * 3] = (uint8_t)a; Ids[k * 3 + 1] = (uint8_t)b2; Ids[k * 3 + 2] = (uint8_t)c;
		W[k * 3] = (float)(wa / sum); W[k * 3 + 1] = (float)(wb / sum); W[k * 3 + 2] = (float)(wc / sum);
	}
	PoolCells();
}

void FBiomeMap::Patchwork(std::vector<std::vector<float>>& fit, const std::vector<uint8_t>& land, const std::vector<float>& elevation,
	const std::vector<float>& mountain, const std::vector<uint8_t>& excluded, double size, int n)
{
	const std::vector<FBiome>& pool = Registry.Pool;
	int count = (int)pool.size();
	auto Upland = [](const FBiomePlacement& p) { return (p.bElevation && p.Elevation[0] >= 800) || (p.bMountain && p.Mountain[0] >= .25); };
	std::vector<int> high, low;
	for (int b = 0; b < count; b++) if (!excluded[b]) (Upland(pool[b].Placement) ? high : low).push_back(b);
	if (low.empty()) low = high;
	if (high.empty()) high = low;
	for (auto& f : fit) std::fill(f.begin(), f.end(), 0.f);
	// Rare (patch) biomes stay rare: weight x 3 x their coverage share.
	auto Weight = [&](int b) { return Mx(.05, pool[b].Placement.Weight) * (pool[b].Placement.bPatch ? Mn(1, pool[b].Placement.PatchCoverage * 3) : 1); };
	auto Pick = [&](const std::vector<int>& from, u64 h)
	{
		double total = 0;
		for (int b : from) total += Weight(b);
		double u = Hashing::Unit(h) * total;
		for (int b : from) { u -= Weight(b); if (u <= 0) return b; }
		return from.back();
	};
	u64 seed = Seed ^ 0x9A7C4E11ULL;
	// (rows write different cells of the same biome arrays: safe in parallel)
	ParallelFor(n, [&](int j)
	{
		for (int i = 0; i < n; i++)
		{
			int k = j * n + i;
			if (!land[k]) continue;
			double x = Grid->CellX(i), z = Grid->CellZ(j);
			double wx = x + Noise::Fbm(x, z, size * .8, 3, size * .1, seed + 1, .5) * size * .45;
			double wz = z + Noise::Fbm(x + 777, z - 333, size * .8, 3, size * .1, seed + 2, .5) * size * .45;
			i64 cx = (i64)std::floor(wx / size), cz = (i64)std::floor(wz / size);
			double best = std::numeric_limits<double>::max(), second = std::numeric_limits<double>::max(); int bestB = 0, secondB = 0;
			for (i64 dz = -1; dz <= 1; dz++)
				for (i64 dx = -1; dx <= 1; dx++)
				{
					u64 h = Hashing::Lattice(cx + dx, cz + dz, seed);
					double px = (cx + dx + .15 + .7 * Hashing::Unit(h)) * size, pz = (cz + dz + .15 + .7 * Hashing::Unit(Hashing::Mix(h))) * size;
					double d = (px - wx) * (px - wx) + (pz - wz) * (pz - wz);
					bool up = mountain[k] > .3f || elevation[k] > 1300;
					int b = Pick(up ? high : low, Hashing::Mix(h + 99));
					if (d < best) { second = best; secondB = bestB; best = d; bestB = b; }
					else if (d < second) { second = d; secondB = b; }
				}
			double edge = (std::sqrt(second) - std::sqrt(best)) / (size * .15);
			float wBest = (float)(.5 + .5 * Mn(1, edge));
			fit[bestB][k] += wBest;
			fit[secondB][k] += 1 - wBest;
		}
	});
}

void FBiomeMap::PoolCells()
{
	const std::vector<FBiome>& pool = Registry.Pool;
	bool anyShaper = false;
	for (const FBiome& b : pool) if (b.ShapesTerrain()) anyShaper = true;
	if (!anyShaper) return;
	int n = N;
	std::vector<uint8_t> has((size_t)n * n, 0);
	for (int k = 0; k < n * n; k++)
		for (int e = 0; e < 3; e++)
			if (W[k * 3 + e] > 0 && pool[Ids[k * 3 + e]].ShapesTerrain()) has[k] = 1;
	PoolNear.assign((size_t)n * n, 0);
	const int r = 3;
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++)
		{
			if (!has[j * n + i]) continue;
			for (int dj = -r; dj <= r; dj++)
				for (int di = -r; di <= r; di++)
				{
					int a = i + di, b = j + dj;
					if (a >= 0 && b >= 0 && a < n && b < n) PoolNear[b * n + a] = 1;
				}
		}
}

int FBiomeMap::SampleAll(double x, double z, int* ids, double* weights) const
{
	if (Registry.Pool.size() == 1) { ids[0] = 0; weights[0] = 1; return 1; }
	double wx = x + Noise::Fbm(x, z, Warp * 6, 3, Warp * .5, Seed + 101, .5) * Warp;
	double wz = z + Noise::Fbm(x + 913, z - 377, Warp * 6, 3, Warp * .5, Seed + 102, .5) * Warp;
	double fx = (wx - Grid->MinX) / Grid->Cell - .5, fz = (wz - Grid->MinZ) / Grid->Cell - .5;
	int i0 = (int)std::floor(fx), j0 = (int)std::floor(fz);
	double tx = fx - i0, tz = fz - j0, total = 0;
	int used = 0;
	for (int c = 0; c < 4; c++)
	{
		int i = ClampI(i0 + (c & 1), 0, N - 1), j = ClampI(j0 + (c >> 1), 0, N - 1);
		double corner = ((c & 1) == 1 ? tx : 1 - tx) * ((c >> 1) == 1 ? tz : 1 - tz);
		int k = (j * N + i) * 3;
		for (int e = 0; e < 3; e++)
		{
			double w = W[k + e] * corner;
			if (w <= 0) continue;
			int id = Ids[k + e], slot = -1;
			for (int q = 0; q < used; q++) if (ids[q] == id) { slot = q; break; }
			if (slot < 0) { slot = used++; ids[slot] = id; weights[slot] = 0; }
			weights[slot] += w; total += w;
		}
	}
	if (used == 0 || total <= 0) { ids[0] = 0; weights[0] = 1; return 1; }
	for (int q = 0; q < used; q++) weights[q] /= total;
	return used;
}

void FBiomeMap::Sample(double x, double z, int& a, int& b, double& shareA) const
{
	a = 0; b = 0; shareA = 1;
	if (Registry.Pool.size() == 1) return;
	double wx = x + Noise::Fbm(x, z, Warp * 6, 3, Warp * .5, Seed + 101, .5) * Warp;
	double wz = z + Noise::Fbm(x + 913, z - 377, Warp * 6, 3, Warp * .5, Seed + 102, .5) * Warp;
	double fx = (wx - Grid->MinX) / Grid->Cell - .5, fz = (wz - Grid->MinZ) / Grid->Cell - .5;
	int i0 = (int)std::floor(fx), j0 = (int)std::floor(fz);
	double tx = fx - i0, tz = fz - j0;
	int ids[12]; double ws[12];
	int used = 0;
	for (int c = 0; c < 4; c++)
	{
		int i = ClampI(i0 + (c & 1), 0, N - 1), j = ClampI(j0 + (c >> 1), 0, N - 1);
		double corner = ((c & 1) == 1 ? tx : 1 - tx) * ((c >> 1) == 1 ? tz : 1 - tz);
		int k = (j * N + i) * 3;
		for (int e = 0; e < 3; e++)
		{
			double w = W[k + e] * corner;
			if (w <= 0) continue;
			int id = Ids[k + e], slot = -1;
			for (int s = 0; s < used; s++) if (ids[s] == id) { slot = s; break; }
			if (slot < 0) { slot = used++; ids[slot] = id; ws[slot] = 0; }
			ws[slot] += w;
		}
	}
	double fa = -1, fb = 0;
	for (int s = 0; s < used; s++)
	{
		if (ws[s] > fa) { b = a; fb = Mx(fa, 0); a = ids[s]; fa = ws[s]; }
		else if (ws[s] > fb) { b = ids[s]; fb = ws[s]; }
	}
	if (fa <= 0) { a = 0; b = 0; shareA = 1; return; }
	if (fb <= 0) b = a;
	shareA = fa / (fa + fb);
}

bool FBiomeMap::TerrainAt(double x, double z, FBiomeTerrainMods& mods, bool allBiomes) const
{
	mods = FBiomeTerrainMods();
	if (PoolNear.empty()) return false;
	int i = ClampI((int)std::floor((x - Grid->MinX) / Grid->Cell), 0, N - 1), j = ClampI((int)std::floor((z - Grid->MinZ) / Grid->Cell), 0, N - 1);
	if (!PoolNear[j * N + i]) return false;
	const std::vector<FBiome>& pool = Registry.Pool;
	if (allBiomes)
	{
		int ids[12]; double ws[12];
		int n = SampleAll(x, z, ids, ws);
		double pools = 0, depth = 0, size = 0, channels = 0;
		for (int q = 0; q < n; q++)
		{
			const FBiome& d = pool[ids[q]]; double w = ws[q];
			mods.Flatten += d.Flatten * w;
			mods.Dunes += d.Dunes * w;
			mods.Mesas += d.Mesas * w;
			mods.Spires += d.Spires * w;
			if (d.Spires > 0) mods.SpirePresence += w;
			double p = d.Pools * w;
			pools += p; depth += d.PoolDepth * p; size += d.PoolSize * p; channels += d.PoolChannels * p;
		}
		mods.Flatten = DetMath::Clamp(mods.Flatten, 0, 1);
		mods.PoolSize = 16;
		if (pools >= .005)
		{
			mods.Pools = pools; mods.PoolDepth = depth / pools; mods.PoolSize = Mx(4, size / pools);
			mods.PoolChannels = DetMath::Clamp(channels / pools, 0, 1);
		}
		return true;
	}
	int a, b; double share;
	Sample(x, z, a, b, share);
	const FBiome& A = pool[a]; const FBiome& B = pool[b];
	double sa = share, sb = 1 - share;
	mods.Flatten = DetMath::Clamp(A.Flatten * sa + B.Flatten * sb, 0, 1);
	mods.Dunes = A.Dunes * sa + B.Dunes * sb; mods.Mesas = A.Mesas * sa + B.Mesas * sb; mods.Spires = A.Spires * sa + B.Spires * sb;
	mods.PoolSize = 16;
	double pa = A.Pools * sa, pb = B.Pools * sb, coverage = pa + pb;
	if (coverage >= .005)
	{
		double t = pa / coverage;
		mods.Pools = coverage;
		mods.PoolDepth = A.PoolDepth * t + B.PoolDepth * (1 - t);
		mods.PoolSize = Mx(4, A.PoolSize * t + B.PoolSize * (1 - t));
		mods.PoolChannels = DetMath::Clamp(A.PoolChannels * t + B.PoolChannels * (1 - t), 0, 1);
	}
	return true;
}

std::vector<double> FBiomeMap::Coverage() const
{
	std::vector<double> c(Registry.Pool.size(), 0.0); double total = 0;
	for (int k = 0; k < N * N; k++) if (LandCells.empty() || LandCells[k]) { c[Ids[k * 3]]++; total++; }
	for (double& v : c) v /= Mx(1, total);
	return c;
}

// ------------------------------------------------------------------------------------------------ plan
int FWorldPlan::RasterSizeFor(const FWorldProfile& Profile)
{
	int size = (int)std::ceil(Profile.WorldSize / 750 / 256) * 256;
	return ClampI(size, 512, 2048);
}

FWorldPlan::FWorldPlan(const FWorldManifest& InManifest, const FTerrainStyle& InStyle, const FBiomeRegistry& InBiomes,
	const std::function<void(int)>& Stage, int RasterSizeOverride)
	: Manifest(InManifest), Style(InStyle), Layout(InManifest)
{
	auto Started = std::chrono::steady_clock::now();
	if (Style.Revision > FTerrainStyle::PortedRevision) Style.Revision = FTerrainStyle::PortedRevision;
	if (Stage) Stage(0);
	const FWorldProfile& p = Manifest.Profile;
	int size = RasterSizeOverride > 0 ? RasterSizeOverride : RasterSizeFor(p);
	double world = p.WorldSize, cell = world / size;
	Macro = FMacroRaster(size, Manifest.OriginX - world * .5, Manifest.OriginZ - world * .5, cell, 3);
	double ox = Manifest.OriginX, oz = Manifest.OriginZ;
	std::vector<float>& landRaster = Macro.Channel(LandChannel);
	std::vector<float>& heightRaster = Macro.Channel(HeightChannel);
	std::vector<float>& mountainRaster = Macro.Channel(MountainChannel);
	ParallelFor(size, [&](int j)
	{
		for (int i = 0; i < size; i++)
		{
			double x = Macro.CellX(i), z = Macro.CellZ(j), lx = x - ox, lz = z - oz;
			int k = j * size + i;
			double land = Layout.LandInfluence(x, z);
			landRaster[k] = (float)land;
			if (land < -.3) continue; // deep sea: relief is never used
			double hs = p.HillScale;
			double hills = (Noise::Value(lx / hs, lz / hs, Layout.ReliefNoise) * .7 +
				Noise::Value(lx / (hs * .35), lz / (hs * .35), Layout.ReliefNoise + 1) * .3) * p.HillHeight;
			double mountain = 0, core, shoulder;
			Layout.MountainEnvelope(lx, lz, core, shoulder);
			if (core > 1e-6 || shoulder > 1e-6)
			{
				double wx, wz;
				Layout.MountainWarp(lx, lz, wx, wz);
				double rugged = Layout.RuggedBroad(wx, wz) * .92;
				mountain = Mn(1, core * (.28 + .72 * rugged) + shoulder * (.08 + .24 * rugged));
			}
			heightRaster[k] = (float)(hills + mountain * p.MountainHeight); // relief; converted below
			mountainRaster[k] = (float)mountain;
		}
	});

	// Big landforms (revision 17 on), before erosion, so rivers flow round, cut through or fill them.
	if (Style.Revision >= 18 && !Style.Landform("escarpments").Off()) PlanLandforms.push_back(std::make_unique<FEscarpments>(Style.Revision));
	if (Style.Revision >= 28 && !Style.Landform("volcanoes").Off())
	{
		auto Made = std::make_unique<FVolcanoes>(Style.Revision);
		Volcanoes = Made.get();
		PlanLandforms.push_back(std::move(Made));
	}
	if (!PlanLandforms.empty() && Stage) Stage(1);
	for (auto& Form : PlanLandforms) Form->Shape(Macro, Manifest, Style.Landform(Form->Id()));

	// Macro height above sea level: the lowland ramp from the coast plus faded relief; sea floor below 0.
	std::vector<double> height((size_t)size * size);
	for (size_t k = 0; k < height.size(); k++)
	{
		double land = landRaster[k];
		if (land <= 0) { height[k] = -p.OceanDepth * DetMath::Smooth(-land / .6); continue; }
		double h = p.LowlandHeight * DetMath::Smooth(land / .4) + DetMath::Smooth(land / .18) * heightRaster[k];
		h = Mx(p.LowlandHeight * DetMath::Smooth(land / .08) * .05, h);
		height[k] = Mx(h, .5); // every land cell stays above the sea for routing
	}
	if (Stage) Stage(2);
	u64 riverSeed = Hashing::FeatureSeed(Manifest.Seed, 100 + FWorldManifest::CurrentGenerator, Manifest.ProfileDigest, "rivers");
	// Revision 28: a caldera's rim resists erosion, and its floor is always a lake.
	std::vector<float> resist; std::vector<int> calderaCells;
	bool bResist = Volcanoes && Volcanoes->Resistance(Macro, resist);
	if (bResist) Volcanoes->LakeCells(Macro, calderaCells);
	Hydrology = std::make_unique<FHydrology>(Macro, height, p, Style, riverSeed, bResist ? &resist : nullptr, bResist ? &calderaCells : nullptr);
	for (size_t k = 0; k < height.size(); k++) heightRaster[k] = (float)height[k];
	Hydrology->RefineSurfaces(Style);
	if (Volcanoes) Volcanoes->SetLakes(*Hydrology, p.SeaLevel); // (revision 29: islands over their caldera lake's real level)
	// Canyons (revision 21): placed on the final rivers, before climate. Cirques (revision 23) read the rivers.
	if (Style.Revision >= 21 && !Style.Landform("canyons").Off()) Canyons = std::make_unique<FCanyons>(*this, Style.Landform("canyons"));
	if (Style.Revision >= 23 && !Style.Landform("cirques").Off()) Cirques = std::make_unique<FCirques>(*this, Style.Landform("cirques"));
	// Troughs (revision 24) lower the rivers they take in: after the canyons and cirques, which read the rivers.
	// Fjords (revision 26) are troughs reaching the sea, placed by the same module after them.
	{
		bool fjords = Style.Revision >= 26 && !Style.Landform("fjords").Off();
		if (Style.Revision >= 24 && (!Style.Landform("troughs").Off() || fjords))
			Troughs = std::make_unique<FTroughs>(*this, Style.Landform("troughs"), fjords, Style.Landform("fjords"));
	}
	if (Stage) Stage(3);
	Climate = std::make_unique<FClimate>(*this, Style.Climate);
	if (Stage) Stage(4);
	Biomes = std::make_unique<FBiomeMap>(*this, *Climate, Style.Climate.Biomes ? InBiomes : FBiomeRegistry::ClassicOnly());
	if (Volcanoes) Volcanoes->Finalize(); // (lava flows traced on the finished plan)
	BuildMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count();
}

FWorldPlan::~FWorldPlan() = default;

i64 FWorldPlan::MemoryBytes() const
{
	i64 bytes = Macro.Bytes() + (Climate ? Climate->Raster.Bytes() : 0);
	if (Hydrology)
	{
		bytes += (i64)Hydrology->LakeCell.size() * 4;
		bytes += Hydrology->PointCount() * (16 + 8 + 4 * 4);
	}
	if (Biomes) bytes += (i64)Climate->Raster.Size * Climate->Raster.Size * (3 + 12 + 2);
	return bytes;
}
}
