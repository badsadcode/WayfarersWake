// Checks of the world generation settings (WwWorldOptions).
//   WorldGenTest <repo root> --options
#include "../../Source/WayfarersWake/WorldGen/WwWorldOptions.h"
#include <cstdio>

using namespace ww;

int OptionsTest(const std::vector<FWorldProfile>& profiles, const FBiomeRegistry& biomes)
{
	int failed = 0, checks = 0;
	auto Check = [&](bool ok, const char* what, double a = 0, double b = 0)
	{
		checks++;
		std::printf("%s %s (%.3f, %.3f)\n", ok ? "ok  " : "FAIL", what, a, b);
		failed += !ok;
	};
	const std::vector<FWorldRecipe> recipes = FWorldRecipe::Catalog(profiles);
	const std::vector<FGenSetting> catalog = FWorldOptions::Catalog(recipes, biomes);
	std::printf("     %d settings\n", (int)catalog.size());
	// Every default lies in its range; keys are unique.
	{
		bool ok = true; std::map<std::string, int> seen;
		for (const FGenSetting& s : catalog) { ok &= s.Default >= s.Min && s.Default <= s.Max && !s.Key.empty() && !s.Name.empty(); seen[s.Key]++; }
		Check(ok && seen.size() == catalog.size(), "every setting has a name, a unique key and a default inside its range", (double)catalog.size(), (double)seen.size());
	}
	// The world as it was made before these settings existed (the game's old path) ...
	auto Old = [&](const char* type, double km, int seed)
	{
		const FWorldRecipe* recipe = nullptr;
		for (const FWorldRecipe& r : recipes) if (r.Id == type) recipe = &r;
		FWorldChoices choices; choices.SizeKm = km;
		FWorldProfile profile = recipe->Resolve(choices);
		return std::make_shared<FWorldPlan>(FWorldManifest::Create(seed, profile), FTerrainStyle::For(profile), biomes);
	};
	auto New = [&](const char* type, double km, int seed, const std::map<std::string, double>& values)
	{
		FWorldProfile profile; FTerrainStyle style; std::string error;
		if (!FWorldOptions::Build(recipes, biomes, type, km, FTerrainStyle::PortedRevision, values, profile, style, error)) { std::printf("build failed: %s\n", error.c_str()); std::exit(1); }
		return std::make_shared<FWorldPlan>(FWorldManifest::Create(seed, profile), style, biomes);
	};
	auto Same = [](const std::shared_ptr<FWorldPlan>& a, const std::shared_ptr<FWorldPlan>& b)
	{
		FTerrainSampler sa(a), sb(b);
		if (a->Hydrology->Reaches.size() != b->Hydrology->Reaches.size() || a->Hydrology->Lakes.size() != b->Hydrology->Lakes.size()) return false;
		const double half = a->Profile().WorldSize / 2;
		for (int i = 0; i < 400; i++)
		{
			const double x = -half + (i % 20 + .37) * half / 10, z = -half + (i / 20 + .61) * half / 10;
			const FTerrainSample ta = sa.Sample(x, z), tb = sb.Sample(x, z);
			if (ta.Height != tb.Height || ta.Water != tb.Water || ta.Land != tb.Land) return false;
			int a1, a2, b1, b2; double as, bs;
			a->Biomes->Sample(x, z, a1, a2, as); b->Biomes->Sample(x, z, b1, b2, bs);
			if (a1 != b1 || a2 != b2 || as != bs) return false;
		}
		return true;
	};
	// ... is, with every setting left alone, exactly the same world: heights, water and biomes at 400 places, bit for bit.
	const std::shared_ptr<FWorldPlan> before = Old("balanced", 64, 7319), after = New("balanced", 64, 7319, {});
	Check(before->Manifest.ProfileDigest == after->Manifest.ProfileDigest && Same(before, after), "balanced 64 km with every setting left alone is the same world as before, bit for bit");
	Check(Same(Old("mountainous", 32, 4242), New("mountainous", 32, 4242, {})), "and so is mountainous 32 km");
	// Settings change the world in the direction they say.
	{
		const std::shared_ptr<FWorldPlan> more = New("balanced", 64, 7319, { { "gen.rivers", 3 } }), fewer = New("balanced", 64, 7319, { { "gen.rivers", .3 } });
		Check(more->Hydrology->Reaches.size() > before->Hydrology->Reaches.size() && fewer->Hydrology->Reaches.size() < before->Hydrology->Reaches.size(),
			"more rivers gives more river reaches, fewer gives fewer", (double)more->Hydrology->Reaches.size(), (double)fewer->Hydrology->Reaches.size());
		const std::shared_ptr<FWorldPlan> flat = New("balanced", 64, 7319, { { "gen.mountain_height", 0 } });
		Check(flat->Profile().MountainHeight < before->Profile().MountainHeight * .1, "mountain height at 0 takes the mountains away", flat->Profile().MountainHeight, before->Profile().MountainHeight);
		const std::shared_ptr<FWorldPlan> cold = New("balanced", 64, 7319, { { "gen.warmth", -15 } });
		double t0, r0, t1, r1;
		before->Climate->At(0, 0, 50, t0, r0); cold->Climate->At(0, 0, 50, t1, r1);
		Check(t1 < t0 - 10, "fifteen degrees colder is colder", t1, t0);
		FWorldProfile profile; FTerrainStyle style; std::string error;
		FWorldOptions::Build(recipes, biomes, "balanced", 64, 44, { { "gen.biome.desert", 0 }, { "gen.landform.volcanoes.amount", 0 }, { "gen.version", 11 } }, profile, style, error);
		Check(style.Climate.ExcludedBiomes.size() == 1 && style.Landform("volcanoes").Off() && !style.Landform("canyons").Off() && style.Revision == 20,
			"a biome switched off is excluded, a landform set to off is off, an old version sets its revision", (double)style.Climate.ExcludedBiomes.size(), style.Revision);
		const bool shaped = FWorldOptions::Build(recipes, biomes, "balanced", 64, 44, { { "gen.shape", 3 } }, profile, style, error);
		Check(shaped && profile.Digest() != before->Profile().Digest(), "another land shape gives another world");
	}
	// What a world records reads back the same.
	{
		const std::map<std::string, double> values = { { "gen.rivers", 2.35 }, { "gen.islands", 120 }, { "gen.biome.desert", 0 } };
		Check(FWorldOptions::Read(FWorldOptions::Write(values)) == values, "a world's record of its settings reads back the same");
	}
	std::printf("%d checks, %d failed\n", checks, failed);
	return failed;
}
