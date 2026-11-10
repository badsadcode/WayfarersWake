# Migration status

_Last updated 2026-11-10 (session 1, after the landform port). Keep this current: it is where the next session starts._

## What works (verified)

| Area | State | Evidence |
|---|---|---|
| World generator, terrain revisions 8-44 (Thornwake's current) | **Bit-identical to Thornwake** | `Tools/WorldGenTest`: 468/468 checks, every frozen fingerprint from revision 15 to 44 (31 fixture sets x 5 profiles x 3 seeds) |
| Same generator inside the engine build | Bit-identical | `-WwSelfTest` logs `MATCH` at revision 44 |
| All 23 world types (7 classic, 5 template, 11 natural) | **Bit-identical to Thornwake** | `WorldGenTest --types`: 42/42 checks against output recorded from the C# generator (16 new types on two seeds, six resized worlds); `Fixtures/world-types.txt` |
| World sizes 32 ... 4096 km | All nine build | `WorldGenTest --sizes` (table in BENCHMARK.md); 4096 km also launched in game |
| UE 5.9 project | Compiles, launches | `Build.bat WayfarersWakeEditor Win64 Development` succeeds; game runs with `-game` |
| Streamed terrain (quadtree LOD, 9 levels) | Works, no holes seen | 330 tiles around the player; 24 km flight at 400 m/s with 0-10 tiles missing |
| Terrain collision + player | Works | Player is placed by a line trace onto async-cooked tile collision (trace hit 77.7 m, generated 77.8 m) |
| Water surfaces (sea, lakes, rivers, ponds) | First version | Flat translucent surfaces at the generator's levels; no waves, flow, foam or swimming |
| Sky, sun, clouds, fog, time of day | First version | Engine sky atmosphere and volumetric clouds; the planet top follows the view |
| Ground look (Thornwake's ground shader, both styles) | **Ported, compared with one Godot dump** | `M_WwTerrain`: biome grass / dry / rock colours, forest floor, rock texture on slopes and sheer faces, scree, beach sand, shore mud and gravel, wetland beds, snow; Noor's sixteen ground textures and the ground-variety patches (the style Tom plays with; parameter `GroundStyle`, x = 0 gives the classic colours). Compared at the place of Tom's Godot dump `dump-20261109-133922`: same landscape and trees; still different: see the gaps |
| F9 debug dump | **Works** (run through the test switch `-WwDump`) | Writes `Saved/Dumps/dump-<time>.txt` and `.png` (packaged build: `Dist/Windows/WayfarersWake/Saved/Dumps`), never Thornwake's folder. The text ends with a `Reproduce:` line of switches. At the place of a Godot dump it reports the same ground, river level, biome share, temperature and rainfall |
| Forests (Thornwake's tree lattice and species rules) | **Ported, visually inspected** | `WwVegetation`: about 79,000 trees around the player from the 17 imported species, full models near, distant models to about 1.5 km; 115-200 fps at 1280x720. Positions not yet checked against Thornwake tree by tree |
| Packaged Windows build (no editor needed) | **Built and launched** | `RunUAT BuildCookRun` (Development, 3 min) to `Dist/Windows/WayfarersWake.exe`, 980 MB, not in git. Launched once: world, ground, 79,102 trees, 200 fps standing still, 2.05 GB memory. Not played by hand |
| Multiplayer shape | Designed in, never run | World settings replicate through `AWwGameState`; every machine rebuilds terrain locally |

In the brief's terms: the generator is **tested**; the project is **compiled and launched**; terrain, collision, sky, ground look and forests are **visually inspected** (automatic screenshots in `docs/img/`); walking by hand with keyboard and mouse has **not been tried by a person**; multiplayer is **implemented, never run**.

## Not done / known gaps

* **Sea stacks and arches** (`SeaStacks.cs`, revision 35) are not ported: they are rock models placed off steep coasts, not ground, so the terrain is complete without them.
* The landforms are in the ground and water levels, but **the game does not draw them specially yet**: no falling water at waterfalls, no lava look (`FTerrainSample::Lava`), no scree stones (`Scree`), ponds and tarns drawn like any water.
* Landform settings (amount and size per module) exist in `FTerrainStyle::Landforms` but there is no screen to set them; every module runs as designed.
* No world-creation menu: the world is chosen on the command line.
* Trees have no collision, no wind, no felling, no middle-distance model (`_LOD1`), and stop at about 1.5 km (Thornwake's far forest is the ground's canopy colour, which is ported). The twelve placeholder plants (palms, cacti, cypresses and so on) have no models, so their biomes have no trees yet.
* No grass, flowers, bushes, rocks, logs, props, wildlife, gameplay or saves.
* **Seen beside the Godot dump, still different:** no grass, flowers, bushes, stones or logs; water is a flat tinted sheet (no ripples, foam, falling water or spray); cliffs are the bare terrain mesh (Thornwake dresses them with its rock kit); no light rays, mist or glow; the colour grading is a rough stand-in for "Fantasy"; Alden is still a block figure.
* Ground look, left out for now: ground relief and parallax (the close-up bumps), the plant-patch tint and plant-driven bare ground (need the flora fields), rain wetness and puddles, dug soil, lava, the second rock look where two biomes meet (one rock texture is used everywhere), rocky shore patches.
* The rock texture shows moire bands on large rock faces seen from far away.
* Exposure: Unreal's automatic exposure with a fixed correction (-0.7 stops) tuned against one Godot dump; the shade and sun limits did not behave as limits in tests and need looking into. Night, dusk and snow have not been looked at.
* The start place can be at the foot of a very tall valley wall (seed 7319: 700 m in 125 m). The heights are the generator's, and trees stand on such walls because Thornwake's slope test looks in one direction only; whether the Godot game shows the same has not been compared.
* Wetland pools are drawn too large on coarse distant tiles (visible in `docs/img/far-world-4096km.png`).
* The player walks on the bed of lakes and the sea (no swimming).
* Memory during fast travel still rises slowly (+0.7 GB over 24 km, flattening). It needs a long soak test before it can be called bounded.
* Input is polled from keys (no Enhanced Input assets yet).
* Terrain edits exist in the generator (`FTerrainEdits`) but nothing in the game calls them or rebuilds tiles.
* The engine logs two `LogEnhancedInput: Error ... Null player input` lines at start-up (not from project code; harmless so far).

## Commands that work

Package a standalone build (about 3 minutes; output in `Dist/Windows`, run `WayfarersWake.exe`; `Dist/` is not in git):

```
D:\EPIC\UE_5.9\Engine\Build\BatchFiles\RunUAT.bat BuildCookRun -project=H:\BIW\WayfarersWake\WayfarersWake.uproject -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory=H:\BIW\WayfarersWake\Dist -nocompileeditor -utf8output -unattended
```

Import Thornwake's art and build the materials (only needed again when the art or the ground shader changes; the results are in `Content/`): `Tools\ImportAssets.cmd`, then `Tools\CreateAssets.cmd`.

Test switches added with the look: `-WwLook=pitch,yaw`, `-WwUp=<m>` (height for `-WwAutoFly`), `-WwHour=`, `-WwEv=` and `-WwEvSun=` (exposure limits), `-WwFog=`, `-WwProfile` (log the generated heights around the start place).

```bat
rem Headless generator test (no engine): parity, sizes, tile timing
Tools\WorldGenTest\build.cmd
Intermediate\WorldGenTest\WorldGenTest.exe . --sizes --bench

rem Compile
D:\EPIC\UE_5.9\Engine\Build\BatchFiles\Build.bat WayfarersWakeEditor Win64 Development -Project=H:\BIW\WayfarersWake\WayfarersWake.uproject -WaitMutex

rem Recreate the material assets (only if Content/Materials is missing)
Tools\CreateAssets.cmd

rem Play
D:\EPIC\UE_5.9\Engine\Binaries\Win64\UnrealEditor.exe H:\BIW\WayfarersWake\WayfarersWake.uproject -game -windowed -ResX=1600 -ResY=900

rem In-engine bit-exactness check (exit code 0 = match)
D:\EPIC\UE_5.9\Engine\Binaries\Win64\UnrealEditor-Cmd.exe H:\BIW\WayfarersWake\WayfarersWake.uproject -game -nullrhi -WwSelfTest -unattended -stdout
```

World options: `-WwSeed=7319 -WwType=balanced -WwSizeKm=64 -WwRevision=44` (any revision from 8 to 44; 44 is the default).
Types: balanced, continental, archipelago, mountainous, many-continents, islands-mainland, vast-world; inland-sea, shattered, rift, twin-continents, peninsula; and natural-continents, natural-mainland, natural-archipelago, natural-mountains, natural-many-continents, natural-islands-mainland, natural-inland-sea, natural-shattered, natural-rift, natural-twin-continents, natural-peninsula. Sizes: 32, 64, 128, 256, 512, 768, 1024, 2048, 4096.

Test options: `-WwAt=<x>,<z>` (start place in metres), `-WwHighland` (start in mountain country), `-WwAutoFly=<m/s>`, `-WwAutoShot=<seconds>` (screenshot to `Saved/Screenshots/WwAutoShot.png`, then quit), `-WwStats` (streaming stats to the log every 5 s).

Controls: WASD, mouse look, Shift sprint, Space jump, F fly (E/Q up and down, Shift x8, Ctrl x40), Esc quit.

## Next concrete actions

1. Play it by hand; fix what walking shows (seams, snagging, camera). Visit the landforms: none has been looked at up close in Unreal.
2. Draw what the generator already knows: waterfalls, lava, scree, sea stacks.
4. World-creation screen (type, size, seed, terrain version) and a world map.
5. Compare the look with the Godot game side by side (Tom's play test); then grass and ground cover, tree collision, the middle tree model, the placeholder plants.
6. Long soak test for memory; benchmark scenes after `Content/WorldData/benchmarks.json`.
