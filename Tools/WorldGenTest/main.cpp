// Headless parity and determinism test for the world generator (no engine needed; build with build.cmd).
// Compares the C++ port against Thornwake's frozen terrain fingerprints (Tests/WorldGen/Fixtures, copied here):
//   terrain-build114.json = terrain revision 16 with the current biome catalogue,
//   terrain-build52.json  = terrain revision 15 with the build-51 biome catalogue.
// A fingerprint is SHA-256 over four 33x33 height grids and the three macro rasters, so a match means the whole
// pipeline (layout, erosion, rivers, lakes, climate, biomes, sampler) is bit-identical to the C# generator.
//
//   WorldGenTest <repo root> [--quick] [--sizes] [--point=profile,seed,x,z] [--bench]
#include "../../Source/WayfarersWake/WorldGen/WwWorld.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>

using namespace ww;

int RunDump(const std::vector<FWorldProfile>& Profiles, const FBiomeRegistry& Biomes, const std::string& Arg);
std::string DumpType(const std::vector<FWorldProfile>& Profiles, const FBiomeRegistry& Biomes, const std::string& Arg);

static std::string ReadFile(const std::string& Path)
{
	std::ifstream In(Path, std::ios::binary);
	if (!In) { std::printf("cannot read %s\n", Path.c_str()); std::exit(2); }
	std::stringstream S; S << In.rdbuf();
	return S.str();
}

static void ThreadedFor(int Count, const std::function<void(int)>& Body)
{
	int Workers = (int)std::thread::hardware_concurrency();
	if (Workers < 2 || Count < 2) { for (int i = 0; i < Count; i++) Body(i); return; }
	std::vector<std::thread> Threads;
	for (int w = 0; w < Workers; w++)
		Threads.emplace_back([&, w] { for (int i = w; i < Count; i += Workers) Body(i); });
	for (auto& t : Threads) t.join();
}

static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static std::string Fingerprint(const FTerrainSampler& s)
{
	const FWorldProfile& profile = s.Plan->Profile();
	FSha256 sha;
	std::vector<double> buffer(33 * 33);
	const FLandFeature& f = s.Plan->Layout.Land[0];
	const double grids[4][3] = { { profile.WorldSize * -.5, profile.WorldSize * -.5, profile.WorldSize / 32 },
		{ f.Center.X, f.Center.Z, 1.0 }, { f.Center.X + f.RadiusX * .5, f.Center.Z, 97.0 }, { f.Center.X - f.RadiusX * 1.1, f.Center.Z, 13.0 } };
	for (const auto& g : grids)
	{
		s.HeightGrid(g[0], g[1], g[2], 33, 33, buffer.data());
		sha.Append(buffer.data(), buffer.size() * sizeof(double));
	}
	for (int c = 0; c < 3; c++) sha.Append(s.Plan->Macro.Channel(c).data(), s.Plan->Macro.Channel(c).size() * sizeof(float));
	return sha.FinishHex();
}

int OceanTest();

int main(int argc, char** argv)
{
	if (argc < 2) { std::printf("usage: WorldGenTest <repo root> [--quick] [--sizes] [--bench]\n"); return 2; }
	std::string root = argv[1];
	bool quick = false, sizes = false, bench = false; std::string point, dump, dumpType; int only = 0; bool typesOnly = false;
	for (int i = 2; i < argc; i++)
	{
		std::string a = argv[i];
		if (a == "--quick") quick = true; else if (a == "--sizes") sizes = true; else if (a == "--bench") bench = true;
		else if (a.rfind("--point=", 0) == 0) point = a.substr(8);
		else if (a.rfind("--rev=", 0) == 0) only = std::stoi(a.substr(6));
		else if (a.rfind("--dump=", 0) == 0) dump = a.substr(7);
		else if (a == "--types") typesOnly = true;
		else if (a == "--ocean") return OceanTest();
		else if (a.rfind("--dump-type=", 0) == 0) dumpType = a.substr(12);
	}
	ParallelForHook() = &ThreadedFor;

	std::string error;
	std::vector<FWorldProfile> profiles;
	if (!FWorldProfile::ParseCatalog(ReadFile(root + "/Content/WorldData/world_profiles.json"), profiles, error)) { std::printf("profiles: %s\n", error.c_str()); return 2; }
	FBiomeRegistry current, build51;
	if (!FBiomeRegistry::Parse(ReadFile(root + "/Content/WorldData/biomes.json"), ReadFile(root + "/Content/WorldData/asset_sets.json"), current, error)) { std::printf("biomes: %s\n", error.c_str()); return 2; }
	if (!FBiomeRegistry::Parse(ReadFile(root + "/Tools/WorldGenTest/Fixtures/biomes-build51.json"), ReadFile(root + "/Tools/WorldGenTest/Fixtures/asset_sets-build51.json"), build51, error)) { std::printf("biomes51: %s\n", error.c_str()); return 2; }
	std::printf("%d profiles, %d biomes in the pool\n", (int)profiles.size(), (int)current.Pool.size());

	if (!dump.empty()) return RunDump(profiles, current, dump);
	if (!dumpType.empty()) { std::printf("%s", DumpType(profiles, current, dumpType).c_str()); return 0; }
	int failures = 0, checks = 0;
	auto Check = [&](bool ok, const std::string& what) { checks++; if (!ok) { failures++; std::printf("  FAIL %s\n", what.c_str()); } };

	if (!point.empty())
	{
		std::stringstream ss(point); std::string id, t; std::getline(ss, id, ',');
		std::getline(ss, t, ','); int seed = std::stoi(t); std::getline(ss, t, ','); double x = std::stod(t); std::getline(ss, t, ','); double z = std::stod(t);
		for (const auto& p : profiles) if (p.Id == id)
		{
			auto plan = std::make_shared<FWorldPlan>(FWorldManifest::Create(seed, p), FTerrainStyle::For(p), current);
			FTerrainSampler s(plan);
			FTerrainSample v = s.Sample(x, z);
			std::printf("digest %s\nheight %.17g land %.17g mountain %.17g water %d level %.17g\n", plan->Manifest.ProfileDigest.c_str(), v.Height, v.Land, v.Mountain, (int)v.Water, v.WaterLevel);
			std::printf("lakes %d reaches %d points %lld\n", (int)plan->Hydrology->Lakes.size(), (int)plan->Hydrology->Reaches.size(), (long long)plan->Hydrology->PointCount());
		}
		return 0;
	}

	// ---- frozen fingerprints
	struct FFixture { const char* File; int Revision; const FBiomeRegistry* Biomes; };
	const FFixture fixtures[] = { { "terrain-build114.json", 16, &current }, { "terrain-build52.json", 15, &build51 },
		{ "terrain-build115.json", 17, &current }, { "terrain-build116.json", 18, &current }, { "terrain-build117.json", 19, &current },
		{ "terrain-r20.json", 20, &current }, { "terrain-build118.json", 21, &current }, { "terrain-r22.json", 22, &current }, { "terrain-build119.json", 23, &current },
		{ "terrain-build120.json", 24, &current }, { "terrain-r25.json", 25, &current }, { "terrain-r26.json", 26, &current }, { "terrain-build121.json", 27, &current },
		{ "terrain-r28.json", 28, &current }, { "terrain-build122.json", 29, &current }, { "terrain-build123.json", 30, &current }, { "terrain-build125.json", 31, &current },
		{ "terrain-build127.json", 32, &current }, { "terrain-build129.json", 33, &current }, { "terrain-build130.json", 34, &current }, { "terrain-build132.json", 35, &current },
		{ "terrain-r36.json", 36, &current }, { "terrain-build133.json", 37, &current }, { "terrain-build137.json", 38, &current }, { "terrain-r39.json", 39, &current },
		{ "terrain-r40.json", 40, &current }, { "terrain-build139.json", 41, &current }, { "terrain-r42.json", 42, &current }, { "terrain-build140.json", 43, &current },
		{ "terrain-r1.json", 44, &current } };
	for (const FFixture& fx : fixtures)
	{
		FJson expected;
		if (!FJson::Parse(ReadFile(root + "/Tools/WorldGenTest/Fixtures/" + fx.File), expected, error)) { std::printf("fixture: %s\n", error.c_str()); return 2; }
		if (only > 0 && fx.Revision != only) continue;
		if (typesOnly) continue;
		std::printf("Fingerprints %s (terrain revision %d)\n", fx.File, fx.Revision);
		for (const FWorldProfile& profile : profiles)
		{
			if (quick && profile.Id != "balanced") continue;
			for (int seed : { -9, 42, 7319 })
			{
				if (quick && seed != 42) continue;
				double t0 = Now();
				FTerrainStyle style = FTerrainStyle::For(profile);
				style.Revision = fx.Revision;
				auto plan = std::make_shared<FWorldPlan>(FWorldManifest::Create(seed, profile), style, *fx.Biomes);
				FTerrainSampler sampler(plan);
				std::string hash = Fingerprint(sampler), name = profile.Id + "/" + std::to_string(seed), want;
				for (const FJson& e : expected.Items) if (e.StringOr("Case", "") == name) want = e.StringOr("Hash", "");
				bool ok = hash == want;
				std::printf("  %-22s %s  %s  (%.1f s, %d lakes, %d reaches)\n", name.c_str(), hash.substr(0, 16).c_str(), ok ? "match" : "MISMATCH",
					Now() - t0, (int)plan->Hydrology->Lakes.size(), (int)plan->Hydrology->Reaches.size());
				Check(ok, "fingerprint " + name + " " + fx.File);
			}
		}
		if (quick && only == 0) break;
	}

	// ---- world types beyond the five fixture profiles (template and natural types, and resized worlds): compared with
	// output recorded from Thornwake's generator (Fixtures/world-types.txt; "# args" then the block --dump-type prints).
	if (only == 0 && !quick)
	{
		std::printf("World types (Fixtures/world-types.txt)\n");
		std::stringstream recorded(ReadFile(root + "/Tools/WorldGenTest/Fixtures/world-types.txt"));
		std::string line, args, want;
		auto Compare = [&]()
		{
			if (args.empty()) return;
			std::string got = DumpType(profiles, current, args);
			bool ok = got == want;
			std::printf("  %-34s %s\n", args.c_str(), ok ? "match" : "MISMATCH");
			Check(ok, "world type " + args);
		};
		while (std::getline(recorded, line))
		{
			if (!line.empty() && line.back() == 13) line.pop_back();
			if (line.rfind("# ", 0) == 0) { Compare(); args = line.substr(2); want.clear(); }
			else if (!line.empty()) want += line + "\n";
		}
		Compare();
	}

	// ---- every selectable world size: plan builds, is deterministic, memory stays bounded, far corners sample finite
	if (sizes)
	{
		std::printf("World sizes (recipe 'balanced', seed 7319)\n");
		std::vector<FWorldRecipe> recipes = FWorldRecipe::Catalog(profiles);
		for (double km : FWorldRecipe::Sizes())
		{
			FWorldChoices choices; choices.SizeKm = km;
			FWorldProfile p = recipes[0].Resolve(choices);
			bool valid = p.Validate(error);
			Check(valid, "profile valid at " + std::to_string((int)km) + " km: " + error);
			if (!valid) continue;
			double t0 = Now();
			auto plan = std::make_shared<FWorldPlan>(FWorldManifest::Create(7319, p), FTerrainStyle::For(p), current);
			double seconds = Now() - t0;
			FTerrainSampler s(plan);
			// A 257x257 tile at 1 m in the far corner region and near the centre: finite, and the same again.
			double far = p.WorldSize * .45;
			std::vector<double> a(65 * 65), b(65 * 65);
			double t1 = Now();
			s.HeightGrid(far, far, 1, 65, 65, a.data());
			double tile = Now() - t1;
			s.HeightGrid(far, far, 1, 65, 65, b.data());
			bool finite = true, same = true;
			for (size_t i = 0; i < a.size(); i++) { if (!std::isfinite(a[i])) finite = false; if (a[i] != b[i]) same = false; }
			const FLandFeature& f = plan->Layout.Land[0];
			FTerrainSample c = s.Sample(f.Center.X, f.Center.Z);
			Check(finite && same && std::isfinite(c.Height), "samples finite and repeatable at " + std::to_string((int)km) + " km");
			std::printf("  %5.0f km  raster %4d (cell %6.1f m)  plan %6.1f s  %7.1f MB  %5d lakes %6d reaches %8lld river points  65x65 tile %.0f ms  centre %.1f m\n",
				km, plan->Macro.Size, plan->Macro.Cell, seconds, plan->MemoryBytes() / 1048576.0, (int)plan->Hydrology->Lakes.size(),
				(int)plan->Hydrology->Reaches.size(), (long long)plan->Hydrology->PointCount(), tile * 1000, c.Height);
		}
	}

	if (bench)
	{
		const FWorldProfile& p = profiles[0];
		auto plan = std::make_shared<FWorldPlan>(FWorldManifest::Create(7319, p), FTerrainStyle::For(p), current);
		FTerrainSampler s(plan);
		const FLandFeature& f = plan->Layout.Land[0];
		std::vector<double> grid(129 * 129);
		for (double spacing : { 1.0, 4.0, 16.0, 64.0 })
		{
			double t0 = Now();
			s.HeightGrid(f.Center.X, f.Center.Z, spacing, 129, 129, grid.data());
			std::printf("  129x129 tile at %4.0f m spacing: %.1f ms single-threaded\n", spacing, (Now() - t0) * 1000);
		}
	}

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
