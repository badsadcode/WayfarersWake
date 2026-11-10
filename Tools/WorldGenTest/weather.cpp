// Checks of the day, the weather and the wind (WwWeather).   WorldGenTest <repo root> --weather
#include "../../Source/WayfarersWake/WorldGen/WwWeather.h"
#include <cstdio>

using namespace ww;

int WeatherTest()
{
	int failed = 0, checks = 0;
	auto Check = [&](bool ok, const char* what, double a = 0, double b = 0)
	{
		checks++;
		std::printf("%s %s", ok ? "ok  " : "FAIL", what);
		if (a != 0 || b != 0) std::printf(" (%.2f, %.2f)", a, b);
		std::printf("\n");
		failed += !ok;
	};
	// Daylight.
	double summer = FGameClock::DaylightHours(50, 23.44), winter = FGameClock::DaylightHours(50, -23.44), equinox = FGameClock::DaylightHours(50, 0);
	Check(summer > 15.8 && summer < 16.6 && winter > 7.6 && winter < 8.4, "50 degrees north: about 16 h of daylight in summer, 8 in winter", summer, winter);
	Check(equinox > 12 && equinox < 12.3 && FGameClock::DaylightHours(80, 23.44) == 24 && FGameClock::DaylightHours(80, -23.44) == 0, "12 h at the equinox; midnight sun and polar night at 80 degrees", equinox);
	// The sun at noon stands in the south at 90 - latitude + declination; it rises in the east.
	double el, az;
	FGameClock::Position(40, 0, 0, el, az);
	Check(std::fabs(el - 50) < .01 && std::fabs(az - 180) < .01, "noon at 40 degrees north, equinox: the sun 50 degrees up, due south", el, az);
	FGameClock::Position(40, 0, -90, el, az);
	double east, up, south;
	FGameClock::Direction(el, az, east, up, south);
	Check(std::fabs(el) < .5 && east > .99, "six hours before noon it is on the horizon, due east", el, east);
	// The clock: one game day in the set number of real minutes; day and night at their own speeds.
	FGameClock clock;
	clock.DayLengthMinutes = 180;
	clock.SetHour(6, 45);
	int day0 = clock.Day;
	double daylightSeconds = 0, total = 0;
	FSkyState sky;
	for (int i = 0; i < 180 * 60; i++) { sky = clock.Advance(1, 45); total += 1; if (sky.SunElevation > -.83) daylightSeconds += 1; }
	Check(clock.Day == day0 + 1 && std::fabs(clock.Hour - 6) < .05, "180 real minutes later it is the same hour of the next day", clock.Hour, clock.Day);
	Check(std::fabs(daylightSeconds / total - sky.DaylightHours / 24) < .02, "daylight takes its share of the real time", daylightSeconds / total, sky.DaylightHours / 24);
	// The moon: full moon stands opposite the sun.
	bool fullOpposite = false;
	FGameClock moon;
	for (int d = 0; d < 30 && !fullOpposite; d++)
	{
		moon.Day = 1 + d; moon.SetHour(0, 45);
		FSkyState m = moon.Advance(0, 45);
		if (std::fabs(m.MoonPhase - .5) < .02) fullOpposite = m.MoonElevation > 20 && m.SunElevation < -20;
	}
	Check(fullOpposite, "at full moon the moon is high at midnight");
	// Weather over 120 game days: a desert gets little rain, a rainforest a lot; values stay in range.
	double rainShare[2] = { 0, 0 }, cloudMean[2] = { 0, 0 }; bool inRange = true; int thunder = 0;
	const double climates[2] = { 200, 2200 };
	for (int c = 0; c < 2; c++)
	{
		FWeather w;
		int steps = 0;
		for (double h = 0; h < 120 * 24; h += .05, steps++)
		{
			w.Update(60, h, climates[c], 18, 0, 7319); // (a minute of real time a step: the easing keeps up)
			rainShare[c] += w.Precipitation > .05f;
			cloudMean[c] += w.Cloudiness;
			inRange = inRange && w.Cloudiness >= 0 && w.Cloudiness <= 1 && w.Precipitation >= 0 && w.Precipitation <= 1;
			if (c == 1) thunder += w.bThunder;
		}
		rainShare[c] /= steps; cloudMean[c] /= steps;
	}
	Check(inRange && rainShare[0] < .06 && rainShare[1] > .2 && rainShare[1] < .8, "share of time with rain: desert (200 mm), rainforest (2,200 mm)", rainShare[0], rainShare[1]);
	Check(cloudMean[0] > .1 && cloudMean[0] < cloudMean[1] && thunder > 0, "the wet climate is cloudier and has thunderstorms", cloudMean[0], cloudMean[1]);
	FWeather cold;
	cold.Update(1, 5, 1500, -3, 0, 1);
	Check(cold.bSnow && !cold.bThunder, "below freezing it snows");
	// Wind: veers, stays in a sensible range, storms blow harder.
	FWind wind, gale;
	double least = 1e9, most = 0, turn = 0, galeMean = 0, calmMean = 0; int n = 0;
	double first = 0;
	for (double h = 0; h < 240; h += .25, n++)
	{
		wind.Update(1, h, 0, 7319); gale.Update(1, h, 1, 7319);
		least = Mn(least, wind.SteadySpeed); most = Mx(most, wind.SteadySpeed);
		double a = std::atan2(wind.DirZ, wind.DirX);
		if (n == 0) first = a;
		turn = Mx(turn, std::fabs(a - first));
		galeMean += gale.SteadySpeed; calmMean += wind.SteadySpeed;
	}
	Check(least >= .4 && most < 12 && turn > .3 && std::fabs(wind.DirX * wind.DirX + wind.DirZ * wind.DirZ - 1) < 1e-9, "wind between calm and fresh, veering over the days", least, most);
	Check(galeMean / n > calmMean / n + 7, "a storm adds about 8 m/s", calmMean / n, galeMean / n);
	std::printf("%d checks, %d failed\n", checks, failed);
	return failed;
}
