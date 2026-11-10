// Landform modules of terrain revisions 17+ (ports of Thornwake Game/WorldGen landform files).
// Plan modules shape the macro raster before erosion; the others are placed on the finished rivers and coasts and
// read by the sampler. Each draws from its own random stream and appears from the revision that brought it.
#pragma once

#include "WwWorld.h"

namespace ww
{
uint64_t LandformSeed(const FWorldManifest& Manifest, const char* Id);

/// A big landform: changes the plan's land, height and mountain channels inside its areas, before erosion.
class FPlanLandform
{
public:
	virtual ~FPlanLandform() = default;
	virtual const char* Id() const = 0;
	virtual void Shape(FMacroRaster& Macro, const FWorldManifest& Manifest, FLandformSetting Setting) = 0;
	/// Walking-scale relief it adds at a point (m). rock: how bare the ground is (0..1); scree: loose rock (revision 33).
	virtual double Ground(double x, double z, double minWavelength, double& rock, double& scree) const { rock = 0; scree = 0; return 0; }
};

/// Escarpments and plateaus (landform no. 1, revision 18; revision 19 draws the face without fins or combs).
class FEscarpments : public FPlanLandform
{
public:
	explicit FEscarpments(int InRevision) : Revision(InRevision) {}
	virtual const char* Id() const override { return "escarpments"; }
	virtual void Shape(FMacroRaster& Macro, const FWorldManifest& Manifest, FLandformSetting Setting) override;
	virtual double Ground(double x, double z, double minWavelength, double& rock, double& scree) const override;
	int Count() const { return (int)Lines.size(); }

private:
	struct FLine { std::vector<double> X, Z, Along; double Length = 0, Height = 0, Depth = 0; u64 Seed = 0; double MinX = 0, MinZ = 0, MaxX = 0, MaxZ = 0; };
	void Place(const FMacroRaster& Macro, const FWorldManifest& Manifest, FLandformSetting Setting);
	bool Near(double x, double z, double distance) const;
	void BuildGrid(const FMacroRaster& Macro);
	int Cell(double v, double min) const;
	double Profile(const FLine& l, double x, double z, double face, double minWavelength, bool detail, double& s, double& nx, double& nz, double& rock, double& fallen) const;
	double Detail(const FLine& l, double s, double t, double lx, double lz, double h, double back, double minWavelength, double& rock, double& fallen) const;

	int Revision;
	std::vector<FLine> Lines;
	const FMacroRaster* Macro = nullptr;
	FMacroRaster ProfileRaster;
	double Ox = 0, Oz = 0, GridMinX = 0, GridMinZ = 0;
	int GridSize = 0;
	std::vector<int> GridStart, GridLines;
};

/// CSR lists per grid cell from per-cell vectors (the source's List<int>[] flattened in cell order).
void FlattenGrid(const std::vector<std::vector<int>>& Cells, std::vector<int>& Start, std::vector<int>& Items);
/// Raster cells with land influence above 0.5.
int LandCellCount(const FMacroRaster& Macro);

/// Gorges and canyons (landform no. 3, revision 21): land raised beside stretches of river that keep their level.
class FCanyons
{
public:
	FCanyons(const FWorldPlan& Plan, FLandformSetting Setting);
	int Count() const { return (int)Canyons.size(); }
	/// How much the land is raised here (m).
	double Uplift(double x, double z) const;
	/// How much a river's valley side here is a canyon wall (0..1).
	static double Wall(double uplift) { return DetMath::Smooth((uplift - 8) / 25); }
private:
	struct FCanyon { std::vector<double> X, Z, Along; double Depth = 0, Band = 0, Fade = 0, Length = 0; u64 Seed = 0; double MinX = 0, MinZ = 0, MaxX = 0, MaxZ = 0; };
	int Cell(double v, double min) const;
	std::vector<FCanyon> Canyons;
	int Revision = 0, GridSize = 0;
	double MinX = 0, MinZ = 0;
	std::vector<int> GridStart, GridItems;
};

/// Cirques with a tarn (landform no. 4, revision 23): bowls high on mountain flanks, mostly facing north.
class FCirques
{
public:
	FCirques(const FWorldPlan& Plan, FLandformSetting Setting);
	int Count() const { return (int)Bowls.size(); }
	/// How much the ground (metres above the sea) changes here (m).
	double Carve(double x, double z, double ground, double detail) const;
	/// The tarn here, if any (level absolute).
	bool Tarn(double x, double z, double ground, double& level) const;
private:
	struct FBowl { double Fx, Fz, Dx, Dz, Radius, Floor, Depth, Back, Front; u64 Seed; };
	int Cell(double v, double min) const;
	std::vector<FBowl> Bowls;
	double MinX = 0, MinZ = 0, Sea = 0;
	int GridSize = 0;
	std::vector<int> GridStart, GridItems;
};
}
