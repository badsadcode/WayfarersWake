// World types whose land does not come from placed continents (ports of Thornwake Heightmap.cs and Landforms.cs):
// "natural" types grow land cell by cell on a coarse hex grid; template types add big shapes from a list of steps.
#pragma once

#include "WwWorld.h"

namespace ww
{
/// "Natural" world shapes: a 160 x 160 hex grid of heights 0..100 (sea level 20) shaped by a list of steps
/// (Hill, Pit, Range, Trough, Strait, Mask, Add, Multiply, Smooth, Invert).
class FHeightmap
{
public:
	static constexpr int N = 160;
	static constexpr double SeaLevel = 20;
	FHeightmap(const std::vector<std::string>& Steps, u64 Seed);
	/// Height 0..100 at world fractions (x east, y south), bilinear between the hex rows.
	double Sample(double x, double y) const;
	double LandShare() const;

private:
	struct FStep { std::string Tool; std::vector<std::string> Args; };
	void Run(const FStep& s);
	int Count(const std::string& s);
	double Pick(const std::string& s);
	int PickCell(const std::string& xs, const std::string& ys, int sign, double height, bool bLine);
	void Blob(double height, const std::string& xs, const std::string& ys, int sign);
	void Line(double height, const std::string& xs, const std::string& ys, int sign, const std::string* toX, const std::string* toY);
	std::vector<int> Path(int from, int to, double wander);
	void Strait(int width, bool vertical);
	void Mask(double fraction);
	void Smooth(double fraction);
	void Invert(const std::string& axes);

	std::vector<double> H;
	FRandomStream Rng;
};

/// A compiled landform template: deterministic shapes for one world, evaluated per macro raster cell.
class FLandformTemplate
{
public:
	explicit FLandformTemplate(const FWorldManifest& Manifest);
	bool Empty() const { return Shapes.empty() && Masks.empty(); }
	int ShapeCount() const { return (int)Shapes.size(); }
	/// Adds the template to one macro cell (origin-relative coordinates). Rugged: the mountains' relief there (0..1).
	void Apply(double lx, double lz, double& land, double& relief, double& mountain, const std::function<double(double, double)>& Rugged) const;

private:
	struct FShape
	{
		double Ax, Az, Bx, Bz, Ra, Rb, T0, T1; bool bLine; int Group; double Land, Relief, Mountain; u64 Seed; double MinX, MinZ, MaxX, MaxZ;
	};
	struct FMask { int Kind; double Strength, Size; u64 Seed; }; // 0 Edges, 1 Centre, 2 North, 3 South, 4 West, 5 East
	static constexpr int Bins = 32;
	int Bin(double local) const;
	std::vector<FShape> Shapes;
	std::vector<FMask> Masks;
	std::vector<std::vector<int>> BinLists;
	double World = 1, Hill = 60;
};
}
