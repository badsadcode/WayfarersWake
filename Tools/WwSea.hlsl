	// The sea's waves, shared by the water's vertex and pixel stages (Tools/CreateAssets.py puts these methods inside each
	// Custom node's struct). Ports from Thornwake's Game/Rewrite/WaterSea.gdshaderinc as its "experimental 4" style uses it
	// (FFT_OCEAN, EXP3A): gerstner() in its simulated-sea form and shore_wave(). Coordinates are Thornwake's: p = (east,
	// south) in metres, displacements (east, up, south).
	float hash(float2 p) { float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return frac((p3.x + p3.y) * p3.z); }
	float2 hash2(float2 p) { return float2(hash(p), hash(p + 17.31)); }
	float noise(float2 p)
	{
		float2 i = floor(p); float2 f = frac(p); float2 u = f * f * (3.0 - 2.0 * f);
		return lerp(lerp(hash(i), hash(i + float2(1.0, 0.0)), u.x), lerp(hash(i + float2(0.0, 1.0)), hash(i + float2(1.0, 1.0)), u.x), u.y);
	}
	// One cascade of the simulated sea (WwOcean: displacement = chop x, height, chop z, foam; slopes = dH/dx, dH/dz, chop
	// dDx/dx, chop dDz/dz). lod >= 0: that mipmap level (vertices); else the level the pixel's footprint asks for.
	void fft_cascade(Texture2D dt, SamplerState ds, Texture2D gt, SamplerState gs, float2 uv, float2 gx, float2 gy, float lod, float amp,
		inout float3 offset, inout float4 deriv, inout float foam)
	{
		float4 d, g;
		if (lod >= 0.0) { d = dt.SampleLevel(ds, uv, lod); g = gt.SampleLevel(gs, uv, lod); }
		else { d = dt.SampleGrad(ds, uv, gx, gy); g = gt.SampleGrad(gs, uv, gx, gy); }
		offset += d.xyz * amp;
		deriv += g * amp;
		foam = max(foam, d.w * min(amp, 1.0));
	}
	// The open sea and lakes from the three cascades (480, 59 and 7.9 m tiles). local: position within the terrain tile (m);
	// ox, oz: the tile's corner modulo each cascade's size. Returns the displacement; accumulates the normal, the crest height
	// and the fold (jac.xy) with the drifting foam field (jac.z, pixels only).
	float3 sea_waves(Texture2D d0, SamplerState d0s, Texture2D g0, SamplerState g0s, Texture2D d1, SamplerState d1s, Texture2D g1, SamplerState g1s,
		Texture2D d2, SamplerState d2s, Texture2D g2, SamplerState g2s, float2 local, float3 ox, float3 oz, float2 gx, float2 gy,
		float depth, float sea, float lake, float fetch_m, float expo, float min_lambda, bool vertex_pass, float sea_state, float crest_scale,
		inout float3 n, inout float crest, inout float3 jac)
	{
		const float3 fft_size = float3(480.0, 59.0, 7.9), fft_longest = float3(480.0, 12.0, 1.6);
		// Big waves die toward the shore (the shore waves take over) and in sheltered bays; lakes carry only the short
		// cascades, scaled by their fetch.
		float a0 = sea * lerp(0.25, 1.0, expo) * smoothstep(0.5, 14.0, depth);
		float lake_amp = lake * clamp(fetch_m / 1500.0, 0.15, 0.75) * (0.6 + 2.4 * pow(sea_state, 1.5));
		lake_amp *= smoothstep(0.15, 3.0, depth);
		float a1 = sea * lerp(0.5, 1.0, expo) * smoothstep(0.2, 3.0, depth) + lake_amp;
		float a2 = sea * smoothstep(0.05, 1.0, depth) + lake_amp;
		// A cascade fades once its longest waves are only a few pixels (vertices) long: its detail would only shimmer.
		a1 *= 1.0 - smoothstep(fft_longest.y * 0.5, fft_longest.y * 1.2, min_lambda);
		a2 *= 1.0 - smoothstep(fft_longest.z * 0.5, fft_longest.z * 1.2, min_lambda);
		float3 offset = float3(0.0, 0.0, 0.0);
		float4 deriv = float4(0.0, 0.0, 0.0, 0.0);
		float foam = 0.0;
		float spacing = min_lambda * 0.2;
		float3 lod = vertex_pass ? log2(max(spacing * 256.0 / fft_size, 1.0)) : float3(-1.0, -1.0, -1.0);
		float2 uv0 = (local + float2(ox.x, oz.x)) / fft_size.x + 0.5 / 256.0;
		float2 uv1 = (local + float2(ox.y, oz.y)) / fft_size.y + 0.5 / 256.0;
		float2 uv2 = (local + float2(ox.z, oz.z)) / fft_size.z + 0.5 / 256.0;
		if (a0 > 0.0) fft_cascade(d0, d0s, g0, g0s, uv0, gx / fft_size.x, gy / fft_size.x, lod.x, a0, offset, deriv, foam);
		if (a1 > 0.0) fft_cascade(d1, d1s, g1, g1s, uv1, gx / fft_size.y, gy / fft_size.y, lod.y, a1, offset, deriv, foam);
		if (a2 > 0.0) fft_cascade(d2, d2s, g2, g2s, uv2, gx / fft_size.z, gy / fft_size.z, lod.z, a2, offset, deriv, foam);
		// Slopes of the displaced surface (the chop compresses the crests: steeper there).
		float sx = 1.0 + deriv.z, sz = 1.0 + deriv.w;
		n.x -= deriv.x / max(sx, 0.25);
		n.z -= deriv.y / max(sz, 0.25);
		crest += offset.y * crest_scale;
		if (vertex_pass) { jac += float3(-deriv.z, -deriv.w, 0.0); return offset; }
		// The foam field is read where the water is shown (rest position + displacement): it drifts with the wind, and read
		// at the rest position it swung back and forth with every wave.
		float f = 0.0;
		if (a0 > 0.0) f = max(f, d0.SampleGrad(d0s, uv0 + offset.xz / fft_size.x, gx / fft_size.x, gy / fft_size.x).w * min(a0, 1.0));
		if (a1 > 0.0) f = max(f, d1.SampleGrad(d1s, uv1 + offset.xz / fft_size.y, gx / fft_size.y, gy / fft_size.y).w * min(a1, 1.0));
		if (a2 > 0.0) f = max(f, d2.SampleGrad(d2s, uv2 + offset.xz / fft_size.z, gx / fft_size.z, gy / fft_size.z).w * min(a2, 1.0) * 0.5);
		jac += float3(-deriv.z, -deriv.w, f);
		return offset;
	}
	// Shore waves (sea): crests along the depth contours rolling in toward the shore. Returns the displacement; accumulates
	// the normal; breaking 0..1 and since (0 at the crest .. 1 one period behind it) for the foam.
	float3 shore_wave(float2 p, float depth, float2 grad, float expo, float min_lambda, float t, float sea_state, inout float3 n,
		out float breaking, out float since, out float k_out)
	{
		breaking = 0.0; since = 1.0; k_out = 1.0;
		float s = length(grad);
		// Hand-over from 19 m: the waves visibly swing round to face the shore further out.
		float active = (1.0 - smoothstep(9.0, 19.0, depth)) * smoothstep(0.003, 0.012, s) * (1.0 - smoothstep(0.22, 0.45, s));
		if (active <= 0.0) return float3(0.0, 0.0, 0.0);
		float2 dir = grad / s; // toward the shore
		float period = 7.0 + 4.0 * sea_state;
		float omega = 6.28318530718 / period;
		float d = max(depth, 0.05);
		// Travel-time phase over a reference bed slope: a function of depth only, so crests are depth contours moving
		// shoreward with time. The wavenumber is the phase's true gradient.
		const float S_REF = 0.03;
		float theta = -(2.0 * omega * sqrt(d) / (S_REF * sqrt(9.81)) + omega * t) + 3.0 * noise(p * 0.004) // slow wobble along the coast
			+ 1.5 * noise(p * 0.013 + 7.7) + 0.9 * noise(p * 0.035 + 2.3); // crest segments run ahead or lag
		float k = omega / sqrt(9.81 * d) * clamp(s / S_REF, 0.15, 4.0);
		k_out = k;
		float lambda = 6.28318530718 / k;
		// Height: open-coast swell, shoaling (Green's law) until it breaks at about 0.75 d.
		float h0 = (0.12 + 1.15 * sea_state) * expo * (0.75 + 0.5 * noise(p * 0.01 + t * 0.02));
		float h = h0 * pow(10.0 / max(d, 0.4), 0.25);
		float limit = (0.62 + 0.2 * noise(p * 0.02 + 3.1) + 0.12 * noise(p * 0.06 + 9.4)) * d + 0.03; // some crest segments break sooner
		breaking = smoothstep(0.55, 0.95, h / limit) * active;
		h = min(h, limit);
		float amp = 0.5 * h * active * smoothstep(min_lambda, min_lambda * 2.0, lambda);
		float q = clamp(0.35 + 0.65 * h / limit, 0.0, 1.0) / max(k * amp * 1.6 + 1e-4, 1.0); // steeper in the shallows, never folding
		float c = cos(theta), sn = sin(theta);
		// Leaning crest: a second harmonic makes the shoreward face steeper than the back.
		float lean = 0.22 * breaking;
		float y = amp * (sn + lean * sin(2.0 * theta - 0.6));
		n.x -= dir.x * k * amp * c; n.z -= dir.y * k * amp * c; n.y -= q * k * amp * sn;
		since = frac((1.5707963 - theta) / 6.28318530718);
		return float3(q * amp * dir.x * c, y, q * amp * dir.y * c);
	}
