// The water's vertex stage: the sea's waves move the sheet (Thornwake WaterSea.gdshaderinc, vertex()). Body of the
// Custom node wired to M_WwWater's world position offset; Tools/CreateAssets.py puts Tools/WwSea.hlsl's methods in the struct.
// Ported: the simulated sea's displacement (the mesh carries waves at least five vertex spacings long; lighting per
//   pixel does the rest), the shore waves, and the swash (the last of each wave running up and down the beach).
// Inputs: TileUv, Col (depth / 20 m, kind code), Shore (the bed's uphill gradient), Shore2 (exposure, fetch), Rel (rest
//   position relative to the camera, cm), Tile, FftX, FftZ (the tile's corner modulo each cascade's size), T (seconds),
//   D0..D2 / G0..G2 (the cascades' displacement and slope textures).
// Returns the offset in Unreal's axes, cm.
struct FWwSeaVertex
{
//#include WwSea
};
FWwSeaVertex fn;

const float sea_state = 0.5;
const float wave_hs = 0.15 + 14.1 * sea_state * sea_state, swell_hs = 0.5 + 2.0 * sea_state;
const float crest_scale = wave_hs / sqrt(wave_hs * wave_hs + swell_hs * swell_hs);
float2 local = TileUv * Tile.z;
float2 p = Tile.xy + local;
float vertex_depth = Col.r * 20.0, body = Col.g;
float sea = 1.0 - smoothstep(0.0, 0.3333, body);
float lake = smoothstep(0.3333, 0.6667, body) * (1.0 - smoothstep(0.6667, 1.0, body));
float2 bed_grad = Shore;
float exposure = Shore2.x, fetch = Shore2.y;
float3 offset = float3(0.0, 0.0, 0.0);
if (sea + lake > 0.0)
{
	// The mesh carries waves at least about five vertex spacings long.
	// (Thornwake: max(1, distance / 40) on its 1 m grid; the nearest tiles here have 2 m squares)
	float spacing = max(2.0, length(Rel) / 100.0 / 40.0);
	float3 n = float3(0.0, 1.0, 0.0); float crest = 0.0; float3 jac = float3(0.0, 0.0, 0.0);
	offset = fn.sea_waves(D0, D0Sampler, G0, D0Sampler, D1, D0Sampler, G1, D0Sampler, D2, D0Sampler, G2, D0Sampler, local, FftX, FftZ,
		float2(0.0, 0.0), float2(0.0, 0.0), vertex_depth, sea, lake, fetch, sea > 0.5 ? exposure : 1.0, 5.0 * spacing, true, sea_state, crest_scale, n, crest, jac);
	if (sea > 0.5)
	{
		float breaking, since, kw;
		offset += fn.shore_wave(p, vertex_depth, bed_grad, exposure, 5.0 * spacing, T, sea_state, n, breaking, since, kw);
		// Swash: the last of each wave runs up and down the beach (the sheet reaches under the beach), in step with the
		// shore waves.
		float swash = 1.0 - smoothstep(0.0, 2.0, vertex_depth);
		float period = 7.0 + 4.0 * sea_state;
		float run = 0.5 + 0.5 * sin(-6.28318530718 / period * T + fn.noise(p * 0.02) * 2.0);
		offset.y += swash * (0.1 + 0.5 * sea_state + 0.55 * smoothstep(0.55, 1.0, sea_state)) * lerp(0.4, 1.0, exposure) * (run * run - 0.3);
	}
}
return float3(offset.x, offset.z, offset.y) * 100.0;
