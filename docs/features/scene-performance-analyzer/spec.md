# Technical Spec — scene-performance-analyzer

**Date:** 2026-04-09
**Status:** Approved
**Discovery:** [discovery.md](discovery.md)

---

## 1. Feature Overview

**Name:** `unreal_scene_performance`
**Purpose:** A read-only scene analysis tool that inspects every actor's mesh components to gather per-actor performance metrics (triangle count, LOD count, material count, mesh sections, shadow settings), estimates per-actor rendering cost, optionally captures live aggregate stats via console commands, validates data sanity, and returns a ranked report of bottleneck actors with specific optimization recommendations. No user interaction required — runs fully autonomously and returns a complete analysis.

**UE Systems:** Level / World / Actors, Static Mesh / Skeletal Mesh APIs, Material system, Console commands (stat)

---

## 2. Architecture

**Layers:**
- Tool class: `FMCPTool_ScenePerformance` in `MCPTool_ScenePerformance.h/.cpp`
- Utility class: `FSceneAnalyzer` in `SceneAnalyzer.h/.cpp` — mesh inspection, cost estimation, recommendation generation
- Registry: add to `MCPToolRegistry.cpp`

**File List:**

| File | Action | Purpose |
|------|--------|---------|
| `Source/UELLMToolkit/Private/MCP/Tools/MCPTool_ScenePerformance.h` | Create | Tool header — GetInfo, Execute, handler declarations |
| `Source/UELLMToolkit/Private/MCP/Tools/MCPTool_ScenePerformance.cpp` | Create | Tool impl — JSON dispatch, parameter handling |
| `Source/UELLMToolkit/Private/SceneAnalyzer.h` | Create | Utility header — structs, analysis functions |
| `Source/UELLMToolkit/Private/SceneAnalyzer.cpp` | Create | Utility impl — mesh stats, cost model, recommendations |
| `Source/UELLMToolkit/Private/Tests/ScenePerformanceTests.cpp` | Create | Automation tests |
| `Source/UELLMToolkit/Private/MCP/MCPToolRegistry.cpp` | Modify | Register `FMCPTool_ScenePerformance` |

---

## 3. Operation Definitions

### Operation: `analyze_scene`

**Description:** Scans all actors in the current level, gathers per-actor mesh statistics from their `UStaticMeshComponent` and `USkeletalMeshComponent` components, estimates rendering cost using a weighted formula, optionally captures live aggregate stats via `stat scenerendering` console command, validates data sanity, ranks actors by cost, and generates per-actor and scene-wide optimization recommendations.

**Annotation:** ReadOnly

**Parameters:**

| Name | Type | Required | Validation | Description |
|------|------|----------|------------|-------------|
| operation | FString | yes | must equal `"analyze_scene"` | Operation selector |
| limit | int32 | no | Clamp(1, 500), default 20 | Max bottleneck actors to return |
| sort_by | FString | no | must be one of: `"triangles"`, `"draw_calls"`, `"materials"`, `"estimated_cost"`. Default `"estimated_cost"` | Ranking metric for bottlenecks |
| include_stats | bool | no | default true | Capture live aggregate stats via console commands |
| class_filter | FString | no | none | Only analyze actors of this class (e.g., `"StaticMeshActor"`) |
| max_actors | int32 | no | Clamp(1, 50000), default 10000 | Max actors to scan (safety limit for large scenes) |

**Cost Estimation Formula:**

```
estimated_cost = (triangle_count / 1000.0)
              * material_count
              * mesh_section_count
              * (shadow_casting ? 1.5 : 1.0)
              * (lod_count == 0 && triangle_count > 5000 ? 2.0 : 1.0)
```

This weights high-poly meshes with many materials, no LODs, and shadow casting as most expensive. The formula produces a relative score — not an absolute GPU cost.

**Return Format (Success):**

```json
{
  "status": "success",
  "message": "Analyzed 342 actors (28 with mesh components)",
  "scene_summary": {
    "total_actors": 342,
    "mesh_actors": 28,
    "total_triangles": 1245000,
    "total_estimated_draw_calls": 156,
    "total_materials": 47,
    "unique_meshes": 12,
    "actors_without_lods": 8
  },
  "live_stats": {
    "captured": true,
    "frame_time_ms": 16.4,
    "game_thread_ms": 8.2,
    "render_thread_ms": 11.3,
    "gpu_time_ms": 14.1,
    "draw_calls": 1523,
    "mesh_draw_calls": 892
  },
  "sanity_check": {
    "valid": true,
    "warnings": []
  },
  "bottlenecks": [
    {
      "actor_label": "SM_Castle_Wall_3",
      "actor_class": "StaticMeshActor",
      "location": {"x": 1200.0, "y": -340.0, "z": 0.0},
      "mesh_path": "/Game/Meshes/SM_Castle_Wall",
      "triangle_count": 52000,
      "vertex_count": 38000,
      "lod_count": 0,
      "material_count": 3,
      "mesh_section_count": 3,
      "shadow_casting": true,
      "has_collision": true,
      "collision_complexity": "complex_as_simple",
      "instance_count": 1,
      "estimated_draw_calls": 3,
      "estimated_cost": 468.0,
      "recommendations": [
        "Add LODs — mesh has 52k triangles but 0 LODs. Expected 3-4 LODs for this poly count.",
        "High material count (3) on high-poly mesh — consider merging materials to reduce draw calls."
      ]
    }
  ],
  "overall_recommendations": [
    "8 mesh actors have no LODs, totaling 380k triangles — adding LODs could reduce distant rendering cost by ~60%.",
    "47 unique materials across 28 mesh actors — consider material instancing to reduce shader permutations.",
    "3 actors use 'complex_as_simple' collision on meshes with >10k triangles — switch to simple collision."
  ]
}
```

**Error Cases:**

| Condition | Error Message |
|-----------|--------------|
| No editor world available | "No editor world available — open a level first" |
| No actors found in level | "Level contains no actors" |
| No mesh actors found | Returns success with `mesh_actors == 0`, empty `bottlenecks`, warning in `sanity_check` |
| Invalid sort_by value | "Invalid sort_by value 'X'. Must be one of: triangles, draw_calls, materials, estimated_cost" |
| Stat command output not parseable | Live stats returned with `"captured": false` and warning in `sanity_check` |

---

### Operation: `list_traces`

**Description:** Lists available `.utrace` files in the project's `Saved/Profiling/` directory. Placeholder for future CSV-based trace analysis.

**Annotation:** ReadOnly

**Parameters:**

| Name | Type | Required | Validation | Description |
|------|------|----------|------------|-------------|
| operation | FString | yes | must equal `"list_traces"` | Operation selector |
| path_filter | FString | no | none | Subdirectory filter within `Saved/Profiling/` |

**Return Format (Success):**

```json
{
  "status": "success",
  "message": "Found 3 trace files",
  "trace_dir": "C:/Projects/MyGame/Saved/Profiling/",
  "traces": [
    {
      "filename": "UnrealInsights_2026-04-09_143022.utrace",
      "path": "C:/Projects/MyGame/Saved/Profiling/UnrealInsights_2026-04-09_143022.utrace",
      "size_mb": 45.2,
      "created": "2026-04-09T14:30:22"
    }
  ]
}
```

**Error Cases:**

| Condition | Error Message |
|-----------|--------------|
| Profiling directory doesn't exist | "No profiling directory found at '<path>'. Run a trace session in Unreal Insights first." |
| No .utrace files found | Success with empty array, informational message |
| path_filter matches no subdirectory | "Subdirectory '<filter>' not found in profiling directory" |

---

## 4. UE API References

| Class / Function | Include Path | Usage | Verified |
|-----------------|-------------|-------|----------|
| `UWorld` | `#include "Engine/World.h"` | Get all actors via `TActorIterator` | yes |
| `TActorIterator<AActor>` | `#include "EngineUtils.h"` | Iterate all actors in level | yes |
| `AActor` | `#include "GameFramework/Actor.h"` | Get components, label, class, transform | yes |
| `UStaticMeshComponent` | `#include "Components/StaticMeshComponent.h"` | Get mesh reference, material slots, shadow settings | yes |
| `USkeletalMeshComponent` | `#include "Components/SkeletalMeshComponent.h"` | Get skeletal mesh reference, material slots | yes |
| `UInstancedStaticMeshComponent` | `#include "Components/InstancedStaticMeshComponent.h"` | Get instance count for ISM/HISM actors | yes |
| `UStaticMesh` | `#include "Engine/StaticMesh.h"` | Access render data, LOD info, sections | [NEEDS VERIFICATION] |
| `UStaticMesh::GetNumTriangles(int32 LODIndex)` | (same header) | Get triangle count per LOD | [NEEDS VERIFICATION] |
| `UStaticMesh::GetNumLODs()` | (same header) | Get LOD count | [NEEDS VERIFICATION] |
| `FStaticMeshLODResources` | `#include "StaticMeshResources.h"` | Access per-LOD vertex/triangle data, sections | [NEEDS VERIFICATION] |
| `USkeletalMesh` | `#include "Engine/SkeletalMesh.h"` | Access LOD info, materials | [NEEDS VERIFICATION] |
| `FSkeletalMeshLODRenderData` | `#include "Rendering/SkeletalMeshRenderData.h"` | Per-LOD triangle/vertex data | [NEEDS VERIFICATION] |
| `UMaterialInterface` | `#include "Materials/MaterialInterface.h"` | Material slot references | yes |
| `UPrimitiveComponent::CastShadow` | `#include "Components/PrimitiveComponent.h"` | Check shadow casting status | yes |
| `UPrimitiveComponent::GetCollisionEnabled()` | (same header) | Check collision complexity | yes |
| `FBodyInstance` | `#include "PhysicsEngine/BodyInstance.h"` | Collision complexity enum | yes |
| `GEngine->Exec()` | `#include "Engine/Engine.h"` | Execute stat console commands | yes |
| `FPlatformFileManager` | `#include "HAL/PlatformFileManager.h"` | Scan for .utrace files | yes |
| `FPaths::ProfilingDir()` | `#include "Misc/Paths.h"` | Get profiling directory path | yes |
| `IFileManager::Get()` | `#include "HAL/FileManager.h"` | File enumeration and metadata | yes |

**Applicable Gotchas:**

| Issue | Impact | Mitigation |
|-------|--------|-----------|
| `.uasset` files are binary | Cannot read mesh data from files | Use UE reflection APIs — `UStaticMesh` render data accessors |
| Output log `since` cursor is byte-offset based | Stat command output may be missed | Capture cursor before stat command, read immediately after |
| PIE throttles to ~3fps when editor unfocused | Live stats inaccurate in throttled state | Add warning in sanity_check if frame time is suspiciously high |

---

## 5. Test Plan

### Tests for `analyze_scene`

**Happy path:** `UELLMToolkit.ScenePerformance.AnalyzeScene.HappyPath`
- Input: `{"operation": "analyze_scene"}`
- Expected: success with `scene_summary` containing `total_actors >= 0`, `mesh_actors >= 0`, `bottlenecks` array, `sanity_check` object

**With limit:** `UELLMToolkit.ScenePerformance.AnalyzeScene.WithLimit`
- Input: `{"operation": "analyze_scene", "limit": 5}`
- Expected: success with `bottlenecks` array length <= 5

**With sort_by:** `UELLMToolkit.ScenePerformance.AnalyzeScene.SortByTriangles`
- Input: `{"operation": "analyze_scene", "sort_by": "triangles"}`
- Expected: success with bottlenecks sorted descending by `triangle_count`

**Without live stats:** `UELLMToolkit.ScenePerformance.AnalyzeScene.NoStats`
- Input: `{"operation": "analyze_scene", "include_stats": false}`
- Expected: success with no `live_stats` field (or `live_stats.captured == false`)

**Invalid sort_by:** `UELLMToolkit.ScenePerformance.AnalyzeScene.InvalidSortBy`
- Input: `{"operation": "analyze_scene", "sort_by": "invalid"}`
- Expected: error containing "Invalid sort_by value"

**Empty scene:** `UELLMToolkit.ScenePerformance.AnalyzeScene.EmptyScene`
- Input: `{"operation": "analyze_scene"}` (in a level with no mesh actors)
- Expected: success with `mesh_actors == 0`, empty `bottlenecks`, warning in `sanity_check`

**Class filter:** `UELLMToolkit.ScenePerformance.AnalyzeScene.ClassFilter`
- Input: `{"operation": "analyze_scene", "class_filter": "StaticMeshActor"}`
- Expected: success with all bottleneck entries having `actor_class` containing "StaticMeshActor"

### Tests for `list_traces`

**Happy path:** `UELLMToolkit.ScenePerformance.ListTraces.HappyPath`
- Input: `{"operation": "list_traces"}`
- Expected: success with `traces` array and `trace_dir` string

**No profiling directory:** `UELLMToolkit.ScenePerformance.ListTraces.NoProfDir`
- Input: `{"operation": "list_traces"}` (with no Saved/Profiling/)
- Expected: error containing "No profiling directory found"

### Tests for dispatch

**Unknown operation:** `UELLMToolkit.ScenePerformance.UnknownOperation`
- Input: `{"operation": "invalid_op"}`
- Expected: error containing "Unknown operation"

**Missing operation:** `UELLMToolkit.ScenePerformance.MissingOperation`
- Input: `{}`
- Expected: error containing "Missing required parameter"

---

## 6. Deliverables Checklist

- [ ] `MCPTool_ScenePerformance.h` — tool header with class declaration and handler methods
- [ ] `MCPTool_ScenePerformance.cpp` — tool implementation with operation dispatch
- [ ] `SceneAnalyzer.h` — utility header: `FActorMeshStats` struct, `FSceneAnalysisResult` struct, analysis functions
- [ ] `SceneAnalyzer.cpp` — utility impl: mesh stats gathering, cost estimation, recommendation engine
- [ ] `MCPToolRegistry.cpp` — add `RegisterTool(MakeShared<FMCPTool_ScenePerformance>())`
- [ ] `ScenePerformanceTests.cpp` — automation tests for all operations
- [ ] `validation-checklist.md` — manual testing guide
- [ ] Domain docs updated: no
- [ ] Context docs updated: no
