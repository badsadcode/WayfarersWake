// The ground colour of M_WwTerrain: a port of the ground of Thornwake's terrain shader, both styles (classic colours
// and Noor's pixel textures, which Tom plays with)
// (Game/Rewrite/TerrainBody.gdshaderinc, fragment()). Body of a material Custom node; Tools/CreateAssets.py builds
// the material around it. The colour maths is in display (gamma) space as in Thornwake, converted at the end.
// Inputs: TileUv (0..1 in the tile), Col (forest, mountain, beach), ColA (submerged), Ex (rock exposure, scree),
//   N (vertex normal, world), Rel (position relative to the camera, cm), AbsZ (world height, cm),
//   Tile (noise x, noise z of the tile's corner, tile size m), Cells, Sea (m), GroundTex (per-tile biome colours),
//   RockTex, NoorTex (Noor's 16 ground layers), Style (x: 1 = Noor's textures, y: ground variety).
// Returns rgb = base colour (linear), a = roughness.
// Returns also Bump: the surface normal with relief taken from the ground's own light and shade.
// Not ported yet: plant-patch tint and plant-driven bare ground (need the flora fields), Thornwake's own relief and parallax,
// rain wetness and puddles, dug soil, the lava map, the second rock look per biome pair.
struct FWwGround
{
	float size;
	float hash(float2 p)
	{
		p = p - 1000.0 * floor(p / 1000.0);
		float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
		p3 += dot(p3, p3.yzx + 33.33);
		return frac((p3.x + p3.y) * p3.z);
	}
	float vnoise(float2 p)
	{
		float2 i = floor(p); float2 f = frac(p); float2 u = f * f * (3.0 - 2.0 * f);
		return lerp(lerp(hash(i), hash(i + float2(1.0, 0.0)), u.x), lerp(hash(i + float2(0.0, 1.0)), hash(i + float2(1.0, 1.0)), u.x), u.y);
	}
	// A ground texture at ~8 m per tile, two differently rotated and scaled copies blended by slow noise so the tiles
	// do not repeat visibly over open ground.
	float3 gtex(Texture2DArray T, SamplerState S, int layer, float2 p)
	{
		// (size: how large the textures are drawn, 1 = 8 m a tile; the menu sets it, since each set of textures suits its own)
		float2 u1 = p / (8.0 * size);
		float2 u2 = float2(0.8 * p.x + 0.6 * p.y, -0.6 * p.x + 0.8 * p.y) / (7.1 * size) + float2(0.37, 0.71);
		float3 a = T.Sample(S, float3(u1, layer)).rgb;
		float3 b = T.Sample(S, float3(u2, layer)).rgb;
		return lerp(a, b, smoothstep(0.35, 0.65, vnoise(p * 0.045 + float2(layer * 7.3, 1.9))));
	}
	// A layer's mean colour: its smallest mip, which is the whole picture averaged. (Read from the texture, not written
	// into the shader, so the ground's textures can be changed for another set while the game runs.)
	float3 gmean(Texture2DArray T, SamplerState S, int layer) { return max(T.SampleLevel(S, float3(0.5, 0.5, layer), 20.0).rgb, 0.02); }
	// The texture as detail over a biome colour: its light and shade around 1, with only a hint of its own hues.
	float3 gdetail(Texture2DArray T, SamplerState S, int layer, float2 p, float3 mean)
	{
		float3 t = gtex(T, S, layer, p), m = max(mean, 0.02);
		float shade = dot(t, float3(0.3, 0.55, 0.15)) / max(dot(m, float3(0.3, 0.55, 0.15)), 0.02);
		float3 d = lerp(float3(shade, shade, shade), t / m, 0.2);
		return 1.0 + (d - 1.0) * 0.7;
	}
};
FWwGround fn;
fn.size = Style.z > 0.01 ? Style.z : 1.0;
static const int G_GRASS = 0, G_DIRT = 1, G_SAND = 2, G_ROCK = 3, G_RIVER_BED = 4, G_SEA_BED = 5, G_FOREST = 6, G_GRAVEL = 7;
static const int G_MUD = 8, G_WET_MUD = 9, G_MOSS = 10, G_SCREE = 11, G_SNOW = 12, G_PEAT = 13;
float noor = step(0.5, Style.x), ground_variety = Style.y * noor;
float cover = 0.5; // (grass cover from the plant fields: not ported yet)

float2 p = Tile.xy + TileUv * Tile.z;
float h = AbsZ / 100.0 - Sea;
float side = Cells + 1.0;
float2 tuv = (TileUv * Cells + 0.5) / side;
float4 biome_a = Texture2DSampleLevel(GroundTex, GroundTexSampler, float2(tuv.x / 3.0, tuv.y), 0);          // grass, snow
float4 biome_b = Texture2DSampleLevel(GroundTex, GroundTexSampler, float2((tuv.x + 1.0) / 3.0, tuv.y), 0);  // dry ground, plants
float4 biome_d = Texture2DSampleLevel(GroundTex, GroundTexSampler, float2((tuv.x + 2.0) / 3.0, tuv.y), 0);  // rock, wetness

float forest = Col.r, mountain = Col.g, wet = Col.b, submerged = smoothstep(0.5, 0.8, ColA);
float slope = 1.0 - saturate(N.z);
// A sheer face is rock however its corners' normals lean: only where the triangle itself stands steeper than ~57 degrees.
float3 face = normalize(cross(ddx(Rel), ddy(Rel)));
float face_up = abs(face.z);
slope = max(slope, (1.0 - face_up) * (1.0 - smoothstep(0.35, 0.55, face_up)));
float n1 = fn.vnoise(p * 0.012), n2 = fn.vnoise(p * 0.21), n3 = fn.vnoise(p * 1.7);
float dist = length(Rel) / 100.0;
float g_near = (1.0 - smoothstep(50.0, 200.0, dist)) * noor;   // Noor's materials up close, the usual colours far away
float g_detail = (1.0 - smoothstep(150.0, 600.0, dist)) * noor; // pattern over the biome colours

// Ground colours from the biome.
float3 grass = lerp(biome_a.rgb, biome_b.rgb, n1);
grass = lerp(grass, biome_b.rgb * float3(1.08, 1.04, 0.95), smoothstep(0.6, 0.95, n2) * 0.35); // dry patches
// Sparse plant cover (deserts, cold deserts, tundra): bare, dry ground shows through in patches.
grass = lerp(biome_b.rgb * float3(1.05, 1.0, 0.92), grass, smoothstep(0.0, 1.0, biome_b.a * 1.3 + (n2 - 0.5) * 0.5));
// Ground variety: warm and cool patches tens of metres across, and bare ground where plants thin out, in the
// biome's character (soil, gravel up in cold mountains, sand where it is dry, moss where it is wet).
float hue = fn.vnoise(p * 0.017 + float2(11.0, 5.0)) * 0.7 + fn.vnoise(p * 0.06 + float2(2.0, 9.0)) * 0.3;
grass *= lerp(float3(1.0, 1.0, 1.0), lerp(float3(0.9, 1.0, 0.95), float3(1.08, 1.03, 0.86), smoothstep(0.3, 0.7, hue)), 0.7 * ground_variety);
float patches = fn.vnoise(p * 0.028 + float2(3.0, 7.0)) * 0.5 + fn.vnoise(p * 0.13 + float2(8.0, 1.0)) * 0.3 + n3 * 0.2;
float bare = smoothstep(0.62, 0.76, patches + (0.55 - cover) * 0.35 + (0.7 - biome_b.a) * 0.35)
	* ground_variety * (1.0 - smoothstep(0.35, 0.5, slope)) * (1.0 - submerged);
{
	float dryness = 1.0 - saturate(biome_b.a);
	float cold = saturate(mountain * 1.4 + biome_a.a * 1.5);
	float w_gravel = smoothstep(0.35, 0.8, cold) * 0.75, w_sand = smoothstep(0.55, 0.95, dryness) * 0.55;
	float w_moss = smoothstep(0.3, 0.7, biome_d.a) * 0.6;
	float3 base = lerp(float3(0.40, 0.33, 0.25), biome_b.rgb * float3(0.8, 0.76, 0.7), 0.3);
	float3 bare_detail = fn.gdetail(NoorTex, NoorTexSampler, G_DIRT, p, fn.gmean(NoorTex, NoorTexSampler, G_DIRT));
	base = lerp(base, float3(0.50, 0.48, 0.45), w_gravel); bare_detail = lerp(bare_detail, fn.gdetail(NoorTex, NoorTexSampler, G_GRAVEL, p, fn.gmean(NoorTex, NoorTexSampler, G_GRAVEL)), w_gravel);
	base = lerp(base, float3(0.70, 0.63, 0.48), w_sand);   bare_detail = lerp(bare_detail, fn.gdetail(NoorTex, NoorTexSampler, G_SAND, p, fn.gmean(NoorTex, NoorTexSampler, G_SAND)), w_sand);
	base = lerp(base, float3(0.28, 0.33, 0.19), w_moss);   bare_detail = lerp(bare_detail, fn.gdetail(NoorTex, NoorTexSampler, G_MOSS, p, fn.gmean(NoorTex, NoorTexSampler, G_MOSS)), w_moss);
	float3 b = base * lerp(float3(1.0, 1.0, 1.0), bare_detail, 0.9) * (0.92 + 0.16 * n3);
	grass = lerp(grass, b, bare);
}
float3 woods = biome_a.rgb * float3(0.70, 0.71, 0.69); // forest floor / far canopy
grass *= lerp(float3(1.0, 1.0, 1.0), fn.gdetail(NoorTex, NoorTexSampler, G_GRASS, p, fn.gmean(NoorTex, NoorTexSampler, G_GRASS)), 0.85 * g_detail * (1.0 - bare));
// Forest floor: Noor's leaf litter, a little of the biome's colour kept.
woods = lerp(woods, fn.gtex(NoorTex, NoorTexSampler, G_FOREST, p) * lerp(float3(1.0, 1.0, 1.0), woods / max(fn.gmean(NoorTex, NoorTexSampler, G_FOREST), 0.02), 0.35), 0.9 * g_near);
float3 c = lerp(grass, woods, smoothstep(0.15, 0.75, forest) * 0.85);
c = lerp(c, float3(0.470, 0.460, 0.350), smoothstep(1300.0, 2300.0, h) * 0.65 * min(1.0, biome_b.a)); // alpine meadow

// All bare rock uses the rock texture, projected from the two sides and from above by the surface direction.
float3 wn = pow(abs(N), 4.0);
wn /= wn.x + wn.y + wn.z;
// Strata wander up and down along the face (no straight bands).
float hs = h + (fn.vnoise(float2(p.x + p.y, 0.0) * 0.012) - 0.5) * 9.0 + (fn.vnoise(float2(p.x - p.y, 3.0) * 0.05) - 0.5) * 2.5;
float2 ux = float2(p.y, -hs) / (10.0 * fn.size), uz = float2(p.x + 3.7, -hs) / (10.0 * fn.size), uy = p / (10.0 * fn.size);
float3 rock_side = Texture2DSample(RockTex, RockTexSampler, ux).rgb * wn.x + Texture2DSample(RockTex, RockTexSampler, uz).rgb * wn.y
	+ Texture2DSample(RockTex, RockTexSampler, uy).rgb * wn.z;
rock_side /= 0.5;
float rock_relief = dot(rock_side, float3(0.333, 0.333, 0.333)) - 1.0;
float3 rock = biome_d.rgb * rock_side * (0.9 + 0.2 * fn.vnoise(float2(p.x + p.y, h) * 0.05));
// Noor: rocky ground on gentle slopes uses Noor's cobbled rock in the biome's rock colour; cliffs keep theirs.
float3 noor_rock = fn.gtex(NoorTex, NoorTexSampler, G_ROCK, p) * biome_d.rgb / max(dot(fn.gmean(NoorTex, NoorTexSampler, G_ROCK), float3(0.333, 0.333, 0.333)), 0.02);
rock = lerp(rock, noor_rock, (1.0 - smoothstep(0.3, 0.5, slope)) * 0.85 * g_near);
// Bare rock: on steep slopes everywhere, and on gentler ground in rocky biomes (patchy).
float exposure = Ex.x * (0.25 + 0.75 * smoothstep(0.35, 0.75, n2 + (n1 - 0.5) * 0.4));
float rocky = smoothstep(0.30, 0.52, slope + (n2 - 0.5) * 0.10 + mountain * 0.06 + exposure * 0.32
	+ rock_relief * 0.16 + (fn.vnoise(p * 0.27 + 4.1) - 0.5) * 0.12);
c = lerp(c, rock, rocky);
// Noor: scree on the steeper slopes of mountains, below the bare rock.
float3 noor_scree = fn.gtex(NoorTex, NoorTexSampler, G_SCREE, p) * biome_d.rgb / max(dot(fn.gmean(NoorTex, NoorTexSampler, G_SCREE), float3(0.333, 0.333, 0.333)), 0.02);
c = lerp(c, noor_scree, smoothstep(0.2, 0.5, mountain) * smoothstep(0.18, 0.28, slope + (n2 - 0.5) * 0.12) * (1.0 - smoothstep(0.34, 0.46, slope)) * 0.8 * g_near);
// Scree: the talus under a wall is loose rock however gentle it lies, in a ragged-edged sheet.
float talus = saturate(Ex.y);
float3 rubble = lerp(rock * (0.85 + 0.3 * n3), noor_scree, g_near);
c = lerp(c, rubble, smoothstep(0.15, 0.55, talus + (n2 - 0.5) * 0.3 + (fn.vnoise(p * 0.33) - 0.5) * 0.2) * (1.0 - rocky) * 0.9 * step(0.001, talus));
// Wet biomes (swamps): darker, muddier ground.
c = lerp(c, c * float3(0.72, 0.7, 0.62), biome_d.a * smoothstep(0.3, 0.7, n2 + 0.2));
float3 sand = lerp(float3(0.800, 0.725, 0.520), float3(0.700, 0.640, 0.470), n2);
float3 sand_detail = fn.gdetail(NoorTex, NoorTexSampler, G_SAND, p, fn.gmean(NoorTex, NoorTexSampler, G_SAND));
sand *= lerp(float3(1.0, 1.0, 1.0), sand_detail, 0.9 * g_detail);
c = lerp(c, sand, smoothstep(0.3, 0.65, wet + (n2 - 0.5) * 0.25) * (1.0 - smoothstep(0.25, 0.5, slope)));
float3 bed = lerp(float3(0.360, 0.340, 0.260), float3(0.450, 0.420, 0.330), n2); // gravel and silt under water
// Noor: sea floor below sea level, river and lake beds above it.
bed = lerp(bed, lerp(fn.gtex(NoorTex, NoorTexSampler, G_RIVER_BED, p), fn.gtex(NoorTex, NoorTexSampler, G_SEA_BED, p), 1.0 - smoothstep(-1.5, -0.2, h)), 0.9 * g_near);
// River and lake banks (not sea beaches): from the waterline to ~0.5 m above it, in patches of dark mud, pale sand
// or gravel, and plain damp grass.
float damp_noise = (n3 - 0.5) * 0.14 + (fn.vnoise(p * 0.47 + 3.7) - 0.5) * 0.16;
float damp = smoothstep(0.2, 0.5, ColA + damp_noise) * (1.0 - submerged) * (1.0 - wet) * (1.0 - smoothstep(0.45, 0.7, slope));
float patch_kind = fn.vnoise(p * 0.045 + float2(17.0, 3.0));
float3 mud = float3(0.30, 0.25, 0.18) * (0.9 + 0.2 * n3);
float3 shore_sand = lerp(float3(0.66, 0.60, 0.45), float3(0.55, 0.52, 0.44), n2);
float3 gravel = lerp(float3(0.52, 0.50, 0.46), float3(0.62, 0.60, 0.55), fn.hash(floor(p * 5.0))) * (0.9 + 0.2 * n3);
float3 moss = c * float3(0.55, 0.62, 0.45);
// Noor: wet mud near the waterline, dry mud higher up the bank.
float soaked = smoothstep(0.4, 0.55, ColA + damp_noise);
mud = lerp(mud, lerp(fn.gtex(NoorTex, NoorTexSampler, G_MUD, p), fn.gtex(NoorTex, NoorTexSampler, G_WET_MUD, p), soaked), g_near);
gravel = lerp(gravel, fn.gtex(NoorTex, NoorTexSampler, G_GRAVEL, p), g_near);
shore_sand *= lerp(float3(1.0, 1.0, 1.0), sand_detail, 0.8 * g_detail);
moss = lerp(moss, fn.gtex(NoorTex, NoorTexSampler, G_MOSS, p) * 0.85, g_near);
float3 shore = lerp(c * float3(0.62, 0.6, 0.55) + float3(0.03, 0.025, 0.015), mud, smoothstep(0.26, 0.36, patch_kind));
shore = lerp(shore, gravel, smoothstep(0.46, 0.56, patch_kind));
shore = lerp(shore, shore_sand, smoothstep(0.62, 0.72, patch_kind));
// Wetlands: pool margins are dark mud and wet moss rather than sand and gravel.
float wetland = smoothstep(0.45, 0.75, biome_d.a);
shore = lerp(shore, lerp(mud * 0.8, moss, smoothstep(0.4, 0.7, patch_kind)), wetland);
c = lerp(c, shore, damp);
// Wetland pool beds: dark peat and silt with sunken leaves and twigs.
float3 peat = lerp(float3(0.15, 0.115, 0.075), float3(0.24, 0.20, 0.13), n2) * (0.85 + 0.3 * n3);
float2 leaf_cell = floor(p * 3.0);
float leaf = step(0.82, fn.hash(leaf_cell)) * step(0.35, fn.hash(floor(p * 9.0)));
float3 leaf_colour = lerp(float3(0.42, 0.26, 0.11), float3(0.30, 0.30, 0.13), fn.hash(leaf_cell + 7.0));
peat = lerp(peat, leaf_colour, leaf * 0.8);
float2 tc = floor(p * 0.8);
float ang = fn.hash(tc) * 6.2832;
float2 q = frac(p * 0.8) - 0.5;
float along = dot(q, float2(cos(ang), sin(ang))), across = dot(q, float2(-sin(ang), cos(ang)));
float twig = step(0.6, fn.hash(tc + 3.0)) * step(abs(across), 0.018) * step(abs(along), 0.35);
peat = lerp(peat, float3(0.09, 0.07, 0.05), twig);
peat = lerp(peat, fn.gtex(NoorTex, NoorTexSampler, G_PEAT, p), g_near);
bed = lerp(bed, peat, wetland);
c = lerp(c, bed, submerged);
// Snow from the biome and the cold, patchy at its edge; it does not hold on steep rock.
float snow = smoothstep(0.25, 0.75, biome_a.a + (n1 - 0.5) * 0.35) * (1.0 - smoothstep(0.42, 0.62, slope + (n2 - 0.5) * 0.12));
float3 snow_c = float3(0.93, 0.94, 0.96) * lerp(float3(1.0, 1.0, 1.0), fn.gdetail(NoorTex, NoorTexSampler, G_SNOW, p, fn.gmean(NoorTex, NoorTexSampler, G_SNOW)), 0.7 * g_detail);
c = lerp(c, snow_c, snow);
c *= 0.92 + 0.16 * n3;
// Close-up detail (fades out by ~45 m): pixel-stepped speckle and small clumps at 6 cm texels.
float near_detail = (1.0 - smoothstep(15.0, 45.0, dist)) * lerp(1.0, 0.3, noor); // Noor's textures bring their own detail
float2 texel = floor(p * 16.0);
float speck = fn.hash(texel);
float clump = fn.vnoise(p * 1.6) * 0.6 + fn.vnoise(p * 4.3) * 0.4;
float rockiness = smoothstep(0.30, 0.52, slope);
float3 grass_detail = lerp(float3(0.94, 0.94, 0.94), float3(1.06, 1.05, 0.96), step(0.72, speck)) * lerp(0.95, 1.04, clump);
grass_detail = lerp(grass_detail, float3(0.86, 0.83, 0.74), step(0.97, speck) * (1.0 - forest * 0.5)); // bare soil flecks
float rock_speck = 0.93 + 0.12 * fn.hash(floor(p * 12.0));
float3 detail = lerp(grass_detail, float3(rock_speck, rock_speck, rock_speck), rockiness);
float calm = 0.92 + 0.12 * speck;
detail = lerp(detail, float3(calm, calm, calm), max(snow, wet)); // calmer on snow and sand
c *= lerp(float3(1.0, 1.0, 1.0), detail, near_detail);
float roughness = lerp(0.95, 0.55, snow);
roughness = lerp(roughness, 0.55, damp * wetland * 0.7); // glistening mud at wetland pool margins
// Relief (new here; Thornwake's ground relief and parallax are not ported): the ground has no normal maps, so the
// surface is tilted by its own light and shade: where the texture is darker the ground is taken to lie lower. Worked
// out from how the finished colour changes across the screen, so it costs no further texture reads. Near the camera
// only (beyond, it would only shimmer); stronger on rock and bare ground than on grass, none on snow.
float3 normal = normalize(N);
{
	float lum = dot(c, float3(0.3, 0.55, 0.15));
	float3 dpx = ddx(Rel), dpy = ddy(Rel);
	float dhx = ddx(lum), dhy = ddy(lum);
	float3 r1 = cross(dpy, normal), r2 = cross(normal, dpx);
	float det = dot(dpx, r1);
	float3 grad = sign(det) * (dhx * r1 + dhy * r2);
	float amount = (1.0 - smoothstep(25.0, 90.0, dist)) * lerp(4.0, 9.0, saturate(rocky + bare)) * (1.0 - snow);
	normal = normalize(abs(det) * normal - amount * grad);
}
Bump = normal;
return float4(pow(max(c, 0.0), 2.2), roughness);
