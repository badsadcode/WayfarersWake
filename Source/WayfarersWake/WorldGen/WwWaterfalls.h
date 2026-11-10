// The world's waterfalls as the water draws them (port of Thornwake Game/Rewrite/Waterfalls.cs: FallSet and the
// mesh of WaterfallView). Every stretch of river between two of its points that drops at least 3 m, steeper than
// 1 in 5. The river's sheet leaves a fall's face out; a curtain of falling water is drawn there instead (falling freely
// from the lip, sliding down the rock wherever the rock lies higher), with white water spreading on the pool at its foot.
// Engine-free and thread-safe. Not ported: spray and mist (Spray.cs).
#pragma once

#include "WwWorld.h"
#include <unordered_map>

namespace ww
{
/// One fall: the lip (where the water leaves its level) and the foot (the next river point, at the pool's level), the
/// water's level at each, the river's width at each, the direction it falls (unit, xz).
struct FFall
{
	int Reach = 0, Point = 0;
	double LipX = 0, LipZ = 0, FootX = 0, FootZ = 0, Top = 0, Bottom = 0, Width = 0, FootWidth = 0, Dx = 0, Dz = 0, Length = 0;
	double Height() const { return Top - Bottom; }
	double WidthAt(double along) const { return Width + (FootWidth - Width) * DetMath::Clamp(along / Length, 0, 1); }
};

class FFallSet
{
public:
	explicit FFallSet(const FHydrology& Rivers);
	const std::vector<FFall>& All() const { return Falls; }
	static double Margin(const FFall& f) { return Mx(3, f.Width * .25); }
	/// Whether a square of the water's sheet is part of a fall's face: near a fall with water between the pool's level
	/// and the lip's (low, high: its corners' lowest and highest).
	bool OnFace(double x, double z, double low, double high) const;
private:
	std::vector<FFall> Falls;
	std::unordered_map<i64, std::vector<int>> Grid;
};

/// A fall's curtain and foam disc, in metres from the lip across (y absolute, as the terrain's).
struct FFallModel
{
	struct FVertex { float X, Y, Z; float U, V; float Travel; float Speed, Foot, OnRock, Edge; };
	std::vector<FVertex> Curtain;   // U = metres across, V = metres down the path, Travel = seconds from the top
	std::vector<int> CurtainIndices;
	std::vector<FVertex> Foam;      // U, V = metres from the foot (downstream, across); Edge = 1 at the foot, 0 at the rim
	std::vector<int> FoamIndices;
	float LipLevel = 0, Left = 0, Right = 0;
	static FFallModel Build(const FTerrainSampler& Terrain, const FFall& f, bool bBody = true);
};
}
