// Port of Thornwake Game/Rewrite/GroundCover.cs (Rocks, Props, RockyShore) and the shapes of RockMeshes.cs,
// PropCatalog.cs and Visuals.Stump.
#include "WwRocks.h"
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
