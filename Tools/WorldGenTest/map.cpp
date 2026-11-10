// Checks of the map picture (WwMap), and pictures to look at.
//   WorldGenTest <repo root> --map     (writes Saved/map-*.bmp: the whole world and a close view, in each style)
#include "../../Source/WayfarersWake/WorldGen/WwMap.h"
#include <chrono>
#include <cstdio>
#include <fstream>

using namespace ww;

namespace
{
double MapNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void WriteBmp(const std::string& path, const FMapImage& image)
{
	std::ofstream f(path, std::ios::binary);
	const unsigned n = (unsigned)image.Pixels, bytes = n * n * 4;
	auto U32 = [&](unsigned v) { f.write((const char*)&v, 4); };
	auto U16 = [&](unsigned short v) { f.write((const char*)&v, 2); };
	f.write("BM", 2); U32(54 + bytes); U32(0); U32(54);
	U32(40); U32(n); U32((unsigned)-(int)n); U16(1); U16(32); U32(0); U32(bytes); U32(2835); U32(2835); U32(0); U32(0); // (negative height: rows from the top)
	f.write((const char*)image.Bgra.data(), bytes);
}

struct FShare { double Land = 0, Warm = 0, Blue = 0; };
FShare Measure(const FMapImage& image)
{
	FShare s;
	const size_t count = (size_t)image.Pixels * image.Pixels;
	for (size_t k = 0; k < count; k++)
	{
		const int b = image.Bgra[k * 4], g = image.Bgra[k * 4 + 1], r = image.Bgra[k * 4 + 2];
		s.Warm += r > b + 25; s.Blue += b > r + 25;
	}
	s.Warm /= count; s.Blue /= count;
	return s;
}
}

int MapTest(const std::string& root, const std::vector<FWorldProfile>& profiles, const FBiomeRegistry& biomes)
{
	int failed = 0, checks = 0;
	auto Check = [&](bool ok, const char* what, double a = 0, double b = 0)
	{
		checks++;
		std::printf("%s %s (%.3f, %.3f)\n", ok ? "ok  " : "FAIL", what, a, b);
		failed += !ok;
	};
	// The world the game starts in: balanced, 64 km, seed 7319.
	const FWorldRecipe* recipe = nullptr;
	std::vector<FWorldRecipe> recipes = FWorldRecipe::Catalog(profiles);
	for (const FWorldRecipe& r : recipes) if (r.Id == "balanced") recipe = &r;
	if (!recipe) { std::printf("no balanced world type\n"); return 1; }
	FWorldChoices choices; choices.SizeKm = 64;
	FWorldProfile profile = recipe->Resolve(choices);
	auto plan = std::make_shared<FWorldPlan>(FWorldManifest::Create(7319, profile), FTerrainStyle::For(profile), biomes);
	FTerrainSampler sampler(plan);

	const char* names[3] = { "terrain", "biomes", "parchment" };
	FMapImage whole[3], close[3];
	for (int s = 0; s < 3; s++)
	{
		double t0 = MapNow();
		whole[s] = FWorldMap::Render(sampler, 1024, (EMapStyle)s);
		double t1 = MapNow();
		// (the forest Tom looks at in his dumps, 3 km across: fine enough for the real terrain)
		close[s] = FWorldMap::Render(sampler, 1024, (EMapStyle)s, 15534, 8044, 3000);
		double t2 = MapNow();
		std::printf("     %s: the whole world in %.0f ms, a 3 km view in %.0f ms\n", names[s], (t1 - t0) * 1000, (t2 - t1) * 1000);
		WriteBmp(root + "/Saved/map-" + names[s] + ".bmp", whole[s]);
		WriteBmp(root + "/Saved/map-" + names[s] + "-close.bmp", close[s]);
	}
	Check(!whole[0].bDetailed && close[0].bDetailed, "the whole world is read from the plan, a close view from the real terrain", whole[0].Span / 1024, close[0].Span / 1024);
	FShare terrain = Measure(whole[0]), parchment = Measure(whole[2]);
	Check(terrain.Blue > .15 && terrain.Blue < .9, "the terrain map has sea and land", terrain.Blue, terrain.Warm);
	Check(parchment.Warm > .9 && parchment.Blue < .01, "the parchment is paper-coloured all over, sea included", parchment.Warm, parchment.Blue);
	// The pixel under a known place shows what is there: land at the start forest, sea in the open ocean.
	{
		const FMapImage& m = whole[0];
		auto At = [&](double x, double z, int c) { int i = (int)((x - (m.CentreX - m.Span / 2)) / m.Span * m.Pixels), j = (int)((z - (m.CentreZ - m.Span / 2)) / m.Span * m.Pixels); return (int)m.Bgra[((size_t)j * m.Pixels + i) * 4 + c]; };
		const bool landIsLand = sampler.Sample(15534, 8044).Land > 0;
		Check(landIsLand && At(15534, 8044, 0) < At(15534, 8044, 2) + 40, "the forest at 15.5 km east, 8.0 km south is drawn as land", At(15534, 8044, 0), At(15534, 8044, 2));
		// (the far north-west corner of the square)
		double sx = m.CentreX - m.Span / 2 + m.Span * .02, sz = m.CentreZ - m.Span / 2 + m.Span * .02;
		const bool sea = sampler.Sample(sx, sz).Land <= 0;
		Check(!sea || At(sx, sz, 0) > At(sx, sz, 2), "the open sea in the corner is drawn as sea", At(sx, sz, 0), At(sx, sz, 2));
	}
	// The same view twice is the same picture (nothing random in it).
	{
		FMapImage again = FWorldMap::Render(sampler, 1024, EMapStyle::Parchment, 15534, 8044, 3000);
		Check(again.Bgra == close[2].Bgra, "the same view drawn twice is the same picture");
	}
	std::printf("%d checks, %d failed\n", checks, failed);
	return failed;
}
