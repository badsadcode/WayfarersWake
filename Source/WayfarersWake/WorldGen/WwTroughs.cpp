// Port of Thornwake Game/WorldGen/Troughs.cs: glacial troughs with hanging valleys (revision 24, 25), fjords
// (revision 26) and fjord coasts with skerries (revision 27); talus at the walls' feet (revision 33).
#include "WwLandforms.h"
#include <unordered_set>

namespace ww
{
namespace
{
const double TroughGridCell = 2000, TroughSpacing = 50, FloorAbove = 1.5;

/// A set of reach ids that also remembers insertion order (the source enumerates a HashSet, which keeps it).
struct FChainSet
{
	std::vector<int> Order; std::unordered_set<int> Set;
	void Add(int id) { if (Set.insert(id).second) Order.push_back(id); }
	bool Contains(int id) const { return Set.count(id) != 0; }
};

double TroughSmoothMin(double a, double b, double k)
{
	double h = DetMath::Clamp(.5 + .5 * (b - a) / k, 0, 1);
	return b + (a - b) * h - k * h * (1 - h);
}

// .NET's Array.BinarySearch on doubles: index of a match, or the complement of the insertion point.
int BinarySearch(const std::vector<double>& a, double value)
{
	int lo = 0, hi = (int)a.size() - 1;
	while (lo <= hi)
	{
		int i = lo + ((hi - lo) >> 1);
		if (a[i] == value) return i;
		if (a[i] < value) lo = i + 1; else hi = i - 1;
	}
	return ~lo;
}
}

struct FTroughs::FTrough
{
	std::vector<double> X, Z, Along, Floor;
	double HalfFloor = 0, Slope = 0, Reach = 0;
	u64 Seed = 0;
	double MinX = 0, MinZ = 0, MaxX = 0, MaxZ = 0;
	bool bFjord = false;
	double Lift = 0, Band = 0, Fade = 0;

	void Bound()
	{
		MinX = MaxX = X[0]; MinZ = MaxZ = Z[0];
		for (size_t i = 1; i < X.size(); i++)
		{
			MinX = Mn(MinX, X[i]); MaxX = Mx(MaxX, X[i]);
			MinZ = Mn(MinZ, Z[i]); MaxZ = Mx(MaxZ, Z[i]);
		}
	}
	double SegmentDistance(int i, double x, double z, double& u) const
	{
		double ax = X[i], az = Z[i], dx = X[i + 1] - ax, dz = Z[i + 1] - az, len2 = Mx(1e-9, dx * dx + dz * dz);
		u = DetMath::Clamp(((x - ax) * dx + (z - az) * dz) / len2, 0, 1);
		return std::sqrt(Sq(ax + dx * u - x) + Sq(az + dz * u - z));
	}
	/// Distance to the line and the position along it, blended over the segments within 60 m of the nearest, or 80 %
	/// of the distance further out.
	void Nearest(double x, double z, double& dist, double& t) const
	{
		double best = std::numeric_limits<double>::max(), u;
		int segments = (int)X.size() - 1;
		for (int i = 0; i < segments; i++) best = Mn(best, SegmentDistance(i, x, z, u));
		double ws = 0, ts = 0, blend = Mx(60, best * .8);
		for (int i = 0; i < segments; i++)
		{
			double d = SegmentDistance(i, x, z, u);
			double w = DetMath::Smooth(1 - (d - best) / blend);
			if (w <= 0) continue;
			ws += w; ts += w * (Along[i] + u * (Along[i + 1] - Along[i]));
		}
		dist = best; t = ts / ws;
	}
	/// The trough's ground here (metres above the sea): floor, then a wall steepening over 120 m to its slope (a U).
	double Surface(double x, double z, double& floor, double& wall, double& along) const
	{
		double dist;
		Nearest(x, z, dist, along);
		int f = BinarySearch(Along, along);
		int i = ClampI(f >= 0 ? f : ~f - 1, 0, (int)Along.size() - 2);
		double u = DetMath::Clamp((along - Along[i]) / Mx(1e-6, Along[i + 1] - Along[i]), 0, 1);
		floor = Floor[i] + (Floor[i + 1] - Floor[i]) * u;
		double toEnd = std::sqrt(Sq(x - X.back()) + Sq(z - Z.back()));
		double half = HalfFloor * (1 + .2 * Noise::Fbm(x, z, 1500, 2, 0, Seed)) * (bFjord ? 1 : DetMath::Smooth(toEnd / 1500));
		wall = dist - half + 25 * Noise::Fbm(x, z, 180, 2, 0, Seed + 1);
		const double Foot = 120; // (a U: the wall steepens upward from the floor)
		double rise = wall <= 0 ? 0 : wall < Foot ? Slope * wall * wall / (2 * Foot) : Slope * (wall - Foot * .5);
		return floor + (bFjord ? FloorAbove * DetMath::Smooth(floor / 3) : FloorAbove) + rise;
	}
};

struct FTroughs::FBuild
{
	FWorldPlan& Plan;
	struct FBounds { double MinX, MinZ, MaxX, MaxZ; };
	std::vector<FBounds> Bounds;
	struct FCirque { double X, Z, Radius; };
	std::vector<FCirque> Cirques;
	std::vector<FPoint2> Canyons;
};

FTroughs::~FTroughs() = default;

int FTroughs::Count() const { int n = 0; for (const auto& c : Troughs) if (!c->bFjord) n++; return n; }
int FTroughs::Cell(double v, double min) const { return ClampI((int)std::floor((v - min) / TroughGridCell), 0, GridSize - 1); }

// The trough's line is the river's course averaged over +-400 m (a glacier cut its valley straighter than the river).
static void SmoothCourse(const std::vector<double>& xs, const std::vector<double>& zs, std::vector<double>& mx, std::vector<double>& mz,
	std::vector<double>& along, double& halfFloor)
{
	int n = (int)xs.size();
	mx.assign(n, 0.0); mz.assign(n, 0.0);
	double bends = 0;
	for (int i = 0; i < n; i++)
	{
		int w = MnI(8, MnI(i, n - 1 - i));
		for (int j = i - w; j <= i + w; j++) { mx[i] += xs[j]; mz[i] += zs[j]; }
		mx[i] /= 2 * w + 1; mz[i] /= 2 * w + 1;
		bends = Mx(bends, std::sqrt(Sq(mx[i] - xs[i]) + Sq(mz[i] - zs[i])));
	}
	halfFloor = Mx(halfFloor, bends + 80);
	along.assign(n, 0.0);
	for (int i = 1; i < n; i++) along[i] = along[i - 1] + std::sqrt(Sq(mx[i] - mx[i - 1]) + Sq(mz[i] - mz[i - 1]));
}

FTroughs::FTroughs(FWorldPlan& Plan, FLandformSetting Setting, bool bFjords, FLandformSetting FjordSetting)
{
	FMacroRaster& macro = Plan.Macro; FHydrology& hy = *Plan.Hydrology; const FWorldProfile& p = Plan.Profile();
	double sea = p.SeaLevel;
	Revision = Plan.Style.Revision;
	FRandomStream rng(LandformSeed(Plan.Manifest, "troughs"));
	int mountainCells = 0;
	for (float v : macro.Channel(FWorldPlan::MountainChannel)) if (v > .25f) mountainCells++;
	double km2 = mountainCells * macro.Cell * macro.Cell / 1e6;
	int want = ClampI((int)std::nearbyint(Setting.Amount * km2 / (25 * 25)), 0, 60);
	std::vector<int> candidates;
	std::vector<FRiverReach>& reaches = hy.Reaches;
	for (const FRiverReach& r : reaches)
	{
		if (r.bDistributary || r.Points.size() < 8 || r.Catchment[r.Catchment.size() / 2] < 20) continue;
		const FPoint2& mid = r.Points[r.Points.size() / 2];
		if (macro.Sample(FWorldPlan::MountainChannel, mid.X, mid.Z) > .1) candidates.push_back(r.Id);
	}
	FBuild build{ Plan };
	build.Bounds.resize(reaches.size());
	for (size_t i = 0; i < reaches.size(); i++)
	{
		const std::vector<FPoint2>& pts = reaches[i].Points;
		double x0 = pts[0].X, x1 = x0, z0 = pts[0].Z, z1 = z0;
		for (const FPoint2& q : pts) { x0 = Mn(x0, q.X); x1 = Mx(x1, q.X); z0 = Mn(z0, q.Z); z1 = Mx(z1, q.Z); }
		build.Bounds[i] = { x0, z0, x1, z1 };
	}
	if (Plan.Cirques) Plan.Cirques->Probes([&](double x, double z, double radius) { build.Cirques.push_back({ x, z, radius }); });
	if (Plan.Canyons) Plan.Canyons->Probes([&](double x, double z) { build.Canyons.push_back({ x, z }); });

	for (int attempt = 0; attempt < want * 30 && (int)Troughs.size() < want && !candidates.empty(); attempt++)
	{
		const FRiverReach* start = &reaches[candidates[rng.Int((int)candidates.size())]];
		int k = rng.Int((int)start->Points.size() - 1);
		double length = rng.Between(3000, 8000) * Setting.Size, halfFloor = rng.Between(150, 350) * Setting.Size, slope = rng.Between(1.3, 2.0);
		u64 seed = rng.Next();
		// The stretch, following the river downstream across junctions, resampled every 50 m.
		std::vector<double> xs, zs, ss;
		FChainSet chain;
		const FRiverReach* r = start; double run = 0, next = 0; bool ok = true;
		for (;;)
		{
			chain.Add(r->Id);
			for (; k < (int)r->Points.size(); k++)
			{
				if (!xs.empty()) run += std::sqrt(Sq(r->Points[k].X - r->Points[k - 1].X) + Sq(r->Points[k].Z - r->Points[k - 1].Z));
				if (run >= next) { xs.push_back(r->Points[k].X); zs.push_back(r->Points[k].Z); ss.push_back(r->Surface[k] - sea); next = run + TroughSpacing; }
				if (run >= length) break;
			}
			if (run >= length) break;
			if (r->EndLake >= 0 || r->bEndsInSea || r->DownstreamReach < 0) { ok = false; break; }
			r = &reaches[r->DownstreamReach]; k = 1;
		}
		if (!ok || xs.size() < 40) continue;
		double drop = ss[0] - ss.back();
		if (drop < 40 || drop / length > .08) continue;
		// In the mountains, away from lakes, other troughs, canyons and cirques.
		int n = (int)xs.size();
		double reach = halfFloor * 1.25 + 40;
		for (int i = 0; i < n && ok; i += 4)
		{
			if (hy.LakeNear(xs[i], zs[i]) >= 0) ok = false;
			for (const auto& other : Troughs)
				if (std::fabs(other->X[other->X.size() / 2] - xs[i]) + std::fabs(other->Z[other->Z.size() / 2] - zs[i]) < 15000) ok = false;
			for (const FPoint2& c : build.Canyons) if (std::fabs(c.X - xs[i]) + std::fabs(c.Z - zs[i]) < 15000) ok = false;
			for (const auto& q : build.Cirques) if (std::sqrt(Sq(q.X - xs[i]) + Sq(q.Z - zs[i])) < q.Radius * 1.3 + reach + 400) ok = false;
		}
		if (!ok || macro.Sample(FWorldPlan::MountainChannel, xs[n / 2], zs[n / 2]) < .15) continue;
		std::vector<double> mx, mz, along;
		SmoothCourse(xs, zs, mx, mz, along, halfFloor);
		// The floor: the water falls in at the head (85 % of the stretch's drop, 160 m at most) and runs on at an even
		// grade to its old level at the end; never above the old river.
		double end = ss.back(), deepest = Mn(.85 * drop, 160 * Setting.Size);
		std::vector<double> floor(n);
		for (int i = 0; i < n; i++) floor[i] = Mn(ss[i], ss[0] - deepest + (end - ss[0] + deepest) * along[i] / along.back());
		// Walls worth the name: the land 300 m beyond the floor's edge stands 50 m or more over the floor on both sides.
		double left = 0, right = 0;
		for (int q = 1; q <= 3; q++)
		{
			int i = q * n / 4, i0 = MxI(0, i - 2), i1 = MnI(n - 1, i + 2);
			double tx = mx[i1] - mx[i0], tz = mz[i1] - mz[i0], tl = Mx(1e-6, std::sqrt(tx * tx + tz * tz)), d = halfFloor + 300;
			left += macro.Sample(FWorldPlan::HeightChannel, mx[i] - tz / tl * d, mz[i] + tx / tl * d) - floor[i];
			right += macro.Sample(FWorldPlan::HeightChannel, mx[i] + tz / tl * d, mz[i] - tx / tl * d) - floor[i];
		}
		if (left < 3 * 50 || right < 3 * 50) continue;
		auto c = std::make_unique<FTrough>();
		c->X = mx; c->Z = mz; c->Along = along; c->Floor = floor; c->HalfFloor = halfFloor; c->Slope = slope; c->Seed = seed;
		c->Reach = halfFloor * 1.25 + 40 + 2 * (deepest + 150) / slope;
		c->Bound();
		if (Lower(build, *c, chain.Order, sea, Setting)) Troughs.push_back(std::move(c));
	}
	if (bFjords && FjordSetting.Amount > 0)
	{
		if (Revision >= 27) PlaceFjordCoasts(build, FjordSetting);
		else PlaceFjords(build, FjordSetting);
	}
	// Troughs near each 2 km cell.
	MinX = macro.MinX; MinZ = macro.MinZ;
	GridSize = (int)std::ceil(macro.Size * macro.Cell / TroughGridCell);
	std::vector<std::vector<int>> cells((size_t)GridSize * GridSize);
	for (int i = 0; i < (int)Troughs.size(); i++)
	{
		const FTrough& c = *Troughs[i];
		for (int j = Cell(c.MinZ - c.Reach, MinZ); j <= Cell(c.MaxZ + c.Reach, MinZ); j++)
			for (int a = Cell(c.MinX - c.Reach, MinX); a <= Cell(c.MaxX + c.Reach, MinX); a++)
				cells[(size_t)j * GridSize + a].push_back(i);
	}
	FlattenGrid(cells, GridStart, GridItems);
}

/// Follows a river upstream from a point along the bigger branch at each junction, sampling every 50 m, until
/// `length` or until the river stands `maxAbove` over the sea. Lists come back head first.
static double UpRiver(const std::vector<FRiverReach>& reaches, const FRiverReach* from, int point, double length, double maxAbove, double sea,
	std::vector<double>& xs, std::vector<double>& zs, std::vector<double>& ss, FChainSet& chain)
{
	const FRiverReach* r = from; int k = point; double run = 0, next = 0;
	for (;;)
	{
		chain.Add(r->Id);
		for (; k >= 0 && run < length && (ss.empty() || ss.back() <= maxAbove); k--)
		{
			if (!xs.empty() && k + 1 < (int)r->Points.size()) run += std::sqrt(Sq(r->Points[k].X - r->Points[k + 1].X) + Sq(r->Points[k].Z - r->Points[k + 1].Z));
			if (run >= next) { xs.push_back(r->Points[k].X); zs.push_back(r->Points[k].Z); ss.push_back(r->Surface[k] - sea); next = run + TroughSpacing; }
		}
		if (run >= length || r->UpstreamReaches.empty() || r->StartLake >= 0 || ss.back() > maxAbove) break;
		int up = r->UpstreamReaches[0];
		for (int u : r->UpstreamReaches) if (reaches[u].Catchment.back() > reaches[up].Catchment.back()) up = u;
		r = &reaches[up]; k = (int)r->Points.size() - 2;
	}
	std::reverse(xs.begin(), xs.end()); std::reverse(zs.begin(), zs.end()); std::reverse(ss.begin(), ss.end());
	return run;
}

// The line runs on 1.5 km out to sea, so the walls do not close around the mouth.
static void ExtendToSea(std::vector<double>& mx, std::vector<double>& mz, std::vector<double>& along, std::vector<double>& floor)
{
	int n = (int)mx.size();
	double lastX = mx[n - 1], lastZ = mz[n - 1], lastAlong = along[n - 1], lastFloor = floor[n - 1];
	double ex = lastX - mx[n - 5], ez = lastZ - mz[n - 5], el = Mx(1e-6, std::sqrt(ex * ex + ez * ez));
	for (int e = 1; e <= 30; e++)
	{
		mx.push_back(lastX + ex / el * e * TroughSpacing); mz.push_back(lastZ + ez / el * e * TroughSpacing);
		along.push_back(lastAlong + e * TroughSpacing); floor.push_back(lastFloor);
	}
}

void FTroughs::PlaceFjordCoasts(FBuild& build, FLandformSetting setting)
{
	FWorldPlan& Plan = build.Plan;
	FMacroRaster& macro = Plan.Macro; FHydrology& hy = *Plan.Hydrology; std::vector<FRiverReach>& reaches = hy.Reaches;
	FRandomStream rng(LandformSeed(Plan.Manifest, "fjords"));
	std::vector<float>& land = macro.Channel(FWorldPlan::LandChannel);
	int size = macro.Size;
	auto IsSea = [&](int i, int j) { return i < 0 || j < 0 || i >= size || j >= size || land[j * size + i] <= 0; };
	std::vector<int> coast;
	int landCells = 0;
	for (int j = 0; j < size; j++)
		for (int i = 0; i < size; i++)
		{
			if (IsSea(i, j)) continue;
			landCells++;
			if (IsSea(i - 1, j) || IsSea(i + 1, j) || IsSea(i, j - 1) || IsSea(i, j + 1)) coast.push_back(j * size + i);
		}
	if (coast.empty()) return;
	// The coldest coast: the northern 30 % of the coastal cells (north is -Z; the list is row-major already).
	int coldCount = MxI(1, (int)coast.size() * 3 / 10);
	std::vector<const FRiverReach*> mouths;
	for (const FRiverReach& r : reaches) if (!r.bDistributary && r.bEndsInSea && r.Catchment.back() >= 20) mouths.push_back(&r);
	double landKm2 = landCells * macro.Cell * macro.Cell / 1e6;
	int want = ClampI((int)std::nearbyint(setting.Amount * landKm2 / (350 * 350)), 1, 6);
	for (int attempt = 0; attempt < want * 40 && (int)Coasts.size() < want; attempt++)
	{
		int k = coast[rng.Int(coldCount)];
		double cx = macro.CellX(k % size), cz = macro.CellZ(k / size);
		double radius = rng.Between(25000, 45000) * setting.Size, lift = rng.Between(500, 900) * setting.Size;
		u64 seed = rng.Next();
		std::vector<const FRiverReach*> here;
		for (const FRiverReach* r : mouths) if (std::sqrt(Sq(r->Points.back().X - cx) + Sq(r->Points.back().Z - cz)) < radius * .7) here.push_back(r);
		if (here.size() < 3) continue;
		bool apart = true;
		for (const FCoast& o : Coasts) if (std::sqrt(Sq(o.X - cx) + Sq(o.Z - cz)) < radius + o.Radius) apart = false;
		if (!apart) continue;
		Coasts.push_back({ cx, cz, radius, seed });
		RaiseCoast(Plan, cx, cz, radius, lift, seed);
		// Fjords: every mouth, biggest first, 3.5 km apart.
		std::stable_sort(here.begin(), here.end(), [](const FRiverReach* a, const FRiverReach* b) { return a->Catchment.back() > b->Catchment.back(); });
		std::vector<FPoint2> placed;
		for (const FRiverReach* mouth : here)
		{
			double mx0 = mouth->Points.back().X, mz0 = mouth->Points.back().Z;
			bool spaced = true;
			for (const FPoint2& q : placed) if (std::sqrt(Sq(q.X - mx0) + Sq(q.Z - mz0)) < 3500) spaced = false;
			if (!spaced) continue;
			double length = rng.Between(8000, 22000) * setting.Size, halfFloor = rng.Between(250, 450) * setting.Size;
			double slope = rng.Between(1.8, 2.6), depth = rng.Between(100, 300) * setting.Size;
			u64 fseed = rng.Next();
			// (shorter and narrower if a river would rise just above a fall into it)
			const FTrough* c = nullptr; std::vector<int> mainChain;
			for (double scale : { 1.0, .6, .35 })
			{
				c = FjordLine(build, mouth, (int)mouth->Points.size() - 1, Mx(4000 * setting.Size, length * scale), halfFloor * (.6 + .4 * scale), slope, depth,
					depth * .25, fseed, setting, mainChain);
				if (c) break;
			}
			if (!c) continue;
			placed.push_back({ mx0, mz0 });
			// Side arms: tributaries joining the fjord where it is 30 m or more deep.
			int arms = 0;
			for (int id : mainChain)
			{
				const FRiverReach& r = reaches[id];
				if (arms >= 2 || r.UpstreamReaches.size() < 2) continue;
				double fj, wall, alongAt;
				c->Surface(r.Points[0].X, r.Points[0].Z, fj, wall, alongAt);
				if (fj > -30) continue;
				for (int u : r.UpstreamReaches)
				{
					if (std::find(mainChain.begin(), mainChain.end(), u) != mainChain.end() || reaches[u].Catchment.back() < 8 || arms >= 2) continue;
					double armLength = rng.Between(3000, 9000) * setting.Size, armHalf = rng.Between(180, 300) * setting.Size;
					double armSlope = rng.Between(1.8, 2.6);
					u64 aseed = rng.Next();
					std::vector<int> armChain;
					if (FjordLine(build, &reaches[u], (int)reaches[u].Points.size() - 1, armLength, armHalf, armSlope, -fj, -fj, aseed, setting, armChain))
						arms++;
				}
			}
		}
	}
}

const FTroughs::FTrough* FTroughs::FjordLine(FBuild& build, const FRiverReach* from, int point, double length, double halfFloor, double slope, double depth,
	double endDepth, u64 seed, FLandformSetting setting, std::vector<int>& chainOut)
{
	FHydrology& hy = *build.Plan.Hydrology;
	double sea = build.Plan.Profile().SeaLevel;
	std::vector<double> xs, zs, ss;
	FChainSet chain;
	double run = UpRiver(hy.Reaches, from, point, length, 300 * setting.Size, sea, xs, zs, ss, chain);
	chainOut = chain.Order;
	if (run < 3000 * setting.Size || xs.size() < 50) return nullptr;
	int n = (int)xs.size();
	for (int i = 0; i < n; i += 4)
	{
		if (hy.LakeNear(xs[i], zs[i]) >= 0) return nullptr;
		for (const auto& q : build.Cirques) if (std::sqrt(Sq(q.X - xs[i]) + Sq(q.Z - zs[i])) < q.Radius * 1.3 + halfFloor * 1.25 + 400) return nullptr;
		for (const auto& other : Troughs)
			if (!other->bFjord)
				for (size_t j = 0; j < other->X.size(); j += 10)
					if (std::fabs(other->X[j] - xs[i]) + std::fabs(other->Z[j] - zs[i]) < 4000) return nullptr;
	}
	std::vector<double> mx, mz, along;
	SmoothCourse(xs, zs, mx, mz, along, halfFloor);
	double head = Mx(Mn(30, ss[0] * .5), ss[0] - 150 * setting.Size), shore = DetMath::Clamp(head / (.035 * along.back()), .1, .5);
	if (head / (shore * along.back()) > .06) return nullptr;
	std::vector<double> floor(n);
	for (int i = 0; i < n; i++)
	{
		double u = along[i] / along.back();
		double f = u < shore ? head * (1 - u / shore)
			: -(depth + (endDepth - depth) * DetMath::Smooth((u - .8) / .2)) * DetMath::Smooth((u - shore) / Mx(.15, (1 - shore) * .4));
		floor[i] = Mn(ss[i], f);
	}
	bool mouth = from->bEndsInSea && point == (int)from->Points.size() - 1;
	if (mouth) ExtendToSea(mx, mz, along, floor);
	auto c = std::make_unique<FTrough>();
	c->X = mx; c->Z = mz; c->Along = along; c->Floor = floor; c->HalfFloor = halfFloor; c->Slope = slope; c->Seed = seed; c->bFjord = true;
	c->Reach = halfFloor * 1.25 + 40 + 2 * (depth + 1500 * setting.Size) / slope;
	c->Bound();
	if (!Lower(build, *c, chain.Order, sea, setting)) return nullptr;
	Troughs.push_back(std::move(c));
	return Troughs.back().get();
}

/// The coastal range of a fjord coast (revision 27), raised in the plan's height raster (and its mountain share).
void FTroughs::RaiseCoast(FWorldPlan& Plan, double cx, double cz, double radius, double lift, u64 seed)
{
	FMacroRaster& macro = Plan.Macro; const FHydrology& hy = *Plan.Hydrology;
	std::vector<float>& land = macro.Channel(FWorldPlan::LandChannel);
	std::vector<float>& height = macro.Channel(FWorldPlan::HeightChannel);
	std::vector<float>& mountain = macro.Channel(FWorldPlan::MountainChannel);
	int size = macro.Size, reach = (int)std::ceil(radius / macro.Cell) + 2;
	int ci = (int)((cx - macro.MinX) / macro.Cell), cj = (int)((cz - macro.MinZ) / macro.Cell);
	int i0 = MxI(0, ci - reach), i1 = MnI(size - 1, ci + reach), j0 = MxI(0, cj - reach), j1 = MnI(size - 1, cj + reach);
	int w = i1 - i0 + 1, h = j1 - j0 + 1;
	// Distance to the sea (cells; chamfer 1, 1.4142) within the box.
	std::vector<double> dist((size_t)w * h);
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++) dist[(size_t)j * w + i] = land[(size_t)(j + j0) * size + i + i0] <= 0 ? 0 : 1e9;
	for (int pass = 0; pass < 2; pass++)
		for (int jj = 0; jj < h; jj++)
			for (int ii = 0; ii < w; ii++)
			{
				int i = pass == 0 ? ii : w - 1 - ii, j = pass == 0 ? jj : h - 1 - jj, dir = pass == 0 ? -1 : 1;
				double d = dist[(size_t)j * w + i];
				if (i + dir >= 0 && i + dir < w) d = Mn(d, dist[(size_t)j * w + i + dir] + 1);
				if (j + dir >= 0 && j + dir < h)
				{
					d = Mn(d, dist[(size_t)(j + dir) * w + i] + 1);
					if (i + dir >= 0 && i + dir < w) d = Mn(d, dist[(size_t)(j + dir) * w + i + dir] + 1.4142);
					if (i - dir >= 0 && i - dir < w) d = Mn(d, dist[(size_t)(j + dir) * w + i - dir] + 1.4142);
				}
				dist[(size_t)j * w + i] = d;
			}
	for (int j = j0; j <= j1; j++)
		for (int i = i0; i <= i1; i++)
		{
			int k = j * size + i;
			if (land[k] <= 0) continue;
			double x = macro.CellX(i), z = macro.CellZ(j), d = std::sqrt(Sq(x - cx) + Sq(z - cz));
			double region = 1 - DetMath::Smooth((d - radius * .6) / (radius * .4));
			if (region <= 0) continue;
			double dc = Mn(dist[(size_t)(j - j0) * w + i - i0], 1e6) * macro.Cell;
			double profile = DetMath::Smooth((dc - 500) / 2500) * (1 - DetMath::Smooth((dc - 10000) / 18000));
			double cells;
			int lake = hy.LakeDistance(x, z, cells);
			if (lake >= 0) profile *= DetMath::Smooth((cells - 1.6) / 2);
			// Ridges and peaks a few kilometres apart, varying over 15 km.
			double ridge = 1 - std::fabs(Noise::Fbm(x, z, 7000, 3, 0, seed + 1));
			double vary = Mx(.15, .45 + .8 * ridge * ridge + .25 * Noise::Fbm(x, z, 15000, 2, 0, seed));
			height[k] += (float)(lift * profile * region * vary);
			float share = (float)(.65 * profile * region);
			if (share > mountain[k]) mountain[k] = share;
		}
}

/// Fjords of revision 26: the last 6-18 km of a river reaching the sea through mountains, with raised walls.
void FTroughs::PlaceFjords(FBuild& build, FLandformSetting setting)
{
	FWorldPlan& Plan = build.Plan;
	FMacroRaster& macro = Plan.Macro; FHydrology& hy = *Plan.Hydrology; std::vector<FRiverReach>& reaches = hy.Reaches;
	double sea = Plan.Profile().SeaLevel;
	FRandomStream rng(LandformSeed(Plan.Manifest, "fjords"));
	int mountainCells = 0;
	for (float v : macro.Channel(FWorldPlan::MountainChannel)) if (v > .25f) mountainCells++;
	double km2 = mountainCells * macro.Cell * macro.Cell / 1e6;
	int want = ClampI((int)std::nearbyint(setting.Amount * km2 / (60 * 60)), 0, 30), placed = 0;
	std::vector<int> mouths;
	for (const FRiverReach& r : reaches)
		if (!r.bDistributary && r.bEndsInSea && r.Catchment.back() >= 50) mouths.push_back(r.Id);
	for (int attempt = 0; attempt < want * 40 && placed < want && !mouths.empty(); attempt++)
	{
		const FRiverReach* mouth = &reaches[mouths[rng.Int((int)mouths.size())]];
		double length = rng.Between(6000, 18000) * setting.Size, halfFloor = rng.Between(250, 500) * setting.Size;
		double slope = rng.Between(1.6, 2.4), depth = rng.Between(80, 220) * setting.Size;
		u64 seed = rng.Next();
		double lift = rng.Between(300, 600) * setting.Size, band = rng.Between(600, 1400), fade = rng.Between(4000, 7000);
		std::vector<double> xs, zs, ss;
		FChainSet chain;
		double run = UpRiver(reaches, mouth, (int)mouth->Points.size() - 1, length, 400 * setting.Size, sea, xs, zs, ss, chain);
		if (run < 3500 * setting.Size || xs.size() < 60) continue;
		int n = (int)xs.size();
		if (ss[0] < 25) continue; // (the head stands above the sea: the river falls into the fjord)
		bool ok = true;
		for (int i = 0; i < n && ok; i += 4)
		{
			if (hy.LakeNear(xs[i], zs[i]) >= 0) ok = false;
			// (no lake under the raised walls either)
			int i0 = MxI(0, i - 2), i1 = MnI(n - 1, i + 2);
			double tx = xs[i1] - xs[i0], tz = zs[i1] - zs[i0], tl = Mx(1e-6, std::sqrt(tx * tx + tz * tz));
			for (double d : { -halfFloor - band - fade, -halfFloor - band - fade * .5, -halfFloor - band, halfFloor + band, halfFloor + band + fade * .5, halfFloor + band + fade })
				if (hy.LakeNear(xs[i] - tz / tl * d, zs[i] + tx / tl * d) >= 0) ok = false;
			for (const auto& other : Troughs)
				for (size_t j = 0; j < other->X.size() && ok; j += 10)
					if (std::fabs(other->X[j] - xs[i]) + std::fabs(other->Z[j] - zs[i]) < 8000) ok = false;
			for (const FPoint2& c : build.Canyons) if (std::fabs(c.X - xs[i]) + std::fabs(c.Z - zs[i]) < 15000) ok = false;
			for (const auto& q : build.Cirques) if (std::sqrt(Sq(q.X - xs[i]) + Sq(q.Z - zs[i])) < q.Radius * 1.3 + halfFloor * 1.25 + 400) ok = false;
		}
		if (!ok) continue;
		std::vector<double> mx, mz, along;
		SmoothCourse(xs, zs, mx, mz, along, halfFloor);
		double head = Mx(Mn(30, ss[0] * .5), ss[0] - 150 * setting.Size), shore = DetMath::Clamp(head / (.035 * along.back()), .15, .55);
		if (head / (shore * along.back()) > .06) continue; // (a steep ramp, not a valley floor)
		std::vector<double> floor(n);
		for (int i = 0; i < n; i++)
		{
			double u = along[i] / along.back();
			double f = u < shore ? head * (1 - u / shore)
				: -depth * DetMath::Smooth((u - shore) / .25) * (1 - .75 * DetMath::Smooth((u - .8) / .2));
			floor[i] = Mn(ss[i], f);
		}
		// Hilly ground on both sides: 400 m beyond the floor the land stands 60 m or more over the sea.
		double left = 0, right = 0;
		for (int q = 1; q <= 3; q++)
		{
			int i = q * n / 5, i0 = MxI(0, i - 2), i1 = MnI(n - 1, i + 2);
			double tx = mx[i1] - mx[i0], tz = mz[i1] - mz[i0], tl = Mx(1e-6, std::sqrt(tx * tx + tz * tz)), d = halfFloor + 400;
			left += macro.Sample(FWorldPlan::HeightChannel, mx[i] - tz / tl * d, mz[i] + tx / tl * d);
			right += macro.Sample(FWorldPlan::HeightChannel, mx[i] + tz / tl * d, mz[i] - tx / tl * d);
		}
		if (left < 3 * 60 || right < 3 * 60) continue;
		ExtendToSea(mx, mz, along, floor);
		auto c = std::make_unique<FTrough>();
		c->X = mx; c->Z = mz; c->Along = along; c->Floor = floor; c->HalfFloor = halfFloor; c->Slope = slope; c->Seed = seed; c->bFjord = true;
		c->Lift = lift; c->Band = band; c->Fade = fade;
		c->Reach = Mx(halfFloor * 1.25 + 40 + 2 * (depth + lift * 1.4 + 300) / slope, halfFloor + band * 1.6 + fade);
		c->Bound();
		if (Lower(build, *c, chain.Order, sea, setting)) { Troughs.push_back(std::move(c)); placed++; }
	}
}

/// Lowers every river the trough takes in (or leaves the plan unchanged and returns false).
bool FTroughs::Lower(FBuild& build, const FTrough& c, const std::vector<int>& chain, double sea, FLandformSetting setting)
{
	FHydrology& hy = *build.Plan.Hydrology;
	std::vector<FRiverReach>& reaches = hy.Reaches;
	std::vector<int> order; // insertion order of the lowered reaches (the source's dictionary)
	std::unordered_map<int, std::vector<double>> lowered;
	auto Get = [&](int id) -> std::vector<double>&
	{
		auto it = lowered.find(id);
		if (it != lowered.end()) return it->second;
		order.push_back(id);
		return lowered.emplace(id, reaches[id].Surface).first->second;
	};
	double f, wl, al;
	auto Inside = [&](double x, double z, double water) { double a, b, d; return c.Surface(x, z, a, b, d) < water - sea + 2; };
	for (int id = 0; id < (int)reaches.size(); id++)
	{
		const FBuild::FBounds& b = build.Bounds[id];
		if (reaches[id].bDistributary || b.MaxX < c.MinX - c.Reach || b.MinX > c.MaxX + c.Reach || b.MaxZ < c.MinZ - c.Reach || b.MinZ > c.MaxZ + c.Reach) continue;
		const FRiverReach& r = reaches[id]; bool inside = false;
		for (int k = 0; k < (int)r.Points.size(); k++)
		{
			if (!inside) inside = Inside(r.Points[k].X, r.Points[k].Z, r.Surface[k]);
			if (!inside) continue;
			double floor;
			c.Surface(r.Points[k].X, r.Points[k].Z, floor, wl, al);
			if (c.bFjord) floor = Mx(floor, 0); // (in a fjord the river meets the sea)
			if (floor + sea < r.Surface[k]) Get(id)[k] = floor + sea;
		}
	}
	if (lowered.empty()) return false;
	// Junctions meet and the water strictly descends (lowering a reach's start carries on downstream as far as it must).
	for (int pass = 0; pass < 30; pass++)
	{
		bool changed = false;
		std::vector<int> keys = order;
		for (int id : keys)
		{
			const FRiverReach& r = reaches[id];
			if (r.DownstreamReach >= 0)
			{
				std::vector<double>& down = Get(r.DownstreamReach);
				std::vector<double>& s = lowered.find(id)->second;
				if (s.back() < down[0]) { down[0] = s.back(); changed = true; }
				else if (s.back() > down[0]) { s.back() = down[0]; changed = true; }
			}
			std::vector<double>& s = lowered.find(id)->second;
			for (size_t k = 1; k < s.size(); k++)
				if (s[k] >= s[k - 1])
				{
					double v = c.bFjord && s[k - 1] <= sea + 1e-9 ? sea : s[k - 1] - 1e-4; // (water at the sea's level stays there)
					if (v != s[k]) { s[k] = v; changed = true; }
				}
		}
		if (!changed) break;
		if (pass == 29) return false;
	}
	for (int id : order)
	{
		const std::vector<double>& s = lowered.find(id)->second;
		const FRiverReach& r = reaches[id];
		if (r.StartLake >= 0 || r.EndLake >= 0) return false;
		for (size_t k = 0; k < s.size(); k++)
			if (r.Surface[k] - s[k] > 3 && !Inside(r.Points[k].X, r.Points[k].Z, r.Surface[k])) return false;
		// Revision 25: a river that rises here (a source) runs 800 m or more before it falls in.
		if (Revision >= 25 && r.UpstreamReaches.empty())
			for (size_t k = 0; k < s.size(); k++)
				if (r.Surface[k] - s[k] > .5) { if (r.Along[k] < 800) return false; break; }
		// (and no water lowered to within a metre of the sea)
		if (Revision >= 25 && !c.bFjord)
			for (size_t k = 0; k < s.size(); k++)
				if (s[k] < r.Surface[k] && s[k] < sea + 1) return false;
	}
	// Accepted: the plan's water takes the new levels; falls into the trough.
	struct FNewFall { int Reach, Point; double Height, To; };
	std::vector<FNewFall> falls;
	for (int id : order)
	{
		const std::vector<double>& s = lowered.find(id)->second;
		FRiverReach& r = reaches[id];
		int lip = -1;
		for (size_t k = 1; k < s.size(); k++) if (r.Surface[k] - s[k] > 4) { lip = (int)k - 1; break; }
		if (lip >= 0 && s[lip] - s[lip + 1] >= 8)
		{
			// The notch down the wall: from the lip to where the river reaches the floor.
			int foot = lip + 1;
			while (foot < (int)s.size() - 1)
			{
				c.Surface(r.Points[foot].X, r.Points[foot].Z, f, wl, al);
				if (wl <= 0) break;
				foot++;
			}
			float to = r.Along[foot] + 30; // (single precision, as the source)
			falls.push_back({ id, lip, s[lip] - s[lip + 1], (double)to });
		}
		r.Surface = s;
		hy.DropFalls(id, [&s](int k) { return k + 1 < (int)s.size() && s[k] - s[k + 1] < 4; }); // (a waterfall the trough's floor has taken in)
	}
	for (const FNewFall& fall : falls)
	{
		FRiverReach& r = reaches[fall.Reach];
		for (int j = fall.Point + 1; j < (int)r.Points.size() && r.Along[j] - r.Along[fall.Point] < 40; j++)
		{
			r.Depth[j] = (float)(r.Depth[j] + Mn(10, .25 * fall.Height) * setting.Size);
			r.Width[j] = (float)(r.Width[j] * (1 + .5 * setting.Size));
		}
		hy.AddGorge(fall.Reach, r.Along[fall.Point], Mn(fall.To, r.Length()));
		Falls.push_back({ r.Points[fall.Point].X, r.Points[fall.Point].Z, fall.Height, std::find(chain.begin(), chain.end(), fall.Reach) == chain.end() });
	}
	return true;
}

/// Islands and skerries off a fjord coast (revision 27): how much the sea floor rises here (m).
double FTroughs::Skerries(double x, double z, double land) const
{
	double best = 0;
	for (const FCoast& coast : Coasts)
	{
		double d = std::sqrt(Sq(x - coast.X) + Sq(z - coast.Z));
		double region = 1 - DetMath::Smooth((d - coast.Radius * .7) / (coast.Radius * .5));
		if (region <= 0) continue;
		double nearCoast = 1 - DetMath::Smooth((-land - .04) / .14);
		if (nearCoast <= 0) continue;
		double cluster = DetMath::Smooth((Noise::Fbm(x, z, 6000, 2, 0, coast.Seed + 5) + .15) / .4);
		double rock = Noise::Fbm(x, z, 700, 5, 0, coast.Seed + 6);
		best = Mx(best, 200 * Mx(0, rock - .2) * region * nearCoast * cluster);
	}
	if (best <= 0) return 0;
	// None in a fjord's channel out to sea (islands there closed the mouths off).
	int cell = Cell(z, MinZ) * GridSize + Cell(x, MinX);
	for (int n = GridStart[cell]; n < GridStart[cell + 1]; n++)
	{
		const FTrough& c = *Troughs[GridItems[n]];
		if (!c.bFjord || x < c.MinX - c.Reach || x > c.MaxX + c.Reach || z < c.MinZ - c.Reach || z > c.MaxZ + c.Reach) continue;
		double dist, t;
		c.Nearest(x, z, dist, t);
		best *= DetMath::Smooth((dist - c.HalfFloor * 1.3) / 400);
	}
	// Rocky, not smooth domes: knobs and clefts a few tens of metres across.
	return best <= 0 ? 0 : best + 8 * Noise::Fbm(x, z, 90, 3, 0, 4271) * DetMath::Smooth(best / 12);
}

/// A fjord's mountain walls (revision 26): how much the land is raised here (m).
double FTroughs::Raise(double x, double z, double coastDistance, double& share) const
{
	share = 0;
	if (Troughs.empty()) return 0;
	int cell = Cell(z, MinZ) * GridSize + Cell(x, MinX);
	double best = 0;
	for (int n = GridStart[cell]; n < GridStart[cell + 1]; n++)
	{
		const FTrough& c = *Troughs[GridItems[n]];
		if (!c.bFjord || x < c.MinX - c.Reach || x > c.MaxX + c.Reach || z < c.MinZ - c.Reach || z > c.MaxZ + c.Reach) continue;
		double dist, t;
		c.Nearest(x, z, dist, t);
		double across = 1 - DetMath::Smooth((dist - c.HalfFloor - c.Band * (1 + .6 * Noise::Fbm(x, z, 4000, 2, 0, c.Seed + 3))) / c.Fade);
		if (across <= 0) continue;
		double s = across * DetMath::Smooth((coastDistance - 100) / 700);
		share = Mx(share, s);
		best = Mx(best, c.Lift * (1 + .35 * Noise::Fbm(x, z, 3000, 3, 0, c.Seed + 2)) * s);
	}
	return best;
}

/// The ground taken down into the troughs here (metres above the sea in and out).
double FTroughs::Carve(double x, double z, double ground, double& flat, double& rock, bool& sea, double& scree) const
{
	flat = 0; rock = 0; sea = false; scree = 0;
	if (Troughs.empty()) return ground;
	int cell = Cell(z, MinZ) * GridSize + Cell(x, MinX);
	double carved = ground;
	for (int n = GridStart[cell]; n < GridStart[cell + 1]; n++)
	{
		const FTrough& c = *Troughs[GridItems[n]];
		if (x < c.MinX - c.Reach || x > c.MaxX + c.Reach || z < c.MinZ - c.Reach || z > c.MaxZ + c.Reach) continue;
		double floor, wall, along;
		double t = c.Surface(x, z, floor, wall, along);
		double k = 8 + 22 * DetMath::Smooth(wall / 150); // (a rounded shoulder at the wall's top; a tight blend on the floor)
		if (t >= carved + k) continue;
		double before = carved;
		carved = TroughSmoothMin(carved, t, k);
		double cut = DetMath::Smooth((before - carved) / 15);
		flat = Mx(flat, cut * (1 - DetMath::Smooth((wall + 20) / 50)));
		rock = Mx(rock, .85 * cut * DetMath::Smooth((wall + 10) / 30));
		// Revision 33: talus at the foot of the walls, and fans of it spread out over the floor.
		if (Revision >= 33 && cut > 0 && wall > -90 && wall < 290)
		{
			double fan = DetMath::Smooth(.5 + 1.7 * Noise::Fbm(x, z, 260, 2, 0, c.Seed + 9));
			double toe = 25 + 45 * fan; // (metres out over the floor)
			double top = 30 * fan * fan, ease = 60, peak = 2 * top / (toe + ease);
			double add = wall <= -toe ? 0 : wall <= 0 ? peak * Sq(wall + toe) / (2 * toe)
				: wall <= ease ? peak * toe / 2 + peak * (wall - wall * wall / (2 * ease)) : top;
			add *= 1 - DetMath::Smooth((wall - 70) / 220);
			// (never above 70 % of what the trough cut here)
			carved += Mn(cut * add, .7 * Mx(0, before - carved));
			scree = Mx(scree, cut * DetMath::Smooth((wall + toe * fan + 15) / 25) * (1 - DetMath::Smooth((wall - 20 - 40 * fan) / 40)) * (.3 + .7 * fan));
		}
		if (c.bFjord && carved < 3) sea = true;
	}
	return carved;
}
}
