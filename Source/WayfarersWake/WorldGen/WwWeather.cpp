// Ports of Thornwake Game/Rewrite/GameClock.cs, Weather.cs (Update: the fronts model) and Wind.cs (Update).
#include "WwWeather.h"

namespace ww
{
namespace
{
const double WePi = 3.14159265358979323846;
double WeRad(double d) { return d * WePi / 180; }
double WeDeg(double r) { return r * 180 / WePi; }
float WeMoveToward(float from, float to, float delta) { return std::fabs(to - from) <= delta ? to : from + (to > from ? delta : -delta); }
}

// ----------------------------------------------------------------------------------------------------------- clock
double FGameClock::DaylightHours(double latitude, double declination)
{
	double phi = WeRad(latitude), d = WeRad(declination), h0 = WeRad(-.83);
	double c = (std::sin(h0) - std::sin(phi) * std::sin(d)) / (std::cos(phi) * std::cos(d));
	if (c <= -1) return 24; // midnight sun
	if (c >= 1) return 0;   // polar night
	return 2 * WeDeg(std::acos(c)) / 15;
}

void FGameClock::Position(double latitude, double declination, double hourAngle, double& elevation, double& azimuth)
{
	double phi = WeRad(latitude), d = WeRad(declination), h = WeRad(hourAngle);
	double sinEl = std::sin(phi) * std::sin(d) + std::cos(phi) * std::cos(d) * std::cos(h);
	double el = std::asin(DetMath::Clamp(sinEl, -1, 1));
	double az = std::atan2(-std::sin(h) * std::cos(d), std::cos(phi) * std::sin(d) - std::sin(phi) * std::cos(d) * std::cos(h));
	elevation = WeDeg(el);
	azimuth = std::fmod(WeDeg(az) + 360, 360);
}

void FGameClock::Direction(double elevation, double azimuth, double& east, double& up, double& south)
{
	// (Thornwake: north = -Z, east = +X, up = +Y)
	double el = WeRad(elevation), az = WeRad(azimuth);
	east = std::cos(el) * std::sin(az); up = std::sin(el); south = -std::cos(el) * std::cos(az);
}

void FGameClock::SetHour(double hour, double latitude)
{
	double hours = DetMath::Clamp(DaylightHours(latitude, Declination(Season)), .5, 23.5), share = DetMath::Clamp(hours / 24, .05, .95);
	double sunrise = 12 - hours / 2;
	double sinceRise = std::fmod(std::fmod(hour - sunrise, 24) + 24, 24);
	Phase = sinceRise < hours ? sinceRise / hours * share : share + (sinceRise - hours) / (24 - hours) * (1 - share);
	Hour = hour;
}

FSkyState FGameClock::Advance(double realSeconds, double latitude)
{
	double hours = DetMath::Clamp(DaylightHours(latitude, Declination(Season)), .5, 23.5), share = DetMath::Clamp(hours / 24, .05, .95);
	if (realSeconds > 0 && DayLengthMinutes > 0)
	{
		Phase += realSeconds / (DayLengthMinutes * 60);
		while (Phase >= 1) { Phase -= 1; Day++; }
	}
	double sunrise = 12 - hours / 2;
	Hour = Phase < share ? sunrise + Phase / share * hours : sunrise + hours + (Phase - share) / (1 - share) * (24 - hours);
	Hour = std::fmod(std::fmod(Hour, 24) + 24, 24);
	FSkyState s;
	s.Hour = Hour; s.Day = Day; s.DaylightHours = hours;
	double decl = Declination(Season);
	Position(latitude, decl, (Hour - 12) * 15, s.SunElevation, s.SunAzimuth);
	// Moon: a 29.5-day cycle; it runs behind the sun by the phase angle (new moon with the sun, full moon opposite).
	double phase = std::fmod((Day - 1 + Hour / 24) / 29.53 + .35, 1.0); // (starts a few days before full)
	double moonDecl = -decl * std::cos(phase * 2 * WePi) + 5 * std::sin(Day * .41);
	Position(latitude, moonDecl, (Hour - 12) * 15 - phase * 360, s.MoonElevation, s.MoonAzimuth);
	s.MoonPhase = phase;
	return s;
}

// --------------------------------------------------------------------------------------------------------- weather
void FWeather::Update(double realSeconds, double gameHours, double annualRain, double temperature, double groundSnow, u64 seed)
{
	// Fronts: a slow signal over game hours (big systems about a day, smaller showers a few hours).
	double front = .5 + .38 * Noise::Value(gameHours / 16, 1, seed + 21) + .17 * Noise::Value(gameHours / 4.5, 2, seed + 22);
	// Wetter climates: rain at weaker fronts. About 250 mm a year: only the strongest fronts; 1800 mm: most of them.
	double threshold = DetMath::Clamp(1.02 - annualRain / 2600, .42, .97) / RainFrequency;
	double humid = DetMath::Clamp(annualRain / 1500, 0, 1);
	// The cloud cover setting biases the weather: 0.45 as the climate says, lower mostly clear, higher mostly grey.
	double bias = (CloudCover - .45) * 1.3;
	CloudTarget = (float)DetMath::Clamp((front - .25) * 1.5 + .15 * humid + bias, 0, 1);
	double over = front - threshold;
	PrecipTarget = over <= 0 ? 0.f : (float)DetMath::Clamp(.25 + over * 3.2, 0, 1);
	bSnow = temperature < 1 || (groundSnow > .45 && temperature < 4);
	bThunder = PrecipTarget > .78f && temperature > 6;
	// Weather changes visibly but not abruptly: a few real seconds to minutes.
	float rate = (float)realSeconds / 25;
	float sign = PrecipTarget > 0 ? 1.f : 0.f;
	Cloudiness = WeMoveToward(Cloudiness, std::max(CloudTarget, PrecipTarget * .95f + .05f * sign), rate);
	Precipitation = WeMoveToward(Precipitation, PrecipTarget, rate * .8f);
}

// ------------------------------------------------------------------------------------------------------------ wind
void FWind::Update(double realSeconds, double gameHours, float storm, u64 seed)
{
	Time += realSeconds;
	// Direction: a slow random walk over game hours (prevailing westerly, about 70 degrees either way).
	double veer = Noise::Value(gameHours / 9, 0, seed + 1) * 1.2 + Noise::Value(gameHours / 2.5, 3, seed + 2) * .35;
	double angle = std::atan2(.6, .8) + veer;
	DirX = std::cos(angle); DirZ = std::sin(angle);
	// Speed: calm to fresh over hours, gusts over seconds; storms blow hard.
	double baseSpeed = 5.5 + 4.5 * Noise::Value(gameHours / 6, 7, seed + 3) + storm * 9;
	Gust = (float)DetMath::Clamp(.5 + .5 * Noise::Value(Time / 4, 11, seed + 4) + .3 * Noise::Value(Time / 1.3, 13, seed + 5), 0, 1);
	const double cloudSpeed = .9; // (Tom's setting in Thornwake)
	SteadySpeed = (float)(Mx(.5, baseSpeed) * cloudSpeed);
	Speed = SteadySpeed * (0.85f + .35f * Gust);
}

const char* FWind::Compass() const
{
	// (the direction is where it blows to; north = -Z)
	double from = std::fmod(WeDeg(std::atan2(-DirX, DirZ)) + 360, 360); // 0 = from the north
	static const char* names[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
	return names[(int)std::nearbyint(from / 45) % 8];
}
}
