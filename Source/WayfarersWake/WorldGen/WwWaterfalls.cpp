// Port of Thornwake Game/Rewrite/Waterfalls.cs (FallSet; WaterfallView.Build with its "with depth" body, D302).
#include "WwWaterfalls.h"
#include "WwLandforms.h"

namespace ww
{
namespace
{
const double MinDrop = 3, MinGrade = .2, GridCell = 2000, Upstream = 3;
int Cell(double v) { return (int)std::floor(v / GridCell); }
i64 Key(int x, int z) { return ((i64)x << 32) ^ (i64)(unsigned int)z; }
double NanTo(double v, double fallback) { return v != v ? fallback : v; }
}

FFallSet::FFallSet(const FHydrology& hy)
{
	for (const FRiverReach& r : hy.Reaches)
	{
		if (r.bDistributary || r.Points.size() < 2 || r.Along.size() != r.Points.size()) continue;
		for (size_t k = 0; k + 1 < r.Points.size(); k++)
		{
			double drop = r.Surface[k] - r.Surface[k + 1], length = (double)r.Along[k + 1] - r.Along[k];
			if (drop < MinDrop || length <= 0 || drop / length < MinGrade) continue;
			const FPoint2& a = r.Points[k]; const FPoint2& b = r.Points[k + 1];
			FFall f;
			f.Reach = r.Id; f.Point = (int)k; f.LipX = a.X; f.LipZ = a.Z; f.FootX = b.X; f.FootZ = b.Z; f.Top = r.Surface[k]; f.Bottom = r.Surface[k + 1];
			f.Width = r.Width[k]; f.FootWidth = r.Width[k + 1]; f.Dx = (b.X - a.X) / length; f.Dz = (b.Z - a.Z) / length; f.Length = length;
			Falls.push_back(f);
		}
	}
	for (int i = 0; i < (int)Falls.size(); i++)
	{
		const FFall& f = Falls[i];
		double reach = f.Length * 2.5 + Mx(f.Width, f.FootWidth) + 40;
		for (int gx = Cell(Mn(f.LipX, f.FootX) - reach); gx <= Cell(Mx(f.LipX, f.FootX) + reach); gx++)
			for (int gz = Cell(Mn(f.LipZ, f.FootZ) - reach); gz <= Cell(Mx(f.LipZ, f.FootZ) + reach); gz++) Grid[Key(gx, gz)].push_back(i);
	}
}

bool FFallSet::OnFace(double x, double z, double low, double high) const
{
	auto found = Grid.find(Key(Cell(x), Cell(z)));
	if (found == Grid.end()) return false;
	for (int i : found->second)
	{
		const FFall& f = Falls[i];
		double px = x - f.LipX, pz = z - f.LipZ, along = px * f.Dx + pz * f.Dz, across = std::fabs(px * f.Dz - pz * f.Dx);
		// (from 1.5 m above the lip; well past the river's sides: by the level test below only the sheet's ramp)
		if (along < -1.5 || along > f.Length * 2.5 + 20 || across > f.WidthAt(Mn(along, f.Length)) * .5 + Mx(Margin(f) * 2.5, 15)) continue;
		// (any corner between the two levels; the sheet kept up to 0.8 m over the pool: it overlaps the curtain's foot)
		if (low < f.Top - .25 && high > f.Bottom + .8) return true;
	}
	return false;
}

FFallModel FFallModel::Build(const FTerrainSampler& terrain, const FFall& f, bool body)
{
	FFallModel m;
	// As wide as the river at the lip, a tenth wider toward the foot (falling water spreads), fading out over its own edges.
	double margin = DetMath::Clamp(f.Width * .12, 1, 4);
	double ax = f.Dz, az = -f.Dx; // across the fall (to its right)
	double lipLevel = NanTo(terrain.Sample(f.LipX, f.LipZ).WaterLevel, f.Top);
	// Revision 44: the lip's water reaches as far as the river's did on the fall's slope, so the falling water spans the
	// water found at the lip, each side measured.
	const FFallLips* lips = terrain.Plan->FallLips.get();
	bool follow = lips && lips->Follow(), faces = lips && lips->Faces();
	double left = f.Width * .5, right = f.Width * .5;
	if (follow)
		for (int pass = 0; pass < 2; pass++)
		{
			double side = pass == 0 ? -1.0 : 1.0, reach = f.Width * .5;
			for (double o = f.Width * .5 + 1; o < f.Width * 1.5 + 30; o += 1)
			{
				FTerrainSample s = terrain.Sample(f.LipX - f.Dx * .5 + ax * o * side, f.LipZ - f.Dz * .5 + az * o * side);
				if (s.Water != EWaterKind::River || std::fabs(s.WaterLevel - lipLevel) > .3) break;
				reach = o;
			}
			if (side < 0) left = reach; else right = reach;
		}
	double span = left + right, shift = (right - left) * .5; // (shift: the water's middle off the river's line, to the right)
	auto Spread = [&](double h) { return 1 + .1 * DetMath::Clamp(h / Mx(1, f.Length), 0, 1); };
	auto Wide = [&](double h) { return Mx(2, span * Spread(h) + 1); };
	double width = Wide(0);
	double v0 = DetMath::Clamp(1.5 + .04 * f.Width, 1.5, 4), bottom = f.Bottom;
	// The centre line's path: draped on the river above the lip, then falling freely or sliding on the rock, to the pool.
	auto Centre = [&](double hh) { return hh <= 0 ? lipLevel : Mx(lipLevel - 4.9 * (hh / v0) * (hh / v0), terrain.Height(f.LipX + f.Dx * hh, f.LipZ + f.Dz * hh) + .3); };
	// Whether the falling water's sides have reached the pool's level.
	auto SidesDown = [&](double hh)
	{
		for (double u : { -.5, -.3, .3, .5 })
		{
			double off = u * f.WidthAt(hh), x = f.LipX + f.Dx * hh + ax * off, z = f.LipZ + f.Dz * hh + az * off;
			if (terrain.Height(x, z) + .3 > bottom + .2) return false;
		}
		return true;
	};
	std::vector<double> steps = { -Upstream };
	{
		double h = -Upstream, y = lipLevel;
		while (h < f.Length * 2.5 + 20 && steps.size() < 900)
		{
			// About a metre of path a step: shorter where it drops steeply.
			double step = 1;
			for (int tries = 0; tries < 6 && std::fabs(Centre(h + step) - y) > 1; tries++) step *= .5;
			h += step; steps.push_back(h);
			y = Centre(h);
			// (on until every column has reached the pool; with a plunge pool under the face it ends where the falling
			// water meets the pool)
			if (faces ? h > 0 && y <= bottom - .1 : h > f.Length + 4 && y <= bottom - .1 && SidesDown(h)) break;
		}
	}
	int cols = (int)std::ceil(Mx(width, Wide(f.Length)) / 1.2);
	cols = (cols < 2 ? 2 : cols > 60 ? 60 : cols) + 1;
	int rows = (int)steps.size();
	std::vector<FVertex>& v = m.Curtain;
	v.resize((size_t)cols * rows);
	for (int c = 0; c < cols; c++)
	{
		double u = (double)c / (cols - 1);
		double pathLength = 0, travel = 0, lastSpeed = v0;
		float lx = 0, ly = 0, lz = 0;
		for (int r = 0; r < rows; r++)
		{
			double h = steps[r], off = shift * Spread(h) + (u - .5) * Wide(h);
			double x = f.LipX + f.Dx * h + ax * off, z = f.LipZ + f.Dz * h + az * off;
			double y, onRock = 0, wall = 0;
			if (h < 0) y = NanTo(terrain.Sample(x, z).WaterLevel, lipLevel) + .03;
			else
			{
				// Each column falls freely or slides on the rock under it; where the rock beside rises well over the
				// centre's path (outside the river's own width) the column fades out instead of climbing it.
				double free = lipLevel - 4.9 * (h / v0) * (h / v0), centre = Centre(h), ground = terrain.Height(x, z) + .3;
				double own = Mx(free, ground);
				double outside = DetMath::Smooth((std::fabs(off - shift * Spread(h)) - (follow ? Mx(f.WidthAt(h), span * Spread(h)) : f.WidthAt(h)) * .5 + 1) / 2);
				wall = DetMath::Smooth((own - centre - 1) / 2.5) * outside;
				y = Mx(own, bottom - .25);
				onRock = DetMath::Smooth((ground - free) / .5);
			}
			float px = (float)(x - f.LipX), py = (float)y, pz = (float)(z - f.LipZ);
			double drop = Mx(0, lipLevel - y), speed = std::sqrt(v0 * v0 + 2 * 9.8 * drop);
			// Travel time from the top: the streaks are drawn at (travel time - clock), so they move at the water's own speed.
			if (r > 0)
			{
				double seg = std::sqrt((double)(px - lx) * (px - lx) + (double)(py - ly) * (py - ly) + (double)(pz - lz) * (pz - lz));
				pathLength += seg; travel += seg / Mx(.5, .5 * (speed + lastSpeed));
			}
			lx = px; ly = py; lz = pz; lastSpeed = speed;
			FVertex& o = v[(size_t)r * cols + c];
			o.X = px; o.Y = py; o.Z = pz;
			o.U = (float)(u * width); o.V = (float)pathLength; o.Travel = (float)travel;
			double foot = DetMath::Smooth((y - bottom) / -Mx(1, (lipLevel - bottom) * .15) + 1); // (1 near the pool)
			double fromSide = Mn(u, 1 - u) * Wide(h);
			double edge = DetMath::Smooth(fromSide / margin) * DetMath::Smooth((h + Upstream) / (Upstream - 1))
				* (h > 0 ? DetMath::Smooth((y - bottom + .2) / .3) : 1) * (1 - wall);
			o.Speed = (float)Mn(1, speed / 25); o.Foot = (float)foot; o.OnRock = (float)onRock; o.Edge = (float)edge;
		}
	}
	// Columns that end on the dry bank above the pool fade out over the lower half of their way down, each column's fade
	// smoothed with its neighbours'.
	std::vector<double> colStrength(cols);
	for (int c = 0; c < cols; c++) colStrength[c] = DetMath::Smooth((v[(size_t)(rows - 1) * cols + c].Y - bottom - .4) / 2.5);
	for (int pass = 0; pass < 3; pass++)
	{
		std::vector<double> next = colStrength;
		for (int c = 0; c < cols; c++) next[c] = (colStrength[c - 1 < 0 ? 0 : c - 1] + 2 * colStrength[c] + colStrength[c + 1 > cols - 1 ? cols - 1 : c + 1]) / 4;
		colStrength.swap(next);
	}
	for (int c = 0; c < cols; c++)
	{
		double strength = colStrength[c];
		if (strength < .01) continue;
		for (int r = 0; r < rows; r++)
		{
			double t = (double)r / (rows - 1 < 1 ? 1 : rows - 1);
			v[(size_t)r * cols + c].Edge *= (float)(1 - strength * DetMath::Smooth((t - .35) / .6));
		}
	}
	std::vector<int>& idx = m.CurtainIndices;
	for (int r = 0; r + 1 < rows; r++)
		for (int c = 0; c + 1 < cols; c++)
		{
			int a = r * cols + c, b = a + 1, d = a + cols, e = d + 1;
			idx.insert(idx.end(), { a, d, b, b, d, e });
		}
	// With depth: a second sheet behind the first, back toward the rock by up to a tenth of the width, growing from the lip
	// as the water spreads, and the two joined along the sides, so from the side the falling water is a band, not a line.
	if (body)
	{
		int front = cols * rows;
		double thickMax = DetMath::Clamp(f.Width * .1, .6, 3);
		v.resize((size_t)front * 2);
		for (int r = 0; r < rows; r++)
		{
			double h = steps[r];
			double th = h <= 0 ? 0 : Mn(thickMax * DetMath::Smooth(h / Mx(2, f.Length * .6) + .15), Mx(0, h - .3));
			for (int c = 0; c < cols; c++)
			{
				int k = r * cols + c;
				FVertex o = v[k];
				o.X -= (float)(f.Dx * th); o.Y += (float)(th * .15); o.Z -= (float)(f.Dz * th);
				o.U += .37f * (float)width; o.V += 1.7f; o.Travel += .23f; o.Edge *= .85f;
				v[(size_t)front + k] = o;
			}
		}
		for (int r = 0; r + 1 < rows; r++)
			for (int c = 0; c + 1 < cols; c++)
			{
				int a = front + r * cols + c, b = a + 1, d = a + cols, e = d + 1;
				idx.insert(idx.end(), { a, b, d, b, e, d });
			}
		// The sides: between the front's and the back's edge columns, as solid as the sheet just inside its faded edge.
		int inner = (int)std::ceil(margin / (Wide(0) / (cols - 1 < 1 ? 1 : cols - 1)));
		inner = inner < 1 ? 1 : inner > cols / 2 ? cols / 2 : inner;
		for (int pass = 0; pass < 2; pass++)
		{
			int c = pass == 0 ? 0 : cols - 1, ci = pass == 0 ? inner : cols - 1 - inner;
			int start = (int)v.size();
			v.resize((size_t)start + rows * 2);
			for (int r = 0; r < rows; r++)
			{
				int k = r * cols + c, kin = r * cols + ci;
				float a = v[kin].Edge * .8f;
				FVertex frontV = v[k], backV = v[(size_t)front + k];
				float gap = std::sqrt((frontV.X - backV.X) * (frontV.X - backV.X) + (frontV.Y - backV.Y) * (frontV.Y - backV.Y) + (frontV.Z - backV.Z) * (frontV.Z - backV.Z));
				FVertex one = v[kin], two = v[kin];
				one.X = frontV.X; one.Y = frontV.Y; one.Z = frontV.Z; one.U = frontV.U; one.V = frontV.V; one.Travel = frontV.Travel; one.Edge = a;
				two.X = backV.X; two.Y = backV.Y; two.Z = backV.Z; two.U = frontV.U + gap; two.V = frontV.V; two.Travel = frontV.Travel; two.Edge = a * .85f;
				v[(size_t)start + 2 * r] = one; v[(size_t)start + 2 * r + 1] = two;
			}
			for (int r = 0; r + 1 < rows; r++)
			{
				int a = start + 2 * r, ba = a + 1, d = a + 2, bd = a + 3;
				idx.insert(idx.end(), { a, d, ba, ba, d, bd });
			}
		}
	}
	// Where the centre line meets the pool: the foam's middle.
	const FVertex* footV = &v[(size_t)(rows - 1) * cols + cols / 2];
	for (int r = 0; r < rows; r++)
		if (v[(size_t)r * cols + cols / 2].Y <= bottom + .05) { footV = &v[(size_t)r * cols + cols / 2]; break; }
	float footX = footV->X, footY = (float)(bottom + .04), footZ = footV->Z;
	double spread = DetMath::Clamp(width * .45 + (lipLevel - bottom) * .15, 4, 35);
	m.FootX = footX; m.FootY = footY; m.FootZ = footZ;
	// Half-way down the falling water, its centre (the face's spray).
	m.FaceX = footX; m.FaceY = footY; m.FaceZ = footZ;
	for (int r = 0; r < rows; r++)
		if (v[(size_t)r * cols + cols / 2].Y <= (lipLevel + bottom) * .5) { const FVertex& c = v[(size_t)r * cols + cols / 2]; m.FaceX = c.X; m.FaceY = c.Y; m.FaceZ = c.Z; break; }
	// The foam disc: rings round the foot, a little longer downstream.
	const int rings = 8, spokes = 28;
	FVertex centreV = {}; centreV.X = footX; centreV.Y = footY; centreV.Z = footZ; centreV.Edge = 1;
	m.Foam.push_back(centreV);
	for (int ring = 1; ring <= rings; ring++)
		for (int s = 0; s < spokes; s++)
		{
			double ang = s * 6.283185307179586 / spokes, rr = spread * ring / rings;
			double down = std::cos(ang), stretch = 1 + .6 * Mx(0, down);
			double ox = (f.Dx * std::cos(ang) + ax * std::sin(ang)) * rr * stretch, oz = (f.Dz * std::cos(ang) + az * std::sin(ang)) * rr * stretch;
			FVertex o = {};
			o.X = footX + (float)ox; o.Y = footY; o.Z = footZ + (float)oz;
			o.U = (float)(std::cos(ang) * rr * stretch); o.V = (float)(std::sin(ang) * rr * stretch); // (downstream, across)
			o.Edge = (float)(1 - (double)ring / rings);
			m.Foam.push_back(o);
		}
	for (int s = 0; s < spokes; s++) m.FoamIndices.insert(m.FoamIndices.end(), { 0, 1 + s, 1 + (s + 1) % spokes });
	for (int ring = 1; ring < rings; ring++)
		for (int s = 0; s < spokes; s++)
		{
			int a = 1 + (ring - 1) * spokes + s, b = 1 + (ring - 1) * spokes + (s + 1) % spokes, c = a + spokes, d = b + spokes;
			m.FoamIndices.insert(m.FoamIndices.end(), { a, c, b, b, c, d });
		}
	m.LipLevel = (float)lipLevel; m.Left = (float)(left + .5); m.Right = (float)(right + .5);
	return m;
}
}
