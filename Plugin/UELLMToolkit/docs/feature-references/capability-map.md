# Capability Map

## Existing Tools (44 tools)

| Category | Tool Name | Key Capabilities |
|----------|-----------|-----------------|
| **Scene Performance** | `unreal_scene_performance` | `analyze_scene` (per-actor mesh stats, tri counts, LODs, materials, shadows, estimated cost, recommendations), `list_traces` (.utrace file discovery) |
| **PIE/Debug** | `gameplay_debug` | `run_sequence` (atomic PIE lifecycle), `start/stop_pie`, `inject_input`, `start/stop/update_continuous`, `capture_pie`, `start/stop_monitor` (passive observation), `montage_*` ops |
| **Output Log** | `get_output_log` | Cursor-based incremental reads, category/verbosity filtering, duplicate collapsing, timestamp stripping |
| **Viewport** | `capture_viewport` | PIE or editor viewport screenshots, asset editor previews, camera positioning |
| **Console** | `run_console_command` | Execute arbitrary console commands (with safety blocklist) |
| **Level** | `level_query`, `spawn_actor`, `move_actor`, `delete_actors`, `set_property`, `get_level_actors`, `open_level` | Full level editing |
| **Blueprint** | `blueprint_query`, `blueprint_modify` | Full blueprint R/W |
| **Assets** | `asset`, `asset_search`, `asset_dependencies`, `asset_referencers`, `asset_import` | Full asset CRUD + FBX import/export |
| **Task Queue** | `task_submit`, `task_status`, `task_result`, `task_list`, `task_cancel` | Async task management |
| **Script** | `execute_script`, `cleanup_scripts`, `get_script_history` | C++/Python/console execution |

## Performance-Relevant UE Console Commands

| Command | What It Provides |
|---------|-----------------|
| `stat fps` | Frame rate + frame time |
| `stat unit` | Game thread, render thread, GPU time breakdown |
| `stat gpu` | Per-pass GPU timing |
| `stat scenerendering` | Draw calls, mesh draw calls, render passes |
| `stat rhi` | RHI-level draw primitives, triangles drawn |
| `stat memory` | Memory usage breakdown |
| `stat streaming` | Texture streaming stats |
| `stat particles` | Particle system counts and costs |
| `stat niagara` | Niagara-specific stats |
| `profilegpu` | Single-frame GPU profiler dump |
| `stat startfile` / `stat stopfile` | Start/stop .utrace capture |

## Key Utility Classes

| Class | Location | Purpose |
|-------|----------|---------|
| `FSceneAnalyzer` | `Private/SceneAnalyzer.h/.cpp` | Static scene analysis — per-actor mesh stats, cost estimation, recommendations |
| `FPIESequenceRunner` | (internal to GameplayDebug) | Timed PIE step execution |
| `FPIEFrameGrabber` | (internal to GameplayDebug) | Async GPU readback for high-speed capture |
| `FMCPToolBase` | `Public/MCP/MCPToolBase.h` | Base class for all MCP tools |
| `MCPToolRegistry` | `Private/MCP/MCPToolRegistry.cpp` | Maps tool names to instances |
