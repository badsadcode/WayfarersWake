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
* The game now generates revision 44 by default; in-engine self-test matches; launched and flown over a 512 km mountain world.
