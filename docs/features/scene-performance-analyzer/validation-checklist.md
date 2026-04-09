# Validation Checklist — scene-performance-analyzer

## Prerequisites
- [ ] Unreal Editor running with UELLMToolkit loaded
- [ ] Plugin HTTP server responding (check http://localhost:3000/mcp/status)
- [ ] A level with mesh actors loaded (for meaningful analyze_scene results)

## Automated Tests
- [ ] Run `Automation RunTests UELLMToolkit.ScenePerformance` in editor console
- [ ] All tests pass

## Manual Operation Tests

### Operation: analyze_scene

- [ ] **Happy path:** Call with `{"operation": "analyze_scene"}`. Expected: success response with `scene_summary`, `bottlenecks` array, `sanity_check`, `live_stats`, and `overall_recommendations`
- [ ] **With limit:** Call with `{"operation": "analyze_scene", "limit": 3}`. Expected: success with `bottlenecks` array length <= 3
- [ ] **Sort by triangles:** Call with `{"operation": "analyze_scene", "sort_by": "triangles"}`. Expected: success with bottlenecks sorted descending by `triangle_count`
- [ ] **Without live stats:** Call with `{"operation": "analyze_scene", "include_stats": false}`. Expected: success with no `live_stats` field in response
- [ ] **Class filter:** Call with `{"operation": "analyze_scene", "class_filter": "StaticMeshActor"}`. Expected: success with all bottleneck entries having `actor_class` containing "StaticMeshActor"
- [ ] **Error — invalid sort_by:** Call with `{"operation": "analyze_scene", "sort_by": "invalid"}`. Expected: error containing "Invalid sort_by value"
- [ ] **Empty scene:** Open a level with no mesh actors and call `{"operation": "analyze_scene"}`. Expected: success with `mesh_actors == 0`, empty `bottlenecks`, warning in `sanity_check`
- [ ] **Operation alias:** Call with `{"operation": "analyse_scene"}` (British spelling). Expected: same as happy path (alias resolved)

### Operation: list_traces

- [ ] **Happy path:** Call with `{"operation": "list_traces"}`. Expected: success with `traces` array and `trace_dir` string
- [ ] **With path filter:** Call with `{"operation": "list_traces", "path_filter": "SomeSubdir"}`. Expected: error if subdirectory doesn't exist, or filtered results if it does
- [ ] **No profiling directory:** If `Saved/Profiling/` doesn't exist, call `{"operation": "list_traces"}`. Expected: error containing "No profiling directory found"

### Dispatch errors

- [ ] **Unknown operation:** Call with `{"operation": "invalid_op"}`. Expected: error containing "Unknown operation"
- [ ] **Missing operation:** Call with `{}`. Expected: error containing "Missing required parameter"

## Output Log Checks
- [ ] No warnings or errors in output log during test operations
- [ ] Tool responses match expected JSON structure from spec
- [ ] Live stats values are reasonable when editor is focused (frame time < 100ms)
