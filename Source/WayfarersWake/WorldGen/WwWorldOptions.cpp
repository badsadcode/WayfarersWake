// A port of Thornwake Game/Rewrite/WorldGenOptions.cs (the settings and Build).
#include "WwWorldOptions.h"
#include <sstream>

namespace ww
{
namespace
{
const double OpAmounts[5] = { 0, .4, 1, 1.6, 2.5 }; // off, rare, normal, common, everywhere

FGenSetting OpSlider(const char* Key, const char* Section, const char* Name, double Min, double Max, double Step, double Default, const char* Unit, const char* Tip = "")
{
	FGenSetting s; s.Key = Key; s.Section = Section; s.Name = Name; s.Min = Min; s.Max = Max; s.Step = Step; s.Default = Default; s.Unit = Unit; s.Tip = Tip;
	return s;
}
FGenSetting OpChoice(const std::string& Key, const char* Section, const std::string& Name, std::vector<std::string> Options, int Default, const char* Tip = "")
{
	FGenSetting s; s.Key = Key; s.Section = Section; s.Name = Name; s.Min = 0; s.Max = (double)Options.size() - 1; s.Step = 1; s.Default = Default; s.Options = std::move(Options); s.Tip = Tip;
	return s;
}
}

const std::vector<std::pair<std::string, std::string>>& FWorldOptions::Landforms()
{
	static const std::vector<std::pair<std::string, std::string>> All = {
		{ "escarpments", "Escarpments and plateaus" }, { "waterfalls", "Waterfalls" }, { "canyons", "Gorges and canyons" }, { "cirques", "Cirques with a tarn" },
		{ "troughs", "Glacial troughs, hanging valleys" }, { "fjords", "Fjords" }, { "volcanoes", "Volcanoes and calderas" }, { "headlands", "Headlands and coves" },
		{ "spits", "Spits, sandbars and lagoons" }, { "oxbows", "Oxbow lakes" }, { "terraces", "River terraces" }, { "fans", "Alluvial fans" } };
	return All;
}

std::vector<FGenSetting> FWorldOptions::Catalog(const std::vector<FWorldRecipe>& Recipes, const FBiomeRegistry& Biomes)
{
	std::vector<FGenSetting> all;
	const char* Shape = "World shape";
	{
		std::vector<std::string> versions = { "current", "Build 9", "Build 8", "Preview 7", "Build 5", "Build 51", "Build 52", "Build 114", "Build 115", "Build 116", "Build 117",
			"Waterfalls (rev. 20)", "Build 118", "Canyons fixed (rev. 22)", "Build 119", "Build 120", "Troughs fixed (rev. 25)", "Fjords first (rev. 26)", "Build 121",
			"Volcanoes first (rev. 28)", "Build 122", "Build 123", "Build 125", "Build 127", "Build 129", "Build 130", "Build 132", "Headlands (rev. 36)", "Build 133", "Build 137",
			"Oxbows (rev. 39)", "Terraces (rev. 40)", "Build 139", "Fall banks (rev. 42)", "Build 140" };
		all.push_back(OpChoice("gen.version", Shape, "Terrain version", versions, 0, "How the ground is made: the current way, or as an earlier Thornwake build made it."));
		std::vector<std::string> shapes = { "the world type's" };
		for (const FWorldRecipe& r : Recipes) shapes.push_back(r.Id);
		all.push_back(OpChoice("gen.shape", Shape, "Land shape", shapes, 0, "Take the layout of land and sea from another world type, keeping the settings here."));
		std::vector<std::string> continents = { "auto" };
		for (int i = 1; i <= 16; i++) continents.push_back(std::to_string(i));
		all.push_back(OpChoice("gen.continents", Shape, "Continents", continents, 0));
		FGenSetting islands = OpSlider("gen.islands", Shape, "Islands", -1, 400, 1, -1, "");
		islands.AutoAt = -1;
		all.push_back(islands);
		all.push_back(OpSlider("gen.continent_size", Shape, "Continent size", .5, 1.6, .05, 1, "x"));
		all.push_back(OpSlider("gen.coast", Shape, "Coastline roughness", 0, 1.5, .05, 1, "x"));
		all.push_back(OpChoice("gen.ranges", Shape, "Mountain ranges per continent", { "auto", "0", "1", "2", "3", "4", "5", "6" }, 0));
		all.push_back(OpSlider("gen.mountain_height", Shape, "Mountain height", 0, 2, .05, 1, "x"));
		all.push_back(OpSlider("gen.mountain_width", Shape, "Mountain width", .5, 2, .05, 1, "x"));
		all.push_back(OpSlider("gen.hill_height", Shape, "Broad hills", 0, 2.5, .05, 1, "x"));
	}
	const char* Terrain = "Terrain";
	all.push_back(OpSlider("gen.hills", Terrain, "Rolling ground", 0, 3, .05, 1, "x"));
	all.push_back(OpSlider("gen.plains", Terrain, "Plains flatness", 0, 1, .05, .6, ""));
	all.push_back(OpSlider("gen.rock", Terrain, "Mountain crags", 0, 3, .05, 1, "x"));
	all.push_back(OpSlider("gen.coast_detail", Terrain, "Coastline detail", 0, 3, .05, 1, "x"));
	all.push_back(OpSlider("gen.erosion", Terrain, "Erosion", 0, 3, .05, 1, "x"));
	all.push_back(OpSlider("gen.shores", Terrain, "Shore steepness", 0, 3, .05, 1, "x"));
	const char* Rivers = "Rivers and lakes";
	all.push_back(OpSlider("gen.rivers", Rivers, "Number of rivers", .25, 4, .05, 1, "x"));
	all.push_back(OpSlider("gen.river_width", Rivers, "River width", .3, 3, .05, 1, "x"));
	all.push_back(OpSlider("gen.meanders", Rivers, "Meanders", 0, 3, .05, 1, "x"));
	all.push_back(OpSlider("gen.wander", Rivers, "Wandering", 0, 3, .05, 1, "x"));
	all.push_back(OpSlider("gen.width_variation", Rivers, "Width variety", 0, 3, .05, 1, "x"));
	all.push_back(OpSlider("gen.banks", Rivers, "Bank irregularity", 0, 3, .05, 1, "x"));
	all.push_back(OpSlider("gen.lakes", Rivers, "Lakes", 0, 4, .05, 1, "x"));
	all.push_back(OpSlider("gen.deltas", Rivers, "Deltas", 0, 4, .05, 1, "x"));
	const char* Forests = "Forests and objects";
	all.push_back(OpSlider("gen.forest_cover", Forests, "Forest cover", 0, 2, .05, 1.15, "x"));
	all.push_back(OpSlider("gen.forest_density", Forests, "Forest density", .2, 1.6, .05, 1.2, "x"));
	all.push_back(OpSlider("gen.rocks", Forests, "Stones, rocks and boulders", 0, 2, .05, 1, "x"));
	all.push_back(OpSlider("gen.props", Forests, "Bushes, stumps and logs", 0, 2, .05, 1, "x"));
	const char* Climate = "Climate and biomes";
	all.push_back(OpSlider("gen.latitude", Climate, "Latitude of the centre", 0, 80, 1, 45, "degrees", "0 the equator, 80 the far north."));
	FGenSetting span = OpSlider("gen.latitude_span", Climate, "Climate range", 0, 90, 1, 0, "degrees", "How many degrees of latitude the world spans from north to south. Auto: by its size.");
	span.AutoAt = 0;
	all.push_back(span);
	all.push_back(OpSlider("gen.warmth", Climate, "Warmer / colder", -15, 15, .5, 0, "C"));
	all.push_back(OpSlider("gen.wetness", Climate, "Wetter / drier", .2, 3, .05, 1, "x"));
	all.push_back(OpChoice("gen.biome_placement", Climate, "Biome layout", { "realistic", "patchwork" }, 0, "Realistic: biomes follow the climate. Patchwork: every biome in patches, whatever the climate."));
	all.push_back(OpSlider("gen.patch_size", Climate, "Patch size", 2, 60, 1, 12, "km", "For the patchwork layout."));
	for (const auto& landform : Landforms())
	{
		all.push_back(OpChoice("gen.landform." + landform.first + ".amount", "Landforms", landform.second, { "off", "rare", "normal", "common", "everywhere" }, 2));
		FGenSetting size = OpSlider("", "Landforms", "", .5, 2, .05, 1, "x");
		size.Key = "gen.landform." + landform.first + ".size"; size.Name = landform.second + " size";
		all.push_back(size);
	}
	for (const FBiome& biome : Biomes.Pool)
	{
		FGenSetting s; s.Key = "gen.biome." + biome.Id; s.Section = "Biomes in this world"; s.Name = biome.Name; s.Min = 0; s.Max = 1; s.Step = 1; s.Default = 1; s.bToggle = true;
		all.push_back(s);
	}
	return all;
}

bool FWorldOptions::Build(const std::vector<FWorldRecipe>& Recipes, const FBiomeRegistry& Biomes, const std::string& Type, double SizeKm, int Revision,
	const std::map<std::string, double>& Values, FWorldProfile& Profile, FTerrainStyle& Style, std::string& Error)
{
	const std::vector<FGenSetting> catalog = Catalog(Recipes, Biomes);
	auto Value = [&](const std::string& key)
	{
		auto it = Values.find(key);
		if (it != Values.end()) return it->second;
		for (const FGenSetting& s : catalog) if (s.Key == key) return s.Default;
		return 0.0;
	};
	auto Index = [&](const std::string& key) { return (int)std::nearbyint(Value(key)); };
	const FWorldRecipe* recipe = nullptr;
	for (const FWorldRecipe& r : Recipes) if (r.Id == Type) recipe = &r;
	if (!recipe) { Error = "Unknown world type '" + Type + "'"; return false; }
	// The land shape of another world type, if chosen.
	const int shape = Index("gen.shape");
	if (shape >= 1 && shape <= (int)Recipes.size()) recipe = &Recipes[(size_t)shape - 1];

	const int version = Index("gen.version");
	const int era = version >= 5 ? 0 : version;
	FWorldChoices choices;
	choices.SizeKm = SizeKm;
	choices.Continents = Index("gen.continents") == 0 ? -1 : Index("gen.continents");
	choices.Islands = Value("gen.islands") < 0 ? -1 : (int)Value("gen.islands");
	choices.Ranges = Index("gen.ranges") == 0 ? -1 : Index("gen.ranges") - 1;
	choices.ContinentSize = Value("gen.continent_size"); choices.CoastRoughness = Value("gen.coast"); choices.MountainHeight = Value("gen.mountain_height");
	choices.MountainWidth = Value("gen.mountain_width"); choices.Hills = Value("gen.hill_height");
	choices.ScaleRevision = era >= 2 ? 8 : 9;
	Profile = recipe->Resolve(choices);
	if (!Profile.Validate(Error)) return false;

	const FTerrainStyle b = FTerrainStyle::For(Profile);
	Style = b;
	Style.LowlandDetail = b.LowlandDetail * Value("gen.hills");
	Style.PlainsRoughness = 1 - Value("gen.plains");
	Style.MountainDetail = b.MountainDetail * Value("gen.rock");
	Style.CoastDetailShift = b.CoastDetailShift * Value("gen.coast_detail");
	Style.ErosionStrength = b.ErosionStrength * Value("gen.erosion");
	Style.RiverMinCatchmentKm2 = b.RiverMinCatchmentKm2 / Value("gen.rivers");
	Style.RiverWidthPerSqrtKm2 = b.RiverWidthPerSqrtKm2 * Value("gen.river_width");
	Style.MeanderStrength = Value("gen.meanders");
	Style.WanderStrength = Value("gen.wander");
	Style.WidthVariation = Value("gen.width_variation");
	Style.BankVariation = era == 4 ? 0 : Value("gen.banks");
	Style.ShoreSlope = era == 4 ? 0 : Value("gen.shores");
	Style.MouthBlend = era >= 3 ? 0 : 1;
	Style.Revision = era >= 2 ? 8 : era == 1 ? 9 : version >= 5 ? version + 9 : ClampI(Revision, 8, FTerrainStyle::PortedRevision);
	for (const auto& landform : Landforms())
	{
		FLandformSetting setting;
		setting.Amount = OpAmounts[ClampI(Index("gen.landform." + landform.first + ".amount"), 0, 4)];
		setting.Size = Value("gen.landform." + landform.first + ".size");
		// (as designed is not recorded: the generator's own default, as before these settings existed)
		if (setting.Amount != 1 || setting.Size != 1) Style.Landforms[landform.first] = setting;
	}
	Style.ForestCover = Value("gen.forest_cover"); Style.ForestDensity = Value("gen.forest_density");
	Style.RockAmount = Value("gen.rocks"); Style.PropAmount = Value("gen.props");
	Style.LakeShare = b.LakeShare * Value("gen.lakes");
	Style.DeltaMinCatchmentKm2 = Value("gen.deltas") <= 0 ? std::numeric_limits<double>::infinity() : b.DeltaMinCatchmentKm2 / Value("gen.deltas");
	Style.Climate.CentreLatitude = Value("gen.latitude");
	Style.Climate.LatitudeSpan = Value("gen.latitude_span") <= 0 ? -1 : Value("gen.latitude_span");
	Style.Climate.TemperatureOffset = Value("gen.warmth");
	Style.Climate.Moisture = Value("gen.wetness");
	Style.Climate.Biomes = era == 0;
	Style.Climate.BiomePlacement = Index("gen.biome_placement");
	Style.Climate.PatchSize = Style.Climate.BiomePlacement == 1 ? Value("gen.patch_size") : 0;
	Style.Climate.ExcludedBiomes.clear();
	for (const FBiome& biome : Biomes.Pool) if (Value("gen.biome." + biome.Id) < .5) Style.Climate.ExcludedBiomes.push_back(biome.Id);
	return true;
}

std::string FWorldOptions::Write(const std::map<std::string, double>& Values)
{
	std::ostringstream out;
	out.precision(10);
	bool first = true;
	for (const auto& entry : Values) { out << (first ? "" : ";") << entry.first << "=" << entry.second; first = false; }
	return out.str();
}

std::map<std::string, double> FWorldOptions::Read(const std::string& Text)
{
	std::map<std::string, double> values;
	std::stringstream in(Text);
	std::string pair;
	while (std::getline(in, pair, ';'))
	{
		size_t eq = pair.find('=');
		if (eq == std::string::npos || eq == 0) continue;
		values[pair.substr(0, eq)] = std::atof(pair.substr(eq + 1).c_str());
	}
	return values;
}
}
