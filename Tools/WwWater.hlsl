// The water surface of M_WwWater: a port of Thornwake's water shader (Game/Rewrite/WaterSea.gdshaderinc as its
// "experimental 4" style uses it for the sea and lakes, and its river style for rivers). Body of a material Custom node;
// Tools/CreateAssets.py builds the material around it and puts Tools/WwSea.hlsl's methods in the struct.
// Ported: the simulated sea's waves lit per pixel (WwOcean's three cascades), the shore waves with their breakers (a
//   foam band on the crest and a trail behind it), foam born on folding crests that drifts with the wind (lip, fresh foam,
//   old streaks) drawn through the foam net, milky aerated crests, light through thin crests, churning at rocky coasts,
//   the swash film; the wind sea on lakes by their size and on wide rivers; ripples (ripple_slope), the wind chop, river
//   ripples carried by the current (flow_slope), bank turbulence, river mouths (the current's ripples and the confused
//   water where waves meet it), the bed seen through the water with per-colour absorption and the water's own scatter
//   colour, stirred-up water, contact foam, rapids and streak foam through Tom's foam textures, sun-independent
//   composition (albedo / emission split, fresnel, opacity film and edge fade), rough water reflecting less.
// New here: rain rings on the water and rings from someone moving in it (Touch). Not ported: standing waves, rocks in
//   the current, wakes, caustics, the shader's own sky and screen-space
//   reflections (the engine's reflections are used, so are its sun glints), biome water tints. The wind and the sea state
//   come from the game's weather (Weather).
// Inputs: TileUv, Col (depth / 20 m, kind code, river share), ColA (edge fade), Flow (current, m/s), Shore (the bed's
//   uphill gradient), Shore2 (exposure, fetch), Rel (position relative to the camera, cm), RestRel (the same before the
//   waves moved it), Tile (noise position of the tile's corner, tile size), Cells, FftX, FftZ (the tile's corner modulo
//   each cascade's size), Debug (a test view, 0 = none), T (seconds), Behind (the scene depth behind the water, cm), SunDir (to the
//   sun), SunLight (its light on the ground), Patches and Streaks (foam textures), Net (the foam net), D0..D2 / G0..G2
//   (the cascades' displacement and slope textures).
// Returns the base colour of what covers the water (foam); outputs Emissive, WorldNormal, Surface (cover, roughness,
// specular), Scatter and Absorb (the water's scattering and absorption per cm, for the engine's single layer water).
struct FWwWater
{
//#include WwSea
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

	// The foam net at the drifted position q: (cover 0..1, structure 0..1: the net's own brightness). Coarse net about 8 m a
	// tile, fine net about 3 m, the fine one taking over in fresh, strong foam; a slow warp so no tile edge runs straight;
	// patches thick and thin over about 60 m. gx, gy: the pixel's footprint on the water (m).
	float2 foam_net_cover(Texture2D net, SamplerState ns, float2 q, float2 dn, float stretch, float amount, float2 gx, float2 gy)
	{
		float2 sd = float2(-dn.y, dn.x);
		q += (float2(noise(q * 0.07), noise(q * 0.07 + 5.7)) - 0.5) * 6.0;
		float2 qs = float2(dot(q, dn) / stretch, dot(q, sd));
		float2 sx = float2(dot(gx, dn) / stretch, dot(gx, sd)), sy = float2(dot(gy, dn) / stretch, dot(gy, sd));
		float lf = net.SampleGrad(ns, qs / 8.0, sx / 8.0, sy / 8.0).r;
		float hf = net.SampleGrad(ns, qs / 3.1 + float2(0.37, 0.71), sx / 3.1, sy / 3.1).g;
		float m = lerp(lf, hf, smoothstep(0.45, 1.0, amount) * 0.65);
		float patchy = net.SampleGrad(ns, q / 61.0 + float2(0.13, 0.29), gx / 61.0, gy / 61.0).b;
		m *= 0.6 + 0.8 * smoothstep(0.3, 0.7, patchy);         // whole patches thick or bare
		float a = min(amount, 1.0);
		float cut = 1.1 - 1.15 * a;                            // thin foam: only the thickest net shows
		float net_c = smoothstep(cut, cut + 0.25, m) * smoothstep(0.05, 0.25, a);
		// Fresh breakers stay solid white bands, holed only where the net's clear cells are.
		float core = smoothstep(0.5, 0.85, a) * smoothstep(0.15, 0.45, m);
		return float2(max(net_c, core), m);
	}
	// Foam in three layers: x = the surface foam, y = milky aerated water just under the surface, z = shading.
	float3 foam_at(Texture2D net, SamplerState ns, float2 q, float2 dn, float stretch, float amount, float2 gx, float2 gy)
	{
		if (amount <= 0.001) return float3(0.0, 0.0, 1.0);
		float2 n = foam_net_cover(net, ns, q, dn, stretch, amount, gx, gy);
		float under = smoothstep(0.2, 0.8, amount) * smoothstep(0.1, 0.5, n.y) * 0.45; // milky water around the net
		return float3(n.x * 0.95, under, 0.78 + 0.22 * n.y);
	}
	// Foam drifting with one even drift (the wind over open water): carried and pulled into streaks along it.
	float3 foam_layers(Texture2D net, SamplerState ns, float2 p, float2 drift, float t, float amount, float2 gx, float2 gy)
	{
		float speed = length(drift);
		float2 dn = speed > 1e-3 ? drift / speed : float2(1.0, 0.0);
		return foam_at(net, ns, p - drift * t, dn, 1.3 + 1.4 * smoothstep(0.1, 1.5, speed), amount, gx, gy);
	}
	// Foam carried by a current that varies from place to place (plumes): two copies scrolled over a short cycle and cross-faded.
	float3 foam_flow(Texture2D net, SamplerState ns, float2 p, float2 drift, float t, float amount, float2 gx, float2 gy)
	{
		if (amount <= 0.001) return float3(0.0, 0.0, 1.0);
		const float cycle = 7.0;
		float p0 = frac(t / cycle), p1 = frac(t / cycle + 0.5);
		float w0 = 1.0 - abs(2.0 * p0 - 1.0);
		float3 a = foam_at(net, ns, p - drift * p0 * cycle, float2(1.0, 0.0), 1.0, amount, gx, gy);
		float3 b = foam_at(net, ns, p - drift * p1 * cycle + float2(31.7, 12.3), float2(1.0, 0.0), 1.0, amount, gx, gy);
		float3 m = lerp(b, a, w0);
		// Two half-faded masks average to a grey wash: restore the contrast at mid-fade.
		m.x = lerp(m.x, saturate((m.x - 0.2) * rsqrt(w0 * w0 + (1.0 - w0) * (1.0 - w0)) + 0.2), 0.5);
		return m;
	}
};
FWwWater fn;

// (One sampler serves the six cascade textures and one the three foam textures: a shader may use 16 at most, and the
// engine needs most of them for lighting.)
const float3 sea_scatter = float3(0.05, 0.22, 0.30), lake_scatter = float3(0.06, 0.20, 0.17), river_scatter = float3(0.07, 0.15, 0.12);
const float3 absorption = float3(0.55, 0.16, 0.12), foam_color = float3(0.93, 0.95, 0.95), sss_color = float3(0.06, 0.62, 0.56);
// The weather of the moment (Weather: where the wind blows to, its strength as a share of 8 m/s, the sea state 0..1).
float2 wdir = normalize(Weather.xy + float2(1e-5, 0.0));
float wind_speed = Weather.z, sea_state = Weather.w;
float wave_hs = 0.15 + 14.1 * sea_state * sea_state, swell_hs = 0.5 + 2.0 * sea_state; // significant wave heights (m)
float crest_scale = wave_hs / sqrt(wave_hs * wave_hs + swell_hs * swell_hs);

float2 local = TileUv * Tile.z;
float2 p = Tile.xy + local;
float2 gx = ddx(local), gy = ddy(local);
float body = Col.g, plume = Col.b, vertex_depth = Col.r * 20.0, edge_fade = ColA;
float sea = 1.0 - smoothstep(0.0, 0.3333, body);
float river = smoothstep(0.0, 0.3333, body) * (1.0 - smoothstep(0.3333, 0.6667, body));
float lake = 1.0 - sea - river, pond = smoothstep(0.6667, 1.0, body);
float2 current = Flow;
float2 bed_grad = Shore;
float exposure = Shore2.x, fetch = Shore2.y;
float dist = length(Rel) / 100.0;
float3 view_dir = normalize(Rel); // from the camera to the water
float graze = max(abs(view_dir.z), 0.02);
float footprint = dist * 0.0012 / graze;

// The scene behind this pixel: how much water lies along the view (m), and the bed's depth below the water's rest level
// (so shore waves follow the real bed's contours, and the breaker's own height does not feed back into its phase).
// (Behind: how far away the scene behind the water is, from the engine's own water pass)
float pixel_depth = Parameters.ScreenPosition.w;
float behind_ratio = max(1.0, Behind / pixel_depth);
float straight_path = max(0.0, (behind_ratio - 1.0) * dist);
float bed_depth = max(0.0, (RestRel.z - Rel.z * behind_ratio) / 100.0);
float contact_depth = max(max(0.0, (Rel.z - Rel.z * behind_ratio) / 100.0), straight_path * abs(view_dir.z) * 0.5);
// Close range only: further out the bed behind is a coarse terrain tile, and the shore-wave phase is sensitive to depth.
float use_px = (1.0 - smoothstep(10.0, 18.0, bed_depth)) * (1.0 - smoothstep(25.0, 55.0, dist)) * step(behind_ratio * dist, 3000.0);
float px_depth = lerp(vertex_depth, bed_depth, use_px);

// Per-pixel wave normal from the simulated sea (smooth at every distance).
float3 n = float3(0.0, 1.0, 0.0); float crest = 0.0; float3 jac = float3(0.0, 0.0, 0.0);
float breaking = 0.0, since = 1.0, shore_k = 1.0;
float3 woff = float3(0.0, 0.0, 0.0); // the open-water displacement here: foam patterns are drawn where the water is shown
// Waves far shorter than a pixel only add sparkle: fade them by the pixel's footprint along the view.
float min_lambda = max(dist / 300.0, footprint * 2.5);
if (sea + lake - pond > 0.5)
{
	woff = fn.sea_waves(D0, D0Sampler, G0, D0Sampler, D1, D0Sampler, G1, D0Sampler, D2, D0Sampler, G2, D0Sampler, local, FftX, FftZ, gx, gy,
		vertex_depth, sea, lake, fetch, sea > 0.5 ? exposure : 1.0, min_lambda, false, sea_state, crest_scale, n, crest, jac);
	if (sea > 0.5) fn.shore_wave(p, px_depth, bed_grad, exposure, min_lambda, T, sea_state, n, breaking, since, shore_k);
}
else if (river > 0.5 && fetch > 40.0)
{
	// Wide rivers get the simulated lake chop too, by their width: normals and foam only (the river surface itself stays
	// where the current puts it).
	float wide_r = smoothstep(40.0, 250.0, fetch);
	float3 rn = float3(0.0, 1.0, 0.0); float rc = 0.0; float3 rj = float3(0.0, 0.0, 0.0);
	fn.sea_waves(D0, D0Sampler, G0, D0Sampler, D1, D0Sampler, G1, D0Sampler, D2, D0Sampler, G2, D0Sampler, local, FftX, FftZ, gx, gy,
		vertex_depth, 0.0, wide_r, fetch, 1.0, min_lambda, false, sea_state, crest_scale, rn, rc, rj);
	n += float3(rn.x, 0.0, rn.z);
	jac += rj * float3(1.0, 1.0, 0.7);
}
crest /= max(wave_hs * 0.5, 0.01); // about 1 at a typical big crest
float fold = 1.0 - (1.0 - jac.x) * (1.0 - jac.y);
float field_foam = jac.z; // the simulated foam (1 just born .. fading toward 0), drifting with the wind
float2 slope = -n.xz / max(n.y, 0.2);
float2 big_slope = slope; // the waves without the small ripples (reflection strength, below)

float bed_slope = length(bed_grad);
float rocky = sea * smoothstep(0.12, 0.35, bed_slope) * (1.0 - smoothstep(2.0, 9.0, vertex_depth)) * (0.4 + sea_state) * exposure;
float bank_foam = 0.0, plume_flow = 0.0, mouth_meet = 0.0;
float fs = length(current);
float2 fdir = fs > 0.01 ? current / fs : wdir;
if (river > 0.5)
{
	slope += fn.flow_slope(p, current, T, dist);
	// Bank turbulence: shallow margins of moving water break into small irregular ripples, now and then foam.
	float margin = (1.0 - smoothstep(0.1, 0.6, px_depth)) * smoothstep(0.15, 0.6, fs) * (1.0 - smoothstep(0.3, 1.0, footprint));
	slope += fn.flow_gnoise(p * 2.4 + 9.0, current * 0.4, T, 2.0) * 2.4 * 0.025 * margin;
	bank_foam = margin * smoothstep(0.55, 0.8, fn.flow_noise(p * 0.9, current * 0.3, T, 3.0)) * 0.55;
	// Wide rivers: wind waves too (fetch = the river's width), steeper where the wind blows against the current.
	float wide = smoothstep(25.0, 150.0, fetch);
	if (wide > 0.0)
	{
		float against = max(0.0, -dot(wdir, fdir)) * smoothstep(0.3, 1.5, fs);
		slope += fn.ripple_slope(p, T, wdir, (0.15 + 0.2 * wind_speed + 0.6 * sea_state) * wide * (1.0 + 0.5 * against), dist, footprint);
	}
}
else
{
	// Small lakes stay calm (little fetch for the wind to raise waves); big ones approach the sea.
	float lake_calm = lerp(1.0, clamp(0.3 + fetch / 4000.0, 0.3, 0.75), lake);
	float strength = (0.2 + 0.22 * wind_speed + 0.8 * sea_state) * lake_calm * lerp(1.0, 0.25, pond) + rocky * 1.5;
	slope += fn.ripple_slope(p, T, wdir, strength, dist, footprint);
	// Wind sea: mid-scale choppy waves running with the wind, fading with distance.
	float chop = (0.03 + 0.35 * sea_state * sea_state + 0.05 * wind_speed) * (sea + lake * 0.35 - pond * 0.3) / (1.0 + dist / 250.0)
		* (1.0 - 0.7 * smoothstep(300.0, 1500.0, dist)) * (1.0 - smoothstep(1.5, 5.0, footprint));
	float2 side = float2(-wdir.y, wdir.x);
	float3 c1 = fn.gnoised(p * 0.07 + wdir * T * 0.25);
	float3 c2 = fn.gnoised(p * 0.17 - (wdir * 0.6 + side * 0.4) * T * 0.4 + 3.7);
	chop *= 1.0 - 0.6 * sea; // (the simulated waves carry the sea's detail; the drawn chop only blurred it)
	slope += (c1.xy * 0.07 + c2.xy * 0.17 * 0.6) / 0.15 * max(chop, 0.0);
	// River mouths: the river's current fans out into the lake or sea and weakens; its flow-carried ripples blend in, and
	// where waves run against the outgoing current the water gets confused and choppy and breaks up.
	if (fs > 0.03)
	{
		plume_flow = smoothstep(0.05, 0.6, fs);
		slope += fn.flow_slope(p, current, T, dist) * plume_flow * 0.8;
		float2 shore_dir = bed_slope > 0.003 ? bed_grad / bed_slope : wdir;
		mouth_meet = plume_flow * max(max(0.0, -dot(wdir, fdir)) * (0.3 + 0.7 * sea_state),
			max(0.0, -dot(shore_dir, fdir)) * sea * (1.0 - smoothstep(6.0, 19.0, vertex_depth)));
		slope += fn.flow_gnoise(p * 0.6 + 2.0, current * 0.2, T, 4.0) * 0.6 * 0.09 * mouth_meet * (0.5 + sea_state) * (1.0 - smoothstep(1.0, 4.0, footprint));
	}
}
// Rain on the water: a ring spreads from every drop and dies away (Touch.x: how hard it rains). Three sizes of cell,
// each with a drop now and then; more of them at once the harder it rains. Near the camera only: farther off the rings
// are finer than a pixel and the rain shows as a duller surface.
float rain = Touch.x;
float rain_near = rain * (1.0 - smoothstep(20.0, 55.0, dist));
if (rain_near > 0.01)
{
	float2 rings = float2(0.0, 0.0);
	for (int layer = 0; layer < 3; layer++)
	{
		float cell = 0.5 + 0.3 * layer; // metres
		float2 q = p / cell + layer * 17.3;
		float2 ci = floor(q), cf = frac(q) - 0.5;
		float h0 = fn.hash(ci + layer * 7.1);
		float beat = T * (1.1 + 0.6 * h0) + h0 * 7.0;
		float life = frac(beat);                                             // 0 the drop lands .. 1 its ring is gone
		float turn = floor(beat);
		float falls = step(fn.hash(ci * 1.7 + turn * 0.37 + 3.1), rain * 1.2);  // does a drop fall in this cell this time
		float2 centre = (float2(fn.hash(ci + turn + 1.3), fn.hash(ci + turn + 2.7)) - 0.5) * 0.5;
		float2 d = (cf - centre) * cell;
		float r = length(d) + 1e-4, radius = life * 0.42;
		float ring = sin((r - radius) * 46.0) * exp(-pow((r - radius) * 11.0, 2.0)) * (1.0 - life) * (1.0 - life);
		rings += d / r * ring * falls;
	}
	slope += rings * 0.55 * rain_near / max(rain, 0.05) * (1.0 - smoothstep(0.02, 0.12, footprint));
}
// Someone moving in the water: rings spread from them (Touch.yz: where, in the same folded metres as p; Touch.w: how
// strongly, by how fast they move).
{
	float2 pd = p - Touch.yz;
	float pr = length(pd) + 1e-3;
	if (Touch.w > 0.01 && pr < 14.0)
		slope += pd / pr * sin(pr * 6.5 - T * 8.0) * exp(-pr * 0.42) * smoothstep(0.2, 0.7, pr) * Touch.w * 0.3;
}
float3 n_world = normalize(float3(-slope.x, -slope.y, 1.0));
float3 to_eye = -view_dir;
// Reflection strength from a softened normal (ripple facets turned toward the horizon mirrored the sky as white streaks).
float2 soft = lerp(big_slope, slope, 0.7);
float3 n_soft = normalize(float3(-soft.x, -soft.y, 1.0));
float fresnel = 0.02 + 0.98 * pow(1.0 - saturate(dot(n_soft, to_eye)), 5.0);

// The bed behind the surface and the light through the water are the engine's work now (its single layer water:
// refraction, absorption by depth and the water's own scattered colour); this shader gives it the coefficients.
float path = straight_path;

// Transmitted light: the bed through clear shallows, absorbed with depth; the water's own colour takes over.
float3 transmittance = exp(-absorption * path * lerp(1.0, 2.2, river) * lerp(1.0, 0.8, sea));
float3 scatter = lerp(sea * sea_scatter + lake * lake_scatter + river * river_scatter, river_scatter * float3(1.15, 1.1, 0.9), plume * 0.85);
transmittance = lerp(transmittance, exp(-absorption * path * 2.2), plume * 0.6);
// Deep saturated blue-teal open sea, greyer and darker in storms.
scatter = lerp(scatter, float3(0.02, 0.15, 0.27), sea * 0.6);
scatter = lerp(scatter, float3(0.05, 0.09, 0.10), sea * smoothstep(0.55, 1.0, sea_state) * 0.55);
// Masses: troughs darker and denser, crests lighter (the contrast that makes big waves read as moving water).
scatter *= lerp(1.0, lerp(0.55, 1.45, smoothstep(-1.0, 1.0, crest)), sea);
// Stirred-up shallows (breakers, rapids) are cloudier.
float stirred = clamp(breaking * 0.7 + rocky * 0.5 + river * smoothstep(1.5, 3.5, fs) * 0.4, 0.0, 0.8);
transmittance *= 1.0 - stirred * 0.5;
float murk = 1.0 - dot(transmittance, float3(0.333, 0.333, 0.333));

// ---- foam amount from every source, revealed through the drifting pattern
float2 drift = river > 0.5 ? current * 0.9 : lerp(wdir * (0.15 + 0.2 * wind_speed), current * 0.9, plume_flow);
float contact = 1.0 - smoothstep(0.0, lerp(0.04, 0.16, sea) + 0.02 * river, contact_depth);
float amount = contact * lerp(0.45, 0.75, sea);
float churned = 0.0;
if (sea > 0.5)
{
	// Breakers: a band on the crest that leaves a fading trail behind it.
	float along = 0.55 + 0.45 * fn.noise(p * 0.07 + float2(T * 0.03, 0.0)); // breakers come and go along the coast
	// The crest band is smooth across the phase wrap and the trail rises from 0 at the crest, so the foam never ends in a
	// hard front line. At least about 1.6 m wide on the water, tumbling: broken along and across by noise.
	float wrap = min(since, 1.0 - since);
	float bw = max(0.055, 1.6 * shore_k / 6.28318530718);
	float tumble = 0.25 + 0.75 * fn.noise(p * 0.3 + float2(T * 0.4, 0.0)) + 0.4 * (fn.noise(p * 1.1 - T * 0.6) - 0.5);
	float band = exp(-(wrap / bw) * (wrap / bw)) * tumble;
	// Trailing foam thins into lace (broken by noise) instead of a solid sheet.
	float trail = exp(-since * 7.0) * smoothstep(0.0, 0.06, since) * smoothstep(0.35, 0.75, fn.noise(p * 0.3 + float2(0.0, T * 0.1)));
	amount = max(amount, breaking * along * smoothstep(0.15, 0.5, px_depth) * (band * 0.8 + trail * 0.7));
	// Rocks and steep coasts churn.
	amount = max(amount, rocky * (0.4 + 0.5 * fn.noise(p * 0.2 + T * 0.3)));
}
else if (river > 0.5)
{
	float fs_r = fs * (0.78 + 0.44 * fn.flow_noise(p * 0.045 + 3.3, current * 0.06, T, 9.0));
	float rapids = smoothstep(1.2, 2.6, fs_r) * (0.65 + 0.35 * (1.0 - smoothstep(0.3, 1.5, vertex_depth)));
	churned = smoothstep(1.1, 2.4, fs_r) * (0.55 + 0.45 * fn.flow_noise(p * 0.35, current * 0.6, T, 3.0));
	float pulse = 0.65 + 0.35 * fn.flow_noise(p * 0.08, current * 0.05, T, 12.0);
	float patches = smoothstep(0.3, 0.7, fn.flow_noise(p * 0.22, current * 0.35, T, 5.0) * 0.7 + fn.noise(p * 0.6 + 4.2) * 0.3 + 0.15 * smoothstep(2.0, 3.5, fs));
	amount = max(amount, rapids * pulse * patches * 0.85);
	amount = max(amount, smoothstep(0.6, 2.2, fs) * 0.25); // streaks in moving water
	amount = max(amount, bank_foam * 0.6);                 // foam where the margins churn
	amount = max(amount, smoothstep(0.45, 0.9, fold) * smoothstep(0.5, 0.9, sea_state) * 0.5); // wind-sea caps on wide rivers
}
else
{
	amount = max(amount, smoothstep(0.45, 0.9, fold) * smoothstep(0.5, 0.9, sea_state) * 0.6 * lake);
}
// Where waves meet a river's outgoing current: broken, foamy patches carried out with the plume.
amount = max(amount, mouth_meet * (0.12 + 0.6 * sea_state) * smoothstep(0.4, 0.75, fn.flow_noise(p * 0.15, current * 0.15, T, 6.0)));
float amt = saturate(amount);

// Rivers: foam from Tom's textures (the foam amount reveals them where their density exceeds what the amount leaves out).
float patches_t = fn.tex_flow(Patches, PatchesSampler, p, river > 0.5 ? current : wdir * (0.15 + 0.2 * wind_speed), 6.0, 3.0, T);
float streak_t = fn.tex_streaks(Streaks, PatchesSampler, p, fdir, fs, 5.0, 2.5, T);
float tex_d = lerp(patches_t, max(patches_t * 0.7, streak_t), smoothstep(0.8, 2.0, fs) * river);
float th = 1.0 - amt * 1.15;
float foam = smoothstep(th - 0.06, th + 0.06, tex_d) * smoothstep(0.0, 0.15, amt);
float foam_shade = 0.82 + 0.18 * tex_d;
float under_foam = max(smoothstep(th - 0.3, th, tex_d) * amt * 0.5, churned * 0.85 * river);
float foam_opacity = 0.9;
if (river <= 0.5)
{
	// The sea, lakes and ponds: the foam net, drifting with the wind (carried by the current in a river's plume).
	float3 layers = plume_flow > 0.01 ? fn.foam_flow(Net, PatchesSampler, p, drift, T, amt, gx, gy) : fn.foam_layers(Net, PatchesSampler, p, drift, T, amt, gx, gy);
	foam = layers.x; under_foam = layers.y; foam_shade = layers.z; foam_opacity = 1.0;
	if (sea > 0.5)
	{
		// Open-sea foam from the simulated surface (crest forms, cap, breaks, fresh foam spreads, fragments, stretched streaks,
		// fades), drawn where the water is shown and carried by the wind: the breaking lip, bright and nearly solid, on the
		// folding crest itself; fresh foam in irregular patches the crest leaves behind; old foam in thin, fragmented
		// streaks pulled out along the wind, translucent, fading.
		float2 pq = p + woff.xz;
		float cap = smoothstep(0.32, 0.8, fold) * smoothstep(0.15, 0.5, sea_state) * smoothstep(0.25, 0.6, field_foam + 0.3);
		float fresh = smoothstep(0.35, 0.85, field_foam);
		float old = smoothstep(0.03, 0.4, field_foam) * (1.0 - fresh);
		float2 fd = wdir * (0.25 + 0.25 * wind_speed);
		float2 fq = pq - fd * T;
		float lip_amount = max(cap * 0.95, fresh * 0.8), lip_fresh = max(cap, fresh), streak_amount = old * 0.72;
		float2 lip2 = lip_amount > 0.001 ? fn.foam_net_cover(Net, PatchesSampler, fq, wdir, 1.6, max(lip_amount, lip_fresh * 0.9), gx, gy) : float2(0.0, 0.0);
		float2 streak2 = streak_amount > 0.001 ? fn.foam_net_cover(Net, PatchesSampler, fq * 0.85 + float2(37.1, 11.3), wdir, 1.6, streak_amount, gx * 0.85, gy * 0.85) : float2(0.0, 0.0);
		float lip = lip2.x;
		float top = max(lip, streak2.x * 0.85);
		float structure = lip2.x >= streak2.x * 0.85 ? lip2.y : streak2.y;
		float age_mix = saturate(lip_fresh * 1.3);
		float over = step(layers.x, top);
		foam = max(foam, top);
		// Partly see-through: the water shows between the bubbles (fresh crest foam nearly opaque, older foam thin).
		foam_opacity = lerp(foam_opacity, lerp(lerp(0.55, 0.92, age_mix), 1.0, structure * 0.35), over);
		// Bubbles under the breaking crest only.
		under_foam = max(under_foam, lip * lip_fresh * 0.6);
		// Shading inside the foam: brighter threads and thick foam, dimmer cell centres.
		foam_shade = lerp(foam_shade, lerp(0.7, 1.0, structure) * lerp(0.9, 1.0, age_mix), over);
	}
	// Whitecaps stay readable far out; the thinnest swash film carries no solid foam.
	foam = foam * (1.0 / (1.0 + dist / 1500.0) * 0.4 + 0.6) * smoothstep(0.2, 0.9, edge_fade) * smoothstep(0.01, 0.12, contact_depth);
}
// Aerated crests in rough weather: milky streaks over the wave tops (softer than foam, not masked).
float aerated = (sea + lake * 0.4) * smoothstep(0.15, 1.0, crest + 0.4 * (fn.noise(p * 0.12 - wdir * T * 0.3) - 0.5))
	* smoothstep(0.4, 0.9, sea_state) * 0.65 * (0.55 + 0.45 * fn.noise(p * 0.8 - T * 0.2)) / (1.0 + dist / 800.0) * 0.6;
under_foam = max(under_foam, aerated * 1.3);
under_foam *= smoothstep(0.1, 0.9, edge_fade);

// Rough water reflects less: from a fresh breeze up the spray-streaked surface breaks the mirror, and steep wave faces
// (breakers, crests) are churned water, so the water's own colour and the light through it carry them.
float face_r = clamp(1.0 - n_world.z, 0.0, 0.5) / 0.5;
float storm_r = smoothstep(0.3, 0.9, sea_state) * saturate(sea + lake * 0.7 + river * 0.5);
fresnel *= lerp(1.0, 0.7, storm_r);
// Light through thin crests (seas, breakers): looking toward the sun, the upper part of waves glows; steep faces more.
float3 sun = SunLight / 3.14159265;
float2 sun_flat = normalize(SunDir.xy + 1e-4);
float back_lit = pow(saturate(dot(view_dir.xy, sun_flat)), 2.0) * saturate(SunDir.z * 3.0);
float thin = saturate(smoothstep(0.15, 1.1, crest) * (sea + lake * 0.3) * min(1.0, 0.35 + wave_hs * 0.6)
	// Breakers glow just ahead of the crest: a soft, wide band that waxes and wanes along the crest.
	+ breaking * exp(-pow(min(1.0 - since, since + 0.2) / 0.24, 2.0)) * 0.45 * smoothstep(0.15, 0.75, fn.noise(p * 0.07 + float2(T * 0.04, -T * 0.03))));
float face = clamp(1.0 - n_world.z, 0.0, 0.6) / 0.6;
float3 glow = sss_color * sun * (back_lit * 2.2 + 0.3 + face * 0.5) * thin * 0.8 * 0.45;
float3 milky = float3(0.55, 0.75, 0.74) * under_foam;
float fo = foam * foam_opacity;
float3 foam_col = foam_color * foam_shade * lerp(float3(0.82, 0.93, 0.96), float3(1.0, 1.0, 1.0), foam_opacity);
// What the engine's water shading is given. On top: foam, as a covering layer (white, rough, not mirror-like). In the
// water: how strongly each colour is absorbed and scattered per centimetre of path, by the kind of water (the open sea
// clear and blue, lakes greener, rivers greener and murkier, ponds peaty), cloudier where it is stirred up or aerated.
float3 albedo = foam_col;
float cover = saturate(fo + under_foam * 0.25);
Emissive = (glow * (1.0 - fresnel) + sss_color * sun * under_foam * 0.12) * (1.0 - fo);
WorldNormal = n_world;
float kind_murk = lerp(1.0, 2.2, river) * lerp(1.0, 0.8, sea) * lerp(1.0, 3.5, pond);
float3 absorb_m = absorption * kind_murk;
float3 scatter_m = scatter * (0.1 + 0.5 * pond) + float3(0.55, 0.75, 0.74) * (stirred * 0.5 + under_foam * 1.2 + plume * 0.2);
Absorb = absorb_m * 0.01;
Scatter = scatter_m * 0.01;
float specular = 0.255 * (1.0 - fo) * lerp(1.0, 0.6, storm_r) * lerp(1.0, 0.7, face_r * sea);
// (rain dulls the mirror)
Surface = float3(cover, lerp(lerp(0.02, 0.1, sea_state) + 0.1 * rain, 0.7, fo), specular);
// Test views (-WwWaterDebug=): 1 = foam amount, foam shown, breaking; 2 = bed slope x 5, exposure, depth / 20 m;
// 3 = shore-wave phase, the simulated foam field, the fold.
if (Debug > 0.5)
{
	float3 shown = Debug < 1.5 ? float3(amt, foam, breaking) : Debug < 2.5 ? float3(bed_slope * 5.0, exposure, vertex_depth / 20.0) : float3(since, field_foam, fold);
	Emissive = shown * sun * 2.0; Surface = float3(1.0, 1.0, 0.0);
	return float3(0.0, 0.0, 0.0); // (a test view: the covering layer, lit by its emission)
}
return albedo;
