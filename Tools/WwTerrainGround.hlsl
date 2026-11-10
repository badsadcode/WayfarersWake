// The ground colour of M_WwTerrain: a port of the classic ("Current") ground of Thornwake's terrain shader
// (Game/Rewrite/TerrainBody.gdshaderinc, fragment()). Body of a material Custom node; Tools/CreateAssets.py builds
// the material around it. The colour maths is in display (gamma) space as in Thornwake, converted at the end.
// Inputs: TileUv (0..1 in the tile), Col (forest, mountain, beach), ColA (submerged), Ex (rock exposure, scree),
//   N (vertex normal, world), Rel (position relative to the camera, cm), AbsZ (world height, cm),
//   Tile (noise x, noise z of the tile's corner, tile size m), Cells, Sea (m), GroundTex (per-tile biome colours),
//   RockTex. Returns rgb = base colour (linear), a = roughness.
// Not ported yet: Noor's pixel ground textures (the optional style), plant-patch tint (needs the flora fields),
// rain wetness and puddles, dug soil, the lava map, the second rock look per biome pair.
struct FWwGround
{
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
};
FWwGround fn;

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

// Ground colours from the biome.
float3 grass = lerp(biome_a.rgb, biome_b.rgb, n1);
grass = lerp(grass, biome_b.rgb * float3(1.08, 1.04, 0.95), smoothstep(0.6, 0.95, n2) * 0.35); // dry patches
// Sparse plant cover (deserts, cold deserts, tundra): bare, dry ground shows through in patches.
grass = lerp(biome_b.rgb * float3(1.05, 1.0, 0.92), grass, smoothstep(0.0, 1.0, biome_b.a * 1.3 + (n2 - 0.5) * 0.5));
float3 woods = biome_a.rgb * float3(0.70, 0.71, 0.69); // forest floor / far canopy
float3 c = lerp(grass, woods, smoothstep(0.15, 0.75, forest) * 0.85);
c = lerp(c, float3(0.470, 0.460, 0.350), smoothstep(1300.0, 2300.0, h) * 0.65 * min(1.0, biome_b.a)); // alpine meadow

// All bare rock uses the rock texture, projected from the two sides and from above by the surface direction.
float3 wn = pow(abs(N), 4.0);
wn /= wn.x + wn.y + wn.z;
// Strata wander up and down along the face (no straight bands).
float hs = h + (fn.vnoise(float2(p.x + p.y, 0.0) * 0.012) - 0.5) * 9.0 + (fn.vnoise(float2(p.x - p.y, 3.0) * 0.05) - 0.5) * 2.5;
float2 ux = float2(p.y, -hs) / 10.0, uz = float2(p.x + 3.7, -hs) / 10.0, uy = p / 10.0;
float3 rock_side = Texture2DSample(RockTex, RockTexSampler, ux).rgb * wn.x + Texture2DSample(RockTex, RockTexSampler, uz).rgb * wn.y
	+ Texture2DSample(RockTex, RockTexSampler, uy).rgb * wn.z;
rock_side /= 0.5;
float rock_relief = dot(rock_side, float3(0.333, 0.333, 0.333)) - 1.0;
float3 rock = biome_d.rgb * rock_side * (0.9 + 0.2 * fn.vnoise(float2(p.x + p.y, h) * 0.05));
// Bare rock: on steep slopes everywhere, and on gentler ground in rocky biomes (patchy).
float exposure = Ex.x * (0.25 + 0.75 * smoothstep(0.35, 0.75, n2 + (n1 - 0.5) * 0.4));
float rocky = smoothstep(0.30, 0.52, slope + (n2 - 0.5) * 0.10 + mountain * 0.06 + exposure * 0.32
	+ rock_relief * 0.16 + (fn.vnoise(p * 0.27 + 4.1) - 0.5) * 0.12);
c = lerp(c, rock, rocky);
// Scree: the talus under a wall is loose rock however gentle it lies, in a ragged-edged sheet.
float talus = saturate(Ex.y);
float3 rubble = rock * (0.85 + 0.3 * n3);
c = lerp(c, rubble, smoothstep(0.15, 0.55, talus + (n2 - 0.5) * 0.3 + (fn.vnoise(p * 0.33) - 0.5) * 0.2) * (1.0 - rocky) * 0.9 * step(0.001, talus));
// Wet biomes (swamps): darker, muddier ground.
c = lerp(c, c * float3(0.72, 0.7, 0.62), biome_d.a * smoothstep(0.3, 0.7, n2 + 0.2));
float3 sand = lerp(float3(0.800, 0.725, 0.520), float3(0.700, 0.640, 0.470), n2);
c = lerp(c, sand, smoothstep(0.3, 0.65, wet + (n2 - 0.5) * 0.25) * (1.0 - smoothstep(0.25, 0.5, slope)));
float3 bed = lerp(float3(0.360, 0.340, 0.260), float3(0.450, 0.420, 0.330), n2); // gravel and silt under water
// River and lake banks (not sea beaches): from the waterline to ~0.5 m above it, in patches of dark mud, pale sand
// or gravel, and plain damp grass.
float damp_noise = (n3 - 0.5) * 0.14 + (fn.vnoise(p * 0.47 + 3.7) - 0.5) * 0.16;
float damp = smoothstep(0.2, 0.5, ColA + damp_noise) * (1.0 - submerged) * (1.0 - wet) * (1.0 - smoothstep(0.45, 0.7, slope));
float patch_kind = fn.vnoise(p * 0.045 + float2(17.0, 3.0));
float3 mud = float3(0.30, 0.25, 0.18) * (0.9 + 0.2 * n3);
float3 shore_sand = lerp(float3(0.66, 0.60, 0.45), float3(0.55, 0.52, 0.44), n2);
float3 gravel = lerp(float3(0.52, 0.50, 0.46), float3(0.62, 0.60, 0.55), fn.hash(floor(p * 5.0))) * (0.9 + 0.2 * n3);
float3 moss = c * float3(0.55, 0.62, 0.45);
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
bed = lerp(bed, peat, wetland);
c = lerp(c, bed, submerged);
// Snow from the biome and the cold, patchy at its edge; it does not hold on steep rock.
float snow = smoothstep(0.25, 0.75, biome_a.a + (n1 - 0.5) * 0.35) * (1.0 - smoothstep(0.42, 0.62, slope + (n2 - 0.5) * 0.12));
c = lerp(c, float3(0.93, 0.94, 0.96), snow);
c *= 0.92 + 0.16 * n3;
// Close-up detail (fades out by ~45 m): pixel-stepped speckle and small clumps at 6 cm texels.
float near_detail = 1.0 - smoothstep(15.0, 45.0, dist);
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
return float4(pow(max(c, 0.0), 2.2), roughness);
