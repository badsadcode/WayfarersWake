// Port of Thornwake Game/WorldGen: LandformModules.cs, Escarpments.cs.
#include "WwLandforms.h"

namespace ww
{
uint64_t LandformSeed(const FWorldManifest& Manifest, const char* Id)
{
	return Hashing::FeatureSeed(Manifest.Seed, 5, Manifest.ProfileDigest, std::string("landform.") + Id);
}

// ------------------------------------------------------------------------------------------------ escarpments
namespace
{
const double EscStep = 2000, EscGridCell = 2000;
const double Tau = 6.283185307179586, PiD = 3.141592653589793;

struct FEscLineView { const std::vector<double>& X; const std::vector<double>& Z; const std::vector<double>& Along; };

/// Nearest point of the line: signed distance (positive on the plateau side, left of the line), position along it,
/// unit normal toward the plateau.
void Nearest(const FEscLineView& l, double x, double z, double& s, double& t, double& nx, double& nz)
{
	double best = std::numeric_limits<double>::max(); s = 0; t = 0; nx = 0; nz = 1;
	for (int i = 0; i + 1 < (int)l.X.size(); i++)
	{
		double ax = l.X[i], az = l.Z[i], dx = l.X[i + 1] - ax, dz = l.Z[i + 1] - az, len2 = dx * dx + dz * dz;
		double u = DetMath::Clamp(((x - ax) * dx + (z - az) * dz) / len2, 0, 1);
		double px = ax + dx * u - x, pz = az + dz * u - z, d2 = px * px + pz * pz;
		if (d2 >= best) continue;
		best = d2;
		double len = std::sqrt(len2), cross = (dx * (z - az) - dz * (x - ax)) / len;
		nx = -dz / len; nz = dx / len;
		s = cross >= 0 ? std::sqrt(d2) : -std::sqrt(d2);
		t = l.Along[i] + u * len;
	}
}

/// Nearest blended over the segments that are almost as near (within 60 m): smooth through the line's bends (revision 19).
void NearestSmooth(const FEscLineView& l, double x, double z, double& s, double& t, double& nx, double& nz)
{
	const double Blend = 60;
	double best = std::numeric_limits<double>::max();
	for (int i = 0; i + 1 < (int)l.X.size(); i++)
	{
		double ax = l.X[i], az = l.Z[i], dx = l.X[i + 1] - ax, dz = l.Z[i + 1] - az, len2 = dx * dx + dz * dz;
		double u = DetMath::Clamp(((x - ax) * dx + (z - az) * dz) / len2, 0, 1);
		best = Mn(best, std::sqrt(Sq(ax + dx * u - x) + Sq(az + dz * u - z)));
	}
	double ws = 0, ss = 0, ts = 0, ns = 0, ms = 0;
	for (int i = 0; i + 1 < (int)l.X.size(); i++)
	{
		double ax = l.X[i], az = l.Z[i], dx = l.X[i + 1] - ax, dz = l.Z[i + 1] - az, len2 = dx * dx + dz * dz;
		double u = DetMath::Clamp(((x - ax) * dx + (z - az) * dz) / len2, 0, 1);
		double dist = std::sqrt(Sq(ax + dx * u - x) + Sq(az + dz * u - z));
		double w = DetMath::Smooth(1 - (dist - best) / Blend);
		if (w <= 0) continue;
		double len = std::sqrt(len2), cross = (dx * (z - az) - dz * (x - ax)) / len;
		ws += w; ss += w * cross; ts += w * (l.Along[i] + u * len); ns += w * -dz / len; ms += w * dx / len;
	}
	s = ss / ws; t = ts / ws;
	double nl = std::sqrt(ns * ns + ms * ms);
	nx = ns / nl; nz = ms / nl;
}
}

void FEscarpments::Shape(FMacroRaster& InMacro, const FWorldManifest& Manifest, FLandformSetting Setting)
{
	Macro = &InMacro; Ox = Manifest.OriginX; Oz = Manifest.OriginZ;
	Place(InMacro, Manifest, Setting);
	if (Lines.empty()) return;
	// The raster form of the profile: the face spread over 2.5 cells, so the bicubic raster holds it without ringing.
	ProfileRaster = FMacroRaster(InMacro.Size, InMacro.MinX, InMacro.MinZ, InMacro.Cell, 1);
	double face = InMacro.Cell * 2.5, mw = InMacro.Cell * 2;
	int size = InMacro.Size;
	std::vector<float>& raised = ProfileRaster.Channel(0);
	ParallelFor(size, [&](int j)
	{
		for (int i = 0; i < size; i++)
		{
			double x = ProfileRaster.CellX(i), z = ProfileRaster.CellZ(j), v = 0, s, nx, nz, rock, fallen;
			for (const FLine& l : Lines)
				if (x >= l.MinX - l.Depth && x <= l.MaxX + l.Depth && z >= l.MinZ - l.Depth && z <= l.MaxZ + l.Depth)
					v = Mx(v, Profile(l, x, z, face, mw, false, s, nx, nz, rock, fallen));
			raised[j * size + i] = (float)v;
		}
	});
	const std::vector<float>& land = InMacro.Channel(FWorldPlan::LandChannel);
	std::vector<float>& height = InMacro.Channel(FWorldPlan::HeightChannel);
	for (size_t k = 0; k < raised.size(); k++)
		if (raised[k] > 0) height[k] += (float)(raised[k] * DetMath::Smooth((land[k] - .2) / .3));
	BuildGrid(InMacro);
}

void FEscarpments::Place(const FMacroRaster& M, const FWorldManifest& Manifest, FLandformSetting Setting)
{
	FRandomStream rng(LandformSeed(Manifest, "escarpments"));
	const std::vector<float>& land = M.Channel(FWorldPlan::LandChannel);
	const std::vector<float>& mountain = M.Channel(FWorldPlan::MountainChannel);
	int landCells = 0;
	std::vector<int> starts; // (inland, off the mountain cores)
	for (int c = 0; c < (int)land.size(); c++)
	{
		if (land[c] > .5f) landCells++;
		if (land[c] >= .6f && mountain[c] <= .3f) starts.push_back(c);
	}
	double landKm2 = landCells * M.Cell * M.Cell / 1e6, world = Manifest.Profile.WorldSize;
	int want = ClampI((int)std::nearbyint(Setting.Amount * landKm2 / (100 * 100)), landKm2 > 3000 ? 1 : 0, 64);
	auto LandAt = [&](double x, double z) { return M.Sample(FWorldPlan::LandChannel, x, z); };
	auto MountainAt = [&](double x, double z) { return M.Sample(FWorldPlan::MountainChannel, x, z); };
	for (int attempt = 0; attempt < want * 40 && (int)Lines.size() < want && !starts.empty(); attempt++)
	{
		int k = starts[rng.Int((int)starts.size())];
		double x = M.CellX(k % M.Size), z = M.CellZ(k / M.Size);
		double length = Mn(rng.Between(20000, 70000) * Setting.Size, world * .35);
		double height = rng.Between(60, 180) * Setting.Size, depth = rng.Between(8000, 20000) * std::sqrt(Setting.Size);
		double angle = rng.Unit() * Tau, side = rng.Unit() < .5 ? -1 : 1, turn = rng.Between(-.06, .06);
		u64 seed = rng.Next();
		// The face looks downhill: the plateau is the side the land already rises toward.
		double ux = -std::sin(angle), uz = std::cos(angle);
		double rise = M.Sample(FWorldPlan::HeightChannel, x + ux * 8000, z + uz * 8000) - M.Sample(FWorldPlan::HeightChannel, x - ux * 8000, z - uz * 8000);
		if (std::fabs(rise) > 5) side = rise > 0 ? 1 : -1;
		std::vector<double> xs{ x }, zs{ z };
		for (double run = 0; run < length; run += EscStep)
		{
			turn = DetMath::Clamp(turn + rng.Between(-.06, .06), -.12, .12); // (gentle bends: the plateau behind stays whole)
			angle += turn;
			double nx = x + std::cos(angle) * EscStep, nz = z + std::sin(angle) * EscStep;
			double px = -std::sin(angle) * side, pz = std::cos(angle) * side; // toward the plateau
			// The edge stays inland on both sides; the plateau stays land a good way back; no mountains; apart.
			if (LandAt(nx, nz) < .55 || MountainAt(nx, nz) > .4 || LandAt(nx - px * 3000, nz - pz * 3000) < .5 ||
				LandAt(nx + px * 3000, nz + pz * 3000) < .5 || LandAt(nx + px * depth * .6, nz + pz * depth * .6) < .4 || Near(nx, nz, 15000))
				break;
			x = nx; z = nz; xs.push_back(x); zs.push_back(z);
		}
		if ((xs.size() - 1) * EscStep < 15000) continue;
		std::vector<double> along(xs.size(), 0.0);
		for (size_t i = 1; i < xs.size(); i++) along[i] = along[i - 1] + std::sqrt(Sq(xs[i] - xs[i - 1]) + Sq(zs[i] - zs[i - 1]));
		// Store the line with the plateau on its left (positive signed distance), whichever side was drawn.
		if (side < 0)
		{
			std::reverse(xs.begin(), xs.end()); std::reverse(zs.begin(), zs.end());
			double last = along.back();
			for (double& a : along) a = last - a;
			std::reverse(along.begin(), along.end());
		}
		FLine line;
		line.X = xs; line.Z = zs; line.Along = along; line.Length = along.back(); line.Height = height; line.Depth = depth; line.Seed = seed;
		line.MinX = line.MaxX = line.X[0]; line.MinZ = line.MaxZ = line.Z[0];
		for (size_t i = 1; i < line.X.size(); i++)
		{
			line.MinX = Mn(line.MinX, line.X[i]); line.MaxX = Mx(line.MaxX, line.X[i]);
			line.MinZ = Mn(line.MinZ, line.Z[i]); line.MaxZ = Mx(line.MaxZ, line.Z[i]);
		}
		Lines.push_back(std::move(line));
	}
}

bool FEscarpments::Near(double x, double z, double distance) const
{
	for (const FLine& l : Lines)
		for (size_t i = 0; i < l.X.size(); i++)
			if (Sq(l.X[i] - x) + Sq(l.Z[i] - z) < distance * distance) return true;
	return false;
}

int FEscarpments::Cell(double v, double min) const { return ClampI((int)std::floor((v - min) / EscGridCell), 0, GridSize - 1); }

void FEscarpments::BuildGrid(const FMacroRaster& M)
{
	double world = M.Size * M.Cell, reach = M.Cell * 2.5 + 400;
	GridMinX = M.MinX; GridMinZ = M.MinZ;
	GridSize = (int)std::ceil(world / EscGridCell);
	std::vector<std::vector<int>> cells((size_t)GridSize * GridSize);
	for (int n = 0; n < (int)Lines.size(); n++)
	{
		const FLine& l = Lines[n];
		FEscLineView view{ l.X, l.Z, l.Along };
		int i0 = Cell(l.MinX - reach, GridMinX), i1 = Cell(l.MaxX + reach, GridMinX), j0 = Cell(l.MinZ - reach, GridMinZ), j1 = Cell(l.MaxZ + reach, GridMinZ);
		for (int j = j0; j <= j1; j++)
			for (int i = i0; i <= i1; i++)
			{
				double cx = GridMinX + (i + .5) * EscGridCell, cz = GridMinZ + (j + .5) * EscGridCell, s, t, nx, nz;
				Nearest(view, cx, cz, s, t, nx, nz);
				if (std::fabs(s) > reach + EscGridCell * .71) continue;
				cells[(size_t)j * GridSize + i].push_back(n);
			}
	}
	GridStart.assign(cells.size() + 1, 0);
	GridLines.clear();
	for (size_t c = 0; c < cells.size(); c++) { GridStart[c] = (int)GridLines.size(); GridLines.insert(GridLines.end(), cells[c].begin(), cells[c].end()); }
	GridStart[cells.size()] = (int)GridLines.size();
}

double FEscarpments::Ground(double x, double z, double minWavelength, double& rock, double& scree) const
{
	rock = 0; scree = 0;
	if (Lines.empty()) return 0;
	int c = Cell(z, GridMinZ) * GridSize + Cell(x, GridMinX);
	int from = GridStart[c], to = GridStart[c + 1];
	if (from == to) return 0;
	double cell = Macro->Cell, best = 0, bestAbs = std::numeric_limits<double>::max(), nx = 0, nz = 0, bare = 0, debris = 0;
	for (int n = from; n < to; n++)
	{
		const FLine& l = Lines[GridLines[n]];
		double s, ux, uz, r, sc;
		double v = Profile(l, x, z, Mx(12 + l.Height * .45, minWavelength * .6), minWavelength, true, s, ux, uz, r, sc);
		if (std::fabs(s) < bestAbs) { bestAbs = std::fabs(s); nx = ux; nz = uz; }
		if (v > best) best = v;
		bare = Mx(bare, r); debris = Mx(debris, sc);
	}
	double reach = DetMath::Smooth((cell * 2.25 - bestAbs) / (cell * .5));
	if (reach <= 0) return 0;
	double correction = best - ProfileRaster.Sample(0, x, z);
	// How much of the step survives erosion here: the eroded height across the edge against the step put in.
	double d = cell * 1.5;
	double topX = x + nx * d, topZ = z + nz * d, footX = x - nx * d, footZ = z - nz * d;
	double put = ProfileRaster.Sample(0, topX, topZ) - ProfileRaster.Sample(0, footX, footZ);
	double kept = Macro->Sample(FWorldPlan::HeightChannel, topX, topZ) - Macro->Sample(FWorldPlan::HeightChannel, footX, footZ);
	double survive = put > 1 ? DetMath::Smooth((kept / put - .3) / .5) : 0;
	rock = bare * reach * survive;
	if (Revision >= 33) scree = debris / .65 * reach * survive;
	return correction * reach * survive;
}

double FEscarpments::Profile(const FLine& l, double x, double z, double face, double minWavelength, bool detail, double& s, double& nx, double& nz,
	double& rock, double& fallen) const
{
	rock = 0; fallen = 0;
	double t;
	FEscLineView view{ l.X, l.Z, l.Along };
	if (Revision >= 19) NearestSmooth(view, x, z, s, t, nx, nz);
	else Nearest(view, x, z, s, t, nx, nz);
	double lx = x - Ox, lz = z - Oz;
	s += 90 * Noise::Fbm(lx, lz, 700, 3, minWavelength, l.Seed) + 260 * Noise::Fbm(lx, lz, 4000, 2, minWavelength, l.Seed + 1);
	double ramp = l.Length * .18;
	double taper = DetMath::Smooth(t / ramp) * DetMath::Smooth((l.Length - t) / ramp);
	// (features fade out on coarse tiles: they are tens of metres wide)
	double feat = detail ? 1 - DetMath::Smooth((minWavelength - 40) / 120) : 0;
	if (taper <= 0 || s < -face - feat * (l.Height * 3.5 + 400)) return 0;
	double h = l.Height * taper * (.7 + .6 * DetMath::Smooth(.5 + Noise::Fbm(t, 0, 9000, 2, 0, l.Seed + 2)));
	double back = s <= l.Depth * .55 ? 1 : 1 - DetMath::Smooth((s - l.Depth * .55) / (l.Depth * .45));
	double plain = h * DetMath::Smooth((s + face * .5) / face) * back;
	if (feat <= 0) return plain;
	if (Revision >= 19)
	{
		double v19 = Detail(l, s, t, lx, lz, h, back, minWavelength, rock, fallen);
		rock *= feat; fallen *= feat;
		return plain + (v19 - plain) * feat;
	}

	// Revision 18's face. The rim, warped at several scales.
	double wx = 70 * Noise::Fbm(lx, lz, 320, 2, minWavelength, l.Seed + 8);
	double e = s + 38 * Noise::Fbm(lx + wx, lz - wx, 190, 3, minWavelength, l.Seed + 7);
	// Ravines and spurs, one candidate per 200 m of edge; sizes skewed (many small, a few big).
	const double Spacing = 200;
	i64 c0 = (i64)std::floor(t / Spacing);
	double shift = 0, ravine = 0, floor = 0, fan = 0;
	for (i64 c = c0 - 2; c <= c0 + 2; c++)
	{
		u64 hash = Hashing::Lattice(c, 7, l.Seed + 5);
		double kind = Hashing::Unit(hash), size = Hashing::Unit(Hashing::Mix(hash + 3)), width = Hashing::Unit(Hashing::Mix(hash + 2));
		double tc = (c + .5 * Hashing::Unit(Hashing::Mix(hash + 1)) + .25) * Spacing;
		if (kind < .5)
		{
			double depth = (50 + 420 * size * size) * std::sqrt(Mn(1, h / 90)), w0 = 18 + .42 * depth * (.6 + .8 * width);
			double bend = .35 * depth * Noise::Fbm(Mx(s, 0), c * 31.7, depth * .8 + 40, 2, 0, l.Seed + 11);
			double a = std::fabs(t - tc - bend), into = DetMath::Clamp(e / depth, 0, 1), half = w0 * (1 - .65 * into) + 5;
			if (e > -10 && a < half)
			{
				double inside = std::pow(DetMath::Smooth((1 - a / half) * 1.25), .8) * DetMath::Smooth((depth - e) / (depth * .3 + 10));
				if (inside > ravine) { ravine = inside; floor = .92 * std::pow(into, 1.25); }
			}
			fan = Mx(fan, DetMath::Smooth((w0 * 1.6 - std::fabs(t - tc)) / (w0 * 1.1)) * Mn(1, depth / 200));
		}
		else if (kind < .78)
		{
			double jut = (15 + 120 * std::pow(size, 1.6)) * Mn(1, h / 80), half = 30 + 110 * width;
			double a = std::fabs(t - tc);
			shift = Mx(shift, jut * DetMath::Smooth((half - a) / (half * .7)));
		}
	}
	e += shift;
	double rim = h * (1 + .05 * Noise::Fbm(lx, lz, 140, 2, minWavelength, l.Seed + 4));
	double cap = h * (.25 + .25 * DetMath::Smooth(.5 + 1.3 * Noise::Fbm(t, 3, 650, 2, 0, l.Seed + 9)));
	double cw = Mx(3, cap * (.08 + .14 * DetMath::Smooth(.5 + 1.3 * Noise::Fbm(t, 4, 380, 2, 0, l.Seed + 12))));
	double debris = rim - cap * (1 + .12 * fan);
	double dw = 1.75 * debris * (1 + .9 * fan) / .9;
	double v;
	if (e >= 0) v = rim * back;
	else if (e >= -cw)
	{
		double f = -e / cw;
		v = (rim - (rim - debris) * std::pow(f, .85)) * back; // (the lip rounds a little)
		rock = 1;
	}
	else
	{
		double d = (-e - cw) / dw;
		if (d >= 1) v = 0;
		else
		{
			v = debris * std::pow(1 - d, 1.7) * back;
			double g = Noise::Fbm(t + 25 * Noise::Fbm(lx, lz, 90, 1, minWavelength, l.Seed + 13), -e * .22, 34, 2, minWavelength, l.Seed + 10);
			v -= debris * .14 * (1 - DetMath::Smooth(std::fabs(g) / .12)) * std::sin(PiD * d);
			v += 1.2 * Noise::Fbm(lx, lz, 7, 2, minWavelength, l.Seed + 6) * (1 - d);
			rock = .65 * (1 - DetMath::Smooth((d - .55) / .4));
		}
	}
	if (ravine > 0)
	{
		double fl = (debris * (1 - .3 * ravine) + (rim - debris) * floor) * back;
		if (e >= -cw && fl < v) { v += (fl - v) * ravine; rock = Mx(rock, .8 * ravine); }
	}
	v = Mx(v, 0);
	rock *= feat;
	return plain + (v - plain) * feat;
}

double FEscarpments::Detail(const FLine& l, double s, double t, double lx, double lz, double h, double back, double minWavelength, double& rock,
	double& fallen) const
{
	double wx = 80 * Noise::Fbm(lx, lz, 360, 3, minWavelength, l.Seed + 8, .55);
	double e = s + 42 * Noise::Fbm(lx + wx, lz - wx, 210, 2, minWavelength, l.Seed + 7, .55);
	double rim = h * (1 + .05 * Noise::Fbm(lx, lz, 140, 3, minWavelength, l.Seed + 4));
	double cap = h * (.22 + .25 * DetMath::Smooth(.5 + 1.3 * Noise::Fbm(t, 3, 650, 2, 0, l.Seed + 9)));
	double cw = Mx(Mx(4, minWavelength * 1.5), cap * (.3 + .25 * DetMath::Smooth(.5 + 1.3 * Noise::Fbm(t, 4, 380, 2, 0, l.Seed + 12))));
	// Spurs and ravines, one candidate per 170 m of edge, sizes skewed (many small, few big).
	const double Spacing = 170;
	i64 c0 = (i64)std::floor(t / Spacing);
	double shift = 0, fan = 0;
	double cut[9], floorAt[9];
	int ravines = 0;
	for (i64 c = c0 - 4; c <= c0 + 4; c++)
	{
		u64 hash = Hashing::Lattice(c, 9, l.Seed + 5);
		double kind = Hashing::Unit(hash), size = Hashing::Unit(Hashing::Mix(hash + 3)), width = Hashing::Unit(Hashing::Mix(hash + 2));
		double tc = (c + .2 + .6 * Hashing::Unit(Hashing::Mix(hash + 1))) * Spacing;
		if (kind < .5)
		{
			double depth = (40 + 460 * std::pow(size, 2.2)) * std::sqrt(Mn(1, h / 90)), w0 = 14 + (.25 + .35 * width) * depth;
			double into = DetMath::Clamp(e / depth, 0, 1);
			double bend = .45 * depth * Noise::Fbm(into * 2.5, c * 31.7, 1, 2, 0, l.Seed + 11) * into;
			double half = (w0 * (1 - .7 * into) + 4) * (1 + .3 * Noise::Fbm(lx, lz, 26, 2, minWavelength, l.Seed + 14));
			double lat = DetMath::Clamp(1 - std::fabs(t - tc - bend) / half, 0, 1);
			double weight = DetMath::Smooth(lat) * DetMath::Smooth((depth - e) / (depth * .35 + 12))
				* DetMath::Smooth((e + cw + 6) / 8);
			if (weight > 0) { cut[ravines] = weight; floorAt[ravines] = .92 * std::pow(into, 1.25); ravines++; }
			fan = Mx(fan, DetMath::Smooth((w0 * 1.6 - std::fabs(t - tc)) / (w0 * 1.1)) * Mn(1, depth / 200));
		}
		else if (kind < .78)
		{
			double jut = (12 + 130 * std::pow(size, 1.8)) * Mn(1, h / 80), half = 25 + 120 * width;
			shift = Mx(shift, jut * DetMath::Smooth((half - std::fabs(t - tc)) / (half * .7)));
		}
	}
	e += shift;
	// Outcrops and ledges: the face line itself is displaced a little around the cliff band.
	double band = DetMath::Smooth(1 - std::fabs(e + cw * .5) / cw);
	e += band * cw * .3 * Noise::Fbm(lx, lz, 16, 3, minWavelength, l.Seed + 15);
	double debris = rim - cap * (1 + .12 * fan);
	double dw = 1.75 * debris * (1 + .9 * fan) / .9;
	double v, d = 0;
	if (e >= 0) v = rim * back;
	else if (e >= -cw)
	{
		double f = -e / cw;
		v = (rim - (rim - debris) * (.6 * f + .4 * DetMath::Smooth(f))) * back;
	}
	else
	{
		d = (-e - cw) / dw;
		v = d >= 1 ? 0 : debris * std::pow(1 - d, 1.7) * back;
		if (d < 1)
		{
			// Rills straight down the slope at irregular spacing, deepest mid-slope; a little rubble.
			double u = t + 14 * Noise::Fbm(lx, lz, 110, 2, minWavelength, l.Seed + 13);
			i64 k0 = (i64)std::floor(u / 36);
			double rill = 0;
			for (i64 k = k0 - 1; k <= k0 + 1; k++)
			{
				u64 hk = Hashing::Lattice(k, 11, l.Seed + 10);
				double uc = (k + Hashing::Unit(hk)) * 36, wide = 5 + 9 * Hashing::Unit(Hashing::Mix(hk + 1));
				double deep = Mn(wide * .45, debris * (.02 + .05 * Hashing::Unit(Hashing::Mix(hk + 2)))), start = .35 * Hashing::Unit(Hashing::Mix(hk + 3));
				double across = DetMath::Clamp(1 - std::fabs(u - uc) / wide, 0, 1);
				rill = Mx(rill, deep * DetMath::Smooth(across) * DetMath::Smooth((d - start) / .12));
			}
			v -= rill * std::sin(PiD * d);
			v += 1.2 * Noise::Fbm(lx, lz, 7, 2, minWavelength, l.Seed + 6) * (1 - d) * DetMath::Smooth(d / .1);
		}
	}
	for (int r = 0; r < ravines; r++)
	{
		double fl = (debris + (rim - debris) * floorAt[r]) * back;
		if (fl < v) v += (fl - v) * cut[r];
	}
	v = Mx(v, 0);
	// How bare: cliff rock, scree on the debris slope (thinning out toward the plain), ravine walls.
	double cliff = DetMath::Smooth((e + cw + 4) / 8) * DetMath::Smooth((6 - e) / 6);
	double screeV = e < -cw && d < 1 ? .65 * (1 - DetMath::Smooth((d - .55) / .4)) * DetMath::Smooth((-e - cw) / 4 + 1) : 0;
	double ravineRock = 0;
	for (int r = 0; r < ravines; r++) ravineRock = Mx(ravineRock, .8 * cut[r]);
	rock = Mx(Mx(cliff, screeV), ravineRock);
	fallen = screeV;
	return v;
}
}
