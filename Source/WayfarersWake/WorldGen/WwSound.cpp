// Ports of Thornwake Game/Rewrite/Lightning.cs (StepAmbience, Thunder) and Game/World/Soundscape.cs (Play).
#include "WwSound.h"

namespace ww
{
namespace
{
const double SoPi = 3.14159265358979323846, SoSpeedOfSound = 340;
// Tom's Thornwake settings.
const double SoWeather = 1.05, SoRain = .4, SoWind = .6, SoThunder = .6, SoMaster = 1.2, SoRainTone = 0;
}

void FSoundscape::Render(int frames, float* out)
{
	double rainTarget = bSnowing ? 0 : std::pow(DetMath::Clamp(Precipitation, 0, 1), .7) * SoWeather * SoRain;
	double windTarget = DetMath::Clamp((WindSpeed - 2) / 22, 0, 1.2) * (.6 + .5 * Gust) * SoWeather * SoWind;
	double waterTarget = DetMath::Clamp(Water, 0, 1) * .5;
	// Rain tone: soft tones low-pass the hiss (a gentler patter) and quieten the hiss and the drops.
	double tone = SoRainTone;
	double hissCut = .12 + .88 * tone, hissGain = .35 * (.55 + .45 * tone), dropGain = .25 * (.5 + .5 * tone);
	// (under cover: the open hiss and single drops mostly gone, the deeper roar from outside kept)
	hissCut *= 1 - .7 * Shelter; hissGain *= 1 - .6 * Shelter; dropGain *= 1 - .7 * Shelter;
	for (int i = 0; i < frames; i++)
	{
		RainLevel += (rainTarget - RainLevel) * .00005;
		WindLevel += (windTarget - WindLevel) * .00008;
		WaterLevel += (waterTarget - WaterLevel) * .00008;
		Muffle += ((bUnderwater ? 1.0 : 0.0) - Muffle) * .0004;
		double n = Next() * 2 - 1, n2 = Next() * 2 - 1;
		// Rain: hiss (high band) with a deeper roar as it gets heavy, and single drops ticking in light rain.
		RainLp += .55 * (n - RainLp);
		double hiss = n - RainLp; // high pass
		RainHi += hissCut * (hiss - RainHi);
		hiss = RainHi;
		RainLp2 += .06 * (n2 - RainLp2);
		double roar = RainLp2 * 2.2 * DetMath::Clamp(RainLevel * 1.5 - .3, 0, 1);
		DropTimer -= 1.0 / Rate;
		if (DropTimer <= 0 && RainLevel > .02)
		{
			DropTimer = Next() * .25 / (1 + RainLevel * 30);
			DropAmp = (float)(.3 + .7 * Next());
		}
		DropAmp *= .992f;
		double rain = RainLevel * (hiss * hissGain + roar) + DropAmp * n * dropGain * Mn(1, RainLevel * 6);
		// Wind: a low rushing band, its pitch and level rising with the speed.
		WindLp += .02 * (n2 - WindLp);
		WindLp2 += .02 * (WindLp - WindLp2);
		WindPhase += 1.0 / Rate;
		double swell = .75 + .25 * std::sin(WindPhase * .7) * std::sin(WindPhase * .23 + 1.3);
		WindBand += (.03 + .05 * WindLevel) * ((WindLp - WindLp2) * 8 - WindBand);
		double wind = WindLevel * swell * WindBand * 1.4;
		// Running water (added here): a middle band of noise that swells and ebbs, with a low wash under it.
		WaterLp += .22 * (n - WaterLp);
		WaterLp2 += .04 * (WaterLp - WaterLp2);
		WaterLow += .012 * (n2 - WaterLow);
		WaterPhase += 1.0 / Rate;
		double lap = .7 + .3 * std::sin(WaterPhase * 1.9) * std::sin(WaterPhase * .37 + .8);
		double water = WaterLevel * lap * ((WaterLp - WaterLp2) * 1.5 + WaterLow * 2.5);
		// (Thornwake plays this stream 4 dB down)
		double l = (rain + wind + water) * .63, r = (rain * .9 + n2 * RainLevel * .05 + wind * 1.05 + water * .95) * .63;
		// Thunder and short sounds, each from its own moment on.
		for (FVoice& v : Voices)
		{
			long long k = Clock - v.Start;
			if (k < 0 || k >= (long long)v.Samples.size()) continue;
			double s = v.Samples[(size_t)k] * v.Gain;
			l += s; r += s;
		}
		// Under water everything is dull and close: the highs go.
		MuffleL += .05 * (l - MuffleL); MuffleR += .05 * (r - MuffleR);
		l = l + (MuffleL * 1.6 - l) * Muffle; r = r + (MuffleR * 1.6 - r) * Muffle;
		out[2 * i] = (float)DetMath::Clamp(l * SoMaster, -1, 1);
		out[2 * i + 1] = (float)DetMath::Clamp(r * SoMaster, -1, 1);
		Clock++;
	}
	// Voices that have played out go.
	for (size_t k = Voices.size(); k-- > 0;)
		if (Clock - Voices[k].Start >= (long long)Voices[k].Samples.size()) Voices.erase(Voices.begin() + k);
}

void FSoundscape::Thunder(double near, bool ground, int seed)
{
	if (near > 30000) return;
	u64 random = (u64)seed * 0x9E3779B97F4A7C15ULL + 1;
	auto Unit = [&]() { random ^= random << 13; random ^= random >> 7; random ^= random << 17; return (double)(random >> 11) * (1.0 / 9007199254740992.0); };
	// The channel: pieces from the nearest point upward (Thornwake has the bolt's real segments; here it is a tall
	// crooked line), each heard when its sound arrives.
	double far = 0;
	std::vector<std::pair<double, double>> arrivals;
	for (int j = 0; j < 16; j++)
	{
		double d = std::sqrt(near * near + Sq(j * 160.0 * (.7 + .6 * Unit())));
		far = Mx(far, d);
		arrivals.push_back({ d, 160.0 * (j < 6 ? 1.0 : .5) * (.5 + Unit()) / Mx(150, d) });
	}
	double spread = (far - near) / SoSpeedOfSound;
	double seconds = Mn(16, spread + 2.5 + near / 3000);
	int count = (int)(seconds * Rate);
	// Envelope at 200 Hz: each piece's arrival, smeared a little, then echoes from the land.
	int bins = (int)(seconds * 200) + 1;
	std::vector<double> env(bins, 0.0), ring(bins, 0.0);
	for (const auto& a : arrivals)
	{
		int k = (int)((a.first - near) / SoSpeedOfSound * 200);
		if (k < bins) env[k] += a.second;
	}
	double decay = std::exp(-1.0 / (200 * (.25 + near / 40000)));
	double acc = 0;
	for (int k = 0; k < bins; k++) { acc = acc * decay + env[k]; ring[k] = acc; }
	std::vector<double> total = ring;
	for (int e = 0; e < 5; e++)
	{
		int lag = (int)((.4 + Unit() * 2.6) * 200);
		double g = .18 + .15 * Unit();
		for (int k = lag; k < bins; k++) total[k] += ring[k - lag] * g;
	}
	// The roll dies away: the first arrivals are the loud clap, the rest rumbles off.
	double fadeTime = 1.1 + near / 6000;
	double peak = 0;
	for (int k = 0; k < bins; k++)
	{
		total[k] *= std::exp(-k / 200.0 / fadeTime) * .8 + .2 * std::exp(-k / 200.0 / (fadeTime * 3));
		peak = Mx(peak, total[k]);
	}
	if (peak <= 0) return;
	// Air takes the highs: about 4 kHz content close by, about 150 Hz at 10 km.
	double cutoff = 70 + 3800 * std::exp(-near / 1800);
	double alpha = 1 - std::exp(-2 * SoPi * cutoff / Rate);
	double alpha2 = 1 - std::exp(-2 * SoPi * Mx(40, cutoff * .35) / Rate);
	double crack = ground ? DetMath::Clamp((1600 - near) / 1300, 0, 1) : 0;
	FVoice voice;
	voice.Samples.resize(count);
	double lp1 = 0, lp2 = 0, lp3 = 0, brown = 0, maxAbs = 1e-9;
	for (int i = 0; i < count; i++)
	{
		double t = (double)i / Rate, f = t * 200;
		int k = MnI(bins - 2, (int)f);
		double e = (total[k] + (total[k + 1] - total[k]) * (f - k)) / peak;
		double n = Unit() * 2 - 1;
		lp1 += alpha * (n - lp1); lp2 += alpha * (lp1 - lp2); // two-pole low pass
		lp3 += alpha2 * (lp2 - lp3);
		brown = brown * .995 + n * .05; // the deep body of the rumble
		double s = std::pow(Mx(0, e), .85) * (lp2 * 1.6 + lp3 * 1.2 + brown * .9);
		// The crack of a close strike: a sharp tearing burst at the start.
		if (crack > 0 && t < .35) s += n * crack * std::exp(-t / .045) * 1.6 + lp1 * crack * std::exp(-t / .12) * 1.2;
		voice.Samples[i] = (float)s;
		maxAbs = Mx(maxAbs, std::fabs(s));
	}
	// Fade in a few ms and out at the end; loudness about 1 / distance, full at about 700 m.
	double loud = DetMath::Clamp(700 / near, .02, 1.4) * SoThunder * SoWeather;
	for (int i = 0; i < count; i++)
	{
		double fade = Mn(1, i / (Rate * .004)) * Mn(1, (count - i) / (Rate * .5));
		voice.Samples[i] = (float)(voice.Samples[i] / maxAbs * .9 * fade * loud);
	}
	voice.Start = Clock + (long long)(near / SoSpeedOfSound * Rate);
	Voices.push_back(std::move(voice));
}

void FSoundscape::Blip(EBlip kind)
{
	double duration = kind == Craft ? .48 : .22;
	double frequency = kind == Hit ? 95 : kind == Wood ? 160 : kind == Stone ? 730 : kind == Craft ? 520 : kind == Step ? 70 : kind == Hurt ? 65 : 360;
	FVoice voice;
	voice.Samples.resize((size_t)(Rate * duration));
	for (size_t i = 0; i < voice.Samples.size(); i++)
	{
		double t = (double)i / Rate;
		double env = std::exp(-t * (kind == Craft ? 8 : 23));
		double signal = std::sin(t * 2 * SoPi * frequency) * (kind == Craft ? .7 : .4) + (Next() * 2 - 1) * (kind == Craft ? .03 : .45);
		voice.Samples[i] = (float)(signal * env * (8000.0 / 32767.0));
	}
	voice.Gain = kind == Step ? .126f : .447f; // (-18 dB for a step, -7 dB otherwise)
	voice.Start = Clock;
	Voices.push_back(std::move(voice));
}
}
