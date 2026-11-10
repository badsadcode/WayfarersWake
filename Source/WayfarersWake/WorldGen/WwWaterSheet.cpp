// Port of Thornwake Game/Rewrite/TileBuilder.cs: BuildWater, Flow, ReachFlow (river water, D287).
#include "WwWaterSheet.h"
#include "WwWaterfalls.h"
#include <unordered_map>

namespace ww
{
namespace
{
float KindCode(EWaterKind kind)
{
	return kind == EWaterKind::Pond ? FWaterSheet::PondCode : kind == EWaterKind::Lake ? FWaterSheet::LakeCode : kind == EWaterKind::River ? FWaterSheet::RiverCode : FWaterSheet::SeaCode;
}

/// A reach's fall per metre over ~60 m either way, per point.
std::vector<double> Grades(const FRiverReach& reach)
{
	int n = (int)reach.Points.size();
	std::vector<double> g(n);
	for (int i = 0; i < n; i++)
	{
		int a = i, b = i;
		while (a > 0 && reach.Along[i] - reach.Along[a] < 60) a--;
		while (b < n - 1 && reach.Along[b] - reach.Along[i] < 60) b++;
		g[i] = b > a ? (reach.Surface[a] - reach.Surface[b]) / Mx(1, (double)reach.Along[b] - reach.Along[a]) : 0;
	}
	return g;
}

struct FReachCache { int Hint = -1; std::vector<double> Grade; };

/// A river reach's way and fall near a point: its direction there, how much it falls per metre and its channel's depth.
bool ReachFlow(const FHydrology& rivers, int reachId, double x, double z, std::unordered_map<int, FReachCache>& cache, double& tx, double& tz, double& grade, double& depth)
{
	tx = tz = grade = depth = 0;
	if (reachId < 0 || reachId >= (int)rivers.Reaches.size()) return false;
	const FRiverReach& r = rivers.Reaches[reachId];
	int n = (int)r.Points.size();
	if (n < 2 || (int)r.Along.size() != n) return false;
	auto D = [&](int i) { return (r.Points[i].X - x) * (r.Points[i].X - x) + (r.Points[i].Z - z) * (r.Points[i].Z - z); };
	FReachCache& c = cache[reachId];
	int k;
	if (c.Hint >= 0)
	{
		k = c.Hint;
		while (k + 1 < n && D(k + 1) < D(k)) k++;
		while (k > 0 && D(k - 1) < D(k)) k--;
	}
	else
	{
		k = 0;
		for (int i = 1; i < n; i++) if (D(i) < D(k)) k = i;
		c.Grade = Grades(r);
	}
	c.Hint = k;
	const std::vector<double>& grades = c.Grade;
	int p0 = k - 1 < 0 ? 0 : k - 1, p1 = k + 1 > n - 1 ? n - 1 : k + 1;
	double ex = r.Points[p1].X - r.Points[p0].X, ez = r.Points[p1].Z - r.Points[p0].Z, el = std::sqrt(ex * ex + ez * ez);
	if (el < 1e-6) return false;
	tx = ex / el; tz = ez / el;
	// Between the nearest point and the next one along the river (the nearest point's values alone jumped halfway).
	int kb = p1, ka = p0;
	auto Proj = [&](int a0, int b0)
	{
		double sx = r.Points[b0].X - r.Points[a0].X, sz = r.Points[b0].Z - r.Points[a0].Z, l2 = sx * sx + sz * sz;
		return l2 < 1e-9 ? 0.0 : ((x - r.Points[a0].X) * sx + (z - r.Points[a0].Z) * sz) / l2;
	};
	double ahead = kb > k ? Proj(k, kb) : -1;
	if (ahead > 0) { double t = Mn(1, ahead); grade = grades[k] + (grades[kb] - grades[k]) * t; depth = r.Depth[k] + (r.Depth[kb] - r.Depth[k]) * t; }
	else if (ka < k) { double t = DetMath::Clamp(Proj(ka, k), 0, 1); grade = grades[ka] + (grades[k] - grades[ka]) * t; depth = r.Depth[ka] + (r.Depth[k] - r.Depth[ka]) * t; }
	else { grade = grades[k]; depth = r.Depth[k]; }
	return true;
}
}

void FWaterSheet::Build(const FTerrainSample* samples, const double* heights, int n, double spacing, double sea, const FHydrology& rivers, double x0, double z0, const FFallSet* falls)
{
	int count = n * n;
	std::vector<char> wet(count);
	bAny = false;
	for (int k = 0; k < count; k++) { wet[k] = samples[k].Water != EWaterKind::None; bAny |= wet[k] != 0; }
	if (!bAny) return;
	Level.assign(count, 0); Depth.assign(count, 0); Kind.assign(count, 0); Plume.assign(count, 0); Edge.assign(count, 0);
	FlowX.assign(count, 0); FlowZ.assign(count, 0);

	// The sea's sheet also reaches ~14 m under the beach, so the swash can run up and down it.
	const int Never = 0x7fffffff;
	std::vector<int> under(count, Never), front;
	for (int k = 0; k < count; k++) if (wet[k] && samples[k].Water == EWaterKind::Ocean) { under[k] = 0; front.push_back(k); }
	int reach = (int)std::ceil(14 / spacing);
	if (reach < 1) reach = 1;
	for (int step = 1; step <= reach && !front.empty(); step++)
	{
		std::vector<int> next;
		for (int k : front)
		{
			int i = k % n, j = k / n;
			for (int b = -1; b <= 1; b++)
				for (int a = -1; a <= 1; a++)
				{
					int x = i + a, z = j + b;
					if (x < 0 || z < 0 || x >= n || z >= n) continue;
					int m = z * n + x;
					if (wet[m] || under[m] <= step) continue;
					under[m] = step; next.push_back(m);
				}
		}
		front.swap(next);
	}
	// A river's channel continuing out to sea at sea level is the sea there, carrying the river's water (plume).
	std::vector<float> bodyKind(count, 0), bodyPlume(count, 0);
	for (int k = 0; k < count; k++)
	{
		if (!wet[k]) continue;
		bool estuary = samples[k].Water == EWaterKind::River && samples[k].Height < sea && samples[k].WaterLevel < sea + .15;
		bodyKind[k] = estuary ? SeaCode : KindCode(samples[k].Water);
		bodyPlume[k] = estuary ? 1 : (float)samples[k].Plume;
	}
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++)
		{
			int k = j * n + i;
			bool has = false;
			double level = 0; float kind = 0, plume = 0;
			if (wet[k]) { level = samples[k].WaterLevel; kind = bodyKind[k]; plume = bodyPlume[k]; has = true; }
			else
			{
				// Average of the wet neighbours (the maximum lifted the sheet over the downstream bank of steep creeks).
				double sum = 0; int found = 0;
				for (int b = -1; b <= 1; b++)
					for (int a = -1; a <= 1; a++)
					{
						int x = i + a, z = j + b;
						if (x < 0 || z < 0 || x >= n || z >= n || !wet[z * n + x]) continue;
						sum += samples[z * n + x].WaterLevel; found++;
						if (bodyKind[z * n + x] > kind) kind = bodyKind[z * n + x]; // (bank vertices: pond, lake, river, then sea)
						if (bodyPlume[z * n + x] > plume) plume = bodyPlume[z * n + x];
					}
				if (found > 0) { level = sum / found; has = true; }
				else if (under[k] != Never) { level = sea; has = true; }
			}
			double ground = heights[k];
			if (!has) level = ground - 1; // unused vertex: keep it under ground
			Level[k] = (float)level;
			Depth[k] = (float)DetMath::Clamp((level - ground) / 20, 0, 1);
			Kind[k] = kind; Plume[k] = plume;
			// 1 on water vertices, fading over the sea's extension under the beach; 0 on river and sea banks, so the
			// sheet fades over its last square instead of ending in the triangles' saw-tooth; 1 at lake banks.
			Edge[k] = wet[k] ? 1 : under[k] != Never ? Mx(0, 1 - under[k] / 3.f) : kind < .5f ? 0.f : 1.f;
		}
	auto Covered = [&](int k) { return wet[k] || under[k] != Never; };
	// A waterfall's face is left to the falling water: the sheet ran straight from the lip to the pool. Its vertices there
	// turn transparent (the sheet fades out across the last square); squares wholly on the face are dropped.
	std::vector<char> onFace(count, 0);
	if (falls)
		for (int j = 0; j < n; j++)
			for (int i = 0; i < n; i++)
			{
				int k = j * n + i;
				if (!Covered(k)) continue;
				double y = Level[k];
				if (falls->OnFace(x0 + i * spacing, z0 + j * spacing, y, y)) { onFace[k] = 1; Edge[k] = 0; }
			}
	for (int j = 0; j < n - 1; j++)
		for (int i = 0; i < n - 1; i++)
		{
			int a = j * n + i, b = a + 1, c = a + n, d = c + 1;
			if (!(Covered(a) || Covered(b) || Covered(c) || Covered(d))) continue;
			if (onFace[a] && onFace[b] && onFace[c] && onFace[d]) continue;
			if (falls)
			{
				// (a square spanning from the lip's level down to a plunge pool's stood as a wall of water at the rock face)
				float low = 1e30f, high = -1e30f;
				for (int k : { a, b, c, d }) if (Covered(k)) { if (Level[k] < low) low = Level[k]; if (Level[k] > high) high = Level[k]; }
				if (high - low > 1.5 && falls->OnFace(x0 + (i + .5) * spacing, z0 + (j + .5) * spacing, low, high)) continue;
			}
			Indices.insert(Indices.end(), { a, b, c, b, d, c });
		}

	// ---- the current (Flow): downhill along the water surface, the speed from the river's fall over ~120 m, faster
	// where it is deep, the direction leaning on the river's own course.
	std::unordered_map<int, FReachCache> cache;
	auto IsRiver = [&](int k) { return k >= 0 && wet[k] && samples[k].Water == EWaterKind::River; };
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++)
		{
			int k = j * n + i;
			if (!IsRiver(k)) continue;
			auto Slope = [&](int a, int b) -> double
			{
				bool fa = IsRiver(a), fb = IsRiver(b);
				if (fa && fb) return (Level[b] - Level[a]) / (2 * spacing);
				if (fb) return (Level[b] - Level[k]) / spacing;
				if (fa) return (Level[k] - Level[a]) / spacing;
				return 0;
			};
			double gx = Slope(i > 0 ? k - 1 : -1, i < n - 1 ? k + 1 : -1);
			double gz = Slope(j > 0 ? k - n : -1, j < n - 1 ? k + n : -1);
			double g = std::sqrt(gx * gx + gz * gz);
			double speed = g < 1e-6 ? .45 : DetMath::Clamp(std::sqrt(g) * 6, .3, 3.5);
			double dx = g < 1e-6 ? 0 : -gx / g, dz = g < 1e-6 ? 0 : -gz / g;
			double tx, tz, grade, channel;
			if (ReachFlow(rivers, samples[k].WaterBody, x0 + i * spacing, z0 + j * spacing, cache, tx, tz, grade, channel))
			{
				double waterDepth = samples[k].WaterLevel - samples[k].Height;
				speed = DetMath::Clamp(.45 + std::sqrt(Mx(0, grade)) * 8, .45, 3.5) * (.55 + .45 * DetMath::Clamp(waterDepth / Mx(.3, channel), 0, 1));
				dx += tx * 2; dz += tz * 2;
				double dl = std::sqrt(dx * dx + dz * dz);
				if (dl > 1e-6) { dx /= dl; dz /= dl; }
			}
			else if (g < 1e-6) continue;
			FlowX[k] = (float)(dx * speed); FlowZ[k] = (float)(dz * speed);
		}
	// River mouths: the current continues into the lake or sea as a fanning plume, weakening with it.
	auto P = [&](int x, int z) { return samples[(z < 0 ? 0 : z > n - 1 ? n - 1 : z) * n + (x < 0 ? 0 : x > n - 1 ? n - 1 : x)].Plume; };
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++)
		{
			int k = j * n + i;
			if (!wet[k] || samples[k].Water == EWaterKind::River || samples[k].Plume < .03) continue;
			double gx = (P(i + 1, j) - P(i - 1, j)) / (2 * spacing), gz = (P(i, j + 1) - P(i, j - 1)) / (2 * spacing);
			double g = std::sqrt(gx * gx + gz * gz);
			if (g < 1e-6) continue;
			double speed = 1.1 * std::pow(samples[k].Plume, .7);
			FlowX[k] = (float)(-gx / g * speed); FlowZ[k] = (float)(-gz / g * speed);
		}
	// Bank vertices take their wet neighbours' current, then two passes of 3x3 averaging over river vertices.
	std::vector<char> river(count);
	for (int k = 0; k < count; k++) river[k] = FlowX[k] != 0 || FlowZ[k] != 0;
	for (int pass = 0; pass < 3; pass++)
	{
		std::vector<float> nx = FlowX, nz = FlowZ;
		for (int j = 0; j < n; j++)
			for (int i = 0; i < n; i++)
			{
				int k = j * n + i;
				if (pass == 0 && river[k]) continue;
				if (pass > 0 && !river[k]) continue;
				float sx = 0, sz = 0; int found = 0;
				for (int b = -1; b <= 1; b++)
					for (int a = -1; a <= 1; a++)
					{
						int x = i + a, z = j + b;
						if (x < 0 || z < 0 || x >= n || z >= n || !river[z * n + x]) continue;
						sx += FlowX[z * n + x]; sz += FlowZ[z * n + x]; found++;
					}
				if (found > 0) { nx[k] = sx / found; nz[k] = sz / found; }
			}
		FlowX.swap(nx); FlowZ.swap(nz);
		if (pass == 0) for (int k = 0; k < count; k++) river[k] = river[k] || FlowX[k] != 0 || FlowZ[k] != 0;
	}
	// Across the channel the speed follows the depth relative to the deepest water nearby (v ~ depth^2/3).
	int r = (int)std::ceil(6 / spacing);
	if (r < 2) r = 2;
	std::vector<float> depth(count);
	for (int k = 0; k < count; k++) depth[k] = river[k] ? Mx(0.f, Level[k] - (float)samples[k].Height) : 0;
	std::vector<float> sx = FlowX, sz = FlowZ;
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++)
		{
			int k = j * n + i;
			if (!river[k] || samples[k].Water != EWaterKind::River) continue; // mouth plumes keep their spread
			float deepest = .3f;
			for (int b = -r; b <= r; b++)
				for (int a = -r; a <= r; a++)
				{
					int x = i + a, z = j + b;
					if (x >= 0 && z >= 0 && x < n && z < n && depth[z * n + x] > deepest) deepest = depth[z * n + x];
				}
			float f = std::pow(depth[k] / deepest, 2.f / 3);
			f = f < .2f ? .2f : f > 1 ? 1 : f;
			sx[k] = FlowX[k] * f; sz[k] = FlowZ[k] * f;
		}
	FlowX.swap(sx); FlowZ.swap(sz);
}
}
