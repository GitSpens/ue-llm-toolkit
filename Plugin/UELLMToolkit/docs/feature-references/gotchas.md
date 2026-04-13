# Known Gotchas

## PIE / Runtime

| Issue | Impact | Workaround |
|-------|--------|------------|
| Editor throttle during PIE | PIE runs ~3fps when editor unfocused | Force `t.MaxFPS 60` + steal foreground (sequence runner does this) |
| Frame time >100ms in editor | Live stats unreliable when throttled | Warn user; compare relative, not absolute values |
| PIE start is async | `RequestPlaySession()` queues for next tick | Poll `pie_status` or use `run_sequence` (handles internally) |
| `since` cursor semantics | Byte-offset based, not timestamp — calling after data written returns 0 lines | Grep log file directly for post-hoc analysis |
| Live reload crashes on struct changes | Hot reload corrupts struct layout | Full rebuild after struct changes |
| Spring arm follows pawn | Viewport captures always show camera behind character | Use `camera_location`/`camera_rotation` for custom angles |

## Stats / Profiling

| Issue | Impact | Workaround |
|-------|--------|------------|
| `stat` commands write to viewport overlay | No structured API to read stat values programmatically | Parse output log or use C++ stat API directly |
| `stat unit` only updates when visible | Values stale if stat not active | Enable stat before capture window |
| GPU profiling requires GPU idle | `profilegpu` forces a GPU flush | Only use for single-frame analysis, not continuous |
| Editor overhead in stats | Editor UI, asset browser etc. inflate frame times | Compare PIE-only metrics; document editor overhead |

## Tool Architecture

| Issue | Impact | Workaround |
|-------|--------|------------|
| All tools run on game thread | Long operations block editor | Keep analysis bounded; use async task queue for heavy work |
| Game thread dispatch via MCPTaskQueue | HTTP server queues lambdas | Timeout handling prevents indefinite hangs |
| `FMCPToolAnnotations::ReadOnly()` | Read-only tools can't start/stop PIE | Use `Modifying()` for tools that control PIE lifecycle |
