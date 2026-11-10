# Development log

## 2026-11-10, session 1

* Audited Thornwake at `376d3f6`: sizes 32 to 4096 km confirmed; terrain is versioned with frozen fingerprints.
* Ported the generator to engine-free C++ (about 2,900 lines): 30 of 30 Thornwake fingerprints match for revisions 15 and 16.
* Created the UE 5.9 project. The first compile failed only at link (`USkyAtmosphereComponent` cannot be subclassed from a game module); fixed by setting its transform mode through reflection.
* The first launch crashed in the tile builder (adding an array's own element to itself); fixed.
* The game runs: generated world, streamed LOD terrain with collision, water surfaces, sky, block-figure player, HUD.
* 4096 km flight test: streaming holds at 400 m/s; found and fixed mesh-component garbage (pooling).
* Wrote the handoff documents and the devlog site (`docs/index.html`).
* Ported the landform modules, revisions 17 to 44, in order, each against its fixture: escarpments, waterfalls and gorges, canyons, cirques, glacial troughs, fjords and fjord coasts, volcanoes with lava, scree, headlands and coves, spits and lagoons, oxbow lakes, river terraces, alluvial fans, fall lips and plunge pools. 468 of 468 checks pass.
* Every module matched on its first run except three volcano cases, off by one bit in one height out of 1,089. Cause: the test linked the static C runtime, whose `pow`/`cos` differ in rare last bits from the system one .NET uses. Found by diffing against a scratch copy of the C# tests (DECISIONS D10, D11).
* Tom noticed the terrain looked flat in the test flight. Causes: the start-place finder chose the lowest, flattest ground it could find; near tiles had 2 m vertices where Thornwake has 1 m (fixed: 64 cells on the nearest level); no textures, trees or shading to show relief; and the natural world types were missing. The start place now prefers hill country.
* Ported the template and natural world types (`Landforms.cs`, `Heightmap.cs`, the rest of `WorldRecipe.cs`): 23 world types in all. Matched the C# generator on the first comparison; 42 reference blocks recorded from a scratch copy of Thornwake's tests are now a permanent check (`WorldGenTest --types`), which also covers resized worlds for the first time.
* The game now generates revision 44 by default; in-engine self-test matches; launched and flown over a 512 km mountain world.

## 2026-11-10, session 1 (continued): the look

* Tom: first a remake as close to the original as possible, then improvements that use Unreal (DECISIONS D13).
* Ported Thornwake's forest rules (`Vegetation.cs` to `WwVegetation`) and its classic ground shader (`TerrainBody.gdshaderinc` to `Tools/WwTerrainGround.hlsl`, built into `M_WwTerrain` by `Tools/CreateAssets.py`).
* Imported Thornwake's own art with `Tools/ImportAssets.py` (the source folder is only read): 17 tree species, full and distant models, and six rock textures.
* Unreal's procedural mesh has too few vertex channels for Thornwake's ground data; the biome colours go in a small texture per tile instead (D14).
* First launch with trees: every tree was default grey. The glTF importer hangs materials on engine materials that are not compiled for instancing; the script now moves them to two parents of our own.
* The first pictures were nearly black: the exposure setting worked the other way round from what I assumed (higher is brighter). Then sunlit ground was washed out while shade was right, so exposure now adapts within limits (D16).
* Several aerial screenshots showed only a rock wall. Two causes, neither in the terrain: my camera test switch read `-18,35` only up to the comma and was ignored, so every shot looked the same way; and the start place really is at the foot of a 700 m valley wall (checked against the generator's heights, `-WwProfile`). The same parsing mistake made `-WwAt=x,z` hang on the loading screen, which Tom saw; fixed.
* Result, inspected in screenshots: forested hills with the right species mix, rock on steep faces, sand at the shore, bare red country in the distance. Not yet compared with the Godot game side by side.
* Packaged the first standalone build (`Dist/Windows/WayfarersWake.exe`, Development, 980 MB). It needed the tree, terrain and material folders added to the always-cook list, because the code loads them by path. Launched once from the EXE: same picture as the editor build, 200 fps standing still, 2.05 GB.

## 2026-11-10, session 1 (continued): Tom's first play test

* Tom: still very bright and washed out; the terrain does not look like the Godot version; asked for an F9 dump like Thornwake's, in a different place. Everything in Thornwake is his own work; the ground textures are by Noor, an AI friend of his.
* Added F9: text dump plus screenshot in `Saved/Dumps`, with a `Reproduce:` line.
* Found Tom's Godot dumps (read only) and reproduced one, the waterfall above the gorge in the 512 km world, at the same place, hour and view. The generator agrees to the last digit shown (river surface 122.768 m here, 122.769 m there; Taiga 60.9 %; 4.925 C; 383.539 mm). The picture did not: about a stop too bright, pale grass, plain rock, flat water.
* The dumps show Tom plays with Noor's ground textures, which I had left out as "optional". Ported that style and the ground variety, with the sixteen textures as a texture array.
* Brightness: the exposure limits I set earlier turned out not to limit anything (changing them by a stop changed nothing in the picture); a plain correction of -0.7 stops does. Field of view now matches Thornwake (59 degrees vertical).
* Still different from the Godot picture, in order of how much it shows: grass and ground plants, water, rock-kit cliffs, light rays and mist.

## 2026-11-10, session 1 (continued): grass and plants

* Ported Thornwake's plant populations (`GroundCover.Flora.cs`, `Flora.cs`) as `WwFlora`: land grass in six communities, shore plants, floating leaves, underwater plants; 28 sprites imported as a texture array.
* Started at the place of a second Godot dump (the river shore): the same willows on the bank, grass tufts between them, eelgrass on the river bed.
* Half the tufts were black: Unreal turns the normal over on back faces, and the sprites' normals lean upward on purpose. The material now turns it back.
* Re-running the import script failed on assets that other assets point at (they cannot be deleted); it now updates them in place.
* Cost: 344,578 plants, 212 fps down to 90 hovering still at 1280x720. Plant shadows made no measurable difference.
* The packaged build's start place, in deep shade, was nearly black: the shade exposure limit set while fixing the washed-out look was too tight (the earlier note that the limits "did not limit anything" was wrong: the sunlit test scenes simply lay inside them). Loosened from 2.4 to 4.5 stops.

## 2026-11-10, session 1 (continued): water

* Ported Thornwake's water sheet (`TileBuilder.BuildWater`, `Flow`, `ReachFlow`) as `WwWaterSheet`: the shoreline is now where the sheet meets the ground, and every river vertex has a current.
* First port of the inland-water surface (`Tools/WwWater.hlsl`): ripples, currents, absorption and scatter, foam through Tom's two foam textures. It compiled and looked right on the first launch.
* At the two Godot dump places (river shore, waterfall lip) the picture now agrees in kind: white streaks down fast water, pale turquoise between them, a soft waterline. Mine is paler and more opaque.
* Frame rate unchanged by the new water (89-90 fps at the shore scene with all plants).
* Not done: the sea's waves, falling water, spray, standing waves, rocks in the current, caustics.

## 2026-11-10, session 1 (continued): waterfalls

* Ported `Waterfalls.cs`: the fall set, the cut-outs in the river sheet, the curtain mesh (free fall or sliding on rock, with depth) and the foam disc; `WaterFall.gdshader` as two materials. 275 falls in the 512 km test world.
* It looked right on the first launch: from above the lip the picture agrees with Tom's Godot dump of the same place; from below it is a wide streaked curtain with white water at its foot.
* One build hung for over ten minutes: four compiler processes sat idle under the build accelerator. Killing them made the build tool retry without it, and that passed. Not a code fault; if it happens again, kill `cl.exe`.
* Not done: spray and mist, the curtain's relief and back-light.
