// Wind on plants and tree crowns: a port of Thornwake Game/Rewrite/WindSway.gdshaderinc with the vertex parts of
// Flora.gdshader (PLANT 1) and Assets/Vegetation/Canopy.gdshader (PLANT 0). Body of a material Custom node that returns
// the world position offset (cm).
// Gusts are patches of stronger wind carried downwind at the wind's speed: the waves that sweep across a meadow.
// Plants lean with the wind's drag, more inside a gust, and swing slowly at their own rate with a faint flutter on top.
// Inputs: Origin (the plant's or tree's own place, world, wrapped, cm), T (seconds), and for plants Local (the vertex in
//   the mesh's own space, cm) and Layer (the sprite: 16-18 float, 19 and above are under water); for trees Rel (the
//   vertex relative to the tree's place, cm).
// The wind is fixed for now (Thornwake's default: 5.5 m/s from the north-west, the simulated weather's gustiness 0.55);
// the regional wind map and storms are not ported.
struct FWwWind
{
	float ws_hash(float2 p) { float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return frac((p3.x + p3.y) * p3.z); }
	float ws_noise(float2 p)
	{
		float2 i = floor(p), f = frac(p), u = f * f * (3.0 - 2.0 * f);
		return lerp(lerp(ws_hash(i), ws_hash(i + float2(1.0, 0.0)), u.x), lerp(ws_hash(i + float2(0.0, 1.0)), ws_hash(i + float2(1.0, 1.0)), u.x), u.y);
	}
	// 0..1: how much of a gust is passing over this point now (elongated across the wind, as real gust fronts).
	float wind_gust_dir(float2 xz, float2 d, float travel, float gustiness)
	{
		float2 q = float2(dot(xz, d), dot(xz, float2(-d.y, d.x)));
		q.x -= travel;
		float g = ws_noise(q * float2(1.0 / 24.0, 1.0 / 40.0)) * 0.6 + ws_noise(q * float2(1.0 / 8.0, 1.0 / 13.0) + 13.7) * 0.4;
		return smoothstep(0.3, 0.8, g) * (0.4 + 0.6 * gustiness) + (1.0 - gustiness) * 0.3;
	}
	// Offset (x, z) of a plant's tip of the given height (m), and how far it dips (y, negative).
	float3 wind_bend(float2 xz, float phase, float height, float stiffness, float t, float2 d, float s, float travel, float gustiness)
	{
		float gust = wind_gust_dir(xz, d, travel, gustiness);
		float push = s * s * (0.3 + 1.0 * gust);
		float lean = height * (1.0 - exp(-push * 0.8)) * 0.6;
		// Natural swing: slow, bigger in stronger wind and as gusts come and go.
		float swing = sin(t * 6.2832 * stiffness + phase) * height * (0.03 + 0.09 * s) * (0.5 + 0.5 * gust);
		float flutter = sin(t * 6.2832 * stiffness * 2.7 + phase * 3.1) * height * 0.012 * s;
		float2 side = float2(-d.y, d.x);
		float along = lean + swing;
		float2 off = d * along + side * (flutter + swing * 0.25);
		// The tip moves on an arc: it dips as it bends.
		float dip = -min(height * 0.5, dot(off, off) / max(2.0 * height, 0.01));
		return float3(off.x, dip, off.y);
	}
};
FWwWind fn;
const float2 wdir = normalize(float2(0.496, 0.868));
const float speed = 5.5, strength = speed / 8.0, gustiness = 0.55;
float travel = fmod(T * speed, 4096.0);
float2 base = Origin.xy / 100.0; // (x east, z south)
float3 offset = float3(0.0, 0.0, 0.0); // Thornwake's axes: x, up, z (metres)
#if PLANT
	// Height share of the vertex (the sprite is 1 m in its own space): roots stay put, tips move.
	float w = saturate(Local.z / 100.0);
	w *= w;
	float phase = fn.ws_hash(base * 7.3) * 6.2832;
	if (Layer > 18.5)
	{
		// Underwater: slow swaying, the same way across a bed (a gentle swell), each plant a little out of step.
		float s = sin(T * 0.7 + dot(base, float2(0.08, 0.05)) + phase * 0.4);
		offset = float3(s * 0.12, 0.0, cos(T * 0.55 + phase) * 0.06) * w;
	}
	else if (Layer > 15.5) offset.y = sin(T * 0.9 + phase) * 0.01; // floating: a slight bob
	else offset = fn.wind_bend(base, phase, 1.0, 0.6 + 0.25 * frac(phase * 1.7), T, wdir, strength, travel, gustiness) * w;
#else
	// The crown leans with the shared gusts and swings slowly as a whole (~0.25 Hz); leaves rustle on top.
	float h = saturate(Rel.z / 100.0 / 14.0);
	float phase = base.x * 0.13 + base.y * 0.17;
	float3 b = fn.wind_bend(base, phase, 1.0, 0.2 + 0.08 * frac(phase), T, wdir, strength, travel, gustiness);
	offset = float3(b.x, 0.0, b.z) * h * h * 2.6;
	offset += float3(sin(T * 4.3 + phase + Rel.z / 100.0 * 0.7), 0.0, cos(T * 3.7 + phase + Rel.x / 100.0 * 0.5)) * 0.025 * h * strength;
#endif
return float3(offset.x, offset.z, offset.y) * 100.0;
