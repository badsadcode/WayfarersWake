// Port of Thornwake Game/Rewrite/GroundCover.cs (Rocks, Props, RockyShore) and the shapes of RockMeshes.cs,
// PropCatalog.cs and Visuals.Stump.
#include "WwRocks.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <tuple>

namespace ww
{
namespace
{
float RkUnit(u64 h, int shift) { return (float)Hashing::Unit(Hashing::Mix(h + (u64)shift)); }
int RkClampI(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
int RkNearest(const FFloraGrid& g, double x, double z)
{
	int i = RkClampI((int)std::nearbyint(x / g.Spacing), 0, g.Side - 1), j = RkClampI((int)std::nearbyint(z / g.Spacing), 0, g.Side - 1);
	return j * g.Side + i;
}
bool RkAt(const FFloraGrid& g, double x, double z, float& height, float& up, float& forest, bool& wet)
{
	height = 0; up = 0; forest = 0; wet = false;
	double gx = x / g.Spacing, gz = z / g.Spacing;
	int i = (int)std::floor(gx), j = (int)std::floor(gz);
	if (i < 0 || j < 0 || i >= g.Side - 1 || j >= g.Side - 1) return false;
	double fx = gx - i, fz = gz - j;
	int n = g.Side, a = j * n + i, b = a + 1, c = a + n, d = c + 1;
	double ha = g.Height[a], hb = g.Height[b], hc = g.Height[c], hd = g.Height[d];
	height = (float)(fx + fz <= 1 ? ha + (hb - ha) * fx + (hc - ha) * fz : hd + (hc - hd) * (1 - fx) + (hb - hd) * (1 - fz));
	int nearest = (fz < .5 ? j : j + 1) * n + (fx < .5 ? i : i + 1);
	up = g.Up[nearest];
	forest = (float)(g.Forest[a] * (1 - fx) * (1 - fz) + g.Forest[b] * fx * (1 - fz) + g.Forest[c] * (1 - fx) * fz + g.Forest[d] * fx * fz);
	wet = g.Samples[a].Water != EWaterKind::None || g.Samples[b].Water != EWaterKind::None || g.Samples[c].Water != EWaterKind::None || g.Samples[d].Water != EWaterKind::None;
	return true;
}
bool HeightAt(const FFloraGrid& g, double x, double z, float& height) { float up, forest; bool wet; return RkAt(g, x, z, height, up, forest, wet); }

void RkCells(double minX, double minZ, double size, double cell, u64 salt, const std::function<void(double, double, u64)>& emit)
{
	i64 c0x = (i64)std::floor(minX / cell), c0z = (i64)std::floor(minZ / cell);
	i64 c1x = (i64)std::ceil((minX + size) / cell), c1z = (i64)std::ceil((minZ + size) / cell);
	for (i64 cz = c0z; cz < c1z; cz++)
		for (i64 cx = c0x; cx < c1x; cx++)
		{
			u64 h = Hashing::Lattice(cx, cz, salt);
			double x = (cx + Hashing::Unit(h)) * cell - minX, z = (cz + Hashing::Unit(Hashing::Mix(h))) * cell - minZ;
			if (x < 0 || z < 0 || x >= size || z >= size) continue;
			emit(x, z, Hashing::Mix(h ^ 0x5DEECE66DULL));
		}
}

// ---- a small 3x3 basis in Thornwake's axes (columns are the local x, y, z axes)
struct FBasis
{
	float X[3] = { 1, 0, 0 }, Y[3] = { 0, 1, 0 }, Z[3] = { 0, 0, 1 };
	static FBasis Turn(const float axis[3], float angle)
	{
		// Rodrigues: the three columns are the rotated unit axes.
		float c = std::cos(angle), s = std::sin(angle), t = 1 - c, x = axis[0], y = axis[1], z = axis[2];
		FBasis b;
		b.X[0] = t * x * x + c;     b.X[1] = t * x * y + s * z; b.X[2] = t * x * z - s * y;
		b.Y[0] = t * x * y - s * z; b.Y[1] = t * y * y + c;     b.Y[2] = t * y * z + s * x;
		b.Z[0] = t * x * z + s * y; b.Z[1] = t * y * z - s * x; b.Z[2] = t * z * z + c;
		return b;
	}
	FBasis operator*(const FBasis& o) const
	{
		FBasis r;
		auto Mul = [&](const float v[3], float out[3]) { for (int k = 0; k < 3; k++) out[k] = X[k] * v[0] + Y[k] * v[1] + Z[k] * v[2]; };
		Mul(o.X, r.X); Mul(o.Y, r.Y); Mul(o.Z, r.Z);
		return r;
	}
	/// Godot's Basis.Scaled: each axis scaled in the parent's frame (the rows).
	FBasis Scaled(float sx, float sy, float sz) const
	{
		FBasis r = *this;
		for (float* col : { r.X, r.Y, r.Z }) { col[0] *= sx; col[1] *= sy; col[2] *= sz; }
		return r;
	}
};
const float UpAxis[3] = { 0, 1, 0 }, RightAxis[3] = { 1, 0, 0 };

void Emit(std::vector<FRockInstance>& out, int kind, const FBasis& b, float x, float y, float z, const FRockGrid& g, double tx, double tz)
{
	FRockInstance r;
	r.Kind = kind;
	for (int k = 0; k < 3; k++) { r.Ax[k] = b.X[k]; r.Ay[k] = b.Y[k]; r.Az[k] = b.Z[k]; }
	r.X = x; r.Y = y; r.Z = z;
	// Biome rock colour relative to the default stone (the meshes' own colour carries that already), and the rock look.
	int k = RkNearest(g.Base, tx, tz);
	if (g.Rock) { r.R = g.Rock[k * 3] / .467f; r.G = g.Rock[k * 3 + 1] / .463f; r.B = g.Rock[k * 3 + 2] / .416f; }
	r.Look = g.RockLook ? g.RockLook[k] : 0;
	out.push_back(r);
}
}

void FRocks::FarTrees(const FFloraGrid& g, int level, double size, double sea, u64 seed, double density, std::vector<FFarTree>& out)
{
	static const float Crown[17][3] = {
		{ .10f, .17f, .09f }, { .11f, .18f, .09f }, { .10f, .16f, .08f }, { .08f, .14f, .08f }, { .09f, .15f, .08f }, // pines, spruces
		{ .16f, .25f, .09f }, { .15f, .24f, .08f }, { .17f, .26f, .09f },                                             // oaks
		{ .24f, .34f, .12f }, { .23f, .33f, .11f }, { .25f, .35f, .12f },                                             // green birches
		{ .42f, .14f, .06f }, { .18f, .28f, .10f }, { .22f, .32f, .12f }, { .17f, .23f, .10f },                       // maple, poplar, willow, sparse
		{ .50f, .36f, .09f }, { .48f, .33f, .08f } };                                                                 // autumn birches
	static const bool Conifer[17] = { true, true, true, true, true, false, false, false, false, false, false, false, false, false, true, false, false };
	static const float HeightOf[17] = { 16, 15, 17, 19, 18, 13, 14, 12, 14, 13, 15, 12, 18, 11, 9, 13, 13 };
	static const int Low[] = { 5, 6, 7, 8, 9, 10, 11, 12, 15, 16, 0, 1 }, Mid[] = { 0, 1, 2, 3, 4, 8, 9, 5, 6, 15 }, High[] = { 0, 1, 2, 3, 4, 3, 4, 14 };
	// Cell and size per level so cards cover the ground about as much as the full trees they replace (one per ~6.4 m cell).
	double cell = level <= 2 ? 7 : level == 3 ? 8 : level == 4 ? 13 : level == 5 ? 21 : 34;
	float grow = level <= 2 ? 1.05f : level == 3 ? 1.2f : level == 4 ? 1.6f : level == 5 ? 2.2f : 3.f;
	float keep = (float)Mn(1.15, density);
	RkCells(g.X0, g.Z0, size, cell, seed ^ 0xFA77EEULL, [&](double x, double z, u64 h)
	{
		float height, up, forest; bool wet;
		if (!RkAt(g, x, z, height, up, forest, wet) || wet || up < .65f) return;
		if (RkUnit(h, 1) > forest * .92f * keep) return;
		double elevation = height - sea;
		int species = elevation < 550 ? Low[Hashing::Mix(h + 99) % 12] : elevation < 1250 ? Mid[Hashing::Mix(h + 99) % 10] : High[Hashing::Mix(h + 99) % 8];
		float scale = (.8f + .45f * RkUnit(h, 2)) * grow;
		out.push_back({ (float)x, height - .3f, (float)z, HeightOf[species] * scale, Crown[species][0], Crown[species][1], Crown[species][2], Conifer[species] });
	});
}

FSoup FPropShapes::Card()
{
	FSoup s;
	auto Vertex = [&](float x, float y, float z, float u, float v, float top)
	{
		s.Position.insert(s.Position.end(), { x, y, z });
		s.Normal.insert(s.Normal.end(), { 0.f, 1.f, 0.f });
		s.Colour.insert(s.Colour.end(), { u, v, top });
	};
	auto Quad = [&](const float a[3], const float b[3], const float c[3], const float d[3], float top)
	{
		// a, b along the bottom (u 0..1, v 1), d, c along the top (v 0)
		Vertex(a[0], a[1], a[2], 0, 1, top); Vertex(b[0], b[1], b[2], 1, 1, top); Vertex(c[0], c[1], c[2], 1, 0, top);
		Vertex(a[0], a[1], a[2], 0, 1, top); Vertex(c[0], c[1], c[2], 1, 0, top); Vertex(d[0], d[1], d[2], 0, 0, top);
	};
	const float w = .31f; // (half width: broadleaf crowns are 0.62 of the height wide; conifers are drawn narrower)
	{ const float a[3] = { -w, 0, 0 }, b[3] = { w, 0, 0 }, c[3] = { w, 1, 0 }, d[3] = { -w, 1, 0 }; Quad(a, b, c, d, 0); }
	{ const float a[3] = { 0, 0, -w }, b[3] = { 0, 0, w }, c[3] = { 0, 1, w }, d[3] = { 0, 1, -w }; Quad(a, b, c, d, 0); }
	{ const float a[3] = { -w, .6f, w }, b[3] = { w, .6f, w }, c[3] = { w, .6f, -w }, d[3] = { -w, .6f, -w }; Quad(a, b, c, d, 1); }
	return s;
}

float FRocks::RockyShore(double worldX, double worldZ, double aboveWater)
{
	if (aboveWater < -1.2 || aboveWater > 3) return 0;
	double patch = Noise::Value(worldX / 160, worldZ / 160, 0x5407EULL) * .7 + Noise::Value(worldX / 45, worldZ / 45, 0x5408ULL) * .3;
	float band = (float)(DetMath::Smooth((aboveWater + 1.2) / .6) * (1 - DetMath::Smooth((aboveWater - 1.8) / 1.2)));
	return (float)DetMath::Smooth((patch - .15) / .25) * band;
}

void FRocks::Scatter(const FRockGrid& grid, int level, double size, double sea, u64 seed, double amount, std::vector<FRockInstance>& out)
{
	const FFloraGrid& g = grid.Base;
	float a = (float)amount, a0 = a < 1 ? a : 1;
	auto Prop = [&](double x, double z) { return grid.PropRocks ? grid.PropRocks[RkNearest(g, x, z)] : 1.f; };
	auto Mountain = [&](double x, double z) { return (float)g.Samples[RkNearest(g, x, z)].Mountain; };
	auto Scree = [&](double x, double z) { return (float)g.Samples[RkNearest(g, x, z)].Scree; };
	auto Place = [&](int kind, double x, double z, u64 h, float baseSize, float sizeRange, float sink)
	{
		float height, up, forest; bool wet;
		if (!RkAt(g, x, z, height, up, forest, wet)) return;
		if (wet && kind == Stone) return;
		float s = baseSize + sizeRange * RkUnit(h, 5) * RkUnit(h, 6);
		// Random tilt, partly buried so rocks sit in the ground rather than on it.
		FBasis b = FBasis::Turn(UpAxis, RkUnit(h, 7) * 6.2831855f) * FBasis::Turn(RightAxis, (RkUnit(h, 8) - .5f) * .6f);
		b = b.Scaled(s * (.8f + .5f * RkUnit(h, 9)), s * (.55f + .5f * RkUnit(h, 10)), s * (.8f + .5f * RkUnit(h, 11)));
		Emit(out, kind, b, (float)x, height - s * sink, (float)z, grid, x, z);
	};
	if (level == 0)
		RkCells(g.X0, g.Z0, size, 3.5, seed ^ 0x570EULL, [&](double x, double z, u64 h)
		{
			float height, up, forest; bool wet;
			if (!RkAt(g, x, z, height, up, forest, wet) || height - sea < .3) return;
			float steep = 1 - up;
			// (more stones on slopes and banks, and strewn thick over scree)
			if (RkUnit(h, 1) > (.1f + 1.2f * steep + 2.5f * Scree(x, z)) * a * Prop(x, z)) return;
			Place(Stone, x, z, h, .12f, .45f, .35f);
		});
	if (level <= 1)
		RkCells(g.X0, g.Z0, size, 14, seed ^ 0x70C4ULL, [&](double x, double z, u64 h)
		{
			float height, up, forest; bool wet;
			if (!RkAt(g, x, z, height, up, forest, wet) || height - sea < .5) return;
			float steep = 1 - up, mountain = Mountain(x, z);
			if (RkUnit(h, 1) > (.05f + 1.5f * steep + .3f * mountain) * a * Prop(x, z)) return;
			Place(Rock, x, z, h, .5f, 1.2f, .3f);
		});
	RkCells(g.X0, g.Z0, size, 48, seed ^ 0xB0D1ULL, [&](double x, double z, u64 h)
	{
		float height, up, forest; bool wet;
		if (!RkAt(g, x, z, height, up, forest, wet) || height - sea < 1) return;
		float steep = 1 - up, mountain = Mountain(x, z);
		float chance = (.04f + 2.2f * (steep - .1f > 0 ? steep - .1f : 0) + .5f * mountain) * a * Prop(x, z);
		if (RkUnit(h, 1) > chance) return;
		// Steep or mountainous ground grows a small outcrop: one big boulder with smaller ones around it.
		int extra = steep > .3f || mountain > .3f ? 2 + (int)(RkUnit(h, 2) * 4) : 0;
		Place(Boulder, x, z, h, 1.8f, 3.5f + 3 * mountain, .35f);
		for (int i = 0; i < extra; i++)
		{
			u64 hh = Hashing::Mix(h + (u64)(i + 1) * 0x9E37ULL);
			double ox = x + (RkUnit(hh, 1) - .5f) * 9, oz = z + (RkUnit(hh, 2) - .5f) * 9;
			Place(Boulder, ox, oz, hh, 1.f, 2.5f, .4f);
		}
	});
	// River stones: in river water under 2.2 m deep, sized to the depth so 0.15-0.8 m stands out of the water, in patches
	// (riffles) about 60 m across, more in mountain streams and shallow water; none on a fall's face or a steep chute.
	if (level <= 1)
		RkCells(g.X0, g.Z0, size, 7, seed ^ 0x51ED5ULL, [&](double x, double z, u64 h)
		{
			int i = (int)std::nearbyint(x / g.Spacing), j = (int)std::nearbyint(z / g.Spacing);
			if (i < 1 || j < 1 || i >= g.Side - 1 || j >= g.Side - 1) return;
			const FTerrainSample& t = g.Samples[j * g.Side + i];
			float ground;
			if (t.Water != EWaterKind::River || !HeightAt(g, x, z, ground)) return;
			double depth = t.WaterLevel - ground;
			if (depth < .05 || depth > 2.2) return;
			double steep = 0;
			for (int k : { j * g.Side + i - 1, j * g.Side + i + 1, (j - 1) * g.Side + i, (j + 1) * g.Side + i })
				if (g.Samples[k].Water == EWaterKind::River) steep = Mx(steep, std::fabs(g.Samples[k].WaterLevel - t.WaterLevel) / g.Spacing);
			if (steep > .15) return;
			double wx = g.X0 + x, wz = g.Z0 + z;
			float patch = (float)DetMath::Smooth((Noise::Fbm(wx, wz, 60, 2, 0, seed ^ 0x51EDULL) + .1) / .25);
			float shallow = (float)(1 - depth / 2.2), mountain = Mountain(x, z);
			if (RkUnit(h, 1) > (.08f + .8f * mountain + .35f * shallow) * patch * a) return;
			float rise = .15f + .65f * RkUnit(h, 2) * RkUnit(h, 3);
			float tall = (float)Mn(3.6, (depth + rise) / .75f), wide = tall * (1.1f + .5f * RkUnit(h, 4));
			if (wide > 4) wide = 4;
			FBasis b = FBasis::Turn(UpAxis, RkUnit(h, 7) * 6.2831855f) * FBasis::Turn(RightAxis, (RkUnit(h, 8) - .5f) * .4f);
			b = b.Scaled(wide * (.8f + .4f * RkUnit(h, 9)), tall, wide * (.8f + .4f * RkUnit(h, 11)));
			// (a quarter of it buried in the bed: its top stands at three quarters of its height over the bed)
			Emit(out, wide > 1.6f ? Boulder : Rock, b, (float)x, ground + tall * .25f, (float)z, grid, x, z);
		});
	// Stones along steep river banks: they straddle the waterline there every few metres, half in the water.
	if (level <= 1)
		RkCells(g.X0, g.Z0, size, 3.5, seed ^ 0xBA4CULL, [&](double x, double z, u64 h)
		{
			float ground, up, forest; bool wet;
			if (!RkAt(g, x, z, ground, up, forest, wet) || up > .9f) return;
			// The nearest river water within three grid steps, and its level.
			int ci = (int)std::nearbyint(x / g.Spacing), cj = (int)std::nearbyint(z / g.Spacing);
			double level2 = 0, best = 1e300; bool found = false;
			for (int dj = -3; dj <= 3; dj++)
				for (int di = -3; di <= 3; di++)
				{
					int i = ci + di, j = cj + dj;
					if (i < 0 || j < 0 || i >= g.Side || j >= g.Side) continue;
					const FTerrainSample& t = g.Samples[j * g.Side + i];
					if (t.Water != EWaterKind::River) continue;
					double d2 = di * di + dj * dj;
					if (d2 < best) { best = d2; level2 = t.WaterLevel; found = true; }
				}
			if (!found || ground < level2 - .6 || ground > level2 + 1.2) return;
			if (RkUnit(h, 1) > .8f * a) return;
			float wide = .9f + 1.6f * RkUnit(h, 2) * (.5f + .5f * RkUnit(h, 3)), tall = wide * (.6f + .3f * RkUnit(h, 4));
			FBasis b = FBasis::Turn(UpAxis, RkUnit(h, 7) * 6.2831855f) * FBasis::Turn(RightAxis, (RkUnit(h, 8) - .5f) * .5f);
			b = b.Scaled(wide * (.8f + .4f * RkUnit(h, 9)), tall, wide * (.8f + .4f * RkUnit(h, 11)));
			// (its middle at the water's level: half in, half out)
			Emit(out, wide > 1.6f ? Boulder : Rock, b, (float)x, (float)level2 - tall * .1f, (float)z, grid, x, z);
		});
	// Rock walls on cliff faces: big flat-faced slabs set into the face, hiding the stretched ground (nearest tiles only).
	if (level == 0 && grid.Normal)
		RkCells(g.X0, g.Z0, size, 6.5, seed ^ 0xC1FFULL, [&](double x, double z, u64 h)
		{
			float height, up, forest; bool wet;
			if (!RkAt(g, x, z, height, up, forest, wet) || up > .55f || height - sea < -1) return;
			const float* n = &grid.Normal[RkNearest(g, x, z) * 3];
			float ox = n[0], oz = n[2], len2 = ox * ox + oz * oz;
			if (len2 < .05f) return;
			float len = std::sqrt(len2); ox /= len; oz /= len;
			// A real face: the ground keeps dropping in front of it.
			float below;
			if (!HeightAt(g, x + ox * 4, z + oz * 4, below) || height - below < 2.5f) return;
			if (RkUnit(h, 1) > .75f * a0) return;
			// along = up x outward
			float along[3] = { oz, 0, -ox }, outward[3] = { ox, 0, oz };
			float wide = 3 + 4 * RkUnit(h, 2), tall = 2.5f + 4 * RkUnit(h, 3), thick = wide * (.55f + .25f * RkUnit(h, 4)); // chunky blocks, not plates
			FBasis tilt = FBasis::Turn(along, (RkUnit(h, 5) - .5f) * .3f) * FBasis::Turn(outward, (RkUnit(h, 6) - .5f) * .25f);
			FBasis frame;
			for (int k = 0; k < 3; k++) { frame.X[k] = along[k] * wide; frame.Y[k] = UpAxis[k] * tall; frame.Z[k] = outward[k] * thick; }
			FBasis b = tilt * frame;
			Emit(out, CliffSlab, b, (float)x - ox * thick * .45f, height - tall * .2f, (float)z - oz * thick * .45f, grid, x, z);
		});
	// Rocks of tall cliffs: on a face, the wall is measured along the way down, from its lip to its foot, and where it stands
	// 8 m or more, a rock sized to it (a third to two thirds of its height, 5-35 m across; several up a tall one) stands half
	// out of the face, a block fallen at its foot now and then. Levels 0-1; level 1 has walls over 15 m only, in coarse blocks.
	auto RiverAlong = [&](double x, double z, double ax, double az, double half)
	{
		for (double t = -half; t <= half; t += 3)
		{
			int i = (int)std::nearbyint((x + ax * t) / g.Spacing), j = (int)std::nearbyint((z + az * t) / g.Spacing);
			if (i < 0 || j < 0 || i >= g.Side || j >= g.Side) continue;
			EWaterKind w = g.Samples[j * g.Side + i].Water;
			if (w == EWaterKind::River || w == EWaterKind::Pond) return true;
		}
		return false;
	};
	auto Tinted = [&](std::vector<FRockInstance>& list, float shade, float warm)
	{
		FRockInstance& r = list.back();
		r.R *= shade * (1 + warm); r.G *= shade; r.B *= shade * (1 - warm);
	};
	if (level <= 1)
		RkCells(g.X0, g.Z0, size, level == 0 ? 7 : 10, seed ^ 0xC1F2ULL, [&](double x, double z, u64 h)
		{
			// Near a face: the ground 4 m either way differs by over 6 m. Downhill is out from the face.
			float height, ex, wx, sz, nz;
			if (!HeightAt(g, x, z, height)) return;
			float r4 = (float)Mx(4, g.Spacing * 2);
			if (!HeightAt(g, x + r4, z, ex) || !HeightAt(g, x - r4, z, wx) || !HeightAt(g, x, z + r4, sz) || !HeightAt(g, x, z - r4, nz)) return;
			float ox = wx - ex, oz = nz - sz, olen = std::sqrt(ox * ox + oz * oz);
			if (olen < 6) return;
			ox /= olen; oz /= olen;
			float step = (float)Mx(1.5, g.Spacing);
			float foot = height, lip = height, footAt = 0, lipAt = 0, probe;
			for (float d = step; d <= 70; d += step)
			{
				if (!HeightAt(g, x + ox * d, z + oz * d, probe) || foot - probe < step * .5f) break;
				foot = probe; footAt = d;
			}
			for (float d = step; d <= 70; d += step)
			{
				if (!HeightAt(g, x - ox * d, z - oz * d, probe) || probe - lip < step * .5f) break;
				lip = probe; lipAt = d;
			}
			float wall = lip - foot;
			// (a cliff, not a steep hillside: steeper than ~50 degrees from the lip to the foot)
			if (wall < (level == 0 ? 8 : 15) || wall < 1.2f * (lipAt + footAt) || RkUnit(h, 1) > .9f * a0) return;
			// along = up x outward
			float along[3] = { oz, 0, -ox }, outward[3] = { ox, 0, oz };
			// (none over a waterfall: no river on the lip, the face or the foot across the width a rock may span)
			if (RiverAlong(x - ox * lipAt, z - oz * lipAt, along[0], along[2], 30) || RiverAlong(x, z, along[0], along[2], 30)
				|| RiverAlong(x + ox * footAt, z + oz * footAt, along[0], along[2], 30)) return;
			int count = 1 + (int)(wall / 25);
			float cellSize = level == 0 ? 7.f : 10.f;
			// How far the wall runs either way: no rock wider than that.
			float extent = 40;
			for (int sgn : { -1, 1 })
				for (float d = 3; d <= 40; d += 3)
				{
					double px = x - ox * lipAt * .5f + along[0] * d * sgn, pz = z - oz * lipAt * .5f + along[2] * d * sgn;
					if (!HeightAt(g, px, pz, probe) || probe < foot + wall * .55f) { if (d < extent) extent = d; break; }
				}
			float widest = 2 * extent * .85f + 2;
			// A needle or a narrow spur: no rocks on it at all; only a block fallen at its foot now and then.
			bool slender = extent < 12;
			auto Frame = [&](float wide, float tall, float thick)
			{
				FBasis f;
				for (int k = 0; k < 3; k++) { f.X[k] = along[k] * wide; f.Y[k] = UpAxis[k] * tall; f.Z[k] = outward[k] * thick; }
				return f;
			};
			for (int i = 0; i < (slender ? 0 : count); i++)
			{
				u64 hh = Hashing::Mix(h + (u64)(i + 1) * 0x2F1BULL);
				float tall = (float)Mn(wall * (.35f + .3f * RkUnit(hh, 2)), wall / count * 1.6f + 4);
				float wide = tall * (.9f + .8f * RkUnit(hh, 3));
				wide = wide < 5 ? 5 : wide > 35 ? 35 : wide;
				if (wide > widest) wide = widest;
				float thick = wide * (.5f + .25f * RkUnit(hh, 4));
				if (tall > wide * 2.2f) tall = wide * 2.2f;
				if (RkUnit(hh, 11) > cellSize * 2.4f / wide) continue;
				// Where the face is at the rock's middle height (a straight face from the lip to the foot).
				float y = foot + tall * .4f + (wall - tall * .8f) * ((i + RkUnit(hh, 5)) / count), share = (lip - y) / wall;
				share = share < 0 ? 0 : share > 1 ? 1 : share;
				float across = -lipAt + (lipAt + footAt) * share;
				float slide = (RkUnit(hh, 6) - .5f) * 4, out2 = across - thick * .05f;
				FBasis tilt = FBasis::Turn(along, (RkUnit(hh, 7) - .5f) * .35f) * FBasis::Turn(outward, (RkUnit(hh, 8) - .5f) * .3f) * FBasis::Turn(UpAxis, (RkUnit(hh, 9) - .5f) * .5f);
				Emit(out, (level == 0 ? CliffRock : CliffCoarse) + (int)(RkUnit(hh, 10) * (CliffShapes - .001f)), tilt * Frame(wide, tall, thick),
					(float)x + along[0] * slide + ox * out2, y, (float)z + along[2] * slide + oz * out2, grid, x, z);
				Tinted(out, .82f + .36f * RkUnit(hh, 12), (RkUnit(hh, 13) - .5f) * .12f); // (each its own: all one tint was bland)
			}
			// The lip broken up: two or three small rocks along each cell's stretch of the edge, flat rather than tall, leaning
			// out over the face, their tops only 0.4-1.5 m above the edge.
			for (int n = 0, lips = slender ? 0 : 2 + (int)(RkUnit(h, 14) * 2); n < lips; n++)
			{
				u64 lh = Hashing::Mix(h + 0x11BULL + (u64)n * 0x9E3BULL);
				if (RkUnit(lh, 12) > .85f) continue;
				float lipTall = wall * .08f;
				lipTall = (lipTall < 1.4f ? 1.4f : lipTall > 3.5f ? 3.5f : lipTall) * (.7f + .5f * RkUnit(lh, 1));
				float lipWide = lipTall * (1.6f + 1.2f * RkUnit(lh, 2));
				if (lipWide > widest) lipWide = widest;
				float lipThick = lipWide * (.55f + .3f * RkUnit(lh, 3));
				float top = lip + .4f + 1.1f * RkUnit(lh, 4); // (the block's top is 0.42 of its height above its middle)
				float back = lipAt - lipThick * (.05f + .25f * RkUnit(lh, 5)), run = ((n + RkUnit(lh, 6)) / lips - .5f) * cellSize * 1.3f;
				FBasis lipTilt = FBasis::Turn(along, (RkUnit(lh, 7) - .5f) * .25f) * FBasis::Turn(outward, .1f + .15f * RkUnit(lh, 8)) * FBasis::Turn(UpAxis, RkUnit(lh, 9) * 6.2831855f);
				Emit(out, (level == 0 ? CliffLip : CliffCoarse) + (int)(RkUnit(lh, 11) * (CliffShapes - .001f)), lipTilt * Frame(lipWide, lipTall, lipThick),
					(float)x - ox * back + along[0] * run, top - lipTall * .42f, (float)z - oz * back + along[2] * run, grid, x, z);
				Tinted(out, .82f + .36f * RkUnit(lh, 10), 0);
			}
			if (RkUnit(h, 9) < .35f)
			{
				float block = (float)Mn(wall * .18f, 6) * (.6f + .6f * RkUnit(h, 10));
				FBasis b = FBasis::Turn(UpAxis, RkUnit(h, 11) * 6.2831855f).Scaled(block, block * .7f, block);
				Emit(out, Boulder, b, (float)x + ox * (footAt + block * .4f), foot - block * .3f, (float)z + oz * (footAt + block * .4f), grid, x, z);
			}
		});
	// The rock kit's formations: in outcrops (patches of rocky ground a few hundred metres across, from a world noise), on
	// crests and hilltops more than in hollows, more where the land is rocky and in the mountains, few in forest; a lone one
	// now and then outside them. A group is one kind (by the land's rock), now and then another among them.
	if (level <= 2 && grid.Normal)
		RkCells(g.X0, g.Z0, size, 55, seed ^ 0xF0E3ULL, [&](double x, double z, u64 h)
		{
			// (not on slopes over ~35 degrees: set upright there, the downhill side stood on nothing)
			float height, up, forest; bool wet;
			if (!RkAt(g, x, z, height, up, forest, wet) || wet || up < .82f || height - sea < 1.5f) return;
			double wx = g.X0 + x, wz = g.Z0 + z;
			float outcrop = (float)DetMath::Smooth((Noise::Value(wx / 520, wz / 520, 0x0C7C0ULL) * .7 + Noise::Value(wx / 170, wz / 170, 0x0C7C1ULL) * .3 - .55) / .14);
			// Crest or hollow: this point against the ground 35 m round it.
			float round = 0, hr; int n = 0;
			for (int q = 0; q < 4; q++)
				if (HeightAt(g, x + 35 * std::cos(q * 3.141592653589793 / 2), z + 35 * std::sin(q * 3.141592653589793 / 2), hr)) { round += hr; n++; }
			float crest = n > 0 ? (height - round / n) / 6 : 0;
			crest = crest < -1 ? -1 : crest > 1 ? 1 : crest;
			float mountain = Mountain(x, z), rocks = Prop(x, z);
			float chance = (.025f + .6f * outcrop * (1 + .8f * crest)) * a0 * (rocks < 2.2f ? rocks : 2.2f) * (1 + mountain)
				* (1 - .75f * (forest * 1.5f < 1 ? forest * 1.5f : 1)) * (crest < -.3f ? .3f : 1);
			if (RkUnit(h, 1) > chance) return;
			int look = grid.RockLook ? (int)std::nearbyint(grid.RockLook[RkNearest(g, x, z)]) : 0;
			float roll = RkUnit(h, 9) < .82f ? (float)Noise::Value(wx / 520, wz / 520, 0x0C7C2ULL) : RkUnit(h, 10);
			// columns where it is basalt or mountain, slab stacks in sandstone and layered country, tors in granite and mossy country
			int kind = mountain > .45f || look == 2 ? (roll < .7f ? 0 : 2) : look == 3 || look == 0 ? (roll < .6f ? 1 : 2) : roll < .65f ? 3 : roll < .85f ? 2 : 0;
			// (bigger in the outcrops' hearts and on crests, smaller at their edges)
			float s = (.55f + .55f * RkUnit(h, 2) * RkUnit(h, 3)) * (.75f + .35f * outcrop + .15f * (crest > 0 ? crest : 0));
			// (leaning a third of the way with the ground, sunk deeper on a slope, so no side stands on air)
			const float* nrm = &grid.Normal[RkNearest(g, x, z) * 3];
			// axis = up x normal
			float axis[3] = { nrm[2], 0, -nrm[0] }, alen = std::sqrt(axis[0] * axis[0] + axis[2] * axis[2]);
			if (alen > 1e-3f) { axis[0] /= alen; axis[2] /= alen; } else { axis[0] = 1; axis[2] = 0; }
			float angle = std::acos(nrm[1] < -1 ? -1 : nrm[1] > 1 ? 1 : nrm[1]);
			FBasis fb = (FBasis::Turn(axis, angle * .35f) * FBasis::Turn(UpAxis, RkUnit(h, 4) * 6.2831855f)).Scaled(s, s, s);
			Emit(out, Formation + kind * 4 + (int)(RkUnit(h, 5) * (4 - .001f)), fb, (float)x, height - (.6f + 6.f * (1 - up)) * s, (float)z, grid, x, z);
		});
	// Scree: rubble over the talus under walls, rocks and boulders of every size, thickest where most has fallen.
	if (level <= 1)
		RkCells(g.X0, g.Z0, size, 3.5, seed ^ 0x5C2E33ULL, [&](double x, double z, u64 h)
		{
			float scree = Scree(x, z);
			if (scree < .05f || RkUnit(h, 1) > .8f * scree * a0) return;
			bool big = RkUnit(h, 2) < .18f;
			Place(big ? Boulder : Rock, x + (RkUnit(h, 3) - .5f) * 3, z + (RkUnit(h, 4) - .5f) * 3, h, big ? .9f : .35f, big ? 1.8f : 1.1f, .3f);
		});
	// Rocky shores: in patches along the sea and lakes, blocky rocks at and just above the waterline.
	if (level <= 1)
		RkCells(g.X0, g.Z0, size, 3.2, seed ^ 0x5407EULL, [&](double x, double z, u64 h)
		{
			float height;
			if (!HeightAt(g, x, z, height)) return;
			if (RockyShore(g.X0 + x, g.Z0 + z, height - sea) < .3f || RkUnit(h, 1) > .45f * a0) return;
			float s = .8f + 2.6f * RkUnit(h, 2) * RkUnit(h, 2);
			FBasis b = FBasis::Turn(UpAxis, RkUnit(h, 3) * 6.2831855f) * FBasis::Turn(RightAxis, (RkUnit(h, 4) - .5f) * .5f);
			b = b.Scaled(s * (.8f + .5f * RkUnit(h, 5)), s * (.5f + .4f * RkUnit(h, 6)), s * (.8f + .5f * RkUnit(h, 7)));
			Emit(out, BigBoulder, b, (float)x, height - s * .3f, (float)z, grid, x, z);
		});
	// Scree at the foot of cliffs: flat ground with a face rising a few metres right next to it.
	if (level <= 1)
		RkCells(g.X0, g.Z0, size, 5, seed ^ 0x5C2EEULL, [&](double x, double z, u64 h)
		{
			float height, up, forest; bool wet;
			if (!RkAt(g, x, z, height, up, forest, wet) || wet || up < .75f || height - sea < -.2) return;
			for (int d = 0; d < 8; d++)
			{
				float ang = d * 3.14159265f / 4, dx = std::cos(ang), dz = std::sin(ang), above;
				if (!HeightAt(g, x + dx * 3.5, z + dz * 3.5, above) || above - height < 2.8f) continue;
				if (RkUnit(h, 1) > .7f * a0) return;
				int count = 1 + (int)(RkUnit(h, 2) * 3);
				for (int i = 0; i < count; i++)
				{
					u64 hh = Hashing::Mix(h + (u64)(i + 3) * 0x51EDULL);
					double bx = x + dx * (.8 + RkUnit(hh, 1) * 1.2) + (RkUnit(hh, 2) - .5f) * 3, bz = z + dz * (.8 + RkUnit(hh, 1) * 1.2) + (RkUnit(hh, 3) - .5f) * 3;
					Place(i == 0 ? Boulder : Rock, bx, bz, hh, i == 0 ? .9f : .5f, i == 0 ? 1.6f : 1.f, .35f);
				}
				return;
			}
		});
}

void FRocks::Props(const FRockGrid& grid, double size, double sea, u64 seed, double amount, std::vector<FRockInstance>& out)
{
	const FFloraGrid& g = grid.Base;
	float a = (float)amount;
	auto Logs = [&](double x, double z) { return grid.PropLogs ? grid.PropLogs[RkNearest(g, x, z)] : 1.f; };
	auto Add = [&](int kind, float yaw, float scale, float x, float y, float z)
	{
		FRockInstance r;
		r.Kind = kind;
		FBasis b = FBasis::Turn(UpAxis, yaw).Scaled(scale, scale, scale);
		for (int k = 0; k < 3; k++) { r.Ax[k] = b.X[k]; r.Ay[k] = b.Y[k]; r.Az[k] = b.Z[k]; }
		r.X = x; r.Y = y; r.Z = z;
		out.push_back(r);
	};
	RkCells(g.X0, g.Z0, size, 9, seed ^ 0xB054ULL, [&](double x, double z, u64 h)
	{
		float height, up, forest; bool wet;
		if (!RkAt(g, x, z, height, up, forest, wet) || wet || up < .8f || height - sea < 2) return;
		// Most bushes at forest edges (cover around 0.3-0.7), a few out in the meadows; fewer where little grows.
		float edge = 1 - std::fabs(forest - .5f) * 2;
		float plants = g.Plants[RkNearest(g, x, z)] * 1.5f;
		if (RkUnit(h, 1) > (.06f + .4f * (edge > 0 ? edge : 0)) * a * Logs(x, z) * (plants < 1 ? plants : 1)) return;
		float scale = .7f + .7f * RkUnit(h, 2);
		Add(RkUnit(h, 3) < .25f ? BerryBush : Bush, RkUnit(h, 4) * 6.2831855f, scale, (float)x, height - .05f, (float)z);
	});
	RkCells(g.X0, g.Z0, size, 24, seed ^ 0x1065ULL, [&](double x, double z, u64 h)
	{
		float height, up, forest; bool wet;
		if (!RkAt(g, x, z, height, up, forest, wet) || wet || up < .85f || forest < .45f) return;
		if (RkUnit(h, 1) > .45f * forest * a * Logs(x, z)) return;
		int kind = RkUnit(h, 2) < .5f ? Stump : Log;
		float scale = .8f + .5f * RkUnit(h, 3);
		Add(kind, RkUnit(h, 4) * 6.2831855f, scale, (float)x, height - (kind == Log ? .08f : .03f), (float)z);
	});
}

// ---------------------------------------------------------------------------------------------------- shapes
namespace
{
struct FV3 { float X, Y, Z; };
FV3 operator+(FV3 a, FV3 b) { return { a.X + b.X, a.Y + b.Y, a.Z + b.Z }; }
FV3 operator-(FV3 a, FV3 b) { return { a.X - b.X, a.Y - b.Y, a.Z - b.Z }; }
FV3 operator*(FV3 a, float s) { return { a.X * s, a.Y * s, a.Z * s }; }
FV3 Cross(FV3 a, FV3 b) { return { a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X }; }
float Dot(FV3 a, FV3 b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
FV3 Norm(FV3 a) { float l = std::sqrt(Dot(a, a)); return l > 1e-12f ? a * (1 / l) : FV3{ 0, 1, 0 }; }
FV3 Hex(unsigned rgb) { return { ((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f }; }
FV3 Lerp(FV3 a, FV3 b, float t) { return a + (b - a) * t; }

/// A triangle; with `inside`, turned so that it faces away from that point (convex shapes).
void Tri(FSoup& s, FV3 a, FV3 b, FV3 c, FV3 colour, const FV3* inside = nullptr)
{
	FV3 n = Norm(Cross(b - a, c - a));
	if (inside && Dot(n, (a + b + c) * (1 / 3.f) - *inside) < 0) { FV3 swap = b; b = c; c = swap; n = n * -1; }
	for (FV3 p : { a, b, c })
	{
		s.Position.insert(s.Position.end(), { p.X, p.Y, p.Z });
		s.Normal.insert(s.Normal.end(), { n.X, n.Y, n.Z });
		s.Colour.insert(s.Colour.end(), { colour.X, colour.Y, colour.Z });
	}
}

/// Auto-smooth, as Blender's: each corner takes the mean of the faces meeting at its position that lie within
/// `degrees` of its own face; sharper edges stay crisp.
void AutoSmooth(FSoup& s, float degrees)
{
	float limit = std::cos(degrees * 3.14159265f / 180);
	std::map<std::tuple<long long, long long, long long>, std::vector<int>> at;
	int count = s.Count();
	auto Key = [&](int i) { return std::make_tuple((long long)std::llround(s.Position[i * 3] * 1e4), (long long)std::llround(s.Position[i * 3 + 1] * 1e4), (long long)std::llround(s.Position[i * 3 + 2] * 1e4)); };
	for (int i = 0; i < count; i++) at[Key(i)].push_back(i);
	std::vector<float> result = s.Normal;
	for (int i = 0; i < count; i++)
	{
		FV3 own = { s.Normal[i * 3], s.Normal[i * 3 + 1], s.Normal[i * 3 + 2] }, sum = { 0, 0, 0 };
		for (int j : at[Key(i)])
		{
			FV3 other = { s.Normal[j * 3], s.Normal[j * 3 + 1], s.Normal[j * 3 + 2] };
			if (Dot(other, own) >= limit) sum = sum + other;
		}
		FV3 n = Dot(sum, sum) > 1e-12f ? Norm(sum) : own;
		result[i * 3] = n.X; result[i * 3 + 1] = n.Y; result[i * 3 + 2] = n.Z;
	}
	s.Normal.swap(result);
}

/// A capped cylinder along y (bottom radius, top radius), centred at `centre`; `turn` lays it on its side (along x).
void Cylinder(FSoup& s, FV3 centre, float bottom, float top, float height, FV3 colour, int segments, bool onSide = false)
{
	auto P = [&](float r, float y, int k)
	{
		float a = k * 6.2831855f / segments;
		FV3 p = { std::cos(a) * r, y, std::sin(a) * r };
		if (onSide) p = { -p.Y, p.X, p.Z }; // (turned a quarter about z: y becomes -x)
		return centre + p;
	};
	FV3 lo = centre + (onSide ? FV3{ height * .5f, 0, 0 } : FV3{ 0, -height * .5f, 0 }), hi = centre + (onSide ? FV3{ -height * .5f, 0, 0 } : FV3{ 0, height * .5f, 0 });
	for (int k = 0; k < segments; k++)
	{
		FV3 a = P(bottom, -height * .5f, k), b = P(bottom, -height * .5f, k + 1), c = P(top, height * .5f, k + 1), d = P(top, height * .5f, k);
		Tri(s, a, d, c, colour, &centre); Tri(s, a, c, b, colour, &centre);
		Tri(s, lo, a, b, colour, &centre); Tri(s, hi, c, d, colour, &centre);
	}
}

/// A ball squashed to (radius, height / 2), as Godot's SphereMesh.
void Ball(FSoup& s, FV3 centre, float radius, float height, FV3 colour, int segments, int rings)
{
	auto P = [&](int ring, int k)
	{
		float v = 3.14159265f * ring / (rings + 1), a = k * 6.2831855f / segments;
		return centre + FV3{ std::sin(v) * std::cos(a) * radius, std::cos(v) * height * .5f, std::sin(v) * std::sin(a) * radius };
	};
	for (int ring = 0; ring <= rings; ring++)
		for (int k = 0; k < segments; k++)
		{
			FV3 a = P(ring, k), b = P(ring, k + 1), c = P(ring + 1, k + 1), d = P(ring + 1, k);
			if (ring > 0) Tri(s, a, b, c, colour, &centre);
			if (ring < rings) Tri(s, a, c, d, colour, &centre);
		}
}

/// A box with half extents, centred.
void Box(FSoup& s, FV3 centre, FV3 half, FV3 colour, FV3 ax = { 1, 0, 0 }, FV3 ay = { 0, 1, 0 }, FV3 az = { 0, 0, 1 })
{
	auto P = [&](int i) { return centre + ax * ((i & 1) ? half.X : -half.X) + ay * ((i & 2) ? half.Y : -half.Y) + az * ((i & 4) ? half.Z : -half.Z); };
	static const int F[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
	for (const int* f : F) { Tri(s, P(f[0]), P(f[1]), P(f[2]), colour, &centre); Tri(s, P(f[0]), P(f[2]), P(f[3]), colour, &centre); }
}

/// A square beam from one point to another.
void Beam(FSoup& s, FV3 from, FV3 to, float thick, FV3 colour)
{
	FV3 along = to - from; float length = std::sqrt(Dot(along, along));
	if (length < 1e-5f) return;
	FV3 x = along * (1 / length), side = Norm(Cross(x, std::fabs(x.Y) < .9f ? FV3{ 0, 1, 0 } : FV3{ 1, 0, 0 })), other = Cross(x, side);
	Box(s, (from + to) * .5f, { length * .5f, thick * .5f, thick * .5f }, colour, x, side, other);
}

struct FRng
{
	u64 State;
	float Next() { State = Hashing::Mix(State + 0x9E3779B97F4A7C15ULL); return (float)Hashing::Unit(State); }
};
}

FSoup FPropShapes::Rock(int kind)
{
	// (subdivisions, seed, roughness): 3 the cliff slab and 4 the huge / shore boulder are chunky, flat-faced.
	static const struct { int Subdivisions; unsigned Seed; float Roughness; } Kinds[5] = { { 1, 0x51u, .22f }, { 1, 0x77u, .28f }, { 2, 0x93u, .3f }, { 0, 0xC1u, .32f }, { 0, 0xB7u, .26f } };
	kind = RkClampI(kind, 0, 4);
	float t = (1 + std::sqrt(5.f)) / 2;
	std::vector<FV3> v = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
	std::vector<std::array<int, 3>> f = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
		{ 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
	for (int sub = 0; sub < Kinds[kind].Subdivisions; sub++)
	{
		std::vector<std::array<int, 3>> next;
		std::map<long long, int> cache;
		auto Mid = [&](int a, int b)
		{
			long long key = (long long)(a < b ? a : b) * 100000LL + (a < b ? b : a);
			auto found = cache.find(key);
			if (found != cache.end()) return found->second;
			v.push_back((v[a] + v[b]) * .5f);
			return cache[key] = (int)v.size() - 1;
		};
		for (const auto& tri : f)
		{
			int ab = Mid(tri[0], tri[1]), bc = Mid(tri[1], tri[2]), ca = Mid(tri[2], tri[0]);
			next.push_back({ tri[0], ab, ca }); next.push_back({ tri[1], bc, ab }); next.push_back({ tri[2], ca, bc }); next.push_back({ ab, bc, ca });
		}
		f.swap(next);
	}
	// Jitter radially (deterministic), flatten the bottom half so rocks sit in the ground.
	// (Thornwake draws these numbers from Godot's own generator; this one differs, so the lumps are alike, not the same.)
	FRng rng{ Kinds[kind].Seed };
	for (FV3& p : v)
	{
		FV3 n = Norm(p);
		float r = 1 + (rng.Next() - .5f) * 2 * Kinds[kind].Roughness;
		p = n * (r * .5f);
		if (p.Y < 0) p.Y *= .45f;
		p.Y += .12f;
	}
	FSoup s;
	FV3 stone = Hex(0x777a72), dark = Hex(0x5d605a), moss = Hex(0x66744f);
	for (const auto& tri : f)
	{
		int a = tri[0], b = tri[1], c = tri[2];
		FV3 normal = Norm(Cross(v[b] - v[a], v[c] - v[a]));
		FV3 centroid = (v[a] + v[b] + v[c]) * (1 / 3.f) - FV3{ 0, .12f, 0 };
		if (Dot(normal, centroid) < 0) { int swap = b; b = c; c = swap; normal = normal * -1; } // outward
		float shade = rng.Next();
		FV3 colour = normal.Y > .75f ? Lerp(moss, stone, .4f + .3f * shade) : Lerp(stone, dark, shade * .6f);
		Tri(s, v[a], v[b], v[c], colour);
	}
	AutoSmooth(s, 50);
	return s;
}

FSoup FPropShapes::Prop(int kind)
{
	FSoup s;
	if (kind == FRocks::Bush || kind == FRocks::BerryBush)
	{
		// The valley game's berry bush: three leafy balls side by side, berries round them.
		for (int i = 0; i < 3; i++) Ball(s, { (i - 1) * .3f, .55f, 0 }, .5f, .9f, Hex(0x526943), 7, 4);
		if (kind == FRocks::BerryBush)
			for (int i = 0; i < 8; i++)
			{
				float a = i * 2.4f;
				Ball(s, { std::cos(a) * .55f, .5f + i * .035f, std::sin(a) * .35f }, .085f, .17f, Hex(0xa45f56), 5, 3);
			}
	}
	else if (kind == FRocks::Stump)
	{
		FV3 wood = Hex(0x5c4632), pale = Hex(0xb39362), ring = Hex(0x9b794f);
		Cylinder(s, { 0, .27f, 0 }, .40f, .32f, .54f, wood, 9);
		for (int i = 0; i < 5; i++)
		{
			float angle = i * 6.2831855f / 5 + .27f;
			Beam(s, { std::cos(angle) * .16f, .22f, std::sin(angle) * .16f }, { std::cos(angle) * .64f, .055f, std::sin(angle) * .64f }, .115f, wood);
		}
		Cylinder(s, { 0, .547f, 0 }, .315f, .315f, .018f, pale, 9);
		Cylinder(s, { 0, .558f, 0 }, .252f, .252f, .008f, ring, 12);
		Cylinder(s, { 0, .564f, 0 }, .222f, .222f, .007f, pale, 12);
		Cylinder(s, { 0, .570f, 0 }, .142f, .142f, .007f, ring, 12);
		Cylinder(s, { 0, .576f, 0 }, .113f, .113f, .007f, pale, 12);
		Box(s, { .20f, .584f, .04f }, { .095f, .004f, .0065f }, wood);
	}
	else
	{
		// A fallen log: a bark cylinder lying on its side with lighter end grain.
		Cylinder(s, { 0, .2f, 0 }, .24f, .2f, 3.4f, Hex(0x5c4632), 8, true);
		Cylinder(s, { -1.7f, .2f, 0 }, .2f, .2f, .02f, Hex(0xb39362), 8, true);
		Cylinder(s, { 1.7f, .2f, 0 }, .23f, .23f, .02f, Hex(0xb39362), 8, true);
	}
	return s;
}
}

// ---------------------------------------------------------------------------------------------------- big rocks
// Port of Thornwake Game/Rewrite/SeaStackView.cs (Noise, SmoothMax, Surface, CliffBlock) and RockKit.cs.
namespace ww
{
namespace
{
const float RockLight = 1.45f;

/// Smooth value noise on a lattice (rock bulges a few metres across).
float Noise3(u64 seed, float x, float y, float z)
{
	int ix = (int)std::floor(x), iy = (int)std::floor(y), iz = (int)std::floor(z);
	float fx = x - ix, fy = y - iy, fz = z - iz;
	fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy); fz = fz * fz * (3 - 2 * fz);
	auto H = [&](int a, int b, int c) { return (float)Hashing::Unit(Hashing::Mix(seed ^ (u64)((i64)a * 73856093LL) ^ (u64)((i64)b * 19349663LL) ^ (u64)((i64)c * 83492791LL))); };
	auto L = [](float a, float b, float t) { return a + (b - a) * t; };
	float x00 = L(H(ix, iy, iz), H(ix + 1, iy, iz), fx), x10 = L(H(ix, iy + 1, iz), H(ix + 1, iy + 1, iz), fx);
	float x01 = L(H(ix, iy, iz + 1), H(ix + 1, iy, iz + 1), fx), x11 = L(H(ix, iy + 1, iz + 1), H(ix + 1, iy + 1, iz + 1), fx);
	return L(L(x00, x10, fy), L(x01, x11, fy), fz) * 2 - 1;
}
float KitSmoothMax(float a, float b, float k)
{
	if (k <= 1e-4f) return a > b ? a : b;
	float d = a - b; if (d < 0) d = -d;
	float h = (k - d > 0 ? k - d : 0) / k;
	return (a > b ? a : b) + h * h * k * .25f;
}
float KitClamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
float KitStep(float e0, float e1, float x) { float t = KitClamp((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t); }

/// A solid's surface (signed distance in metres, below 0 inside), taken with surface nets on a grid and flat-shaded:
/// a point in each cell the surface crosses, a quad across each crossed edge. The beds show as bands of paler and darker
/// rock; moss on open tops; darker where rock lies close above (crevices, the undersides of ledges).
FSoup KitSurface(const std::function<float(float, float, float)>& solid, FV3 lo, FV3 hi, float cell, u64 seed, float tilt, float aoStrength, float aoReach)
{
	int nx = (int)std::ceil((hi.X - lo.X) / cell) + 1, ny = (int)std::ceil((hi.Y - lo.Y) / cell) + 1, nz = (int)std::ceil((hi.Z - lo.Z) / cell) + 1;
	std::vector<float> field((size_t)nx * ny * nz);
	auto Idx = [&](int i, int j, int k) { return ((size_t)k * ny + j) * nx + i; };
	auto Corner = [&](int i, int j, int k) { return FV3{ lo.X + i * cell, lo.Y + j * cell, lo.Z + k * cell }; };
	for (int k = 0; k < nz; k++)
		for (int j = 0; j < ny; j++)
			for (int i = 0; i < nx; i++) { FV3 p = Corner(i, j, k); field[Idx(i, j, k)] = solid(p.X, p.Y, p.Z); }
	std::vector<int> point(field.size(), -1);
	std::vector<FV3> points;
	for (int k = 0; k + 1 < nz; k++)
		for (int j = 0; j + 1 < ny; j++)
			for (int i = 0; i + 1 < nx; i++)
			{
				FV3 sum = { 0, 0, 0 }; int crossings = 0;
				for (int e = 0; e < 8; e++)
					for (int axis = 0; axis < 3; axis++)
					{
						if ((e >> axis & 1) != 0) continue;
						int ai = i + (e & 1), aj = j + (e >> 1 & 1), ak = k + (e >> 2 & 1);
						int bi = ai + (axis == 0 ? 1 : 0), bj = aj + (axis == 1 ? 1 : 0), bk = ak + (axis == 2 ? 1 : 0);
						float da = field[Idx(ai, aj, ak)], db = field[Idx(bi, bj, bk)];
						if ((da < 0) == (db < 0)) continue;
						sum = sum + Lerp(Corner(ai, aj, ak), Corner(bi, bj, bk), da / (da - db)); crossings++;
					}
				if (crossings == 0) continue;
				point[Idx(i, j, k)] = (int)points.size();
				points.push_back(sum * (1.f / crossings));
			}
	FSoup s;
	FRandomStream shadeRng(seed ^ 0x5EA5ULL);
	FV3 stone = Hex(0x777a72), dark = Hex(0x5d605a), moss = Hex(0x66744f);
	auto Occlusion = [&](FV3 p, FV3 n)
	{
		float occ = 0;
		const float steps[3][2] = { { 1.f, .5f }, { 2.5f, .3f }, { 5.f, .2f } };
		for (const float* st : steps)
		{
			float d = st[0] * aoReach;
			occ += st[1] * KitClamp((d - solid(p.X + n.X * d, p.Y + n.Y * d, p.Z + n.Z * d)) / d, 0, 1);
		}
		return 1 - aoStrength * KitClamp(occ * 1.4f, 0, 1);
	};
	// (bands wider than the cells, one colour across a quad)
	float bandHeight = 3 > cell * 2.6f ? 3 : cell * 2.6f;
	auto Face = [&](FV3 a, FV3 b, FV3 c, float band, float shade)
	{
		FV3 normal = Cross(b - a, c - a);
		if (Dot(normal, normal) < 1e-6f) return;
		normal = Norm(normal);
		FV3 mid = (a + b + c) * (1 / 3.f);
		FV3 colour = Lerp(stone, dark, .05f + .4f * band + .1f * shade);
		if (normal.Y > .72f) colour = Lerp(moss, colour, .5f);
		float ao = Occlusion(mid, normal) * RockLight;
		Tri(s, a, b, c, colour * ao);
	};
	// (facing from the grid: flipping a triangle by its own normal turned folded ones inside out)
	auto Quad = [&](int a, int b, int c, int d, bool flip)
	{
		if (a < 0 || b < 0 || c < 0 || d < 0) return;
		if (flip) { int swap = b; b = d; d = swap; }
		FV3 mid = (points[a] + points[b] + points[c] + points[d]) * .25f;
		float band = .5f + .5f * Noise3(seed + 11, 0, (mid.Y + tilt * mid.X) / bandHeight, 0), shade = (float)shadeRng.Unit();
		Face(points[a], points[b], points[c], band, shade); Face(points[a], points[c], points[d], band, shade);
	};
	for (int k = 1; k + 1 < nz; k++)
		for (int j = 1; j + 1 < ny; j++)
			for (int i = 1; i + 1 < nx; i++)
			{
				float here = field[Idx(i, j, k)];
				float x = field[Idx(i + 1, j, k)], y = field[Idx(i, j + 1, k)], z = field[Idx(i, j, k + 1)];
				if ((here < 0) != (x < 0)) Quad(point[Idx(i, j - 1, k - 1)], point[Idx(i, j, k - 1)], point[Idx(i, j, k)], point[Idx(i, j - 1, k)], x < here);
				if ((here < 0) != (y < 0)) Quad(point[Idx(i - 1, j, k - 1)], point[Idx(i, j, k - 1)], point[Idx(i, j, k)], point[Idx(i - 1, j, k)], y > here);
				if ((here < 0) != (z < 0)) Quad(point[Idx(i - 1, j - 1, k)], point[Idx(i, j - 1, k)], point[Idx(i, j, k)], point[Idx(i - 1, j, k)], z < here);
			}
	return s;
}

// ---- the rock kit's pieces
FV3 Apply(const FBasis& b, FV3 v) { return { b.X[0] * v.X + b.Y[0] * v.Y + b.Z[0] * v.Z, b.X[1] * v.X + b.Y[1] * v.Y + b.Z[1] * v.Z, b.X[2] * v.X + b.Y[2] * v.Y + b.Z[2] * v.Z }; }
FV3 ApplyInverse(const FBasis& b, FV3 v) { return { b.X[0] * v.X + b.X[1] * v.Y + b.X[2] * v.Z, b.Y[0] * v.X + b.Y[1] * v.Y + b.Y[2] * v.Z, b.Z[0] * v.X + b.Z[1] * v.Y + b.Z[2] * v.Z }; }
float Len(FV3 v) { return std::sqrt(Dot(v, v)); }
FV3 TurnY(FV3 v, float angle) { float c = std::cos(angle), s = std::sin(angle); return { c * v.X + s * v.Z, v.Y, -s * v.X + c * v.Z }; }

struct FPlane { FV3 N; float D; };
/// One piece as a solid: a bevelled box with corners cut.
struct FBlock
{
	FV3 Centre, Half;
	FBasis Basis;
	FPlane Faces[6];
	std::vector<FPlane> Cuts;
	float Bevel = 0, Reach = 0;
	float Distance(FV3 p) const
	{
		FV3 q = ApplyInverse(Basis, p - Centre);
		float d = Dot(q, Faces[0].N) - Faces[0].D;
		for (int f = 1; f < 6; f++) d = KitSmoothMax(d, Dot(q, Faces[f].N) - Faces[f].D, Bevel);
		for (const FPlane& c : Cuts) d = KitSmoothMax(d, Dot(q, c.N) - c.D, Bevel * .7f);
		return d;
	}
};

/// The pieces as polyhedra: each the intersection of its half-spaces (the box's six faces, a chamfer along each of its
/// twelve edges, the corner cuts), its vertices every three planes' meeting point inside all the rest, each face the
/// points on its plane in order round them. Flat-shaded, each face its own shade, moss on open tops, darker by how much
/// rock lies close above it.
FSoup Polyhedra(const std::vector<FBlock>& blocks, const std::function<float(float, float, float)>& solid, u64 seed)
{
	FSoup s;
	FRandomStream rng(seed ^ 0xFACEULL);
	FV3 stone = Hex(0x777a72), dark = Hex(0x5d605a), moss = Hex(0x66744f);
	for (const FBlock& b : blocks)
	{
		std::vector<FPlane> planes(b.Faces, b.Faces + 6);
		float c = .15f > b.Bevel * .7f ? .15f : b.Bevel * .7f;
		for (int i = 0; i < 6; i++)
			for (int j = i + 1; j < 6; j++)
			{
				if (j == (i ^ 1)) continue; // (opposite faces)
				FV3 sum = b.Faces[i].N + b.Faces[j].N; float len = Len(sum);
				planes.push_back({ sum * (1 / len), (b.Faces[i].D + b.Faces[j].D) / len - c });
			}
		for (const FPlane& cut : b.Cuts) planes.push_back(cut);
		// The corners: every three planes meeting inside all the others.
		std::vector<FV3> points;
		int count = (int)planes.size();
		for (int i = 0; i < count; i++)
			for (int j = i + 1; j < count; j++)
				for (int k = j + 1; k < count; k++)
				{
					FV3 n1 = planes[i].N, n2 = planes[j].N, n3 = planes[k].N;
					float det = Dot(n1, Cross(n2, n3));
					if (std::fabs(det) < 1e-5f) continue;
					FV3 q = (Cross(n2, n3) * planes[i].D + Cross(n3, n1) * planes[j].D + Cross(n1, n2) * planes[k].D) * (1 / det);
					bool inside = true;
					for (const FPlane& p : planes) if (Dot(p.N, q) > p.D + 1e-3f) { inside = false; break; }
					if (inside) points.push_back(q);
				}
		for (const FPlane& plane : planes)
		{
			// This face's corners (no repeats), in order round its middle.
			std::vector<FV3> face;
			for (FV3 q : points)
			{
				if (std::fabs(Dot(plane.N, q) - plane.D) >= 2e-3f) continue;
				bool repeat = false;
				for (FV3 f : face) if (Dot(f - q, f - q) < 1e-6f) { repeat = true; break; }
				if (!repeat) face.push_back(q);
			}
			if (face.size() < 3) continue;
			FV3 mid = { 0, 0, 0 };
			for (FV3 q : face) mid = mid + q;
			mid = mid * (1.f / face.size());
			FV3 n = plane.N;
			FV3 u = Norm(Cross(std::fabs(n.Y) < .9f ? FV3{ 0, 1, 0 } : FV3{ 1, 0, 0 }, n)), v = Cross(n, u);
			std::stable_sort(face.begin(), face.end(), [&](FV3 p1, FV3 p2)
				{ return std::atan2(Dot(p1 - mid, v), Dot(p1 - mid, u)) < std::atan2(Dot(p2 - mid, v), Dot(p2 - mid, u)); });
			// Into the formation's space.
			FV3 wn = Norm(Apply(b.Basis, n));
			std::vector<FV3> wp;
			for (FV3 q : face) wp.push_back(b.Centre + Apply(b.Basis, q));
			FV3 wmid = b.Centre + Apply(b.Basis, mid);
			if (wmid.Y < -2.4f && wn.Y < -.5f) continue; // (a buried underside)
			float shade = (float)rng.Unit();
			float band = .5f + .5f * std::sin(wmid.Y * 1.3f + (float)rng.Unit() * .6f);
			FV3 colour = Lerp(stone, dark, .08f + .3f * band + .2f * shade);
			if (wn.Y > .72f) colour = Lerp(moss, colour, .45f + .2f * shade);
			// Crevice shadow: how much rock lies close above the face.
			float occ = 0;
			const float steps[3][2] = { { .5f, .5f }, { 1.2f, .3f }, { 2.5f, .2f } };
			for (const float* st : steps)
			{
				FV3 at = wmid + wn * st[0];
				occ += st[1] * KitClamp((st[0] - solid(at.X, at.Y, at.Z)) / st[0], 0, 1);
			}
			float ao = 1 - .5f * KitClamp(occ * 1.4f, 0, 1);
			// (the corners run counter-clockwise round n)
			for (size_t t = 1; t + 1 < wp.size(); t++) Tri(s, wp[0], wp[t], wp[t + 1], colour * (RockLight * ao));
			// (flat-shaded by the face's own normal)
			for (size_t t = s.Normal.size() - (wp.size() - 2) * 9; t < s.Normal.size(); t += 3) { s.Normal[t] = wn.X; s.Normal[t + 1] = wn.Y; s.Normal[t + 2] = wn.Z; }
		}
	}
	return s;
}
}

FSoup FPropShapes::CliffBlock(int shape, bool coarse)
{
	u64 seed = 0xB10C1ULL + (u64)shape;
	float cell = coarse ? 4.4f : 2.2f;
	FRandomStream rng(seed);
	float a = (float)rng.Between(8, 10), b = (float)rng.Between(6, 9), h = 20;
	if (shape == 4) a = (float)rng.Between(9, 10.5);
	float tilt = (float)rng.Between(-.2, .2), bed = (float)rng.Between(4.5, 6.5), taper = (float)rng.Between(.05, .2);
	float turn = (float)rng.Unit() * 6.2831855f, ca = std::cos(turn), sa = std::sin(turn);
	float power = shape == 5 ? 2.1f : 3.2f, cleft = (float)rng.Between(-2.5, 2.5);
	int ledges = 2 + rng.Int(2);
	auto Solid = [&](float x, float y, float z)
	{
		float px = x + 2.4f * Noise3(seed + 2, x / 14, y / 14, z / 14), pz = z + 2.4f * Noise3(seed + 3, x / 14 + 7, y / 14, z / 14);
		float rx = ca * px + sa * pz, rz = ca * pz - sa * px;
		float t = KitClamp(y / h, 0, 1), scale = 1 - taper * (t > .55f ? 1 : t / .55f * .3f);
		if (shape == 2) { float v = t * ledges, step = std::floor(v); scale = 1 - .38f * (step + KitClamp((v - step - .75f) / .25f, 0, 1)) / ledges; }
		if (shape == 3) scale = .82f + .3f * KitStep(.2f, .85f, t);
		float plan = (std::pow(std::pow(std::fabs(rx) / (a * scale), power) + std::pow(std::fabs(rz) / (b * scale), power), 1 / power) - 1) * (a < b ? a : b) * scale;
		float top = h * .92f + tilt * rx * .6f + 1.2f * Noise3(seed + 4, x / 8, 0, z / 8);
		float d = KitSmoothMax(KitSmoothMax(plan, y - top, shape == 5 ? 6.f : 2.f), .5f - y, 2);
		if (shape == 4) { float cut = (1.6f - std::fabs(rx - cleft)) * KitStep(4, 9, y); if (cut > d) d = cut; }
		float joints = std::fabs(Noise3(seed + 7, x / 5, y / 25, z / 5)), sy = y + tilt * x;
		d += 1.3f * (1 - (joints * 5 < 1 ? joints * 5 : 1)) - .25f + 1.1f * Noise3(seed + 8, x / 6, y / 6, z / 6) + .4f * Noise3(seed + 9, x / 2.5f, y / 2.5f, z / 2.5f);
		float f = sy - bed * std::floor(sy / bed); f /= bed;
		d += .6f * (-1 + 1.25f * KitStep(.2f, .42f, f) - 1.25f * KitStep(.84f, 1, f));
		return d;
	};
	FSoup s = KitSurface(Solid, { -19, -3, -19 }, { 19, 26, 19 }, cell, seed, tilt, .7f, 1);
	// Unit size and centred, to be scaled per rock.
	for (size_t i = 0; i < s.Position.size(); i += 3) { s.Position[i] /= h; s.Position[i + 1] = (s.Position[i + 1] - h * .5f) / h; s.Position[i + 2] /= h; }
	AutoSmooth(s, 50);
	return s;
}

FSoup FPropShapes::Formation(int kind, int variant)
{
	u64 seed = 0x40C4C17ULL + (u64)kind * 7919 + (u64)variant * 104729;
	FRandomStream rng(seed);
	std::vector<FBlock> blocks;
	const float RkTau = 6.2831855f;
	const float ForwardAxis[3] = { 0, 0, -1 };
	// A piece: half extents, turned about y, tilted a little, corners cut by `cuts` planes.
	auto Add = [&](FV3 centre, FV3 half, float turn, float tilt, int cuts, float bevel)
	{
		FBlock b;
		float tx = (float)rng.Between(-tilt, tilt), tz = (float)rng.Between(-tilt, tilt);
		b.Basis = FBasis::Turn(UpAxis, turn) * FBasis::Turn(RightAxis, tx) * FBasis::Turn(ForwardAxis, tz);
		// The faces turned each their own way: sides leaning in toward the top (mostly), the top and bottom sloping.
		const FV3 axes0[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
		const float h0[6] = { half.X, half.X, half.Y, half.Y, half.Z, half.Z };
		for (int f = 0; f < 6; f++)
		{
			FV3 n = axes0[f];
			if (f == 2 || f == 3) { float jx = (float)rng.Between(-.22, .22), jz = (float)rng.Between(-.22, .22); n = n + FV3{ jx, 0, jz }; }
			else
			{
				float up = (float)rng.Between(-.06, .3), sideways = (float)rng.Between(-.16, .16);
				n = n + FV3{ 0, up, 0 } + (f < 2 ? FV3{ 0, 0, 1 } : FV3{ 1, 0, 0 }) * sideways;
			}
			b.Faces[f] = { Norm(n), h0[f] * (float)rng.Between(.88, 1.05) };
		}
		cuts += rng.Int(3);
		for (int c = 0; c < cuts; c++)
		{
			// A plane cutting off a corner: pointing toward one, set inside it by a fifth to a half of the way.
			float cx = rng.Unit() < .5 ? -1.f : 1.f, cy = rng.Unit() < .65 ? 1.f : -1.f, cz = rng.Unit() < .5 ? -1.f : 1.f;
			float jx = (float)rng.Between(-.5, .5), jy = (float)rng.Between(-.4, .4), jz = (float)rng.Between(-.5, .5);
			FV3 n = Norm(FV3{ cx + jx, cy + jy, cz + jz });
			float full = std::fabs(n.X) * half.X + std::fabs(n.Y) * half.Y + std::fabs(n.Z) * half.Z;
			b.Cuts.push_back({ n, full * (float)rng.Between(.48, .8) });
		}
		b.Centre = centre; b.Half = half; b.Bevel = bevel; b.Reach = Len(half) + bevel + 1;
		blocks.push_back(b);
	};
	auto Rubble = [&](float spread, int count, float size)
	{
		for (int i = 0; i < count; i++)
		{
			float a = (float)rng.Unit() * RkTau, r = spread * (float)rng.Between(.75, 1.15), s = size * (float)rng.Between(.5, 1.1);
			float hy = s * (float)rng.Between(.4, .7), hz = s * (float)rng.Between(.6, 1);
			float turn = (float)rng.Unit() * RkTau;
			Add({ std::cos(a) * r, s * .3f, std::sin(a) * r }, { s, hy, hz }, turn, .35f, 3, s * .25f);
		}
	};
	if (kind == 0)
	{
		// Columns: tall prisms packed on a jittered grid in an ellipse, the tallest toward the middle, their tops cut at angles.
		float rx = (float)rng.Between(5, 8), rz = (float)rng.Between(3.5, 6), top = (float)rng.Between(11, 17), step = (float)rng.Between(2.6, 3.4);
		float turn = (float)rng.Unit() * RkTau;
		for (float gx = -rx; gx <= rx; gx += step)
			for (float gz = -rz; gz <= rz; gz += step * .9f)
			{
				float u = (gx * gx) / (rx * rx) + (gz * gz) / (rz * rz);
				if (u > 1 || rng.Unit() < .12) continue;
				float hgt = top * (1 - .65f * u) * (float)rng.Between(.6, 1.05);
				float w = step * (float)rng.Between(.45, .62);
				float jx = (float)rng.Between(-.5, .5), jz = (float)rng.Between(-.5, .5);
				FV3 at = { gx + jx, hgt * .5f - 2, gz + jz };
				float wz = w * (float)rng.Between(.7, 1), own = turn + (float)rng.Between(-.4, .4);
				Add(TurnY(at, turn), { w, hgt * .5f + 1, wz }, own, .07f, 3, w * .18f);
			}
		int rubble = 6 + rng.Int(5);
		Rubble((rx > rz ? rx : rz) + 1.5f, rubble, 1.2f);
	}
	else if (kind == 1)
	{
		// Slabs: flat layers, each smaller and set off from the one below, a little tilted: a stack weathered from bedded rock.
		int layers = 3 + rng.Int(4);
		float w = (float)rng.Between(6, 9), d = (float)rng.Between(4, 6.5), y = -1.5f, turn = (float)rng.Unit() * RkTau;
		FV3 off = { 0, 0, 0 };
		for (int i = 0; i < layers; i++)
		{
			float t = (float)rng.Between(.8, 1.5);
			float ox = (float)rng.Between(-1, 1), oz = (float)rng.Between(-.8, .8);
			off = { KitClamp(off.X + ox, -w * .3f, w * .3f), 0, KitClamp(off.Z + oz, -d * .3f, d * .3f) };
			float own = turn + (float)rng.Between(-.25, .25);
			Add(TurnY(off + FV3{ 0, y + t, 0 }, turn), { w, t, d }, own, .05f, 4, t * .35f);
			y += t * 2 - .15f;
			w *= (float)rng.Between(.72, .92); d *= (float)rng.Between(.72, .95);
		}
		// A slab fallen against the side.
		if (rng.Unit() < .7)
		{
			float a = (float)rng.Unit() * RkTau;
			Add({ std::cos(a) * 7, 1.2f, std::sin(a) * 6 }, { 3, .8f, 2.2f }, a, .5f, 3, .3f);
		}
		int rubble = 3 + rng.Int(4);
		Rubble(9, rubble, 1);
	}
	else if (kind == 2)
	{
		// Tiered: steps of blocks round a high middle, rubble at the foot.
		int tiers = 3 + rng.Int(2);
		float r = (float)rng.Between(7, 9.5), y = -1.5f;
		for (int t = 0; t < tiers; t++)
		{
			int count = 7 - 2 * t + rng.Int(2);
			if (count < 1) count = 1;
			float ring = r * (1 - (float)t / tiers) * .75f, size = r * .32f * (1 - .12f * t), hgt = (float)rng.Between(1.6, 2.6) + t * .4f;
			float a0 = (float)rng.Unit() * RkTau;
			for (int i = 0; i < count; i++)
			{
				float a = a0 + i * RkTau / count + (float)rng.Between(-.25, .25), rr = count == 1 ? 0 : ring * (float)rng.Between(.7, 1);
				float sx = size * (float)rng.Between(.7, 1.1), sz = size * (float)rng.Between(.6, 1), own = a + (float)rng.Between(-.3, .3);
				Add({ std::cos(a) * rr, y + hgt, std::sin(a) * rr }, { sx, hgt, sz }, own, .08f, 3, size * .16f);
			}
			y += hgt * 1.6f;
		}
		int rubble = 8 + rng.Int(5);
		Rubble(r + 1.5f, rubble, 1.1f);
	}
	else
	{
		// A tor: big rounded granite blocks, a few on the ground side by side and one or two resting on them, a slab split
		// off and lying beside.
		int ground = 2 + rng.Int(2);
		std::vector<std::pair<FV3, FV3>> bases;
		for (int i = 0; i < ground; i++)
		{
			float s = (float)rng.Between(2.8, 4.2);
			float hx = s * (float)rng.Between(1, 1.4), hy = s * (float)rng.Between(.55, .8), hz = s * (float)rng.Between(.8, 1.15);
			float jx = (float)rng.Between(-1, 1), jz = (float)rng.Between(-1.5, 1.5);
			FV3 hh = { hx, hy, hz }, at = { i * 4.5f - (ground - 1) * 2.25f + jx, hh.Y * .75f - 1.3f, jz };
			float turn = (float)rng.Unit() * RkTau;
			Add(at, hh, turn, .12f, 2, s * .5f);
			bases.push_back({ at, hh });
		}
		for (int up = 0, n = 1 + rng.Int(2); up < n; up++)
		{
			std::pair<FV3, FV3> base = bases[rng.Int((int)bases.size())];
			float s = (base.second.X < base.second.Z ? base.second.X : base.second.Z) * (float)rng.Between(.6, .85);
			float hx = s * (float)rng.Between(1, 1.3), hy = s * (float)rng.Between(.5, .7), hz = s * (float)rng.Between(.8, 1.1);
			float jx = (float)rng.Between(-.3, .3), jz = (float)rng.Between(-.3, .3);
			FV3 hh = { hx, hy, hz }, at = base.first + FV3{ jx * base.second.X, base.second.Y + hh.Y * .8f, jz * base.second.Z };
			float turn = (float)rng.Unit() * RkTau;
			Add(at, hh, turn, .1f, 2, s * .5f);
			bases.push_back({ at, hh });
		}
		for (int i = 0, slabs = 1 + rng.Int(2); i < slabs; i++)
		{
			float a = (float)rng.Unit() * RkTau;
			Add({ std::cos(a) * 6.5f, .3f, std::sin(a) * 5.5f }, { 2.6f, .7f, 1.9f }, a, .35f, 3, .5f);
		}
		int rubble = 3 + rng.Int(3);
		Rubble(8, rubble, 1);
	}
	// The solid: the nearest piece (each only near its own box); kept for the crevice shadow.
	auto Solid = [&](float x, float y, float z)
	{
		FV3 p = { x, y, z };
		float d = 3.4e38f;
		for (const FBlock& b : blocks)
		{
			float far = Len(p - b.Centre) - b.Reach;
			float own = far > 0 ? far + 1 : b.Distance(p);
			if (own < d) d = own;
		}
		return KitSmoothMax(d, -2.5f - y, .5f); // (closed below their buried foot)
	};
	return Polyhedra(blocks, Solid, seed);
}
}
