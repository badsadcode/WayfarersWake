// Port of Thornwake Game/WorldGen: Headlands.cs (revisions 36, 38) and Spits.cs (revision 37).
#include "WwLandforms.h"

namespace ww
{
namespace
{
const double CoastGridCell = 8000, CoastPi = 3.141592653589793;

struct FAway { double X, Z, Radius; };

// Places the other coastal landforms keep clear of: fjord coasts, volcanoes, river mouths, lakes.
void CollectAway(const FWorldPlan& Plan, double MouthRadius, std::vector<FAway>& Away)
{
	if (Plan.Troughs) Plan.Troughs->CoastProbes([&](double x, double z, double r) { Away.push_back({ x, z, r }); });
	if (Plan.Volcanoes) Plan.Volcanoes->Cones([&](double x, double z, double r, bool, bool) { Away.push_back({ x, z, r * 1.3 }); });
	for (const FRiverReach& reach : Plan.Hydrology->Reaches)
		if (reach.bEndsInSea) Away.push_back({ reach.Points.back().X, reach.Points.back().Z, MouthRadius });
	for (const FLake& lake : Plan.Hydrology->Lakes) Away.push_back({ lake.Center.X, lake.Center.Z, 2 * std::sqrt(lake.Area / CoastPi) + 1500 });
}
}

// ------------------------------------------------------------------------------------------------ headlands
int FHeadlands::Cell(double v, double min) const { return ClampI((int)std::floor((v - min) / CoastGridCell), 0, GridSize - 1); }

FHeadlands::FHeadlands(const FWorldPlan& Plan, FLandformSetting Setting)
{
	const FMacroRaster& macro = Plan.Macro; const FHydrology& hy = *Plan.Hydrology;
	FRandomStream rng(LandformSeed(Plan.Manifest, "headlands"));
	const std::vector<float>& land = macro.Channel(FWorldPlan::LandChannel);
	int n = macro.Size;
	std::vector<int> coast;
	for (int j = 1; j < n - 1; j++)
		for (int i = 1; i < n - 1; i++)
		{
			int k = j * n + i;
			if (land[k] > 0 && (land[k - 1] <= 0 || land[k + 1] <= 0 || land[k - n] <= 0 || land[k + n] <= 0)) coast.push_back(k);
		}
	std::vector<FAway> away;
	CollectAway(Plan, 900, away);
	int want = ClampI((int)std::nearbyint(Setting.Amount * coast.size() * macro.Cell / 45000), !coast.empty() ? 1 : 0, 160);
	for (int attempt = 0; attempt < want * 12 && (int)Stretches.size() < want && !coast.empty(); attempt++)
	{
		int k = coast[rng.Int((int)coast.size())];
		double x = macro.CellX(k % n), z = macro.CellZ(k / n);
		double radius = rng.Between(4000, 10000) * Setting.Size, wave = rng.Between(1400, 2800) * Setting.Size;
		double outward = rng.Between(280, 700) * Setting.Size, inward = rng.Between(160, 420) * Setting.Size, rise = rng.Between(18, 60) * Setting.Size;
		u64 seed = rng.Next();
		double gx, gz;
		macro.SampleGradient(FWorldPlan::LandChannel, x, z, gx, gz);
		double g = std::sqrt(gx * gx + gz * gz);
		if (g < 1e-12) continue;
		double dx = -gx / g, dz = -gz / g;
		bool clear = true;
		for (const FAway& a : away) if (std::sqrt((a.X - x) * (a.X - x) + (a.Z - z) * (a.Z - z)) < a.Radius + radius) { clear = false; break; }
		for (const FStretch& s : Stretches) if (std::sqrt((s.X - x) * (s.X - x) + (s.Z - z) * (s.Z - z)) < s.Radius + radius + 3000) { clear = false; break; }
		// No river or lake within the stretch's reach of the coast: their mouths and shores stay where the plan has them.
		for (double t = -radius; t <= radius && clear; t += Mx(1000, radius / 4))
			for (double o : { -1000.0, 0.0, 1000.0 })
			{
				double px = x - dz * t + dx * o, pz = z + dx * t + dz * o;
				FRiverHit hit;
				if (hy.NearestRiver(px, pz, 1600, hit) || hy.LakeNear(px, pz) >= 0) { clear = false; break; }
			}
		if (clear) Stretches.push_back({ x, z, dx, dz, radius, outward, inward, wave, rise, seed });
	}
	MinX = macro.MinX; MinZ = macro.MinZ;
	GridSize = MxI(1, (int)std::ceil(macro.Size * macro.Cell / CoastGridCell));
	std::vector<std::vector<int>> lists((size_t)GridSize * GridSize);
	for (int s = 0; s < (int)Stretches.size(); s++)
	{
		const FStretch& st = Stretches[s];
		for (int j = Cell(st.Z - st.Radius, MinZ); j <= Cell(st.Z + st.Radius, MinZ); j++)
			for (int i = Cell(st.X - st.Radius, MinX); i <= Cell(st.X + st.Radius, MinX); i++)
				lists[(size_t)j * GridSize + i].push_back(s);
	}
	FlattenGrid(lists, GridStart, GridItems);
}

/// How far the coastline moves here (metres, out to sea above 0). point: how high this point's rock stands;
/// cove: how much of a cove this is (0..1).
double FHeadlands::Shift(double x, double z, double& point, double& cove) const
{
	point = 0; cove = 0;
	if (Stretches.empty()) return 0;
	int c = Cell(z, MinZ) * GridSize + Cell(x, MinX);
	double total = 0;
	for (int k = GridStart[c]; k < GridStart[c + 1]; k++)
	{
		const FStretch& s = Stretches[GridItems[k]];
		double dx = x - s.X, dz = z - s.Z, d2 = dx * dx + dz * dz;
		if (d2 >= s.Radius * s.Radius) continue;
		double w = 1 - DetMath::Smooth((std::sqrt(d2) - .5 * s.Radius) / (.5 * s.Radius));
		if (w <= 0) continue;
		// Point (above 0) or cove (below) at a position along the coast.
		double n = Noise::Fbm(dx * -s.Dz + dz * s.Dx, 11.5, s.Wave, 2, 0, s.Seed);
		double p = DetMath::Smooth((n - .1) / .34), q = DetMath::Smooth((-n + .04) / .3);
		total += w * (s.Out * p - s.In * q);
		point = Mx(point, w * p * s.Rise);
		cove = Mx(cove, w * q);
	}
	return total;
}

/// The land's height there (metres above the sea in and out): a point is rock with cliffs and crags; a cove's ground
/// is let down to a low beach. coast: metres from the detailed coastline. rounded: revision 38's rolled-over cliff top.
double FHeadlands::Relief(double x, double z, double minWavelength, double ground, double point, double cove, double coast, double& rock,
	bool rounded, double moved)
{
	rock = 0;
	double shoulder = 0;
	if (rounded && moved != 0 && ground > 3 && !std::isinf(coast))
	{
		double run = .25 * ground + 5, u = DetMath::Clamp((coast - 1.5) / run, 0, 1), v = 1 - u;
		double rough = (Noise::Ridged(x, z, 18, 3, minWavelength, 0x4EB0ULL, .5) - .45) * Mn(6, .12 * ground);
		double cap = 2.5 * DetMath::Smooth(coast / 5) + (ground - 2.5) * (1 - v * v * v) + rough * 4 * u * v;
		double k = DetMath::Smooth(std::fabs(moved) / 15);
		ground += (Mn(ground, cap) - ground) * k;
		shoulder = k * (1 - DetMath::Smooth((coast - run) / 6));
	}
	if (point > 0)
	{
		double c = std::isinf(coast) ? 1000 : coast;
		double line = c + 28 * Noise::Fbm(x, z, 75, 2, minWavelength, 0x4EAEULL);
		double crag = Noise::Ridged(x, z, 240, 5, minWavelength, 0x4EADULL, .5) - .3;
		double cliff = DetMath::Smooth(line / 35);
		double top = point * (.8 + 1.1 * crag) * cliff * (.7 + .3 * DetMath::Smooth(c / 300));
		if (rounded)
		{
			// h = top (1 - (1 - u)^3): steep at the foot, rolling over to level at the top (a convex shoulder, no lip).
			double run = .25 * Mx(top, 0) + 5, u = DetMath::Clamp((c - 1.5) / run, 0, 1), v = 1 - u;
			ground = Mx(ground, 2.5 * DetMath::Smooth(c / 5) + top * (1 - v * v * v));
		}
		else ground = Mx(ground, 2.5 * DetMath::Smooth(c / 5) + DetMath::Smooth(c / 8) * top);
		// Bare: the cliff band, and patches over the top (more on the higher crags).
		double strength = Mn(1, point / 12);
		double patches = DetMath::Smooth((crag + .1 + .35 * Noise::Fbm(x, z, 110, 2, minWavelength, 0x4EAFULL)) / .3);
		rock = strength * Mx(.85 * (1 - DetMath::Smooth((line - 45) / 45)), .75 * patches * (1 - DetMath::Smooth((c - 420) / 200)));
	}
	rock = Mx(rock, .8 * shoulder);
	if (cove > 0 && !std::isinf(coast))
	{
		double beach = 1.2 + .03 * coast, k = cove * (1 - DetMath::Smooth((coast - 180) / 340));
		if (beach < ground) ground += (beach - ground) * k;
	}
	return ground;
}

// ------------------------------------------------------------------------------------------------ spits
int FSpits::Cell(double v, double min) const { return ClampI((int)std::floor((v - min) / CoastGridCell), 0, GridSize - 1); }

FSpits::FSpits(const FWorldPlan& Plan, FLandformSetting Setting)
{
	const FMacroRaster& macro = Plan.Macro; double oceanDepth = Plan.Profile().OceanDepth;
	FRandomStream rng(LandformSeed(Plan.Manifest, "spits"));
	const std::vector<float>& land = macro.Channel(FWorldPlan::LandChannel);
	int n = macro.Size;
	std::vector<FAway> away;
	CollectAway(Plan, 1500, away);
	if (Plan.Headlands) Plan.Headlands->Probes([&](double x, double z, double r) { away.push_back({ x, z, r + 500 }); });
	// Flat coasts with a gentle sea floor: low land behind, and under 40 m of water 1.5 km out.
	auto Gentle = [&](double x, double z, double& dx, double& dz, double& g)
	{
		double gx, gz;
		macro.SampleGradient(FWorldPlan::LandChannel, x, z, gx, gz);
		g = std::sqrt(gx * gx + gz * gz);
		dx = dz = 0;
		if (g < 1e-12) return false;
		dx = -gx / g; dz = -gz / g;
		return oceanDepth * DetMath::Smooth(g * 1500 / .6) < 40;
	};
	struct FCandidate { double X, Z, Dx, Dz; };
	std::vector<FCandidate> candidates;
	for (int j = 1; j < n - 1; j++)
		for (int i = 1; i < n - 1; i++)
		{
			int k = j * n + i;
			if (land[k] <= 0 || (land[k - 1] > 0 && land[k + 1] > 0 && land[k - n] > 0 && land[k + n] > 0)) continue;
			double x = macro.CellX(i), z = macro.CellZ(j), dx, dz, g;
			if (!Gentle(x, z, dx, dz, g)) continue;
			if (macro.Sample(FWorldPlan::HeightChannel, x - dx * 1500, z - dz * 1500) > 110) continue;
			candidates.push_back({ x, z, dx, dz });
		}
	int want = ClampI((int)std::nearbyint(Setting.Amount * candidates.size() * .05), !candidates.empty() ? 1 : 0, 70);
	for (int attempt = 0; attempt < want * 12 && (int)Sites.size() < want && !candidates.empty(); attempt++)
	{
		const FCandidate c = candidates[rng.Int((int)candidates.size())];
		double roll = rng.Unit(), side = rng.Unit() < .5 ? -1 : 1;
		int kind = roll < .45 ? 0 : roll < .8 ? 1 : 2; // spit, barrier, bar
		double length = rng.Between(1800, 5500) * Setting.Size, off = rng.Between(380, 850) * Setting.Size, width = rng.Between(45, 95) * Setting.Size;
		double crest = kind == 2 ? rng.Between(.2, .7) : rng.Between(1.3, 2.3), lagoon = rng.Between(1.2, 3.2);
		double gap = rng.Between(-.4, .4) * length, gapWidth = rng.Between(90, 240);
		u64 seed = rng.Next();
		double tx = -c.Dz * side, tz = c.Dx * side;
		double t0 = kind == 0 ? -300 : -length - 300, t1 = length + 300;
		bool clear = true;
		for (const FAway& a : away) if (std::sqrt((a.X - c.X) * (a.X - c.X) + (a.Z - c.Z) * (a.Z - c.Z)) < a.Radius + length + 500) { clear = false; break; }
		for (const FSite& s : Sites) if (std::sqrt((s.X - c.X) * (s.X - c.X) + (s.Z - c.Z) * (s.Z - c.Z)) < s.Length + length + 2500) { clear = false; break; }
		// The coast runs straight enough, with the same gentle sea, all along: no ridge across a headland or a bend.
		for (double t = t0; t <= t1 && clear; t += 400)
		{
			double px = c.X + tx * t + c.Dx * off, pz = c.Z + tz * t + c.Dz * off, ux, uz, g;
			if (!Gentle(px, pz, ux, uz, g) || ux * c.Dx + uz * c.Dz < .8) { clear = false; break; }
			double d = -macro.Sample(FWorldPlan::LandChannel, px, pz) / g;
			if (d < off * .45 || d > off * 1.7) clear = false;
		}
		if (clear) Sites.push_back({ kind, c.X, c.Z, tx, tz, length, off, width, crest, lagoon, gap, gapWidth, seed });
	}
	MinX = macro.MinX; MinZ = macro.MinZ;
	GridSize = MxI(1, (int)std::ceil(macro.Size * macro.Cell / CoastGridCell));
	std::vector<std::vector<int>> lists((size_t)GridSize * GridSize);
	for (int s = 0; s < (int)Sites.size(); s++)
	{
		const FSite& st = Sites[s];
		double reach = st.Length + st.Off + 400;
		for (int j = Cell(st.Z - reach, MinZ); j <= Cell(st.Z + reach, MinZ); j++)
			for (int i = Cell(st.X - reach, MinX); i <= Cell(st.X + reach, MinX); i++)
				lists[(size_t)j * GridSize + i].push_back(s);
	}
	FlattenGrid(lists, GridStart, GridItems);
}

/// The sea bed here (metres above the sea in and out) with the sand on it. distance: metres out from the plan's coastline.
double FSpits::Seabed(double x, double z, double distance, double bed) const
{
	if (Sites.empty()) return bed;
	int c = Cell(z, MinZ) * GridSize + Cell(x, MinX);
	for (int k = GridStart[c]; k < GridStart[c + 1]; k++)
	{
		const FSite& s = Sites[GridItems[k]];
		double dx = x - s.X, dz = z - s.Z, t = dx * s.Tx + dz * s.Tz;
		// How far along (0 at the root or the near end, 1 at the tip or far end), and its line out from the coast.
		double u = s.Kind == 0 ? t / s.Length : (t + s.Length) / (2 * s.Length);
		if (u < -.05 || u > 1.05) continue;
		double along = Noise::Fbm(t, 3.5, 900, 2, 0, s.Seed);
		double off, width = s.Width * (1 + .35 * Noise::Fbm(t, 8.5, 260, 2, 0, s.Seed + 1)), crest = s.Crest;
		bool lagoon;
		switch (s.Kind)
		{
		case 0:
			// Out from the shore over its first third, curling back toward it at its tip, thinner toward the tip.
			off = s.Off * (DetMath::Smooth(u / .35) * (1 - .6 * DetMath::Smooth((u - .84) / .16)) + .16 * along);
			width *= 1 - .45 * DetMath::Clamp(u, 0, 1);
			crest *= DetMath::Smooth((1.02 - u) / .05);
			lagoon = true;
			break;
		case 1:
			// Joined to the shore at both ends, an inlet left between the two spits.
			off = s.Off * (DetMath::Smooth(u / .28) * DetMath::Smooth((1 - u) / .28) + .13 * along);
			crest = -2.6 + (crest + 2.6) * DetMath::Smooth((std::fabs(t - s.Gap) - s.GapWidth) / 70);
			lagoon = true;
			break;
		default:
			// Offshore, ending in the water both ways, broken into shoals.
			off = s.Off * (1.1 + .22 * along);
			width *= 1.3;
			crest = -2.2 + (crest + 2.2) * DetMath::Smooth(.55 + 1.5 * Noise::Fbm(t, 5.5, 480, 2, 0, s.Seed + 2)) * DetMath::Smooth(Mn(u, 1 - u) / .14);
			lagoon = false;
			break;
		}
		double ends = DetMath::Smooth(Mn(u, 1 - u) / .05 + 1);
		// Behind the ridge the sea floor rises to a shallow lagoon; the bay behind a spit opens toward its tip.
		if (lagoon)
		{
			double behind = DetMath::Smooth((off - distance) / 70) * DetMath::Smooth(u / .12) * (s.Kind == 0 ? 1 - DetMath::Smooth((u - .8) / .25) : DetMath::Smooth((1 - u) / .12));
			double floor = -s.Lagoon * (.75 + .35 * Noise::Fbm(x, z, 300, 2, 0, s.Seed + 3)) * DetMath::Smooth(distance / 90 + .25);
			if (behind > 0 && floor > bed) bed += (floor - bed) * behind;
		}
		// The ridge: a low crest with small dunes, its sides falling away under the water.
		double across = std::fabs(distance - off);
		if (across < width * 2.4 && width > 1)
		{
			double top = crest > 0 ? crest * (1 + .55 * Noise::Fbm(x, z, 90, 2, 0, s.Seed + 4)) : crest;
			double ridge = top - (top + 4.5) * DetMath::Smooth(across / (width * 2.1));
			bed = Mx(bed, ridge - (1 - ends) * 6);
		}
	}
	return bed;
}
}
