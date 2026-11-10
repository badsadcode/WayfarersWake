// Port of Thornwake Game/WorldGen: Oxbows.cs (revision 39), Terraces.cs (40), Fans.cs (41), FallLips.cs (42-44).
#include "WwLandforms.h"

namespace ww
{
namespace
{
const double RsTau = 6.283185307179586, RsPi = 3.141592653589793;
}

// ------------------------------------------------------------------------------------------------ oxbow lakes
int FOxbows::Cell(double v, double min) const { return ClampI((int)std::floor((v - min) / 4000.0), 0, GridSize - 1); }

FOxbows::FOxbows(const FWorldPlan& Plan, FLandformSetting Setting)
{
	const FMacroRaster& macro = Plan.Macro; const FHydrology& hy = *Plan.Hydrology;
	Sea = Plan.Profile().SeaLevel;
	FRandomStream rng(LandformSeed(Plan.Manifest, "oxbows"));
	// Gentle lowland reaches of some size: the rivers that meander across floodplains.
	std::vector<const FRiverReach*> reaches;
	double total = 0;
	for (const FRiverReach& r : hy.Reaches)
	{
		if (r.bDistributary || r.Points.size() < 12 || r.Length() < 3000) continue;
		float w = r.Width[r.Width.size() / 2];
		if (w < 10 || w > 140) continue;
		reaches.push_back(&r); total += r.Length();
	}
	int want = ClampI((int)std::nearbyint(Setting.Amount * total / 9000), 0, 400);
	for (int attempt = 0; attempt < want * 20 && (int)Bows.size() < want && !reaches.empty(); attempt++)
	{
		const FRiverReach& r = *reaches[rng.Int((int)reaches.size())];
		int n = (int)r.Points.size(), i = 3 + rng.Int(n - 6);
		double side = rng.Unit() < .5 ? -1 : 1, radiusShare = rng.Between(2.4, 4.2), gap = rng.Between(60, 160), open = rng.Between(.65, 1.05);
		double widthShare = rng.Between(.8, 1.15), depth = rng.Between(1.5, 3.2);
		u64 seed = rng.Next();
		// Gentle: under 1.5 m a km here.
		double run = r.Along[i + 3] - r.Along[i - 3];
		if (run <= 0 || (r.Surface[i - 3] - r.Surface[i + 3]) / run > .0015) continue;
		double width = r.Width[i];
		double fx = r.Points[i + 1].X - r.Points[i - 1].X, fz = r.Points[i + 1].Z - r.Points[i - 1].Z, fl = std::sqrt(fx * fx + fz * fz);
		if (fl < 1e-6) continue;
		double nx = -fz / fl * side, nz = fx / fl * side; // out from the river, the side the loop lies on
		double radius = DetMath::Clamp(width * radiusShare, 70, 420) * Setting.Size, w = Mx(14, width * widthShare);
		double cx = r.Points[i].X + nx * (width * .5 + gap + radius), cz = r.Points[i].Z + nz * (width * .5 + gap + radius);
		double level = r.Surface[i] + .3;
		// Room for it: flat lowland, no river, lake or sea near the crescent (16 points round it and its middle).
		bool clear = true;
		for (int a = 0; a <= 16 && clear; a++)
		{
			double px, pz;
			if (a == 16) { px = cx; pz = cz; }
			else
			{
				double ang = a * RsTau / 16;
				px = cx + std::cos(ang) * radius; pz = cz + std::sin(ang) * radius;
			}
			FRiverHit hit; double lakeCells;
			if (hy.NearestRiver(px, pz, w * .5 + 34, hit) || hy.LakeNear(px, pz) >= 0 || (hy.LakeDistance(px, pz, lakeCells) >= 0 && lakeCells < 2.5)) clear = false;
			else if (macro.Sample(FWorldPlan::LandChannel, px, pz) < .25 || macro.Sample(FWorldPlan::MountainChannel, px, pz) > .12) clear = false;
			else if (macro.Sample(FWorldPlan::HeightChannel, px, pz) + Sea - level > 7) clear = false;
		}
		for (const FBow& b : Bows)
			if (std::sqrt((b.X - cx) * (b.X - cx) + (b.Z - cz) * (b.Z - cz)) < b.Radius + radius + b.Width + w + 60) { clear = false; break; }
		if (clear) Bows.push_back({ cx, cz, nx, nz, radius, w, open, level, depth, seed });
	}
	MinX = macro.MinX; MinZ = macro.MinZ;
	GridSize = MxI(1, (int)std::ceil(macro.Size * macro.Cell / 4000.0));
	std::vector<std::vector<int>> cells((size_t)GridSize * GridSize);
	for (int k = 0; k < (int)Bows.size(); k++)
	{
		const FBow& b = Bows[k];
		double reach = b.Radius + b.Width + 40;
		for (int j = Cell(b.Z - reach, MinZ); j <= Cell(b.Z + reach, MinZ); j++)
			for (int i = Cell(b.X - reach, MinX); i <= Cell(b.X + reach, MinX); i++)
				cells[(size_t)j * GridSize + i].push_back(k);
	}
	FlattenGrid(cells, GridStart, GridItems);
}

/// The ground (metres above the sea, in and out) round and under the oxbow lakes, and whether this is one's water.
/// nearRim: inside an oxbow lake's rim (the wetland pools stay out).
bool FOxbows::Apply(double x, double z, double& height, double& level, bool& nearRim) const
{
	level = NaNValue(); nearRim = false;
	if (Bows.empty()) return false;
	int c = Cell(z, MinZ) * GridSize + Cell(x, MinX);
	bool wet = false;
	for (int k = GridStart[c]; k < GridStart[c + 1]; k++)
	{
		const FBow& b = Bows[GridItems[k]];
		double dx = x - b.X, dz = z - b.Z, r = std::sqrt(dx * dx + dz * dz);
		if (r > b.Radius + b.Width + 40 || r < 1e-6) continue;
		// The angle from the open side (toward the river): the water only where it is over the opening's half-angle.
		double ang = std::acos(DetMath::Clamp(-(dx * b.Nx + dz * b.Nz) / r, -1, 1));
		double radius = b.Radius * (1 + .07 * Noise::Fbm(x, z, b.Radius * 1.3, 2, 0, b.Seed));
		double d = std::fabs(r - radius), half = b.Width * .5 * DetMath::Smooth((ang - b.Open) / .4) * (1 + .15 * Noise::Fbm(x, z, 60, 2, 0, b.Seed + 1));
		double lvl = b.Level - Sea;
		double rim = (1 - DetMath::Smooth((d - half - 6) / 22)) * DetMath::Smooth((ang - b.Open + .25) / .25);
		if (rim > 0) nearRim = true;
		if (rim > 0 && height < lvl + .5) height += (lvl + .5 - height) * rim;
		if (half > .5 && d < half)
		{
			double bed = lvl - .25 - b.Depth * (1 - (d / half) * (d / half));
			if (bed < height) height = bed;
			wet = true; level = b.Level;
		}
	}
	return wet;
}

// ------------------------------------------------------------------------------------------------ river terraces
FTerraces::FTerraces(const FWorldPlan& Plan, FLandformSetting Setting)
	: Amount(Setting.Amount), Size(Setting.Size), Seed(LandformSeed(Plan.Manifest, "terraces")) {}

/// Where along the river there are terraces (0..1).
double FTerraces::Presence(double lx, double lz, double width) const
{
	double wide = DetMath::Smooth((width - 12) / 12);
	if (wide <= 0) return 0;
	double patch = Noise::Fbm(lx, lz, 1700, 2, 0, Seed + 3);
	return wide * DetMath::Smooth((patch + .25 * (Amount - 1) + .02) / .07);
}

static double Staircase(double w, double step, double tiers, double tread)
{
	double pos = tread * .6, g = 0, riser = 2 * step;
	if (w < pos) return 0; // the floodplain, widened
	for (int k = 1; k <= 3; k++)
	{
		if (w < pos + riser) { double t = (w - pos) / riser; return g + step * (.5 * DetMath::Smooth(t) + .5 * t); }
		g += step; pos += riser;
		double width = tread * DetMath::Clamp(tiers - k + 1, 0, 1);
		if (w < pos + width) return g;
		pos += width;
	}
	return g + (w - pos) * .5; // the valley wall behind
}

/// The ground beside a river cut into terraces: h its height over the river's bank top at w metres out from the bank.
double FTerraces::Step(double h, double w, double lx, double lz, double weight) const
{
	const double MaxCut = 10;
	if (h <= 0 || weight <= 0 || w <= 0) return h;
	weight *= 1 - DetMath::Smooth((w - 180) / 70);
	if (weight <= 0) return h;
	double step = Size * (3 + 4 * DetMath::Smooth(.5 + 1.5 * Noise::Fbm(lx, lz, 2600, 2, 0, Seed + 5)));
	double tiers = 1 + 2 * DetMath::Smooth(.5 + 1.5 * Noise::Fbm(lx, lz, 3300, 2, 0, Seed + 7));
	double tread = DetMath::Clamp((150 - 2 * step * tiers) / (tiers + .6), 15, 70) * (.8 + .4 * DetMath::Smooth(.5 + 1.5 * Noise::Fbm(lx, lz, 900, 2, 0, Seed + 9)));
	double g = Staircase(w, step, tiers, tread);
	// A soft minimum (no crease where the cut meets the ground), and never over 10 m deep (a soft maximum).
	double k = 1.5, m = DetMath::Clamp(.5 + .5 * (g - h) / k, 0, 1), cut = g + (h - g) * m - k * m * (1 - m);
	double floor = h - MaxCut, kk = 2, mm = DetMath::Clamp(.5 + .5 * (cut - floor) / kk, 0, 1);
	cut = floor + (cut - floor) * mm + kk * mm * (1 - mm);
	return h + (Mn(cut, h) - h) * weight;
}

// ------------------------------------------------------------------------------------------------ alluvial fans
int FFans::Cell(double v, double min) const { return ClampI((int)std::floor((v - min) / 4000.0), 0, GridSize - 1); }

FFans::FFans(const FWorldPlan& Plan, const FClimate& Climate, FLandformSetting Setting)
{
	const FMacroRaster& macro = Plan.Macro; const FHydrology& hy = *Plan.Hydrology;
	FRandomStream rng(LandformSeed(Plan.Manifest, "fans"));
	for (const FRiverReach& r : hy.Reaches)
	{
		if (r.bDistributary || r.Points.size() < 10 || r.Length() < 1500) continue;
		int n = (int)r.Points.size();
		// The mountain front: the first point where the stream, falling over 2 % above it, eases to under 0.7 % below it
		// onto land that is not mountain.
		int apex = -1;
		for (int i = 4; i < n - 5 && apex < 0; i++)
		{
			double up = r.Along[i] - r.Along[i - 4], down = r.Along[i + 4] - r.Along[i];
			if (up <= 0 || down <= 0) continue;
			double above = (r.Surface[i - 4] - r.Surface[i]) / up, below = (r.Surface[i] - r.Surface[i + 4]) / down;
			if (above < .03 || below > .006 || below > above / 4) continue;
			if (macro.Sample(FWorldPlan::MountainChannel, r.Points[i + 4].X, r.Points[i + 4].Z) > .15) continue;
			// (not where the stream leaves a gorge or runs into a canyon)
			if (hy.Gorge(r.Id, r.Along[i]) > 0 || hy.Gorge(r.Id, r.Along[i + 4]) > 0) continue;
			apex = i;
		}
		u64 seed = rng.Next();
		double roll = rng.Unit(), radiusShare = rng.Between(0, 1), halfShare = rng.Between(0, 1), thick = rng.Between(5, 12);
		if (apex < 0 || r.Width[apex] < 3 || r.Width[apex] > 45) continue;
		const FPoint2 p = r.Points[apex];
		// More in dry country: 40 % of the places below ~300 mm a year, 6 % over ~1500.
		double temperature, rain;
		Climate.At(p.X, p.Z, 0, temperature, rain);
		if (roll > Setting.Amount * (.06 + .34 * DetMath::Smooth((1500 - rain) / 1200))) continue;
		double ax = r.Points[apex + 3].X - p.X, az = r.Points[apex + 3].Z - p.Z, al = std::sqrt(ax * ax + az * az);
		if (al < 1e-6) continue;
		ax /= al; az /= al;
		double radius = (300 + 500 * radiusShare) * Setting.Size, half = .7 + .35 * halfShare;
		// Room for it: land, no canyon, no lake, not over another fan (the apex, the toe and the edges at three distances).
		bool clear = true;
		for (int a = 0; a < 10 && clear; a++)
		{
			double t = a < 1 ? 0 : (a - 1) / 3 / 2.0 + .35, ang = a < 1 ? 0 : ((a - 1) % 3 - 1) * half;
			if (a == 9) { t = 1; ang = 0; }
			double ca = std::cos(ang), sa = std::sin(ang);
			double px = p.X + (ax * ca - az * sa) * radius * t, pz = p.Z + (az * ca + ax * sa) * radius * t, lakeCells;
			if (macro.Sample(FWorldPlan::LandChannel, px, pz) < .2 || (Plan.Canyons && Plan.Canyons->Uplift(px, pz) > 1) || hy.LakeNear(px, pz) >= 0
				|| (hy.LakeDistance(px, pz, lakeCells) >= 0 && lakeCells < 2.5)) clear = false;
		}
		for (const FFan& f : Fans)
			if (std::sqrt((f.X - p.X) * (f.X - p.X) + (f.Z - p.Z) * (f.Z - p.Z)) < f.Radius + radius + 100) { clear = false; break; }
		if (clear) Fans.push_back({ p.X, p.Z, ax, az, radius, half, thick * Setting.Size, seed });
	}
	MinX = macro.MinX; MinZ = macro.MinZ;
	GridSize = MxI(1, (int)std::ceil(macro.Size * macro.Cell / 4000.0));
	std::vector<std::vector<int>> cells((size_t)GridSize * GridSize);
	for (int k = 0; k < (int)Fans.size(); k++)
	{
		const FFan& f = Fans[k];
		double reach = f.Radius * 1.15 + 20;
		for (int j = Cell(f.Z - reach, MinZ); j <= Cell(f.Z + reach, MinZ); j++)
			for (int i = Cell(f.X - reach, MinX); i <= Cell(f.X + reach, MinX); i++)
				cells[(size_t)j * GridSize + i].push_back(k);
	}
	FlattenGrid(cells, GridStart, GridItems);
}

/// The ground (metres above the sea, in and out) with the fans' gravel laid on it; gravel: how much lies there (0..1).
double FFans::Fill(double x, double z, double ground, double& gravel) const
{
	gravel = 0;
	if (Fans.empty()) return ground;
	int c = Cell(z, MinZ) * GridSize + Cell(x, MinX);
	double add = 0;
	for (int k = GridStart[c]; k < GridStart[c + 1]; k++)
	{
		const FFan& f = Fans[GridItems[k]];
		double dx = x - f.X, dz = z - f.Z, r = std::sqrt(dx * dx + dz * dz);
		if (r > f.Radius * 1.15 || r < 1e-6) continue;
		double along = dx * f.Ax + dz * f.Az, ang = std::acos(DetMath::Clamp(along / r, -1, 1));
		// The edge wanders (12 % of the half-angle and of the length); the gravel thins out toward it.
		double half = f.Half * (1 + .12 * Noise::Fbm(dx, dz, f.Radius * .6, 2, 0, f.Seed));
		double length = f.Radius * (1 + .12 * Noise::Fbm(dx, dz, f.Radius * .5, 2, 0, f.Seed + 1));
		double side = 1 - DetMath::Smooth((ang - half * .55) * r / Mx(half * .45 * r, 60));
		if (side <= 0 || r >= length) continue;
		double t = r / length, thick = f.Height * std::pow(1 - t, 1.4) * DetMath::Smooth(r / 150) * side;
		// Dry channels radiating from the apex, a few tens of metres apart, wandering.
		double arc = ang * r + 25 * Noise::Fbm(dx, dz, 140, 2, 0, f.Seed + 2);
		double channel = 1 - DetMath::Smooth(std::fabs(std::sin(arc * RsPi / 55)) / .35);
		thick -= Mn(thick * .5, .8 * channel * (1 - t));
		if (thick > add) add = thick;
		gravel = Mx(gravel, side * DetMath::Smooth(thick / 1.5) * (.3 + .4 * (1 - t)));
	}
	return ground + add;
}

// ------------------------------------------------------------------------------------------------ fall lips
struct FFallLips::FLip
{
	std::vector<FPoint2> Points; std::vector<double> Along;
	double Width = 0, Top = 0, Length = 0, Upstream = 0, MinX = 0, MinZ = 0, MaxX = 0, MaxZ = 0;
	u64 Seed = 0; double Bottom = 0; int Reach = 0; int LipIndex = 0;

	/// How far a point lies past the lip straight down the fall and to its side.
	void Straight(double x, double z, double& along, double& side) const
	{
		const FPoint2& p = Points[LipIndex]; const FPoint2& q = Points[MnI((int)Points.size() - 1, LipIndex + 1)];
		double fx = q.X - p.X, fz = q.Z - p.Z, fl = Mx(1e-6, std::sqrt(fx * fx + fz * fz));
		along = ((x - p.X) * fx + (z - p.Z) * fz) / fl;
		side = ((x - p.X) * fz - (z - p.Z) * fx) / fl;
	}
	/// Revision 44: the water's level along the lip's stretch (along past the lip, negative upstream).
	double LipLevel(double along, double river) const
	{
		double toLip = 1 - DetMath::Smooth(-along / Upstream);
		return std::isnan(river) ? Top - (1 - toLip) * .5 : river + (Top - river) * toLip;
	}
	/// Distance to the river's line near a lip and how far along it from the lip (negative upstream).
	void Locate(double x, double z, double& d, double& along) const
	{
		double best = std::numeric_limits<double>::max(); along = 0;
		int count = (int)Points.size();
		for (int s = 0; s + 1 < count; s++)
		{
			const FPoint2& p = Points[s]; const FPoint2& q = Points[s + 1];
			double sx = q.X - p.X, sz = q.Z - p.Z, l2 = sx * sx + sz * sz;
			double t = l2 < 1e-9 ? 0 : ((x - p.X) * sx + (z - p.Z) * sz) / l2;
			double tc = DetMath::Clamp(t, 0, 1);
			double ex = p.X + sx * tc - x, ez = p.Z + sz * tc - z;
			double e2 = ex * ex + ez * ez;
			if (s + 2 == count && t > 1)
			{
				// beyond the foot: across from the extended line, along beyond the foot
				double len = std::sqrt(l2), ux = sx / len, uz = sz / len, px = x - q.X, pz = z - q.Z;
				double ahead = px * ux + pz * uz, side = std::fabs(px * uz - pz * ux);
				e2 = side * side;
				if (e2 < best) { best = e2; along = Along[s + 1] + ahead; }
				continue;
			}
			if (e2 < best) { best = e2; along = Along[s] + (Along[s + 1] - Along[s]) * tc; }
		}
		d = std::sqrt(best);
	}
};

namespace
{
double LipSpill(double width) { return DetMath::Clamp(width * .25, 1, 4); }
double LipInner(double width) { return width * .5 + FHydrology::Bank(width) * .6; }
double LipBand(double width) { return 12 + width * .25; }
double LipReach(double width) { return LipInner(width) + LipBand(width) * 1.5; }
int LipCell(double v) { return (int)std::floor(v / 2000.0); }
}

FFallLips::~FFallLips() = default;
int FFallLips::Count() const { return (int)Lips.size(); }

FFallLips::FFallLips(const FWorldPlan& Plan, bool bInFaces, bool bInFollow) : bFaces(bInFaces), bFollow(bInFaces && bInFollow)
{
	FRandomStream rng(Hashing::Mix((u64)(i64)Plan.Manifest.Seed ^ 0xFA1111B5ULL));
	for (const FRiverReach& r : Plan.Hydrology->Reaches)
	{
		if (r.bDistributary || r.Points.size() < 2 || r.Along.size() != r.Points.size()) continue;
		int n = (int)r.Points.size();
		for (int k = 0; k + 1 < n; k++)
		{
			double drop = r.Surface[k] - r.Surface[k + 1], length = r.Along[k + 1] - r.Along[k];
			if (drop < 3 || length <= 0 || drop / length < .2) continue;
			double width = r.Width[k], upstream = Mx(25, width * .8);
			int a = k; while (a > 0 && r.Along[k] - r.Along[a] < upstream + 15) a--;
			int b = MnI(n - 1, k + 1);
			int count = b - a + 1;
			FLip lip;
			lip.Points.resize(count); lip.Along.resize(count);
			double minX = std::numeric_limits<double>::max(), minZ = minX, maxX = std::numeric_limits<double>::lowest(), maxZ = maxX;
			for (int i = 0; i < count; i++)
			{
				lip.Points[i] = r.Points[a + i]; lip.Along[i] = r.Along[a + i] - r.Along[k];
				minX = Mn(minX, lip.Points[i].X); maxX = Mx(maxX, lip.Points[i].X); minZ = Mn(minZ, lip.Points[i].Z); maxZ = Mx(maxZ, lip.Points[i].Z);
			}
			// (revision 44: the river's water on the slope of a tall drop reaches well out to the sides and past the foot)
			double reach = bFollow ? Mx(LipReach(width), width * .5 + length + 30) : LipReach(width);
			lip.Width = width; lip.Top = r.Surface[k]; lip.Length = length; lip.Upstream = upstream;
			lip.MinX = minX - reach; lip.MinZ = minZ - reach; lip.MaxX = maxX + reach; lip.MaxZ = maxZ + reach;
			lip.Seed = rng.Next(); lip.Bottom = r.Surface[k + 1]; lip.Reach = r.Id;
			lip.LipIndex = 0;
			for (int i = 0; i < count; i++) if (lip.Along[i] >= 0) { lip.LipIndex = i; break; }
			Lips.push_back(std::move(lip));
		}
	}
	for (int i = 0; i < (int)Lips.size(); i++)
	{
		const FLip& l = Lips[i];
		for (int gx = LipCell(l.MinX); gx <= LipCell(l.MaxX); gx++)
			for (int gz = LipCell(l.MinZ); gz <= LipCell(l.MaxZ); gz++)
				Grid[{ gx, gz }].push_back(i);
	}
}

/// Revision 43: the rock face at a lip and the plunge pool below it; revision 44: the river's water on a fall's slope
/// takes the lip's level above the face and the pool's below it.
double FFallLips::Pool(double x, double z, double height, double sea, double riverLevel, int riverBody, bool dry, double& level, int& reach,
	double& rock, bool& ramp) const
{
	level = NaNValue(); reach = -1; rock = 0; ramp = false;
	if (!bFaces) return height;
	auto found = Grid.find({ LipCell(x), LipCell(z) });
	if (found == Grid.end()) return height;
	double result = height;
	for (int i : found->second)
	{
		const FLip& l = Lips[i];
		if (x < l.MinX || x > l.MaxX || z < l.MinZ || z > l.MaxZ) continue;
		double d, along;
		l.Locate(x, z, d, along);
		double half = (l.Width * .55 + 3) * (1 + .08 * Noise::Fbm(x, z, 30, 2, 0, l.Seed + 3));
		// Revision 44: the river's water on this fall's slope (its level between the lip's and the foot's).
		bool onSlope = false;
		if (bFollow && dry && height < l.Top - sea - .05)
		{
			// (dry ground in the channel below the water's level along the lip's stretch)
			double sa, ss;
			l.Straight(x, z, sa, ss);
			if (sa >= -l.Upstream * .5 && sa < .2 && std::fabs(ss) <= l.Width * .5 + LipSpill(l.Width) && height < l.LipLevel(sa, NaNValue()) - sea - .05)
			{
				level = l.LipLevel(sa, NaNValue()); reach = l.Reach; ramp = true; continue;
			}
		}
		if (bFollow && riverBody == l.Reach && riverLevel < l.Top - .05)
		{
			double straight, side;
			l.Straight(x, z, straight, side);
			onSlope = riverLevel > l.Bottom + (straight > l.Length ? .5 : .05);
			// (upstream of the lip's stretch, as before)
			if (onSlope && straight < -l.Upstream) continue;
			if (onSlope && straight < .2)
			{
				// Above the face: within the river's width the lip's water, level to the edge; beyond it dry.
				if (std::fabs(side) <= l.Width * .5 + LipSpill(l.Width)) { level = l.LipLevel(straight, riverLevel); reach = l.Reach; }
				ramp = true;
				continue;
			}
			if (onSlope) along = Mx(along, straight);
		}
		// Sheer: the face rises over half a metre past the lip, the walls over 2 m at the pool's sides; the pool ends at
		// the foot, shoaling over 8 m into the river below.
		double inPool = DetMath::Smooth((along - .2) / .6) * (1 - DetMath::Smooth((d - half) / 2)) * (1 - DetMath::Smooth((along - l.Length) / 8));
		if (onSlope) inPool = 1; // (revision 44: the pool takes in all of the slope's water past the face)
		if (inPool <= 0) continue;
		double depth = DetMath::Clamp((l.Top - l.Bottom) * .15, 2, 8);
		double bowl = 1 - (d / half) * (d / half) * .6;
		double floor = l.Bottom - sea - depth * Mx(.25, bowl) * (1 - DetMath::Smooth((along - l.Length + 4) / 8));
		if (onSlope)
		{
			double edgeBowl = 1 - Mn(1, (d / half) * (d / half)) * .6;
			floor = Mn(floor, l.Bottom - sea - Mx(.6, depth * edgeBowl * (1 - DetMath::Smooth((along - l.Length + 4) / 8))));
			ramp = true;
		}
		double lowered = height + (Mn(height, floor) - height) * inPool;
		if (lowered < result) result = lowered;
		if (inPool > .5 && result < l.Bottom - sea - .05) { level = l.Bottom; reach = l.Reach; }
		// Bare rock on the face and the walls (where the ground drops away steeply).
		rock = Mx(rock, DetMath::Smooth((height - result) / 3) * .9);
	}
	return result;
}

/// Revision 42: raises dry ground (metres above the sea) beside fall lips to the sill's height.
double FFallLips::Apply(double x, double z, double height, double sea, double& rock) const
{
	rock = 0;
	auto found = Grid.find({ LipCell(x), LipCell(z) });
	if (found == Grid.end()) return height;
	double result = height;
	for (int i : found->second)
	{
		const FLip& l = Lips[i];
		if (x < l.MinX || x > l.MaxX || z < l.MinZ || z > l.MaxZ) continue;
		// Distance to the river's line near the lip and how far along it (negative upstream of the lip).
		double best = std::numeric_limits<double>::max(), along = 0;
		for (int s = 0; s + 1 < (int)l.Points.size(); s++)
		{
			const FPoint2& p = l.Points[s]; const FPoint2& q = l.Points[s + 1];
			double sx = q.X - p.X, sz = q.Z - p.Z, l2 = sx * sx + sz * sz;
			double t = l2 < 1e-9 ? 0 : DetMath::Clamp(((x - p.X) * sx + (z - p.Z) * sz) / l2, 0, 1);
			double ex = p.X + sx * t - x, ez = p.Z + sz * t - z, d2 = ex * ex + ez * ez;
			if (d2 < best) { best = d2; along = l.Along[s] + (l.Along[s + 1] - l.Along[s]) * t; }
		}
		double d = std::sqrt(best), w = l.Width, inner = LipInner(w), band = LipBand(w);
		// Revision 44: near the lip in the fall's straight frame.
		if (bFollow)
		{
			double sa, ss;
			l.Straight(x, z, sa, ss);
			if (sa > -l.Upstream - 10 && sa < l.Length) { along = sa; d = std::fabs(ss); }
		}
		// Out of the channel (rising from its edge over the bank), fading out beyond the band.
		double across = DetMath::Smooth((d - w * .5 - .5) / Mx(2, inner - w * .5)) * (1 - DetMath::Smooth((d - inner - band * .5) / band));
		if (bFollow && along < .2) across = DetMath::Smooth((d - w * .5 - LipSpill(w) * .25) / (LipSpill(w) * .75)) * (1 - DetMath::Smooth((d - inner - band * .5) / band));
		// From the upstream end to the lip, on down the fall's sides, fading out toward its foot.
		double alongW = DetMath::Smooth((along + l.Upstream) / (l.Upstream * .6)) * (1 - DetMath::Smooth((along - l.Length * .6) / (l.Length * .4 + 4)));
		double weight = across * alongW;
		if (weight <= 0) continue;
		double rise = 1.8 + 1.4 * DetMath::Smooth(.5 + 1.5 * Noise::Fbm(x, z, 40, 2, 0, l.Seed));
		double target = l.Top - sea + rise;
		if (target <= result) continue;
		result += (target - result) * weight;
		rock = Mx(rock, weight * .85);
	}
	return result;
}
}
