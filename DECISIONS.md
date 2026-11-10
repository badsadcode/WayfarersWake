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
