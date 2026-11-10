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
| Water sheet (Thornwake's mesh) | **Ported, compared with two Godot dumps** | `WwWaterSheet`: one sheet per tile on the vertex grid, banks carried under the ground, the sea reaching under the beach, edge fade, kind codes, river-water share, river currents from the reach's fall and depth. The saw-tooth shoreline is gone |
| Waterfalls (Thornwake's fall set and falling water) | **Ported, compared with one Godot dump** | `WwWaterfalls`: every river drop of 3 m or more steeper than 1 in 5 (275 in the 512 km test world); the river sheet is cut away at the face; a curtain falls freely from the lip or slides on the rock, with depth and sides, and white water spreads at the foot. Shown within 4 km. At the lip of Godot dump `dump-20261109-133922` the view agrees; seen from below once. Spray and mist are not ported |
| Waterfall spray and mist | **Ported, visually inspected** | `WwSpray`: Thornwake's emitters (foot cloud; on falls over 8 m also face spray, side veils, lip spray) as camera-facing instances moved on the game thread; the drawn billowing puff as `M_WwSpray`. Within 1.5 km |
| Stones, rocks, boulders, cliff slabs, scree, rocky shores; bushes, stumps, logs | **Ported, visually inspected** | `WwRocks` (Thornwake's `GroundCover.Rocks`, `Props`, `RockyShore`), shapes built in code. 8,600-37,000 around the player in the test scenes, no measurable cost. The lumps of the small rocks differ from Thornwake's (its shapes use Godot's own random numbers) |
| Cliff rocks and the rock kit (columns, slab stacks, tiered piles, tors) | **Ported, compared with one Godot dump** | Worn cliff blocks up tall faces, a broken lip, blocks at the foot; formations in outcrops. Surface nets and the kit's polyhedra ported; the shapes use the generator's own random stream, so they should be Thornwake's exactly (not checked vertex by vertex). `M_WwRockBig` with Tom's six cliff textures |
| Wind on plants and tree crowns | **Ported, motion measured** | Gust patches carried downwind, lean, slow swing; underwater sway; floating leaves bob. Two captures a second apart differ in grass and crowns, not in the background. The wind itself is fixed (5.5 m/s from the north-west) |
| Collision with trunks, rocks, cliff blocks, props | **First version, counted** | On the nearest tiles: trunk cylinders of Thornwake's radii, boxes for rocks and props. 140 solid objects within 30 m of the start place (F9 dump). Not walked into by hand |
| Swimming; Thornwake's movement speeds | **Ported, measured** | Swim in water over 1.25 m deep: the player floats with feet 1.3 m under the surface (measured in a dump), Space up, C dive. Walk 5, sprint 11, fly 45 / 220 m/s |
| The sea's waves (Thornwake's simulated sea, its "experimental 4" water as Tom plays it) | **Ported, checked by numbers and by eye** | `WwOcean`: three cascades (480, 59, 7.9 m tiles, 256 x 256) of a wind sea plus a swell, choppy, with a foam field born where the surface folds, drifting with the wind. Thornwake runs it on the graphics card; here on worker threads (about 3 ms across them a frame, 15 ms on one). Headless checks: wave height 3.96 m where the sea state asks for 3.97 m; the surface swimming uses agrees with the textures; the sea repeats after 600 s. The sheet is moved by the waves and lit per pixel. Two captures a second apart differ in the sea, not in land or sky. Sea state fixed at 0.5 |
| Shore waves, breakers, swash | **Ported, visually inspected** | Shore data per water vertex (the bed's uphill direction, how open the coast is: `FShore`, after `WaterShore.cs`); crests along the depth contours rolling in, a foam band on each breaking crest with a trail behind it, light through the crest, the last of each wave running up the beach. Seen from above and from the surf on one sandy beach |
| Sea foam (the foam net) and churning at rocky coasts | **Ported, compared with one Godot dump** | `FFoamNet` (Thornwake's `FoamNet.cs`, made when the game starts); lip, fresh foam and old streaks from the simulated foam field. At Godot dump `dump-20261108-154929` (a rocky shore) the white net round the rocks looks alike; mine is wider, the water brighter and bluer, the cliff paler |
| Swimming on the waves | **Ported, measured** | The player rides the sea's surface (the simulated long waves plus the shore wave, worked out on the CPU as Thornwake does): feet 1.40 m under still level with the surface 0.09 m below it, 2 cm from the 1.3 m rule (F9 dump). Not swum by hand |
| Water surface (inland water) | **First port, compared with two Godot dumps** | `M_WwWater`: wind ripples and chop, river ripples carried by the current, bank turbulence, the bed seen through the water with per-colour absorption, rapids and streak foam through Tom's foam textures, contact foam, stirred-up milky water. Looks alike in the two dump scenes; mine reads paler and more opaque |
| Sky, sun, clouds, fog, time of day | First version | Engine sky atmosphere and volumetric clouds; the planet top follows the view |
| Ground look (Thornwake's ground shader, both styles) | **Ported, compared with one Godot dump** | `M_WwTerrain`: biome grass / dry / rock colours, forest floor, rock texture on slopes and sheer faces, scree, beach sand, shore mud and gravel, wetland beds, snow; Noor's sixteen ground textures and the ground-variety patches (the style Tom plays with; parameter `GroundStyle`, x = 0 gives the classic colours). Compared at the place of Tom's Godot dump `dump-20261109-133922`: same landscape and trees; still different: see the gaps |
| Grass and small plants (Thornwake's plant populations) | **Ported, compared with one Godot dump** | `WwFlora`: six grass communities in noise-shaped patches, reeds, cattails and rushes on banks, floating leaves, underwater plants, seagrass and kelp; 28 sprites; grass density 3 as Tom plays (`-WwGrass=`). At the place of Godot dump `dump-20261109-153359`: 344,578 plants on the nearest tiles, 90 fps at 1280x720 (210 without plants). Rules ported line by line but not checked plant by plant against Thornwake |
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
* Trees have no felling, no middle-distance model (`_LOD1`), and stop at about 1.5 km (Thornwake's far forest is the ground's canopy colour, which is ported). The twelve placeholder plants (palms, cacti, cypresses and so on) have no models, so their biomes have no trees yet.
* No wildlife, gameplay or saves. No greenery on cliff rocks, no hanging grass and roots at cliff lips (`CliffDressing`), no spray round river stones; the rock kit's formations have no collision; rocks collide as boxes, not their own shape; stones and berries cannot be picked up.
* Plants have no leaf translucency, cannot be pulled or trampled, and reach only as far as the nearest tiles (about 100-190 m; Thornwake's setting is 400 m). Water hyacinth (bayou) is not ported.
* Water, not ported: the curtain's bump relief and the light through white water (in a cliff's shade the falling water looks blue), hiding the curtain behind the river above the lip, standing waves, foam round rocks, wakes, caustics, rain rings, the shader's own sky and screen-space reflections (the engine's are used instead, and its sun glints), biome water tints, storms, the underwater view (from below the sea's surface is not drawn at all), the fade between sea states. The wind and the sea state are fixed (5.5 m/s from the north-west, sea state 0.5: waves about 3.7 m high in open water).
* The sea, known differences: within about 80 m only waves 10 m and longer move the mesh (Thornwake: 5 m; the nearest tiles here have 2 m squares), shorter ones are lighting only. Lakes and wide rivers now carry the simulated chop too: not looked at. Small dark facets show in the water's reflections close by (they mirror the dark below the horizon). Sea stacks are not ported, so rocky coasts lack their stacks.
* Shadows: back since 2026-11-10 (they had been missing since the grass round, see D24). They are harder and blacker than Thornwake's: its tree crowns let light through and its environment adds ambient occlusion and other post-processing (Tom). For the look round.
* **Seen beside the Godot dumps, still different:** river water reads paler and more opaque than Thornwake's, lakes a stronger cyan, the sea brighter and bluer (Thornwake's is a darker green-teal); coastal cliffs are pale where Thornwake's are red-orange; no light rays, mist or glow; the colour grading is a rough stand-in for "Fantasy"; Alden is still a block figure.
* Ground look, left out for now: ground relief and parallax (the close-up bumps), the plant-patch tint and plant-driven bare ground (need the flora fields), rain wetness and puddles, dug soil, lava, the second rock look where two biomes meet (one rock texture is used everywhere), rocky shore patches.
* The rock texture shows moire bands on large rock faces seen from far away.
* Exposure: Unreal's automatic exposure with a fixed correction (-0.7 stops) between a shade limit and a sun limit, tuned by eye on three scenes (two Godot dumps in the sun, the shaded start place). The first shade limit was too tight and left the start place nearly black; it is looser now and may be a little bright and blue in deep shade. Night, dusk and snow have not been looked at.
* The start place can be at the foot of a very tall valley wall (seed 7319: 700 m in 125 m). The heights are the generator's, and trees stand on such walls because Thornwake's slope test looks in one direction only; whether the Godot game shows the same has not been compared.
* Wetland pools are drawn too large on coarse distant tiles (visible in `docs/img/far-world-4096km.png`).
* Swimming leaves out the waves under about 20 m and the swash (as Thornwake's does); no underwater view.
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

Test options: `-WwAt=<x>,<z>` (start place in metres), `-WwCoast` (start over the nearest open, shelving sea shore), `-WwLevels=<n>` / `-WwTreeLevels=<n>` (how many tile sizes; how far trees reach), `-WwSun=<x>` (the sun's strength), `-WwNoSunShadows`, `-WwWaterDebug=<1-3>` (show the water's foam, shore data or wave phase), `-WwHighland` (start in mountain country), `-WwAutoFly=<m/s>`, `-WwAutoShot=<seconds>` (screenshot to `Saved/Screenshots/WwAutoShot.png`, then quit), `-WwStats` (streaming stats to the log every 5 s).

Controls: WASD, mouse look, Shift sprint, Space jump, F fly (E/Q up and down, Shift x8, Ctrl x40), Esc quit.

## Next concrete actions

1. Play it by hand; fix what walking shows (seams, snagging, camera). Visit the landforms: none has been looked at up close in Unreal.
2. Draw what the generator already knows: waterfalls, lava, scree, sea stacks.
4. World-creation screen (type, size, seed, terrain version) and a world map.
5. Tom's play test; then the underwater view, far tree cards and the middle tree model, light rays and mist, the wind and weather simulation.
6. Long soak test for memory; benchmark scenes after `Content/WorldData/benchmarks.json`.
