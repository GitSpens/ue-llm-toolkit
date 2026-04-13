# Runtime Performance Analyzer — Design Spec

**Feature:** runtime-performance-analyzer
**Created:** 2026-04-10
**Branch:** feature/runtime-performance-analyzer
**Status:** Approved design, pending implementation

## Problem

The existing `unreal_scene_performance` tool performs static analysis — it iterates actors in the editor world, counts triangles/LODs/materials/draw calls, estimates cost with a formula, and generates worst-case recommendations. Its `CaptureLiveStats` is a single-frame snapshot of `stat unit` values, not a sustained measurement.

This produces theoretical "what could be expensive" analysis rather than concrete "what is actually causing problems during gameplay" analysis. Users need to identify real performance bottlenecks that manifest during actual gameplay in specific scene locations.

## Solution

A new MCP tool (`runtime_profiler`) that collects real-time performance metrics during PIE gameplay, correlates performance spikes with scene content visible in the camera frustum, and generates actionable recommendations — optionally adjusted for a target device's hardware budget.

## Tool Interface

### MCP Tool Name: `runtime_profiler`

### Operations

| Operation | Purpose |
|-----------|---------|
| `start_profiling` | Begin collecting runtime metrics. Starts PIE if not running, or attaches to running PIE. Auto-stops when PIE ends. |
| `get_status` | Check if profiling is active, duration, sample count |
| `get_results` | Retrieve last session's analysis (available after PIE ends) |

### Parameters

#### start_profiling

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `interval_ms` | number | 200 | Sampling interval in milliseconds (5 samples/sec default) |
| `channels` | array | all | Metric groups to capture: `frame_timing`, `rendering`, `memory` |
| `target_device` | string | `pc_high` | Device preset for recommendation thresholds |
| `spike_threshold` | number | 2.0 | Multiplier over session average to flag a frame as a spike |

#### get_results

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `detail_level` | string | `summary` | `summary` (compact) or `full` (includes per-spike actor breakdowns) |

### Lifecycle

1. User tells Claude: "profile my scene"
2. Claude calls `start_profiling` (starts PIE if needed, or attaches to running PIE)
3. Claude confirms profiling is active
4. User plays the game (manual play or automated via `run_sequence`)
5. User stops PIE normally
6. Profiler auto-detects PIE ended via `FEditorDelegates::EndPIE`, stops collection, processes results
7. Claude calls `get_results`, receives analysis, presents findings to user

No manual `stop_profiling` operation needed — profiling always ends when PIE ends.

**Edge cases:**
- Calling `start_profiling` while already profiling: returns an error ("Profiling session already active. Stop PIE to end the current session.").
- Calling `get_results` while profiling is still active: returns an error ("Profiling in progress. Stop PIE first to generate results.").
- Calling `get_results` with no previous session: returns an error ("No profiling session found. Call start_profiling first.").
- PIE ends abnormally (crash): profiler cleans up gracefully, partial data is still analyzed and written to disk if any samples were collected.

## Data Collection

### Sampling Approach

A `FTSTicker` callback fires at `interval_ms` intervals during PIE. Each tick captures a `FProfileSample` struct containing all enabled metric groups plus spatial context.

### Metric Groups

**Frame Timing** (`frame_timing`)
- Frame time (ms), FPS
- Game thread time, render thread time, GPU time
- Source: UE stat system / `FPlatformTime` / engine stat counters

**Rendering** (`rendering`)
- Draw calls, mesh draw calls
- Triangles actually rendered
- Active render passes
- Source: RHI stats / scene rendering counters

**Memory & Streaming** (`memory`)
- Total memory used
- Texture streaming pool usage, pending texture requests
- Source: memory / streaming stat counters

**Spatial Context** (always captured)
- Player pawn world location + rotation
- Camera location + rotation + view projection matrix (for frustum queries)
- Current level/sublevel name

### Storage

Samples are stored in a `TArray<FProfileSample>` in memory during the session. On PIE end, the full array is processed and written to disk as JSON.

## Analysis Pipeline

Processing runs in three passes after PIE ends.

### Pass 1 — Statistical Summary

Computes min/avg/max/P95 for all captured metrics across the full session. Identifies the bottleneck thread (game vs render vs GPU) based on which consistently has the highest frame contribution.

### Pass 2 — Spike Detection & Spatial Correlation

Identifies frames where frame time exceeds `spike_threshold` multiplied by the session average. Groups consecutive spike frames into clusters.

For each spike cluster:

1. **Frustum query** — Using the camera transform at that moment, construct a view frustum and test actor bounds against it. This identifies actors actually being rendered (the primary correlation with rendering cost).
2. **Shadow caster query** — Find shadow-casting actors behind the camera but within shadow distance, since these still cost GPU time.
3. **Actor mesh stats** — For each identified actor, call `FSceneAnalyzer::GatherActorMeshStats` to get triangle counts, draw calls, materials, LODs, shadow settings.
4. **Delta analysis** — Compare the spike cluster's frustum contents against the previous smooth period. Flag actors that entered the frustum, sublevels that streamed in, or significant metric deltas.

### Pass 3 — Recommendation Generation

Based on correlation data, generates specific actionable suggestions. Recommendations are concrete and reference specific actors/locations:

- "FPS dropped to 24 at position (1200, -500, 100) — 3 actors in view have 800K+ triangles with no LODs: [MeshActor_12, Tree_Large_03, Rock_Cliff_01]. Add LOD chains."
- "Render thread bottleneck during frames 500-650. Draw calls peak at 4200 in the marketplace area. 12 actors use unique materials that could be instanced."
- "GPU time spikes to 28ms when facing the waterfall. 6 Niagara particle systems active simultaneously in frustum."
- "Texture streaming pool exceeded budget by 200MB in the village. 8 actors use 4K textures at distances where 2K would suffice."

If a `target_device` is set, recommendations include device-specific warnings:
- "Draw calls exceed Quest 3 standalone budget (200) in 68% of sampled frames."
- "Scene triangle count (1.2M in frustum) exceeds Quest 3 budget of 750K."

## Target Device Presets

A static data map of device names to budget thresholds.

| Device | Max Draw Calls | Triangle Budget | Texture Memory | Notes |
|--------|---------------|-----------------|----------------|-------|
| `quest_3_standalone` | ~200 | ~750K | ~2GB | No RT, limited dynamic shadows |
| `mobile` | ~300 | ~500K | ~1.5GB | Varies widely |
| `pc_mid` | ~2000 | ~5M | ~6GB | Full feature set |
| `pc_high` | ~5000 | ~10M+ | ~12GB | Full feature set |

Presets are defined in `DeviceBudgets.h` as a simple struct + static lookup. Easy to extend with new devices.

## Output Format

### On-Disk Files

Written to `Saved/Profiling/RuntimeProfiler/<session-timestamp>/`:

- `manifest.json` — Session metadata: start/end time, level, device target, sample count, file paths
- `session.json` — Full time-series data (every sample with all metrics + position + camera)
- `summary.json` — Statistical summary, spike clusters, recommendations

### get_results Response

**detail_level: `summary`** (default — compact, good for Claude to reason about):

```json
{
  "session": {
    "duration_s": 45.2,
    "sample_count": 226,
    "level": "/Game/Maps/Village",
    "target_device": "quest_3_standalone"
  },
  "summary": {
    "fps": { "min": 18, "avg": 52, "max": 60, "p95": 24 },
    "frame_time_ms": { "min": 16.6, "avg": 19.2, "max": 55.5, "p95": 41.6 },
    "bottleneck_thread": "gpu",
    "draw_calls": { "min": 180, "avg": 420, "max": 3800, "p95": 1200 },
    "triangles_rendered": { "min": 120000, "avg": 450000, "max": 1800000, "p95": 1200000 },
    "memory_mb": { "min": 2800, "avg": 3100, "max": 3400 }
  },
  "spike_count": 3,
  "recommendations": [
    "FPS dropped to 18 near (1200, -500, 100) — 3 actors in frustum have 800K+ triangles with no LODs: MeshActor_12, Tree_Large_03, Rock_Cliff_01. Add LOD chains.",
    "Draw calls peak at 3800 in marketplace area. 12 actors use unique materials — consider material instancing.",
    "GPU time spikes when facing waterfall. 6 Niagara systems active simultaneously in frustum."
  ],
  "device_warnings": [
    "Draw calls exceed Quest 3 budget (200) in 68% of frames.",
    "Triangle count in frustum exceeds Quest 3 budget (750K) in 42% of frames.",
    "Dynamic shadows detected — not supported on Quest 3 standalone."
  ],
  "output_dir": "D:/Project/Saved/Profiling/RuntimeProfiler/2026-04-10_143022/"
}
```

**detail_level: `full`** — Adds per-spike actor breakdowns:

```json
{
  "...same as summary...",
  "spikes": [
    {
      "frame_range": [500, 520],
      "time_range_s": [8.3, 8.6],
      "location": { "x": 1200, "y": -500, "z": 100 },
      "avg_frame_time_ms": 42.0,
      "cause_analysis": {
        "actors_in_frustum": [
          { "name": "MeshActor_12", "triangles": 850000, "draw_calls": 24, "materials": 8, "lods": 1 }
        ],
        "shadow_casters_behind_camera": [],
        "total_draw_calls": 3800,
        "delta_from_smooth": "+2400 draw calls, +12 actors entered frustum"
      }
    }
  ]
}
```

Full time-series data is always written to disk but never returned in the response to avoid blowing up response size.

## Architecture

### New Files

| File | Purpose |
|------|---------|
| `Private/MCP/Tools/MCPTool_RuntimeProfiler.h` | Tool class — operation dispatch, parameter validation |
| `Private/MCP/Tools/MCPTool_RuntimeProfiler.cpp` | Tool implementation |
| `Private/RuntimeProfiler.h` | Utility class — profiler engine (ticker, collection, analysis) |
| `Private/RuntimeProfiler.cpp` | Utility implementation |
| `Private/DeviceBudgets.h` | Static device preset data |

### Key Classes

- **`FRuntimeProfiler`** — Manages the profiling session lifecycle: start, sample via ticker, auto-stop on PIE end, run analysis passes. Singleton-style (one active session at a time).
- **`FProfileSample`** — One tick's captured metrics + player position + camera transform.
- **`FProfileSession`** — Full session: `TArray<FProfileSample>`, metadata (duration, device target, level name).
- **`FProfileAnalysis`** — Output of the 3-pass analysis: summary stats, spike clusters with spatial correlations, recommendations, device warnings.
- **`FDeviceBudget`** — Struct with budget numbers per target device. Static lookup by name.

### Data Flow

```
start_profiling
  → FRuntimeProfiler::Start()
    → Starts PIE if not running
    → Hooks FTSTicker for sampling at interval_ms
    → Hooks FEditorDelegates::EndPIE for auto-stop

Each ticker tick
  → FProfileSample captured (stats + position + camera)
  → Pushed to FProfileSession::Samples

PIE ends (FEditorDelegates::EndPIE fires)
  → FRuntimeProfiler::Stop()
    → Unhooks ticker + delegates
    → Runs 3-pass analysis → produces FProfileAnalysis
    → Writes session.json, summary.json, manifest.json to disk

get_results
  → Returns FProfileAnalysis as JSON (summary or full detail)
```

### Integration Points

- **Reuses `FSceneAnalyzer::GatherActorMeshStats`** for actor analysis during spatial correlation (Pass 2)
- **Registered in `MCPToolRegistry.cpp`** alongside existing tools
- **PIE lifecycle** via `FEditorDelegates::EndPIE` (same pattern as `gameplay_debug` monitor mode)
- **Ticker** via `FTSTicker::GetCoreTicker()` (same pattern as `gameplay_debug` monitor mode)

### Tool Annotations

`FMCPToolAnnotations::Modifying()` — because `start_profiling` can start PIE.

## Future: Unreal Insights Integration

Not in scope for this implementation, but designed to be addable later:

1. `start_profiling` gains an optional `enable_trace: true` param
2. On start, calls `FTraceAuxiliary::Start()` with selected channels (`cpu`, `gpu`, `frame`, `counters`)
3. On PIE end, stops the trace, gets the `.utrace` file path
4. Shells out to UnrealInsights commandlet for CSV export
5. Parses CSV and merges with ticker-sampled data for enriched analysis

The ticker-based profiler remains the primary data source; Insights adds deep-dive capability for frames that need GPU-pass-level granularity.

## Testing

- `Private/Tests/RuntimeProfilerTests.cpp` — UE Automation Tests
- Test analysis passes with synthetic sample data (known spikes at known positions)
- Test device budget threshold logic
- Test frustum query accuracy with placed actors at known positions
- Integration test: start profiling → brief PIE → stop → verify results structure
