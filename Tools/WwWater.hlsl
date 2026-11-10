// The water surface of M_WwWater: a first port of the inland-water parts of Thornwake's water shader
// (Game/Rewrite/WaterSea.gdshaderinc, as its river and "experimental 4" styles use it). Body of a material Custom node;
// Tools/CreateAssets.py builds the material around it.
// Ported: ripples (ripple_slope), the wind chop, river ripples carried by the current (flow_slope), bank turbulence,
//   the bed seen through the water with per-colour absorption and the water's own scatter colour, stirred-up water,
//   contact foam, rapids and streak foam through Tom's foam textures (tex_flow, tex_streaks), sun-independent
//   composition (albedo / emission split, fresnel, opacity film and edge fade).
// Not ported: the sea (simulated waves, shore waves and swash, breakers, the foam net), standing waves, rocks in the
//   current, wakes, caustics, rain rings, the shader's own sky and screen-space reflections (the engine's reflections
//   are used), sun glints, biome water tints, storms. The wind is fixed (Thornwake's default: 5.5 m/s from the north-west).
// Inputs: TileUv, Col (depth / 20 m, kind code, river share), ColA (edge fade), Flow (current, m/s), Rel (position
//   relative to the camera, cm), Tile (noise position of the tile's corner, tile size), Cells, T (seconds), Behind (unused
//   value; makes the scene behind the water available), Patches and Streaks (foam textures).
// Returns the base colour; outputs Emissive, WorldNormal, Surface (opacity, roughness, specular).
struct FWwWater
{
	float hash(float2 p) { float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return frac((p3.x + p3.y) * p3.z); }
	float2 hash2(float2 p) { return float2(hash(p), hash(p + 17.31)); }
	float noise(float2 p)
	{
		float2 i = floor(p); float2 f = frac(p); float2 u = f * f * (3.0 - 2.0 * f);
		return lerp(lerp(hash(i), hash(i + float2(1.0, 0.0)), u.x), lerp(hash(i + float2(0.0, 1.0)), hash(i + float2(1.0, 1.0)), u.x), u.y);
	}
	float2 grad2(float2 i) { float h = hash(i) * 6.28318530718; return float2(cos(h), sin(h)); }
	// Gradient noise with analytic derivatives: xy = gradient, z = value (about +-0.7).
	float3 gnoised(float2 p)
	{
		float2 i = floor(p), f = frac(p);
		float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
		float2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
		float2 ga = grad2(i), gb = grad2(i + float2(1.0, 0.0)), gc = grad2(i + float2(0.0, 1.0)), gd = grad2(i + float2(1.0, 1.0));
		float va = dot(ga, f), vb = dot(gb, f - float2(1.0, 0.0)), vc = dot(gc, f - float2(0.0, 1.0)), vd = dot(gd, f - float2(1.0, 1.0));
		float v = va + u.x * (vb - va) + u.y * (vc - va) + u.x * u.y * (va - vb - vc + vd);
		float2 g = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.x * u.y * (ga - gb - gc + gd) + du * (u.yx * (va - vb - vc + vd) + float2(vb, vc) - va);
		return float3(g, v);
	}
	// Wind ripples: six short wave trains about the wind's direction, warped, in gusty patches, plus fine detail close by.
	float2 ripple_slope(float2 p, float t, float2 wdir, float strength, float dist, float footprint)
	{
		float2 slope = float2(0.0, 0.0);
		float gust = 0.55 + 0.9 * (gnoised(p * 0.012 + wdir * t * 0.05).z + 0.5);
		for (int i = 0; i < 6; i++)
		{
			float fi = float(i);
			float lambda = 7.0 * pow(0.68, fi);
			float k = 6.28318530718 / lambda;
			float ang = (hash(float2(fi, 3.7)) - 0.5) * 2.4 + (fi - 2.5) * 0.15;
			float2 d = float2(wdir.x * cos(ang) - wdir.y * sin(ang), wdir.x * sin(ang) + wdir.y * cos(ang));
			float fade = (1.0 - smoothstep(lambda * 25.0, lambda * 80.0, dist)) * (1.0 - smoothstep(lambda * 0.2, lambda * 0.45, footprint));
			float warp = 2.2 * gnoised(p * (0.04 / lambda) + float2(fi * 7.1, fi * 3.3)).z;
			float theta = k * dot(d, p) - sqrt(9.81 * k) * t + fi * 1.7 + warp;
			float a = lambda * 0.014 * fade * (0.6 + 0.8 * hash(float2(fi, 9.1)));
			slope += d * (k * a * cos(theta));
		}
		slope *= strength * gust;
		float near_fine = (1.0 - smoothstep(10.0, 40.0, dist)) * (1.0 - smoothstep(0.08, 0.2, footprint));
		float2 fine = gnoised(p * 1.7 + wdir * t * 0.8).xy * 1.7 * 0.03 + gnoised(p * 4.1 - wdir.yx * t * 1.3 + 5.0).xy * 4.1 * 0.012;
		return slope + fine * near_fine * (0.5 + 0.5 * strength);
	}
	// River ripples carried downstream, smeared along the current, so fast water reads as streaks; glassy in slow pools.
	float2 flow_layer(float2 q, float2 dir, float stretch, float dist)
	{
		float2 s1 = gnoised(q * 0.5).xy * 0.5 * 0.35 + gnoised((q - dir * stretch) * 0.5).xy * 0.5 * 0.35;
		float2 s2 = gnoised(q * 1.4 + 3.1).xy * 1.4 * 0.12 + gnoised((q - dir * stretch * 0.6) * 1.4 + 3.1).xy * 1.4 * 0.12;
		float2 s3 = gnoised(q * 3.6 + 7.7).xy * 3.6 * 0.035 * (1.0 - smoothstep(6.0, 25.0, dist));
		return (s1 + s2) * 0.5 + s3;
	}
	float2 flow_slope(float2 p, float2 f, float t, float dist)
	{
		float speed = length(f);
		const float cycle = 2.8;
		float p0 = frac(t / cycle), p1 = frac(t / cycle + 0.5);
		float w0 = 1.0 - abs(2.0 * p0 - 1.0);
		float2 dir = speed > 0.001 ? f / speed : float2(1.0, 0.0);
		float stretch = 0.3 + smoothstep(0.2, 2.5, speed) * 2.2;
		float2 sa = flow_layer(p - f * p0 * cycle, dir, stretch, dist);
		float2 sb = flow_layer(p - f * p1 * cycle + float2(13.1, 7.7), dir, stretch, dist);
		float rough = (0.15 + 0.85 * smoothstep(0.05, 1.6, speed)) * 0.7;
		return (sa * w0 + sb * (1.0 - w0)) * rough / (1.0 + dist / 90.0);
	}
	// Noise carried by a current that varies from place to place: two copies scrolled over a short cycle and cross-faded.
	float flow_noise(float2 p, float2 f, float t, float cycle)
	{
		float p0 = frac(t / cycle), p1 = frac(t / cycle + 0.5);
		float w0 = 1.0 - abs(2.0 * p0 - 1.0);
		return lerp(noise(p - f * p1 * cycle + float2(7.3, 1.9)), noise(p - f * p0 * cycle), w0);
	}
	float2 flow_gnoise(float2 p, float2 f, float t, float cycle)
	{
		float p0 = frac(t / cycle), p1 = frac(t / cycle + 0.5);
		float w0 = 1.0 - abs(2.0 * p0 - 1.0);
		return lerp(gnoised(p - f * p1 * cycle + float2(7.3, 1.9)).xy, gnoised(p - f * p0 * cycle).xy, w0);
	}
	// A foam texture carried by the current (two copies slid over a short cycle and cross-faded, contrast restored).
	float tex_flow(Texture2D tex, SamplerState s, float2 p, float2 f, float size, float cycle, float t)
	{
		float2 q = p / size;
		float p0 = frac(t / cycle), p1 = frac(t / cycle + 0.5), w0 = 1.0 - abs(2.0 * p0 - 1.0);
		float a = tex.Sample(s, q - f / size * p0 * cycle).r, b = tex.Sample(s, q - f / size * p1 * cycle + float2(0.37, 0.61)).r;
		float m = lerp(b, a, w0);
		return saturate((m - 0.35) * rsqrt(w0 * w0 + (1.0 - w0) * (1.0 - w0)) + 0.35);
	}
	// Streaks along the current, in local frames anchored to a 16 m grid and blended between its corners.
	float tex_streaks(Texture2D tex, SamplerState s, float2 p, float2 fdir, float fs, float size, float stretch, float t)
	{
		const float CELL = 16.0;
		float2 g = p / CELL, g0 = floor(g), gf = g - g0;
		gf = gf * gf * (3.0 - 2.0 * gf);
		float2 side = float2(-fdir.y, fdir.x);
		const float cycle = 2.0;
		float p0 = frac(t / cycle), p1 = frac(t / cycle + 0.5), w0 = 1.0 - abs(2.0 * p0 - 1.0);
		float sum = 0.0;
		for (int ci = 0; ci < 4; ci++)
		{
			float2 corner = g0 + float2(float(ci & 1), float(ci >> 1));
			float wgt = ((ci & 1) == 1 ? gf.x : 1.0 - gf.x) * ((ci >> 1) == 1 ? gf.y : 1.0 - gf.y);
			float2 local = (g - corner) * CELL;
			float2 uv = float2(dot(local, side) / size, dot(local, fdir) / (size * stretch)) + hash2(corner);
			float drift = fs / (size * stretch) * cycle;
			float a = tex.SampleLevel(s, uv - float2(0.0, drift * p0), 0).r, b = tex.SampleLevel(s, uv - float2(0.0, drift * p1) + float2(0.5, 0.23), 0).r;
			sum += wgt * lerp(b, a, w0);
		}
		return saturate((sum - 0.3) * 1.3 + 0.3);
	}
};
FWwWater fn;

const float3 sea_scatter = float3(0.05, 0.22, 0.30), lake_scatter = float3(0.06, 0.20, 0.17), river_scatter = float3(0.07, 0.15, 0.12);
const float3 absorption = float3(0.55, 0.16, 0.12), foam_color = float3(0.93, 0.95, 0.95);
const float2 wdir = normalize(float2(0.496, 0.868));
const float wind_speed = 0.69, sea_state = 0.5, refraction = 0.03;

float2 p = Tile.xy + TileUv * Tile.z;
float body = Col.g, plume = Col.b, vertex_depth = Col.r * 20.0, edge_fade = ColA;
float sea = 1.0 - smoothstep(0.0, 0.3333, body);
float river = smoothstep(0.0, 0.3333, body) * (1.0 - smoothstep(0.3333, 0.6667, body));
float lake = 1.0 - sea - river, pond = smoothstep(0.6667, 1.0, body);
float2 current = Flow;
float dist = length(Rel) / 100.0;
float3 view_dir = normalize(Rel); // from the camera to the water
float graze = max(abs(view_dir.z), 0.02);
float footprint = dist * 0.0012 / graze;

// How much water lies behind this pixel along the view (the scene's depth less the water's), metres.
float2 screen_uv = SvPositionToBufferUV(Parameters.SvPosition);
float pixel_depth = Parameters.ScreenPosition.w;
float straight_path = max(0.0, CalcSceneDepth(screen_uv) - pixel_depth) / 100.0;
float contact_depth = straight_path * graze; // (the vertical part: the bed's depth below the surface here)
float px_depth = lerp(vertex_depth, contact_depth, (1.0 - smoothstep(10.0, 18.0, contact_depth)) * (1.0 - smoothstep(25.0, 55.0, dist)));

float2 slope = float2(0.0, 0.0);
float bank_foam = 0.0, plume_flow = 0.0;
float fs = length(current);
float2 fdir = fs > 0.01 ? current / fs : wdir;
if (river > 0.5)
{
	slope += fn.flow_slope(p, current, T, dist);
	// Bank turbulence: shallow margins of moving water break into small irregular ripples, now and then foam.
	float margin = (1.0 - smoothstep(0.1, 0.6, px_depth)) * smoothstep(0.15, 0.6, fs) * (1.0 - smoothstep(0.3, 1.0, footprint));
	slope += fn.flow_gnoise(p * 2.4 + 9.0, current * 0.4, T, 2.0) * 2.4 * 0.025 * margin;
	bank_foam = margin * smoothstep(0.55, 0.8, fn.flow_noise(p * 0.9, current * 0.3, T, 3.0)) * 0.55;
}
else
{
	// Small lakes stay calm; ponds calmer still. (The sea uses the same ripples until its waves are ported.)
	float lake_calm = lerp(1.0, 0.45, lake);
	float strength = (0.2 + 0.22 * wind_speed + 0.8 * sea_state) * lake_calm * lerp(1.0, 0.25, pond);
	slope += fn.ripple_slope(p, T, wdir, strength, dist, footprint);
	float chop = (0.03 + 0.35 * sea_state * sea_state + 0.05 * wind_speed) * (sea + lake * 0.35 - pond * 0.3) / (1.0 + dist / 250.0)
		* (1.0 - 0.7 * smoothstep(300.0, 1500.0, dist)) * (1.0 - smoothstep(1.5, 5.0, footprint));
	float2 side = float2(-wdir.y, wdir.x);
	float3 c1 = fn.gnoised(p * 0.07 + wdir * T * 0.25);
	float3 c2 = fn.gnoised(p * 0.17 - (wdir * 0.6 + side * 0.4) * T * 0.4 + 3.7);
	slope += (c1.xy * 0.07 + c2.xy * 0.17 * 0.6) / 0.15 * max(chop, 0.0);
	// River mouths: the river's current fans out into the lake or sea; its flow-carried ripples blend in.
	if (fs > 0.03)
	{
		plume_flow = smoothstep(0.05, 0.6, fs);
		slope += fn.flow_slope(p, current, T, dist) * plume_flow * 0.8;
	}
}
float3 n_world = normalize(float3(-slope.x, -slope.y, 1.0));
float3 to_eye = -view_dir;
// Reflection strength from a softened normal (ripple facets turned toward the horizon mirrored the sky as white streaks).
float3 n_soft = normalize(float3(-slope.x * 0.7, -slope.y * 0.7, 1.0));
float fresnel = 0.02 + 0.98 * pow(1.0 - saturate(dot(n_soft, to_eye)), 5.0);

// Behind the surface, refracted; never pull in things above the water.
float2 ruv = clamp(screen_uv + n_world.xy * refraction * saturate(straight_path * 0.5) * float2(1.0, -1.0), 0.001, 0.999);
float rpath = (CalcSceneDepth(ruv) - pixel_depth) / 100.0;
if (rpath < 0.0) { ruv = screen_uv; rpath = straight_path; }
float path = max(0.0, rpath);
float3 ground = DecodeSceneColorForMaterialNode(ruv) + Behind * 0.0;

// Transmitted light: the bed through clear shallows, absorbed with depth; the water's own colour takes over.
float3 transmittance = exp(-absorption * path * lerp(1.0, 2.2, river) * lerp(1.0, 0.8, sea));
float3 scatter = lerp(sea * sea_scatter + lake * lake_scatter + river * river_scatter, river_scatter * float3(1.15, 1.1, 0.9), plume * 0.85);
transmittance = lerp(transmittance, exp(-absorption * path * 2.2), plume * 0.6);
scatter = lerp(scatter, float3(0.02, 0.15, 0.27), sea * 0.6);
// Stirred-up shallows (rapids) are cloudier.
float stirred = clamp(river * smoothstep(1.5, 3.5, fs) * 0.4, 0.0, 0.8);
transmittance *= 1.0 - stirred * 0.5;
float murk = 1.0 - dot(transmittance, float3(0.333, 0.333, 0.333));

// ---- foam amount from every source, revealed through the drifting pattern
float contact = 1.0 - smoothstep(0.0, lerp(0.04, 0.16, sea) + 0.02 * river, contact_depth);
float amount = contact * lerp(0.45, 0.75, sea);
float churned = 0.0;
if (river > 0.5)
{
	float fs_r = fs * (0.78 + 0.44 * fn.flow_noise(p * 0.045 + 3.3, current * 0.06, T, 9.0));
	float rapids = smoothstep(1.2, 2.6, fs_r) * (0.65 + 0.35 * (1.0 - smoothstep(0.3, 1.5, vertex_depth)));
	churned = smoothstep(1.1, 2.4, fs_r) * (0.55 + 0.45 * fn.flow_noise(p * 0.35, current * 0.6, T, 3.0));
	float pulse = 0.65 + 0.35 * fn.flow_noise(p * 0.08, current * 0.05, T, 12.0);
	float patches = smoothstep(0.3, 0.7, fn.flow_noise(p * 0.22, current * 0.35, T, 5.0) * 0.7 + fn.noise(p * 0.6 + 4.2) * 0.3 + 0.15 * smoothstep(2.0, 3.5, fs));
	amount = max(amount, rapids * pulse * patches * 0.85);
	amount = max(amount, smoothstep(0.6, 2.2, fs) * 0.25); // streaks in moving water
	amount = max(amount, bank_foam * 0.6);                 // foam where the margins churn
}
float amt = saturate(amount);
// River foam from Tom's textures: the foam amount reveals them where their density exceeds what the amount leaves out.
float patches_t = fn.tex_flow(Patches, PatchesSampler, p, river > 0.5 ? current : wdir * (0.15 + 0.2 * wind_speed), 6.0, 3.0, T);
float streak_t = fn.tex_streaks(Streaks, StreaksSampler, p, fdir, fs, 5.0, 2.5, T);
float tex_d = lerp(patches_t, max(patches_t * 0.7, streak_t), smoothstep(0.8, 2.0, fs) * river);
float th = 1.0 - amt * 1.15;
float foam = smoothstep(th - 0.06, th + 0.06, tex_d) * smoothstep(0.0, 0.15, amt);
float foam_shade = 0.82 + 0.18 * tex_d;
float under_foam = max(smoothstep(th - 0.3, th, tex_d) * amt * 0.5, churned * 0.85 * river);
under_foam *= smoothstep(0.1, 0.9, edge_fade);

float3 transmitted = ground * transmittance * (1.0 - under_foam * 0.6); // milky water hides the bed
float3 milky = float3(0.55, 0.75, 0.74) * under_foam;
const float foam_opacity = 0.9;
float fo = foam * foam_opacity;
float3 foam_col = foam_color * foam_shade * lerp(float3(0.82, 0.93, 0.96), float3(1.0, 1.0, 1.0), foam_opacity);
float3 albedo = lerp(scatter * murk * (1.0 - fresnel) * 0.75 + milky * 0.45 * (1.0 - fresnel), foam_col, fo);
Emissive = transmitted * (1.0 - fresnel) * (1.0 - fo);
WorldNormal = n_world;
// The film at the water's edge thins out softly; the sheet fades over its extension under banks and beaches.
float alpha = smoothstep(0.0, 0.04, contact_depth) * smoothstep(0.1, 0.9, edge_fade);
Surface = float3(alpha, lerp(lerp(0.02, 0.08, sea_state), 0.6, fo), 0.5 * (1.0 - fo));
return albedo;
