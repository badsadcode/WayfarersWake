// Checks of the synthesised sound (WwSound), and a demonstration file to listen to.
//   WorldGenTest <repo root> --sound      (writes Saved/sound-demo.wav: wind, rain, a storm with thunder, a river, steps)
#include "../../Source/WayfarersWake/WorldGen/WwSound.h"
#include <cstdio>
#include <fstream>

using namespace ww;

namespace
{
struct FLevel { double Rms = 0, Peak = 0, High = 0; };
/// Renders seconds of sound; the level of the last half (after the easing). High: the share of fast change (hiss).
FLevel Listen(FSoundscape& s, double seconds, std::vector<float>* keep = nullptr)
{
	int frames = (int)(seconds * FSoundscape::Rate);
	std::vector<float> out((size_t)frames * 2);
	s.Render(frames, out.data());
	FLevel level;
	double sum = 0, diff = 0; int n = 0;
	for (int i = frames / 2; i < frames; i++)
	{
		double v = out[2 * i];
		sum += v * v; n++;
		if (i > frames / 2) diff += (v - out[2 * i - 2]) * (v - out[2 * i - 2]);
	}
	for (float v : out) level.Peak = Mx(level.Peak, std::fabs(v));
	level.Rms = std::sqrt(sum / n);
	level.High = sum > 0 ? diff / sum : 0;
	if (keep) keep->insert(keep->end(), out.begin(), out.end());
	return level;
}
}

int SoundTest(const std::string& root)
{
	int failed = 0, checks = 0;
	auto Check = [&](bool ok, const char* what, double a = 0, double b = 0)
	{
		checks++;
		std::printf("%s %s (%.4f, %.4f)\n", ok ? "ok  " : "FAIL", what, a, b);
		failed += !ok;
	};
	{ FSoundscape s; FLevel calm = Listen(s, 4); Check(calm.Rms < 1e-4 && calm.Peak < 1e-3, "a calm dry day is silent", calm.Rms, calm.Peak); }
	FLevel light, heavy, breeze, gale, river, snow;
	{ FSoundscape s; s.Precipitation = .2f; light = Listen(s, 8); }
	{ FSoundscape s; s.Precipitation = .9f; heavy = Listen(s, 8); }
	Check(light.Rms > .003 && heavy.Rms > light.Rms * 2 && heavy.Peak <= 1.0, "rain is heard, heavy rain much louder, nothing clips", light.Rms, heavy.Rms);
	{ FSoundscape s; s.WindSpeed = 5; s.Gust = .5f; breeze = Listen(s, 8); }
	{ FSoundscape s; s.WindSpeed = 20; s.Gust = .5f; gale = Listen(s, 8); }
	Check(breeze.Rms > .0005 && gale.Rms > breeze.Rms * 3 && gale.Peak <= 1.0, "a breeze is faint, a gale loud", breeze.Rms, gale.Rms);
	Check(light.High > gale.High * 5, "rain hisses (high), wind rushes (low)", light.High, gale.High);
	{ FSoundscape s; s.Water = 1; river = Listen(s, 8); }
	Check(river.Rms > .01 && river.Peak <= 1.0, "running water close by is heard", river.Rms, river.Peak);
	{ FSoundscape s; s.Precipitation = .9f; s.bSnowing = true; snow = Listen(s, 8); }
	Check(snow.Rms < 1e-4, "snow falls silently", snow.Rms);
	// Under water the hiss goes.
	{
		FSoundscape s; s.Precipitation = .9f; s.bUnderwater = true;
		FLevel under = Listen(s, 8);
		Check(under.High < heavy.High * .5, "under water the rain is dull", under.High, heavy.High);
	}
	// Thunder: later and quieter from afar.
	{
		FSoundscape nearStrike, farStrike;
		nearStrike.Thunder(700, true, 5); farStrike.Thunder(7000, false, 5);
		std::vector<float> a, b;
		Listen(nearStrike, 26, &a); Listen(farStrike, 26, &b);
		auto First = [](const std::vector<float>& v) { for (size_t i = 0; i < v.size(); i += 2) if (std::fabs(v[i]) > .002f) return (double)(i / 2) / FSoundscape::Rate; return -1.0; };
		auto Peak = [](const std::vector<float>& v) { double p = 0; for (float x : v) p = Mx(p, std::fabs(x)); return p; };
		Check(std::fabs(First(a) - 700 / 340.0) < .3 && std::fabs(First(b) - 7000 / 340.0) < .6, "thunder arrives after the sound's travel time: 700 m, 7 km", First(a), First(b));
		Check(Peak(a) > Peak(b) * 4 && Peak(a) <= 1.0, "and is much louder close by", Peak(a), Peak(b));
	}
	// Short sounds end.
	{
		FSoundscape s; s.Blip(FSoundscape::Step); s.Blip(FSoundscape::Wood);
		std::vector<float> a;
		Listen(s, 1.5, &a);
		double early = 0, late = 0;
		for (size_t i = 0; i < a.size(); i += 2) (i / 2 < (size_t)(FSoundscape::Rate * .25) ? early : late) = Mx(i / 2 < (size_t)(FSoundscape::Rate * .25) ? early : late, std::fabs(a[i]));
		Check(early > .01 && late < 1e-4, "a step and an axe blow sound, then end", early, late);
	}
	// A file to listen to: a breeze, rain coming on, a thunderstorm with two strikes, then a river and footsteps.
	{
		FSoundscape s;
		std::vector<float> demo;
		s.WindSpeed = 6; s.Gust = .5f; Listen(s, 5, &demo);
		s.Precipitation = .35f; Listen(s, 6, &demo);
		s.Precipitation = .95f; s.WindSpeed = 17; s.Thunder(900, true, 3); Listen(s, 7, &demo);
		s.Thunder(5000, false, 9); Listen(s, 9, &demo);
		s.Precipitation = 0; s.WindSpeed = 4; s.Water = 1; Listen(s, 4, &demo);
		for (int k = 0; k < 8; k++) { s.Blip(FSoundscape::Step); Listen(s, .45, &demo); }
		s.Blip(FSoundscape::Wood); Listen(s, .6, &demo); s.Blip(FSoundscape::Wood); Listen(s, 1.5, &demo);
		std::string path = root + "/Saved/sound-demo.wav";
		std::ofstream f(path, std::ios::binary);
		unsigned int bytes = (unsigned int)demo.size() * 2, rate = FSoundscape::Rate;
		auto U32 = [&](unsigned int v) { f.write((const char*)&v, 4); };
		auto U16 = [&](unsigned short v) { f.write((const char*)&v, 2); };
		f.write("RIFF", 4); U32(36 + bytes); f.write("WAVEfmt ", 8); U32(16); U16(1); U16(2); U32(rate); U32(rate * 4); U16(4); U16(16);
		f.write("data", 4); U32(bytes);
		for (float v : demo) U16((unsigned short)(short)(DetMath::Clamp(v, -1, 1) * 32767));
		std::printf("     wrote %s (%.0f s)\n", path.c_str(), demo.size() / 2.0 / rate);
	}
	std::printf("%d checks, %d failed\n", checks, failed);
	return failed;
}
