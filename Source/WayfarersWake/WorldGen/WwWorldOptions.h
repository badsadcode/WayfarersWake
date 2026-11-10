// The settings of world generation (a port of Thornwake's Game/Rewrite/WorldGenOptions.cs): what the player can set
// before a world is made, and how each setting changes what the generator is given (the profile and the terrain
// style). Engine-free, so the headless test can check that a world with every setting left alone is exactly the world
// made before these settings existed.
//
// Each setting is a number under a key ("gen.rivers"). Choices hold the index of the chosen option. A world records
// only the settings that differ from their defaults, so the same record always gives the same world.
// Not ported: the amount and size of sea stacks, dunes, mesas and needle spires (this port's generator does not read
// those four yet), and saving a set of settings as a named world type.
#pragma once

#include "WwWorld.h"

namespace ww
{
struct FGenSetting
{
	std::string Key, Section, Name, Unit, Tip;
	double Min = 0, Max = 1, Step = .05, Default = 1;
	/// Choices (the value is the index); empty for a slider or a toggle.
	std::vector<std::string> Options;
	bool bToggle = false;
	/// Shown as "auto" at this value (NaN: never).
	double AutoAt = std::numeric_limits<double>::quiet_NaN();
};

class FWorldOptions
{
public:
	/// Every setting, in the order and sections of Thornwake's New world tab.
	static std::vector<FGenSetting> Catalog(const std::vector<FWorldRecipe>& Recipes, const FBiomeRegistry& Biomes);
	/// The profile and the style for a world type with these settings (a missing key is its default). `revision` is the
	/// terrain revision used when the version setting is "current". False with an error if the result is not a valid world.
	static bool Build(const std::vector<FWorldRecipe>& Recipes, const FBiomeRegistry& Biomes, const std::string& Type, double SizeKm, int Revision,
		const std::map<std::string, double>& Values, FWorldProfile& Profile, FTerrainStyle& Style, std::string& Error);
	/// "key=value;key=value" and back (what a world records; what -WwGen= takes).
	static std::string Write(const std::map<std::string, double>& Values);
	static std::map<std::string, double> Read(const std::string& Text);
	/// The landform modules whose amount and size this port's generator reads: id and name.
	static const std::vector<std::pair<std::string, std::string>>& Landforms();
};
}
