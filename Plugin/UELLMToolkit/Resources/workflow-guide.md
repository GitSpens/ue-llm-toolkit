# UE LLM Toolkit — Workflow Guide

**This is NOT a "prompt-to-game" tool.** It is a debugging, analysis, and productivity multiplier for developers who already know what they want to build. Claude's role is to help execute the developer's vision using the plugin's tools — not to make design decisions autonomously.

## Recognizing Plugin Usage Intent

When the user asks to create, modify, test, inspect, debug, or build anything in Unreal Engine, Claude should follow the workflow below **before taking any action**. Look for requests like:
- "Create a combat system" / "Add a new animation" / "Set up input mapping"
- "Test the player movement" / "Debug why X isn't working"
- "Show me what's in the level" / "Inspect the player blueprint"
- "Import this FBX" / "Set up retargeting"
- Any request that involves reading or writing UE editor state

## First-Time Setup Check

Before starting any work, verify the plugin is ready:

1. **Check if `domains/` exists at the project root.** If not, the user hasn't run initial setup.
2. Tell the user: *"I see the UE LLM Toolkit plugin is installed, but project domains haven't been generated yet. Let's do that first — say `prep one time init` and I'll scan your project and generate the domain files that help me understand your project structure."*
3. **Check plugin health:** `bash Scripts/ue-tool.sh status` — if this fails, the editor isn't running or the plugin didn't load.

## Per-Session Workflow (Follow This Every Time)

**Step 1 — Load Domains First**
Before doing ANY work, load the relevant domain files. Ask the user or infer from their request:

| Task Type | Domains to Load |
|-----------|----------------|
| C++ gameplay, components, build | `prep code` |
| Blueprint editing, AnimBP, state machines | `prep blueprints` |
| FBX import, retargeting, skeleton work | `prep assets` |
| PIE testing, input injection, output log | `prep debug` |
| Plugin architecture, extension patterns | `prep tooling` |
| Multiple areas | `prep code + blueprints` (combine as needed) |

Tell the user: *"For this task, I'll need to load the [X] domain(s) first. Say `prep [domain]` and I'll get the context I need."*

**Step 2 — Ask for Specifics**
Before proposing a plan, ensure the user has provided:
- **Explicit asset paths** (e.g., `/Game/Animations/Combat/`) — not vague references
- **Clear scope** — "add light and heavy attack montages" is better than "implement combat"
- **What already exists** — point Claude at existing assets/blueprints to inspect

If the request is vague, ask: *"Can you be more specific? Which Blueprint/asset/level should I be working with? What exactly should the end result look like?"*

**Step 3 — Plan Before Executing**
Always propose a plan and get approval before making changes:
1. Use read-only tools (`blueprint_query`, `level_query`, `asset_search`, etc.) to inspect current state
2. Present a numbered plan of what you'll do and which tools you'll use
3. Wait for the user to approve, modify, or reject the plan
4. Only then execute changes

*Plans are cheap. Undoing bad edits is not.*

**Step 4 — Execute With Dedicated Tools**
Use the plugin's dedicated tools rather than `execute_script` whenever possible:
- Spawn actors with `spawn_actor`, not a script
- Edit blueprints with `blueprint_modify`, not Python
- Query with `blueprint_query` / `level_query` / `asset_search`, not console commands

**Step 5 — Update Domains After Work**
After completing non-trivial work, remind the user: *"We've made significant changes. Consider updating your project domains so future sessions have this context: say `prep meta` and describe what we changed."*

## Debugging Pattern (Two Sessions)

For non-trivial bugs, recommend splitting into two sessions:
1. **Session 1 — Diagnose:** Gather output logs (`get_output_log`), capture viewport state, run PIE sequences with monitoring, add instrumentation. End with a clear diagnosis.
2. **Session 2 — Fix:** Start fresh with clean context, load the diagnosis, and focus purely on the code change.

## Calling Plugin Tools — Use the CLI Wrapper

Always use `Scripts/ue-tool.sh` instead of raw curl/HTTP. It reduces token usage by ~70%, handles connectivity checks, formats output compactly, and provides inline parameter docs.

```bash
bash Scripts/ue-tool.sh status                              # check editor + plugin state
bash Scripts/ue-tool.sh list                                # see all available tools
bash Scripts/ue-tool.sh help <tool>                         # get tool parameters
bash Scripts/ue-tool.sh call <tool> '{"param":"value"}'     # call a tool
bash Scripts/ue-tool.sh save                                # save all dirty assets
bash Scripts/ue-tool.sh close                               # save + graceful editor shutdown
bash Scripts/ue-tool.sh launch                              # start editor, wait for plugin
bash Scripts/ue-tool.sh --port 3001 call <tool> '{...}'     # multi-editor support
```

Do NOT construct raw curl commands to `localhost:3000`. Use the wrapper.

## Building C++ Changes

When the user's project has C++ source and changes need compiling, use `Scripts/build.sh`:

```bash
bash Scripts/build.sh          # auto-detect best strategy (live coding → VS → full rebuild)
bash Scripts/build.sh --live   # force live coding (fastest, editor must be running)
bash Scripts/build.sh --vs     # force Visual Studio build
bash Scripts/build.sh --clean  # close editor first, then VS build
bash Scripts/build.sh --full   # full rebuild (use when modules/macros/Build.cs changed)
```

The auto mode detects whether the editor and VS are running and picks the fastest viable strategy. Full build output goes to `/tmp/ue-build.log`.

## Domain Knowledge System

See `Plugins/UELLMToolkit/Resources/domains/README.md` for the full domain loading guide.

### `prep one time init`
When the user says this, read and follow `Plugins/UELLMToolkit/Resources/prep-init-guide.md` step by step.

### `prep <domain>`
Load both plugin domain (`Plugins/UELLMToolkit/Resources/domains/<domain>.md`) and project domain (`domains/<domain>.md`) if they exist.
