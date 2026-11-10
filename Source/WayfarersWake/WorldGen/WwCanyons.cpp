// Port of Thornwake Game/WorldGen: Canyons.cs (revision 21, 22) and Cirques.cs (revision 23).
#include "WwLandforms.h"

namespace ww
{
void FlattenGrid(const std::vector<std::vector<int>>& cells, std::vector<int>& start, std::vector<int>& items)
{
	start.assign(cells.size() + 1, 0);
	items.clear();
	for (size_t c = 0; c < cells.size(); c++) { start[c] = (int)items.size(); items.insert(items.end(), cells[c].begin(), cells[c].end()); }
	start[cells.size()] = (int)items.size();
}

int LandCellCount(const FMacroRaster& M)
{
	int n = 0;
	for (float v : M.Channel(FWorldPlan::LandChannel)) if (v > .5f) n++;
	return n;
}

// ------------------------------------------------------------------------------------------------ canyons
namespace
{
const double CanyonGridCell = 2000, CanyonSpacing = 100;

template <class T> double CanyonSegmentDistance(const T& c, int i, double x, double z, double& u)
{
	double ax = c.X[i], az = c.Z[i], dx = c.X[i + 1] - ax, dz = c.Z[i + 1] - az, len2 = Mx(1e-9, dx * dx + dz * dz);
	u = DetMath::Clamp(((x - ax) * dx + (z - az) * dz) / len2, 0, 1);
	return std::sqrt(Sq(ax + dx * u - x) + Sq(az + dz * u - z));
}
}

int FCanyons::Cell(double v, double min) const { return ClampI((int)std::floor((v - min) / CanyonGridCell), 0, GridSize - 1); }

FCanyons::FCanyons(const FWorldPlan& Plan, FLandformSetting Setting)
{
	Revision = Plan.Style.Revision;
	const FMacroRaster& macro = Plan.Macro; const FHydrology& hy = *Plan.Hydrology;
	FRandomStream rng(LandformSeed(Plan.Manifest, "canyons"));
	double landKm2 = LandCellCount(macro) * macro.Cell * macro.Cell / 1e6;
	int want = ClampI((int)std::nearbyint(Setting.Amount * landKm2 / (180 * 180)), landKm2 > 6000 ? 1 : 0, 48);
	std::vector<int> candidates;
	for (const FRiverReach& r : hy.Reaches)
		if (!r.bDistributary && r.Length() > 4000 && r.Catchment[r.Catchment.size() / 2] >= 150) candidates.push_back(r.Id);
	for (int attempt = 0; attempt < want * 30 && (int)Canyons.size() < want && !candidates.empty(); attempt++)
	{
		const FRiverReach& r = hy.Reaches[candidates[rng.Int((int)candidates.size())]];
		double rLength = r.Length();
		double length = Mn(rng.Between(3000, 10000) * Setting.Size, rLength * .8);
		double start = rng.Between(rLength * .1, Mx(rLength * .1, rLength * .9 - length));
		double depth = rng.Between(60, 180) * Setting.Size, band = rng.Between(600, 1500), fade = rng.Between(1500, 2500);
		u64 seed = rng.Next();
		if (length < 2500) continue;
		// The stretch, resampled every 100 m; checked all along: inland, off the mountains, no lake, a gentle grade.
		std::vector<double> xs, zs;
		int np = (int)r.Points.size();
		int k = 0; while (k < np - 1 && r.Along[k] < start) k++;
		int k0 = k;
		double next = r.Along[k];
		for (; k < np && r.Along[k] <= start + length; k++)
			if (r.Along[k] >= next) { xs.push_back(r.Points[k].X); zs.push_back(r.Points[k].Z); next = r.Along[k] + CanyonSpacing; }
		int k1 = MnI(k, np - 1);
		float run = r.Along[k1] - r.Along[k0];
		if (xs.size() < 20 || (r.Surface[k0] - r.Surface[k1]) / (run > 1.f ? run : 1.f) > .01) continue;
		bool ok = true;
		double reach = band + fade;
		int count = (int)xs.size();
		for (int i = 0; i < count && ok; i += 3)
		{
			int i1 = MnI(i + 1, count - 1), i0 = MxI(i - 1, 0);
			double tx = xs[i1] - xs[i0], tz = zs[i1] - zs[i0], tl = Mx(1e-6, std::sqrt(tx * tx + tz * tz));
			double nx = -tz / tl, nz = tx / tl;
			for (double side : { -1.0, 1.0 })
				for (double d : { band * .5, reach })
				{
					double px = xs[i] + nx * d * side, pz = zs[i] + nz * d * side;
					if (macro.Sample(FWorldPlan::LandChannel, px, pz) < .5 || macro.Sample(FWorldPlan::MountainChannel, px, pz) > .3 ||
						hy.LakeNear(px, pz) >= 0) ok = false;
				}
			if (hy.LakeNear(xs[i], zs[i]) >= 0) ok = false;
			for (const FCanyon& other : Canyons)
				if (std::fabs(other.X[other.X.size() / 2] - xs[i]) + std::fabs(other.Z[other.Z.size() / 2] - zs[i]) < 20000) ok = false;
		}
		if (!ok) continue;
		std::vector<double> along(count, 0.0);
		for (int i = 1; i < count; i++) along[i] = along[i - 1] + std::sqrt(Sq(xs[i] - xs[i - 1]) + Sq(zs[i] - zs[i - 1]));
		FCanyon c;
		c.X = xs; c.Z = zs; c.Along = along; c.Depth = depth; c.Band = band; c.Fade = fade; c.Length = along.back(); c.Seed = seed;
		c.MinX = c.MaxX = c.X[0]; c.MinZ = c.MaxZ = c.Z[0];
		for (int i = 1; i < count; i++)
		{
			c.MinX = Mn(c.MinX, c.X[i]); c.MaxX = Mx(c.MaxX, c.X[i]);
			c.MinZ = Mn(c.MinZ, c.Z[i]); c.MaxZ = Mx(c.MaxZ, c.Z[i]);
		}
		Canyons.push_back(std::move(c));
	}
	// Canyons near each 2 km cell.
	MinX = macro.MinX; MinZ = macro.MinZ;
	GridSize = (int)std::ceil(macro.Size * macro.Cell / CanyonGridCell);
	std::vector<std::vector<int>> cells((size_t)GridSize * GridSize);
	for (int n = 0; n < (int)Canyons.size(); n++)
	{
		const FCanyon& c = Canyons[n];
		double reach = c.Band + c.Fade + CanyonGridCell;
		for (int j = Cell(c.MinZ - reach, MinZ); j <= Cell(c.MaxZ + reach, MinZ); j++)
			for (int i = Cell(c.MinX - reach, MinX); i <= Cell(c.MaxX + reach, MinX); i++)
				cells[(size_t)j * GridSize + i].push_back(n);
	}
	FlattenGrid(cells, GridStart, GridItems);
}

double FCanyons::Uplift(double x, double z) const
{
	if (Canyons.empty()) return 0;
	int cell = Cell(z, MinZ) * GridSize + Cell(x, MinX);
	double best = 0;
	for (int n = GridStart[cell]; n < GridStart[cell + 1]; n++)
	{
		const FCanyon& c = Canyons[GridItems[n]];
		// Distance to the canyon's river line and the position along it, blended over the segments within 60 m.
		double nearest = std::numeric_limits<double>::max(), u;
		int segments = (int)c.X.size() - 1;
		for (int i = 0; i < segments; i++) nearest = Mn(nearest, CanyonSegmentDistance(c, i, x, z, u));
		double ws = 0, ts = 0;
		for (int i = 0; i < segments; i++)
		{
			double d = CanyonSegmentDistance(c, i, x, z, u);
			double w = DetMath::Smooth(1 - (d - nearest) / 60);
			if (w <= 0) continue;
			ws += w; ts += w * (c.Along[i] + u * (c.Along[i + 1] - c.Along[i]));
		}
		double dist = nearest, t = ts / ws;
		double across = 1 - DetMath::Smooth((dist - c.Band) / c.Fade);
		if (across <= 0) continue;
		double ends, vary;
		if (Revision >= 22)
		{
			double toStart = std::sqrt(Sq(x - c.X[0]) + Sq(z - c.Z[0])), toEnd = std::sqrt(Sq(x - c.X.back()) + Sq(z - c.Z.back()));
			ends = DetMath::Smooth(toStart / 1500) * DetMath::Smooth(toEnd / 1500);
			vary = 1 + .2 * Noise::Fbm(x, z, 2500, 2, 0, c.Seed);
		}
		else
		{
			ends = DetMath::Smooth(t / 1200) * DetMath::Smooth((c.Length - t) / 1200);
			vary = 1 + .2 * Noise::Fbm(t, 0, 2500, 2, 0, c.Seed);
		}
		best = Mx(best, c.Depth * vary * across * ends);
	}
	return best;
}

// ------------------------------------------------------------------------------------------------ cirques
namespace
{
const double CirqueGridCell = 2000, TarnBelow = 2;

template <class B> double CirqueReach(const B& b, double x, double z, double& uphill)
{
	double px = (x - b.Fx) / b.Radius, pz = (z - b.Fz) / b.Radius;
	double along = px * b.Dx + pz * b.Dz, across = -px * b.Dz + pz * b.Dx;
	double r = std::sqrt(along * along * 1.2 + across * across / 1.25);
	uphill = r > 1e-9 ? -along / std::sqrt(along * along + across * across + 1e-12) : 0;
	return r * (1 + .2 * Noise::Fbm(x, z, b.Radius * .7, 2, 0, b.Seed));
}

template <class B> double CirqueSurface(const B& b, double x, double z, double detail)
{
	double uphill;
	double r = CirqueReach(b, x, z, uphill);
	if (r > 1.2) return Inf;
	double rough = r < .55 ? 3 * Noise::Fbm(x, z, 45, 3, 0, b.Seed + 1) * (1 - DetMath::Smooth((r - .45) / .1))
		: .8 * detail * DetMath::Smooth((r - .55) / .35);
	if (r < .55) { double u = r / .55; return b.Floor - b.Depth * (1 - u * u) + rough; }
	double slope = b.Front + (b.Back - b.Front) * DetMath::Smooth(.5 + .5 * uphill);
	return b.Floor + (r - .55) * b.Radius * slope + rough;
}

double CirqueSmoothMin(double a, double b, double k)
{
	double h = DetMath::Clamp(.5 + .5 * (b - a) / k, 0, 1);
	return b + (a - b) * h - k * h * (1 - h);
}
}

int FCirques::Cell(double v, double min) const { return ClampI((int)std::floor((v - min) / CirqueGridCell), 0, GridSize - 1); }

FCirques::FCirques(const FWorldPlan& Plan, FLandformSetting Setting)
{
	const FMacroRaster& macro = Plan.Macro; const FHydrology& hy = *Plan.Hydrology; const FWorldProfile& p = Plan.Profile();
	Sea = p.SeaLevel;
	FRandomStream rng(LandformSeed(Plan.Manifest, "cirques"));
	const std::vector<float>& mountain = macro.Channel(FWorldPlan::MountainChannel);
	const std::vector<float>& height = macro.Channel(FWorldPlan::HeightChannel);
	double high = p.LowlandHeight + .3 * p.MountainHeight;
	std::vector<int> starts;
	for (int k = 0; k < (int)mountain.size(); k++)
		if (mountain[k] > .35f && height[k] > high) starts.push_back(k);
	double km2 = starts.size() * macro.Cell * macro.Cell / 1e6;
	int want = ClampI((int)std::nearbyint(Setting.Amount * km2 / (10 * 10)), 0, 600);
	for (int attempt = 0; attempt < want * 40 && (int)Bowls.size() < want && !starts.empty(); attempt++)
	{
		int k = starts[rng.Int((int)starts.size())];
		double cx = macro.CellX(k % macro.Size) + rng.Between(-.5, .5) * macro.Cell, cz = macro.CellZ(k / macro.Size) + rng.Between(-.5, .5) * macro.Cell;
		double radius = rng.Between(250, 650) * Setting.Size, depth = rng.Between(6, 18), chance = rng.Unit();
		double gx, gz;
		double hc = macro.SampleGradient(FWorldPlan::HeightChannel, cx, cz, gx, gz), grade = std::sqrt(gx * gx + gz * gz);
		if (grade < .12 || grade > .8) continue; // a flank: not a summit or a plain, not a cliff
		double dx = -gx / grade, dz = -gz / grade; // downhill, where the bowl opens
		if (chance > .25 + .75 * Mx(0, -dz)) continue; // mostly facing north (-Z), away from the sun
		double lipX = cx + dx * radius * .85, lipZ = cz + dz * radius * .85;
		double floor = macro.Sample(FWorldPlan::HeightChannel, lipX, lipZ);
		if (hc - floor < 20) continue;
		double fx = cx + dx * radius * .15, fz = cz + dz * radius * .15;
		FRiverHit hit;
		if (hy.NearestRiver(fx, fz, radius + 300, hit) || hy.LakeNear(fx, fz) >= 0 || hy.LakeNear(lipX, lipZ) >= 0) continue;
		bool apart = true;
		for (const FBowl& b : Bowls) if (std::sqrt((b.Fx - fx) * (b.Fx - fx) + (b.Fz - fz) * (b.Fz - fz)) < 2.5 * Mx(b.Radius, radius)) apart = false;
		if (!apart) continue;
		FBowl b;
		b.Fx = fx; b.Fz = fz; b.Dx = dx; b.Dz = dz; b.Radius = radius; b.Floor = floor; b.Depth = depth;
		b.Back = rng.Between(1.3, 1.9); b.Front = rng.Between(.25, .45); b.Seed = rng.Next();
		Bowls.push_back(b);
	}
	MinX = macro.MinX; MinZ = macro.MinZ;
	GridSize = (int)std::ceil(macro.Size * macro.Cell / CirqueGridCell);
	std::vector<std::vector<int>> cells((size_t)GridSize * GridSize);
	for (int n = 0; n < (int)Bowls.size(); n++)
	{
		const FBowl& b = Bowls[n];
		for (int j = Cell(b.Fz - b.Radius * 1.2, MinZ); j <= Cell(b.Fz + b.Radius * 1.2, MinZ); j++)
			for (int i = Cell(b.Fx - b.Radius * 1.2, MinX); i <= Cell(b.Fx + b.Radius * 1.2, MinX); i++)
				cells[(size_t)j * GridSize + i].push_back(n);
	}
	FlattenGrid(cells, GridStart, GridItems);
}

double FCirques::Carve(double x, double z, double ground, double detail) const
{
	if (Bowls.empty()) return 0;
	int c = Cell(z, MinZ) * GridSize + Cell(x, MinX);
	double carved = ground;
	for (int n = GridStart[c]; n < GridStart[c + 1]; n++)
	{
		const FBowl& b = Bowls[GridItems[n]];
		double s = CirqueSurface(b, x, z, detail);
		if (s < carved + 6) carved = CirqueSmoothMin(carved, s, 6);
		double uphill;
		double r = CirqueReach(b, x, z, uphill);
		if (r >= .55 && r < .68)
		{
			double sill = b.Floor + .5, keep = DetMath::Smooth((r - .62) / .06);
			if (carved < sill) carved = sill + (carved - sill) * keep;
		}
	}
	return carved - ground;
}

bool FCirques::Tarn(double x, double z, double ground, double& level) const
{
	level = NaNValue();
	if (Bowls.empty()) return false;
	int c = Cell(z, MinZ) * GridSize + Cell(x, MinX);
	for (int n = GridStart[c]; n < GridStart[c + 1]; n++)
	{
		const FBowl& b = Bowls[GridItems[n]];
		double uphill;
		if (CirqueReach(b, x, z, uphill) >= .55) continue;
		double l = b.Floor - TarnBelow;
		if (ground < l) { level = Sea + l; return true; }
	}
	return false;
}
}
