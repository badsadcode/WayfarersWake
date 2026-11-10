# System audit: Thornwake to Wayfarer's Wake

Source audited: `badsadcode/Thornwake_Rewrite` at `376d3f6` (build 142, v0.9.142). The local copy `F:\Codex\ThornwakeNEW` is at the same commit, plus untracked art (`ArtKit/NatureAssets`, `ArtKit/Textures`, `.blend` files, `AnimalsAssets.zip`). Both are read-only references.

## World sizes (from `WorldRecipe.Sizes` and `WorldProfile.Validate`)

Selectable: **32, 64, 128, 256, 512, 768, 1024, 2048, 4096 km**. The profile validator accepts 16 km to 4096 km. A world type is a recipe that scales its landmass counts and sizes with the world (`WorldRecipe.Resolve`); the macro raster is 512 to 2048 cells across whatever the size.

## World generation (`Game/WorldGen`, 10,380 lines)

| Thornwake | Lines | State there | Here (`WorldGen/`) | Parity |
|---|---|---|---|---|
| Hashing, DetMath, Noise, MacroRaster | 470 | working | `WwCore` | exact |
| WorldProfile, TerrainStyle, WorldManifest | 221 | working | `WwProfile`, `WwWorld.h` | exact (digest included) |
| WorldRecipe (sizes, scaling) | 277 | working | `WwProfile` | classic types only |
| WorldLayout (continents, islands, mountain belts) | 337 | working | `WwProfile` | exact |
| Hydrology (flood, erosion, lakes, rivers, deltas, index) | 1,143 | working | `WwHydrology` | exact to revision 16; waterfalls (revision 20) missing |
| Climate, BiomeMap, BiomeRegistry, BiomeDefinition | 1,095 | working | `WwClimate` | exact; the volcano biome rule is missing |
| WorldPlan | 212 | working | `WwClimate` | exact for classic types |
| TerrainSampler (+ Water), BiomeLandforms | 1,274 | working | `WwSampler` | exact, revisions 8-16 |
| TerrainEdits | 179 | working | `WwSampler` | ported (brush, chunks); the game does not use it yet |
| ObjectRemovals, TerrainSculpt | 175 | working | - | not ported |
| Landforms (templates), Heightmap (natural types) | 616 | working | - | **not ported** |
| LandformModules, Escarpments, Canyons, Cirques, Troughs, Volcanoes, SeaStacks, Headlands, Spits, Oxbows, Terraces, Fans, FallLips | 4,300 | working (revisions 17-44) | - | **not ported** |
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

Each revision has a fixture in Thornwake's `Tests/WorldGen/Fixtures` (`terrain-build115.json` is 17, `-build116` 18, `-build117` 19, `-r20`, `-build118` 21, and so on; the list is in `Tests/WorldGen/CoreTests.cs`). Port the module, copy its fixture into `Tools/WorldGenTest/Fixtures`, add it to the `fixtures` table in `main.cpp`, and raise `FTerrainStyle::PortedRevision`.
