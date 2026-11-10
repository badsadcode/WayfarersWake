// Ports of Thornwake Game/Rewrite/Ocean/OceanFft.cs (+ ocean_spectrum, ocean_fft, ocean_assemble, ocean_mip .glsl),
// Game/Rewrite/WaterShore.cs and Game/Rewrite/FoamNet.cs.
#include "WwOcean.h"

namespace ww
{
const float FOcean::Sizes[FOcean::Cascades] = { 480.f, 59.f, 7.9f };

namespace
{
const double OcG = 9.81, OcPi = 3.14159265358979323846;
// Each cascade carries the wavelengths between its tile's long end and the next one's (no wave counted twice).
const double OcBandShortest[3] = { 12, 1.6, .25 };

struct FOcTables
{
	int Rev[FOcean::N];
	float Cos[FOcean::N / 2], Sin[FOcean::N / 2];
	FOcTables()
	{
		for (int i = 0; i < FOcean::N; i++)
		{
			int r = 0;
			for (int b = 0; b < FOcean::Log2N; b++) if (i & (1 << b)) r |= 1 << (FOcean::Log2N - 1 - b);
			Rev[i] = r;
		}
		for (int j = 0; j < FOcean::N / 2; j++) { Cos[j] = (float)std::cos(2 * OcPi * j / FOcean::N); Sin[j] = (float)std::sin(2 * OcPi * j / FOcean::N); }
	}
};
const FOcTables& OcTables() { static const FOcTables t; return t; }

/// Inverse transform of N complex numbers in place (not scaled: the sum of the waves, as Thornwake's compute shader).
void OcIfft(float* d)
{
	const FOcTables& t = OcTables();
	const int n = FOcean::N;
	for (int i = 0; i < n; i++)
	{
		int j = t.Rev[i];
		if (j > i) { std::swap(d[2 * i], d[2 * j]); std::swap(d[2 * i + 1], d[2 * j + 1]); }
	}
	for (int len = 2; len <= n; len <<= 1)
	{
		const int half = len >> 1, step = n / len;
		for (int i = 0; i < n; i += len)
			for (int j = 0; j < half; j++)
			{
				const float wr = t.Cos[j * step], wi = t.Sin[j * step];
				float* a = d + 2 * (i + j);
				float* b = d + 2 * (i + j + half);
				const float tr = wr * b[0] - wi * b[1], ti = wr * b[1] + wi * b[0];
				b[0] = a[0] - tr; b[1] = a[1] - ti;
				a[0] += tr; a[1] += ti;
			}
	}
}

uint16_t OcHalf(float f)
{
	uint32_t x; std::memcpy(&x, &f, 4);
	const uint32_t sign = (x >> 16) & 0x8000u, man = x & 0x7fffffu;
	const int e = (int)((x >> 23) & 0xff) - 127 + 15;
	if (e >= 31) return (uint16_t)(sign | 0x7bffu); // (too big: the largest half; never happens with metres of sea)
	if (e <= 0)
	{
		if (e < -10) return (uint16_t)sign;
		const uint32_t m = man | 0x800000u, shift = (uint32_t)(14 - e);
		uint32_t h = m >> shift;
		if ((m >> (shift - 1)) & 1u) h++;
		return (uint16_t)(sign | h);
	}
	uint32_t h = ((uint32_t)e << 10) | (man >> 13);
	if (man & 0x1000u) h++;
	return (uint16_t)(sign | h);
}

/// Godot's Mathf.AngleDifference.
double OcAngleDifference(double from, double to)
{
	const double tau = 2 * OcPi;
	double difference = std::fmod(to - from, tau);
	return std::fmod(2 * difference, tau) - difference;
}

/// JONSWAP (gamma 3.3) in wavenumber space with a cos^2s(dtheta/2) spread (unnormalised).
double OcEnergy(double k, double theta, double peakWavelength, double heading, double spread)
{
	double omega = std::sqrt(OcG * k), omegaPeak = std::sqrt(OcG * 2 * OcPi / peakWavelength);
	double sigma = omega <= omegaPeak ? .07 : .09;
	double r = (omega - omegaPeak) / (sigma * omegaPeak);
	double jonswap = std::pow(omega, -5) * std::exp(-1.25 * std::pow(omegaPeak / omega, 4)) * std::pow(3.3, std::exp(-.5 * r * r));
	double sk = jonswap * OcG / (2 * omega) / k; // S(omega) domega/dk, per k dk dtheta
	double d = std::pow(std::fabs(std::cos(.5 * OcAngleDifference(heading, theta))), 2 * spread);
	return sk * d * std::exp(-k * k * 1e-4); // (ripples under a few cm dropped)
}

double OcSmoothstep(double e0, double e1, double x)
{
	double u = DetMath::Clamp((x - e0) / (e1 - e0), 0, 1);
	return u * u * (3 - 2 * u);
}
}

double FOcean::Hash(int c, int x, int y, int salt) const
{
	// (C#: the three int products wrap and are sign-extended; the fourth is a long)
	const int32_t low = (int32_t)((uint32_t)c * 73856093u) ^ (int32_t)((uint32_t)x * 19349663u) ^ (int32_t)((uint32_t)y * 83492791u);
	const int64_t mix = (int64_t)low ^ ((int64_t)salt * 2654435761LL);
	u64 h = ((u64)(uint32_t)Seed * 0x9E3779B97F4A7C15ULL) ^ (u64)mix;
	h ^= h >> 33; h *= 0xff51afd7ed558ccdULL; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL; h ^= h >> 33;
	return (double)(h >> 11) * (1.0 / (double)(1ULL << 53));
}

void FOcean::Configure(double seaState, double windAngle)
{
	State = DetMath::Clamp(seaState, 0, 1); Angle = windAngle;
	Chop = .8 + .4 * State;
	BuildH0();
}

void FOcean::BuildH0()
{
	const double s = State;
	const double hs = .15 + 14.1 * s * s, swellHs = .5 + 2.0 * s; // SpectrumWaves.Configure
	const double peak = 9 + 130 * s * s;                          // SpectrumWaves.PeakWavelength
	// Directional spread cos^2s(dtheta/2): wide and confused in a storm, more orderly in a breeze; the swell narrow,
	// about 40 degrees off the wind.
	const double spreadWind = 10 - 6 * s, spreadSwell = 30;
	const double swellAngle = Angle + .7;
	std::vector<double> wind(3 * N * N, 0.0), swell(3 * N * N, 0.0);
	double sumWind = 0, sumSwell = 0;
	for (int c = 0; c < 3; c++)
	{
		double dk = 2 * OcPi / Sizes[c];
		double kMin = c == 0 ? 0 : 2 * OcPi / OcBandShortest[c - 1], kMax = 2 * OcPi / OcBandShortest[c];
		for (int y = 1; y < N; y++)
			for (int x = 1; x < N; x++)
			{
				double kx = (x - N / 2) * dk, kz = (y - N / 2) * dk, k = std::sqrt(kx * kx + kz * kz);
				if (k <= kMin || k > kMax || k < 1e-9) continue;
				double theta = std::atan2(kz, kx);
				int i = (c * N + y) * N + x;
				wind[i] = OcEnergy(k, theta, peak, Angle, spreadWind) * dk * dk;
				swell[i] = OcEnergy(k, theta, 220, swellAngle, spreadSwell) * dk * dk;
				sumWind += wind[i]; sumSwell += swell[i];
			}
	}
	// Height variance: sum 2|h0|^2 (h0 and its mirror) = (Hs/4)^2 for each part.
	double scaleWind = sumWind > 0 ? hs * hs / 16 / (2 * sumWind) : 0;
	double scaleSwell = sumSwell > 0 ? swellHs * swellHs / 16 / (2 * sumSwell) : 0;
	std::vector<double> amp(3 * N * N * 2, 0.0);
	for (int c = 0; c < 3; c++)
		for (int y = 0; y < N; y++)
			for (int x = 0; x < N; x++)
			{
				int i = (c * N + y) * N + x;
				double e = wind[i] * scaleWind + swell[i] * scaleSwell;
				if (e <= 0) continue;
				double u1 = Mx(Hash(c, x, y, 1), 1e-12), u2 = Hash(c, x, y, 2);
				double r = std::sqrt(-2 * std::log(u1));
				double a = std::sqrt(e / 2);
				amp[2 * i] = r * std::cos(2 * OcPi * u2) * a; amp[2 * i + 1] = r * std::sin(2 * OcPi * u2) * a;
			}
	Modes.clear();
	const double omegaBase = 2 * OcPi / Period;
	for (int c = 0; c < 3; c++)
	{
		FCascade& cas = Cascade[c];
		cas.Waves.clear(); cas.Rows.clear(); cas.RowStart.clear();
		const double dk = 2 * OcPi / Sizes[c];
		for (int y = 0; y < N; y++)
		{
			bool any = false;
			for (int x = 0; x < N; x++)
			{
				int i = (c * N + y) * N + x;
				int mx = (N - x) % N, my = (N - y) % N, m = (c * N + my) * N + mx; // -k
				double re = amp[2 * i], im = amp[2 * i + 1], reM = amp[2 * m], imM = -amp[2 * m + 1]; // conj(h0(-k))
				if (re == 0 && im == 0 && reM == 0 && imM == 0) continue;
				double kx = (x - N / 2) * dk, kz = (y - N / 2) * dk, kl = std::sqrt(kx * kx + kz * kz);
				if (kl <= 1e-6) continue;
				// Frequencies rounded to whole multiples of 2 pi / Period: the sea repeats exactly, so the time can wrap.
				double omega = std::floor(std::sqrt(OcG * kl) / omegaBase) * omegaBase;
				if (!any) { cas.Rows.push_back(y); cas.RowStart.push_back((int)cas.Waves.size()); any = true; }
				cas.Waves.push_back({ x, y, (float)kx, (float)kz, (float)(kx / kl), (float)(kz / kl), omega, (float)re, (float)im, (float)reM, (float)imM });
				// The CPU copy: the first cascade's waves longer than about 20 m (all the height a swimmer feels).
				if (c == 0 && std::abs(x - N / 2) <= 24 && std::abs(y - N / 2) <= 24 && (re * re + im * im + reM * reM + imM * imM) > 1e-8)
					Modes.push_back({ kx, kz, omega, (double)(float)re, (double)(float)im, (double)(float)reM, (double)(float)imM });
			}
		}
		cas.RowStart.push_back((int)cas.Waves.size());
		cas.RowData.assign(cas.Rows.size() * 4 * N * 2, 0.f);
		cas.Foam.assign(N * N, 0.f); cas.FoamNext.assign(N * N, 0.f);
	}
}

std::shared_ptr<FOceanFrame> FOcean::Update(double oceanTime, double driftX, double driftZ, const FParallel& parallel)
{
	const double t = oceanTime - std::floor(oceanTime / Period) * Period;
	const float dt = bHaveTime ? (float)DetMath::Clamp(oceanTime - LastTime, 0, .25) : 0.f;
	LastTime = oceanTime; bHaveTime = true;
	// Foam (Thornwake's "carried" foam of 3A and 4): the fold threshold and the lifetime grow with the sea state (whitecaps
	// linger and trail in a gale, a breeze makes a few short-lived caps), the ripples' cascade hardly makes any, and it
	// drifts with the wind.
	const float s = (float)State;
	const float threshold = .42f + (.67f - .42f) * s * s, fade = .5f + (.22f - .5f) * s;
	const float gains[3] = { 1.5f, .55f, .2f };
	const float chops[3] = { (float)Chop, (float)Chop, (float)Chop * .9f };
	const float shiftX = (float)driftX * dt, shiftZ = (float)driftZ * dt;
	const float keep = std::exp(-dt * fade);
	const int Slices = 16;
	std::vector<float> disp[3], deriv[3];
	const int total = MipOffset(Mips);
	for (int c = 0; c < 3; c++) { disp[c].resize((size_t)total * 4); deriv[c].resize((size_t)total * 4); }

	// 1. The spectrum now, row by row (only the rows that hold waves), each row transformed.
	parallel(Cascades * Slices, [&](int job)
	{
		FCascade& cas = Cascade[job / Slices];
		const int slice = job % Slices, rows = (int)cas.Rows.size();
		for (int ri = rows * slice / Slices; ri < rows * (slice + 1) / Slices; ri++)
		{
			float* row = cas.RowData.data() + (size_t)ri * 4 * N * 2;
			std::memset(row, 0, sizeof(float) * 4 * N * 2);
			for (int w = cas.RowStart[ri]; w < cas.RowStart[ri + 1]; w++)
			{
				const FWave& v = cas.Waves[w];
				const double phase = v.Omega * t;
				const float co = (float)std::cos(phase), si = (float)std::sin(phase);
				// h0 e^{-i w t} + conj(h0(-k)) e^{i w t}: each wave runs along its k (with the wind).
				const float hr = v.Re * co + v.Im * si + v.ReM * co - v.ImM * si;
				const float hi = -v.Re * si + v.Im * co + v.ReM * si + v.ImM * co;
				const float mr = -hi, mi = hr; // i h
				const float dxr = mr * v.Nx, dxi = mi * v.Nx, dzr = mr * v.Nz, dzi = mi * v.Nz;       // +i k/|k| h (the chop)
				const float yxr = mr * v.Kx, yxi = mi * v.Kx, yzr = mr * v.Kz, yzi = mi * v.Kz;       // i k h (the slopes)
				const float xxr = -hr * v.Kx * v.Nx, xxi = -hi * v.Kx * v.Nx;
				const float zzr = -hr * v.Kz * v.Nz, zzi = -hi * v.Kz * v.Nz;
				const float xzr = -hr * v.Kx * v.Nz, xzi = -hi * v.Kx * v.Nz;
				float* f = row + 2 * v.X;
				f[0] = dxr - dzi; f[1] = dxi + dzr;                             // Dx + i Dz
				f[2 * N] = hr - yxi; f[2 * N + 1] = hi + yxr;                   // Dy + i dDy/dx
				f[4 * N] = yzr - xxi; f[4 * N + 1] = yzi + xxr;                 // dDy/dz + i dDx/dx
				f[6 * N] = zzr - xzi; f[6 * N + 1] = zzi + xzr;                 // dDz/dz + i dDx/dz
			}
			for (int f = 0; f < 4; f++) OcIfft(row + (size_t)f * N * 2);
		}
	});

	// 2. Every column transformed, and the fields put together: displacement, slopes, and the foam (born where the
	// surface folds, carried from where this texel's water was a frame ago, fading).
	parallel(Cascades * Slices, [&](int job)
	{
		const int c = job / Slices, slice = job % Slices;
		FCascade& cas = Cascade[c];
		const int rows = (int)cas.Rows.size();
		const float lambda = chops[c], gain = gains[c];
		const float advX = shiftX * N / Sizes[c], advZ = shiftZ * N / Sizes[c];
		std::vector<float> line(4 * N * 2);
		float* outD = disp[c].data();
		float* outG = deriv[c].data();
		for (int x = N * slice / Slices; x < N * (slice + 1) / Slices; x++)
		{
			std::fill(line.begin(), line.end(), 0.f);
			for (int ri = 0; ri < rows; ri++)
			{
				const float* row = cas.RowData.data() + (size_t)ri * 4 * N * 2 + 2 * x;
				const int y = cas.Rows[ri];
				for (int f = 0; f < 4; f++) { line[(size_t)f * 2 * N + 2 * y] = row[(size_t)f * 2 * N]; line[(size_t)f * 2 * N + 2 * y + 1] = row[(size_t)f * 2 * N + 1]; }
			}
			for (int f = 0; f < 4; f++) OcIfft(line.data() + (size_t)f * N * 2);
			for (int y = 0; y < N; y++)
			{
				// The centred spectrum: (-1)^(x+y) after the transform.
				const float sgn = ((x + y) & 1) == 0 ? 1.f : -1.f;
				const float ax = line[2 * y] * sgn, ay = line[2 * y + 1] * sgn, az = line[2 * N + 2 * y] * sgn, aw = line[2 * N + 2 * y + 1] * sgn;
				const float bx = line[4 * N + 2 * y] * sgn, by = line[4 * N + 2 * y + 1] * sgn, bz = line[6 * N + 2 * y] * sgn, bw = line[6 * N + 2 * y + 1] * sgn;
				const float dxx = lambda * by, dzz = lambda * bz, dxz = lambda * bw;
				const float jacobian = (1 + dxx) * (1 + dzz) - dxz * dxz;
				const float sx = (float)x - advX, sz = (float)y - advZ;
				const float fx0 = std::floor(sx), fz0 = std::floor(sz), fx = sx - fx0, fz = sz - fz0;
				const int x0 = ((int)fx0 % N + N) % N, z0 = ((int)fz0 % N + N) % N, x1 = (x0 + 1) % N, z1 = (z0 + 1) % N;
				const float* prev = cas.Foam.data();
				const float p0 = prev[z0 * N + x0] + (prev[z0 * N + x1] - prev[z0 * N + x0]) * fx;
				const float p1 = prev[z1 * N + x0] + (prev[z1 * N + x1] - prev[z1 * N + x0]) * fx;
				const float born = std::min(std::max((threshold - jacobian) * gain, 0.f), 1.f);
				const float foam = std::max((p0 + (p1 - p0) * fz) * keep, born);
				cas.FoamNext[y * N + x] = foam;
				float* d = outD + ((size_t)y * N + x) * 4;
				float* g = outG + ((size_t)y * N + x) * 4;
				d[0] = lambda * ax; d[1] = az; d[2] = lambda * ay; d[3] = foam;
				g[0] = aw; g[1] = bx; g[2] = dxx; g[3] = dzz;
			}
		}
	});
	for (int c = 0; c < 3; c++) Cascade[c].Foam.swap(Cascade[c].FoamNext);

	// 3. The mipmap levels (2 x 2 averages: distant water samples band-limited waves instead of shimmering), as half floats.
	auto frame = std::make_shared<FOceanFrame>();
	parallel(Cascades * 2, [&](int job)
	{
		const int c = job / 2;
		std::vector<float>& a = job % 2 == 0 ? disp[c] : deriv[c];
		for (int m = 1; m < Mips; m++)
		{
			const int size = N >> m, big = size * 2;
			const float* src = a.data() + (size_t)MipOffset(m - 1) * 4;
			float* dst = a.data() + (size_t)MipOffset(m) * 4;
			for (int y = 0; y < size; y++)
				for (int x = 0; x < size; x++)
					for (int k = 0; k < 4; k++)
						dst[((size_t)y * size + x) * 4 + k] = (src[((size_t)(2 * y) * big + 2 * x) * 4 + k] + src[((size_t)(2 * y) * big + 2 * x + 1) * 4 + k]
							+ src[((size_t)(2 * y + 1) * big + 2 * x) * 4 + k] + src[((size_t)(2 * y + 1) * big + 2 * x + 1) * 4 + k]) * .25f;
		}
		std::vector<uint16_t>& out = job % 2 == 0 ? frame->Displacement[c] : frame->Derivatives[c];
		out.resize(a.size());
		for (size_t i = 0; i < a.size(); i++) out[i] = OcHalf(a[i]);
	});
	return frame;
}

double FOcean::Height(double worldX, double worldZ, double oceanTime) const
{
	const double t = oceanTime - std::floor(oceanTime / Period) * Period, chop = Chop;
	double x = worldX, z = worldZ, y = 0;
	for (int iter = 0; iter < 3; iter++)
	{
		double dx = 0, dz = 0; y = 0;
		for (const FMode& m : Modes)
		{
			// h = h0 e^{i(k.x - wt)} + conj(h0(-k)) e^{i(k.x + wt)} (the spectrum, transformed)
			double a = m.Kx * x + m.Kz * z, p1 = a - m.Omega * t, p2 = a + m.Omega * t;
			double c1 = std::cos(p1), s1 = std::sin(p1), c2 = std::cos(p2), s2 = std::sin(p2);
			double re = m.Re * c1 - m.Im * s1 + m.ReM * c2 - m.ImM * s2;
			double im = m.Re * s1 + m.Im * c1 + m.ReM * s2 + m.ImM * c2;
			y += re;
			// D = +i k/|k| h: real part -(k/|k|) Im(h)
			double kl = std::sqrt(m.Kx * m.Kx + m.Kz * m.Kz);
			dx -= m.Kx / kl * im; dz -= m.Kz / kl * im;
		}
		x = worldX - chop * dx; z = worldZ - chop * dz;
	}
	return y;
}

// ------------------------------------------------------------------------------------------------------------ shore
void FShore::Coast(const FTerrainSampler& sampler, double x, double z, double spacing, float& gx, float& gz, float& exposure)
{
	// How steeply the bed rises (over about 50 m); but the direction "toward the shore" comes from the coastline's
	// large-scale shape (the macro land field, smooth over kilometres): from the local bed it was noise on gentle shelves.
	double d = Mx(48, spacing * 3);
	double bx = (sampler.Height(x + d, z, d) - sampler.Height(x - d, z, d)) / (2 * d);
	double bz = (sampler.Height(x, z + d, d) - sampler.Height(x, z - d, d)) / (2 * d);
	double slope = std::sqrt(bx * bx + bz * bz);
	const FMacroRaster& macro = sampler.Plan->Macro;
	double lx = 0, lz = 0;
	macro.SampleGradient(FWorldPlan::LandChannel, x, z, lx, lz);
	double ll = std::sqrt(lx * lx + lz * lz);
	// Where the coast's direction is unclear (far from land, or between shores), fall back to the bed's own.
	double sure = DetMath::Smooth(ll / 1e-4 - .5);
	double dx = ll > 0 ? lx / ll : 0, dz = ll > 0 ? lz / ll : 0;
	double ox = slope > 0 ? bx / slope : 0, oz = slope > 0 ? bz / slope : 0;
	double fx = dx * sure + ox * (1 - sure), fz = dz * sure + oz * (1 - sure), fl = std::sqrt(fx * fx + fz * fz);
	gx = fl > 1e-6 ? (float)(fx / fl * slope) : 0.f; gz = fl > 1e-6 ? (float)(fz / fl * slope) : 0.f;
	static const double Radii[3] = { 600, 2000, 6000 };
	double open = 0;
	for (int a = 0; a < 8; a++)
	{
		double ang = a * OcPi / 4, cx = std::cos(ang), cz = std::sin(ang), dirOpen = 1;
		for (double r : Radii)
		{
			double land, h, m;
			macro.Sample(x + cx * r, z + cz * r, FWorldPlan::LandChannel, FWorldPlan::HeightChannel, FWorldPlan::MountainChannel, land, h, m);
			// Smoothly by how far inland the probe lands (a yes/no test switched waves on and off in triangle-sized steps).
			double w = DetMath::Smooth((land + .12) / .24);
			dirOpen *= 1 - w * (r < 1000 ? 1 : r < 3000 ? .65 : .3);
		}
		open += dirOpen;
	}
	exposure = (float)DetMath::Clamp(open / 8 / .45, 0, 1); // a straight coast (about half open) counts as fully exposed
}

void FShore::CoastAt(const FTerrainSampler& sampler, double x, double z, double depth, double spacing, float& gx, float& gz, float& exposure)
{
	if (depth >= 25) { gx = 0; gz = 0; exposure = 1; return; }
	float e;
	Coast(sampler, x, z, spacing, gx, gz, e);
	float fade = (float)DetMath::Smooth((depth - 17) / 8);
	gx = gx * (1 - fade); gz = gz * (1 - fade); exposure = e + (1 - e) * fade;
}

namespace
{
float ShFract(float v) { return v - std::floor(v); }
float ShHash(float x, float y)
{
	float p3x = ShFract(x * .1031f), p3y = ShFract(y * .1031f), p3z = ShFract(x * .1031f);
	float dd = p3x * (p3y + 33.33f) + p3y * (p3z + 33.33f) + p3z * (p3x + 33.33f);
	p3x += dd; p3y += dd; p3z += dd;
	return ShFract((p3x + p3y) * p3z);
}
}

double FShore::Noise(double x, double y)
{
	float fx0 = (float)x, fy0 = (float)y;
	float ix = std::floor(fx0), iy = std::floor(fy0), fx = fx0 - ix, fy = fy0 - iy;
	fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
	float a = ShHash(ix, iy), b = ShHash(ix + 1, iy), c = ShHash(ix, iy + 1), d = ShHash(ix + 1, iy + 1);
	return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

double FShore::Wave(double lx, double lz, double t, double depth, double gx, double gz, double exposure, double seaState)
{
	double s = std::sqrt(gx * gx + gz * gz);
	double active = OcSmoothstep(19, 9, depth) * OcSmoothstep(.003, .012, s) * (1 - OcSmoothstep(.22, .45, s));
	if (active <= 0) return 0;
	double period = 7 + 4 * seaState, omega = 2 * OcPi / period;
	const double SRef = .03; // reference bed slope: the phase depends on depth only (see the shader)
	double d = Mx(depth, .05);
	double theta = -(2 * omega * std::sqrt(d) / (SRef * std::sqrt(OcG)) + omega * t) + 3 * Noise(lx * .004, lz * .004)
		+ 1.5 * Noise(lx * .013 + 7.7, lz * .013 + 7.7) + .9 * Noise(lx * .035 + 2.3, lz * .035 + 2.3); // crest segments run ahead or lag
	double h0 = (.12 + 1.15 * seaState) * exposure * (.75 + .5 * Noise(lx * .01 + t * .02, lz * .01 + t * .02));
	// Some crest segments break sooner than others (an irregular foam line).
	double h = h0 * std::pow(10 / Mx(d, .4), .25), limit = (.62 + .2 * Noise(lx * .02 + 3.1, lz * .02 + 3.1) + .12 * Noise(lx * .06 + 9.4, lz * .06 + 9.4)) * d + .03;
	double breaking = OcSmoothstep(.55, .95, h / limit) * active;
	h = Mn(h, limit);
	double amp = .5 * h * active;
	return amp * (std::sin(theta) + .22 * breaking * std::sin(2 * theta - .6));
}

double FShore::Surface(const FOcean& ocean, double worldX, double worldZ, double lx, double lz, double t, double depth, double exposure, double gx, double gz)
{
	double y = ocean.Height(worldX, worldZ, t) * (.25 + .75 * exposure) * OcSmoothstep(.5, 14, depth);
	return y + Wave(lx, lz, t, depth, gx, gz, exposure, ocean.SeaState());
}

// --------------------------------------------------------------------------------------------------------- foam net
namespace
{
uint32_t FnHash(int x, int y, uint32_t salt)
{
	uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + salt * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}
float FnSmooth(float a, float b, float x)
{
	float t = std::min(std::max((x - a) / (b - a), 0.f), 1.f);
	return t * t * (3 - 2 * t);
}
float FnValue(float u, float v, int period, uint32_t salt)
{
	float x = u * period, y = v * period;
	int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
	float fx = x - x0, fy = y - y0;
	fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
	auto L = [&](int a, int b) { return (FnHash(((a % period) + period) % period, ((b % period) + period) % period, salt) & 65535u) / 65535.f; };
	float a0 = L(x0, y0) + (L(x0 + 1, y0) - L(x0, y0)) * fx;
	float a1 = L(x0, y0 + 1) + (L(x0 + 1, y0 + 1) - L(x0, y0 + 1)) * fx;
	return a0 + (a1 - a0) * fy;
}
float FnFbm(float u, float v, int period, int octaves, uint32_t salt)
{
	float sum = 0, amp = 1, norm = 0;
	for (int o = 0; o < octaves; o++) { sum += amp * FnValue(u, v, period << o, salt + (uint32_t)o); norm += amp; amp *= .5f; }
	return sum / norm;
}
/// Nearest and second-nearest feature distance (in cells) and a random 0..1 of the nearest cell, periodic over the unit square.
void FnWorley(float u, float v, int cells, uint32_t salt, float& f1, float& f2, float& id)
{
	float x = u * cells, y = v * cells;
	int cx = (int)std::floor(x), cy = (int)std::floor(y);
	float b1 = 9, b2 = 9; id = 0;
	for (int dy = -1; dy <= 1; dy++)
		for (int dx = -1; dx <= 1; dx++)
		{
			int ix = cx + dx, iy = cy + dy;
			int wx = ((ix % cells) + cells) % cells, wy = ((iy % cells) + cells) % cells;
			uint32_t h = FnHash(wx, wy, salt);
			float px = ix + .1f + .8f * (h & 1023u) / 1023.f, py = iy + .1f + .8f * ((h >> 10) & 1023u) / 1023.f;
			float d = std::sqrt((px - x) * (px - x) + (py - y) * (py - y));
			if (d < b1) { b2 = b1; b1 = d; id = ((h >> 20) & 1023u) / 1023.f; } else if (d < b2) b2 = d;
		}
	f1 = b1; f2 = b2;
}
/// Foam 0..1 at (u, v): round clear holes of different sizes in warped cells, the foam between them thick and fluffy,
/// small holes in the thickest parts, faint thin cracks across the holes.
float FnNet(float u, float v, int cells, uint32_t salt, float warp)
{
	float wu = u + (FnFbm(u, v, 4, 3, salt + 7) - .5f) * warp * 2;
	float wv = v + (FnFbm(u, v, 4, 3, salt + 13) - .5f) * warp * 2;
	float f1, f2, size;
	FnWorley(wu, wv, cells, salt, f1, f2, size);
	float edge = f2 - f1; // 0 on the line between two cells
	float r = .4f + .5f * size;
	float hole = (1 - FnSmooth(r * .4f, r * 1.05f, f1)) * FnSmooth(0, .3f, edge);
	float foam = 1 - hole;
	float s1, s2, ss;
	FnWorley(wu + .31f, wv + .77f, cells * 3, salt + 51, s1, s2, ss);
	float small = (1 - FnSmooth(.12f + .2f * ss, .4f + .2f * ss, s1)) * FnSmooth(.6f, 1, foam) * FnSmooth(.3f, .7f, FnFbm(u, v, 8, 2, salt + 61));
	foam *= 1 - .85f * small;
	foam *= .6f + .4f * FnSmooth(.2f, .8f, FnFbm(u, v, 32, 3, salt + 29));
	float g1, g2, gi;
	FnWorley(wu + .5f, wv + .25f, cells * 3, salt + 41, g1, g2, gi);
	float crack = (1 - FnSmooth(0, .05f, g2 - g1)) * .22f * hole;
	return std::min(std::max(std::max(foam, crack), 0.f), 1.f);
}
}

std::vector<uint8_t> FFoamNet::Create(int size, uint32_t seed)
{
	std::vector<uint8_t> data((size_t)size * size * 4);
	for (int y = 0; y < size; y++)
		for (int x = 0; x < size; x++)
		{
			float u = x / (float)size, v = y / (float)size;
			float r = FnNet(u, v, 8, seed, .11f);
			float g = FnNet(u + .37f, v + .61f, 15, seed + 101, .13f);
			float b = FnFbm(u, v, 4, 4, seed + 211);
			size_t i = ((size_t)y * size + x) * 4;
			data[i] = (uint8_t)(r * 255); data[i + 1] = (uint8_t)(g * 255); data[i + 2] = (uint8_t)(b * 255); data[i + 3] = 255;
		}
	return data;
}
}
