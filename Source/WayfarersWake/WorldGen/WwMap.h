// The world map as a picture, seen from above (a port of Thornwake's Game/Rewrite/WorldPreview.cs, and more).
// Engine-free: the game shows the picture; the headless test writes it to a file.
//
// Styles. Terrain: sea by depth, land by height with hill shading from the north-west, snow on the heights, lakes and
// rivers (Thornwake's map). Biomes: the same with each biome in its map colour (Thornwake's B view). Parchment (new):
// an old chart drawn in ink on paper: a dark coastline with lines of water along it, washes of colour for lowland,
// hill, mountain and biome, sepia shading, inked rivers and lakes, and the paper's own stains and darkened edges.
//
// Where Thornwake's map reads only the coarse plan of the world (so it blurs when zoomed in), this one reads the real
// terrain once a pixel is finer than the plan's cells: zoomed in, the map shows the very ground, river banks and
// ponds one walks on.
#pragma once

#include "WwWorld.h"

namespace ww
{
enum class EMapStyle { Terrain, Biomes, Parchment };

struct FMapImage
{
	int Pixels = 0;
	/// The square of the world shown: its centre and side, metres.
	double CentreX = 0, CentreZ = 0, Span = 0;
	EMapStyle Style = EMapStyle::Terrain;
	bool bDetailed = false;        // drawn from the real terrain, not the coarse plan
	std::vector<uint8_t> Bgra;     // Pixels x Pixels x 4, rows from the north, blue first
};

class FWorldMap
{
public:
	/// A square of the world `span` metres across centred on (cx, cz); span <= 0: the whole world.
	/// Uses all but two of the machine's threads.
	static FMapImage Render(const FTerrainSampler& Sampler, int pixels, EMapStyle style, double cx = 0, double cz = 0, double span = 0);
	/// Metres a pixel must be finer than for the real terrain to be read instead of the plan.
	static constexpr double DetailBelow = 40;
};
}
