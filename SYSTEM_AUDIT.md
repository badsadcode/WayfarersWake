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

## Game (`Game/Rewrite`, 26,197 lines)

Drawing the world (state on 2026-11-10; details in `MIGRATION_STATUS.md`):

| Thornwake | Here |
|---|---|
| WorldStreamer, TileBuilder (LOD rings, time-budgeted commits, origin rebasing) | `AWwTerrainStreamer`: a new implementation of the same idea, without rebasing |
| Ground look, Noor's textures | ported (`M_WwTerrain`) |
| Vegetation, GroundCover, rocks, rock kit, props, wind on plants | ported (`WwVegetation`, `WwFlora`, `WwRocks`) |
| TreeField, impostors, far cards | 3D trees to 1.5 km and far cards ported; impostors and fades set aside (D25) |
| Water: sheet, rivers, waterfalls, spray, FFT sea, shore waves, foam net, underwater view | ported; wakes, caustics from above, local water simulation not |
| Sound (`Lightning.cs` ambience and thunder, `Soundscape.cs`) | `WwSound`, played by `AWwSky`: ported (D28); not placed in the world |
| Atmosphere 2.0, Weather, Lightning, clouds, light profiles, mist, light rays | `AWwSky`: engine sky, fixed weather and wind; not ported (look round, D25) |
| Character | `AWwCharacter`: block figure, Thornwake's speeds, swimming, flight |
| SeaStacks, lava look, local water (pouring, springs) | not ported |

Gameplay: the rules of step 1 are ported and tested headless (`Source/WayfarersWake/Rules`, 2026-11-10: Thornwake's `Game/Gameplay` Catalog, Inventory, Needs, PlacedObjects, the core of GameRules, and `ObjectRemovals`); a light version runs in the game (`FWwGameplay`: needs, pack, gathering, felling). Tom, 2026-11-10: Thornwake's crafting and building are placeholders he will redesign, so the steps below are not to be ported in depth; they stay as a map of what exists there. Thornwake's parts, in the order planned here (each depends on the ones before):

| Step | Thornwake | Lines | What it is |
|---|---|---|---|
| 1 | `WorldLab.Gameplay.cs`, `GameplayModels.cs`, `Toolbox.cs`, `WorldLab.ActionBar.cs`, `GameClock.cs` | about 1,300 | Inventory, tools, needs (food, warmth, stamina), gathering (stones, sticks, berries), the action bar |
| 2 | `WorldLab.Felling.cs`, `ObjectRemovals` | about 700 | Chopping and felling trees, logs, removed objects staying removed |
| 3 | `WorldLab.Building.cs`, `WorldLab.BuildMenu.cs`, `Core/Construction.cs`, `Core/RoofGeometry.cs`, `PieceIcons.cs` | about 1,500 | Crafting and building pieces, snapping, roofs |
| 4 | `AmbientLife*.cs`, `AnimalView.cs`, `WorldLab.Hunting.cs`, `WorldLab.Tracking.cs` | about 1,500 | Animals, hunting with spear and bow, tracks |
| 5 | `WorldLab.Fishing.cs` | 270 | Fishing |
| 6 | `WorldLab.Save.cs`, settings, `WorldMap.cs`, `WorldPreview.cs`, world-creation options | about 1,500 | Saves, the world map, the new-world screen |
| 7 | `TerrainEdits` in the game, `TerrainSculpt`, `Core/Earthworks.cs`, `Toolbox` terraform tools | about 700 | Shovel, pickaxe, hoe, terraforming |
| 8 | `Net/`, `WorldLab.Net.cs` | about 1,500 | Host and join, replication of all the above |
| 9 | `Torch.cs`, `WorldLab.Sites.cs`, `Landmarks.cs`, weather as it affects play (rain, cold, wetness) | - | The rest |

## How to extend parity

Every revision from 15 to 44 has its Thornwake fixture in `Tools/WorldGenTest/Fixtures` and a row in the `fixtures` table of `main.cpp` (`terrain-r1.json` is the current revision, 44). When Thornwake gains revision 45: port the change behind `Style.Revision >= 45`, copy the re-recorded `terrain-r1.json` and the frozen fixture of 44, add the rows, raise `FTerrainStyle::PortedRevision`. If a fingerprint differs, see DECISIONS D10.
