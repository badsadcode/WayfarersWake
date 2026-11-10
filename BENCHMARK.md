# Benchmarks

Machine: the development PC (RTX 3080 target). Every number is from the run named; nothing here is an estimate.

## Generator, headless (`WorldGenTest.exe . --sizes --bench`, 2026-11-10)

Recipe `balanced`, seed 7319. The plan is everything computed once per world.

| World | Raster | Cell | Plan time | Plan memory | Lakes | River reaches | River points |
|---|---|---|---|---|---|---|---|
| 32 km | 512 | 62.5 m | 0.1 s | 5.6 MB | 6 | 0 | 0 |
| 64 km | 512 | 125 m | 0.1 s | 5.6 MB | 5 | 4 | 1,471 |
| 128 km | 512 | 250 m | 0.1 s | 6.0 MB | 18 | 30 | 11,689 |
| 256 km | 512 | 500 m | 0.2 s | 7.3 MB | 8 | 105 | 45,136 |
| 512 km | 768 | 667 m | 0.5 s | 22.8 MB | 8 | 683 | 268,460 |
| 768 km | 1024 | 750 m | 1.0 s | 45.6 MB | 13 | 1,516 | 611,558 |
| 1024 km | 1536 | 667 m | 1.9 s | 84.1 MB | 13 | 2,238 | 893,266 |
| 2048 km | 2048 | 1000 m | 5.2 s | 251.8 MB | 25 | 10,561 | 4,266,642 |
| 4096 km | 2048 | 2000 m | 13.2 s | 802.4 MB | 40 | 46,723 | 18,701,140 |

Plan memory is the estimate from `FWorldPlan::MemoryBytes` (rasters and river polylines); river points dominate at large sizes. A 32 km world has no river above the 30 km2 catchment threshold, as in Thornwake.

Tile sampling, single thread, 129 x 129 heights: 14.9 ms at 1 m spacing, 11.4 ms at 64 m (about 0.9 microseconds per sample).

## In game: straight flight over the 4096 km world

`-WwSizeKm=4096 -WwAutoFly=400 -WwAutoShot=60 -WwStats`: 1280 x 720 window, editor binary with `-game` (Development), 400 m/s for 60 s = 24 km, about 1,050 km from the world origin.

| Measure | Result |
|---|---|
| Plan build in engine | 16.6 s |
| Frame rate | 211-224 fps |
| Frame time, p99 per 5 s window | 7.5-8.7 ms |
| Frame time, worst per 5 s window | 8.8-22.7 ms |
| Tiles resident | 327-336 |
| Tiles missing at a report | 0-10 |
| Tiles built | 8,325 (2.98 ms average, on worker threads) |
| Commit time per frame, maximum | 3.2-3.5 ms (budget 3 ms) |
| Process memory | 4,244 MB after 2 km, 4,911 MB after 24 km |

Before mesh components were pooled, the same run went from 4,599 MB to 8,538 MB. The remaining rise (about 0.7 GB, slowing to 10 MB per 5 s by the end) is not explained yet; a long soak test is needed.

Standing still in the 64 km world: 330 tiles, 222-240 fps, 1.7 ms average tile build.

## Not measured yet

GPU and CPU frame breakdown, 1 % lows over long runs, the scene types of `Content/WorldData/benchmarks.json` (forest, lake, coast, mountain, flight), packaged (non-editor) builds, anything with vegetation.
