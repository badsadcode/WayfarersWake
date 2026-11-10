# Decisions

**D1. The generator is engine-independent C++ in `Source/WayfarersWake/WorldGen/`.**
Standard library only, no Unreal headers. It compiles into the game module and, unchanged, into a console test (`Tools/WorldGenTest`) that builds in seconds. Reason: parity with Thornwake can be tested without starting the engine.

**D2. Port by terrain revision, starting at 16, and prove each with Thornwake's own fingerprints.**
Thornwake versions its terrain and freezes a SHA-256 fingerprint per revision. Revision 16 is the whole pipeline (layout, erosion, rivers, lakes, climate, biomes, sampler, wetland pools, dunes, mesas, spires) before the landform modules of revisions 17-44. Porting to a frozen revision gives an exact pass or fail instead of "looks similar". Later revisions are added in order, each with its fixture. A world records its revision, so adding revisions never reshapes an existing world.

**D3. Bit-exactness is a requirement, and it has consequences.**
* The module is built with `FPSemantics = Precise` (Unreal's default is fast-math), and the core header sets `float_control(precise)` and `fp_contract(off)`.
* .NET's `PriorityQueue` (a 4-ary heap, not stable) is replicated exactly: the order in which equal priorities leave it decides river courses on flat ground.
* Single-precision sub-expressions of the C# source are kept in single precision.
* `System.Text.Json`'s number and string formatting is replicated for the profile digest, which seeds everything.
* A few functions use the C runtime (`pow`, `log`, `exp`, `cos` in biome fitness, pools and dunes). They match .NET on Windows x64. **Other platforms are unverified** and would need deterministic replacements, as the source already has for sin, cos, atan and exp.

**D10. Finding a parity mismatch: diff against a scratch copy of the C# tests.**
When a fingerprint differs, copy Thornwake's `Game/WorldGen`, `Game/Simulation`, `Game/Gameplay`, `Game/Core`, `Content` and `Tests/WorldGen` to a scratch folder (never build in the source), add an empty `project.godot` at its root, and add a `--dump=profile,seed,revision[,gridSpacing]` option to its `Program.cs` that prints what `Tools/WorldGenTest/dump.cpp` prints: a hash per height grid and per raster, the volcanoes, the lakes, and optionally every height of one grid as raw bits. Diff the two outputs. This found the one real problem of the landform port in minutes: a single height off by one bit.

**D11. The C runtime is part of the generator's contract.**
That one-bit difference came from `pow`/`cos` in the *statically linked* C runtime, which differ in rare last bits from the system `ucrtbase.dll` that .NET uses. The parity test is therefore linked with `/MD`; Unreal modules already are. The landform modules of revisions 18+ use `pow`, `cos`, `sin`, `atan2`, `acos` and `remainder` heavily, so on another platform or runtime their terrain may differ in the last bit until those calls get deterministic replacements. Windows x64 with the system runtime is verified.

**D12. Lazy state in the source is computed up front here.**
Thornwake traces lava flows and reads crater-lake levels the first time a sample needs them, under a lock. Here `FVolcanoes::Finalize()` does it at the end of plan construction, and the river-coast distances are filled in the sampler's constructor. Same values (the fixtures prove it), and sampling touches no shared mutable state.

**D4. Terrain is custom streamed procedural meshes, not Landscape or World Partition.**
Landscape cannot be created from a seed at runtime, and World Partition streams authored cells. The world here is a pure function of position, so tiles are built on worker threads from the sampler and dropped when out of view. `UProceduralMeshComponent` was chosen for the first version (simple, async collision cooking). To revisit with measurements: `UDynamicMeshComponent` or a custom vertex factory for cheaper updates.

**D5. Quadtree LOD with Thornwake's tile geometry.**
Level 0 tiles are 64 m with 32 cells (2 m); each level doubles; 9 levels (16 km roots, about 24 km of view). A tile splits when a viewer is within 1.5 tile sizes. Old tiles stay until everything replacing them is ready. LOD seams are hidden by skirts. Collision on the two finest levels only. Mesh components are pooled.

**D6. No origin rebasing yet: Large World Coordinates with tile-local vertices.**
Tile vertices are floats relative to the tile's corner; tile transforms are doubles. Rendering and physics were stable in a flight test 1,057 km from the origin in a 4096 km world. Niagara, water materials and networking precision at that distance are untested. The sky's planet top follows the view, because Unreal's default pins the planet under the world origin.

**D7. The world is defined by four values and rebuilt on every machine.**
Seed, world type, size and terrain revision replicate through the game state; clients build the plan and stream their own terrain. Only changes (edits, buildings, removals) will ever be sent or saved.

**D8. The game is code-driven: no authored map.**
The default map is the engine's empty `Entry`; the game mode and game state spawn the sky and the terrain streamer. Material assets are created by a script (`Tools/CreateAssets.py`) so they can be regenerated.

**D9. Coordinates.**
Generator metres: X east, Z south, height up (Thornwake's). Unreal: X = east, Y = south, Z = up, centimetres. No mirroring.

**D13. Phase order: a faithful remake first, Unreal's own strengths second.**
Tom, 10 November 2026: first remake the game in Unreal as close to the original as possible; improving it with what UE 5.9 offers comes after. So the look is ported from Thornwake's shaders and rules, not rebuilt from Unreal features. Ideas for the second phase are noted, not acted on.

**D14. Ground colours travel as a small texture per tile.**
Thornwake gives every ground vertex sixteen extra numbers (four custom channels). Unreal's procedural mesh has four half-precision UV pairs and one 8-bit colour, which is not enough, so the three biome colour sets (grass + snow, dry ground + plant cover, rock + wetness) go into one small texture per tile (three squares side by side, one texel per vertex, bilinear), read by the material through a pooled material instance. The remaining values ride in the vertex colour and two UV pairs. The shader maths is Thornwake's, in display space, converted at the end.

**D15. Trees are instances in 16 km squares, shown with their tile.**
Each terrain tile carries its trees and adds them to shared instanced-mesh components when it becomes visible, removes them when it leaves; so a tree stands exactly once whatever mix of levels is on screen. Components cover 16 km squares because instance positions are single precision (about 2 mm at the far edge). Levels 0-1 use the full model, levels 2-3 the distant one.

**D16. Exposure adapts within limits.**
A fixed exposure could not serve both a shaded forest floor and open sunlit ground (either black or washed out). Thornwake adapts; so does this: Unreal's histogram exposure clamped between a shade limit and a sun limit, both falling at night. Motion blur is off. Haze is light (fog density 0.0002): the first value, 0.004, greyed out everything beyond a kilometre.

**D17. Tom's Godot dumps are the reference for the look.**
Thornwake's F9 writes a text dump and a screenshot to `%APPDATA%\Thornwake\debug` (several hundred exist). Each has a `Reproduce:` line; the same place, hour and view can be started here with `-WwSizeKm= -WwAt= -WwLook= -WwHour=` (Godot's yaw and pitch are radians; Unreal yaw = atan2(-cos yaw, -sin yaw) in degrees). Those dumps also record Tom's settings: Noor's ground textures, ground variety, the rock kit, plant populations, "Fantasy" grading at 0.6, 59 degrees vertical field of view. That is the look to match, not the shader's defaults. This game's own F9 writes to `Saved/Dumps`, never to Thornwake's folder.

**D18. Grass is instanced per tile, with the sprite and colour as per-instance numbers.**
Thornwake draws all plants of a tile as one multimesh with a per-instance colour and sprite layer. Here each nearest-level tile owns two pooled instanced-mesh components (upright sprites, floating leaves); every plant carries four numbers (sprite layer, colour) read by one material from a texture array of the 28 sprites. Transforms and numbers are prepared on the worker thread with the tile; the game thread only hands them over. The sprite meshes are Thornwake's (three crossed, leaning quads; one flat quad), generated as glTF by a script and imported.

**D19. Water: Thornwake's mesh exactly, its shader in parts, the engine's reflections.**
Thornwake's water is a 1,400-line forward shader that reads the screen and depth buffers itself, marches its own reflections and, for the sea, runs a wave simulation. The mesh rules (`BuildWater`, `Flow`) are ported whole. The surface is ported in parts, inland water first: the ripple, current, absorption, scatter and foam maths are Thornwake's lines; the scene behind the water comes from Unreal's scene colour and depth; reflections are left to the engine (specular 0.5, roughness 0.02 as Thornwake sets them) instead of the shader's own sky and screen-space march. The albedo / emission split is Thornwake's: the water body's own colour is lit, the bed seen through it is added as emission, so opacity is 1 except at the edge film. The sea waits for its own port.

**D20. Shapes made in code are built into meshes when the game starts.**
Thornwake has no model files for rocks, cliff blocks, formations, bushes, stumps or logs: it builds them in code. So does this game: the engine-free generators (`WwRocks`) produce triangle soups, and `WwBuildStaticMesh` turns each into a static mesh at start-up (`UStaticMesh::BuildFromMeshDescriptions`, fast build). About 45 meshes, well under a second. Nothing is baked into assets, so a change to a shape needs no import step. Rocks and props then ride the trees' instance batches (16 km squares, added and removed with their tile), with the rock colour and look as per-instance numbers.

**D21. Particles without particle assets.**
Unreal's particle systems are authored in the editor; this project is built from code and scripts. Waterfall spray is therefore a small emitter of its own (a few hundred puffs a fall, moved on the game thread, drawn as one set of camera-facing instances with the puff drawn by the material). If particle counts grow, this is the place a real particle system would pay off.

**D22. The simulated sea runs on worker threads, not on the graphics card.**
Thornwake's sea is a Tessendorf FFT ocean computed by four compute shaders every frame. Unreal compute shaders need shader files of their own, registered by a module that loads before the engine's shaders (in practice a plugin), which this code-and-script project does not have yet. The sea's spectrum is sparse (about 12,000 waves in three 256 x 256 cascades), so `WwOcean` transforms only the rows that hold waves, on worker threads, and sends the finished moment to six textures a frame later: about 3 ms spread over the workers, 15 ms on a single thread. It is engine-free like the rest of the generator, so the headless test checks it, and swimming reads the same waves on the CPU. If the workers get crowded (tile building uses them too), moving it to the graphics card is the next step; the textures and the shader would not change.

**D23. One sea state for now.**
Thornwake's sea follows its weather (sea state 0 to 1; Tom's setting is 0.5). Weather is not ported, so the sea state is fixed at 0.5 and the wind at 5.5 m/s from the north-west, the values in Tom's dumps. The fade between sea states is left out until something can change them.

**D24. Shadow casters are found without the scene octree.**
`r.Shadow.UseOctreeForCulling=0`. With the engine's default, every shadow in the scene vanished as soon as the far terrain tiles (1 km and more across) were shown: the octree query returned no casters. The world here is a few hundred large components (tiles, 16 km batches of trees and rocks), not thousands of small ones, so testing each is cheap. If the scene's make-up changes (many small actors), try the default again.

**D25. Features before looks; visuals in the plainest form that shows the feature.**
Tom, 2026-11-10: the two renderers differ too much to match picture for picture, and the aim in the end is Unreal's own strengths. So the port follows what Thornwake does (rules, systems, things the player can do) and gives each visual feature a reasonable first form, noting the difference, instead of rebuilding Godot-specific rendering machinery. First case: distant forests are Thornwake's tree cards (crossed quads with its drawn silhouettes), not its baked impostors and dithered hand-overs. The Unreal-native look comes after the port, possibly on its own branch (Tom's idea; nothing branched yet).

**D26. Gameplay stays a light placeholder; the world and its atmosphere come first.**
Tom, 2026-11-10: Thornwake's crafting and building were added so friends had something to do in multiplayer tests; they will be replaced by a deeper design. What matters to him is atmosphere (lighting, sound, environment, on Valheim's principle, not its look). So the game has only the lightest mechanics (needs, a pack, gathering, felling), run through the same rules layer Thornwake uses, with the player's machine asking and the rules answering, so a host can own them later. Building, blueprints and the recipe content are not ported. Multiplayer is kept possible and done last.

**D27. Weather in the simple form first; the sky from the engine's parts.**
Thornwake has two weather models: fronts passing on the clock (simple) and a field of pressure systems and storm cells moving over the map (what Tom plays). The simple one is ported, as engine-free code with checks, because it gives the things a player feels (cloud, rain, storms, snow, wind by the place's climate) for a tenth of the work; the field can replace it behind the same four numbers. On screen the weather drives Unreal's own sky: its volumetric clouds (coverage, density and storm darkening through the engine material's parameters), cloud shadows from the sun, height fog as haze. Rain and snow are a few thousand thin shapes moved round the camera in code, as the waterfall spray is (D21). The stars are a dome that also shows the engine sky's own light, because a mesh drawn as sky hides the sky behind it.

**D28. Sound is made in code, as in Thornwake, and played through one procedural wave.**
Thornwake ships no sound files: rain, wind, thunder and its few short sounds are synthesised. The synthesiser is ported as engine-free code (`WwSound`) so it can be checked without the engine and written to a file to listen to, and it keeps Tom's volume settings. The game asks it for frames a third of a second ahead and queues them on a `USoundWaveProcedural` that is not placed in space. This is the faithful form and a floor, not the aim: atmosphere matters most to Tom (D26), and the Unreal-native round can replace it with recorded or designed sound, placed in the world, with reverb and occlusion, behind the same inputs (rain, wind, water near, under water, shelter). Running water and the underwater dullness are additions; Thornwake has neither.

**D29. One list of settings; the menu is drawn from it; the old switches go through it.**
Thornwake's Esc menu is where Tom tests and tunes, and he asked for the same here, better if possible. Its weakness is one scroll of about 140 rows, many named after builds and drafts, with related things on different tabs. Here every setting is one entry (page, section, plain name, range, default, help) in `WwSettings`, the menu (`WwMenu`, Slate in code, no assets) draws its pages from that list, and the game reads values by key each frame. Only settings that do something exist; the list grows with the features. Short pages and a search box replace the long scroll; rows that depend on a switch are greyed while it is off. The command-line test switches set the same entries (and win over the file until the menu changes them), so there is one path for a value whether a script or the player sets it, and scripted checks (`-WwSet=`, `-WwPress=`) use the path the menu uses. The state of the moment is never written to the file, and `-unattended` runs ignore the file, so tests do not depend on what was last chosen. Buttons are entries too: a press bumps a counter that the part concerned watches; whoever starts watching later (a new level's game mode) starts from the present count, or a press would be acted on again after the level reopens (it was: "Make this world" looped until this was fixed). The world keeps running behind the menu, as in Thornwake.
