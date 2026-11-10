# System audit: Thornwake to Wayfarer's Wake

Source audited: `badsadcode/Thornwake_Rewrite` at `376d3f6` (build 142, v0.9.142). The local copy `F:\Codex\ThornwakeNEW` is at the same commit, plus untracked art (`ArtKit/NatureAssets`, `ArtKit/Textures`, `.blend` files, `AnimalsAssets.zip`). Both are read-only references.

## World sizes (from `WorldRecipe.Sizes` and `WorldProfile.Validate`)

Selectable: **32, 64, 128, 256, 512, 768, 1024, 2048, 4096 km**. The profile validator accepts 16 km to 4096 km. A world type is a recipe that scales its landmass counts and sizes with the world (`WorldRecipe.Resolve`); the macro raster is 512 to 2048 cells across whatever the size.

## World generation (`Game/WorldGen`, 10,380 lines)

| Thornwake | Lines | State there | Here (`WorldGen/`) | Parity |
|---|---|---|---|---|
| Hashing, DetMath, Noise, MacroRaster | 470 | working | `WwCore` | exact |
| WorldProfile, TerrainStyle, WorldManifest | 221 | working | `WwProfile`, `WwWorld.h` | exact (digest included) |
| WorldRecipe (sizes, scaling, all world types) | 277 | working | `WwProfile`, `WwWorldTypes` | exact, resized worlds included |
| WorldLayout (continents, islands, mountain belts) | 337 | working | `WwProfile` | exact |
| Hydrology (flood, erosion, lakes, rivers, deltas, waterfalls, index) | 1,143 | working | `WwHydrology` | exact |
| Climate, BiomeMap, BiomeRegistry, BiomeDefinition | 1,095 | working | `WwClimate` | exact |
| WorldPlan | 212 | working | `WwClimate` | exact |
| TerrainSampler (+ Water), BiomeLandforms | 1,274 | working | `WwSampler`, `WwSamplerWalls` | exact, revisions 8-44 |
| TerrainEdits | 179 | working | `WwSampler` | ported (brush, chunks); the game does not use it yet |
| ObjectRemovals, TerrainSculpt | 175 | working | - | not ported |
| Landforms (templates), Heightmap (natural types) | 616 | working | `WwWorldTypes` | exact (checked against recorded C# output, not Thornwake fixtures: it has none for these types) |
| LandformModules, Escarpments (rev 18-19) | 600 | working | `WwLandforms` | exact |
| Canyons (21-22), Cirques (23) | 357 | working | `WwCanyons` | exact |
| Troughs, fjords, fjord coasts (24-27) | 867 | working | `WwTroughs` | exact |
| Volcanoes, calderas, lava (28-32) | 943 | working | `WwVolcanoes` | exact |
| Headlands (36, 38), Spits (37) | 385 | working | `WwCoasts` | exact |
| Oxbows (39), Terraces (40), Fans (41), FallLips (42-44) | 639 | working | `WwRiverside` | exact |
| SeaStacks (35: rock models off steep coasts, no ground) | 193 | working | - | not ported |
| OceanWaves, SpectrumWaves | 387 | working / experimental | - | not ported (rendering side) |
| Sites | 9 | planned (empty) | - | - |

## Game (`Game/Rewrite`, 26,197 lines): not ported yet

| Thornwake | State there | Here |
|---|---|---|
| WorldStreamer, TileBuilder (LOD rings, time-budgeted commits, origin rebasing) | working | `AWwTerrainStreamer`: a new implementation of the same idea, without rebasing |
| Character | working | `AWwCharacter`: new, block figure |
| Atmosphere 2.0, Weather, Lightning, clouds, light profiles | working, large | `AWwSky`: engine sky only |
| Water (shore, FFT ocean, waterfalls, foam, wakes, local water) | working, mid-rework | flat surfaces only |
| Vegetation, TreeField, impostors, GroundCover, rocks | working | - |
| Gameplay (inventory, crafting, building, felling, hunting, fishing) | working | - |
| Net (host and join, bots, lag simulation) | working | settings replication only |
| Saves, settings, world map, benchmarks | working | stats logging only |

`Game/Simulation` (663 lines) and the gameplay data in `Content/` have not been audited in detail.

## How to extend parity

Every revision from 15 to 44 has its Thornwake fixture in `Tools/WorldGenTest/Fixtures` and a row in the `fixtures` table of `main.cpp` (`terrain-r1.json` is the current revision, 44). When Thornwake gains revision 45: port the change behind `Style.Revision >= 45`, copy the re-recorded `terrain-r1.json` and the frozen fixture of 44, add the rows, raise `FTerrainStyle::PortedRevision`. If a fingerprint differs, see DECISIONS D10.
