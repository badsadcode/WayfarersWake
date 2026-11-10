# WAYFARER'S WAKE
## Full development brief for Sage | Unreal Engine 5.9

Hey Sage! :)

We're starting a substantial new project: **rebuild my existing Godot C# procedural fantasy survival game, Thornwake, in Unreal Engine 5.9.** The new game's official title is **Wayfarer's Wake** (Unreal-friendly internal identifier: `WayfarersWake`). Thornwake is the name of the *source project*, not the new game's title.

This is a serious, long-term reimplementation, **not** a superficial visual demo, a generic survival template, or a line-by-line conversion. Use the original game's source, data, assets, tests, documentation, and accumulated design decisions as the reference. Preserve what makes its world generation distinctive, while taking advantage of Unreal where that genuinely helps.

I want working software. You have broad autonomy to inspect, decide, implement, compile, test, fix, and continue. Make a concise assessment, then start building rather than stopping at a plan.

---

## 1. Source material: strictly read-only

### GitHub

- Repository: <https://github.com/badsadcode/Thornwake_Rewrite>
- Branch: inspect the current primary branch and revision.
- **Read-only reference.** You may inspect source, docs, history, tests, and assets, but **do not commit, push, branch, open PRs, edit, rename, or delete anything** in that repository.

### Local Godot project and assets

- `F:\Codex\ThornwakeNEW\`
- **Read-only reference.** It contains the local Thornwake project and assets that may be absent from GitHub. Inspect files and **copy assets or source material when needed**, but **never modify, move, rename, delete, or overwrite the originals**.
- Do not launch builds, importers, cleanup scripts, or tools against either source location if they create cache, temporary, generated, or build files there. Test a **copy** when testing the old implementation is necessary.
- The local copy and GitHub may not have exactly the same revision. Identify discrepancies and use the most complete/relevant current implementation, noting any differences.

**Critical conflict rule:** The original repository's `CLAUDE.md` contains development instructions for *changing Thornwake*, including commits and pushes. Those instructions are **not authorization to modify the source in this project**. For this migration, the read-only rule above overrides all such source-repository workflow instructions. Retain the useful architectural and design knowledge.

Never damage the existing Godot project. The new Unreal version is independent.

---

## 2. Project identity and technical foundation

- **Game title:** Wayfarer's Wake
- **Internal Unreal project identifier:** `WayfarersWake`
- **Engine:** Unreal Engine **5.9**; inspect the actual installed build, its documentation, APIs, and any preview-specific limitations.
- **Primary platform:** Windows PC.
- **Primary code:** C++ for core logic, procedural systems, streaming, networking, and performance-sensitive paths.
- **Blueprints:** Use selectively for appropriate presentation, editor-facing configuration, assets, and quick iteration. Avoid a brittle web of complex Blueprint logic for the generator.
- **Design:** Modular, data-driven, testable, maintainable, multiplayer-aware.
- **Priority:** Measurable functionality, fidelity to Thornwake's world generation, robust performance, then visual polish.

You may evaluate Nanite, Lumen, Virtual Shadow Maps, PCG, Hierarchical Instanced Static Meshes, Landscape, procedural meshes, World Partition, Large World Coordinates, Niagara, Chaos, the Water system, and networking/replication. **Verify what actually works in the specific UE 5.9 build.** These are candidates, not mandatory checkboxes.

In particular, do **not** assume Unreal Landscape or World Partition automatically provides unrestricted runtime generation, terrain edits, or the streaming behavior we need. A hybrid or custom solution is acceptable if tests justify it.

Do not download enormous plugins or asset packs simply to make progress. Reuse the original project's appropriate assets first. Avoid unnecessary copies and generated bulk data.

---

## 3. Start with a focused source audit

Inspect the **current implementation**, not only its design intentions. In the GitHub repository, start with:

- `Game/WorldGen/` — deterministic generation, macro terrain, landforms, climate, hydrology, world profiles and revisions.
- `Game/Rewrite/` — current playable rewrite, terrain tiles, streaming, vegetation, water, sky, gameplay, networking, saves, benchmarks.
- `Game/Simulation/` — simulation and world state.
- `Content/` — JSON definitions for biomes, world profiles, assets, gameplay, benchmarks, and looks.
- `Tests/WorldGen/` — determinism and world-generation regression tests.
- `Assets/` and the local project's asset directories — reusable art and audio.

Important starting documents (also discover newer or more applicable material):

- `CLAUDE.md`
- `docs/HANDOFF.md`
- `docs/DECISIONS.md`
- `docs/REWRITE_PLAN.md`
- `docs/GAMEPLAY_PLAN.md`
- `docs/BENCHMARK.md`
- `docs/SNAPSHOTS.md`
- `docs/PLAYTEST_REWRITE.md`
- `Persistent_Fantasy_Life_RPG_Game_Design_Bible_v0.1.md`

Read selectively and intelligently. Huge documentation files can contain superseded ideas. Cross-check current code and data. Classify subsystems as **working, partial, experimental, planned, or superseded**. The source implementation and tests are authoritative for implemented behavior.

Identify the smallest coherent set of source algorithms necessary to produce a faithful first Unreal world. Do not reread the entire codebase in every iteration.

---

## 4. Vision: a world to live in

Wayfarer's Wake is an open-ended procedural fantasy survival sandbox. It is a **world to live in**, not a mandatory-main-quest RPG.

Players should eventually be able to explore, hunt, fish, gather, craft, farm, build, travel, survive, discover unusual places, encounter wildlife, and interact with settlements and other people. The world should feel geographically believable and persist independently of the player's immediate actions. Environmental systems should interact coherently, without requiring deep simulation everywhere at once.

Broad inspirations include Valheim, Project Zomboid, Vintage Story, Conan Exiles, Kenshi, and Wurm Online; do not clone any of them. Preserve Thornwake's own evolving identity and sandbox freedom.

---

## 5. CRITICAL: world sizes, scalability, and deterministic generation

**Do not mistake the common 32 × 32 km testing world for the project's maximum size.**

The existing Thornwake project supports **selectable world sizes up to 4096 × 4096 kilometres**. The minimum selectable size is **not confirmed in this brief** (it may be approximately 4 × 4 km). **Inspect the existing code, configuration, and UI to find the real minimum, available presets/increments, and constraints. Do not invent or hardcode a minimum.**

**Preserve the existing world-size selection behavior and maximum supported size in the eventual Unreal implementation.** Smaller worlds, including 32 × 32 km, are useful for development and benchmarking, but are not the architectural ceiling.

That maximum area is approximately **16.8 million square kilometres**. This does **not** mean allocating a full-resolution heightfield, spawning every tree, or storing every terrain tile at once.

Requirements:

1. Deterministic generation from seed, world profile, relevant parameters, and terrain-generation revision.
2. Bounded-memory world streaming and procedural reconstruction around the player or relevant observers.
3. Coherent global macro layout without requiring the full high-resolution terrain in memory.
4. Correct handling of distances, world origin/coordinates, precision, physics, navigation, and multiplayer at enormous scales.
5. Asynchronous generation and carefully budgeted game-thread/GPU work to minimize hitches.
6. Persistent *changes* (terrain edits, buildings, removed resources, etc.) saved efficiently, rather than saving an entire unmodified generated world.
7. Seamless or well-controlled terrain tile edges, LOD changes, collision, and loading boundaries.
8. Workable generation times, memory requirements, and disk usage across supported world sizes.

Use Unreal's Large World Coordinates and/or appropriate origin/local-coordinate strategies, but validate rendering, physics, material precision, Niagara, networking, and subsystem behavior rather than assuming one feature solves everything.

**First make a smaller representative world work faithfully, then demonstrate that the architecture scales.** Do not claim 4096 km support merely because a size field accepts the number. Measure/verify actual generation, streaming, coordinate stability, and resource bounds.

---

## 6. Preserve the original world-generation intelligence

Study and adapt Thornwake's real algorithms for:

- Macro raster/world layouts and world profiles.
- Heightmaps, noise, deterministic math, and hashing.
- Erosion, drainage, watershed logic, rivers, and natural valley formation.
- Terrain sampling and versioned generation.
- Climate and biome selection.
- Coastlines, islands, archipelagos, mountains, plains, basins, and lakes.
- Tile construction and streaming.
- Terrain modifications and object removals.

The source also contains specialized landforms such as escarpments, canyons, glacial troughs, cirques, fjords, volcanoes and calderas, sea stacks and arches, headlands, coves, spits, lagoons, oxbows, river terraces, alluvial fans, and waterfalls. The first milestone need not implement every one, but plan for them without discarding their existing logic.

**Do not replace Thornwake's terrain generator with unrelated Perlin-noise hills just because they look pretty.** Use existing algorithms and data definitions as the behavioral reference. Where preserving exact output is possible, test parity using original seeds/profiles and headless fingerprints. When different numeric behavior is unavoidable, document the discrepancy and establish new regression tests. Respect versioned terrain so future changes need not silently reshape older worlds.

The resulting world should make geographical sense: mountain systems guide drainage, rivers occupy valleys, lakes and coastlines connect plausibly, and biomes follow climate and terrain.

---

## 7. Choose a workable Unreal terrain architecture

Evaluate, with an actual prototype:

- Unreal Landscape where it meets runtime and editing requirements.
- Custom/runtime procedural terrain meshes where needed.
- PCG for secondary placement and environmental detail.
- World Partition and/or custom streaming for generated worlds.
- Chunk LOD, collision LOD, caching, and deterministic regeneration.
- A hybrid of these tools if that is superior.

Choose the simplest robust solution that supports seeded **runtime creation**, very large selectable world sizes, terrain modification, persistence, and eventual multiplayer.

Avoid architectures dependent on manually authoring or baking each possible world in the editor.

Preserve the ability to dig, fill, level, or otherwise alter terrain. Investigate how edits affect collision, terrain materials, props, hydrology, buildings, and saved state. Full editing can arrive after basic world navigation, but do not close off the possibility.

---

## 8. Water: oceans, rivers, lakes, waterfalls

Study the original hydrology and water rendering separately; they solve different problems. Hydrology must remain driven by world generation.

**Oceans:** large surfaces, wind/storm-driven waves, shore-directed effects where appropriate, reflection/refraction, foam, underwater visuals, and sensible scalability.

**Rivers:** true paths and widths from the generated world, directional current, meanders, junctions, banks, rapids, rocks and trailing foam, river mouths, and waterfalls.

**Lakes:** level surfaces, natural shores, wind response, underwater visibility, and correct river connections.

Pay particular attention to transitions: river-to-lake, river-to-sea, waterfall lips, plunge pools, and shoreline geometry. The original project has dedicated systems and experimental water styles; study their behavior and reuse useful textures, not Godot-specific shader syntax.

Unreal Water and materials can be used where effective. If they cannot support a required runtime behavior, implement an appropriate custom solution. Distinguish visual water movement from actual water flow/interaction.

---

## 9. Vegetation, biomes, and geological clutter

Recreate the ecological logic and placement patterns, not only the models:

- Appropriate trees, bushes, grass, ground cover, and biome transitions.
- Forest stands, believable edges and clearings, vegetation density based on climate, elevation, slope, moisture, and land use.
- Temperate, plains, taiga, tundra, steppe, desert, swamp, mountain, and coastal environments as supported by the source.
- Rock faces, cliffs, outcrops, boulders, scree, talus, sea stacks, and other landform-specific clutter.

Evaluate instancing, HISM, PCG, Nanite where suitable, impostors, shadow proxies, distance culling, and LOD transitions. Existing Thornwake uses detailed near trees with cheaper mid/far representations. Improve the approach if tests justify it, but be mindful of GPU-bound tree shading and shadows, temporal flicker, billboard pop, and streaming hitches.

Collision must match visible geometry well enough for natural traversal. Prevent snagging, rubber-banding, floating props, and walk-through rock formations.

---

## 10. Atmosphere, time of day, and weather

Inspect the original Atmosphere 2.0 work and its settings, presets, and reference captures. Recreate the intended mood using suitable Unreal features:

- Sky Atmosphere, dynamic sun and moon, sunrise/sunset/night lighting.
- Volumetric clouds and fog; local mist, ground fog, and shafts through forests.
- Wind and environmental response, rain and storms, wet materials and splashes.
- Different lighting profiles and conditions across biomes and weather states.
- Flexible graphics settings and visual comparison options.

Use Lumen, Niagara, Volumetric Clouds, and other systems selectively. The aim is a convincing, atmospheric world with acceptable frame times, not a checklist of expensive features.

Preserve alternate looks or provide reversible settings for significant visual changes. Defer major art-direction changes rather than silently changing the game's identity.

---

## 11. Gameplay feature parity, progressively

Audit the current Godot rewrite and reimplement the **actually working** systems in sensible order:

- Player movement, camera, input, interaction, health, stamina, stats.
- Inventory, equipment, hotbar, items, and persistence.
- Gathering, chopping, hunting, butchering, fishing, and basic survival.
- Crafting recipes, ingredients, quantities/queues, and stations.
- Blueprint construction, materials, snapping, rotation, support rules, doors, dismantling.
- Wildlife spawning, basic AI, damage/death, harvesting, and environmental behavior.
- Terrain editing, plants, and resource changes.
- World creation, settings, maps, save/load, development tools, and UI.

Do not attempt every feature simultaneously. Prioritize a playable vertical slice after the procedural environment works. Use the source project's data-driven definitions rather than hardcoding large duplicate tables.

The ultimate goal is complete feature parity where practical, followed by new development.

---

## 12. Multiplayer-aware from day one

Target cooperative play for roughly **12–16 players** eventually, beginning with local/player-hosted sessions and the option for dedicated servers later.

Use an authoritative design for mutable state. Replicate seeds, generation settings/revisions, relevant player/world events, wildlife, buildings, inventory interactions, and edits; let clients deterministically reconstruct unmodified terrain where possible rather than replicating enormous landscapes actor-by-actor.

Consider joining an already-running world, different player positions, save consistency, large-world coordinates, and network relevancy. Do not build full multiplayer before the world prototype, but avoid single-player assumptions that would force a major rewrite.

---

## 13. Assets and licensing

Examine the local Godot project's extra assets as well as the repository's `Assets/`, `ArtKit/`, and `Licenses/` areas.

When useful, **copy** existing meshes, textures, animals, buildings, sounds, animation resources, and UI assets into the new project. Never move originals. Inspect formats, units, orientation, materials, collisions, animation, and import behavior.

Rebuild Godot shaders as Unreal materials/material functions or suitable shader code; don't assume `.gdshader` files are directly portable. Reuse water, foam, mist, and environment textures where their format and licenses permit.

Record the original source and license for each reused asset. Avoid silently importing restricted third-party content. Prefer suitable existing assets to throwaway replacements, but placeholder assets are acceptable when needed for rapid functional tests.

---

## 14. Performance requirements and reproducible benchmarks

Primary development graphics card: **NVIDIA RTX 3080**. Aim for smooth **60 FPS** at a reasonable graphics preset, with scalable options for weaker systems. This is a measured target, not a promise.

Avoid frame spikes from synchronous chunk generation, excessive collision cooking, streaming commits, shader compilation, shadows, and foliage overdraw. Track memory and disk-cache growth as well as GPU/CPU costs.

Study `docs/BENCHMARK.md` and `Content/benchmarks.json`. Build comparable repeatable tests across forest, lake, coastline, mountain, and fast-traversal/flight environments. Where possible, hold seed, place, hour, weather, resolution, and quality settings constant.

Measure average FPS, 1% lows, frame-time percentiles, hitches, CPU/GPU times, memory, and relevant draw/streaming statistics. Optimize based on measurements, not assumptions.

---

## 15. Implementation milestones

### Milestone 1 — Establish a working UE 5.9 game

- Create the `WayfarersWake` C++ project.
- Set up basic game mode, playable character, camera, inputs, logs, and simple UI.
- Establish test/build workflow, module structure, and key documentation.
- **Actually compile and launch it.** Record what was verified.

### Milestone 2 — Faithful procedural terrain core

- Port/adapt the minimum real Thornwake generation pipeline.
- Support seed and world profile selection, deterministic height sampling, rendered terrain, collision, initial chunk/LOD streaming, and safe player spawning.
- Begin on a modest representative world. 32 × 32 km is a **test size, not a cap**. Smaller test sizes are fine if supported by the original generator.
- Compare representative seeds and terrain features against the Godot reference.

### Milestone 3 — Believable environment

- Terrain materials, biome distribution, trees and grass, rocks.
- Rivers, lakes, coastline/ocean basics.
- Time of day, sky, light, and basic weather.
- Improve streaming and benchmark representative scenes.

### Milestone 4 — Playable exploration build

- Launch the game, choose/generate a seeded world, walk and explore.
- Travel across streaming boundaries, through forest, mountain and water environments with valid collision.
- Test varied world sizes and demonstrate that memory remains bounded during traversal.

### Milestone 5 — Systems and persistence

- Migrate core gameplay systems, building, gathering, hunting, crafting, survival, save/load, and terrain/world changes.

### Milestone 6 — Multiplayer and long-range scaling

- Host/join flow, authoritative world edits, player/world synchronization.
- Larger worlds and extreme-distance precision tests.
- Additional landforms, expanded content, and feature parity.

Milestones guide priorities; they are not permission to stop after writing the plan. Ship the smallest **working, testable** increment of each system.

---

## 16. Autonomy and decision-making

Make routine technical decisions independently. Do not ask me how to name every class, whether to use a Blueprint, or whether to factor a function. Investigate, implement, compile, test, correct, document briefly, and move forward.

If you hit a hard engine limitation, document it and select a viable fallback that does not sabotage world-scale requirements. Do not silently drop important functionality. Ask me only when a decision has serious artistic, product-scope, licensing, destructive, or irreversible implications.

Prefer clear code, localized responsibilities, reproducible tests, and compact comments over elaborate abstractions built for hypothetical needs.

---

## 17. Token and work efficiency: this is a model evaluation

I am testing Fable's practical productivity and usage efficiency against other coding models. **Working progress per unit of usage** matters more than impressive-sounding reports.

- Search relevant files instead of loading the whole repository repeatedly.
- Reuse already established findings and keep a concise handoff.
- Avoid repetitive explanations and planning loops.
- Make short, meaningful increments; compile and test them.
- Diagnose compiler/runtime failures from actual logs instead of guessing.
- Do not repeatedly try failed approaches unchanged.
- Do not pretend to know billed token counts or usage percentages if they are unavailable.
- State real accomplishments, build status, blockers, and next steps succinctly.

Do not use token efficiency as an excuse for untested, fragile, or misleading code. Minimize wasted work, not necessary engineering.

---

## 18. Maintain a reliable handoff in the new project

Maintain these concise Markdown files as appropriate:

- `MIGRATION_STATUS.md` — current status; working vs incomplete; known bugs; successful build/test commands; blockers; next concrete action. **Keep this current so another model can continue immediately.**
- `SYSTEM_AUDIT.md` — original Godot subsystem/file references, current behavior, Unreal implementation, parity status, differences.
- `DECISIONS.md` — important technical choices and rationale.
- `ASSET_MANIFEST.md` — imported assets, sources, licenses, conversion notes.
- `BENCHMARK.md` — repeatable test procedures and measured results.
- `DEVELOPMENT_LOG.md` — brief progress and major fixes, not a novel.

These documents support delivery. **The playable game is the deliverable.**

---

## 19. Validation rules

Never report "works" merely because code was written.

Validate as far as the actual environment permits:

- Unreal project generation, compilation, and editor startup.
- In-game launch, movement, and terrain collision.
- Deterministic generation for the same seed/profile/revision.
- Different seeds/presets and world sizes.
- Tile seams, LOD, traversal, and stream/unload behavior.
- Hydrology continuity, shorelines, waterfalls where implemented.
- Stable player position and precision over long distances.
- No uncontrolled memory growth as the player travels.
- Performance on representative scenes.

Use tests from the original project's algorithms where practical, but execute on copies when they produce build artifacts. If a test cannot be run, be explicit about what remains unverified.

Distinguish **implemented**, **compiled**, **launched**, **visually inspected**, and **tested**. They are not the same milestone.

---

## 20. Things to avoid

1. A generic UE open-world/survival template unrelated to Thornwake.
2. Replacing sophisticated source hydrology, terrain, and climate logic with arbitrary noise.
3. Designing only for a 32 × 32 km world; **4096 × 4096 km is the maximum target**.
4. Claiming huge-world support without testing streaming, memory, and precision.
5. A pretty screenshot with no playable game behind it.
6. Unnecessary new assets when we already have reusable ones.
7. Blindly using Landscape, PCG, Nanite, World Partition, or Lumen everywhere.
8. Single-player-only architecture that blocks future multiplayer.
9. Excessive planning, speculative infrastructure, and repeated documentation.
10. Unauthorized modifications to either read-only source.
11. Reporting successful compilation or testing without evidence.
12. Quietly changing the game's tone, direction, or established world behavior.

---

## 21. Your immediate execution sequence

**Start now:**

1. Inspect the relevant current Godot source, tests, and handoff docs. Establish the exact world-size options and their implementation, including the confirmed **4096 × 4096 km maximum**.
2. Identify the smallest complete world-generation pipeline to adapt, plus relevant assets.
3. Perform a concise, actionable audit and architecture decision. Do not spend the entire session drafting a migration proposal.
4. Create the new **WayfarersWake** UE 5.9 project without modifying either source.
5. Implement the minimal playable foundation and real procedural terrain path.
6. Compile, open/run, and diagnose real issues.
7. Add deterministic regression checks and work toward streamed, explorable terrain.
8. Update `MIGRATION_STATUS.md`, then continue with the next useful implementation task.

If you cannot execute a step because of an environmental limitation, report the blocker and the best verified next action. Don't invent results.

### First meaningful success criterion

A **compiled, launchable, explorable** Unreal Engine 5.9 project called **Wayfarer's Wake**, using the original Thornwake procedural algorithms for a seeded world, with working terrain collision, basic streaming, controllable character, and a clear path to water, vegetation, larger sizes, and future gameplay.

The 4096 × 4096 km requirement is a **long-term architectural and validation target**, not a demand to fully generate that entire world during the first milestone.

---

## 22. Final priorities

1. Protect the two original Thornwake sources, **read-only**.
2. Build **Wayfarer's Wake**, with the correct name.
3. Preserve the richness, logic, determinism, and selectable size range of the original world generator, **up to 4096 × 4096 km**.
4. Use Unreal Engine 5.9 pragmatically, not dogmatically.
5. Reuse suitable assets and document licensing.
6. Make something **real, playable, tested, and performant**.
7. Keep the code comprehensible and the project easy for another model to inherit.
8. Work autonomously and efficiently; demonstrate meaningful progress.

Don't stop after the initial assessment. **Build the game.**

Let's see what you've got, Sage. :)
