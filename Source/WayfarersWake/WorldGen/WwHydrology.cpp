// Port of Thornwake Game/WorldGen/Hydrology.cs (terrain revision 16: no waterfalls module).
// Priority-flood, implicit stream-power erosion, lakes, river reaches with meanders, deltas, and the segment index.
// Single-threaded with fixed neighbour order, so results are deterministic and bit-identical to the source.
#include "WwWorld.h"

namespace ww
{
namespace
{
const int Dx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
const int Dz[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
const double Dist[8] = { 1, 1.4142135623730951, 1, 1.4142135623730951, 1, 1.4142135623730951, 1, 1.4142135623730951 };

// .NET's PriorityQueue<int, double> (quaternary min-heap). Replicated exactly: it is not stable, and the order in
// which equal priorities leave it decides flow receivers on flat filled ground.
struct FDotNetQueue
{
	struct FNode { int Element; double Priority; };
	std::vector<FNode> Nodes;

	void Enqueue(int Element, double Priority)
	{
		Nodes.push_back({ Element, Priority });
		FNode node = Nodes.back();
		int index = (int)Nodes.size() - 1;
		while (index > 0)
		{
			int parent = (index - 1) >> 2;
			if (node.Priority < Nodes[parent].Priority) { Nodes[index] = Nodes[parent]; index = parent; }
			else break;
		}
		Nodes[index] = node;
	}

	int Dequeue()
	{
		int element = Nodes[0].Element;
		int last = (int)Nodes.size() - 1;
		FNode node = Nodes[last];
		Nodes.pop_back();
		if (last > 0)
		{
			int size = last, index = 0, i;
			while ((i = (index << 2) + 1) < size)
			{
				FNode minChild = Nodes[i]; int minIndex = i;
				int upper = MnI(i + 4, size);
				while (++i < upper)
					if (Nodes[i].Priority < minChild.Priority) { minChild = Nodes[i]; minIndex = i; }
				if (node.Priority <= minChild.Priority) break;
				Nodes[index] = minChild; index = minIndex;
			}
			Nodes[index] = node;
		}
		return element;
	}
	bool Empty() const { return Nodes.empty(); }
};

int Flood(int n, const std::vector<double>& h, std::vector<double>& filled, std::vector<int>& receiver, std::vector<int>& order)
{
	const double Epsilon = 1e-3;
	int count = n * n;
	FDotNetQueue queue;
	queue.Nodes.reserve(count / 8);
	std::vector<uint8_t> done(count, 0);
	std::fill(receiver.begin(), receiver.end(), -1);
	for (int c = 0; c < count; c++)
	{
		filled[c] = h[c];
		if (h[c] > 0) continue;
		done[c] = 1; // sea is base level
		int cx = c % n, cz = c / n;
		for (int d = 0; d < 8; d++)
		{
			int x = cx + Dx[d], z = cz + Dz[d];
			if (x >= 0 && z >= 0 && x < n && z < n && h[z * n + x] > 0) { queue.Enqueue(c, h[c]); break; }
		}
	}
	int ordered = 0;
	while (!queue.Empty())
	{
		int c = queue.Dequeue();
		order[ordered++] = c;
		int cx = c % n, cz = c / n;
		for (int d = 0; d < 8; d++)
		{
			int x = cx + Dx[d], z = cz + Dz[d];
			if (x < 0 || z < 0 || x >= n || z >= n) continue;
			int m = z * n + x;
			if (done[m]) continue;
			done[m] = 1;
			filled[m] = Mx(h[m], filled[c] + Epsilon);
			queue.Enqueue(m, filled[m]);
		}
	}
	// Steepest descent on the filled surface.
	for (int k = 0; k < ordered; k++)
	{
		int c = order[k];
		if (h[c] <= 0) continue;
		int cx = c % n, cz = c / n, best = -1;
		double bestSlope = 0;
		for (int d = 0; d < 8; d++)
		{
			int x = cx + Dx[d], z = cz + Dz[d];
			if (x < 0 || z < 0 || x >= n || z >= n) continue;
			int m = z * n + x;
			double slope = (filled[c] - filled[m]) / Dist[d];
			if (slope > bestSlope) { bestSlope = slope; best = m; }
		}
		receiver[c] = best;
	}
	return ordered;
}

void Accumulate(int ordered, const std::vector<int>& order, const std::vector<int>& receiver, std::vector<double>& area, double cellArea)
{
	for (int k = 0; k < ordered; k++) area[order[k]] = cellArea;
	for (int k = ordered - 1; k >= 0; k--)
	{
		int c = order[k], r = receiver[c];
		if (r >= 0) area[r] += area[c];
	}
}

double CellDistance(int a, int b, int n)
{
	int dx = std::abs(a % n - b % n), dz = std::abs(a / n - b / n);
	return dx + dz == 2 ? 1.4142135623730951 : 1;
}

double Freeboard(double depth) { return .6 + .15 * depth; }

double CatmullRom(double p0, double p1, double p2, double p3, double t)
{
	return .5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
}

using FAttr = std::vector<std::vector<double>>;

/// Resample a polyline at arclength steps chosen per point from its interpolated attributes; ends kept.
void Resample(const std::vector<double>& x, const std::vector<double>& z, const FAttr& attr, const std::function<double(const std::vector<double>&)>& step,
	std::vector<double>& ox, std::vector<double>& oz, FAttr& oa)
{
	int na = (int)attr.size();
	ox.assign(1, x[0]); oz.assign(1, z[0]);
	oa.assign(na, std::vector<double>());
	std::vector<double> current(na);
	for (int a = 0; a < na; a++) { oa[a].push_back(attr[a][0]); current[a] = attr[a][0]; }
	double next = step(current), lastStep = next;
	for (int i = 0; i + 1 < (int)x.size(); i++)
	{
		double segment = std::sqrt(Sq(x[i + 1] - x[i]) + Sq(z[i + 1] - z[i])), t = 0;
		while (segment - t >= next)
		{
			t += next;
			double f = t / segment;
			ox.push_back(x[i] + (x[i + 1] - x[i]) * f); oz.push_back(z[i] + (z[i + 1] - z[i]) * f);
			for (int a = 0; a < na; a++) { current[a] = attr[a][i] + (attr[a][i + 1] - attr[a][i]) * f; oa[a].push_back(current[a]); }
			next = step(current);
			lastStep = next;
		}
		next -= segment - t;
	}
	if (ox.size() > 1 && lastStep - next < lastStep * .4)
	{
		ox.pop_back(); oz.pop_back();
		for (auto& list : oa) list.pop_back();
	}
	ox.push_back(x.back()); oz.push_back(z.back());
	for (int a = 0; a < na; a++) oa[a].push_back(attr[a].back());
}

void SmoothPass(std::vector<double>& x, std::vector<double>& z)
{
	double px = x[0], pz = z[0];
	for (int i = 1; i < (int)x.size() - 1; i++)
	{
		double nx = .25 * px + .5 * x[i] + .25 * x[i + 1], nz = .25 * pz + .5 * z[i] + .25 * z[i + 1];
		px = x[i]; pz = z[i]; x[i] = nx; z[i] = nz;
	}
}

/// Douglas-Peucker over position and attributes (see Hydrology.cs Simplify).
std::vector<int> Simplify(const std::vector<FPoint2>& p, const std::vector<double>& w, const std::vector<double>& d)
{
	int n = (int)p.size();
	std::vector<uint8_t> keep(n, 0);
	keep[0] = keep[n - 1] = 1;
	std::vector<std::pair<int, int>> stack;
	stack.push_back({ 0, n - 1 });
	while (!stack.empty())
	{
		int a = stack.back().first, b = stack.back().second;
		stack.pop_back();
		if (b - a < 2) continue;
		double dx = p[b].X - p[a].X, dz = p[b].Z - p[a].Z, len = std::sqrt(dx * dx + dz * dz);
		int worst = -1; double worstError = 1;
		for (int i = a + 1; i < b; i++)
		{
			double t = len > 0 ? DetMath::Clamp(((p[i].X - p[a].X) * dx + (p[i].Z - p[a].Z) * dz) / (len * len), 0, 1) : 0;
			double ex = p[i].X - p[a].X - dx * t, ez = p[i].Z - p[a].Z - dz * t;
			double f = (i - a) / (double)(b - a);
			double error = Mx(std::sqrt(ex * ex + ez * ez) / (.04 + .015 * w[i]),
				Mx(std::fabs(w[i] - (w[a] + (w[b] - w[a]) * f)) / (.05 + .04 * w[i]), std::fabs(d[i] - (d[a] + (d[b] - d[a]) * f)) / (.02 + .03 * d[i])));
			if (error > worstError) { worstError = error; worst = i; }
		}
		if (worst < 0 && len > Mn(120, 5 * Mn(w[a], w[b]) + 20)) worst = (a + b) / 2;
		if (worst < 0) continue;
		keep[worst] = 1;
		stack.push_back({ a, worst }); stack.push_back({ worst, b });
	}
	std::vector<int> result;
	for (int i = 0; i < n; i++) if (keep[i]) result.push_back(i);
	return result;
}

struct FChain
{
	std::vector<int> Cells; std::vector<double> Surface, Width, Depth, Catchment; int End = -1, StartLake = -1;
};

std::vector<double> Prepend(const std::vector<double>& values, double first)
{
	std::vector<double> result; result.reserve(values.size() + 1);
	result.push_back(first); result.insert(result.end(), values.begin(), values.end());
	return result;
}
}

// ---------------------------------------------------------------------------------------------------- build
FHydrology::FHydrology(const FMacroRaster& InRaster, std::vector<double>& height, const FWorldProfile& profile, const FTerrainStyle& style, u64 seed)
	: Raster(&InRaster), SeaLevel(profile.SeaLevel), MinWidth(style.RiverMinWidth), Style(style)
{
	int n = InRaster.Size, count = n * n;
	double cell = InRaster.Cell, cellArea = cell * cell;
	std::vector<double> original = height;
	std::vector<double> filled(count), area(count);
	std::vector<int> receiver(count), order(count);

	// ---- erosion
	double strength = style.ErosionStrength;
	for (int iteration = 0; iteration < style.ErosionIterations; iteration++)
	{
		int ordered = Flood(n, height, filled, receiver, order);
		Accumulate(ordered, order, receiver, area, cellArea);
		for (int k = 0; k < ordered; k++)
		{
			int c = order[k], r = receiver[c];
			if (r < 0 || height[c] <= 0) continue;
			double distance = CellDistance(c, r, n) * cell;
			double f = strength * std::sqrt(area[c]) / distance;
			double next = (height[c] + f * height[r]) / (1 + f);
			// No deposition: depressions stay depressions (they become lakes); land never sinks below the sea.
			if (next < height[c]) height[c] = Mx(next, .5);
		}
	}
	double maxIncision = 0;
	for (int c = 0; c < count; c++) maxIncision = Mx(maxIncision, original[c] - height[c]);
	MaxIncision = maxIncision;

	// ---- lakes
	int finalCount = Flood(n, height, filled, receiver, order);
	Accumulate(finalCount, order, receiver, area, cellArea);
	LakeCell.assign(count, -1);
	std::vector<uint8_t> visited(count, 0);
	std::vector<int> stack;
	struct FCandidate { std::vector<int> Members; double Level, MaxDepth, Volume; };
	std::vector<FCandidate> candidates;
	double landCells = 0;
	for (int c = 0; c < count; c++) if (height[c] > 0) landCells++;
	for (int k = 0; k < finalCount; k++)
	{
		int start = order[k];
		if (visited[start] || height[start] <= 0 || filled[start] - height[start] < .01) continue;
		FCandidate cand;
		stack.push_back(start); visited[start] = 1;
		while (!stack.empty())
		{
			int c = stack.back(); stack.pop_back(); cand.Members.push_back(c);
			int cx = c % n, cz = c / n;
			for (int d = 0; d < 8; d++)
			{
				int x = cx + Dx[d], z = cz + Dz[d];
				if (x < 0 || z < 0 || x >= n || z >= n) continue;
				int m = z * n + x;
				if (visited[m] || height[m] <= 0 || filled[m] - height[m] < .01) continue;
				visited[m] = 1; stack.push_back(m);
			}
		}
		double level = Inf, maxDepth = 0, volume = 0;
		for (int c : cand.Members) level = Mn(level, filled[c]);
		for (int c : cand.Members) { double depth = level - height[c]; maxDepth = Mx(maxDepth, depth); if (depth > 0) volume += depth * cellArea; }
		cand.Level = level; cand.MaxDepth = maxDepth; cand.Volume = volume;
		candidates.push_back(std::move(cand));
	}
	// Keep the most voluminous depressions as lakes, up to a share of the land area; fill the rest.
	std::vector<int> byVolume(candidates.size());
	for (int i = 0; i < (int)byVolume.size(); i++) byVolume[i] = i;
	std::stable_sort(byVolume.begin(), byVolume.end(), [&](int a, int b)
	{
		if (candidates[a].Volume != candidates[b].Volume) return candidates[a].Volume > candidates[b].Volume;
		return candidates[a].Members[0] < candidates[b].Members[0];
	});
	double budget = style.LakeShare * landCells, used = 0;
	std::vector<int> memberOf(count, -1);
	for (int ci : byVolume)
	{
		FCandidate& candidate = candidates[ci];
		const std::vector<int>& members = candidate.Members;
		bool keep = (int)members.size() >= style.LakeMinCells && candidate.MaxDepth >= style.LakeMinDepth && used + members.size() <= budget;
		if (!keep) { for (int c : members) height[c] = filled[c]; continue; }
		used += members.size();
		int id = (int)Lakes.size();
		for (int c : members) memberOf[c] = ci;
		int outlet = members[0];
		for (int c : members) if (receiver[c] < 0 || memberOf[receiver[c]] != ci) { outlet = c; break; }
		double sx = 0, sz = 0;
		for (int c : members) { LakeCell[c] = id; sx += InRaster.CellX(c % n); sz += InRaster.CellZ(c / n); }
		FLake lake;
		lake.Id = id; lake.Level = SeaLevel + candidate.Level; lake.Area = members.size() * cellArea; lake.Volume = candidate.Volume;
		lake.MaxDepth = candidate.MaxDepth; lake.OutletCell = outlet;
		lake.Center.X = sx / members.size(); lake.Center.Z = sz / members.size();
		Lakes.push_back(lake);
	}

	// ---- rivers
	ExtractRivers(n, height, filled, receiver, order, finalCount, area, style, seed);
}

void FHydrology::ExtractRivers(int n, std::vector<double>& height, std::vector<double>& filled, std::vector<int>& receiver, std::vector<int>& order,
	int ordered, std::vector<double>& area, const FTerrainStyle& style, u64 seed)
{
	int count = n * n;
	double minArea = style.RiverMinCatchmentKm2 * 1e6;
	auto IsRiver = [&](int c) { return c >= 0 && height[c] > 0 && LakeCell[c] < 0 && area[c] >= minArea; };
	std::vector<int> donors(count, 0);
	for (int c = 0; c < count; c++)
		if (IsRiver(c) && receiver[c] >= 0 && IsRiver(receiver[c])) donors[receiver[c]]++;
	std::vector<int> rank(count, 0);
	for (int k = 0; k < ordered; k++) rank[order[k]] = k;

	std::vector<int> starts;
	for (int c = 0; c < count; c++)
		if (IsRiver(c) && donors[c] != 1) starts.push_back(c);
	// Upstream reaches first, so a confluence knows the surfaces arriving at it. (ranks of river cells are unique)
	std::sort(starts.begin(), starts.end(), [&](int a, int b) { return rank[a] > rank[b]; });

	// Pass 1: cell chains and water surfaces, upstream first.
	std::vector<FChain> chains;
	std::vector<int> chainAt(count, -1); // start cell -> chain
	std::unordered_map<int, double> arriving; // junction cell -> lowest arriving surface
	for (int start : starts)
	{
		FChain chain;
		std::vector<int>& cells = chain.Cells;
		cells.push_back(start);
		int cur = start, end;
		for (;;)
		{
			int r = receiver[cur];
			if (r < 0 || height[r] <= 0 || LakeCell[r] >= 0 || !IsRiver(r) || donors[r] >= 2) { end = r; if (r >= 0) cells.push_back(r); break; }
			cells.push_back(r); cur = r;
		}
		int m = (int)cells.size();
		chain.Width.resize(m); chain.Depth.resize(m); chain.Catchment.resize(m); chain.Surface.resize(m);
		for (int i = 0; i < m; i++)
		{
			double km2 = area[cells[i]] / 1e6;
			chain.Catchment[i] = km2;
			chain.Width[i] = DetMath::Clamp(style.RiverWidthPerSqrtKm2 * std::sqrt(km2), style.RiverMinWidth, style.RiverMaxWidth);
			chain.Depth[i] = .5 + .35 * std::sqrt(chain.Width[i]);
		}
		int startLake = -1;
		double previous;
		auto arrive = arriving.find(start);
		if (arrive != arriving.end()) previous = arrive->second + .01;
		else
		{
			// A lake outlet starts at the lake surface.
			int sx = start % n, sz = start / n;
			for (int d = 0; d < 8 && startLake < 0; d++)
			{
				int x = sx + Dx[d], z = sz + Dz[d];
				if (x < 0 || z < 0 || x >= n || z >= n) continue;
				int mm = z * n + x;
				if (LakeCell[mm] >= 0 && receiver[mm] == start) startLake = LakeCell[mm];
			}
			previous = startLake >= 0 ? Lakes[startLake].Level - SeaLevel + .01 : height[start] - Freeboard(chain.Depth[0]) + .01;
		}
		// Below the bank (freeboard), strictly descending, never below the sea.
		for (int i = 0; i < m; i++)
		{
			int c = cells[i];
			double ground = height[c] > 0 ? height[c] : 0;
			double step = i == 0 ? .01 : style.RiverMinSlope * CellDistance(cells[i - 1], c, n) * Raster->Cell;
			double sv = Mn(previous - step, ground - Freeboard(chain.Depth[i]));
			if (i == 0 && startLake >= 0) sv = previous - .01; // an outlet leaves at the lake's own level
			if (LakeCell[c] >= 0) sv = Mx(sv, Mn(Lakes[LakeCell[c]].Level - SeaLevel, previous - 1e-3));
			sv = Mx(sv, 0);
			if (sv >= previous) sv = Mx(0, previous - 1e-4); // flat only where it meets the sea
			chain.Surface[i] = sv; previous = sv;
		}
		if (end >= 0 && height[end] > 0 && LakeCell[end] < 0)
		{
			auto other = arriving.find(end);
			arriving[end] = other != arriving.end() ? Mn(other->second, chain.Surface.back()) : chain.Surface.back();
		}
		chain.End = end; chain.StartLake = startLake;
		chainAt[start] = (int)chains.size();
		chains.push_back(std::move(chain));
	}
	auto HasChain = [&](int cell) { return cell >= 0 && chainAt[cell] >= 0; };

	// Pass 2: confluences share one level, the lowest of the reaches meeting there. Repeat until stable.
	for (int sweep = 0; sweep < 1000; sweep++)
	{
		bool changed = false;
		std::vector<std::pair<int, double>> lowest; // insertion order, as the source's dictionary enumerates
		std::unordered_map<int, int> lowestIndex;
		for (const FChain& chain : chains)
			if (HasChain(chain.End))
			{
				auto it = lowestIndex.find(chain.End);
				if (it == lowestIndex.end()) { lowestIndex[chain.End] = (int)lowest.size(); lowest.push_back({ chain.End, chain.Surface.back() }); }
				else lowest[it->second].second = Mn(lowest[it->second].second, chain.Surface.back());
			}
		for (const auto& entry : lowest)
		{
			std::vector<double>& down = chains[chainAt[entry.first]].Surface;
			double level = Mn(entry.second, down[0]);
			if (down[0] != level)
			{
				down[0] = level; changed = true;
				for (int i = 1; i < (int)down.size(); i++)
				{
					if (down[i] >= down[i - 1]) down[i] = Mx(0, down[i - 1] - 1e-4); else break;
				}
			}
		}
		for (FChain& chain : chains)
			if (HasChain(chain.End) && chain.Surface.back() != chains[chainAt[chain.End]].Surface[0])
			{
				chain.Surface.back() = Mn(chain.Surface.back(), chains[chainAt[chain.End]].Surface[0]); changed = true;
			}
		if (!changed) break;
	}

	// Pass 2b: a river entering a lake arrives at (not below) the lake surface; raise through confluences.
	std::unordered_map<int, std::vector<int>> arrivingChains;
	for (int i = 0; i < (int)chains.size(); i++)
		if (HasChain(chains[i].End)) arrivingChains[chains[i].End].push_back(i);
	std::function<void(int, double)> Raise = [&](int index, double endLevel)
	{
		std::vector<double>& surface = chains[index].Surface;
		if (surface.back() >= endLevel) return;
		surface.back() = endLevel;
		for (int i = (int)surface.size() - 2; i >= 0; i--)
		{
			if (surface[i] <= surface[i + 1]) surface[i] = surface[i + 1] + 1e-4; else break;
		}
		auto up = arrivingChains.find(chains[index].Cells[0]);
		if (up != arrivingChains.end())
			for (int u : up->second) Raise(u, chains[index].Surface[0]);
	};
	for (int i = 0; i < (int)chains.size(); i++)
		if (chains[i].End >= 0 && height[chains[i].End] > 0 && LakeCell[chains[i].End] >= 0)
			Raise(i, Lakes[LakeCell[chains[i].End]].Level - SeaLevel);

	// Pass 3: polylines and links.
	for (int id = 0; id < (int)chains.size(); id++)
	{
		const FChain& ch = chains[id];
		int end = ch.End, startLake = ch.StartLake;
		bool mouth = end < 0 || height[end] <= 0 || LakeCell[end] >= 0;
		double endWidth = mouth ? ch.Width.back() * style.RiverMouthWidening
			: HasChain(end) ? Mx(ch.Width.back(), chains[chainAt[end]].Width[0] * .85) : ch.Width.back();
		bool toSea = end < 0 || height[end] <= 0;
		std::vector<int> course = ch.Cells;
		std::vector<double> cs = ch.Surface, cw = ch.Width, cd = ch.Depth, ca = ch.Catchment;
		if (startLake >= 0)
		{
			// A lake outlet starts inside the lake (at the cell that spills into it).
			int sx = ch.Cells[0] % n, sz = ch.Cells[0] / n;
			for (int dir = 0; dir < 8; dir++)
			{
				int x = sx + Dx[dir], z = sz + Dz[dir];
				if (x < 0 || z < 0 || x >= n || z >= n) continue;
				int lakeCell = z * n + x;
				if (LakeCell[lakeCell] != startLake || receiver[lakeCell] != ch.Cells[0]) continue;
				course.insert(course.begin(), lakeCell);
				cs = Prepend(ch.Surface, ch.Surface[0]); cw = Prepend(ch.Width, ch.Width[0]); cd = Prepend(ch.Depth, ch.Depth[0]); ca = Prepend(ch.Catchment, ch.Catchment[0]);
				break;
			}
		}
		FShaped shaped = BuildReach(course, cs, cw, cd, ca, n, seed, endWidth, mouth, toSea);
		FRiverReach reach;
		reach.Id = id;
		reach.Points = std::move(shaped.Points); reach.Surface = std::move(shaped.Surface);
		reach.Width = std::move(shaped.Width); reach.Depth = std::move(shaped.Depth); reach.Catchment = std::move(shaped.Catchment);
		reach.StartLake = startLake;
		reach.EndCell = end;
		if (startLake >= 0) Lakes[startLake].OutletReach = id;
		if (end < 0 || height[end] <= 0) reach.bEndsInSea = true;
		else if (LakeCell[end] >= 0) { reach.EndLake = LakeCell[end]; Lakes[LakeCell[end]].InflowReaches.push_back(id); }
		else if (chainAt[end] >= 0) reach.DownstreamReach = chainAt[end];
		Reaches.push_back(std::move(reach));
	}
	for (int i = 0; i < (int)Reaches.size(); i++)
		if (Reaches[i].DownstreamReach >= 0) Reaches[Reaches[i].DownstreamReach].UpstreamReaches.push_back(Reaches[i].Id);
}

FHydrology::FShaped FHydrology::BuildReach(const std::vector<int>& cells, const std::vector<double>& surface, const std::vector<double>& width,
	const std::vector<double>& depth, const std::vector<double>& catchment, int n, u64 seed, double endWidth, bool mouth, bool toSea)
{
	int m = (int)cells.size();
	std::vector<double> cx(m), cz(m), sea(m);
	for (int i = 0; i < m; i++) { cx[i] = Raster->CellX(cells[i] % n); cz[i] = Raster->CellZ(cells[i] / n); }
	for (int i = 0; i < m; i++) sea[i] = surface[i] + SeaLevel;
	std::vector<double> bx, bz; FAttr battr;
	double quarter = Raster->Cell * .25;
	Resample(cx, cz, { sea, width, depth, catchment }, [quarter](const std::vector<double>&) { return quarter; }, bx, bz, battr);
	for (int pass = 0; pass < 20; pass++) SmoothPass(bx, bz);
	double drop = Mx(0, surface[0] - surface[m - 1]);
	u64 noiseSeed = seed ^ Hashing::Mix((u64)cells[0] + 0x51ED27ULL);
	return Shape(bx, bz, battr, drop, noiseSeed, endWidth, mouth, toSea, 1);
}

FHydrology::FShaped FHydrology::Shape(const std::vector<double>& bx, const std::vector<double>& bz, const FAttr& battr, double drop,
	u64 noiseSeed, double endWidth, bool mouth, bool toSea, double wanderScale)
{
	// Densify the base course (Catmull-Rom) so the adaptive resampling below follows a smooth curve.
	int nb = (int)bx.size();
	const int Sub = 8;
	int dn = (nb - 1) * Sub + 1, na = (int)battr.size();
	std::vector<double> dx(dn), dz(dn);
	FAttr dattr(na, std::vector<double>(dn));
	for (int i = 0; i < nb - 1; i++)
		for (int j = 0; j < Sub; j++)
		{
			double t = j / (double)Sub;
			int i0 = MxI(0, i - 1), i3 = MnI(nb - 1, i + 2), k = i * Sub + j;
			dx[k] = CatmullRom(bx[i0], bx[i], bx[i + 1], bx[i3], t); dz[k] = CatmullRom(bz[i0], bz[i], bz[i + 1], bz[i3], t);
			for (int a = 0; a < na; a++) dattr[a][k] = battr[a][i] + (battr[a][i + 1] - battr[a][i]) * t;
		}
	dx[dn - 1] = bx[nb - 1]; dz[dn - 1] = bz[nb - 1];
	for (int a = 0; a < na; a++) dattr[a][dn - 1] = battr[a][nb - 1];
	std::vector<double> px, pz; FAttr attr;
	Resample(dx, dz, dattr, [](const std::vector<double>& v) { return DetMath::Clamp(v[1] * .9 + 6, 12, 110); }, px, pz, attr);
	std::vector<double>& s = attr[0]; std::vector<double>& w = attr[1]; std::vector<double>& d = attr[2]; std::vector<double>& a2 = attr[3];
	int total = (int)px.size();
	std::vector<double> length(total, 0.0);
	for (int k = 1; k < total; k++) length[k] = length[k - 1] + std::sqrt(Sq(px[k] - px[k - 1]) + Sq(pz[k] - pz[k - 1]));
	double reachLength = length[total - 1];
	double slope = reachLength > 0 ? drop / reachLength : 0;
	double flatness = 1 - DetMath::Smooth((slope - .003) / .03);

	// Meanders and wander, perpendicular to the base course.
	std::vector<double> rx = px, rz = pz;
	double phase = Hashing::Unit(noiseSeed) * 6.283185307179586;
	double wander = Raster->Cell * .35 * (.4 + .6 * flatness) * wanderScale;
	double wanderLength = Mx(2200, Raster->Cell * 3);
	for (int k = 1; k < total - 1; k++)
	{
		double tx = px[k + 1] - px[k - 1], tz = pz[k + 1] - pz[k - 1];
		double len = std::sqrt(tx * tx + tz * tz);
		if (len <= 0) continue;
		double baseLambda = Mx(140, w[k] * 12), sk = length[k];
		double lambda = baseLambda * (1 + .25 * DetMath::Clamp(2.5 * Noise::Gradient(sk / (baseLambda * 3) + 1.3, 7.7, noiseSeed + 1), -1.6, 1.6));
		phase += 6.283185307179586 * (length[k] - length[k - 1]) / lambda;
		double ratio = .13 + .1 * (1 - DetMath::Smooth((w[k] - 4) / 30));
		double amplitude = lambda * ratio * DetMath::Clamp(.7 + 1.5 * Noise::Gradient(sk / (baseLambda * 4) + 4.1, 2.3, noiseSeed + 2), .2, 1.2)
			* (.45 + .55 * flatness);
		double envelope = DetMath::Smooth(sk / (baseLambda * .75)) * DetMath::Smooth((reachLength - sk) / (baseLambda * .75));
		double offset = amplitude * Style.MeanderStrength * DetMath::Sin(phase) * envelope;
		double wanderEnvelope = DetMath::Smooth(sk / (wanderLength * .4)) * DetMath::Smooth((reachLength - sk) / (wanderLength * .7));
		offset += Noise::Gradient(sk / wanderLength + 3.7, 11.1, noiseSeed + 3) * 2.5 * wander * Style.WanderStrength * wanderEnvelope;
		double bendLength = Mx(700, w[k] * 25), bendEnvelope = DetMath::Smooth(sk / (bendLength * .5)) * DetMath::Smooth((reachLength - sk) / bendLength);
		offset += Noise::Fbm(sk, 23.9, bendLength, 2, 0, noiseSeed + 4, .45) * 2.5 * (65 + w[k] * 2.5) * (.6 + .4 * flatness) * Style.WanderStrength * bendEnvelope;
		rx[k] = px[k] - tz / len * offset; rz[k] = pz[k] + tx / len * offset;
	}

	// Width: pools and narrows, broad stretches, wider on flat ground; network widths kept at both ends.
	double tail = mouth ? DetMath::Clamp(endWidth * 5, 250, 1200) : Mx(endWidth * 3, 40);
	for (int k = 0; k < total; k++)
	{
		double sk = length[k], ends = DetMath::Smooth(sk / 150) * DetMath::Smooth((reachLength - sk) / 150);
		double z1 = 2.5 * Noise::Fbm(sk, 17.3, Mx(400, w[k] * 30), 2, 0, noiseSeed + 7, .5);
		double z2 = 2.5 * Noise::Gradient(sk / Mx(150, w[k] * 8) + 5.1, 2.7, noiseSeed + 8);
		double pool = DetMath::Smooth((2.5 * Noise::Gradient(sk / Mx(900, w[k] * 45) + 9.3, 4.4, noiseSeed + 9) - .9) / .8);
		double vary = DetMath::Clamp(1 + ((1 + .3 * z1 + .12 * z2) * (1 + .9 * pool) * (.8 + .4 * flatness) - 1) * Style.WidthVariation, .4, 3.5);
		double funnel = DetMath::Smooth(1 - (reachLength - sk) / tail);
		double baseWidth = Mx(w[k] * (1 + (vary - 1) * ends), Mn(w[k], MinWidth));
		if (baseWidth < w[k]) d[k] *= std::sqrt(baseWidth / w[k]);
		w[k] = baseWidth + (Mx(endWidth, baseWidth) - baseWidth) * funnel * funnel;
		if (mouth) d[k] *= 1 + .25 * funnel;
	}

	std::vector<FPoint2> points(total);
	for (int k = 0; k < total; k++) { points[k].X = rx[k]; points[k].Z = rz[k]; }
	if (toSea && total >= 2)
	{
		// Continue straight out to sea: the detailed coastline can lie a few hundred metres beyond the raster's.
		double ex = rx[total - 1] - rx[total - 2], ez = rz[total - 1] - rz[total - 2], el = std::sqrt(ex * ex + ez * ez);
		if (el > 0)
		{
			ex /= el; ez /= el;
			const int Extra = 8;
			double lastW = w[total - 1], lastD = d[total - 1], lastA = a2[total - 1];
			for (int e = 0; e < Extra; e++)
			{
				double t = (e + 1) * 110;
				points.push_back({ rx[total - 1] + ex * t, rz[total - 1] + ez * t });
				s.push_back(SeaLevel); w.push_back(lastW); d.push_back(lastD); a2.push_back(lastA);
			}
		}
	}
	std::vector<int> keep = Simplify(points, w, d);
	FShaped out;
	for (int k : keep)
	{
		out.Points.push_back(points[k]); out.Surface.push_back(s[k]);
		out.Width.push_back((float)w[k]); out.Depth.push_back((float)d[k]); out.Catchment.push_back((float)a2[k]);
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------- refine
void FHydrology::RefineSurfaces(const FTerrainStyle& style)
{
	auto Ground = [&](double x, double z) { return Raster->Sample(1, x, z); }; // HeightChannel (metres above sea)
	std::vector<int> pending(Reaches.size());
	std::vector<int> ready; size_t head = 0;
	for (const FRiverReach& r : Reaches) pending[r.Id] = (int)r.UpstreamReaches.size();
	for (const FRiverReach& r : Reaches) if (r.UpstreamReaches.empty()) ready.push_back(r.Id);
	while (head < ready.size())
	{
		FRiverReach& r = Reaches[ready[head++]];
		const std::vector<FPoint2>& pts = r.Points;
		int np = (int)pts.size();
		std::vector<double> surface(np);
		double previous;
		if (r.StartLake >= 0) previous = Lakes[r.StartLake].Level - SeaLevel + .01;
		else if (!r.UpstreamReaches.empty())
		{
			double lowest = Inf;
			for (int u : r.UpstreamReaches) { double v = Reaches[u].Surface.back() - SeaLevel; if (v < lowest) lowest = v; }
			previous = lowest + .01;
		}
		else previous = Inf;
		// Revision 15: a lake's outlet keeps the lake's level while its course still runs inside the lake.
		bool inStartLake = style.Revision >= 15 && r.StartLake >= 0;
		for (int k = 0; k < np; k++)
		{
			if (inStartLake)
			{
				double cells;
				inStartLake = LakeDistance(pts[k].X, pts[k].Z, cells) == r.StartLake && cells < 1.6;
				if (inStartLake && k > 0) { surface[k] = previous = previous - 1e-4; continue; }
			}
			// Lowest ground across the channel at this point (centre and both edges).
			double tx = pts[MnI(k + 1, np - 1)].X - pts[MxI(k - 1, 0)].X, tz = pts[MnI(k + 1, np - 1)].Z - pts[MxI(k - 1, 0)].Z;
			double len = Mx(1e-9, std::sqrt(tx * tx + tz * tz)), half = r.Width[k] * .5 + 25;
			double ground = Mn(Ground(pts[k].X, pts[k].Z), Mn(Ground(pts[k].X - tz / len * half, pts[k].Z + tx / len * half),
				Ground(pts[k].X + tz / len * half, pts[k].Z - tx / len * half)));
			double step = k == 0 ? .01 : style.RiverMinSlope * std::sqrt(Sq(pts[k].X - pts[k - 1].X) + Sq(pts[k].Z - pts[k - 1].Z));
			double v = Mn(previous - step, ground - Freeboard(r.Depth[k]));
			if (k == 0 && r.StartLake >= 0) v = previous - .01;
			if (k == np - 1 && r.bEndsInSea) v = 0;
			v = Mx(v, 0);
			if (k > 0 && v >= surface[k - 1]) v = Mx(0, surface[k - 1] - 1e-4);
			surface[k] = v; previous = v;
		}
		for (int k = 0; k < np; k++) surface[k] += SeaLevel;
		r.Surface = std::move(surface);
		if (r.DownstreamReach >= 0 && --pending[r.DownstreamReach] == 0) ready.push_back(r.DownstreamReach);
	}
	if (style.Revision >= 20 && !style.Landform("waterfalls").Off()) AddWaterfalls(style.Landform("waterfalls"));
	// Confluences: all arriving reaches end exactly where the outgoing reach starts.
	for (FRiverReach& r : Reaches)
		if (r.DownstreamReach >= 0) r.Surface.back() = Mn(r.Surface.back(), Reaches[r.DownstreamReach].Surface[0]);
	for (FRiverReach& r : Reaches)
		if (!r.UpstreamReaches.empty())
		{
			double lowest = Inf;
			for (int u : r.UpstreamReaches) if (Reaches[u].Surface.back() < lowest) lowest = Reaches[u].Surface.back();
			r.Surface[0] = lowest;
		}
	// Lake inflows arrive at the lake surface (raise only, propagated upstream through confluences).
	std::function<void(int, double)> Raise = [&](int index, double end)
	{
		FRiverReach& r = Reaches[index];
		if (r.Surface.back() >= end) return;
		r.Surface.back() = end;
		for (int k = (int)r.Surface.size() - 2; k >= 0; k--)
		{
			if (r.Surface[k] <= r.Surface[k + 1]) r.Surface[k] = r.Surface[k + 1] + 1e-4; else break;
		}
		for (int u : r.UpstreamReaches) Raise(u, Reaches[index].Surface[0]);
	};
	for (int i = 0; i < (int)Reaches.size(); i++) if (Reaches[i].EndLake >= 0) Raise(i, Lakes[Reaches[i].EndLake].Level);
	AddDeltas(style);
	for (FRiverReach& r : Reaches)
	{
		r.Along.assign(r.Points.size(), 0.f);
		double total = 0;
		for (int k = 1; k < (int)r.Along.size(); k++)
		{
			total += std::sqrt(Sq(r.Points[k].X - r.Points[k - 1].X) + Sq(r.Points[k].Z - r.Points[k - 1].Z));
			r.Along[k] = (float)total;
		}
	}
	BuildIndex(768);
}

double FHydrology::Gorge(int reach, double along) const
{
	if (Gorges.empty()) return 0;
	auto it = Gorges.find(reach);
	if (it == Gorges.end()) return 0;
	double g = 0;
	for (const auto& stretch : it->second)
	{
		double from = stretch.first, to = stretch.second;
		if (along > from - 60 && along < to)
			g = Mx(g, DetMath::Smooth((along - from + 60) / 50) * DetMath::Smooth((to - along) / ((to - from) * .3 + 1)));
	}
	return g;
}

void FHydrology::AddGorge(int reach, double from, double to) { Gorges[reach].push_back({ from, to }); }

void FHydrology::DropFalls(int reach, const std::function<bool(int)>& gone)
{
	const FRiverReach& r = Reaches[reach];
	for (int i = (int)Falls.size() - 1; i >= 0; i--)
	{
		const FWaterfall f = Falls[i];
		if (f.Reach != reach || !gone(f.Point)) continue;
		Falls.erase(Falls.begin() + i);
		auto it = Gorges.find(reach);
		if (it != Gorges.end())
		{
			auto& list = it->second;
			list.erase(std::remove_if(list.begin(), list.end(), [&](const std::pair<double, double>& g) { return std::fabs(g.first - r.Along[f.Point]) < .1; }), list.end());
		}
	}
}

/// Waterfalls and the rapids below them (landform no. 2, revision 20): where a river drops much more steeply than
/// just upstream, the water keeps its level to the lip, falls there, and runs on in a pool and gentle rapids.
void FHydrology::AddWaterfalls(FLandformSetting setting)
{
	const double Window = 400, Upstream = 2000, MaxRun = 1500, MinAbove = 800;
	double minDrop = 15 / Mx(setting.Amount, .1), minGrade = .04, maxFall = 120 * setting.Size;
	for (FRiverReach& r : Reaches)
	{
		if (r.bDistributary || r.Points.size() < 8) continue;
		std::vector<double>& s = r.Surface;
		int np = (int)r.Points.size();
		// (Along is only filled after this step: measure it here)
		std::vector<double> along(np, 0.0);
		for (int k = 1; k < np; k++) along[k] = along[k - 1] + std::sqrt(Sq(r.Points[k].X - r.Points[k - 1].X) + Sq(r.Points[k].Z - r.Points[k - 1].Z));
		auto Ahead = [&](int k, double metres) { int j = k; while (j < np - 2 && along[j] - along[k] < metres) j++; return j; };
		// A lake's outlet keeps the lake's level while its course runs inside the lake: no fall before it has left.
		int first = 1;
		if (r.StartLake >= 0)
		{
			double cells;
			while (first < np && LakeDistance(r.Points[first].X, r.Points[first].Z, cells) == r.StartLake && cells < 1.6) first++;
		}
		for (int k = MxI(1, first); k < np - 3; k++)
		{
			// (a source has nothing upstream to judge a step against: mountain streams near their source stay cascades)
			if (along[k] < MinAbove && r.UpstreamReaches.empty() && r.StartLake < 0) continue;
			int w = Ahead(k, Window);
			double drop = s[k] - s[w], grade = drop / Mx(1, along[w] - along[k]);
			if (drop < minDrop || grade < minGrade) continue;
			// Upstream grade over the last 2 km (or what the reach has): the step must be much steeper than that.
			int u = k; while (u > 0 && along[k] - along[u] < Upstream) u--;
			double up = u < k ? (s[u] - s[k]) / Mx(1, along[k] - along[u]) : 0;
			if (grade < 4 * up) continue;
			// The steep stretch: until the river's grade falls back toward its upstream grade, or the fall would be too high.
			int end = w;
			while (end < np - 2 && along[end] - along[k] < MaxRun && (s[k] - s[end]) * .85 < maxFall)
			{
				int next = Ahead(end, 200);
				if (next == end || (s[end] - s[next]) / Mx(1, along[next] - along[end]) < Mx(2 * up, .015)) break;
				end = next;
			}
			while (end > k + 2 && (s[k] - s[end]) * .85 > maxFall) end--; // (a steep first window: trim it back)
			double top = s[k], bottom = s[end], height = (top - bottom) * .85;
			// (not where the river already reaches the sea's level)
			if (bottom < SeaLevel + .5) { k = end; continue; }
			// Below the lip: at the pool's level at once, then rapids carrying the last 15 % of the drop.
			for (int j = k + 1; j <= end; j++)
			{
				double f = (along[j] - along[k + 1]) / Mx(1, along[end] - along[k + 1]);
				s[j] = Mn(s[j], bottom + (top - bottom) * .15 * (1 - f));
			}
			// Strictly descending, and above the stretch's end, which stays as it was.
			for (int j = k + 1; j < end; j++)
			{
				if (s[j] >= s[j - 1]) s[j] = s[j - 1] - 1e-5;
				s[j] = Mx(s[j], s[end] + (end - j) * 1e-5);
			}
			// The plunge pool: deeper and wider for the first ~40 m below the lip.
			for (int j = k + 1; j <= end && along[j] - along[k] < 40; j++)
			{
				r.Depth[j] = (float)(r.Depth[j] + Mn(10, .25 * height) * setting.Size);
				r.Width[j] = (float)(r.Width[j] * (1 + .5 * setting.Size));
			}
			FWaterfall fall;
			fall.Reach = r.Id; fall.Point = k; fall.Lip = r.Points[k]; fall.Height = height;
			Falls.push_back(fall);
			Gorges[r.Id].push_back({ along[k], along[end] });
			k = end;
		}
	}
	// (the source's List.Sort: unstable; equal heights are not expected between different falls)
	std::sort(Falls.begin(), Falls.end(), [](const FWaterfall& a, const FWaterfall& b) { return a.Height > b.Height; });
}

bool FHydrology::NearestRiver(double x, double z, double radius, FRiverHit& hit) const
{
	hit = FRiverHit();
	int bx = (int)std::floor((x - IndexMinX) / IndexBucket), bz = (int)std::floor((z - IndexMinZ) / IndexBucket);
	if (bx < 0 || bz < 0 || bx >= IndexSide || bz >= IndexSide) return false;
	int b = bz * IndexSide + bx;
	double best = Inf;
	bool found = false;
	for (int i = IndexStart[b]; i < IndexStart[b + 1]; i++)
		for (int k = IndexRuns[i].First, end = IndexRuns[i].First + IndexRuns[i].Count; k < end; k++)
		{
			int r = IndexRuns[i].Reach;
			const FRiverReach& reach = Reaches[r];
			const FPoint2& p = reach.Points[k]; const FPoint2& q = reach.Points[k + 1];
			double dx = q.X - p.X, dz = q.Z - p.Z, l2 = dx * dx + dz * dz;
			double t = l2 > 0 ? DetMath::Clamp(((x - p.X) * dx + (z - p.Z) * dz) / l2, 0, 1) : 0;
			double ex = x - p.X - dx * t, ez = z - p.Z - dz * t;
			double distance = std::sqrt(ex * ex + ez * ez);
			double width = reach.Width[k] + (reach.Width[k + 1] - reach.Width[k]) * t;
			// Rank by distance from the channel edge, so a wide river's water wins over a creek's centreline.
			double score = distance - width * .5;
			if (distance > Influence(width) || (distance > radius && radius > 0)) continue;
			if (score < best)
			{
				best = score; found = true;
				hit.Reach = r; hit.Distance = distance;
				hit.Surface = reach.Surface[k] + (reach.Surface[k + 1] - reach.Surface[k]) * t;
				hit.Width = width;
				hit.Depth = reach.Depth[k] + (reach.Depth[k + 1] - reach.Depth[k]) * t;
				hit.Catchment = reach.Catchment[k] + (reach.Catchment[k + 1] - reach.Catchment[k]) * t;
			}
		}
	return found;
}

void FHydrology::AddDeltas(const FTerrainStyle& style)
{
	auto LandAt = [&](double x, double z) { return Raster->Sample(0, x, z); };
	int count = (int)Reaches.size();
	for (int id = 0; id < count; id++)
	{
		if (!Reaches[id].bEndsInSea || Reaches[id].bDistributary || Reaches[id].Catchment.back() < style.DeltaMinCatchmentKm2 || Reaches[id].Points.size() < 4) continue;
		// (copies: adding branches below may move the reach array)
		const std::vector<FPoint2> pts = Reaches[id].Points;
		const std::vector<double> mainSurface = Reaches[id].Surface;
		const std::vector<float> mainWidth = Reaches[id].Width, mainDepth = Reaches[id].Depth, mainCatchment = Reaches[id].Catchment;
		int np = (int)pts.size();
		std::vector<double> length(np, 0.0);
		for (int k = 1; k < np; k++) length[k] = length[k - 1] + std::sqrt(Sq(pts[k].X - pts[k - 1].X) + Sq(pts[k].Z - pts[k - 1].Z));
		double split = DetMath::Clamp(mainWidth.back() * 8, 500, 2400);
		int kb = np - 1;
		while (kb > 1 && length[np - 1] - length[kb] < split) kb--;
		if (mainSurface[kb] - SeaLevel > 6) continue; // only across a flat coastal plain
		double dx = pts[np - 1].X - pts[kb].X, dz = pts[np - 1].Z - pts[kb].Z, dl = std::sqrt(dx * dx + dz * dz);
		if (dl < 100) continue;
		dx /= dl; dz /= dl;
		u64 h = Hashing::Mix((u64)id * 0x9E3779B97F4A7C15ULL + 0xDE17AULL);
		for (int sideIndex = 0; sideIndex < 2; sideIndex++)
		{
			double side = sideIndex == 0 ? -1.0 : 1.0;
			double spread = dl * (.35 + .3 * Hashing::Unit(Hashing::Mix(h + (u64)(side + 2))));
			// End point out to sea, beside the main mouth.
			double ex = pts[np - 1].X - dz * spread * side + dx * dl * .1, ez = pts[np - 1].Z + dx * spread * side + dz * dl * .1;
			for (int i = 0; i < 8 && LandAt(ex, ez) > 0; i++) { ex += dx * Raster->Cell * .5; ez += dz * Raster->Cell * .5; }
			if (LandAt(ex, ez) > 0) continue;
			// Base course: a quadratic Bezier bowing away from the main channel, then meandered like any river.
			double cx = pts[kb].X + dx * dl * .55 - dz * spread * .35 * side, cz = pts[kb].Z + dz * dl * .55 + dx * spread * .35 * side;
			const int Segments = 24;
			std::vector<double> bx(Segments + 1), bz(Segments + 1);
			FAttr attr(4, std::vector<double>(Segments + 1));
			double s0 = mainSurface[kb];
			for (int i = 0; i <= Segments; i++)
			{
				double t = i / (double)Segments, u = 1 - t;
				bx[i] = u * u * pts[kb].X + 2 * u * t * cx + t * t * ex; bz[i] = u * u * pts[kb].Z + 2 * u * t * cz + t * t * ez;
				attr[0][i] = Mx(SeaLevel, s0 - (s0 - SeaLevel) * t);
				attr[1][i] = mainWidth[kb] * (.3 + .3 * t);
				attr[2][i] = mainDepth[kb] * .7;
				attr[3][i] = mainCatchment[kb] * .25;
			}
			FShaped branch = Shape(bx, bz, attr, s0 - SeaLevel, Hashing::Mix(h + (u64)(side + 5)), mainWidth[kb] * .6 * 1.6, true, true, .35);
			// Strictly descending to the sea.
			std::vector<double>& surface = branch.Surface;
			for (int i = 1; i < (int)surface.size(); i++) surface[i] = Mx(SeaLevel, Mn(surface[i], surface[i - 1] - 1e-4));
			surface.back() = SeaLevel;
			FRiverReach reach;
			reach.Id = (int)Reaches.size();
			reach.Points = std::move(branch.Points); reach.Surface = std::move(branch.Surface);
			reach.Width = std::move(branch.Width); reach.Depth = std::move(branch.Depth); reach.Catchment = std::move(branch.Catchment);
			reach.bEndsInSea = true; reach.bDistributary = true; reach.ParentReach = id; reach.ParentPoint = kb;
			Reaches.push_back(std::move(reach));
		}
	}
}

// ---------------------------------------------------------------------------------------------------- queries
int FHydrology::LakeAt(double x, double z) const
{
	int i = (int)std::floor((x - Raster->MinX) / Raster->Cell), j = (int)std::floor((z - Raster->MinZ) / Raster->Cell);
	if (i < 0 || j < 0 || i >= Raster->Size || j >= Raster->Size) return -1;
	return LakeCell[j * Raster->Size + i];
}

int FHydrology::LakeNear(double x, double z) const
{
	int i = (int)std::floor((x - Raster->MinX) / Raster->Cell), j = (int)std::floor((z - Raster->MinZ) / Raster->Cell);
	int best = -1;
	for (int b = -1; b <= 1 && best < 0; b++)
		for (int a = -1; a <= 1; a++)
		{
			int ii = i + a, jj = j + b;
			if (ii < 0 || jj < 0 || ii >= Raster->Size || jj >= Raster->Size) continue;
			int id = LakeCell[jj * Raster->Size + ii];
			if (id >= 0 && ((a == 0 && b == 0) || best < 0)) { best = id; if (a == 0 && b == 0) break; }
		}
	return best;
}

int FHydrology::LakeDistance(double x, double z, double& cells) const
{
	double gx = (x - Raster->MinX) / Raster->Cell, gz = (z - Raster->MinZ) / Raster->Cell;
	int i = (int)std::floor(gx), j = (int)std::floor(gz);
	int best = -1; double bestD = std::numeric_limits<double>::max();
	for (int b = -2; b <= 2; b++)
		for (int a = -2; a <= 2; a++)
		{
			int ii = i + a, jj = j + b;
			if (ii < 0 || jj < 0 || ii >= Raster->Size || jj >= Raster->Size) continue;
			int id = LakeCell[jj * Raster->Size + ii];
			if (id < 0) continue;
			double ddx = Mx(0, Mx(ii - gx, gx - (ii + 1))), ddz = Mx(0, Mx(jj - gz, gz - (jj + 1)));
			double d = std::sqrt(ddx * ddx + ddz * ddz);
			if (d < bestD) { bestD = d; best = id; }
		}
	cells = bestD;
	return best;
}

i64 FHydrology::PointCount() const
{
	i64 total = 0;
	for (const FRiverReach& r : Reaches) total += (i64)r.Points.size();
	return total;
}

// ---------------------------------------------------------------------------------------------------- index
void FHydrology::BuildIndex(double bucket)
{
	IndexBucket = bucket; IndexMinX = Raster->MinX; IndexMinZ = Raster->MinZ;
	IndexSide = (int)std::ceil(Raster->Size * Raster->Cell / bucket) + 1;
	auto BucketOf = [&](double offset) { return ClampI((int)std::floor(offset / IndexBucket), 0, IndexSide - 1); };
	struct FBucketRun { int Bucket, Reach, First, Count; };
	std::vector<FBucketRun> runs;
	std::map<int, std::pair<int, int>> open; // bucket -> (first, last); ordered, as the source flushes sorted by key
	for (int r = 0; r < (int)Reaches.size(); r++)
	{
		const FRiverReach& reach = Reaches[r];
		open.clear();
		for (int k = 0; k + 1 < (int)reach.Points.size(); k++)
		{
			double pad = Influence(reach.Width[k] > reach.Width[k + 1] ? reach.Width[k] : reach.Width[k + 1]);
			const FPoint2& a = reach.Points[k]; const FPoint2& b = reach.Points[k + 1];
			int x0 = BucketOf(Mn(a.X, b.X) - pad - IndexMinX), x1 = BucketOf(Mx(a.X, b.X) + pad - IndexMinX);
			int z0 = BucketOf(Mn(a.Z, b.Z) - pad - IndexMinZ), z1 = BucketOf(Mx(a.Z, b.Z) + pad - IndexMinZ);
			for (int z = z0; z <= z1; z++)
				for (int x = x0; x <= x1; x++)
				{
					int key = z * IndexSide + x;
					auto it = open.find(key);
					if (it != open.end() && it->second.second == k - 1) it->second.second = k;
					else
					{
						if (it != open.end()) { runs.push_back({ key, r, it->second.first, it->second.second - it->second.first + 1 }); it->second = { k, k }; }
						else open[key] = { k, k };
					}
				}
		}
		for (const auto& e : open) runs.push_back({ e.first, r, e.second.first, e.second.second - e.second.first + 1 });
	}
	IndexStart.assign((size_t)IndexSide * IndexSide + 1, 0);
	for (const FBucketRun& run : runs) IndexStart[run.Bucket + 1]++;
	for (int i = 0; i < IndexSide * IndexSide; i++) IndexStart[i + 1] += IndexStart[i];
	IndexRuns.resize(runs.size());
	std::vector<int> fill = IndexStart;
	for (const FBucketRun& run : runs) IndexRuns[fill[run.Bucket]++] = { run.Reach, run.First, run.Count };
}

int FHydrology::NearbyRivers(double x, double z, FRiverHit* hits, int maxHits) const
{
	int bx = (int)std::floor((x - IndexMinX) / IndexBucket), bz = (int)std::floor((z - IndexMinZ) / IndexBucket);
	if (bx < 0 || bz < 0 || bx >= IndexSide || bz >= IndexSide) return 0;
	int b = bz * IndexSide + bx, count = 0;
	double scores[32];
	for (int i = IndexStart[b]; i < IndexStart[b + 1]; i++)
		for (int k = IndexRuns[i].First, end = IndexRuns[i].First + IndexRuns[i].Count; k < end; k++)
		{
			int r = IndexRuns[i].Reach;
			const FRiverReach& reach = Reaches[r];
			const FPoint2& p = reach.Points[k]; const FPoint2& q = reach.Points[k + 1];
			double dx = q.X - p.X, dz = q.Z - p.Z, l2 = dx * dx + dz * dz;
			double t = l2 > 0 ? DetMath::Clamp(((x - p.X) * dx + (z - p.Z) * dz) / l2, 0, 1) : 0;
			double ex = x - p.X - dx * t, ez = z - p.Z - dz * t;
			double width = reach.Width[k] + (reach.Width[k + 1] - reach.Width[k]) * t;
			double influence = Influence(width), d2 = ex * ex + ez * ez;
			if (d2 > influence * influence) continue;
			double distance = std::sqrt(d2);
			double score = distance - width * .5;
			int slot;
			if (count < maxHits) slot = count++;
			else
			{
				// Buffer full: replace the farthest if this one is nearer.
				int worst = 0;
				for (int j = 1; j < count; j++) if (scores[j] > scores[worst]) worst = j;
				if (score >= scores[worst]) continue;
				slot = worst;
			}
			scores[slot] = score;
			FRiverHit& hit = hits[slot];
			hit.Reach = r; hit.Distance = distance;
			hit.Surface = reach.Surface[k] + (reach.Surface[k + 1] - reach.Surface[k]) * t;
			hit.Width = width;
			hit.Depth = reach.Depth[k] + (reach.Depth[k + 1] - reach.Depth[k]) * t;
			hit.Catchment = reach.Catchment[k] + (reach.Catchment[k + 1] - reach.Catchment[k]) * t;
			hit.Along = reach.Along[k] + (reach.Along[k + 1] - reach.Along[k]) * t;
			hit.Side = dx * (z - p.Z) - dz * (x - p.X) >= 0 ? 1 : -1;
		}
	// Deterministic order: nearest channel edge first (ties by reach id).
	for (int i = 1; i < count; i++)
		for (int j = i; j > 0 && (scores[j] < scores[j - 1] || (scores[j] == scores[j - 1] && hits[j].Reach < hits[j - 1].Reach)); j--)
		{
			std::swap(scores[j], scores[j - 1]);
			std::swap(hits[j], hits[j - 1]);
		}
	return count;
}
}
