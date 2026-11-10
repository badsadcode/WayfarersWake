// --dump=profile,seed,revision[,gridSpacing]: per-grid and per-raster hashes, volcanoes and lakes of one test world,
// in the same format as the "--dump" option added to a scratch copy of Thornwake's Tests/WorldGen/Program.cs
// (see DECISIONS.md, "Finding a parity mismatch"). Diff the two outputs to find where a revision diverges.
#include "../../Source/WayfarersWake/WorldGen/WwLandforms.h"
#include <cstdio>
#include <sstream>

using namespace ww;

static std::string Short(const void* Data, size_t Bytes)
{
	FSha256 Sha;
	Sha.Append(Data, Bytes);
	return Sha.FinishHex().substr(0, 12);
}

static unsigned long long Bits(double V) { unsigned long long B; std::memcpy(&B, &V, sizeof B); return B; }

int RunDump(const std::vector<FWorldProfile>& Profiles, const FBiomeRegistry& Biomes, const std::string& Arg)
{
	std::stringstream ss(Arg); std::string id, t, grid;
	std::getline(ss, id, ','); std::getline(ss, t, ','); int seed = std::stoi(t); std::getline(ss, t, ','); int revision = std::stoi(t);
	std::getline(ss, grid, ',');
	for (const FWorldProfile& profile : Profiles)
	{
		if (profile.Id != id) continue;
		FTerrainStyle style = FTerrainStyle::For(profile);
		style.Revision = revision;
		auto plan = std::make_shared<FWorldPlan>(FWorldManifest::Create(seed, profile), style, Biomes);
		FTerrainSampler s(plan);
		const FLandFeature& f = plan->Layout.Land[0];
		std::vector<double> buffer(33 * 33);
		const double grids[4][3] = { { profile.WorldSize * -.5, profile.WorldSize * -.5, profile.WorldSize / 32 },
			{ f.Center.X, f.Center.Z, 1.0 }, { f.Center.X + f.RadiusX * .5, f.Center.Z, 97.0 }, { f.Center.X - f.RadiusX * 1.1, f.Center.Z, 13.0 } };
		for (const auto& g : grids)
		{
			s.HeightGrid(g[0], g[1], g[2], 33, 33, buffer.data());
			std::printf("grid %.0f %s\n", g[2], Short(buffer.data(), buffer.size() * sizeof(double)).c_str());
			char name[32]; std::snprintf(name, sizeof name, "%.0f", g[2]);
			if (grid == name) for (size_t i = 0; i < buffer.size(); i++) std::printf("h %d %016llx %.3f\n", (int)i, Bits(buffer[i]), buffer[i]);
		}
		for (int c = 0; c < 3; c++) std::printf("channel %d %s\n", c, Short(plan->Macro.Channel(c).data(), plan->Macro.Channel(c).size() * sizeof(float)).c_str());
		if (plan->Volcanoes)
			plan->Volcanoes->Cones([](double x, double z, double radius, bool caldera, bool active)
			{
				std::printf("volcano %016llx %016llx %016llx %s %s\n", Bits(x), Bits(z), Bits(radius), caldera ? "True" : "False", active ? "True" : "False");
			});
		std::printf("lakes %d reaches %d points %lld\n", (int)plan->Hydrology->Lakes.size(), (int)plan->Hydrology->Reaches.size(), (long long)plan->Hydrology->PointCount());
		for (const FLake& l : plan->Hydrology->Lakes) std::printf("lake %d %016llx %016llx\n", l.Id, Bits(l.Level), Bits(l.Area));
	}
	return 0;
}
