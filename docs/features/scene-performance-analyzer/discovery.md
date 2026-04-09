# Discovery — scene-performance-analyzer

**Date:** 2026-04-09
**Status:** Approved

---

## 1. Feature Summary

**Description:** A fully autonomous scene performance analysis tool that inspects every actor's mesh data (triangle counts, LODs, materials, mesh sections) and combines it with live aggregate stats to produce a structured report identifying bottleneck actors and recommending specific optimization fixes (LOD additions, mesh merging, material simplification, culling distance changes).

**UE Systems:** Unreal Insights / Profiling, Level / World / Actors, Rendering / Viewport

---

## 2. Operations

### Operation: `analyze_scene`
**Description:** Scans all actors in the current level, gathers per-actor mesh statistics (triangle count, LOD count, material count, mesh sections, shadow settings, collision complexity), captures live aggregate stats via console commands, cross-references actor costs against aggregate totals, validates data sanity, and returns a structured report with ranked bottlenecks and actionable recommendations.

**Inputs:**
- `operation` (string, required): `"analyze_scene"`
- `limit` (number, optional, default 20): Max bottleneck actors to return
- `sort_by` (string, optional, default `"estimated_cost"`): Ranking metric — `"triangles"`, `"draw_calls"`, `"materials"`, `"estimated_cost"`
- `include_stats` (boolean, optional, default true): Whether to also capture live aggregate stats via console commands
- `class_filter` (string, optional): Only analyze actors of this class

**Output:** JSON object containing:
- `scene_summary`: total actors, total triangles, total estimated draw calls, total materials, actors without LODs
- `live_stats` (if `include_stats`): aggregate frame time, GPU time, draw call count from stat commands
- `sanity_check`: validation result — whether data looks plausible, warnings if static/suspicious
- `bottlenecks`: array of top N actors ranked by cost, each with:
  - `actor_label`, `actor_class`, `location`
  - `triangle_count`, `lod_count`, `material_count`, `mesh_section_count`
  - `shadow_casting`, `collision_complexity`
  - `estimated_draw_calls`
  - `recommendations`: array of specific fix suggestions (e.g., "Add LODs — mesh has 50k triangles but 0 LODs", "Disable shadow casting — small actor with high shadow cost")
- `overall_recommendations`: scene-wide suggestions (e.g., "Consider HLOD for 45 actors in zone X", "32% of draw calls come from unique materials — consider material instancing")

### Operation: `list_traces`
**Description:** Lists available `.utrace` files in the project's profiling directory for future CSV-based analysis (backup approach).

**Inputs:**
- `operation` (string, required): `"list_traces"`
- `path_filter` (string, optional): Filter by subdirectory

**Output:** Array of trace files with filename, size, and creation date.

---

## 3. Success Criteria

- `analyze_scene` returns accurate per-actor triangle counts that match what the editor's Statistics window shows
- LOD presence/absence is correctly detected per mesh
- Material count per actor matches the number of material slots on its mesh components
- Recommendations are specific and actionable — each references a named actor and a concrete fix
- Data sanity validation detects: empty scenes, scenes with zero mesh actors, suspiciously uniform data
- Live stats capture (when enabled) returns parseable frame time, GPU time, and draw call numbers from the output log
- Handles edge cases: actors with no mesh components are skipped, instanced meshes are counted once with instance count noted

---

## 4. Existing Tool Overlap

| Existing Tool | Overlapping Capability | Relationship |
|---------------|----------------------|-------------|
| `get_level_actors` | Actor enumeration with class/name filtering | Used internally — analyze_scene builds on actor enumeration |
| `level_query` | Actor listing with component info | Used as reference — analyze_scene needs deeper component inspection |
| `asset` | `get_asset_info` for asset metadata | Used internally — to fetch mesh asset triangle/LOD data |
| `blueprint_query` | Component tree inspection | Used as reference — pattern for component traversal |
| `run_console_command` | Execute `stat` commands | Used internally — for live stats capture |
| `get_output_log` | Parse stat command output | Used internally — to read stat results |
| `capture_viewport` | Screenshot capture | Not used — screenshots don't add value for this analysis |

No existing tool provides the combined analysis + recommendations. This is a new tool that leverages existing infrastructure internally.

---

## 5. Architecture Decision

**Tool type:** New tool
**Utility classes needed:** Yes — `FSceneAnalyzer` utility class to separate mesh inspection logic (triangle counting, LOD detection, cost estimation) from JSON dispatch. This logic is complex enough to warrant independent testability.
**Tool name:** `unreal_scene_performance`
**Annotations:** `ReadOnly()` — purely analytical, no modifications
**File organization:**
- `MCPTool_ScenePerformance.h` — tool header
- `MCPTool_ScenePerformance.cpp` — tool implementation (JSON dispatch, parameter handling)
- `SceneAnalyzer.h` — utility class header (mesh stats gathering, cost estimation, recommendation engine)
- `SceneAnalyzer.cpp` — utility implementation

---

## 6. Known Gotchas

| Issue | Impact | Mitigation |
|-------|--------|-----------|
| `.uasset` files are binary | Cannot read mesh data from files directly | Use UE reflection APIs (`UStaticMesh::GetNumTriangles()`, LOD accessors) |
| Output log `since` cursor is byte-offset based | Stat command output may be missed if cursor is stale | Capture cursor before issuing stat command, read immediately after |
| PIE throttles to ~3fps when editor unfocused | Live stats may be inaccurate if captured during throttled state | Document requirement: editor must be focused, or use `t.MaxFPS 60` |
| Instanced Static Meshes | Triangle count per instance vs total could be misleading | Report both instance count and per-instance triangle count |

---

## 7. Deliverables

- [ ] Tool header: `Source/UELLMToolkit/Private/MCP/Tools/MCPTool_ScenePerformance.h`
- [ ] Tool implementation: `Source/UELLMToolkit/Private/MCP/Tools/MCPTool_ScenePerformance.cpp`
- [ ] Utility class: `Source/UELLMToolkit/Private/MCP/SceneAnalyzer.h` + `SceneAnalyzer.cpp`
- [ ] Registry update: `MCPToolRegistry.cpp`
- [ ] Test file: `Source/UELLMToolkit/Private/Tests/ScenePerformanceTests.cpp`
- [ ] Domain doc update: none (new domain area, but covered by feature docs)
- [ ] Context file update: none
- [ ] Validation checklist: `docs/features/scene-performance-analyzer/validation-checklist.md`

---

## 8. Gaps & Recommendations

- **UE API verification needed:** `UStaticMesh::GetNumTriangles()`, `GetNumLODs()`, and `UMaterialInterface::GetMaterial()` need to be verified as available in UE 5.7. This will be checked during the verify phase.
- **Cost estimation model:** The "estimated cost" ranking needs a weighting formula (e.g., triangles x materials x shadow_factor). The spec phase should define this formula explicitly.
- **Backup approach:** If static analysis proves insufficient for accurate recommendations, `.utrace` to CSV parsing can be added as a second operation. The `list_traces` operation is included as a placeholder for this.
- **Large scene performance:** Scenes with 10k+ actors could make `analyze_scene` slow. Consider pagination or a `max_actors` scan limit.
