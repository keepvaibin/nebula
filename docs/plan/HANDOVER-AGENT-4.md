# Agent 4 hand-over to Agent 1 (2026-11-18, owner's stop order of 08:22)

Nothing of mine runs: the quick gate of eb5f08d5a was stopped part way, no lock or queue entry is held, no GitHub run
is pending. Git: `/c/Program Files/Git/cmd/git.exe` (PowerShell's git is msys2 and sees every file changed).

## Branches

| Repository | Branch | Tip | Holds that main does not |
|---|---|---|---|
| nebula | link/work | this file's commit, on eb5f08d5a | M13: docs checked against the code (parts 1 to 13), gate.py `lumalink_tests` env (LUMA_TEST_BIN, LUMA_TEST_RUNNER), pin 7524fa3, rows N-4094 to N-4100, M-0xx row texts, this file |
| nebula | link/m10 | 5854c653b | M-058, M-061, M-062, M-069 (mouse look hold, SDL grab, live controls with no host work); based on app/work b61e80c09 |
| nebula | wip/agent-4 | see the board line | docs part 14, not checked (see below) |
| lumalink | link/work | 7524fa3 | `optional <game call>` (ddf38a4, S0162, LUMA_OPTIONAL rows); build checks text rows against the mod's merge files (7524fa3); riivolution timing test best of 5 |
| lumalink | wip/agent-4 | ed4edce | VS Code extension words in 14 languages: NOT built, NOT tested |

Pin: nebula `tools/lumacore/lumalink.commit` = 7524fa32a85b133482c741fe10cc0d4025acf008 (set in eb5f08d5a).
LumaLink workspace at 7524fa3: `cargo test --workspace` 498 passed, 0 failed (in-game test included).

## Landing (as the Planner set it, 06:49)

1. app/work reaches main first (Agent 3's rows P-076, P-102 are in it).
2. link/m10 then lands on main through its own gate: `python tools/gate.py` on a tree at link/m10 rebased on main.
3. link/work lands after: its last full quick gate (2c4051083) passed 23 of 24; the one failure was riivolution's
   timing test, fixed in 7524fa3. The quick gate of eb5f08d5a was stopped: run `python tools/gate.py --quick` again.
4. LumaLink link/work goes to LumaLink main at the same time as the nebula commit that pins it.

## Open rows

| Row | Coded where | Owed | Proof |
|---|---|---|---|
| M-057 | app/work 874754e3b (P-076, macOS GCMouse counts) | Mac run, the owner's | owner's Mac test |
| M-058 | link/m10 5854c653b | Mac run (the Mac jobs only compile it) | owner's Mac test; CI macOS job builds |
| M-061 | link/m10 5854c653b | land | `ctest -R galaxy_mouse_look_hold_tests` |
| M-062 | link/m10 5854c653b | land | `ctest -R galaxy_mouse_look_hold_tests` (sdl_grab_where_relative_is_refused) |
| M-063 | app/work 70192173c (P-102, ControlsRulesTest.mouseEvent) | tablet run with a mouse after landing | tablet R52WA0FXE5J, never touch dev.nebula.galaxy |
| M-069 | link/m10 5854c653b | land | `ctest -R galaxy_live_controls_engine_tests` (no_look_no_host_work) |
| N-4005 | main | Mac/iPhone proofs world/2491-native, audio/2606, audio/2607 | owner's later test |
| N-4075 | c80e1a971, in 82ff9390a | closed in ledger.tsv by this commit | run 44334406274: both Linux sanitizer jobs green |

Rows filed for Agent 2 (now Agent 1's):
- N-4060: open as filed; ledger.tsv has the text.
- N-4090: the L206 message; ledger.tsv has the text.
- N-4094: kept text outlives the mod. Agent 2 planned `keep_text` and keeping libraries mapped. My half (lumart:
  `lumart_text_keep` calls `keep_text`; `lumart_end` stops freeing) comes after that commit; not coded.
- N-4095: overrides of a second base's virtuals; not coded.
- N-4076: the draw.view cut, after e13a is on main.

## VS Code words (lumalink wip/agent-4, ed4edce)

Text rows `LumaLink.Editor.Vscode.*` (11) in LumaLinkText.tsv. editors/vscode/make.py writes package.nls*.json,
l10n/bundle.l10n.*.json and messages.js; extension.js uses `vscode.l10n.t(M.X, ...)`. Owed:
- `cargo test -p lumalink-core vscode` (the_extension_says_its_words_in_every_language failed before make.py ran,
  not run after) and `every_id_has_a_row_and_every_row_is_said`.
- installer/vsix.py: resolve `%description%` from package.nls.json for the manifest; list LocalizedLanguages.
- docs how-to setting-up-an-editor quotes "LumaLink could not start."; the real text is the NoStart row.
- The translations are mine, not a translator's: check them like any new row.

## Docs part 14 (nebula wip/agent-4)

concepts/lumascript.md (temporaries are given back once used, not every frame), the-game-description.md and
glossary.md (the description is the `luma` folder), mod-files-and-layers.md (`merge/` in the layer listing, a section
on adding to a file many mods add to). Not run through docs_check: run `python tools/docs_check.py` before landing.

## How far the M13 reading went

Read and fixed: every reference page (parts 1 to 13); concepts lumascript, the-game-description, permissions,
when-a-mod-fails, how-nebula-loads-mods, mod-files-and-layers, services, lumakit, game-content; get-started
about-modding-nebula; how-tos creating-a-mod, packing, reading-the-mod-log, reloading, setting-up-an-editor (part).
Every `lumalink` flag the docs write is checked against the command pages (flags_check.py, scratchpad, not kept).
Not read: how-tos converting-existing-mods, patching-game-tables, placing-objects, adding-models,
adding-animations, writing-material-shaders, adding-particle-effects, adding-sounds-and-music,
adding-screens-and-fonts, changing-nebulas-text, writing-code-in-c; tutorials' prose; quickstart; glossary
(but one entry).

## Traps written nowhere else

- `gate.py --quick` fetches the pinned LumaLink into build/gate-<tree>/lumalink-pin: a pin to an unpushed commit
  fails there, not locally. Push LumaLink first.
- Timing tests on a loaded machine: take each size's best of several runs, never one.
- Every `NotYet` diag needs a TSV row, or every_s0800_phrase fails.
- A nested `optional` must restore the outer `optional_next` (emit/expr.rs `optional_game`); a test guards it.
- make.py rewrites package.json: edit make.py, never package.json; write files back with CRLF.
- New text rows must not share a prefix with server rows (`LumaLink.Editor.*` is the server's; hence `.Vscode.`).
- Killing a gate: msys `ps` shows no args; find the PIDs by /proc/PID/cmdline, then the Windows children
  (docs_check, samples_check, cargo) by command line.
- "text" is in `game_unlisted`: a key no merge file has still builds, as one of the game's messages.

## D: folders

May delete (mine): D:/NebulaWork/build/a4-logs, D:/NebulaWork/build/a4-names, D:/NebulaWork/build/gate-Nebula-A4.
Keep until Agent 1 has taken what it needs: D:/NebulaWork/MarioGalaxyRecomp/Nebula-A4 and
D:/NebulaWork/lumalink-work/lumalink-a4 (both clean, everything pushed).
