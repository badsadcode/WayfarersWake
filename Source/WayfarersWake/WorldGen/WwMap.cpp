// The map picture: a port of Thornwake Game/Rewrite/WorldPreview.cs (Render, RenderBiomes), with the real terrain when
// zoomed in and a parchment style.
#include "WwMap.h"
#include <thread>

namespace ww
{
namespace
{
struct FRgb { float R = 0, G = 0, B = 0; };
FRgb MpMix(const FRgb& a, const FRgb& b, float t) { return { a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t, a.B + (b.B - a.B) * t }; }
FRgb MpScale(const FRgb& a, float s) { return { a.R * s, a.G * s, a.B * s }; }
float MpClamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/// Rows shared out over the machine's threads.
template <typename F> void MpRows(int rows, F&& body)
{
	int threads = (int)std::thread::hardware_concurrency() - 2;
	if (threads < 1) threads = 1;
	if (threads > rows) threads = rows;
	std::vector<std::thread> pool;
	for (int t = 0; t < threads; t++)
		pool.emplace_back([&, t] { for (int j = t; j < rows; j += threads) body(j); });
	for (std::thread& thread : pool) thread.join();
}

// Paper: blotches and grain that stay put on the sheet (they belong to the paper, not to the world).
float MpHash(int x, int y) { unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.f; }
float MpNoise(float x, float y)
{
	int xi = (int)std::floor(x), yi = (int)std::floor(y);
	float fx = x - xi, fy = y - yi;
	fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
	float a = MpHash(xi, yi), b = MpHash(xi + 1, yi), c = MpHash(xi, yi + 1), d = MpHash(xi + 1, yi + 1);
	return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

/// A line of ink or water, `width` pixels wide, blended at `strength`.
void MpLine(std::vector<FRgb>& img, int n, double ax, double az, double bx, double bz, double width, const FRgb& colour, float strength)
{
	if ((ax < -8 && bx < -8) || (az < -8 && bz < -8) || (ax > n + 8 && bx > n + 8) || (az > n + 8 && bz > n + 8)) return;
	int steps = (int)std::ceil(Mx(std::fabs(bx - ax), std::fabs(bz - az))) + 1;
	if (steps > n * 4) return; // (a segment far longer than the picture: its ends are far outside)
	int half = (int)(width * .5);
	for (int s = 0; s <= steps; s++)
	{
		double t = s / (double)steps;
		int x = (int)(ax + (bx - ax) * t), z = (int)(az + (bz - az) * t);
		for (int dz = -half; dz <= half; dz++)
			for (int dx = -half; dx <= half; dx++)
			{
				int px = x + dx, pz = z + dz;
				if (px < 0 || pz < 0 || px >= n || pz >= n || dx * dx + dz * dz > half * half + 1) continue;
				FRgb& c = img[(size_t)pz * n + px];
				c = MpMix(c, colour, strength);
			}
	}
}
}

FMapImage FWorldMap::Render(const FTerrainSampler& Sampler, int pixels, EMapStyle style, double cx, double cz, double span)
{
	const FWorldPlan& plan = *Sampler.Plan;
	const FWorldProfile& p = plan.Profile();
	if (span <= 0) { span = p.WorldSize; cx = plan.Manifest.OriginX; cz = plan.Manifest.OriginZ; }
	const double x0 = cx - span / 2, z0 = cz - span / 2, step = span / pixels;
	const double half = p.WorldSize / 2, ox = plan.Manifest.OriginX, oz = plan.Manifest.OriginZ;
	const bool detailed = step < DetailBelow;
	const bool parchment = style == EMapStyle::Parchment, biomes = style == EMapStyle::Biomes;
	const int n = pixels;
	const size_t count = (size_t)n * n;

	// What lies under each pixel: land or sea, height above the sea, the sea's depth measure, inland water, the biome colour.
	std::vector<float> height(count), land(count);
	std::vector<uint8_t> water(count, 0), outside(count, 0); // water: 1 lake or pond, 2 river
	std::vector<FRgb> biome(biomes || parchment ? count : 0);
	const std::vector<FBiome>& pool = plan.Biomes->Registry.Pool;
	MpRows(n, [&](int j)
	{
		for (int i = 0; i < n; i++)
		{
			const double x = x0 + (i + .5) * step, z = z0 + (j + .5) * step;
			const size_t k = (size_t)j * n + i;
			if (std::fabs(x - ox) > half || std::fabs(z - oz) > half) { outside[k] = 1; land[k] = -1; continue; }
			double l, h, m;
			plan.Macro.Sample(x, z, FWorldPlan::LandChannel, FWorldPlan::HeightChannel, FWorldPlan::MountainChannel, l, h, m);
			if (detailed)
			{
				// The real ground: the shore, the banks and the ponds as they are walked.
				const FTerrainSample t = Sampler.Sample(x, z, step * 2);
				const bool sea = t.Water == EWaterKind::Ocean;
				land[k] = sea ? (float)Mn(l, -.001) : (float)Mx(l, .001);
				height[k] = (float)(t.Height - p.SeaLevel);
				water[k] = t.Water == EWaterKind::Lake || t.Water == EWaterKind::Pond ? 1 : t.Water == EWaterKind::River ? 2 : 0;
			}
			else
			{
				land[k] = (float)l; height[k] = (float)h;
				if (l > 0 && plan.Hydrology->LakeAt(x, z) >= 0) water[k] = 1;
			}
			if (!biome.empty() && land[k] > 0)
			{
				int a, b; double share;
				plan.Biomes->Sample(x, z, a, b, share);
				const FColor3& ca = pool[a].Map; const FColor3& cb = pool[b].Map;
				biome[k] = MpMix({ cb.R, cb.G, cb.B }, { ca.R, ca.G, ca.B }, (float)share);
			}
		}
	});

	const double top = p.LowlandHeight + p.HillHeight + p.MountainHeight;
	std::vector<FRgb> img(count);
	const FRgb lake{ .3f, .5f, .62f }, river{ 70 / 255.f, 130 / 255.f, 200 / 255.f };
	const FRgb ink{ .23f, .15f, .09f }, inkWater{ .27f, .34f, .38f };

	// Parchment: how far each sea pixel lies from land, in pixels (for the lines of water along the coast).
	std::vector<float> offshore;
	if (parchment)
	{
		offshore.assign(count, 1e9f);
		for (size_t k = 0; k < count; k++) if (land[k] > 0 && !outside[k]) offshore[k] = 0;
		auto Relax = [&](int i, int j, int di, int dj, float cost)
		{
			int a = i + di, b = j + dj;
			if (a < 0 || b < 0 || a >= n || b >= n) return;
			float& here = offshore[(size_t)j * n + i];
			float there = offshore[(size_t)b * n + a] + cost;
			if (there < here) here = there;
		};
		for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) { Relax(i, j, -1, 0, 1); Relax(i, j, 0, -1, 1); Relax(i, j, -1, -1, 1.414f); Relax(i, j, 1, -1, 1.414f); }
		for (int j = n - 1; j >= 0; j--) for (int i = n - 1; i >= 0; i--) { Relax(i, j, 1, 0, 1); Relax(i, j, 0, 1, 1); Relax(i, j, 1, 1, 1.414f); Relax(i, j, -1, 1, 1.414f); }
	}

	MpRows(n, [&](int j)
	{
		for (int i = 0; i < n; i++)
		{
			const size_t k = (size_t)j * n + i;
			const int il = MxI(i - 1, 0), ir = MnI(i + 1, n - 1), ju = MxI(j - 1, 0), jd = MnI(j + 1, n - 1);
			// Hill shading from the north-west.
			const float hx = height[(size_t)j * n + ir] - height[(size_t)j * n + il], hz = height[(size_t)jd * n + i] - height[(size_t)ju * n + i];
			// (the plan's gentle slopes are exaggerated six times, as in Thornwake; real ground, far steeper, less so, and
			// the shading levels off on cliffs instead of going black)
			const float gain = (float)DetMath::Clamp(step / 10, 1.2, 6) / 6;
			float slope = (hx + hz) / (float)(step * 2) * gain;
			slope = slope / std::sqrt(1 + 36 * slope * slope * .5f);
			FRgb c;
			if (!parchment)
			{
				if (outside[k]) c = { .05f, .07f, .1f };
				else if (land[k] <= 0)
				{
					const float deep = MpClamp(-land[k] / .5f, 0, 1);
					c = MpMix({ .36f, .58f, .7f }, { .1f, .22f, .38f }, deep);
				}
				else
				{
					const float h = (float)(height[k] / top);
					if (biomes) c = biome[k];
					else c = h < .12f ? MpMix({ .42f, .55f, .3f }, { .5f, .58f, .33f }, MpClamp(h / .12f, 0, 1))
						: h < .35f ? MpMix({ .5f, .58f, .33f }, { .55f, .48f, .32f }, (h - .12f) / .23f)
						: h < .7f ? MpMix({ .55f, .48f, .32f }, { .5f, .47f, .45f }, (h - .35f) / .35f)
						: MpMix({ .5f, .47f, .45f }, { .93f, .94f, .96f }, MpClamp((h - .7f) / .3f, 0, 1));
					c = MpScale(c, biomes ? MpClamp(1 - slope * 6, .6f, 1.3f) : MpClamp(1 - slope * 6, .55f, 1.35f));
					if (water[k] == 1) c = lake;
					else if (water[k] == 2) c = river;
				}
			}
			else
			{
				// The sheet itself: warm paper with faint blotches and grain.
				const float blot = MpNoise(i / 90.f, j / 90.f) * .6f + MpNoise(i / 23.f + 40, j / 23.f + 11) * .3f + MpHash(i, j) * .1f;
				const FRgb paper = MpScale({ .9f, .81f, .62f }, .93f + .1f * blot);
				if (outside[k]) c = MpScale(paper, .93f);
				else if (land[k] <= 0)
				{
					const float deep = MpClamp(-land[k] / .5f, 0, 1);
					c = MpMix(paper, { .58f, .68f, .7f }, .2f + .16f * deep);
					// Lines of water drawn along the shore, each fainter than the last.
					const float d = offshore[k];
					const float lines[3] = { 4, 9, 16 };
					for (int q = 0; q < 3; q++)
					{
						const float off = std::fabs(d - lines[q]);
						if (off < 1.f) c = MpMix(c, inkWater, (1 - off) * (.7f - .2f * q));
					}
				}
				else
				{
					const float h = (float)(height[k] / top);
					FRgb wash = h < .12f ? MpMix({ .74f, .76f, .5f }, { .78f, .74f, .5f }, MpClamp(h / .12f, 0, 1))
						: h < .35f ? MpMix({ .78f, .74f, .5f }, { .7f, .58f, .4f }, (h - .12f) / .23f)
						: h < .7f ? MpMix({ .7f, .58f, .4f }, { .56f, .45f, .36f }, (h - .35f) / .35f)
						: MpMix({ .56f, .45f, .36f }, { .93f, .9f, .84f }, MpClamp((h - .7f) / .3f, 0, 1));
					// (a little of the biome: forests darker and greener, deserts paler, the far north white)
					wash = MpMix(wash, MpMix(biome[k], { .8f, .72f, .52f }, .4f), .3f);
					c = MpMix(paper, wash, .6f);
					// Shading in sepia: the lee side inked darker, the lit side left to the paper.
					const float shade = MpClamp(1 - slope * 5, .62f, 1.14f);
					c = shade < 1 ? MpMix(c, ink, (1 - shade) * 1.1f) : MpScale(c, shade);
					if (water[k]) c = MpMix(paper, { .5f, .6f, .62f }, .6f);
					// The coast and the banks in ink.
					const bool coast = land[(size_t)j * n + il] <= 0 || land[(size_t)j * n + ir] <= 0 || land[(size_t)ju * n + i] <= 0 || land[(size_t)jd * n + i] <= 0;
					const bool bank = !water[k] && (water[(size_t)j * n + il] || water[(size_t)j * n + ir] || water[(size_t)ju * n + i] || water[(size_t)jd * n + i]);
					if (coast) c = MpMix(c, ink, .85f);
					else if (bank) c = MpMix(c, inkWater, .6f);
				}
			}
			img[k] = c;
		}
	});

	// Rivers: lines, stronger and wider with the water they carry. (Zoomed in, the real channel is already drawn where
	// it is wider than a pixel; the line keeps brooks visible.)
	for (const FRiverReach& reach : plan.Hydrology->Reaches)
	{
		const std::vector<FPoint2>& pts = reach.Points;
		for (size_t q = 1; q < pts.size(); q++)
		{
			const double strength = DetMath::Clamp(std::log10(reach.Catchment[q] / 30 + 1) / 2.2, .25, 1);
			const double wide = reach.Width[q] / step;
			if (detailed && wide > 1.5) continue;
			MpLine(img, n, (pts[q - 1].X - x0) / step, (pts[q - 1].Z - z0) / step, (pts[q].X - x0) / step, (pts[q].Z - z0) / step,
				detailed ? 1 : DetMath::Clamp(wide, 1, 6), parchment ? inkWater : river, (float)strength * (parchment ? .8f : 1.f));
		}
	}

	FMapImage out;
	out.Pixels = n; out.CentreX = cx; out.CentreZ = cz; out.Span = span; out.Style = style; out.bDetailed = detailed;
	out.Bgra.resize(count * 4);
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++)
		{
			const size_t k = (size_t)j * n + i;
			const FRgb c = img[k];
			out.Bgra[k * 4] = (uint8_t)(MpClamp(c.B, 0, 1) * 255);
			out.Bgra[k * 4 + 1] = (uint8_t)(MpClamp(c.G, 0, 1) * 255);
			out.Bgra[k * 4 + 2] = (uint8_t)(MpClamp(c.R, 0, 1) * 255);
			out.Bgra[k * 4 + 3] = 255;
		}
	return out;
}
}
