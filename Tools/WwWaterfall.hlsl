// Falling water at waterfalls: a port of Thornwake Game/Rewrite/WaterFall.gdshader. Body of a material Custom node;
// Tools/CreateAssets.py builds two materials from it (FOAM_DISC 0: the curtain, 1: the white water on the pool).
// The curtain: Uv = (metres across the fall, metres down its path from the lip), Travel = seconds from the top,
// Col = (speed / 25 m/s, near the pool, sliding on rock), ColA = edge fade. Streaks run down it, stretching and speeding
// as the water falls; it turns white with the drop, thins and breaks up at its edges, churns where it meets the pool.
// The foam disc: Uv = metres from the foot (downstream, across), ColA = 1 at the foot fading to 0 at its rim.
// Not ported: the bump relief from the strands, the back-light through white water, hiding the curtain behind the
// river above the lip (D302).
// Returns rgb = colour, a = opacity; output Rough.
struct FWwFall
{
	float hash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
	float noise(float2 p)
	{
		float2 i = floor(p), f = frac(p);
		float2 w = f * f * (3.0 - 2.0 * f);
		return lerp(lerp(hash(i), hash(i + float2(1.0, 0.0)), w.x), lerp(hash(i + float2(0.0, 1.0)), hash(i + float2(1.0, 1.0)), w.x), w.y);
	}
	float fbm(float2 p) { return 0.5 * noise(p) + 0.3 * noise(p * 2.03 + 7.1) + 0.2 * noise(p * 4.1 + 13.7); }
};
FWwFall fn;
const float3 deep_color = float3(0.16, 0.30, 0.33), white_color = float3(0.90, 0.94, 0.96);
float3 albedo;
float alpha;
#if FOAM_DISC
	// White water at the foot, churning and carried off downstream.
	float carried = fn.fbm(float2(Uv.x * 0.4 - T * 1.1, Uv.y * 0.5));
	float boil = fn.fbm(Uv * 0.45 + float2(-T * 0.5, T * 0.21));
	float fine = fn.noise(float2(Uv.x * 2.0 - T * 1.6, Uv.y * 2.3));
	float cover = ColA;
	float churn = 0.45 * carried + 0.35 * boil + 0.2 * fine;
	float white = (1.0 - smoothstep(0.42, 0.58, churn - 0.6 * cover + 0.42)) * smoothstep(0.0, 0.3, cover);
	albedo = white_color;
	alpha = clamp(white * 0.95, 0.0, 0.95);
	Rough = 0.8;
#else
	// Strands: long along the path, narrow across, at the water's speed; clumps of white falling faster over them.
	float tt = Travel - T;
	float s1 = fn.fbm(float2(Uv.x * 1.1, tt * 1.6));
	float s2 = fn.fbm(float2(Uv.x * 4.0 + 5.0, tt * 4.5));
	float clumps = fn.noise(float2(Uv.x * 0.45 + 2.0, tt * 2.2));
	float streak = 0.5 * s1 + 0.3 * s2 + 0.2 * clumps;
	// Clear and glassy at the lip, white once air is churned in (with the drop, sliding on rock), churning at the pool.
	float aeration = saturate(smoothstep(0.24, 0.52, Col.r) + Col.b * 0.5);
	float white = smoothstep(0.35, 0.6, streak * (0.4 + 0.6 * aeration) + 0.45 * aeration - 0.25 + Col.g * 0.5);
	white = max(white, Col.g * smoothstep(0.3, 0.7, s2 + 0.3));
	// Strands of white and gaps of darker, glassier water between them.
	float strands = smoothstep(0.42, 0.62, s2 * 0.6 + s1 * 0.4);
	white = saturate(white * (0.55 + 0.6 * strands));
	albedo = lerp(deep_color * 0.8, white_color, white);
	// Ragged edges: the sides fray where the strands part, moving down with the water.
	float fray = smoothstep(0.0, 0.55, ColA + 0.55 * (streak - 0.55) + 0.25 * (clumps - 0.5));
	float body = smoothstep(0.25, 0.55, streak + 0.4 * (ColA - 0.5));
	alpha = clamp((0.4 + 0.6 * max(body, white)) * fray, 0.0, 0.97);
	Rough = lerp(0.05, 0.7, white);
#endif
return float4(albedo, alpha);
