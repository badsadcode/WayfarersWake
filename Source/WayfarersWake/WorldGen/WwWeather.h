// The day, the weather and the wind (ports of Thornwake Game/Rewrite/GameClock.cs, Weather.cs in its simple "fronts"
// form, and Wind.cs). Engine-free.
//
// FGameClock: one game day lasts a number of real minutes; daylight takes its realistic share of the day for the
// latitude and the season, and the clock runs day and night at different speeds to fit. Sun and moon positions come
// from simple spherical astronomy (the moon on a 29.5-day cycle).
// FWeather: fronts pass every several game hours; how often they bring rain, and how hard, comes from the place's
// climate (deserts stay dry, rainforests get downpours). Strong fronts are thunderstorms; where it freezes it snows.
// FWind: a direction that veers slowly over game hours and a speed with gusts, raised by storms.
// Not ported: Thornwake's newer weather field (pressure systems and storm cells moving over the map), its cloud types,
// forecasts, and the stars' turning.
#pragma once

#include "WwCore.h"

namespace ww
{
/// Where the sun and moon are (degrees; azimuth from north, clockwise; elevation above the horizon).
struct FSkyState
{
	double Hour = 12; int Day = 1;
	double SunElevation = 45, SunAzimuth = 180, MoonElevation = -30, MoonAzimuth = 0;
	double MoonPhase = 0;      // 0 new .. 0.5 full .. 1 new
	double DaylightHours = 12;
};

class FGameClock
{
public:
	/// Real minutes a game day lasts (Thornwake's default is 120; Tom plays with 180) and the season (0 spring, 1
	/// summer, 2 autumn, 3 winter; Tom plays in spring).
	double DayLengthMinutes = 180;
	int Season = 0;
	int Day = 1;
	double Hour = 10.5;
	/// Position in the day cycle (0 = sunrise .. the daylight share = sunset .. 1 = the next sunrise).
	double Phase = .2;

	static double Declination(int season) { return season == 1 ? 23.44 : season == 3 ? -23.44 : 0; }
	/// Hours of daylight at a latitude and declination (sunrise to sunset, with 0.8 degrees for refraction).
	static double DaylightHours(double latitude, double declination);
	/// Elevation and azimuth for a declination and hour angle (degrees).
	static void Position(double latitude, double declination, double hourAngle, double& elevation, double& azimuth);
	/// Unit vector toward a sky position: east, up, south.
	static void Direction(double elevation, double azimuth, double& east, double& up, double& south);

	/// Set the clock to an hour of the day.
	void SetHour(double hour, double latitude);
	/// Advance by real seconds and work out the sky at a latitude.
	FSkyState Advance(double realSeconds, double latitude);
	/// Game hours since the world began.
	double TotalHours() const { return (Day - 1) * 24.0 + Hour; }
};

class FWeather
{
public:
	float Cloudiness = .4f;     // 0 clear .. 1 overcast
	float Precipitation = 0;    // 0 none, about 0.3 light, 0.6 heavy, 0.9 storm
	bool bSnow = false, bThunder = false;
	/// Tom's settings in Thornwake: rain frequency 0.9, cloud cover 0.4.
	double RainFrequency = .9, CloudCover = .4;

	/// annualRain in mm a year, temperature in degrees C, groundSnow 0..1 (the biome's snow cover).
	void Update(double realSeconds, double gameHours, double annualRain, double temperature, double groundSnow, u64 seed);
	/// 0..1: how much of a storm this is (wind, the sea).
	float Storm() const { return Precipitation > .6f ? (Precipitation - .6f) / .4f : 0.f; }

private:
	float CloudTarget = .4f, PrecipTarget = 0;
};

class FWind
{
public:
	double DirX = .8, DirZ = .6; // unit vector the wind blows toward (east, south)
	float Speed = 6;             // m/s at about 10 m above the ground, gusts included
	float SteadySpeed = 6;       // without gusts: what builds the sea and bends the plants
	float Gust = 0;
	double Time = 0;             // the gusts' own clock (real seconds)

	/// Advance with game time (hours) and real time; storm 0..1 strengthens it.
	void Update(double realSeconds, double gameHours, float storm, u64 seed);
	/// "N", "NE" ..: where the wind comes from.
	const char* Compass() const;
};
}
