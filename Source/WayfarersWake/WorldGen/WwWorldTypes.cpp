// Port of Thornwake Game/WorldGen: Heightmap.cs, Landforms.cs and the template and natural world types of WorldRecipe.cs.
// Every random draw happens in the source's order (including draws the source makes for values it then does not use).
#include "WwWorldTypes.h"
#include <cstdlib>
#include <deque>

namespace ww
{
// ------------------------------------------------------------------------------------------------ heightmap
namespace
{
const double BlobPower = .985, LinePower = .86, BlobFade = .35;
const char* const HeightTools[] = { "Hill", "Pit", "Range", "Trough", "Strait", "Mask", "Add", "Multiply", "Smooth", "Invert" };
const int HN = FHeightmap::N;

int HIndex(int i, int j) { return j * HN + i; }
double Lim(double h) { return DetMath::Clamp(h, 0, 100); }

/// Cell centre in world fractions (0..1); odd rows are offset by half a cell (hex grid).
void Centre(int i, int j, double& x, double& y) { x = (i + .5 + (j & 1) * .5) / HN; y = (j + .5) / HN; }

int Neighbours(int c, int* into)
{
	int i = c % HN, j = c / HN, n = 0;
	auto Add = [&](int a, int b) { if (a >= 0 && b >= 0 && a < HN && b < HN) into[n++] = HIndex(a, b); };
	int o = j & 1; // odd rows sit half a cell to the right
	Add(i - 1, j); Add(i + 1, j);
	Add(i - 1 + o, j - 1); Add(i + o, j - 1);
	Add(i - 1 + o, j + 1); Add(i + o, j + 1);
	return n;
}

int CellAt(double x, double y)
{
	int j = ClampI((int)(y * HN), 0, HN - 1);
	int i = ClampI((int)(x * HN - (j & 1) * .5), 0, HN - 1);
	return HIndex(i, j);
}

/// "a-b" (a leading '-' is a sign) or a single value.
void ParseRange(const std::string& s, double& a, double& b)
{
	size_t dash = s.find('-', 1);
	if (dash == std::string::npos) { a = b = std::strtod(s.c_str(), nullptr); return; }
	a = std::strtod(s.substr(0, dash).c_str(), nullptr);
	b = std::strtod(s.substr(dash + 1).c_str(), nullptr);
}

void ParseInterval(const std::string& s, double& lo, double& hi)
{
	if (s == "land") { lo = FHeightmap::SeaLevel; hi = 100; }
	else if (s == "all") { lo = 0; hi = 100; }
	else ParseRange(s, lo, hi);
}

bool SameTool(const std::string& a, const char* b)
{
	size_t n = std::strlen(b);
	if (a.size() != n) return false;
	for (size_t i = 0; i < n; i++) if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
	return true;
}
}

FHeightmap::FHeightmap(const std::vector<std::string>& Steps, u64 Seed) : H((size_t)HN * HN, 0.0), Rng(Seed)
{
	for (const std::string& line : Steps)
	{
		FStep step;
		size_t at = 0;
		while (at < line.size())
		{
			while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) at++;
			size_t end = at;
			while (end < line.size() && line[end] != ' ' && line[end] != '\t') end++;
			if (end > at) { if (step.Tool.empty()) step.Tool = line.substr(at, end - at); else step.Args.push_back(line.substr(at, end - at)); }
			at = end;
		}
		for (const char* tool : HeightTools) if (SameTool(step.Tool, tool)) { step.Tool = tool; break; }
		Run(step);
	}
	Smooth(3); // single-cell roughness would read as speckled coasts at world scale
}

double FHeightmap::Pick(const std::string& s)
{
	double a, b;
	ParseRange(s, a, b);
	return a == b ? a : a + (b - a) * Rng.Unit();
}

/// A count; a fraction is a chance of one more (1.5 = one, and a second half the time).
int FHeightmap::Count(const std::string& s)
{
	double c = Pick(s);
	int whole = (int)std::floor(c);
	return whole + (Rng.Unit() < c - whole ? 1 : 0);
}

int FHeightmap::PickCell(const std::string& xs, const std::string& ys, int sign, double height, bool bLine)
{
	int cell = 0;
	for (int attempt = 0; attempt < 50; attempt++)
	{
		double px = Pick(xs), py = Pick(ys);
		cell = CellAt(px / 100, py / 100);
		double h = H[cell];
		bool ok = bLine ? (sign > 0 ? h < 80 : h > SeaLevel) : (sign > 0 ? h + height <= 90 : h >= SeaLevel);
		if (ok) break;
	}
	return cell;
}

void FHeightmap::Run(const FStep& s)
{
	const std::vector<std::string>& a = s.Args;
	if (s.Tool == "Hill" || s.Tool == "Pit")
	{
		int sign = s.Tool == "Hill" ? 1 : -1;
		int count = Count(a[0]);
		for (int k = 0; k < count; k++) { double height = Pick(a[1]); Blob(height, a[2], a[3], sign); }
	}
	else if (s.Tool == "Range" || s.Tool == "Trough")
	{
		int sign = s.Tool == "Range" ? 1 : -1;
		int count = Count(a[0]);
		bool to = a.size() >= 6;
		for (int k = 0; k < count; k++) { double height = Pick(a[1]); Line(height, a[2], a[3], sign, to ? &a[4] : nullptr, to ? &a[5] : nullptr); }
	}
	else if (s.Tool == "Strait") { int width = Count(a[0]); Strait(width, a[1] == "vertical"); }
	else if (s.Tool == "Mask") Mask(Pick(a[0]));
	else if (s.Tool == "Add")
	{
		double v = Pick(a[0]), lo, hi;
		ParseInterval(a[1], lo, hi);
		for (double& h : H) if (h >= lo && h <= hi) h = Lim(h + v);
	}
	else if (s.Tool == "Multiply")
	{
		double v = Pick(a[0]), lo, hi;
		ParseInterval(a[1], lo, hi);
		for (double& h : H) if (h >= lo && h <= hi) h = Lim(lo >= SeaLevel ? SeaLevel + (h - SeaLevel) * v : h * v);
	}
	else if (s.Tool == "Smooth") Smooth(Pick(a[0]));
	else if (s.Tool == "Invert") { if (Rng.Unit() < Pick(a[0])) Invert(a[1]); }
}

/// A hill (sign +1) or pit (-1): the change spreads breadth-first, each neighbour getting change^power x random
/// 0.9..1.1 of its parent, until it drops below 1.
void FHeightmap::Blob(double height, const std::string& xs, const std::string& ys, int sign)
{
	int start = PickCell(xs, ys, sign, height, false);
	std::vector<double> change((size_t)HN * HN, 0.0);
	change[start] = height;
	std::deque<int> queue;
	queue.push_back(start);
	int nb[6];
	while (!queue.empty())
	{
		int q = queue.front(); queue.pop_front();
		int count = Neighbours(q, nb);
		for (int k = 0; k < count; k++)
		{
			int n = nb[k];
			if (change[n] > 0) continue;
			// (minus a small fade per ring: a finite reach, so the faint tails of many hills do not add up to land everywhere)
			change[n] = Mx(1e-6, std::pow(change[q], BlobPower) * (.9 + .2 * Rng.Unit()) - BlobFade);
			if (change[n] > 1) queue.push_back(n);
		}
	}
	for (size_t c = 0; c < H.size(); c++) H[c] = Lim(H[c] + sign * change[c]);
}

/// A wandering path from a start toward an end; the path gets the height, which spreads sideways ring by ring;
/// short spurs branch off downhill.
void FHeightmap::Line(double height, const std::string& xs, const std::string& ys, int sign, const std::string* toX, const std::string* toY)
{
	int start = PickCell(xs, ys, sign, height, true);
	double sx, sy;
	Centre(start % HN, start / HN, sx, sy);
	int end = start;
	if (toX) { double ex = Pick(*toX), ey = Pick(*toY); end = CellAt(ex / 100, ey / 100); }
	else for (int attempt = 0; attempt < 50; attempt++)
	{
		double ex = .05 + .9 * Rng.Unit(), ey = .05 + .9 * Rng.Unit();
		double d = std::sqrt((ex - sx) * (ex - sx) + (ey - sy) * (ey - sy));
		end = CellAt(ex, ey);
		if (d > 1.0 / 8 && d < 1.0 / 3) break;
	}
	std::vector<int> path = Path(start, end, .15);
	std::vector<uint8_t> used((size_t)HN * HN, 0);
	std::vector<int> ring = path;
	for (int c : ring) used[c] = 1;
	double h = height;
	int nb[6];
	while (h > 2 && !ring.empty())
	{
		for (int c : ring) H[c] = Lim(H[c] + sign * h * (.85 + .3 * Rng.Unit()));
		h = std::pow(h, LinePower) - 1;
		std::vector<int> next;
		for (int c : ring)
		{
			int count = Neighbours(c, nb);
			for (int k = 0; k < count; k++) if (!used[nb[k]]) { used[nb[k]] = 1; next.push_back(nb[k]); }
		}
		ring = std::move(next);
	}
	// Spurs: every sixth path cell, a short ridge (or side valley) running downhill.
	for (size_t p = 0; p < path.size(); p += 6)
	{
		int c = path[p];
		double spur = height * .5;
		for (int step = 0; step < 6 && spur > 2; step++)
		{
			int count = Neighbours(c, nb), low = nb[0];
			for (int k = 1; k < count; k++) if (H[nb[k]] < H[low]) low = nb[k];
			c = low;
			H[c] = Lim(H[c] + sign * spur * .5);
			spur *= .75;
		}
	}
}

/// Greedy path toward the end cell; with probability `wander` a random neighbour.
std::vector<int> FHeightmap::Path(int from, int to, double wander)
{
	std::vector<int> path{ from };
	double tx, ty;
	Centre(to % HN, to / HN, tx, ty);
	int nb[6];
	int c = from;
	for (int guard = 0; c != to && guard < HN * 4; guard++)
	{
		int count = Neighbours(c, nb), best = nb[0];
		double bestD = std::numeric_limits<double>::max();
		for (int k = 0; k < count; k++)
		{
			double x, y;
			Centre(nb[k] % HN, nb[k] / HN, x, y);
			double d = (x - tx) * (x - tx) + (y - ty) * (y - ty);
			if (Rng.Unit() < wander) d *= .5 + Rng.Unit(); // wander off the straight line
			if (d < bestD) { bestD = d; best = nb[k]; }
		}
		c = best;
		path.push_back(c);
	}
	return path;
}

/// A sea channel across the map: a wandering path from one edge to the other, lowering cells around it.
void FHeightmap::Strait(int width, bool vertical)
{
	double a = .15 + .7 * Rng.Unit(), b = .15 + .7 * Rng.Unit();
	int from = vertical ? CellAt(a, 0) : CellAt(0, a), to = vertical ? CellAt(b, .999) : CellAt(.999, b);
	std::vector<int> ring = Path(from, to, .25);
	std::vector<uint8_t> used((size_t)HN * HN, 0);
	for (int c : ring) used[c] = 1;
	int nb[6];
	for (int r = 0; r < width && !ring.empty(); r++)
	{
		double keep = .55 + .45 * r / width; // the middle drops most
		for (int c : ring) H[c] = Mn(H[c], SeaLevel + (H[c] - SeaLevel) * keep - (width - r) * .8);
		std::vector<int> next;
		for (int c : ring)
		{
			int count = Neighbours(c, nb);
			for (int k = 0; k < count; k++) if (!used[nb[k]]) { used[nb[k]] = 1; next.push_back(nb[k]); }
		}
		ring = std::move(next);
	}
	for (double& h : H) h = Lim(h);
}

/// Fade heights toward the edges (fraction > 0; larger = gentler) or toward the centre (fraction < 0).
void FHeightmap::Mask(double fraction)
{
	double fr = std::fabs(fraction);
	if (fr < 1) fr = 1;
	for (int j = 0; j < HN; j++)
		for (int i = 0; i < HN; i++)
		{
			double x, y;
			Centre(i, j, x, y);
			double nx = 2 * x - 1, ny = 2 * y - 1, distance = (1 - nx * nx) * (1 - ny * ny);
			if (fraction < 0) distance = 1 - distance;
			int c = HIndex(i, j);
			H[c] = Lim((H[c] * (fr - 1) + H[c] * distance) / fr);
		}
}

void FHeightmap::Smooth(double fraction)
{
	double fr = Mx(1, fraction);
	std::vector<double> copy = H;
	int nb[6];
	for (int c = 0; c < (int)H.size(); c++)
	{
		int count = Neighbours(c, nb);
		double sum = copy[c];
		for (int k = 0; k < count; k++) sum += copy[nb[k]];
		double mean = sum / (count + 1);
		H[c] = Lim((copy[c] * (fr - 1) + mean) / fr);
	}
}

void FHeightmap::Invert(const std::string& axes)
{
	std::vector<double> copy = H;
	bool fx = axes == "x" || axes == "both", fy = axes == "y" || axes == "both";
	for (int j = 0; j < HN; j++)
		for (int i = 0; i < HN; i++)
			H[HIndex(i, j)] = copy[HIndex(fx ? HN - 1 - i : i, fy ? HN - 1 - j : j)];
}

double FHeightmap::Sample(double x, double y) const
{
	double fy = y * HN - .5;
	int j0 = (int)std::floor(fy);
	double ty = fy - j0;
	auto Row = [&](int j)
	{
		j = ClampI(j, 0, HN - 1);
		double fx = x * HN - .5 - (j & 1) * .5;
		int i0 = (int)std::floor(fx);
		double tx = fx - i0;
		double a = H[HIndex(ClampI(i0, 0, HN - 1), j)], b = H[HIndex(ClampI(i0 + 1, 0, HN - 1), j)];
		return a + (b - a) * tx;
	};
	double r0 = Row(j0), r1 = Row(j0 + 1);
	return r0 + (r1 - r0) * ty;
}

double FHeightmap::LandShare() const
{
	int land = 0;
	for (double h : H) if (h >= SeaLevel) land++;
	return land / (double)H.size();
}

// ------------------------------------------------------------------------------------------------ landform template
namespace
{
const double TPi = 3.141592653589793;

struct FStepDefaults { double Land, Relief, Mountain; FOptionalRange Size, Length; };

FStepDefaults DefaultsFor(const std::string& step)
{
	if (step == "Hills") return { .25, 1.6, 0, { .03, .07 }, { 0.0 } };
	if (step == "Raise") return { .7, .6, 0, { .1, .2 }, { 0.0 } };
	if (step == "Lower") return { -.8, -.6, 0, { .08, .16 }, { 0.0 } };
	if (step == "Basin") return { 0, -2.5, 0, { .02, .05 }, { 0.0 } };
	if (step == "Range") return { .4, 0, 1, { 1.0 }, { .15, .35 } };
	if (step == "Trough") return { -.1, -2.2, 0, { .006, .012 }, { .1, .25 } };
	if (step == "Strait") return { -2.2, 0, 0, { .004, .008 }, { .15, .3 } };
	if (step == "Islands") return { 1.7, .8, 0, { .004, .008 }, { .1, .3 } };
	return { 0, 0, 0, { 0.0 }, { 0.0 } };
}
}

int FLandformTemplate::Bin(double local) const { return ClampI((int)((local / World + .5) * Bins), 0, Bins - 1); }

FLandformTemplate::FLandformTemplate(const FWorldManifest& Manifest)
{
	const FWorldProfile& p = Manifest.Profile;
	World = p.WorldSize; Hill = Mx(p.HillHeight, 60);
	int group = 0;
	const int MaxShapes = 1200;
	if (p.bLandforms)
		for (int n = 0; n < (int)p.Landforms.size(); n++)
		{
			const FLandformStep& s = p.Landforms[n];
			FRandomStream rng(Hashing::FeatureSeed(Manifest.Seed, 3, Manifest.ProfileDigest, "landform-" + s.Step, 0, 0, n));
			// (the fallback is always computed first, as an argument: its random draws happen whether it is used or not)
			auto Pick = [&](const FOptionalRange& r, double fallback) { return !r.bSet ? fallback : r.Values.size() == 1 ? r.Values[0] : rng.Between(r.Values[0], r.Values[1]); };
			if (s.Step == "Mask")
			{
				std::string kind = s.bShape ? s.Shape : "Edges";
				int code = kind == "Centre" ? 1 : kind == "North" ? 2 : kind == "South" ? 3 : kind == "West" ? 4 : kind == "East" ? 5 : 0;
				double size = DetMath::Clamp(Pick(s.Size, code <= 1 ? .22 : .4), .02, .5);
				Masks.push_back({ code, s.Strength.bSet ? s.Strength.Value : .8, size, rng.Next() });
				continue;
			}
			FStepDefaults d = DefaultsFor(s.Step);
			double land = s.Land.bSet ? s.Land.Value : d.Land, relief = (s.Relief.bSet ? s.Relief.Value : d.Relief) * Hill, mountain = s.Mountain.bSet ? s.Mountain.Value : d.Mountain;
			double bend = s.Step == "Trough" ? .06 : s.Step == "Strait" ? .12 : s.Step == "Islands" ? .14 : .1;
			auto Add = [&](double ax, double az, double bx, double bz, double ra, double rb, double t0, double t1, bool line, u64 seed)
			{
				double reach = Mx(ra, rb) * 1.9;
				Shapes.push_back({ ax, az, bx, bz, ra, rb, t0, t1, line, group, land, relief, mountain, seed,
					Mn(ax, bx) - reach, Mn(az, bz) - reach, Mx(ax, bx) + reach, Mx(az, bz) + reach });
			};
			int count = (int)std::nearbyint(Pick(s.Count, 1));
			for (int c = 0; c < count && (int)Shapes.size() < MaxShapes; c++)
			{
				double fallbackX = rng.Between(.15, .85);
				double ax = (Pick(s.X, fallbackX) - .5) * World;
				double fallbackZ = rng.Between(.15, .85);
				double az = (Pick(s.Z, fallbackZ) - .5) * World;
				double defaultSize = Pick(d.Size, 0);
				double size = Pick(s.Size, defaultSize);
				// Mountain ranges are as wide as the profile's ranges; everything else is a world fraction, with floors.
				double radius = s.Step == "Range" ? Mx(p.MountainWidth * size, 600) : Mx(size * World, s.Step == "Islands" ? 350 : 500);
				// Any step with a length is a line (a long ridge, an arm of land); Islands become a chain.
				double defaultLength = Pick(d.Length, 0);
				double length = Pick(s.Length, defaultLength) * World;
				if (length <= 0) { Add(ax, az, ax, az, radius, radius, 0, 1, false, rng.Next()); group++; continue; }
				double fallbackAngle = rng.Between(0, 180);
				double angle = Pick(s.Angle, fallbackAngle) * TPi / 180;
				// A smoothly bending polyline (two sine bends) with varying width.
				int segments = ClampI((int)(length / (radius * 2)), 1, 10);
				std::vector<double> px(segments + 1), pz(segments + 1), pr(segments + 1);
				double cosA = std::cos(angle), sinA = std::sin(angle);
				double b1 = rng.Between(-1, 1), b2 = rng.Between(-1, 1), phase = rng.Between(0, 2 * TPi);
				for (int i = 0; i <= segments; i++)
				{
					double u = i / (double)segments, along = length * u;
					double off = length * bend * (b1 * std::sin(TPi * u) + .5 * b2 * (std::sin(2 * TPi * u + phase) - std::sin(phase) * (1 - u)));
					px[i] = ax + cosA * along - sinA * off; pz[i] = az + sinA * along + cosA * off;
					pr[i] = radius * rng.Between(.7, 1.3);
				}
				if (s.Step != "Islands")
				{
					u64 seed = rng.Next();
					for (int i = 0; i < segments && (int)Shapes.size() < MaxShapes; i++)
						Add(px[i], pz[i], px[i + 1], pz[i + 1], pr[i], pr[i + 1], i / (double)segments, (i + 1) / (double)segments, true, seed);
					group++;
					continue;
				}
				// An island chain: islands of varied size scattered along the line.
				int islands = MxI(2, (int)std::nearbyint(length / (radius * 3.5)));
				for (int i = 0; i < islands && (int)Shapes.size() < MaxShapes; i++)
				{
					double t = DetMath::Clamp((i + rng.Between(-.3, .3)) / MxI(1, islands - 1), 0, 1) * segments;
					int k = MnI((int)t, segments - 1); double f = t - k, off = rng.Between(-1.5, 1.5) * radius;
					double cx = px[k] + (px[k + 1] - px[k]) * f - sinA * off, cz = pz[k] + (pz[k + 1] - pz[k]) * f + cosA * off;
					double r = radius * rng.Between(.5, 1.3);
					Add(cx, cz, cx, cz, r, r, 0, 1, false, rng.Next());
					group++;
				}
			}
		}
	BinLists.assign((size_t)Bins * Bins, std::vector<int>());
	for (int n = 0; n < (int)Shapes.size(); n++)
	{
		const FShape& sh = Shapes[n];
		int i0 = Bin(sh.MinX), i1 = Bin(sh.MaxX), j0 = Bin(sh.MinZ), j1 = Bin(sh.MaxZ);
		for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) BinLists[(size_t)j * Bins + i].push_back(n);
	}
}

void FLandformTemplate::Apply(double lx, double lz, double& land, double& relief, double& mountain, const std::function<double(double, double)>& Rugged) const
{
	for (const FMask& m : Masks)
	{
		// -0.5 .. 0.5 across the world, warped so mask edges are not geometric.
		double u = lx / World + Noise::Value(lx / World * 5, lz / World * 5, m.Seed) * .05;
		double v = lz / World + Noise::Value(lx / World * 5 + 17.3, lz / World * 5 - 9.1, m.Seed) * .05;
		double fade;
		switch (m.Kind)
		{
		case 1: fade = 1 - DetMath::Smooth(std::sqrt(u * u + v * v) / m.Size); break;
		case 2: fade = DetMath::Smooth((-v - (.45 - m.Size)) / (m.Size * .9)); break;
		case 3: fade = DetMath::Smooth((v - (.45 - m.Size)) / (m.Size * .9)); break;
		case 4: fade = DetMath::Smooth((-u - (.45 - m.Size)) / (m.Size * .9)); break;
		case 5: fade = DetMath::Smooth((u - (.45 - m.Size)) / (m.Size * .9)); break;
		default: fade = DetMath::Smooth((Mx(std::fabs(u), std::fabs(v)) - (.5 - m.Size)) / (m.Size * .9)); break;
		}
		land -= fade * m.Strength * 1.5;
	}
	if (Shapes.empty()) return;
	int current = -1, best = -1; double strongest = 0;
	double addLand = 0, cutLand = 0, addRelief = 0, outMountain = mountain;
	auto Flush = [&]()
	{
		if (best < 0) return;
		const FShape& s = Shapes[best];
		double r = Mx(s.Ra, s.Rb);
		// Coast and bank irregularity: noise strongest where the shape fades out (its coastline).
		double coast = Noise::Value(lx / (r * .5), lz / (r * .5), s.Seed + 1) * .6 + Noise::Value(lx / (r * .16), lz / (r * .16), s.Seed + 2) * .4;
		double change = s.Land * strongest + Mn(std::fabs(s.Land) * .22, .45) * coast * DetMath::Smooth(strongest * 5);
		if (s.Land < 0) cutLand += Mn(0, change); else addLand += change;
		addRelief += s.Relief * strongest;
		if (s.Mountain != 0)
		{
			// Peaks and passes along the range.
			double along = .6 + .4 * Noise::Value(lx / (r * 3), lz / (r * 3), s.Seed + 3);
			outMountain = DetMath::Clamp(outMountain + s.Mountain * strongest * along * (.3 + .7 * Rugged(lx, lz)), 0, 1);
		}
		best = -1;
	};
	for (int n : BinLists[(size_t)Bin(lz) * Bins + Bin(lx)])
	{
		const FShape& s = Shapes[n];
		if (s.Group != current) { Flush(); current = s.Group; strongest = 0; best = -1; }
		if (lx < s.MinX || lx > s.MaxX || lz < s.MinZ || lz > s.MaxZ) continue;
		// Domain-warped distance to the segment (a point for blobs), so shapes are not circles or tubes.
		double rw = Mx(s.Ra, s.Rb) * .8;
		double wx = lx + Noise::Value(lx / rw, lz / rw, s.Seed) * rw * .22;
		double wz = lz + Noise::Value(lx / rw + 31.7, lz / rw - 18.3, s.Seed) * rw * .22;
		double ex = s.Bx - s.Ax, ez = s.Bz - s.Az, len2 = ex * ex + ez * ez;
		double t = len2 > 0 ? DetMath::Clamp(((wx - s.Ax) * ex + (wz - s.Az) * ez) / len2, 0, 1) : 0;
		double dx = wx - (s.Ax + ex * t), dz = wz - (s.Az + ez * t), r = s.Ra + (s.Rb - s.Ra) * t;
		double q = (dx * dx + dz * dz) / (r * r);
		if (q >= 1) continue;
		double falloff = (1 - q) * (1 - q);
		if (s.bLine) { double tg = s.T0 + (s.T1 - s.T0) * t; falloff *= DetMath::Smooth(Mn(tg, 1 - tg) * 6 + .12); }
		if (falloff > strongest) { strongest = falloff; best = n; }
	}
	Flush();
	land += addLand;
	// Cuts (straits, lowered land) make shallow sea, never deepen sea that is already there.
	if (cutLand < 0) land = Mx(land + cutLand, Mn(land, -.2));
	relief += addRelief; mountain = outMountain;
}

// ------------------------------------------------------------------------------------------------ world types
void AddTemplateWorldTypes(const std::vector<FWorldProfile>& Profiles, std::vector<FWorldRecipe>& List)
{
	auto P = [&](const char* Id) -> const FWorldProfile* { for (const auto& p : Profiles) if (p.Id == Id) return &p; return nullptr; };
	const FWorldProfile* Balanced = P("balanced");
	if (!Balanced) return;
	auto AddRecipe = [&](const std::string& Id, const std::string& Name, const char* Description, const FWorldProfile& Base, bool Mainland)
	{
		FWorldRecipe r; r.Id = Id; r.Name = Name; r.Description = Description; r.Base = Base; r.bMainland = Mainland;
		List.push_back(r);
	};
	using R = FOptionalRange;
	auto L = [](const char* Step) { FLandformStep s; s.Step = Step; return s; };
	auto Template = [&](const char* Id, const char* Name, int Islands, std::vector<FLandformStep> Steps)
	{
		FWorldProfile p = *Balanced;
		p.Id = Id; p.Name = Name; p.Continents = 0; p.Islands = Islands; p.IslandRadius = 5000; p.RangesPerContinent = 0;
		p.bLandforms = true; p.Landforms = std::move(Steps);
		return p;
	};
	// Template types: the land comes from a landform template rather than placed continents.
	{
		FLandformStep a = L("Raise"); a.X = R(.5); a.Z = R(.5); a.Size = R(.47, .5); a.Land = 3.2; a.Relief = .4;
		FLandformStep b = L("Mask"); b.bShape = true; b.Shape = "Centre"; b.Size = R(.26, .3); b.Strength = 1.7;
		FLandformStep c = L("Strait"); c.X = R(.5); c.Z = R(.5); c.Angle = R(-30, 30); c.Length = R(.5); c.Size = R(.007, .01);
		FLandformStep d = L("Range"); d.Count = R(2, 3); d.X = R(.2, .8); d.Z = R(.2, .8); d.Size = R(1, 1.4);
		FLandformStep e = L("Islands"); e.X = R(.4, .45); e.Z = R(.42, .58); e.Angle = R(-25, 25); e.Length = R(.12, .18); e.Size = R(.006, .01);
		AddRecipe("inland-sea", "Inland sea", "A ring of land around a great inner sea with islands, open to the ocean through one strait.",
			Template("inland-sea", "Inland sea", 24, { a, b, c, d, e }), true);
	}
	{
		FLandformStep a = L("Raise"); a.X = R(.5); a.Z = R(.5); a.Size = R(.4, .43); a.Land = 2.4; a.Relief = .3;
		FLandformStep b = L("Mask"); b.bShape = true; b.Shape = "Edges"; b.Strength = .5;
		FLandformStep c = L("Strait"); c.Count = R(6, 9); c.X = R(.25, .75); c.Z = R(.25, .75); c.Length = R(.25, .5); c.Size = R(.008, .014);
		FLandformStep d = L("Range"); d.Count = R(3, 4);
		FLandformStep e = L("Hills"); e.Count = R(4, 7);
		AddRecipe("shattered", "Shattered lands", "One great landmass broken into many pieces by narrow sea channels.",
			Template("shattered", "Shattered lands", 50, { a, b, c, d, e }), true);
	}
	{
		FLandformStep a = L("Raise"); a.X = R(.5); a.Z = R(.5); a.Size = R(.38, .41); a.Land = 2.3; a.Relief = .5;
		FLandformStep b = L("Trough"); b.X = R(.24); b.Z = R(.48, .52); b.Angle = R(-6, 6); b.Length = R(.52); b.Size = R(.012, .016); b.Relief = -3.5;
		FLandformStep c = L("Basin"); c.Count = R(3, 4); c.X = R(.3, .7); c.Z = R(.47, .53); c.Size = R(.015, .025); c.Relief = -3;
		FLandformStep d = L("Range"); d.X = R(.25); d.Z = R(.42); d.Angle = R(-8, 8); d.Length = R(.5); d.Size = R(.8);
		FLandformStep e = L("Range"); e.X = R(.25); e.Z = R(.58); e.Angle = R(-8, 8); e.Length = R(.5); e.Size = R(.8);
		AddRecipe("rift", "Rift continent", "A great continent split by a rift valley full of long lakes, walled by mountains.",
			Template("rift", "Rift continent", 20, { a, b, c, d, e }), true);
	}
	{
		FLandformStep a = L("Raise"); a.X = R(.25, .3); a.Z = R(.42, .58); a.Size = R(.23, .26); a.Land = 2.2; a.Relief = .4;
		FLandformStep b = L("Raise"); b.X = R(.7, .75); b.Z = R(.42, .58); b.Size = R(.21, .25); b.Land = 2.2; b.Relief = .4;
		FLandformStep c = L("Range"); c.X = R(.2, .3); c.Z = R(.3, .38); c.Angle = R(70, 110); c.Length = R(.25, .35);
		FLandformStep d = L("Range"); d.X = R(.68, .78); d.Z = R(.3, .38); d.Angle = R(70, 110); d.Length = R(.25, .35);
		FLandformStep e = L("Islands"); e.X = R(.5); e.Z = R(.2, .25); e.Angle = R(80, 100); e.Length = R(.5, .6); e.Size = R(.004, .008);
		AddRecipe("twin-continents", "Twin continents", "Two large continents facing each other across an ocean with an island chain.",
			Template("twin-continents", "Twin continents", 36, { a, b, c, d, e }), false);
	}
	{
		FLandformStep a = L("Raise"); a.X = R(.82, .86); a.Z = R(.4, .6); a.Size = R(.26, .3); a.Land = 2.2; a.Relief = .4;
		FLandformStep b = L("Raise"); b.X = R(.82); b.Z = R(.48, .52); b.Angle = R(165, 195); b.Length = R(.55, .62); b.Size = R(.06, .08); b.Land = 2.0; b.Relief = .6;
		FLandformStep c = L("Range"); c.X = R(.75); c.Z = R(.48, .52); c.Angle = R(170, 190); c.Length = R(.5); c.Size = R(.8);
		FLandformStep d = L("Islands"); d.X = R(.2, .25); d.Z = R(.4, .6); d.Angle = R(150, 210); d.Length = R(.15, .2); d.Size = R(.003, .005);
		AddRecipe("peninsula", "Great peninsula", "A long mountainous peninsula reaching from a mainland into the ocean.",
			Template("peninsula", "Great peninsula", 30, { a, b, c, d }), true);
	}
	// Natural types: heightmap templates, land grown cell by cell.
	auto Natural = [&](const char* Id, const char* Name, const char* Description, const FWorldProfile* Source, std::vector<std::string> Steps)
	{
		if (!Source) return;
		FWorldProfile p = *Source;
		p.Id = std::string("natural-") + Id; p.Name = std::string("Natural \xC2\xB7 ") + Name;
		p.Continents = 0; p.Islands = 0; p.IslandRadius = Mx(Source->WorldSize * .0021, 4000); p.RangesPerContinent = 0;
		p.ContinentRadius = Mx(Source->ContinentRadius, Source->WorldSize * .036);
		p.bHeightmap = true; p.Heightmap = std::move(Steps);
		AddRecipe(p.Id, p.Name, Description, p, false);
	};
	Natural("continents", "Balanced continents", "Two to four continents of different sizes with ragged coasts, bays, peninsulas and offshore islands.", Balanced,
		{ "Hill 1 60-75 18-32 30-70", "Hill 1 55-70 62-80 25-75", "Hill 1-2 35-50 35-65 15-85",
		  "Hill 6-9 18-30 10-90 10-90", "Pit 3-5 15-25 15-85 15-85",
		  "Range 4-6 25-40 15-85 15-85", "Trough 2-4 15-25 15-85 15-85",
		  "Hill 10-16 8-14 5-95 5-95", "Strait 2-3 vertical", "Mask 3", "Smooth 2" });
	Natural("mainland", "Great mainland", "One huge landmass with deep bays, long peninsulas and a few islands.", P("continental"),
		{ "Hill 1 85-95 40-60 40-60", "Hill 6-8 30-45 20-80 20-80", "Pit 3-4 12-20 25-75 25-75",
		  "Range 5-7 25-40 20-80 20-80", "Trough 2-3 15-22 25-75 25-75", "Hill 8-12 8-14 5-95 5-95",
		  "Mask 3", "Smooth 2" });
	Natural("archipelago", "Archipelago", "Dozens of islands of every size, from small continents to rocks.", P("archipelago"),
		{ "Add 13 all", "Range 4-5 30-45 15-85 15-85", "Hill 8-10 14-22 10-90 15-85", "Smooth 2",
		  "Trough 10-14 18-28 5-95 5-95", "Strait 2 vertical", "Strait 1-2 horizontal", "Mask 4", "Smooth 2" });
	Natural("mountains", "Mountain kingdoms", "Continents crossed by many high mountain chains with deep valleys between.", P("mountainous"),
		{ "Hill 1 60-70 20-40 30-70", "Hill 1 55-70 60-80 30-70", "Hill 3-5 25-35 15-85 15-85",
		  "Range 8-12 35-55 15-85 15-85", "Trough 4-6 15-25 15-85 15-85", "Hill 8-12 8-12 5-95 5-95", "Mask 3", "Smooth 2" });
	Natural("many-continents", "Many small continents", "Seven to ten medium landmasses separated by seas and straits.", Balanced,
		{ "Hill 7-9 42-55 12-88 12-88", "Pit 4-6 12-18 15-85 15-85", "Range 4-6 20-30 10-90 10-90",
		  "Hill 10-15 8-12 5-95 5-95", "Trough 3-5 12-18 15-85 15-85", "Mask 4", "Smooth 2" });
	Natural("islands-mainland", "Islands + mainland", "A large mainland ringed by islands and island chains.", Balanced,
		{ "Hill 1 70-85 30-60 35-65", "Hill 3-5 20-30 25-65 25-75", "Range 3-5 25-40 25-65 25-75",
		  "Hill 25-35 12-20 5-95 5-95", "Pit 4-6 15-20 20-80 20-80", "Mask 3", "Smooth 2" });
	Natural("inland-sea", "Inland sea", "Lands around a great inner sea that opens to the ocean.", Balanced,
		{ "Hill 4-6 32-42 18-82 15-25", "Hill 4-6 32-42 18-82 75-85", "Hill 3-4 32-42 15-25 25-75", "Hill 3-4 32-42 75-85 25-75",
		  "Range 4-5 20-32 15-85 15-85", "Mask -2", "Mask 3", "Strait 1 horizontal", "Hill 5-8 10-16 35-65 35-65", "Smooth 2" });
	Natural("shattered", "Shattered lands", "A broken continent: many large islands separated by winding straits.", Balanced,
		{ "Hill 1 90-99 40-60 40-60", "Hill 5-7 35-50 15-85 15-85", "Trough 12-16 20-30 15-85 15-85",
		  "Strait 2 vertical", "Strait 2 horizontal", "Pit 6-8 12-18 15-85 15-85", "Range 3-4 25-35 20-80 20-80",
		  "Mask 3", "Smooth 2" });
	Natural("rift", "Rift continent", "A great continent split by rift valleys with long lakes, walled by mountains.", Balanced,
		{ "Hill 1 90-99 40-60 40-60", "Hill 6-8 30-40 20-80 20-80", "Trough 2-3 22-32 25-75 25-75",
		  "Range 3-4 35-45 25-75 25-75", "Pit 3-4 15-22 30-70 30-70", "Mask 3", "Smooth 2" });
	Natural("twin-continents", "Twin continents", "Two large continents facing each other across an ocean dotted with islands.", Balanced,
		{ "Hill 1 75-85 20-30 35-65", "Hill 1 75-85 70-80 35-65", "Hill 3-4 25-35 10-40 15-85", "Hill 3-4 25-35 60-90 15-85",
		  "Range 2 30-45 15-35 25-75", "Range 2 30-45 65-85 25-75", "Hill 6-10 8-14 40-60 10-90", "Mask 3", "Smooth 2" });
	Natural("peninsula", "Great peninsula", "A mainland with a long, mountainous peninsula reaching into the ocean.", Balanced,
		{ "Hill 1 80-90 72-80 35-65", "Hill 2-3 30-40 65-85 20-80", "Range 1 45-55 60-68 40-60 10-20 35-65",
		  "Range 1 25-35 55-65 40-60 15-25 35-65", "Hill 6-8 10-16 5-35 20-80", "Mask 3", "Smooth 2" });
}
}
