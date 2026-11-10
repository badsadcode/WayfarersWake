// The sound of the weather and the world, made in code (ports of Thornwake's continuous rain-and-wind synthesiser and
// its thunder, Game/Rewrite/Lightning.cs, and the small palette of Game/World/Soundscape.cs). Thornwake has no sound
// files; neither has this. Engine-free: the game asks for frames and plays them.
//
// Rain: a hiss (high band) with a deeper roar as it gets heavy, and single drops ticking in light rain. Wind: a low
// rushing band whose pitch and level rise with the speed, swelling slowly. Thunder: a clap and a roll that dies away,
// duller and later the farther the strike. Short sounds: a footstep, the axe on wood, a stone, something made, a hurt.
// Added here (not in Thornwake): running water near rivers, the sea and lakes, and a muffled world under water.
// Volumes are Tom's Thornwake settings (weather 1.05, rain 0.4, wind 0.6, thunder 0.6, master 1.2, rain tone 0).
#pragma once

#include "WwCore.h"

namespace ww
{
class FSoundscape
{
public:
	static constexpr int Rate = 22050;

	// What is heard now (the game sets these; the levels ease toward them over about a second).
	float Precipitation = 0;   // 0..1
	bool bSnowing = false;     // snow falls silently
	float WindSpeed = 0;       // m/s
	float Gust = 0;            // 0..1
	float Water = 0;           // 0..1: running water or surf close by
	float Shelter = 0;         // 0 in the open .. 1 under cover
	bool bUnderwater = false;
	// Volumes (the defaults are Tom's Thornwake settings; the game's menu sets them).
	float VolMaster = 1.2f, VolWeather = 1.05f, VolRain = .4f, VolWind = .6f, VolThunder = .6f, VolWater = 1, VolSmall = 1;
	float RainTone = 0;        // 0 a soft patter .. 1 a sharp bright hiss

	enum EBlip { Step, Wood, Stone, Craft, Hit, Hurt, Other };

	/// The next frames, interleaved left and right, each within -1..1.
	void Render(int frames, float* out);
	/// A lightning strike `distance` metres away: its thunder is heard after the sound's travel time.
	void Thunder(double distance, bool ground, int seed);
	/// A short sound now.
	void Blip(EBlip kind);

private:
	struct FVoice { std::vector<float> Samples; long long Start = 0; float Gain = 1; };
	double Next() { State ^= State << 13; State ^= State >> 7; State ^= State << 17; return (double)(State >> 11) * (1.0 / 9007199254740992.0); }

	u64 State = 0x9E3779B97F4A7C15ULL;
	long long Clock = 0; // frames rendered
	double RainLp = 0, RainLp2 = 0, RainHi = 0, WindLp = 0, WindLp2 = 0, WindBand = 0, WindPhase = 0, RainLevel = 0, WindLevel = 0, DropTimer = 0;
	double WaterLevel = 0, WaterLp = 0, WaterLp2 = 0, WaterLow = 0, WaterPhase = 0, MuffleL = 0, MuffleR = 0, Muffle = 0;
	float DropAmp = 0;
	std::vector<FVoice> Voices;
};
}
