# UE LLM Toolkit — Capability Map

All tools in `Plugins/UELLMToolkit/Source/UELLMToolkit/Private/MCP/Tools/`.

| Tool Name | Description | Key Operations |
|-----------|-------------|----------------|
| anim_blueprint_modify | Animation Blueprint modification | State machines, states, transitions, condition graphs, animation assignment, layer interfaces, validation, layout |
| anim_edit | Animation track editing | Adjust/inspect tracks, resample, curves, sync mesh bones, rename bone, ref pose, additive type, transform vertices, extract range |
| asset | Generic asset operations via reflection | Set property, save, get info, list, rename, duplicate, delete, migrate, folder ops, hashes, open editor, preview mesh |
| asset_dependencies | Query asset dependencies | Hard/soft reference filtering, pagination |
| asset_import | FBX import/export/reimport | Single/batch FBX import, export, reimport from source, source info |
| asset_referencers | Query assets referencing a target | Hard/soft reference filtering, impact analysis |
| asset_search | Search assets | Filter by class, path prefix, name pattern, pagination |
| audio | Audio asset inspection/editing | Inspect/list sounds, audio components, sound cue, attenuation, ambient sound |
| blend_space | Blend space read/write | Inspect, list, create, add/remove/move samples, set sample anim, set axis, batch |
| blueprint_modify | Blueprint creation/modification | Create, reparent, variables, functions, nodes, pins, component/CDO defaults, debug print, layout, compile |
| blueprint_query | Blueprint inspection (read-only) | List, inspect, graphs, components, collision, event/anim graph, state machine detail, defaults, nodes, exec chain, graph summary |
| capture_viewport | Viewport/editor screenshots | PIE viewport, asset editor preview, frame scrubbing, camera positioning |
| character | Character actor operations | List, info, movement params, components |
| character_data | Character DataAssets & DataTables | Create/query/update character data, stats tables, apply to runtime |
| cleanup_scripts | Remove generated scripts | Destructive cleanup |
| control_rig | Control Rig editing | Inspect, structs, templates, nodes, variable nodes, pin links, defaults, member variables, recompile, batch |
| delete_actors | Delete actors | Single/multiple/by class filter |
| enhanced_input | Enhanced Input assets | Create InputAction/MappingContext, add/remove mappings, triggers, modifiers, query |
| execute_script | Execute custom scripts | C++, Python, console commands, Editor Utility scripts with history |
| game_framework | Game framework config | Game mode, input mode, widgets, pause, mouse cursor, DataTable ops |
| gameplay_debug | PIE gameplay automation | Run sequences, start/stop PIE, inject input, continuous input, capture, montages, monitoring |
| get_level_actors | Level actor queries | Class/name filtering, pagination |
| get_output_log | Output log retrieval | Cursor-based reads, category/verbosity filtering, duplicate collapsing |
| get_script_history | Script execution history | Recent script metadata |
| level_query | Smart level actor queries | List gameplay actors, find by pattern, detailed info (noise-filtered) |
| lighting | Lighting/atmosphere/post-process | Inspect/list/spawn lights, light properties, sky light, atmosphere, fog, post-process volumes |
| material | Material operations | Create instances, set parameters, set skeletal/actor materials, get info |
| metasound | MetaSound editing | Inspect, list nodes, get graph, create, add/remove nodes, connect/disconnect, input defaults, graph I/O, preview |
| montage_modify | Animation Montage editing | Info, create, save, sections, link sections, segments, slots, notifies, blend in/out, curves |
| move_actor | Actor transforms | Absolute/relative position, rotation, scale |
| niagara | Niagara particle systems | Inspect/list systems, parameters, emitter info, set parameter, spawn |
| open_level | Level management | Open, create, create from template, save as, list templates |
| retarget | Animation retargeting | Skeleton inspect, IK rig creation, retargeter setup, FBX import, batch retarget, root motion, analysis |
| run_console_command | Console command execution | Direct command execution |
| sequencer | Sequencer/Take Recorder | Take recorder start/stop/status, sequencer open/scrub/state/close |
| set_property | Set actor properties | Dot notation for nested/component properties |
| spawn_actor | Spawn actors | Class, location, rotation, scale |
| task_cancel | Cancel async tasks | Cancel pending/running tasks |
| task_list | List async tasks | Filter by status, pagination |
| task_result | Get async task results | Full result data for completed tasks |
| task_status | Get async task status | Status, progress, timing |
| task_submit | Submit async background tasks | Submit any tool for background execution with timeout |
| widget_editor | Widget Blueprint editing | Inspect tree, add/remove widgets, set properties, slot/brush, reparent/reorder/clone, event/property bindings, animations, batch |
