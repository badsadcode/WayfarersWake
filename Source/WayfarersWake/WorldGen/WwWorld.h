// Wayfarer's Wake world generation: profile, layout, hydrology, climate, biomes, plan and the terrain sampler.
// Port of Thornwake Game/WorldGen at terrain revision 16 (everything before the landform modules of revision 17+).
// World coordinates are Thornwake's: metres, X east, Z south, height up; the engine layer converts to Unreal units.
#pragma once

#include "WwCore.h"
#include <map>
#include <mutex>
#include <unordered_map>

namespace ww
{
// ------------------------------------------------------------------------------------------------ JSON (small reader)
struct FJson
{
	enum EType { Null, Bool, Number, String, Array, Object } Type = Null;
	double Num = 0;
	bool B = false;
	std::string Str;
	std::vector<FJson> Items;
	std::vector<std::pair<std::string, FJson>> Members; // source order kept

	/// Case-insensitive member lookup (as System.Text.Json with PropertyNameCaseInsensitive); null if absent.
	const FJson* Find(const char* Name) const;
	double NumberOr(const char* Name, double Default) const;
	std::string StringOr(const char* Name, const std::string& Default) const;
	bool BoolOr(const char* Name, bool Default) const;
	/// Parses JSON with comments and trailing commas allowed. Returns false (and Error) on damage.
	static bool Parse(const std::string& Text, FJson& Out, std::string& Error);
};

// ------------------------------------------------------------------------------------------------ profile and style
/// World-shape preset. Same schema and property order as Thornwake's WorldProfile: the digest (SHA-256 of the
/// canonical JSON) seeds every generated feature, so it must stay identical.
struct FWorldProfile
{
	std::string Id, Name;
	double WorldSize = 512000;
	int Continents = 3, Islands = 32;
	double ContinentRadius = 85000, IslandRadius = 6200, CoastRoughness = .22, SeaLevel = 0, OceanDepth = 2200;
	double LowlandHeight = 260, HillHeight = 180, HillScale = 8000;
	int RangesPerContinent = 2;
	double MountainHeight = 2600, MountainWidth = 9500;

	bool Validate(std::string& Error) const;
	std::string CanonicalJson() const;
	std::string Digest() const;
	static bool ParseCatalog(const std::string& Json, std::vector<FWorldProfile>& Out, std::string& Error);
};

struct FClimateSettings
{
	double CentreLatitude = 45;
	double LatitudeSpan = -1;
	double TemperatureOffset = 0;
	double Moisture = 1;
	bool Biomes = true;
	int BiomePlacement = 0;
	double PatchSize = 0;
	std::vector<std::string> ExcludedBiomes;
	double Span(double WorldSize) const;
};

/// A world's setting for one landform module: Amount 0 = off, 1 = as designed; Size 1 = as designed.
struct FLandformSetting { double Amount = 1, Size = 1; bool Off() const { return Amount <= 0; } };

/// Terrain tuning outside the frozen profile schema (Thornwake TerrainStyle).
struct FTerrainStyle
{
	double LowlandDetail = 30, DetailWavelength = 1600, DetailGain = .6, PlainsRoughness = .4, RegionWavelength = 28000;
	double MountainDetail = .05, CoastDetailShift = 260;
	double ErosionStrength = .006; int ErosionIterations = 10;
	double LakeMinDepth = 8; int LakeMinCells = 4; double LakeShare = .03;
	double RiverMinCatchmentKm2 = 30, RiverWidthPerSqrtKm2 = .9, RiverMinWidth = 4, RiverMaxWidth = 320, RiverMinSlope = .00005;
	double RiverMouthWidening = 3, DeltaMinCatchmentKm2 = 1500;
	double ForestCover = 1.15, ForestDensity = 1.2, RockAmount = 1, PropAmount = 1;
	/// Terrain revision: worlds record theirs, so a new revision never changes an existing world. This port implements
	/// 8..PortedRevision; Thornwake's current is 44.
	int Revision = PortedRevision;
	/// Landform module settings by module id (revision 17 on; missing = as designed).
	std::map<std::string, FLandformSetting> Landforms;
	FLandformSetting Landform(const char* Id) const
	{
		if (Revision >= 17) { auto it = Landforms.find(Id); if (it != Landforms.end()) return it->second; }
		return FLandformSetting();
	}
	FClimateSettings Climate;
	double MouthBlend = 1, ShoreSlope = 1, BankVariation = 1, MeanderStrength = 1, WanderStrength = 1, WidthVariation = 1;

	static constexpr int PortedRevision = 27;
	static FTerrainStyle For(const FWorldProfile& Profile) { FTerrainStyle s; s.LowlandDetail = 20 + Profile.HillHeight * .05; return s; }
};

struct FWorldManifest
{
	static constexpr int CurrentGenerator = 1;
	int Seed = 0;
	double OriginX = 0, OriginZ = 0;
	FWorldProfile Profile;
	std::string ProfileDigest;
	static FWorldManifest Create(int Seed, const FWorldProfile& Profile, double OriginX = 0, double OriginZ = 0);
};

/// Player choices on the world creation screen; negative counts / 1.0 = Auto.
struct FWorldChoices
{
	double SizeKm = 0;
	int Continents = -1, Islands = -1, Ranges = -1;
	double ContinentSize = 1, CoastRoughness = 1, MountainHeight = 1, MountainWidth = 1, Hills = 1;
	int ScaleRevision = 9;
};

/// A world type as a size-independent recipe (Thornwake WorldRecipe): at its native size with all choices on Auto
/// it gives exactly its base profile; other sizes scale proportions and counts.
struct FWorldRecipe
{
	std::string Id, Name, Description;
	FWorldProfile Base;
	bool bMainland = false;
	double NativeKm() const { return Base.WorldSize / 1000; }
	FWorldProfile Resolve(const FWorldChoices& Choices) const;

	/// Selectable world sizes in km, exactly Thornwake's: 32 … 4096.
	static const std::vector<double>& Sizes();
	/// The classic (placed-continent) world types. Template and "natural" heightmap types are not ported yet.
	static std::vector<FWorldRecipe> Catalog(const std::vector<FWorldProfile>& Profiles);
};

// ------------------------------------------------------------------------------------------------ layout
struct FLandFeature
{
	std::string Id; bool bIsland = false; FPoint2 Center; double RadiusX = 0, RadiusZ = 0, Angle = 0; u64 NoiseSeed = 0;
	double Cos = 1, Sin = 0, LobePhase = 0;
};
struct FBeltKnot { FPoint2 Point; double Width = 0, Height = 0, U = 0; };
struct FBeltPass { double U = 0, Span = 0, Depth = 0; };
struct FMountainBelt
{
	std::string Id; int Continent = 0; std::vector<FBeltKnot> Points; std::vector<FBeltPass> Passes; double Taper = 0;
	double MinX = 0, MinZ = 0, MaxX = 0, MaxZ = 0, BaseWidth = 0, Amplitude = 0;
};

/// Where continents, islands and mountain belts are.
class FWorldLayout
{
public:
	static constexpr int MaximumSegmentsPerBelt = 32;
	FWorldManifest Manifest;
	std::vector<FLandFeature> Land;
	std::vector<FMountainBelt> Belts;
	u64 MountainNoise = 0, ReliefNoise = 0;

	explicit FWorldLayout(const FWorldManifest& InManifest);
	double LandInfluence(double x, double z) const;
	void MountainWarp(double lx, double lz, double& wx, double& wz) const;
	double RuggedBroad(double wx, double wz) const;
	void MountainEnvelope(double lx, double lz, double& core, double& shoulder) const;
private:
	void BuildBelts();
};

// ------------------------------------------------------------------------------------------------ hydrology
enum class EWaterKind : uint8_t { None, Ocean, Lake, River, Pond };

struct FLake
{
	int Id = 0; double Level = 0, Area = 0, Volume = 0, MaxDepth = 0; int OutletCell = 0; FPoint2 Center;
	int OutletReach = -1; std::vector<int> InflowReaches;
};

struct FRiverReach
{
	int Id = 0;
	std::vector<FPoint2> Points;
	std::vector<double> Surface;                 // absolute metres, strictly decreasing downstream
	std::vector<float> Width, Depth, Catchment;  // per point
	std::vector<float> Along;                    // metres from the reach start
	int DownstreamReach = -1, EndLake = -1, StartLake = -1;
	bool bEndsInSea = false, bDistributary = false;
	int ParentReach = -1, ParentPoint = -1, EndCell = -1;
	std::vector<int> UpstreamReaches;
	double Length() const { return Along.empty() ? 0 : Along.back(); }
};

struct FRiverHit
{
	int Reach = 0; double Distance = 0, Surface = 0, Width = 0, Depth = 0, Catchment = 0, Along = 0; int Side = 0;
};

/// Raster-scale erosion and hydrology: priority-flood, stream-power incision, lakes, river reaches.
class FHydrology
{
public:
	std::vector<FLake> Lakes;
	std::vector<FRiverReach> Reaches;
	std::vector<int> LakeCell;
	double MaxIncision = 0;

	FHydrology(const FMacroRaster& Raster, std::vector<double>& Height, const FWorldProfile& Profile, const FTerrainStyle& Style, u64 Seed);
	void RefineSurfaces(const FTerrainStyle& Style);

	int LakeAt(double x, double z) const;
	int LakeNear(double x, double z) const;
	int LakeDistance(double x, double z, double& cells) const;
	int NearbyRivers(double x, double z, FRiverHit* Hits, int MaxHits) const;
	/// Nearest river within radius metres (ranked by distance from the channel edge), if any.
	bool NearestRiver(double x, double z, double radius, FRiverHit& Hit) const;

	/// A waterfall (revision 20): where on which reach, the lip's position, the height of the drop.
	struct FWaterfall { int Reach = 0, Point = 0; FPoint2 Lip; double Height = 0; };
	/// Waterfalls of this world, biggest first.
	std::vector<FWaterfall> Falls;
	/// How much a point of a reach lies in the gorge below a waterfall (0..1).
	double Gorge(int Reach, double Along) const;
	/// A gorge below a fall made by another landform (glacial troughs).
	void AddGorge(int Reach, double From, double To);
	/// Removes the falls of a reach whose drop another landform has taken away (and the gorge below each).
	void DropFalls(int Reach, const std::function<bool(int)>& Gone);
	i64 PointCount() const;

	static double Bank(double width) { return Mx(8, width * .8); }
	static double MaxFloodplain(double width) { return Mx(260, width * 3); }
	static double Influence(double width) { return width * .5 + Bank(width) + MaxFloodplain(width); }
	static double Floodplain(double width, double rise) { return DetMath::Clamp(Mx(Mx(30, width * 2.5), rise * 4), 30, MaxFloodplain(width)); }

private:
	struct FShaped { std::vector<FPoint2> Points; std::vector<double> Surface; std::vector<float> Width, Depth, Catchment; };
	void ExtractRivers(int n, std::vector<double>& height, std::vector<double>& filled, std::vector<int>& receiver, std::vector<int>& order,
		int ordered, std::vector<double>& area, const FTerrainStyle& style, u64 seed);
	FShaped BuildReach(const std::vector<int>& cells, const std::vector<double>& surface, const std::vector<double>& width,
		const std::vector<double>& depth, const std::vector<double>& catchment, int n, u64 seed, double endWidth, bool mouth, bool toSea);
	FShaped Shape(const std::vector<double>& bx, const std::vector<double>& bz, const std::vector<std::vector<double>>& battr, double drop,
		u64 noiseSeed, double endWidth, bool mouth, bool toSea, double wanderScale);
	void AddDeltas(const FTerrainStyle& style);
	void AddWaterfalls(FLandformSetting setting);
	std::unordered_map<int, std::vector<std::pair<double, double>>> Gorges; // reach -> (from, to) metres along
	void BuildIndex(double bucket);

	const FMacroRaster* Raster;
	double SeaLevel, MinWidth;
	FTerrainStyle Style;
	// Segment index (CSR bucket grid of runs of consecutive segments).
	struct FRun { int Reach, First, Count; };
	double IndexMinX = 0, IndexMinZ = 0, IndexBucket = 768; int IndexSide = 0;
	std::vector<int> IndexStart; std::vector<FRun> IndexRuns;
};

// ------------------------------------------------------------------------------------------------ biomes
struct FBiomePlacement
{
	double Weight = 1;
	bool bTemperature = false, bMoisture = false, bElevation = false, bSlope = false, bMountain = false, bLatitude = false;
	double Temperature[2] = { 0, 0 }, Moisture[2] = { 0, 0 }, Elevation[2] = { 0, 0 }, Slope[2] = { 0, 0 }, Mountain[2] = { 0, 0 }, Latitude[2] = { 0, 0 };
	double Coast = 0, River = 0;
	double RegionSize[2] = { 30, 250 };
	bool bPatch = false; std::vector<std::string> PatchWithin; double PatchCoverage = .15;
	std::vector<std::string> PreferNeighbours, ForbiddenNeighbours;
};

struct FColor3 { float R = 0, G = 0, B = 0; };

/// A biome of the generation pool with the data the generator and the first Unreal presentation need.
struct FBiome
{
	std::string Id, Name;
	int Index = 0;
	FBiomePlacement Placement;
	// terrain and water
	double Flatten = 0, Dunes = 0, Mesas = 0, Spires = 0;
	double Pools = 0, PoolDepth = 1, PoolSize = 16, PoolChannels = 0;
	// look and vegetation (presentation)
	double RockExposure = 0, Darkness = 0, TreeDensity = 1, GrassAmount = 1, Wetness = 0;
	FColor3 Grass, Dry, Soil, ForestFloor, Rock, Map;
	double Snow = 0;
	std::vector<std::pair<std::string, double>> Trees; // asset name, cumulative weight
	bool ShapesTerrain() const { return Pools > 0 || Flatten > 0 || Dunes > 0 || Mesas > 0 || Spires > 0; }
};

class FBiomeRegistry
{
public:
	std::vector<FBiome> Pool;
	/// Built-in definitions (Content/biomes.json + asset_sets.json format). False and Error on damaged data.
	static bool Parse(const std::string& BiomesJson, const std::string& SetsJson, FBiomeRegistry& Out, std::string& Error);
	/// Biomes off: one classic temperate biome.
	static FBiomeRegistry ClassicOnly();
};

struct FBiomeTerrainMods { double Pools = 0, PoolDepth = 0, PoolSize = 0, PoolChannels = 0, Flatten = 0, Dunes = 0, Mesas = 0, Spires = 0, SpirePresence = 0; };

class FWorldPlan;
class FPlanLandform;

/// Annual mean temperature and precipitation on a raster half the macro resolution.
class FClimate
{
public:
	static constexpr int TemperatureChannel = 0, PrecipitationChannel = 1;
	FMacroRaster Raster;
	FClimateSettings Settings;
	FClimate(const FWorldPlan& Plan, const FClimateSettings& InSettings);
	double Latitude(double z) const;
	static double SeaLevelTemperature(double latitude) { return 28 - .0078 * latitude * latitude; }
	void At(double x, double z, double elevation, double& Temperature, double& Precipitation) const;
	double Snow(double groundSnow, double temperature, double elevation) const;
private:
	double North, South, World, OriginZ;
};

/// Biome regions: the three best biomes per climate cell, blended with a domain warp when sampled.
class FBiomeMap
{
public:
	FBiomeRegistry Registry;
	FBiomeMap(const FWorldPlan& Plan, const FClimate& Climate, const FBiomeRegistry& InRegistry);
	bool TerrainAt(double x, double z, FBiomeTerrainMods& mods, bool allBiomes) const;
	/// Every contributing biome with its weight (up to 12; weights sum to 1). Returns the count.
	int SampleAll(double x, double z, int* ids, double* weights) const;
	/// The two strongest biomes and the first one's share (0.5..1).
	void Sample(double x, double z, int& a, int& b, double& shareA) const;
	std::vector<double> Coverage() const;
private:
	void Patchwork(std::vector<std::vector<float>>& fit, const std::vector<uint8_t>& land, const std::vector<float>& elevation,
		const std::vector<float>& mountain, const std::vector<uint8_t>& excluded, double size, int n);
	void PoolCells();
	const FMacroRaster* Grid = nullptr;
	std::vector<uint8_t> Ids; std::vector<float> W; std::vector<uint8_t> LandCells, PoolNear;
	int N = 0; u64 Seed = 0; double Warp = 0;
};

// ------------------------------------------------------------------------------------------------ plan
/// Everything about a world that is computed once: layout, macro rasters, hydrology, climate, biomes.
/// Immutable after construction and safe to share between threads.
class FWorldPlan
{
public:
	static constexpr int LandChannel = 0, HeightChannel = 1, MountainChannel = 2;
	FWorldManifest Manifest;
	FTerrainStyle Style;
	FWorldLayout Layout;
	FMacroRaster Macro;
	std::unique_ptr<FHydrology> Hydrology;
	std::unique_ptr<FClimate> Climate;
	std::unique_ptr<FBiomeMap> Biomes;
	std::unique_ptr<class FCanyons> Canyons;   // revision 21
	std::unique_ptr<class FCirques> Cirques;   // revision 23
	std::unique_ptr<class FTroughs> Troughs;   // revisions 24-27
	/// Big landform modules applied before erosion (revision 17 on).
	std::vector<std::unique_ptr<FPlanLandform>> PlanLandforms;
	double BuildMilliseconds = 0;

	const FWorldProfile& Profile() const { return Manifest.Profile; }
	static int RasterSizeFor(const FWorldProfile& Profile);
	/// Stage: called with 0 land shape, 2 erosion/rivers/lakes, 3 climate, 4 biomes as each starts.
	FWorldPlan(const FWorldManifest& InManifest, const FTerrainStyle& InStyle, const FBiomeRegistry& InBiomes,
		const std::function<void(int)>& Stage = nullptr, int RasterSizeOverride = 0);
	~FWorldPlan();
	/// Approximate bytes held by the plan (rasters, rivers, biome map).
	i64 MemoryBytes() const;
};

// ------------------------------------------------------------------------------------------------ player edits
/// Player changes to the generated ground: a sparse height offset on a 1 m vertex grid in 32 m chunks.
/// Readers take a shared snapshot per chunk; writers replace whole chunks under a lock.
class FTerrainEdits
{
public:
	static constexpr int ChunkVertices = 32;
	using FChunk = std::shared_ptr<const std::vector<float>>;
	double Delta(double x, double z) const;
	/// Raise/lower the ground with a smooth round brush. Touched rectangle returned in world metres.
	void Apply(double x, double z, double radius, double amount, double limit, double& MinX, double& MinZ, double& MaxX, double& MaxZ);
	bool Empty() const { return bEmpty; }
	i64 Version() const { return VersionCounter; }
	/// Whole chunks for saving and replication.
	std::vector<std::pair<std::pair<i64, i64>, FChunk>> Snapshot() const;
	void SetChunk(i64 cx, i64 cz, const std::vector<float>* Offsets);
	void Clear();
private:
	float Vertex(i64 x, i64 z) const;
	mutable std::mutex Lock;
	std::map<std::pair<i64, i64>, FChunk> Chunks;
	volatile bool bEmpty = true;
	i64 VersionCounter = 0;
};

// ------------------------------------------------------------------------------------------------ sampler
struct FTerrainSample
{
	double Height = 0;      // metres, absolute
	double Land = 0;        // > 0 land, <= 0 sea
	double Mountain = 0;    // 0..1 share of profile mountain height
	double Detail = 0;      // metres of walking-scale relief in Height
	EWaterKind Water = EWaterKind::None;
	double WaterLevel = 0;  // absolute surface elevation (NaN if dry)
	int WaterBody = -1;     // lake id or river reach id
	double Plume = 0;
	double Lava = 0;        // 0..1 lava cover (revision 31)
	double Scree = 0;       // 0..1 loose rock fallen from a wall above (revision 33)
};

/// The terrain authority for generated ground: a pure, thread-safe function of world position.
/// minWavelength is the LOD knob (about twice the vertex spacing): shorter detail octaves are skipped.
class FTerrainSampler
{
public:
	std::shared_ptr<const FWorldPlan> Plan;
	FTerrainEdits Edits;

	explicit FTerrainSampler(std::shared_ptr<const FWorldPlan> InPlan);
	FTerrainSample Sample(double x, double z, double minWavelength = 2) const;
	double Height(double x, double z, double minWavelength = 2) const { return Sample(x, z, minWavelength).Height; }
	/// Row-major grid of heights, each bit-identical to Height() at that point (tiles share exact edges).
	void HeightGrid(double x0, double z0, double spacing, int nx, int nz, double* output, double minWavelength = -1) const;

private:
	void Coast(double x, double z, double lx, double lz, double minWavelength, double& land, double& macroHeight, double& mountainShare,
		double& rasterLand, double& coastDistance) const;
	FTerrainSample Sea(double x, double z, double land, double coastDistance) const;
	double Continent(double land, double rasterLand, double macroHeight, double coastDistance, double& mountainShare) const;
	void Detail(double lx, double lz, double minWavelength, double land, double mountainShare, double& inland, double& detail, double& roughness) const;
	double Rivers(double x, double z, double minWavelength, double baseHeight, double inland, double detail, double& riverDamp,
		EWaterKind& water, double& level, int& body, double& scree, double wall) const;
	void LakesStage(double x, double z, double& height, EWaterKind& water, double& level, int& body, double& lakePlume, double& lakeCells) const;
	double Pool(double lx, double lz, double height, double smooth, double cover, double depth, double size, double channels,
		double minWavelength, EWaterKind& water, double& level) const;
	double Excess(double lx, double lz, double size, double minW, double cover, double channels) const;
	double PoolNoise(double x, double z, double size, double minWavelength) const;
	double ChannelNoise(double x, double z, double size, double minWavelength) const;
	double LeveeFactor(const FRiverHit& r) const;
	double MouthChannel(double x, double z, double bed, int lake, double& plume) const;
	double BeachWidth(const FRiverHit& r) const;
	double WaterHalfWidth(const FRiverHit& r, double& shelf, double& slow) const;
	double Estuary(const FRiverHit& r) const;
	double CoastAlong(int index) const;
	double Profile(const FRiverHit& r0, double terrain, double rise, double spacing, double& weight, double& inside, double x, double z, double wall) const;
	double Ledge(double h, const FRiverHit& r, double half, double bulge, double& inside) const;
	double Shelf(double bed, const FRiverHit& r, double nominal, double half, double shelf) const;
	double ChannelProfile(const FRiverHit& r, double half, double terrain, double rise, double& weight, double& inside, double distance,
		double beach, double estuary, double x, double z, double wall) const;
	double GorgeWall(double bankTop, double terrain, double w, double ramp, double gorge, double x, double z) const;
	double PlanGround(double x, double z, double minWavelength, double& bare, double& scree) const;
	double BiomeLandforms(double x, double z, double lx, double lz, double detail, double inland, double minWavelength, const FBiomeTerrainMods& mods) const;
	double DuneHeight(double lx, double lz, double minWavelength) const;
	double MesaUplift(double lx, double lz, double minWavelength) const;
	double MesaTerrace(double h, double lx, double lz, double minWavelength) const;
	double SpireHeight(double lx, double lz, double minWavelength, double density) const;

	const FWorldProfile& P;
	const FTerrainStyle& Style;
	const FMacroRaster& Macro;
	const FHydrology& Hydro;
	double Ox, Oz;
	u64 DetailSeed, RockSeed, CoastSeed, RegionSeed, BankSeed, PoolSeed, LandformSeed;
	double DuneAngle;
	std::vector<double> PoolQuantiles, ChannelQuantiles;
	double PoolStd = 1, ChannelStd = 1, CoastBand = .25;
	const class FCanyons* Canyons = nullptr;
	const class FCirques* Cirques = nullptr;
	const class FTroughs* Troughs = nullptr;
	std::vector<double> CoastAlongCache; // computed up front (the source caches lazily)
};
}
