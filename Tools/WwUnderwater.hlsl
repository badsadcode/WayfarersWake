// The underwater view of M_WwUnderwater: a port of Thornwake's Game/Rewrite/Underwater.gdshader (a full-screen pass drawn
// when the camera is below a water surface). Body of a post-process material's Custom node; Tools/CreateAssets.py builds
// the material around it, and AWwSky switches it on and feeds it.
// Per pixel, the view ray either hits the ground or objects below the surface or the surface from below (a level plane
// SurfaceDepth metres above the camera). Light fades with the distance travelled in water (red first, then green) and
// with the camera's depth. Looking up, inside Snell's window (within about 49 degrees of vertical) the world above shows
// through a rippling surface; outside it the surface mirrors the dark water below. Sunlight comes in through the moving
// surface, each wave a lens: shafts of light in the water and a dancing net on the bed, from the simulated sea's own
// slopes in the sea, from drifting noise in lakes and rivers.
// Thornwake's settings as Tom plays it: underwater light "full" (32 steps), shafts leaning toward the vertical.
// Inputs: ViewUv (0..1 across the view), Ray (from the pixel to the camera), T (seconds),
//   SurfaceDepth (m), WaterColor, Density (per m), UseFft, CamFft1 / CamFft2 (the camera's place modulo the 59 m and 7.9 m cascades),
//   CamNoise (the camera's place, folded), SunDir (to the sun), SunLight (its light on the ground), G1, G2 (the slope
//   textures of those cascades), Scene and Depth (unused values: they make the scene's colour and depth available).
// Thornwake's colours are display values where a sunlit white surface is about 1; here they are scaled by the sun's light.
struct FWwUnder
{
	float hash(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }
	float vnoise(float2 p)
	{
		float2 i = floor(p), f = frac(p);
		f = f * f * (3.0 - 2.0 * f);
		return lerp(lerp(hash(i), hash(i + float2(1.0, 0.0)), f.x), lerp(hash(i + float2(0.0, 1.0)), hash(i + float2(1.0, 1.0)), f.x), f.y);
	}
	// Curvature of one cascade of the simulated sea at s, over a footprint e (wider deeper down: a lens's focus blurs with depth).
	float fft_curvature(Texture2D g, SamplerState gs, float2 s, float size, float e)
	{
		float2 uv = s / size + 0.5 / 256.0;
		float d = e / size;
		float gx = g.SampleLevel(gs, uv + float2(d, 0.0), 0).x - g.SampleLevel(gs, uv - float2(d, 0.0), 0).x;
		float gz = g.SampleLevel(gs, uv + float2(0.0, d), 0).y - g.SampleLevel(gs, uv - float2(0.0, d), 0).y;
		return (gx + gz) / (2.0 * e);
	}
	// How much sunlight reaches depth d below the surface point where it came in (1 = flat water): a wave crest is a
	// converging lens, bright lines form beneath it. Fine ripples focus near the surface, longer waves deeper.
	float light_pattern(Texture2D g1, Texture2D g2, SamplerState gs, float2 s1, float2 s2, float2 sn, float d, float blur, float use_fft, float t)
	{
		if (use_fft > 0.5)
		{
			float e = 0.12 + d * 0.03 + blur;
			float fine = fft_curvature(g2, gs, s2, 7.9, e);
			float wide = fft_curvature(g1, gs, s1, 59.0, e * 2.5);
			float lap = lerp(fine, wide, smoothstep(1.0, 8.0, d));
			// Focusing with depth (thin lens: the light from a curved patch converges below it).
			float focus = 1.0 + lap * d * 0.25;
			return clamp(1.0 / max(focus, 0.2), 0.0, 4.0);
		}
		// Without the simulated surface: a drifting net of light (thin bright lines where two ridge fields cross), coarser
		// and fainter deeper down, broken into short bright patches so it projects into columns of light.
		float2 q = sn * (0.9 / (1.0 + d * 0.04 + blur * 0.3));
		q += float2(vnoise(q * 0.5 + t * 0.05), vnoise(q * 0.5 + 7.3 - t * 0.05)) * 1.5;
		float r1 = 1.0 - abs(2.0 * vnoise(q + float2(t * 0.31, t * 0.19)) - 1.0);
		float r2 = 1.0 - abs(2.0 * vnoise(q * 1.7 - float2(t * 0.23, -t * 0.37)) - 1.0);
		float ridge = pow(r1 * r2, 4.0);
		ridge *= smoothstep(0.3, 0.7, vnoise(q * 2.3 + 11.1));
		return 0.5 + ridge * 3.0 * lerp(1.0, 0.4, smoothstep(2.0, 25.0, d)) / (1.0 + blur);
	}
};
FWwUnder fn;

const float3 absorption = float3(0.45, 0.12, 0.09); // per metre (red is absorbed first)
const int shaft_steps = 32;
const float shaft_strength = 1.0;
float3 light = SunLight / 3.14159265; // what a sunlit white surface shows
float surface_depth = SurfaceDepth, density = Density;
float3 water_color = WaterColor;
// Gentle wobble of the whole view (refraction by moving water).
float2 uv = ViewUv + float2(sin(ViewUv.y * 40.0 + T * 1.7), cos(ViewUv.x * 33.0 + T * 1.3)) * 0.0018;
float3 ray = -normalize(Ray); // (the input points from the pixel to the camera)
float depth_cm = SceneTextureLookup(ViewportUVToSceneTextureUV(uv, 1), 1, false).r + Depth.r * 0.0;
float scene_dist = min(depth_cm / 100.0 / max(dot(ray, ResolvedView.ViewForward), 0.05), 1e6);
float to_surface = ray.z > 1e-4 ? surface_depth / ray.z : 1e6;
float3 behind = SceneTextureLookup(ViewportUVToSceneTextureUV(uv, 14), 14, false).rgb + Scene.rgb * 0.0;

// Colour of the water itself: darker and bluer with depth, but never black (light scatters down from above).
float3 deep = water_color * light * (exp(-absorption * 0.2 * (surface_depth + 4.0)) * 0.85 + 0.15 * float3(0.2, 0.45, 0.8) * exp(-surface_depth * 0.01));
float3 c;
float dist;
if (to_surface < scene_dist)
{
	// The surface seen from below.
	dist = to_surface;
	float cos_up = ray.z;
	float window = smoothstep(0.62, 0.68, cos_up); // the edge of Snell's window, about 48.6 degrees from vertical
	float2 ripple = float2(sin(ray.x * 60.0 + T * 2.3), cos(ray.y * 55.0 - T * 2.0)) * 0.012;
	float3 above = SceneTextureLookup(ViewportUVToSceneTextureUV(clamp(uv + ripple, 0.001, 0.999), 14), 14, false).rgb * 1.1;
	float3 mirror = deep * 0.8;
	c = lerp(mirror, above, window);
	// Bright, flickering edge of the window.
	c += float3(0.6, 0.8, 0.85) * light * 0.25 * smoothstep(0.55, 0.65, cos_up) * (1.0 - window);
}
else
{
	dist = scene_dist;
	c = behind;
	// Sunlight reaching the ground fades with its depth below the surface.
	float ground_depth = surface_depth - ray.z * dist;
	c *= exp(-absorption * max(ground_depth, 0.0) * 0.5);
}
// Along the ray: light is absorbed (per colour) and replaced by the water's own scattered colour.
float3 transmit = exp(-(absorption + density) * dist);
c = c * transmit + deep * (1.0 - transmit);
// Sunlight in the water: refracted into it, fading with the path it travelled down.
float3 sun = normalize(SunDir);
float sun_up = saturate(sun.z * 4.0);
if (sun_up > 0.0)
{
	float2 sun_flat = sun.xy / 1.33; // the level part shrinks by the refractive index
	float3 refr = normalize(float3(sun_flat.x, sun_flat.y, sqrt(max(1.0 - dot(sun_flat, sun_flat), 0.05)))); // toward the sun, in water
	refr = normalize(lerp(refr, float3(0.0, 0.0, 1.0), 0.6)); // (leaning toward the vertical: light from the bright sky above as much as from the sun)
	float reach = min(dist, clamp(4.0 + surface_depth * 6.0, 8.0, 70.0));
	float jitter = 0.2 + 0.6 * fn.hash(Parameters.SvPosition.xy * 0.731 + frac(T * 7.13) * 91.7);
	float3 sigma = absorption + density;
	float3 shafts = float3(0.0, 0.0, 0.0);
	// Forward scattering plus a little even light.
	float cos_t = dot(ray, refr);
	float phase = 0.0119 / pow(1.5625 - 1.5 * cos_t, 1.5) * 4.0 + 0.15;
	for (int i = 0; i < shaft_steps; i++)
	{
		// Samples crowd near the camera (most of what reaches the eye scatters there).
		float u = (float(i) + jitter) / float(shaft_steps);
		float st = reach * u * u;
		float step_len = reach * 2.0 * u / float(shaft_steps);
		float d = max(surface_depth - ray.z * st, 0.0); // depth below the surface here
		float2 rel = ray.xy * st + refr.xy / refr.z * d; // where this light came through the surface, from the camera
		float lit = fn.light_pattern(G1, G2, G1Sampler, CamFft1.xy + rel, CamFft2.xy + rel, CamNoise.xy + rel, d, st * 0.008, UseFft, T);
		float3 atten = exp(-sigma * (st + d / refr.z));
		shafts += atten * (lit - 0.55) * step_len; // the shafts: the focused light less the even glow
	}
	// (fainter in shallow water: there the light mostly shows as the net on the bed and a bright surface)
	float shallow = 0.6 + 0.4 * smoothstep(2.0, 8.0, surface_depth);
	c += max(shafts, 0.0) * density * phase * light * sun_up * shaft_strength * 2.0 * shallow;
	// The sunlit water's own glow: an even brightening, brighter looking up.
	c += water_color * light * sun_up * shaft_strength * 0.35 * (0.6 + 0.4 * max(ray.z, 0.0)) * (1.0 - exp(-density * reach));
	if (to_surface >= scene_dist)
	{
		// The bed: the same lenses draw a dancing net of light on it.
		float d = max(surface_depth - ray.z * dist, 0.0);
		float2 rel = ray.xy * dist + refr.xy / refr.z * d;
		float net = fn.light_pattern(G1, G2, G1Sampler, CamFft1.xy + rel, CamFft2.xy + rel, CamNoise.xy + rel, d, dist * 0.01, UseFft, T);
		c += behind * transmit * exp(-sigma * d / refr.z) * max(net - 0.6, 0.0) * 2.2 * sun_up * shaft_strength;
	}
	else
	{
		// The sun seen through the surface from below (inside Snell's window): a bright, flickering spot.
		float spot = pow(saturate(dot(ray, refr)), 400.0) * (0.7 + 0.3 * fn.vnoise(ray.xy * 80.0 + T * 3.0));
		c += light * spot * 3.0 * sun_up * transmit;
	}
}
return c;
