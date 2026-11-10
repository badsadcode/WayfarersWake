# Migration status

_Last updated 2026-11-10 (session 1, after the landform port). Keep this current: it is where the next session starts._

## What works (verified)

| Area | State | Evidence |
|---|---|---|
| World generator, terrain revisions 8-44 (Thornwake's current) | **Bit-identical to Thornwake** | `Tools/WorldGenTest`: 468/468 checks, every frozen fingerprint from revision 15 to 44 (31 fixture sets x 5 profiles x 3 seeds) |
| Same generator inside the engine build | Bit-identical | `-WwSelfTest` logs `MATCH` at revision 44 |
| World sizes 32 ... 4096 km | All nine build | `WorldGenTest --sizes` (table in BENCHMARK.md); 4096 km also launched in game |
| UE 5.9 project | Compiles, launches | `Build.bat WayfarersWakeEditor Win64 Development` succeeds; game runs with `-game` |
| Streamed terrain (quadtree LOD, 9 levels) | Works, no holes seen | 330 tiles around the player; 24 km flight at 400 m/s with 0-10 tiles missing |
| Terrain collision + player | Works | Player is placed by a line trace onto async-cooked tile collision (trace hit 77.7 m, generated 77.8 m) |
| Water surfaces (sea, lakes, rivers, ponds) | First version | Flat translucent surfaces at the generator's levels; no waves, flow, foam or swimming |
| Sky, sun, clouds, fog, time of day | First version | Engine sky atmosphere and volumetric clouds; the planet top follows the view |
| Multiplayer shape | Designed in, never run | World settings replicate through `AWwGameState`; every machine rebuilds terrain locally |

In the brief's terms: the generator is **tested**; the project is **compiled and launched**; terrain, collision and sky are **visually inspected** (two automatic screenshots in `docs/img/`); walking by hand with keyboard and mouse has **not been tried by a person**; multiplayer is **implemented, never run**.

## Not done / known gaps

* **Template and "natural" world types are not ported** (`Landforms.cs`, `Heightmap.cs`): only the seven classic placed-continent types exist. This is now the largest gap in the generator. Thornwake's fixtures cover only the five classic profiles, so these types will need new reference hashes from a scratch copy of the C# tests (see DECISIONS D10).
* **Sea stacks and arches** (`SeaStacks.cs`, revision 35) are not ported: they are rock models placed off steep coasts, not ground, so the terrain is complete without them.
* The landforms are in the ground and water levels, but **the game does not draw them specially yet**: no falling water at waterfalls, no lava look (`FTerrainSample::Lava`), no scree stones (`Scree`), ponds and tarns drawn like any water.
* Landform settings (amount and size per module) exist in `FTerrainStyle::Landforms` but there is no screen to set them; every module runs as designed.
* No world-creation menu: the world is chosen on the command line.
* No vegetation, rocks, props, wildlife, gameplay or saves.
* The ground look is a placeholder (vertex colours from the biome palettes). Scene exposure is too bright and pale.
* Wetland pools are drawn too large on coarse distant tiles (visible in `docs/img/far-world-4096km.png`).
* The player walks on the bed of lakes and the sea (no swimming).
* Memory during fast travel still rises slowly (+0.7 GB over 24 km, flattening). It needs a long soak test before it can be called bounded.
* Input is polled from keys (no Enhanced Input assets yet).
* Terrain edits exist in the generator (`FTerrainEdits`) but nothing in the game calls them or rebuilds tiles.
* The engine logs two `LogEnhancedInput: Error ... Null player input` lines at start-up (not from project code; harmless so far).

## Commands that work

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
Types: balanced, continental, archipelago, mountainous, many-continents, islands-mainland, vast-world. Sizes: 32, 64, 128, 256, 512, 768, 1024, 2048, 4096.

Test options: `-WwAt=<x>,<z>` (start place in metres), `-WwAutoFly=<m/s>`, `-WwAutoShot=<seconds>` (screenshot to `Saved/Screenshots/WwAutoShot.png`, then quit), `-WwStats` (streaming stats to the log every 5 s).

Controls: WASD, mouse look, Shift sprint, Space jump, F fly (E/Q up and down, Shift x8, Ctrl x40), Esc quit.

## Next concrete actions

1. Play it by hand; fix what walking shows (seams, snagging, camera). Visit the landforms: none has been looked at up close in Unreal.
2. Port `Heightmap.cs` and the template `Landforms.cs` so the natural world types exist; record reference hashes for them from the scratch C# copy first.
3. Draw what the generator already knows: waterfalls, lava, scree, sea stacks.
4. World-creation screen (type, size, seed, terrain version) and a world map.
5. Ground materials, trees and grass from the biome data; fix exposure.
6. Long soak test for memory; benchmark scenes after `Content/WorldData/benchmarks.json`.
