// Wayfarer's Wake world generation core: deterministic math, hashing, noise, SHA-256 and the macro raster.
// Engine-independent (standard C++ only) so it also builds as a headless parity test (Tools/WorldGenTest).
// A faithful port of Thornwake's Game/WorldGen (Hashing.cs, DetMath.cs, Noise.cs, MacroRaster.cs): every
// expression keeps the source's order of operations, so results are bit-identical to the C# generator.
// Must be compiled with precise floating point (no fast-math, no FMA contraction).
#pragma once

#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(precise, on)
#pragma fp_contract(off)
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace ww
{
using u64 = uint64_t;
using i64 = int64_t;

constexpr double Inf = std::numeric_limits<double>::infinity();
inline double NaNValue() { return std::numeric_limits<double>::quiet_NaN(); }

// .NET Math.Min / Math.Max semantics (NaN and signed zero included).
inline double Mn(double a, double b)
{
	if (a != b) { if (!std::isnan(a)) return a < b ? a : b; return a; }
	return std::signbit(a) ? a : b;
}
inline double Mx(double a, double b)
{
	if (a != b) { if (!std::isnan(a)) return b < a ? a : b; return a; }
	return std::signbit(b) ? a : b;
}
inline int MnI(int a, int b) { return a < b ? a : b; }
inline int MxI(int a, int b) { return a > b ? a : b; }
inline int ClampI(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
inline double Sq(double v) { return v * v; }

/// Runs Body(Index) for Index in [0, Count). The engine installs a task-graph version; the default is serial.
/// Bodies must be pure per index (results never depend on scheduling).
using FParallelFor = void (*)(int Count, const std::function<void(int)>& Body);
FParallelFor& ParallelForHook();
inline void ParallelFor(int Count, const std::function<void(int)>& Body) { ParallelForHook()(Count, Body); }

// ---------------------------------------------------------------------------------------------------- hashing
namespace Hashing
{
	/// SplitMix64 finalizer.
	inline u64 Mix(u64 v)
	{
		v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
		v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
		return v ^ (v >> 31);
	}
	/// Uniform double in [0, 1) from the top 53 bits.
	inline double Unit(u64 bits) { return (double)(bits >> 11) * (1.0 / 9007199254740992.0); }
	inline u64 Lattice(i64 x, i64 z, u64 seed) { return Mix(((u64)x * 0x9E3779B97F4A7C15ULL ^ (u64)z * 0xD1B54A32D192ED03ULL) ^ seed); }
	std::string FeatureId(int seed, int algorithm, const std::string& revision, const std::string& layer, i64 ownerX, i64 ownerZ, int ordinal);
	/// FNV-1a over the identity string.
	u64 FeatureSeed(int seed, int algorithm, const std::string& revision, const std::string& layer, i64 ownerX = 0, i64 ownerZ = 0, int ordinal = 0);
}

/// Seeded SplitMix64 stream.
struct FRandomStream
{
	u64 State;
	explicit FRandomStream(u64 Seed) : State(Seed) {}
	u64 Next() { State += 0x9E3779B97F4A7C15ULL; return Hashing::Mix(State); }
	double Unit() { return Hashing::Unit(Next()); }
	double Between(double Min, double Max) { return Min + (Max - Min) * Unit(); }
	int Int(int Count) { return (int)(Unit() * Count); }
};

// ---------------------------------------------------------------------------------------------------- DetMath
namespace DetMath
{
	constexpr double Pi = 3.14159265358979311600e+00;
	constexpr double TwoPi = 6.28318530717958623200e+00;
	constexpr double HalfPi = 1.57079632679489655800e+00;

	double Sin(double x);
	double Cos(double x);
	double Atan(double x);
	double Atan2(double y, double x);
	double Exp(double x);

	/// Quintic smoothstep on [0,1] (clamped).
	inline double Smooth(double t)
	{
		t = t < 0 ? 0 : t > 1 ? 1 : t;
		return t * t * t * (t * (t * 6 - 15) + 10);
	}
	inline double Clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
	inline double Lerp(double a, double b, double t) { return a + (b - a) * t; }
}

// ---------------------------------------------------------------------------------------------------- noise
namespace Noise
{
	double Value(double x, double z, u64 seed);
	double Gradient(double x, double z, u64 seed, double& dx, double& dz);
	double Gradient(double x, double z, u64 seed);
	double ErodedFbm(double x, double z, double wavelength, int octaves, double minWavelength, u64 seed, double gain = 0.5, double erosion = 0.35);
	double Fbm(double x, double z, double wavelength, int octaves, double minWavelength, u64 seed, double gain = 0.5);
	double Ridged(double x, double z, double wavelength, int octaves, double minWavelength, u64 seed, double gain = 0.5);
	inline double OctaveFade(double wavelength, double minWavelength)
	{
		return minWavelength <= 0 ? 1 : DetMath::Clamp(wavelength / minWavelength - 1, 0, 1);
	}
}

// ---------------------------------------------------------------------------------------------------- SHA-256
class FSha256
{
public:
	FSha256();
	void Append(const void* Data, size_t Bytes);
	/// Lower-case hex digest; the hasher must not be used afterwards.
	std::string FinishHex();
	static std::string HexOf(const std::string& Text);
private:
	void Block(const uint8_t* P);
	uint32_t H[8];
	uint8_t Buffer[64];
	size_t Used = 0;
	u64 Total = 0;
};

// ---------------------------------------------------------------------------------------------------- raster
struct FPoint2 { double X = 0, Z = 0; };

/// Square float grid covering the whole world, cell-centred samples, Catmull-Rom (bicubic) reconstruction.
class FMacroRaster
{
public:
	int Size = 0;
	double MinX = 0, MinZ = 0, Cell = 1;
	std::vector<std::vector<float>> Channels;

	FMacroRaster() = default;
	FMacroRaster(int InSize, double InMinX, double InMinZ, double InCell, int ChannelCount);

	std::vector<float>& Channel(int c) { return Channels[c]; }
	const std::vector<float>& Channel(int c) const { return Channels[c]; }
	i64 Bytes() const { return (i64)Channels.size() * Size * Size * (i64)sizeof(float); }
	double CellX(int i) const { return MinX + (i + .5) * Cell; }
	double CellZ(int j) const { return MinZ + (j + .5) * Cell; }

	/// Bicubic sample of up to three channels at once (pass -1 for unused channels).
	void Sample(double x, double z, int c0, int c1, int c2, double& v0, double& v1, double& v2) const;
	double Sample(int channel, double x, double z) const { double v, a, b; Sample(x, z, channel, -1, -1, v, a, b); return v; }
	/// Bicubic sample with its world-space gradient (one channel).
	double SampleGradient(int channel, double x, double z, double& ddx, double& ddz) const;

private:
	double Fast(const float* d, int k, double a0, double a1, double a2, double a3, double b0, double b1, double b2, double b3) const;
	double Gather(int channel, int i, int j, const double* wu, const double* ww) const;
};
}
