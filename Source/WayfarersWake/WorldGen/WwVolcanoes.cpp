// Port of Thornwake Game/WorldGen/Volcanoes.cs: volcanoes and calderas (revision 28), tuned shapes and traced lava
// flows (29), cinder cones, ravines and lava fields (30), active volcanoes with lava lakes (31), flows spilling from
// the crater (32). The source traces flows and reads lake levels lazily on first use; here Finalize() does it once
// the plan is complete, which gives the same values and keeps sampling free of shared mutable state.
#include "WwLandforms.h"

namespace ww
{
namespace
{
const double VTau = 6.283185307179586, VPi = 3.141592653589793;
}

struct FVolcanoes::FFlow
{
	std::vector<double> X, Z, W;
	double Thickness = 0;
	struct FBox { double MinX, MinZ, MaxX, MaxZ; };
	std::vector<FBox> Chunks;
	double MinX = 0, MinZ = 0, MaxX = 0, MaxZ = 0;
	u64 Seed = 0;
	bool bSpill = false;
	double SpillLevel = 0;

	/// The nearest point of the flow's line: distance, width there, how far along (0..1) and the line's direction.
	bool Nearest(double x, double z, double& best, double& width, double& along, double& dirX, double& dirZ) const
	{
		best = std::numeric_limits<double>::max(); width = 0; along = 0; dirX = dirZ = 0;
		int last = (int)X.size() - 1;
		for (int c = 0; c < (int)Chunks.size(); c++)
		{
			const FBox& b = Chunks[c];
			if (x < b.MinX || x > b.MaxX || z < b.MinZ || z > b.MaxZ) continue;
			int end = MnI(last, c * 10 + 10);
			for (int i = c * 10; i < end; i++)
			{
				double ax = X[i], az = Z[i], ex = X[i + 1] - ax, ez = Z[i + 1] - az, len2 = Mx(1e-9, ex * ex + ez * ez);
				double t = DetMath::Clamp(((x - ax) * ex + (z - az) * ez) / len2, 0, 1);
				double d = std::sqrt(Sq(ax + ex * t - x) + Sq(az + ez * t - z));
				if (d < best) { best = d; width = W[i] + (W[i + 1] - W[i]) * t; along = (i + t) / last; dirX = ex; dirZ = ez; }
			}
		}
		double dl = std::sqrt(dirX * dirX + dirZ * dirZ);
		if (dl > 1e-9) { dirX /= dl; dirZ /= dl; }
		return best != std::numeric_limits<double>::max();
	}
};

struct FVolcanoes::FVolcano
{
	double X = 0, Z = 0, Height = 0, Radius = 0, Power = 0;
	double Inner = 0, Depth = 0;
	bool bCaldera = false, bLake = false;
	double BreachX = 0, BreachZ = 0, Breach = 0;
	struct FRadialFlow { double Angle, From, To, Width, Thickness; };
	std::vector<FRadialFlow> Flows; // revision 28
	u64 Seed = 0;
	double LakeLevel = 0;
	bool bTuned = false;
	double CollapseX = 0, CollapseZ = 0, Collapse = 0;
	double IslandX = 0, IslandZ = 0, IslandRadius = 0, IslandHeight = 0, IslandAbove = 0;
	bool bWorn = false;
	bool bTraced = false;
	std::vector<FFlow> Traced;
	double FlowMinX = std::numeric_limits<double>::max(), FlowMinZ = std::numeric_limits<double>::max();
	double FlowMaxX = std::numeric_limits<double>::lowest(), FlowMaxZ = std::numeric_limits<double>::lowest();
	bool bDetailed = false, bActive = false;
	int FreshFlows = 0;
	double LavaLevel = 0;
	struct FCinder { double X, Z, Radius, Height; };
	std::vector<FCinder> Cinders;
	struct FFlowStart { double Angle, Length, Width, Thickness; };
	std::vector<FFlowStart> FlowStarts;
	double Base = 0, TiltX = 0, TiltZ = 0;

	/// How much the volcano replaces the land here: whole near the middle, fading out to its foot.
	double Weight(double x, double z) const
	{
		double r = std::sqrt(Sq(x - X) + Sq(z - Z));
		if (bTuned) return 1 - DetMath::Smooth((r - Radius * .6) / (Radius * .45)); // (revision 29: a wider blend)
		return 1 - DetMath::Smooth((r - Radius * .75) / (Radius * .25));
	}

	/// The volcano's height over the land it stands on (m): exact with its crater or caldera cliffs (walking scale),
	/// otherwise the shape the raster holds. steep: how steep the ground there is meant to be (0..1, for the rock).
	double Profile(double x, double z, bool exact, double& steep) const
	{
		steep = 0;
		double dx = x - X, dz = z - Z, r0 = std::sqrt(dx * dx + dz * dz);
		if (r0 >= Radius * 1.1) return 0;
		double r = r0 * (1 + .06 * Noise::Fbm(x, z, Radius * .3, 2, 0, Seed));
		if (r >= Radius) return 0;
		if (!bCaldera)
		{
			double u = Mx(0, (r - Inner) / (Radius - Inner));
			double cone = Height * std::pow(1 - u, Power);
			steep = DetMath::Smooth((Power * Height / (Radius - Inner) * std::pow(1 - u, Power - 1) - .45) / .3);
			if (Collapse > 0)
			{
				// A collapsed side (revision 29): a horseshoe amphitheatre cut into the summit, opening on the low side.
				if (r < Inner && exact) cone = Mn(cone, Height - Depth * (1 - Sq(r / Inner)));
				double rs = Radius * .18, px = X + CollapseX * rs * .7, pz = Z + CollapseZ * rs * .7;
				double d = std::sqrt(Sq(x - px) + Sq(z - pz)) * (1 + .12 * Noise::Fbm(x, z, rs * .5, 2, 0, Seed + 31));
				double scar = 1 - DetMath::Smooth((d - rs * .85) / (rs * .3));
				if (scar > 0)
				{
					double down = (x - px) * CollapseX + (z - pz) * CollapseZ;
					double atP = Height * std::pow(1 - Mx(0, (rs * .7 - Inner) / (Radius - Inner)), Power);
					double scarFloor = atP - Collapse * Height - .3 * down;
					double cut = scar * Mx(0, cone - scarFloor);
					steep = Mx(steep, scar * DetMath::Smooth((cut - 40) / 120) * DetMath::Smooth(-down / rs + .2));
					cone -= cut;
				}
				return cone;
			}
			if (r >= Inner || !exact) return cone;
			// The crater: a bowl under a sharp rim.
			double q = r / Inner;
			steep = Mx(steep, DetMath::Smooth((q - .55) / .3));
			return Height - Depth * (1 - q * q);
		}
		// Caldera: a shield outside the rim; inside, cliffs down to a flat floor; the rim breached on one side.
		r *= 1 + .1 * Noise::Fbm(x, z, Inner * .6, 3, 0, Seed + 3);
		if (r >= Radius) return 0;
		double breach = 0;
		if (r0 > 1e-6)
		{
			double along = (dx * BreachX + dz * BreachZ) / r0;
			breach = bTuned
				? Breach * DetMath::Smooth((along - .82) / .14) * (1 - DetMath::Smooth((r - Inner * 1.6) / (Inner * .6)))
				: Breach * DetMath::Smooth((along - .93) / .06) * (1 - DetMath::Smooth((r - Inner * 1.6) / (Inner * .5)));
		}
		double top = Height - Depth * breach;
		if (bWorn) top -= Depth * .33 * DetMath::Smooth(.5 + Noise::Fbm(x, z, Inner * .5, 2, 0, Seed + 21)); // (an old rim, worn down unevenly)
		if (r >= Inner)
		{
			double u = (r - Inner) / (Radius - Inner);
			return Mn(top, Height * std::pow(1 - u, Power) + (top - Height) * (1 - u));
		}
		double wall = Depth * (exact ? bWorn ? 2.2 : 1 : 3), floor = Height - Depth;
		double t = DetMath::Smooth((r - (Inner - wall)) / wall);
		steep = exact ? DetMath::Smooth(1 - std::fabs(t - .5) * (bWorn ? 4 : 2.5)) : 0;
		double inside = floor + (top - floor) * t;
		if (IslandRadius > 0 && exact)
		{
			// An island cone in the lake (revision 29), a small crater at its top.
			double ir = std::sqrt(Sq(dx - IslandX) + Sq(dz - IslandZ)) / IslandRadius;
			if (ir < 1)
			{
				double island = floor + IslandHeight * std::pow(1 - ir, 1.6) - IslandHeight * .15 * (1 - DetMath::Smooth(ir / .12));
				if (island > inside) { inside = island; steep = Mx(steep, .5 * DetMath::Smooth((.6 - ir) / .3)); }
			}
		}
		return inside;
	}

	/// Revision 30's walking-scale detail: cinder cones, ravines and knife ridges, old lava fields, rim outcrops.
	double Detail(double x, double z, double r, double fade, double& rock) const
	{
		double g = 0, u = r / Radius;
		for (const FCinder& c : Cinders)
		{
			double d = std::sqrt(Sq(x - c.X) + Sq(z - c.Z));
			if (d >= c.Radius) continue;
			double q = d / c.Radius * (1 + .08 * Noise::Fbm(x, z, c.Radius * .6, 2, 0, Seed + 61));
			if (q >= 1) continue;
			g += c.Height * std::pow(1 - q, 1.3) - c.Height * .45 * (1 - DetMath::Smooth(q / .28));
			rock = Mx(rock, .55 * (1 - DetMath::Smooth((q - .5) / .5)));
		}
		if (u > 1.35) return g * fade;
		if (!bCaldera)
		{
			// Ravines and knife ridges high on the cone, starting below the crater's rim.
			double start = Mx(.07, Inner * 1.5 / Radius);
			double zone = DetMath::Smooth((u - start) / .06) * (1 - DetMath::Smooth((u - .45) / .2));
			if (zone > 0)
			{
				int count = 9 + (int)(Seed % 6);
				double c = std::cos(count * std::atan2(z - Z, x - X) + 2 * Noise::Fbm(x, z, 3000, 2, 0, Seed + 71));
				double ravine = DetMath::Smooth((c - .45) / .55), crest = DetMath::Smooth((-c - .6) / .4);
				double deep = (70 + 70 * DetMath::Smooth(.5 + Noise::Fbm(x, z, 2000, 2, 0, Seed + 72))) * Mn(1, Height / 2000);
				g += (25 * crest - deep * ravine) * zone;
				rock = Mx(rock, zone * Mx(.5 * ravine, .7 * crest));
			}
		}
		// Old lava fields: hummocky, blocky ground on the lower flanks and round the foot, in patches.
		double field = DetMath::Smooth((u - .4) / .15) * (1 - DetMath::Smooth((u - 1.2) / .15))
			* DetMath::Smooth((Noise::Fbm(x, z, 3500, 2, 0, Seed + 41) + .05) / .25);
		if (field > 0)
		{
			double ridged = 1 - std::fabs(Noise::Fbm(x, z, 140, 3, 0, Seed + 42));
			g += field * (6 * ridged * ridged - 2 + 2.5 * Noise::Fbm(x, z, 35, 2, 0, Seed + 43));
			rock = Mx(rock, .75 * field);
		}
		// Rock outcrops on a crater's rim or along a caldera's cliffs.
		double band = bCaldera ? 1 - DetMath::Smooth(std::fabs(r - Inner) / (Inner * .15))
			: 1 - DetMath::Smooth(std::fabs(r - Inner) / (Inner * .35));
		if (band > 0)
		{
			double outcrop = Mx(0, Noise::Fbm(x, z, 90, 3, 0, Seed + 51) - .05);
			g += band * 30 * outcrop;
			rock = Mx(rock, .9 * band);
		}
		return g * fade;
	}
};

FVolcanoes::FVolcanoes(int InRevision) : Revision(InRevision) {}
FVolcanoes::~FVolcanoes() = default;
int FVolcanoes::Count() const { return (int)Volcanoes.size(); }

void FVolcanoes::Shape(FMacroRaster& InMacro, const FWorldManifest& Manifest, FLandformSetting Setting)
{
	Macro = &InMacro;
	Sea = Manifest.Profile.SeaLevel;
	Place(InMacro, Manifest, Setting);
	if (Volcanoes.empty()) return;
	// Each volcano is built on a level base (the land's mean height round its foot), replacing the relief under it.
	const std::vector<float>& land = InMacro.Channel(FWorldPlan::LandChannel);
	std::vector<float>& height = InMacro.Channel(FWorldPlan::HeightChannel);
	std::vector<float>& mountain = InMacro.Channel(FWorldPlan::MountainChannel);
	for (FVolcano& v : Volcanoes)
	{
		double sum = 0;
		for (int d = 0; d < 16; d++)
		{
			double a = d * VTau / 16;
			sum += InMacro.Sample(FWorldPlan::HeightChannel, v.X + std::cos(a) * v.Radius * 1.05, v.Z + std::sin(a) * v.Radius * 1.05);
		}
		v.Base = sum / 16;
		if (v.bTuned)
		{
			double sx = 0, sz = 0;
			for (int d = 0; d < 16; d++)
			{
				double a = d * VTau / 16, h = InMacro.Sample(FWorldPlan::HeightChannel, v.X + std::cos(a) * v.Radius * 1.05, v.Z + std::sin(a) * v.Radius * 1.05);
				sx += (h - v.Base) * std::cos(a); sz += (h - v.Base) * std::sin(a);
			}
			v.TiltX = sx / 8 / (v.Radius * 1.05); v.TiltZ = sz / 8 / (v.Radius * 1.05); // (least squares on the ring)
		}
	}
	RaiseRaster = FMacroRaster(InMacro.Size, InMacro.MinX, InMacro.MinZ, InMacro.Cell, 1);
	std::vector<float>& raised = RaiseRaster.Channel(0);
	int size = InMacro.Size;
	for (int j = 0; j < size; j++)
		for (int i = 0; i < size; i++)
		{
			int k = j * size + i;
			double x = InMacro.CellX(i), z = InMacro.CellZ(j);
			for (const FVolcano& v : Volcanoes)
			{
				double w = v.Weight(x, z) * DetMath::Smooth((land[k] - .2) / .3);
				if (w <= 0) continue;
				double steep;
				double shape = v.Profile(x, z, false, steep);
				raised[k] = (float)(w * shape);
				// (a caldera's floor stays level inside its rim: tilted, its basin drained over the low side)
				double tilt = v.bCaldera ? DetMath::Smooth((std::sqrt(Sq(x - v.X) + Sq(z - v.Z)) - v.Inner * 1.2) / (v.Inner * .6)) : 1;
				double level = v.Base + tilt * (v.TiltX * (x - v.X) + v.TiltZ * (z - v.Z));
				height[k] = (float)(height[k] * (1 - w) + w * (level + shape));
				if (v.bDetailed)
				{
					double patch = DetMath::Smooth(.5 + 1.2 * Noise::Fbm(x, z, 5000, 2, 0, v.Seed + 81));
					double m = .08 + .27 * patch * DetMath::Smooth((std::sqrt(Sq(x - v.X) + Sq(z - v.Z)) / v.Radius - .3) / .5);
					mountain[k] = (float)(mountain[k] * (1 - w) + w * m);
				}
				else mountain[k] = (float)(mountain[k] * (1 - w) + w * .2 * shape / v.Height); // (kept in this exact form: bit-exact)
			}
		}
}

bool FVolcanoes::Resistance(const FMacroRaster& M, std::vector<float>& resist) const
{
	bool any = false;
	for (const FVolcano& v : Volcanoes) if (v.bCaldera) any = true;
	if (!any) return false;
	resist.assign((size_t)M.Size * M.Size, 0.f);
	for (int j = 0; j < M.Size; j++)
		for (int i = 0; i < M.Size; i++)
		{
			double x = M.CellX(i), z = M.CellZ(j);
			for (const FVolcano& v : Volcanoes)
			{
				if (!v.bCaldera) continue;
				double r = std::sqrt(Sq(x - v.X) + Sq(z - v.Z));
				double ring = DetMath::Smooth((r - v.Inner * .6) / (v.Inner * .2)) * (1 - DetMath::Smooth((r - v.Inner * 1.5) / (v.Inner * .3)));
				float f = (float)ring;
				if (f > resist[(size_t)j * M.Size + i]) resist[(size_t)j * M.Size + i] = f;
			}
		}
	return true;
}

void FVolcanoes::LakeCells(const FMacroRaster& M, std::vector<int>& cells) const
{
	for (const FVolcano& v : Volcanoes)
		if (v.bCaldera) cells.push_back((int)((v.Z - M.MinZ) / M.Cell) * M.Size + (int)((v.X - M.MinX) / M.Cell));
}

void FVolcanoes::Place(const FMacroRaster& M, const FWorldManifest& Manifest, FLandformSetting setting)
{
	FRandomStream rng(LandformSeed(Manifest, "volcanoes"));
	const std::vector<float>& land = M.Channel(FWorldPlan::LandChannel);
	const std::vector<float>& mountain = M.Channel(FWorldPlan::MountainChannel);
	int landCells = 0;
	std::vector<int> shoulders, plains;
	for (int c = 0; c < (int)land.size(); c++)
	{
		if (land[c] > 0) landCells++;
		if (land[c] < .6f) continue;
		if (mountain[c] > .05f && mountain[c] < .45f) shoulders.push_back(c);
		else if (mountain[c] <= .05f) plains.push_back(c);
	}
	double landKm2 = landCells * M.Cell * M.Cell / 1e6;
	int want = ClampI((int)std::nearbyint(setting.Amount * landKm2 / (160 * 160)), landKm2 > 5000 ? 1 : 0, 16);
	for (int attempt = 0; attempt < want * 40 && (int)Volcanoes.size() < want; attempt++)
	{
		const std::vector<int>& from = rng.Unit() < .65 && !shoulders.empty() ? shoulders : plains;
		if (from.empty()) continue;
		int k = from[rng.Int((int)from.size())];
		FVolcano v;
		v.X = M.CellX(k % M.Size); v.Z = M.CellZ(k / M.Size);
		v.bCaldera = rng.Unit() < .3;
		v.Seed = rng.Next();
		if (Revision >= 29)
		{
			// Revision 29's shapes: taller cones (some with a collapsed side), deeper calderas with islands and worn rims.
			v.bTuned = true;
			if (v.bCaldera)
			{
				v.Height = rng.Between(500, 1100) * setting.Size; v.Radius = rng.Between(9000, 16000) * setting.Size; v.Power = 1.5;
				v.Inner = rng.Between(2500, 5000) * setting.Size; v.Depth = rng.Between(350, 700) * setting.Size;
				double a = rng.Unit() * VTau;
				v.BreachX = std::cos(a); v.BreachZ = std::sin(a); v.Breach = rng.Between(.55, .7);
				if (rng.Unit() < .45)
				{
					double ia = rng.Unit() * VTau, id = rng.Between(0, .3) * v.Inner;
					v.IslandX = std::cos(ia) * id; v.IslandZ = std::sin(ia) * id;
					v.IslandRadius = rng.Between(400, 900) * setting.Size;
					v.IslandAbove = rng.Between(30, 150);
					v.IslandHeight = v.Depth * (1 - v.Breach) + v.IslandAbove; // (set from the lake's level once it is known: SetLakes)
				}
				v.bWorn = rng.Unit() < .4;
			}
			else
			{
				v.Height = rng.Between(1200, 3000) * setting.Size; v.Radius = v.Height * rng.Between(3.5, 5); v.Power = rng.Between(2.2, 2.8);
				v.Inner = rng.Between(250, 600) * setting.Size; v.Depth = rng.Between(80, 200) * setting.Size;
				if (rng.Unit() < .3)
				{
					double a = rng.Unit() * VTau;
					v.CollapseX = std::cos(a); v.CollapseZ = std::sin(a); v.Collapse = rng.Between(.2, .35);
				}
				v.bLake = v.Collapse == 0 && rng.Unit() < .6;
				int flows = 3 + rng.Int(4);
				for (int f = 0; f < flows; f++)
				{
					FVolcano::FFlowStart s;
					s.Angle = rng.Unit() * VTau; s.Length = rng.Between(2000, 7000) * setting.Size; s.Width = rng.Between(150, 350) * setting.Size; s.Thickness = rng.Between(6, 15);
					v.FlowStarts.push_back(s);
				}
			}
		}
		else if (v.bCaldera)
		{
			v.Height = rng.Between(500, 1100) * setting.Size; v.Radius = rng.Between(9000, 16000) * setting.Size; v.Power = 1.5;
			v.Inner = rng.Between(2500, 5000) * setting.Size; v.Depth = rng.Between(250, 500) * setting.Size;
			double a = rng.Unit() * VTau;
			v.BreachX = std::cos(a); v.BreachZ = std::sin(a); v.Breach = rng.Between(.55, .7);
		}
		else
		{
			v.Height = rng.Between(900, 2200) * setting.Size; v.Radius = v.Height * rng.Between(4, 6); v.Power = rng.Between(1.8, 2.4);
			v.Inner = rng.Between(250, 600) * setting.Size; v.Depth = rng.Between(80, 200) * setting.Size;
			v.bLake = rng.Unit() < .6;
			int flows = 3 + rng.Int(4);
			for (int f = 0; f < flows; f++)
			{
				FVolcano::FRadialFlow s;
				s.Angle = rng.Unit() * VTau; s.From = rng.Between(.12, .25); s.To = rng.Between(.5, .9); s.Width = rng.Between(150, 400) * setting.Size; s.Thickness = rng.Between(6, 15);
				v.Flows.push_back(s);
			}
		}
		// Revision 30: cinder cones (their own random stream, from the volcano's seed).
		if (Revision >= 30)
		{
			v.bDetailed = true;
			FRandomStream own(v.Seed ^ 0x5C0B1A5EULL);
			int count = v.bCaldera ? 2 + own.Int(4) : 3 + own.Int(5);
			for (int n = 0; n < count; n++)
			{
				double a = own.Unit() * VTau;
				double u = v.bCaldera ? own.Between(Mx(.5, v.Inner * 1.6 / v.Radius), 1.2) : own.Unit() < .5 ? own.Between(.45, .9) : own.Between(1, 1.3);
				double cr = own.Between(300, 700) * setting.Size, ch = own.Between(50, 150) * setting.Size;
				double x = v.X + std::cos(a) * u * v.Radius, z = v.Z + std::sin(a) * u * v.Radius;
				if (M.Sample(FWorldPlan::LandChannel, x, z) < .45) continue;
				v.Cinders.push_back({ x, z, cr, ch });
			}
		}
		// Revision 31: whether a cone is active (its own random stream too): a lava lake instead of water, fresh flows.
		if (Revision >= 31 && !v.bCaldera)
		{
			FRandomStream own(v.Seed ^ 0x1A7A1A7AULL);
			v.bActive = own.Unit() < .45;
			if (v.bActive) { v.bLake = false; v.FreshFlows = 1 + own.Int(2); }
		}
		bool ok = true;
		for (int d = 0; d < 8 && ok; d++)
		{
			double a = d * VTau / 8;
			if (M.Sample(FWorldPlan::LandChannel, v.X + std::cos(a) * v.Radius, v.Z + std::sin(a) * v.Radius) < .35) ok = false;
		}
		for (const FVolcano& o : Volcanoes) if (std::sqrt(Sq(o.X - v.X) + Sq(o.Z - v.Z)) < o.Radius + v.Radius + 5000) ok = false;
		if (ok) Volcanoes.push_back(std::move(v));
	}
}

/// The ground a flow runs on (m): the plan's height with the volcano's exact shape, no walking-scale relief.
double FVolcanoes::Ground0(const FVolcano& v, double x, double z) const
{
	double steep;
	return Macro->Sample(FWorldPlan::HeightChannel, x, z) + v.Weight(x, z) * v.Profile(x, z, true, steep) - RaiseRaster.Sample(0, x, z);
}

double FVolcanoes::Ground(double x, double z, double minWavelength, double& rock, double& scree) const
{
	rock = 0; scree = 0;
	if (Volcanoes.empty() || !Macro) return 0;
	double g = 0;
	for (const FVolcano& v : Volcanoes)
	{
		double dx = x - v.X, dz = z - v.Z, r = std::sqrt(dx * dx + dz * dz);
		if (v.bDetailed && r < v.Radius * 1.4 + 800)
		{
			double nearLand = DetMath::Smooth((Macro->Sample(FWorldPlan::LandChannel, x, z) - .2) / .3);
			g += v.Detail(x, z, r, nearLand, rock);
		}
		if (r >= v.Radius * 1.1 + Macro->Cell * 2) continue;
		double fade = DetMath::Smooth((Macro->Sample(FWorldPlan::LandChannel, x, z) - .2) / .3);
		double steep;
		double exact = v.Profile(x, z, true, steep);
		g += v.Weight(x, z) * fade * exact - RaiseRaster.Sample(0, x, z);
		rock = Mx(rock, steep * fade);
		if (v.bCaldera || r >= v.Radius) continue;
		double angle = std::atan2(dz, dx), u = r / v.Radius;
		// Gullies: V-shaped channels round the cone, from below the crater to the lower slopes.
		double zone = DetMath::Smooth((u - .06) / .08) * (1 - DetMath::Smooth((u - .6) / .3));
		if (v.bTuned)
		{
			// Revision 29: gullies of different depths and lengths, a second, shallower set between them lower down.
			if (u > .05 && u < .95)
			{
				int count = 16 + (int)(v.Seed % 11);
				double warp = 3 * Noise::Fbm(x, z, 2200, 2, 0, v.Seed + 13);
				double depthVary = DetMath::Smooth(.45 + 1.3 * Noise::Fbm(x, z, 2600, 2, 0, v.Seed + 14));
				double reach = .4 + .35 * DetMath::Smooth(.5 + Noise::Fbm(x, z, 3000, 2, 0, v.Seed + 15));
				double z1 = DetMath::Smooth((u - .06) / .08) * (1 - DetMath::Smooth((u - reach) / .2));
				double g1 = DetMath::Smooth((std::cos(count * angle + warp) - .5) / .5) * depthVary * z1 * (8 + 26 * DetMath::Smooth(1 - u / .6));
				double z2 = DetMath::Smooth((u - .25) / .1) * (1 - DetMath::Smooth((u - .85) / .1));
				double g2 = DetMath::Smooth((std::cos(count * 1.7 * angle + warp * 1.3 + 1.1) - .6) / .4) * z2 * 9
					* DetMath::Smooth(.5 + Noise::Fbm(x, z, 1700, 2, 0, v.Seed + 16));
				g -= Mx(g1, g2) * fade;
			}
		}
		else if (zone > 0)
		{
			int count = 18 + (int)(v.Seed % 13);
			double phase = count * angle + 2.2 * Noise::Fbm(x, z, 1800, 2, 0, v.Seed + 13);
			double gully = DetMath::Smooth((std::cos(phase) - .45) / .55) * (.6 + .4 * Noise::Fbm(x, z, 900, 2, 0, v.Seed + 14));
			g -= (10 + 20 * DetMath::Smooth(1 - u / .6)) * Mx(0, gully) * zone * fade;
		}
		// Lava flows of revision 28: lobes straight down the flanks, each along a wavering line.
		if (v.bTuned) continue;
		for (const FVolcano::FRadialFlow& flow : v.Flows)
		{
			if (u < flow.From - .02 || u > flow.To + .05) continue;
			double a = flow.Angle + .25 * Noise::Fbm(r, flow.Angle * 1000, v.Radius * .25, 2, 0, v.Seed + 9);
			double off = std::fabs(std::remainder(angle - a, VTau)) * r;
			double w = flow.Width * (.6 + .8 * (u - flow.From) / Mx(.05, flow.To - flow.From)); // (widening downhill)
			double across = 1 - DetMath::Smooth((off - w * .5) / (w * .25));
			if (across <= 0) continue;
			double ends = DetMath::Smooth((u - flow.From) / .03) * (1 - DetMath::Smooth((u - flow.To) / .04));
			double lobe = flow.Thickness * across * ends * (1 + .3 * Noise::Fbm(x, z, 120, 2, 0, v.Seed + 11));
			g += lobe * fade;
			rock = Mx(rock, .9 * across * ends * fade);
		}
	}
	if (Revision >= 29) g += TracedFlows(x, z, rock);
	return g;
}

/// Lava flows (revision 29): raised lobes along lines traced down the ground from vents below the summit.
double FVolcanoes::TracedFlows(double x, double z, double& rock) const
{
	double g = 0, cut = 0;
	for (const FVolcano& v : Volcanoes)
	{
		if (!v.bTraced || x < v.FlowMinX || x > v.FlowMaxX || z < v.FlowMinZ || z > v.FlowMaxZ) continue;
		for (const FFlow& f : v.Traced)
		{
			if (x < f.MinX || x > f.MaxX || z < f.MinZ || z > f.MaxZ) continue;
			double best, width, along, dirX, dirZ;
			if (!f.Nearest(x, z, best, width, along, dirX, dirZ)) continue;
			double edge = width * .5 * (1 + .25 * Noise::Fbm(x, z, 160, 2, 0, f.Seed));
			double across = 1 - DetMath::Smooth((best - edge * .7) / (edge * .3 + 1));
			// (revision 32, a spilling flow: no lobe in the lake or the notch, growing in below it)
			if (f.bSpill) across *= DetMath::Smooth((std::sqrt(Sq(x - v.X) + Sq(z - v.Z)) - v.Inner * .95) / (v.Inner * .35));
			double lobe = 0;
			if (across > 0)
			{
				lobe = f.Thickness * across * (1 + .3 * Noise::Fbm(x, z, 120, 2, 0, f.Seed + 1));
				if (Revision >= 30) lobe += across * 3 * (1 - std::fabs(Noise::Fbm(x, z, 45, 2, 0, f.Seed + 2))); // (a blocky crust)
				g = Mx(g, lobe);
				rock = Mx(rock, .9 * across);
			}
			if (f.bSpill)
			{
				// (revision 32: the notch it leaves the lake by, its walls steep)
				double wall = 1 - DetMath::Smooth((best - width * .5) / (width * .45 + 1));
				if (wall > 0)
				{
					double floor = f.SpillLevel - .04 * along * ((int)f.X.size() - 1) * 40 - lobe;
					double deep = Ground0(v, x, z) - floor;
					if (deep > 0) { cut = Mx(cut, deep * wall); rock = Mx(rock, .9 * wall); }
				}
			}
		}
	}
	return g - cut;
}

/// Once the plan is final: traces every lava flow and reads the crater and lava lakes' levels.
void FVolcanoes::Finalize()
{
	if (Volcanoes.empty() || !Macro) return;
	double steepUnused;
	for (FVolcano& v : Volcanoes)
	{
		double floor = Macro->Sample(FWorldPlan::HeightChannel, v.X, v.Z) + v.Profile(v.X, v.Z, true, steepUnused) - RaiseRaster.Sample(0, v.X, v.Z);
		v.LakeLevel = floor + .4 * v.Depth;
		v.LavaLevel = floor + .3 * v.Depth;
		if (v.bActive)
		{
			double reach = v.Radius * .1 + v.Inner * 2;
			for (const FVolcano::FFlowStart& s : v.FlowStarts) reach = Mx(reach, v.Inner * 1.4 + v.Radius * .05 + s.Length + s.Width * 2);
			LavaBoxes.push_back({ v.X - reach, v.Z - reach, v.X + reach, v.Z + reach });
		}
	}
	if (Revision < 29) return;
	for (FVolcano& v : Volcanoes)
	{
		if (!v.bTuned || v.bCaldera) continue;
		v.bTraced = true;
		for (int n = 0; n < (int)v.FlowStarts.size(); n++)
		{
			double angle = v.FlowStarts[n].Angle, length = v.FlowStarts[n].Length, width = v.FlowStarts[n].Width, thickness = v.FlowStarts[n].Thickness;
			u64 seed = v.Seed + 100 + (u64)n;
			// Revision 32: an active cone's fresh flows leave the lava lake over the lowest stretch of the rim within
			// 60 degrees of their direction, through a notch, half as wide there as below.
			bool spill = Revision >= 32 && v.bActive && n < v.FreshFlows;
			if (spill)
			{
				double low = std::numeric_limits<double>::max(), pick = angle;
				for (int k = -8; k <= 8; k++)
				{
					double a = angle + k * VPi / 24, h = Ground0(v, v.X + std::cos(a) * v.Inner, v.Z + std::sin(a) * v.Inner);
					if (h < low) { low = h; pick = a; }
				}
				angle = pick;
			}
			double vr = spill ? v.Inner * .5 : v.Inner * 1.4 + v.Radius * .05;
			double x = v.X + std::cos(angle) * vr, z = v.Z + std::sin(angle) * vr, dx = std::cos(angle), dz = std::sin(angle);
			std::vector<double> xs{ x }, zs{ z }, ws{ width * (spill ? .45 : 1) };
			int flat = 0;
			for (double run = 0; run < length; run += 40)
			{
				double gx = (Ground0(v, x + 20, z) - Ground0(v, x - 20, z)) / 40, gz = (Ground0(v, x, z + 20) - Ground0(v, x, z - 20)) / 40;
				double grade = std::sqrt(gx * gx + gz * gz);
				double r = std::sqrt(Sq(x - v.X) + Sq(z - v.Z));
				double turn;
				if (Revision >= 32)
				{
					// (down the fall line on a steep flank, wandering only where the ground flattens)
					double steep = DetMath::Smooth(grade / .15), pull = .35 + .5 * steep;
					if (spill && r < v.Inner * 1.2) { dx = std::cos(angle); dz = std::sin(angle); } // (out through the notch)
					else if (grade > 1e-6) { dx = dx * (1 - pull) - gx / grade * pull; dz = dz * (1 - pull) - gz / grade * pull; }
					turn = (spill && r < v.Inner * 1.2 ? 0 : .12 * (1 - .7 * steep)) * Noise::Fbm(run, n * 977, 600, 2, 0, seed);
				}
				else
				{
					if (grade > 1e-6) { dx = dx * .6 - gx / grade * .4; dz = dz * .6 - gz / grade * .4; }
					turn = .35 * Noise::Fbm(run, n * 977, 600, 2, 0, seed);
				}
				double c = std::cos(turn), s = std::sin(turn), l = Mx(1e-9, std::sqrt(dx * dx + dz * dz));
				double nx = (dx * c - dz * s) / l, nz = (dx * s + dz * c) / l;
				x += nx * 40; z += nz * 40;
				if (Macro->Sample(FWorldPlan::LandChannel, x, z) < .25) break; // (the sea)
				flat = grade < .02 && !(spill && r < v.Inner * 1.2) ? flat + 1 : 0; // (not in the crater it spills from)
				double narrow = spill ? .45 + .55 * DetMath::Smooth((std::sqrt(Sq(x - v.X) + Sq(z - v.Z)) - v.Inner * 1.1) / (v.Inner * .6)) : 1;
				xs.push_back(x); zs.push_back(z); ws.push_back(width * narrow * (1 + run / Mx(1, length)) * (flat > 0 ? 1.5 : 1));
				if (flat > 6) break; // (spread out on the flat)
			}
			if (xs.size() < 5) continue;
			ws.back() *= 1.3;
			FFlow f;
			int points = (int)xs.size();
			f.Chunks.resize((points - 1 + 9) / 10);
			double pad = width * 3;
			for (int c = 0; c < (int)f.Chunks.size(); c++)
			{
				double x0 = std::numeric_limits<double>::max(), z0 = x0, x1 = std::numeric_limits<double>::lowest(), z1 = x1;
				for (int i = c * 10; i <= MnI(points - 1, c * 10 + 10); i++)
				{
					x0 = Mn(x0, xs[i]); z0 = Mn(z0, zs[i]); x1 = Mx(x1, xs[i]); z1 = Mx(z1, zs[i]);
				}
				f.Chunks[c] = { x0 - pad, z0 - pad, x1 + pad, z1 + pad };
			}
			f.X = xs; f.Z = zs; f.W = ws; f.Thickness = thickness; f.Seed = seed; f.bSpill = spill;
			if (spill) f.SpillLevel = v.LavaLevel; // (as the lava lake's level)
			f.MinX = std::numeric_limits<double>::max(); f.MinZ = f.MinX; f.MaxX = std::numeric_limits<double>::lowest(); f.MaxZ = f.MaxX;
			for (const FFlow::FBox& b : f.Chunks) { f.MinX = Mn(f.MinX, b.MinX); f.MinZ = Mn(f.MinZ, b.MinZ); f.MaxX = Mx(f.MaxX, b.MaxX); f.MaxZ = Mx(f.MaxZ, b.MaxZ); }
			v.FlowMinX = Mn(v.FlowMinX, f.MinX); v.FlowMinZ = Mn(v.FlowMinZ, f.MinZ);
			v.FlowMaxX = Mx(v.FlowMaxX, f.MaxX); v.FlowMaxZ = Mx(v.FlowMaxZ, f.MaxZ);
			v.Traced.push_back(std::move(f));
		}
	}
}

/// Once the hydrology has made the caldera lakes (revision 29): each island stands over its lake's real level.
void FVolcanoes::SetLakes(const FHydrology& hy, double sea)
{
	for (FVolcano& v : Volcanoes)
	{
		if (!v.bCaldera || v.IslandRadius <= 0) continue;
		int lake = hy.LakeNear(v.X, v.Z);
		if (lake < 0) continue;
		v.IslandHeight = hy.Lakes[lake].Level - sea - v.Base - (v.Height - v.Depth) + v.IslandAbove;
	}
}

bool FVolcanoes::InCaldera(double x, double z, double& cx, double& cz) const
{
	cx = cz = 0;
	for (const FVolcano& v : Volcanoes)
	{
		if (!v.bCaldera || !v.bTuned || Sq(x - v.X) + Sq(z - v.Z) > Sq(v.Inner * 1.15)) continue;
		cx = v.X; cz = v.Z;
		return true;
	}
	return false;
}

bool FVolcanoes::NearLava(double x, double z) const
{
	for (const FBox& b : LavaBoxes) if (x >= b.X0 && x <= b.X1 && z >= b.Z0 && z <= b.Z1) return true;
	return false;
}

/// An active cone's lava lake (revision 31): flat molten rock within 85 % of the crater's radius.
bool FVolcanoes::LavaLake(double x, double z, double ground, double& level) const
{
	level = NaNValue();
	if (Volcanoes.empty() || !Macro || !NearLava(x, z)) return false;
	for (const FVolcano& v : Volcanoes)
	{
		if (!v.bActive) continue;
		double dx = x - v.X, dz = z - v.Z;
		if (dx * dx + dz * dz > Sq(v.Inner * .85)) continue;
		if (ground < v.LavaLevel) { level = v.LavaLevel; return true; }
	}
	return false;
}

/// How molten and how crusted the ground is here, how much lava covers it, and which way it moves (revision 31).
FVolcanoes::FLava FVolcanoes::LavaFlow(double x, double z) const
{
	FLava out;
	if (Volcanoes.empty() || Revision < 31 || !NearLava(x, z)) return out;
	double molten = 0, crust = 0, cover = 0, dirX = 0, dirZ = 0, lead = 0;
	for (const FVolcano& v : Volcanoes)
	{
		if (!v.bActive) continue;
		double r = std::sqrt(Sq(x - v.X) + Sq(z - v.Z));
		if (r < v.Inner)
		{
			// (the lake: where the crater's exact bowl lies below the lake's level, 30 % of the depth over its floor)
			double steep;
			double bowl = v.Profile(x, z, true, steep);
			double lake = DetMath::Smooth((v.Height - .7 * v.Depth - bowl) / 3);
			molten = Mx(molten, lake);
			if (lake > lead && r > 1e-6) { lead = lake; double outward = .3 * Mn(1, r / (v.Inner * .55)); dirX = (x - v.X) / r * outward; dirZ = (z - v.Z) / r * outward; }
		}
		if (!v.bTraced || x < v.FlowMinX || x > v.FlowMaxX || z < v.FlowMinZ || z > v.FlowMaxZ) continue;
		for (int n = 0; n < MnI(v.FreshFlows, (int)v.Traced.size()); n++)
		{
			const FFlow& f = v.Traced[n];
			double best, width, along, tx, tz;
			if (x < f.MinX || x > f.MaxX || z < f.MinZ || z > f.MaxZ || !f.Nearest(x, z, best, width, along, tx, tz)) continue;
			double heat = 1 - .65 * along;
			double across = 1 - DetMath::Smooth((best - width * .35) / (width * .15 + 1));
			double core = (1 - DetMath::Smooth((best - width * .12) / (width * .16 + 1))) * (.55 + .45 * (1 - DetMath::Smooth(best / (width * .14 + 1))));
			crust = Mx(crust, across * heat);
			cover = Mx(cover, across);
			molten = Mx(molten, core * heat * (1 - DetMath::Smooth((along - .85) / .15)));
			if (across * heat > lead) { lead = across * heat; dirX = tx * heat; dirZ = tz * heat; }
		}
	}
	out.Molten = (float)molten; out.Crust = (float)crust; out.Cover = (float)Mx(cover, molten); out.DirX = (float)dirX; out.DirZ = (float)dirZ;
	return out;
}

/// A quiet cone's crater lake (revision 28): water within 85 % of the crater's radius (level absolute).
bool FVolcanoes::CraterLake(double x, double z, double ground, double& level) const
{
	level = NaNValue();
	if (Volcanoes.empty() || !Macro) return false;
	for (const FVolcano& v : Volcanoes)
	{
		if (!v.bLake) continue;
		double dx = x - v.X, dz = z - v.Z;
		if (dx * dx + dz * dz > Sq(v.Inner * .85)) continue;
		if (ground < v.LakeLevel) { level = Sea + v.LakeLevel; return true; }
	}
	return false;
}

void FVolcanoes::Cones(const std::function<void(double, double, double, bool, bool)>& Visit) const
{
	for (const FVolcano& v : Volcanoes) Visit(v.X, v.Z, v.Radius, v.bCaldera, v.bActive);
}
}
