// The sea's waves (ports of Thornwake Game/Rewrite/Ocean/OceanFft.cs with its four compute shaders, WaterShore.cs and
// FoamNet.cs). Engine-free.
//
// FOcean: Thornwake's "experimental 3A / 4" sea, a Tessendorf FFT ocean: three cascades (480, 59 and 7.9 m tiles,
// 256 x 256 each) of one JONSWAP wind sea plus a swell, choppy (the horizontal displacement pinches the crests), with a
// foam field born where the surface folds, which lingers, fades and drifts with the wind. Thornwake runs it on the
// graphics card; here it runs on worker threads (the spectrum is sparse: about 12,000 waves in all, so only the rows
// that hold any are transformed). Deterministic: the random amplitudes come from a fixed seed and the time is the game's
// clock, looping every Period seconds exactly, so Height() gives swimming the surface the player sees.
// Not ported: the fade between two sea states (the sea state is fixed until the weather is ported).
//
// FShore: where the sea meets the land: the bed's uphill direction and how open the coast is (per water vertex), and
// the shore wave's height on the CPU (the shader evaluates the same formulas).
//
// FFoamNet: the "foam net" texture, a seamless map of sea foam as a thick net round clear holes.
#pragma once

#include "WwWorld.h"
#include <functional>
#include <memory>

namespace ww
{
/// One moment of the sea, ready for the graphics card: per cascade the displacement (chop x, height, chop z, foam) and
/// the slopes (dHeight/dx, dHeight/dz, chop dDx/dx, chop dDz/dz), four half floats a texel, every mipmap level one
/// after another (256, 128 ... 1).
struct FOceanFrame
{
	std::vector<uint16_t> Displacement[3], Derivatives[3];
};

class FOcean
{
public:
	static constexpr int N = 256, Log2N = 8, Mips = Log2N + 1, Cascades = 3;
	/// The sea repeats every Period seconds (frequencies rounded to multiples of 2 pi / Period).
	static constexpr double Period = 600;
	static const float Sizes[Cascades];
	/// Texels of all mipmap levels before level m.
	static int MipOffset(int m) { int o = 0; for (int i = 0; i < m; i++) o += (N >> i) * (N >> i); return o; }

	/// Runs Count jobs, on as many threads as the caller likes.
	using FParallel = std::function<void(int Count, const std::function<void(int)>& Job)>;

	explicit FOcean(int seed = 77) : Seed(seed) {}

	/// The sea state (0..1) and the heading the waves run to (radians). Call before the first Update, not during one.
	void Configure(double seaState, double windAngle);
	/// The sea at the game's clock. DriftX/Z: how fast the foam drifts over the water (m/s; Thornwake: 3 % of the wind).
	std::shared_ptr<FOceanFrame> Update(double oceanTime, double driftX, double driftZ, const FParallel& parallel);

	/// The surface height (m, before the shader's depth and exposure weights) at a world position: the long waves of
	/// the first cascade, with the choppy displacement undone.
	double Height(double worldX, double worldZ, double oceanTime) const;

	double SeaState() const { return State; }
	double Choppiness() const { return Chop; }
	/// Significant wave heights (Thornwake SpectrumWaves.Configure).
	double Hs() const { return .15 + 14.1 * State * State; }
	double SwellHs() const { return .5 + 2.0 * State; }

private:
	struct FWave { int X, Y; float Kx, Kz, Nx, Nz; double Omega; float Re, Im, ReM, ImM; };
	struct FMode { double Kx, Kz, Omega, Re, Im, ReM, ImM; };
	struct FCascade
	{
		std::vector<FWave> Waves;       // sorted by row
		std::vector<int> Rows, RowStart; // the rows that hold waves; where each row's waves start (one more at the end)
		std::vector<float> RowData;     // per held row: four fields of N complex numbers
		std::vector<float> Foam, FoamNext;
	};

	void BuildH0();
	double Hash(int c, int x, int y, int salt) const;

	int Seed;
	double State = -1, Angle = 0, Chop = 1, LastTime = -1;
	bool bHaveTime = false;
	FCascade Cascade[Cascades];
	std::vector<FMode> Modes;
};

struct FShore
{
	/// Near a coast: the bed's uphill gradient (toward the shore; length = slope) and how open the sea is around
	/// (0 sheltered .. 1 open coast). A pure function of the world position.
	static void Coast(const FTerrainSampler& sampler, double x, double z, double spacing, float& gx, float& gz, float& exposure);
	/// Coast() for a sea point Depth metres deep, faded out toward 25 m (beyond: open water, flat bed).
	static void CoastAt(const FTerrainSampler& sampler, double x, double z, double depth, double spacing, float& gx, float& gz, float& exposure);
	/// The shore wave's height (the shader's shore_wave, vertical part). Lx, Lz: the shader's position.
	static double Wave(double lx, double lz, double t, double depth, double gx, double gz, double exposure, double seaState);
	/// The sea's surface above its still level: the simulated long waves with the shader's weights, plus the shore wave.
	static double Surface(const FOcean& ocean, double worldX, double worldZ, double lx, double lz, double t, double depth, double exposure, double gx, double gz);
	/// The shader's value noise (float hash, the same constants).
	static double Noise(double x, double y);
};

struct FFoamNet
{
	/// Size x Size texels, four bytes each: the coarse net, a finer net, soft patches, 255.
	static std::vector<uint8_t> Create(int size = 512, uint32_t seed = 6121);
};
}
