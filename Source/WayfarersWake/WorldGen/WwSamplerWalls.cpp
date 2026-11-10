// Port of Thornwake Game/WorldGen/TerrainSampler.Water.cs: GorgeWall (revisions 21 and 33).
#include "WwLandforms.h"

namespace ww
{
thread_local double TTalus = 0;

/// A gorge's or canyon's wall (revision 21): instead of one even smooth slope, a wall line that wanders (4 m at ~23 m,
/// 10 m at ~95 m) and three to five ledges - flat treads and steep risers, how layered rock weathers - their number
/// changing along the gorge. w: metres from the foot of the wall. Revision 33 adds the talus apron at its foot.
double FTerrainSampler::GorgeWall(double bankTop, double terrain, double w, double ramp, double gorge, double x, double z) const
{
	double lx = x - Ox, lz = z - Oz;
	u64 seed = BankSeed + 77;
	w += gorge * (4 * Noise::Fbm(lx, lz, 23, 2, 2, seed) + 10 * Noise::Fbm(lx, lz, 95, 2, 2, seed + 1));
	double talusShare = 0, talusRun = 0, height = terrain - bankTop;
	if (Style.Revision >= 33 && height > 6 && w > 0)
	{
		double fan = DetMath::Smooth(.5 + 1.6 * Noise::Fbm(lx, lz, 170, 2, 2, seed + 3));
		talusShare = gorge * (.15 + .25 * fan);
		talusRun = Mn(talusShare * height / .7, ramp * .6);
		talusShare = talusRun * .7 / height;
		if (w < talusRun)
		{
			double u = w / talusRun;
			TTalus = Mx(TTalus, gorge * (1 - .4 * DetMath::Smooth((u - .85) / .15)));
			return bankTop + height * talusShare * (.5 * u + .5 * u * u);
		}
		TTalus = Mx(TTalus, gorge * .4 * (1 - DetMath::Smooth((w - talusRun) / 6)));
		// (the wall above starts a quarter of the way into its S-curve)
		w = (.25 + .75 * (w - talusRun) / (ramp - talusRun)) * ramp;
	}
	double f = DetMath::Smooth(w / ramp);
	if (talusShare > 0) f = (f - .15625) / .84375; // (Smooth(0.25) = 0.15625: the wall's foot sits on the talus's top)
	double tiers = 3 + 2 * DetMath::Smooth(.5 + 1.2 * Noise::Fbm(lx, lz, 420, 2, 2, seed + 2));
	double q = f * tiers, k = std::floor(q), fr = q - k;
	double stepped = Mn(1, (k + DetMath::Smooth((fr - .35) / .65)) / tiers);
	f += (stepped - f) * .75 * gorge;
	return bankTop + (terrain - bankTop) * (talusShare + (1 - talusShare) * f);
}
}
