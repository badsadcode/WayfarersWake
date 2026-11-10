// Port of Thornwake Game/WorldGen: Hashing.cs, DetMath.cs, Noise.cs, MacroRaster.cs (see WwCore.h).
#include "WwCore.h"

namespace ww
{
static void SerialFor(int Count, const std::function<void(int)>& Body) { for (int i = 0; i < Count; i++) Body(i); }
FParallelFor& ParallelForHook() { static FParallelFor Hook = &SerialFor; return Hook; }

// ---------------------------------------------------------------------------------------------------- hashing
std::string Hashing::FeatureId(int seed, int algorithm, const std::string& revision, const std::string& layer, i64 ownerX, i64 ownerZ, int ordinal)
{
	return "world:" + std::to_string(seed) + ":" + std::to_string(algorithm) + ":" + revision + ":" + layer + ":" +
		std::to_string(ownerX) + ":" + std::to_string(ownerZ) + ":" + std::to_string(ordinal);
}

u64 Hashing::FeatureSeed(int seed, int algorithm, const std::string& revision, const std::string& layer, i64 ownerX, i64 ownerZ, int ordinal)
{
	u64 hash = 14695981039346656037ULL;
	for (char c : FeatureId(seed, algorithm, revision, layer, ownerX, ownerZ, ordinal))
		hash = (hash ^ (u64)(unsigned char)c) * 1099511628211ULL;
	return hash;
}

// ---------------------------------------------------------------------------------------------------- DetMath
namespace DetMath
{
static const double Pio2Hi = 1.57079632673412561417e+00, Pio2Lo = 6.07710050650619224932e-11;
static const double InvPio2 = 6.36619772367581382433e-01;

static double KernelSin(double x)
{
	const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03, S3 = -1.98412698298579493134e-04;
	const double S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
	double z = x * x;
	return x + x * z * (S1 + z * (S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)))));
}

static double KernelCos(double x)
{
	const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03, C3 = 2.48015872894767294178e-05;
	const double C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
	double z = x * x;
	double r = z * z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
	double hz = 0.5 * z, w = 1.0 - hz;
	return w + (((1.0 - w) - hz) + r);
}

double Sin(double x)
{
	double n = std::floor(x * InvPio2 + 0.5);
	double r = (x - n * Pio2Hi) - n * Pio2Lo;
	switch ((i64)n & 3)
	{
	case 0: return KernelSin(r);
	case 1: return KernelCos(r);
	case 2: return -KernelSin(r);
	default: return -KernelCos(r);
	}
}

double Cos(double x)
{
	double n = std::floor(x * InvPio2 + 0.5);
	double r = (x - n * Pio2Hi) - n * Pio2Lo;
	switch ((i64)n & 3)
	{
	case 0: return KernelCos(r);
	case 1: return -KernelSin(r);
	case 2: return -KernelCos(r);
	default: return KernelSin(r);
	}
}

static const double AtanHi[] = { 4.63647609000806093515e-01, 7.85398163397448278999e-01, 9.82793723247329054082e-01, 1.57079632679489655800e+00 };
static const double AtanLo[] = { 2.26987774529616870924e-17, 3.06161699786838301793e-17, 1.39033110312309984516e-17, 6.12323399573676603587e-17 };

double Atan(double x)
{
	if (std::isnan(x)) return x;
	double sign = x < 0 ? -1 : 1, ax = std::fabs(x);
	if (ax > 1e17) return sign * HalfPi;
	int id;
	if (ax < 0.4375) id = -1;
	else if (ax < 0.6875) { id = 0; ax = (2.0 * ax - 1.0) / (2.0 + ax); }
	else if (ax < 1.1875) { id = 1; ax = (ax - 1.0) / (ax + 1.0); }
	else if (ax < 2.4375) { id = 2; ax = (ax - 1.5) / (1.0 + 1.5 * ax); }
	else { id = 3; ax = -1.0 / ax; }
	double z = ax * ax, w = z * z;
	double s1 = z * (3.33333333333329318027e-01 + w * (1.42857142725034663711e-01 + w * (9.09088713343650656196e-02 +
		w * (6.66107313738753120669e-02 + w * (4.97687799461593236017e-02 + w * 1.62858201153657823623e-02)))));
	double s2 = w * (-1.99999999998764832476e-01 + w * (-1.11111104054623557880e-01 + w * (-7.69187620504482999495e-02 +
		w * (-5.83357013379057348645e-02 + w * -3.65315727442169155270e-02))));
	if (id < 0) return sign * (ax - ax * (s1 + s2));
	return sign * (AtanHi[id] - ((ax * (s1 + s2) - AtanLo[id]) - ax));
}

double Atan2(double y, double x)
{
	if (std::isnan(x) || std::isnan(y)) return NaNValue();
	if (x == 0) return y > 0 ? HalfPi : y < 0 ? -HalfPi : 0;
	double a = Atan(std::fabs(y / x));
	if (x > 0) return y < 0 ? -a : a;
	return y < 0 ? a - Pi : Pi - a;
}

static double BitsToDouble(i64 Bits) { double d; std::memcpy(&d, &Bits, sizeof d); return d; }

static double ScaleByPowerOfTwo(double y, int k)
{
	if (k > 1000) { y *= BitsToDouble((i64)(1000 + 1023) << 52); k -= 1000; }
	if (k < -1000) { y *= BitsToDouble((i64)(-1000 + 1023) << 52); k += 1000; }
	return y * BitsToDouble((i64)(k + 1023) << 52);
}

double Exp(double x)
{
	if (std::isnan(x)) return x;
	if (x > 709.78) return Inf;
	if (x < -745.13) return 0;
	const double Ln2Hi = 6.93147180369123816490e-01, Ln2Lo = 1.90821492927058770002e-10, InvLn2 = 1.44269504088896338700e+00;
	const double P1 = 1.66666666666666019037e-01, P2 = -2.77777777770155933842e-03, P3 = 6.61375632143793436117e-05;
	const double P4 = -1.65339022054652515390e-06, P5 = 4.13813679705723846039e-08;
	double k = std::floor(x * InvLn2 + 0.5);
	double hi = x - k * Ln2Hi, lo = k * Ln2Lo, r = hi - lo;
	double t = r * r;
	double c = r - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
	double y = 1.0 - ((lo - (r * c) / (2.0 - c)) - hi);
	return ScaleByPowerOfTwo(y, (int)k);
}
}

// ---------------------------------------------------------------------------------------------------- noise
namespace Noise
{
static inline double Fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static inline double FadeDerivative(double t) { return 30 * t * t * (t * (t - 2) + 1); }
static inline double Corner(i64 x, i64 z, u64 seed) { return Hashing::Unit(Hashing::Lattice(x, z, seed)) * 2 - 1; }

double Value(double x, double z, u64 seed)
{
	double fx = std::floor(x), fz = std::floor(z);
	i64 ix = (i64)fx, iz = (i64)fz;
	double u = Fade(x - fx), v = Fade(z - fz);
	double a = Corner(ix, iz, seed), b = Corner(ix + 1, iz, seed), c = Corner(ix, iz + 1, seed), d = Corner(ix + 1, iz + 1, seed);
	double low = a * (1 - u) + b * u, high = c * (1 - u) + d * u;
	return low * (1 - v) + high * v;
}

static inline void Grad(i64 x, i64 z, u64 seed, double& gx, double& gz)
{
	u64 h = (u64)x * 0x9E3779B97F4A7C15ULL ^ (u64)z * 0xC2B2AE3D27D4EB4FULL ^ seed;
	h = (h ^ (h >> 31)) * 0xD6E8FEB86659FD93ULL;
	h ^= h >> 32;
	gx = (double)(h >> 43) * (2.0 / 2097152.0) - 1;
	gz = (double)((h >> 22) & 0x1FFFFF) * (2.0 / 2097152.0) - 1;
}

double Gradient(double x, double z, u64 seed, double& dx, double& dz)
{
	double fx0 = std::floor(x), fz0 = std::floor(z);
	i64 ix = (i64)fx0, iz = (i64)fz0;
	double fx = x - fx0, fz = z - fz0;
	double gax, gaz, gbx, gbz, gcx, gcz, gdx, gdz;
	Grad(ix, iz, seed, gax, gaz);
	Grad(ix + 1, iz, seed, gbx, gbz);
	Grad(ix, iz + 1, seed, gcx, gcz);
	Grad(ix + 1, iz + 1, seed, gdx, gdz);
	double va = gax * fx + gaz * fz;
	double vb = gbx * (fx - 1) + gbz * fz;
	double vc = gcx * fx + gcz * (fz - 1);
	double vd = gdx * (fx - 1) + gdz * (fz - 1);
	double ux = Fade(fx), uz = Fade(fz);
	double dux = FadeDerivative(fx), duz = FadeDerivative(fz);
	double k = va - vb - vc + vd;
	double value = va + ux * (vb - va) + uz * (vc - va) + ux * uz * k;
	dx = gax + ux * (gbx - gax) + uz * (gcx - gax) + ux * uz * (gax - gbx - gcx + gdx) + dux * (uz * k + (vb - va));
	dz = gaz + ux * (gbz - gaz) + uz * (gcz - gaz) + ux * uz * (gaz - gbz - gcz + gdz) + duz * (ux * k + (vc - va));
	const double Scale = 2.2;
	dx *= Scale; dz *= Scale;
	return value * Scale;
}

double Gradient(double x, double z, u64 seed) { double dx, dz; return Gradient(x, z, seed, dx, dz); }

static inline double GeometricNorm(double gain, int octaves)
{
	double norm = 0, a = 1;
	for (int i = 0; i < octaves; i++) { norm += a * a; a *= gain; }
	return std::sqrt(norm);
}

double ErodedFbm(double x, double z, double wavelength, int octaves, double minWavelength, u64 seed, double gain, double erosion)
{
	double sum = 0, amplitude = 1, sx = 0, sz = 0;
	double frequency = 1 / wavelength;
	const double C = 0.8, S = 0.6;
	double px = x, pz = z;
	for (int i = 0; i < octaves; i++)
	{
		double fade = OctaveFade(wavelength, minWavelength);
		if (fade <= 0) break;
		double dx, dz;
		double n = Gradient(px * frequency, pz * frequency, seed + (u64)i * 0x632BE59BD9B4E019ULL, dx, dz);
		sx += dx * amplitude * fade; sz += dz * amplitude * fade;
		sum += fade * amplitude * n / (1 + erosion * (sx * sx + sz * sz));
		amplitude *= gain; frequency *= 2; wavelength *= 0.5;
		double rx = C * px - S * pz, rz = S * px + C * pz; px = rx; pz = rz;
	}
	return sum / GeometricNorm(gain, octaves);
}

double Fbm(double x, double z, double wavelength, int octaves, double minWavelength, u64 seed, double gain)
{
	double sum = 0, amplitude = 1, frequency = 1 / wavelength;
	for (int i = 0; i < octaves; i++)
	{
		double fade = OctaveFade(wavelength, minWavelength);
		if (fade <= 0) break;
		sum += fade * amplitude * Gradient(x * frequency + i * 17.17, z * frequency - i * 9.31, seed + (u64)i * 0x9E3779B97F4A7C15ULL);
		amplitude *= gain; frequency *= 2; wavelength *= 0.5;
	}
	return sum / GeometricNorm(gain, octaves);
}

double Ridged(double x, double z, double wavelength, int octaves, double minWavelength, u64 seed, double gain)
{
	double sum = 0, amplitude = 1, frequency = 1 / wavelength, weight = 1;
	const double C = 0.6, S = 0.8;
	double px = x, pz = z;
	for (int i = 0; i < octaves; i++)
	{
		double fade = OctaveFade(wavelength, minWavelength);
		if (fade <= 0) break;
		double n = 1 - std::fabs(Gradient(px * frequency, pz * frequency, seed + (u64)i * 0xD1B54A32D192ED03ULL));
		n *= n;
		sum += fade * amplitude * n * weight;
		weight = DetMath::Clamp(n * 1.6, 0, 1);
		amplitude *= gain; frequency *= 2; wavelength *= 0.5;
		double rx = C * px - S * pz, rz = S * px + C * pz; px = rx; pz = rz;
	}
	return sum / GeometricNorm(gain, octaves);
}
}

// ---------------------------------------------------------------------------------------------------- SHA-256
static const uint32_t ShaK[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

FSha256::FSha256()
{
	static const uint32_t Init[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	std::memcpy(H, Init, sizeof H);
}

static inline uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void FSha256::Block(const uint8_t* P)
{
	uint32_t W[64];
	for (int i = 0; i < 16; i++) W[i] = (uint32_t)P[i * 4] << 24 | (uint32_t)P[i * 4 + 1] << 16 | (uint32_t)P[i * 4 + 2] << 8 | (uint32_t)P[i * 4 + 3];
	for (int i = 16; i < 64; i++)
	{
		uint32_t s0 = Rotr(W[i - 15], 7) ^ Rotr(W[i - 15], 18) ^ (W[i - 15] >> 3);
		uint32_t s1 = Rotr(W[i - 2], 17) ^ Rotr(W[i - 2], 19) ^ (W[i - 2] >> 10);
		W[i] = W[i - 16] + s0 + W[i - 7] + s1;
	}
	uint32_t a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
	for (int i = 0; i < 64; i++)
	{
		uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
		uint32_t ch = (e & f) ^ (~e & g);
		uint32_t t1 = h + S1 + ch + ShaK[i] + W[i];
		uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
		uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
		uint32_t t2 = S0 + mj;
		h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
	}
	H[0] += a; H[1] += b; H[2] += c; H[3] += d; H[4] += e; H[5] += f; H[6] += g; H[7] += h;
}

void FSha256::Append(const void* Data, size_t Bytes)
{
	const uint8_t* P = (const uint8_t*)Data;
	Total += Bytes;
	while (Bytes > 0)
	{
		if (Used == 0 && Bytes >= 64) { Block(P); P += 64; Bytes -= 64; continue; }
		size_t Take = 64 - Used < Bytes ? 64 - Used : Bytes;
		std::memcpy(Buffer + Used, P, Take);
		Used += Take; P += Take; Bytes -= Take;
		if (Used == 64) { Block(Buffer); Used = 0; }
	}
}

std::string FSha256::FinishHex()
{
	u64 Bits = Total * 8;
	uint8_t Pad[72] = { 0x80 };
	size_t PadBytes = (Used < 56 ? 56 - Used : 120 - Used);
	Append(Pad, PadBytes);
	uint8_t Length[8];
	for (int i = 0; i < 8; i++) Length[i] = (uint8_t)(Bits >> (56 - i * 8));
	Append(Length, 8);
	static const char* Digits = "0123456789abcdef";
	std::string Out;
	for (int i = 0; i < 8; i++)
		for (int s = 28; s >= 0; s -= 4) Out.push_back(Digits[(H[i] >> s) & 15]);
	return Out;
}

std::string FSha256::HexOf(const std::string& Text)
{
	FSha256 Sha;
	Sha.Append(Text.data(), Text.size());
	return Sha.FinishHex();
}

// ---------------------------------------------------------------------------------------------------- raster
FMacroRaster::FMacroRaster(int InSize, double InMinX, double InMinZ, double InCell, int ChannelCount)
	: Size(InSize), MinX(InMinX), MinZ(InMinZ), Cell(InCell)
{
	Channels.resize(ChannelCount);
	for (auto& c : Channels) c.assign((size_t)Size * Size, 0.f);
}

double FMacroRaster::Fast(const float* d, int k, double a0, double a1, double a2, double a3, double b0, double b1, double b2, double b3) const
{
	int s = Size;
	double r0 = d[k] * a0 + d[k + 1] * a1 + d[k + 2] * a2 + d[k + 3] * a3; k += s;
	double r1 = d[k] * a0 + d[k + 1] * a1 + d[k + 2] * a2 + d[k + 3] * a3; k += s;
	double r2 = d[k] * a0 + d[k + 1] * a1 + d[k + 2] * a2 + d[k + 3] * a3; k += s;
	double r3 = d[k] * a0 + d[k + 1] * a1 + d[k + 2] * a2 + d[k + 3] * a3;
	return r0 * b0 + r1 * b1 + r2 * b2 + r3 * b3;
}

double FMacroRaster::Gather(int channel, int i, int j, const double* wu, const double* ww) const
{
	const float* data = Channels[channel].data();
	double sum = 0;
	int last = Size - 1;
	for (int b = 0; b < 4; b++)
	{
		int row = ClampI(j - 1 + b, 0, last) * Size;
		double s = 0;
		for (int a = 0; a < 4; a++) s += data[row + ClampI(i - 1 + a, 0, last)] * wu[a];
		sum += s * ww[b];
	}
	return sum;
}

void FMacroRaster::Sample(double x, double z, int c0, int c1, int c2, double& v0, double& v1, double& v2) const
{
	double u = (x - MinX) / Cell - .5, w = (z - MinZ) / Cell - .5;
	double fu = std::floor(u), fw = std::floor(w);
	double tu = u - fu, tw = w - fw;
	double tu2 = tu * tu, tu3 = tu2 * tu, tw2 = tw * tw, tw3 = tw2 * tw;
	double a0 = .5 * (-tu3 + 2 * tu2 - tu), a1 = .5 * (3 * tu3 - 5 * tu2 + 2), a2 = .5 * (-3 * tu3 + 4 * tu2 + tu), a3 = .5 * (tu3 - tu2);
	double b0 = .5 * (-tw3 + 2 * tw2 - tw), b1 = .5 * (3 * tw3 - 5 * tw2 + 2), b2 = .5 * (-3 * tw3 + 4 * tw2 + tw), b3 = .5 * (tw3 - tw2);
	if (fu >= 1 && fw >= 1 && fu <= Size - 3 && fw <= Size - 3)
	{
		int k = ((int)fw - 1) * Size + (int)fu - 1;
		v0 = Fast(Channels[c0].data(), k, a0, a1, a2, a3, b0, b1, b2, b3);
		v1 = c1 < 0 ? 0 : Fast(Channels[c1].data(), k, a0, a1, a2, a3, b0, b1, b2, b3);
		v2 = c2 < 0 ? 0 : Fast(Channels[c2].data(), k, a0, a1, a2, a3, b0, b1, b2, b3);
		return;
	}
	int i = (int)DetMath::Clamp(fu, -2, Size + 1), j = (int)DetMath::Clamp(fw, -2, Size + 1);
	if (fu < -2 || fu > Size + 1) { a0 = 0; a1 = 1; a2 = 0; a3 = 0; }
	if (fw < -2 || fw > Size + 1) { b0 = 0; b1 = 1; b2 = 0; b3 = 0; }
	double wu[4] = { a0, a1, a2, a3 }, ww[4] = { b0, b1, b2, b3 };
	v0 = Gather(c0, i, j, wu, ww);
	v1 = c1 < 0 ? 0 : Gather(c1, i, j, wu, ww);
	v2 = c2 < 0 ? 0 : Gather(c2, i, j, wu, ww);
}

static void CrWeights(double t, double* w)
{
	double t2 = t * t, t3 = t2 * t;
	w[0] = .5 * (-t3 + 2 * t2 - t);
	w[1] = .5 * (3 * t3 - 5 * t2 + 2);
	w[2] = .5 * (-3 * t3 + 4 * t2 + t);
	w[3] = .5 * (t3 - t2);
}

static void CrDerivatives(double t, double* w)
{
	double t2 = t * t;
	w[0] = .5 * (-3 * t2 + 4 * t - 1);
	w[1] = .5 * (9 * t2 - 10 * t);
	w[2] = .5 * (-9 * t2 + 8 * t + 1);
	w[3] = .5 * (3 * t2 - 2 * t);
}

double FMacroRaster::SampleGradient(int channel, double x, double z, double& ddx, double& ddz) const
{
	double u = (x - MinX) / Cell - .5, w = (z - MinZ) / Cell - .5;
	double fu = std::floor(u), fw = std::floor(w);
	int i = (int)DetMath::Clamp(fu, -2, Size + 1), j = (int)DetMath::Clamp(fw, -2, Size + 1);
	double tu = DetMath::Clamp(u - fu, 0, 1), tw = DetMath::Clamp(w - fw, 0, 1);
	double wu[4], ww[4], du[4], dw[4];
	CrWeights(tu, wu); CrWeights(tw, ww); CrDerivatives(tu, du); CrDerivatives(tw, dw);
	ddx = Gather(channel, i, j, du, ww) / Cell;
	ddz = Gather(channel, i, j, wu, dw) / Cell;
	return Gather(channel, i, j, wu, ww);
}
}
