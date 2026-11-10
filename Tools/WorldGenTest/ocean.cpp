// Checks of the simulated sea (WwOcean): wave height against the sea state, the textures against the CPU surface that
// swimming uses, the foam field, and the time one update takes.   WorldGenTest <repo root> --ocean
#include "../../Source/WayfarersWake/WorldGen/WwOcean.h"
#include <chrono>
#include <cstdio>

static float FromHalf(uint16_t h)
{
	int e = (h >> 10) & 31, m = h & 1023;
	float v = e == 0 ? std::ldexp((float)m, -24) : std::ldexp((float)(m + 1024), e - 25);
	return (h & 0x8000) ? -v : v;
}

int OceanTest()
{
	using namespace ww;
	int failed = 0;
	FOcean ocean(77);
	ocean.Configure(.5, 1.052);
	FOcean::FParallel serial = [](int n, const std::function<void(int)>& job) { for (int i = 0; i < n; i++) job(i); };
	std::shared_ptr<FOceanFrame> frame;
	auto t0 = std::chrono::steady_clock::now();
	const int steps = 120;
	for (int i = 0; i < steps; i++) frame = ocean.Update(100 + i / 60.0, .14, .08, serial);
	double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / steps;
	const double t = 100 + (steps - 1) / 60.0;
	const int N = FOcean::N;
	// 1. Four standard deviations of the height = the significant wave height of wind sea and swell together.
	double var = 0;
	for (int c = 0; c < 3; c++)
	{
		double sum = 0, sq = 0;
		for (int i = 0; i < N * N; i++) { double y = FromHalf(frame->Displacement[c][i * 4 + 1]); sum += y; sq += y * y; }
		var += sq / (N * N) - (sum / (N * N)) * (sum / (N * N));
	}
	double hs = 4 * std::sqrt(var), want = std::sqrt(ocean.Hs() * ocean.Hs() + ocean.SwellHs() * ocean.SwellHs());
	bool ok = std::fabs(hs - want) < want * .2;
	std::printf("%s significant wave height %.2f m (the sea state asks for %.2f m)\n", ok ? "ok  " : "FAIL", hs, want); failed += !ok;
	// 2. The CPU surface (swimming) against the first cascade's texture: where a texel's water is shown.
	double err = 0, mag = 0; int count = 0;
	for (int y = 3; y < N; y += 17)
		for (int x = 5; x < N; x += 13)
		{
			const uint16_t* d = &frame->Displacement[0][(y * N + x) * 4];
			double wx = x * (double)FOcean::Sizes[0] / N + FromHalf(d[0]), wz = y * (double)FOcean::Sizes[0] / N + FromHalf(d[2]);
			double h = ocean.Height(wx, wz, t), g = FromHalf(d[1]);
			err += (h - g) * (h - g); mag += g * g; count++;
		}
	err = std::sqrt(err / count); mag = std::sqrt(mag / count);
	ok = err < mag * .3; // (the CPU copy leaves out the waves under about 20 m)
	std::printf("%s CPU surface against the texture: %.3f m apart on average, waves %.2f m (%d points)\n", ok ? "ok  " : "FAIL", err, mag, count); failed += !ok;
	// 3. Foam: some, not everywhere.
	double foam = 0, most = 0;
	for (int i = 0; i < N * N; i++) { double f = FromHalf(frame->Displacement[0][i * 4 + 3]); foam += f; most = std::max(most, f); }
	foam /= N * N;
	ok = most > .05 && foam < .5 && foam >= 0;
	std::printf("%s foam field: mean %.3f, most %.2f\n", ok ? "ok  " : "FAIL", foam, most); failed += !ok;
	// 4. The sea repeats after its period.
	double a = ocean.Height(1234.5, -987.25, 17.25), b = ocean.Height(1234.5, -987.25, 17.25 + FOcean::Period);
	ok = std::fabs(a - b) < 1e-6;
	std::printf("%s repeats after %.0f s (%.6f against %.6f m)\n", ok ? "ok  " : "FAIL", FOcean::Period, a, b); failed += !ok;
	std::vector<uint8_t> net = FFoamNet::Create(64);
	std::printf("     one update on one thread: %.2f ms; foam net sample %d %d %d\n", ms, net[0], net[1], net[2]);
	return failed;
}
