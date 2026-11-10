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

## 2026-11-10, session 1 (continued): "everything that is not there yet", in order

* Tom: continue with everything missing, in the planned order.
* Waterfall spray and mist: a small emitter system in code; the first try drew solid white squares (the puff's shape never reached the opacity: a wrongly named pin). Fixed.
* Stones, rocks, boulders, river and bank stones, cliff slabs, scree, rocky shores, bushes, stumps, logs: rules and shapes ported; meshes built at start-up.
* Cliff rocks and the rock kit: surface nets, the worn cliff blocks and the four kinds of formation ported. First launch crashed on start: adding an array's own element to itself, the same mistake as on day one (`TreeMeshes.Add(TreeMeshes[i])`). Then the rebuild failed because the crashed process still held the game DLL; killed it.
* Wind on grass and tree crowns. A failed run of the import script left all 86 tree materials without a parent (it deleted the parents before recreating them); the script now updates parents in place and can reattach instances from their own stored texture. The plant material then failed to compile (the wind read a value that exists only in the pixel stage): grey squares for grass. Fixed.
* Collision for trunks, rocks and props on the nearest tiles; the F9 dump counts solid objects nearby.
* Swimming: switched on correctly but the player stayed on the river bed; the engine's braking cancelled the swim velocity nearly every frame. Braking is off while swimming. Movement speeds set to Thornwake's.
* Not reached: the sea's waves and shore, the underwater view, far tree cards, the middle tree model.
* Frame rate at the river-shore scene, 1280x720, hovering: 90 fps before this round, 74 after (the wind's moving shadows cost most of that).

## 2026-11-10, session 1 (continued): the sea

* Tom: "Yes, the sea."
* Read Thornwake's water shader again in full, its FFT ocean and compute shaders, `WaterShore.cs`, `FoamNet.cs`. Tom plays "experimental 4" water with the foam net, so that is what was ported, not the older wave sets.
* The simulated sea as engine-free code on worker threads (D22), with headless checks. The wave height came out right first time (3.96 m against 3.97 m).
* The water material gained a vertex stage (the waves move the sheet), shore waves, breakers, the foam net, swash, light through crests. Swimming rides the waves.
* Things that went wrong: adding one generator file reshuffled which files compile together, and three old name clashes surfaced (fixed by renaming). The build accelerator hung again (two compiler processes idle for ten minutes); builds now run without it. The first water material used more than 16 texture samplers and fell back to the default grey; the cascade textures now share one sampler. My patch to the material script changed the wrong function (same text twice in the file).
* Believed for a while that breakers were missing on the sandy test beach: from 200 m at a low angle they do not show. From above they are there. A debug view (`-WwWaterDebug=`) was added on the way; its dark blotches fooled me into suspecting bad shore data, which a dump disproved.
* Compared with Tom's Godot dump of a rocky shore (`dump-20261108-154929`): the foam net round the rocks looks alike; my water is brighter and bluer, my cliff paler, and the sea stacks are missing.
* Not done: underwater view, far tree cards, the middle tree model, light rays and mist, weather.
* The packaged game had no clouds (the engine's cloud material was never packaged: every EXE so far, not noticed before) and logged a warning once from the rock meshes built in code. Both fixed; the packaged game was launched in the surf: clouds, sea, 143 fps, a clean log.

## 2026-11-10, session 1 (continued): the missing shadows

* Tom played the build: "there were no shadows", and later the clue that solved it: they flash for a split second right after the loading screen, then go.
* True, and not new: the devlog pictures since the grass round have none. I never noticed. Only the first forest picture has them.
* Not the packaging (the editor build is the same), not the time of day, not the light (its state is as set), not the shadow method. They vanish when terrain tiles of 1 km and larger arrive. Stopping those tiles, the tree batches and the rock batches from casting changed nothing; the engine's own shadow dump (`r.DumpShadows`) was identical with and without them.
* Cause: the engine's octree culling of shadow casters. With the far tiles in the scene it finds no casters at all. `r.Shadow.UseOctreeForCulling=0` (test every component instead; there are a few hundred) brings every shadow back. Why the octree fails was not looked into further.
* Cost: Tom's forest view, 1280x720: 86 fps without shadows, 67 with.
* Also seen on the way: the sun's shadows are the classic cascades out to 600 m; virtual shadow maps are switched on in the config but are not what draws them. Left as it is (Godot's are cascades too).
* Tom, same day: the Godot game's look has more to it (its environment's post-processing, perhaps ambient occlusion; tree crowns let light through, so shadows under them were not as black). Noted for the look round, after the missing features.

## 2026-11-10, session 1 (continued): the underwater view

* Thornwake's full-screen underwater pass as a post-process material (before the bloom), switched on by the sky actor when the camera is under a water surface; the sea's light pattern reads the simulated sea's slope textures.
* It worked on the first run. The exposure lifted the dim uniform water into a bright fog; its shade limit is now held near the sunlit one while submerged. Still bright.
* Seen: sea at 3 m down, level and looking up (shafts fanning from the surface); a river from its bed (green, murky, waterweed with the net of light on it). 132-190 fps in the sea views, 55 in the river scene (54 just above the water there, with shadows now on).

## 2026-11-10, session 1 (continued): distant forests, and a change of course

* Tom on the water: Godot has depth transparency and depth colour. Both are ported with Thornwake's numbers; sampled beside his rocky-shore picture mine is a more saturated teal (10, 73, 66 against 58, 95, 77). Left for the look round.
* Tom, before a walk with the dog: the renderers differ too much; port the mechanics and features without digging deep into graphics; if it is not the same it does not matter much; the Unreal-native version could later be a branch or fork; first finish the port. Recorded as D25.
* So distant forests went in the short way: Thornwake's card placement and drawn silhouettes on crossed quads, riding the rock batches. Its impostor bake and fades were read and set aside.
* The build hung again, this time with the accelerator already off: two compiler processes idle for ten minutes. Killing them fails the build; running it again works.

## 2026-11-10, session 1 (continued): the first game rules

* With Tom away and features first (D25): the system audit's game side brought up to date and the gameplay port laid out in nine steps.
* Thornwake keeps its rules in a small engine-free layer (`Game/Gameplay`, about 2,200 lines) that its game talks to as host and client, alone or networked, and it has tests for them. So the port starts there: step 1's rules as engine-free C++ beside the generator, and Thornwake's tests for them ported as checks. 84 pass.
* The engine builds without exceptions, so the catalogue reports a damaged content file through an error text instead of throwing.
* Not done: any of it in the game. Next: the game's side (who is host, the pack and needs on screen, E to gather, the axe), which also needs the gatherable stones and berry bushes of the ground cover and the removals to reach the tile builder.

## 2026-11-10, session 1 (continued): something to do

* Tom explained where Thornwake is going: atmosphere first, the present crafting and building only placeholders for multiplayer tests, multiplayer last. He asked for the light placeholder mechanics all the same, so there is something to do while walking (D26).
* `FWwGameplay` runs the ported rules for the player: needs, the pack, E, the axe, eating, dying. The streamer takes felled trees out of its batches and draws trunks and piles as plain shapes.
* A scripted run (`-WwPlayTest`) walks to the nearest tree and plays: fells it in 8 hits, cuts the trunk in 12, picks up 7 logs. The first run stood out of reach of any tree and proved only the grass and the eating.

## 2026-11-10, session 1 (continued): day, night and weather

* Tom: yes, day and weather next.
* Clock, weather (the simple fronts model) and wind ported as engine-free code with 12 headless checks; then the sky actor: sun and moon from the clock, a star dome, the engine's clouds driven by the weather, cloud shadows, haze, rain and snow round the camera, lightning flashes; rain, wind and season reach the needs.
* Lost an hour to a black picture that had nothing to do with the new code: my test camera stood inside a tree's crown. Found by taking the weather code out and seeing it was still black.
* The engine's cloud material has no documentation in reach, so its coverage number was measured: four pictures from one viewpoint (-0.9 none, -0.4 scattered, 0.1 heavy, 0.6 a smooth deck). Before measuring I guessed its direction wrong twice, misled by a full deck that looks like smooth blue sky.
* The star dome hid the real sky by day (a mesh drawn as sky stands in front of it); it now adds the sky's own light itself.
* Night was black, then a blue day; settled on a dim blue night with moon shadows and faint stars.
* Not done: wind in the plant, tree and water materials (still the fixed wind), rain sound, wet ground, the weather field Tom plays with.

## 2026-11-10, session 1 (continued): sound

* Tom: yes, sound next.
* Thornwake has no sound files at all: rain and wind are a noise synthesiser, thunder is built from the bolt, and a handful of short sounds are a sine and noise. So there was nothing to copy and no licence to record; the synthesiser was ported as engine-free code with 10 checks and a file to listen to.
* Added what Thornwake lacks and the place asks for: water sound near rivers, the sea and lakes, and a dull world under water.
* In the game it is one procedural sound wave fed a third of a second ahead. Proof it plays: the seconds handed over keep pace with real time (the queue only takes more when the sound card has drained it), 25.4 s in 25 s.
* I cannot hear. Whether it sounds right is for Tom's ears; the levels are his Thornwake settings.
* Not done: sounds placed in the world, shelter under trees, birds (none in Thornwake).
* Tom listened: the rain sounds exactly as in the Godot version.

## 2026-11-10, session 1 (continued): the menu

* Tom asked for a menu like Thornwake's for testing features and tuning, better if I could. Read Thornwake's (seven tabs, about 140 rows, a weather test page), proposed four short pages with a search, and he said build it.
* One list of settings, a Slate menu drawn from it, and every old test switch routed through the list. Fifty-odd entries; the sound's volumes became fields so the menu can set them.
* Tom watched a test window sit on "Raising the ground under your feet" and said so. Cause: the new level's game mode saw the "Make this world" press as new and reopened the level, for ever. Watchers now start from the present count.
* Two builds stalled in the compiler again (two idle processes); killed, run again, fine.
* Checked by script, not by hand: Esc in and out, settings taking effect, going to a place, making a world, the file.

## 2026-11-10, session 1 (continued): the map, and the world page

* Tom asked for a map: nicer than Thornwake's if possible, his position and direction on it, zoom, pan, right click to teleport exactly, and two looks, Thornwake's and an old map on parchment.
* The picture maker is engine-free and reads the real terrain when zoomed in; the first close view was black with shading (real slopes are far steeper than the plan's), so the shading now eases off with zoom and levels off on cliffs.
* A scripted right click into the open sea put the player on the sea bed 427 m down. Arrivals over water are now at the surface.
* Tom played while I built: he could not drag the time of day (he had to type it). The spin box I had used wants a mouse mode the game window does not give it; replaced with a plain bar and a number box. He also asked for a map preview before making a world and all of Thornwake's generation settings: 76 settings ported with their arithmetic and checked bit for bit against the old path, and a preview panel on the World page.
* Tom then set a new direction for after this: Wayfarer's Wake as its own game, with proper data-driven gameplay, a jointed character with animations, no staff or pack on the base figure, and customisable characters by race from his reference sheets (`docs/ConceptArt/Thornwake_Race_Reference_Sheets`). Not started; a plan comes first.
