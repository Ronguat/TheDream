# Authoring and transferring animation

**Trigger: before authoring a clip, or moving one between Unreal and Cascadeur.** Not otherwise.
`Docs/Animation-Library.md` is the other half — what already exists and how to find it; this is how
to make something that does not.

**The capability exists because selection under constraint is a search problem.** Polish's bespoke
windup pass wants "a legible anticipation apex to blend into", which is a request no library
reliably answers. Authoring dissolves it. Past the current roster, a second weapon makes this
permanent infrastructure rather than a one-off.

**Everything below was measured, not assumed.** Dates are the measurement's, and a limit is a
measurement with a shelf life — re-test any that blocks you and record the result.

---

## The route

**Cascadeur authors from endpoints Unreal supplies** *(decided 2026-08-22; text-to-motion was tried
and rejected, reasoning in that day's decision entry)*. The clip's first and last frames are poses
that already exist in the game, so the result joins what it has to join:

- Frame 0 — the held ground pose, `AS_SwordSwordAnimV3_Death_Bw_RM` at 0.9 s with auto blend-out off
- Last frame — the idle, `AS_SwordSwordAnimV3_Idle1_IP` frame 0, the locomotion blendspace's zero sample

Both are on our skeleton, and Cascadeur ships a rigged UE5 Manny, so the clip returns on our bone
names without retargeting. **The script owns the endpoints, the transfer, the timing and the
montage; the designer's eye owns the middle.**

`AS_GetUpAttack` is the first output. `Tools/AnimPipeline/` holds every script named here.

## The constraint that shapes every output

**Displacement is authored where the distance drives gameplay** — `Combat-Spec.md`. A clip serving
an authored distance must therefore carry no travel of its own.

**The failure is silent and total.** Animation root motion suppresses *every* root motion source, so
a montage carrying root motion produces **zero** lunge rather than a doubled one. `bEnableRootMotion
= false` is not enough on its own: the displacement stays baked and the mesh drifts off the capsule.
**`bForceRootLock = true` is the flag that makes an `_RM` clip behave in place.**

**A force-locked clip can foot-slide**, because its body motion was choreographed to cover its own
distance. Pinning the root while the capsule travels differently sells a stride nobody is taking.
Stationary moves escape this; a lunging one will not.

The one deliberate exception is `AM_KipUp`, which keeps its own root motion by ruling — see the
spec's displacement rule and the tuning map's row.

## The surface, measured

Confirmed 2026-08-22 unless noted.

**Python in the editor** — 3.11.8. `py <file>` runs a script file; `ProgrammaticToolset` is **not** a
route, its sandbox refusing `import unreal`. **Remote execution is on and verified**, and
`Tools/AnimPipeline/run-in-editor.py` wraps Epic's `remote_execution.py` under the engine's own
Python. **It has to be enabled in `Config/DefaultEngine.ini`, not by toggling the settings CDO** — a
live toggle dies with the editor, so the next launch finds it off and every call fails with
`Unable to connect`. That is the workhorse — the fallback is `py`
through the editor's Cmd box with `LogPython` read back.

**Cascadeur Pro** — `%LOCALAPPDATA%\Cascadeur`, user-mode, bundled Python 3.11,
`samples/UE5_Manny.casc`. Its **native MCP server listens on `127.0.0.1:8765`**: `POST /mcp`
(JSON-RPC, one tool — `run_script(code)`, run on the next scene-idle event) and `POST /run
{"code"}`. Source under `resources/scripts/python/scripts/mcp`; it ships its own `.mcp.json`.
`Tools/AnimPipeline/casc-run.sh` drives it over HTTP. **Starting it needs no human**:
`cascadeur.exe --run-script scripts.mcp.start_server` forwards into the already-running instance
rather than spawning a second, comes up in about 30 s, and `GET /health` answers once it has. Community bridges do the same job
(`ysk424/cascadeur-mcp`, TCP with viewport capture; `BYGGOLDENSTONE/cascadeur-mcp-bridge`,
file-polling). **Registering any of them takes a Claude Code restart** — the startup rule.

**Both FBX directions are script-exposed.** UE: `AssetExportTask` + `FbxExportOption`,
`AssetImportTask` + `FbxImportUI`, all `BlueprintType`. Cascadeur: `FbxLoader.import_animation` /
`export_joints`, `FbxSettings{mode, up_axis, bake_animation}`. Cascadeur's documented UE settings:
import preset *Animation*, up axis Z, adjust axis *Root*; export binary FBX, import onto
`SK_Mannequin` with animations only.

**Notifies are writable from Python** — `unreal.AnimationLibrary`, which the toolset cannot reach.
`add_animation_notify_state_event(montage, track, start, duration, cls)` on any `UAnimSequenceBase`;
`get_animation_notify_events` with `get_anim_notify_event_trigger_time` / `_duration` to read back;
`add_animation_notify_track` for the track. It does **not** dirty the package — `modify()` does,
`EditorLoadingAndSavingUtils.save_packages` saves, and `git status` is the check.

**Live Link is a secondary route and half-absent.** Cascadeur's side is present
(`tool_ue_live_link.dll`); the UE plugin is not in the engine — searched `Engine/Plugins`, the
project's `Plugins/` and the uproject. FBX does not need it.

**The IK retarget API is Python-exposed** — `IKRigController.ApplyAutoGeneratedRetargetDefinition`,
`IKRetargetBatchOperation.RunBatchRetarget`, both with factories — and **gates nothing today**,
being the route for a foreign-skeleton source. There is no IK Rig or IK Retargeter *toolset*.

### Not measured

- Whether **AutoPosing**, **AutoPhysics** and `View.MotionGeneration_Run` answer a script. Menu
  action ids exist for all three; the tool classes document `add/update/activate` and nothing beyond.
- **Meshy** — no account, and its Animation endpoint is undocumented.

## The round trip, and the one fix it needs

**Fidelity is exact** *(2026-08-22)*: 89/89 joints, frame-exact lengths, every bone within **0.13°
and 0.04 cm** across a full export-import cycle. Scripts: `casc_roundtrip.py`,
`ue_roundtrip_check.py`.

**That took one fix, and the fix is conditional.** Cascadeur's UE5 Manny bakes a **−90° roll into
`root`** on export, and the remedy — its own docs' — is the UE import's `import_rotation` roll of
**+90**, which cancels it. `convert_scene` and `force_front_x_axis` do nothing for it.

**But the roll is not always there.** A gap-edited scene's export carried none, and applying the
same fix over-rotated it. So `ue_import_clip.py` **imports plain, reads `root`, and re-imports
rotated only when it reads −90**. Do not make it unconditional.

Two smaller things the round trip taught: **`take_image` renders after the script returns**, so a
capture must be its own `/run` call; and a clip with baked travel walks out of the default camera.

## Assembling a clip

**The rough is built in UE, not posed in Cascadeur** *(2026-08-22)*. `ue_build_rough.py` writes every
bone track key-exact and verifies its anchors to 0.000° — for `AS_GetUpAttack` that was frames 0–3
the ground pose, 10–30 `Attack2_Stage2_IP` frames 8–28, 45–46 the idle, root held at the idle's.
*(`ue_bone_speed.py` finds a motion peak when one is needed; the attack's right hand peaks at frame
19.)*

**Cascadeur then receives it baked and owns only the gaps.** `casc_open_gaps.py` drops the keys in
the spans to be inbetweened and sets the intervals before them to `AI` interpolation, so Cascadeur's
physics fills them. **Assembly moved to UE because `Timeline.Remove frames` ignores the API's frame
selection** — that is the reason, not preference.

**The AI fill is real and measurable**: pelvis 12.6 → 78 cm across frames 3–10 on the get-up attack,
against a linear ramp of exactly +9.365 cm per frame in the un-inbetweened rough — 10.3 cm apart at
the widest. The processed clip re-imports matching the rough on every anchor to **≤0.07°**.

**Save the scene with `view.Scene.save`.** `DataSourceManager.save_scene_as` does not write.
Authored scenes are tracked under `AnimSource/`.

## Making the montage, and promoting out of Scratch

**Montage-from-clip is scriptable** *(2026-08-22)*: `AnimMontageFactory` with `target_skeleton` and
`source_animation` through `AssetTools.create_asset`. `ue_make_montage.py` does that, places the
Release Window, and prints the derived phase rates and the blend-out check.

**Montages are ~90% scriptable** *(2026-08-21)*. `slotAnimTracks` reads and writes whole;
**multi-section montages are out**, and `compositeSections` is neither readable nor writable.

**Repointing a montage's segment is a toolset write, not a Python one** *(2026-08-24, both failure
modes seen)*. `set_editor_property("anim_reference", clip)` on the segment returns without error and
**changes nothing** — the struct comes back by value, so the mutation lands on a copy. Writing every
nested struct back whole in Python fails the same way. **`ObjectTools.set_properties` with the whole
`slotAnimTracks` array is what lands it**, and it takes `values` as a JSON *string*. Verify through
Python afterwards, so the read does not go back through the layer that wrote it.

**Promoting out of `Scratch/` is a rename**, not a copy: `EditorAssetLibrary.rename_asset` fixes up
referencers, then resolve any `ObjectRedirector` left behind. The montage's segment does **not**
follow automatically if it pointed at a different clip — repoint it explicitly, then check the
Release Window survived by reading it back.

## Deferred, with reasons

- **The IK retarget path.** Built when a foreign-skeleton source arrives; the API is named above.
- **The two-clip composition** — a rise front-half spliced to `Attack2_Stage2`'s 360° spin. It is
  **buildable**: eight segments in `AM_Dodge` prove the structure and the trim is one field.
  Deferred on **quality, not difficulty** — the designer's assessment is that it reads badly, and a
  placeholder reaching Interplay teaches the wrong tell for a move whose whole read is *from the
  floor*. It survives as a **test fixture**, worth building only if authoring ever stalls long
  enough that mechanical verification would otherwise sit idle. It never reaches a feel verdict.
