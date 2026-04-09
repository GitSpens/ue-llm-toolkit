# UE LLM Toolkit — Known Gotchas & Workarounds

Extracted from `Plugins/UELLMToolkit/Resources/domains/`.

## Critical Issues

| Issue | Impact | Workaround |
|-------|--------|------------|
| AnimBP duplication broken | Stale generated class references | Always build from scratch, never duplicate |
| Struct layout + live coding | Silent memory corruption, crash | Always full rebuild after struct changes |
| `SetFrameRate()` crash (UE 5.7) | Assertion on non-multiple rate changes | FBX reimport with `custom_sample_rate` |
| Missing `UPROPERTY()` on `TObjectPtr` | GC collects it, instant crash | Always mark with UPROPERTY() |
| State graph ops without `state_machine` param | Silent failure | Always pass `state_machine` + `state_name` |
| CDO changes don't affect placed actors | Values serialized in `.umap` | Also `set_property` on instances or re-place |

## Code Domain

- `SavePackage` returns `bool` in 5.7 (not `FSavePackageResultStruct`)
- Interchange is default import pipeline — look for `UInterchangeAssetImportData`, not `UFbxAssetImportData`
- `get_asset_by_object_path` deprecated → use `GetAssetsByPath()` + filter
- Win32 `TRUE`/`FALSE` macros undefined after `HideWindowsPlatformTypes.h` — use `1`/`0`
- `SetupAttachment()` is construction-only; runtime requires `AttachToComponent()` with `FAttachmentTransformRules`

## Blueprints Domain

- `add_transition` ignores duration param → always 0.2s default; use `set_transition_duration` after
- `setup_transition_conditions` broken — `type`/`comparison_type` ignored; use `add_comparison_chain`
- State machine internal names differ from display names
- 1D BlendSpace pin name is always `"X"` regardless of axis display name
- Enum values require fully qualified names: `EMyEnum::ValueName` (bare names silently fail for `TEnumAsByte`)
- After `add_layer_interface`, recompile before `list_layers`

## Assets Domain

- FBX import doesn't auto-enable root motion → enable via `retarget set_root_motion`
- IK Rig: Pelvis chain must be named "Pelvis" not "Root"
- `add_default_ops()` creates duplicates (6 real + 5 `_0` dupes) → remove `_0` immediately
- FK Pelvis `TranslationMode` should be `GLOBALLY_SCALED` (not `NONE`)
- `import_text()` replaces ALL fields — must include `ChainMapping` or mappings get wiped
- Blender FBX roundtrip: bake armature +90 X rotation into edit bone matrices before re-export
- `.uasset` files are binary — never read directly, use plugin tools

## Debug Domain

- Digital actions (IA_Equip, IA_Jump) require `hold` step type with 100ms+ duration
- Axis actions (IA_Move, IA_Look) work with single `input` frame
- Consecutive digital presses need 50ms zero-value hold between them
- `delay_ms` is relative to previous step's START, not absolute
- Output log `since` cursor is byte-offset based, not timestamp-based
- PIE start is async — poll `pie_status` for manual ops
- Editor throttles PIE to ~3fps when unfocused
- `run_console_command SaveAll` triggers confirmation dialog — use `save_all` instead
