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
