# Service: input

## More rules

### Services-input

The `input` service: a mod's own actions, which the player binds in Controls. Header `native/include/luma_input.h`.
Inherits services-registry whole: calling rules (4), a mod's life (8), no cost with no mod (10). Reviewers: H (the
provider, the host call `binding_name` it asked for), J (Controls rows), G (luma-format's key option, the validator).

1. What it is. `input` 1.0, no permission (a mod reads only its own actions). Catalogued (`LumaServices.cpp`
   cCatalog), provided by every host (`runtime/src/luma_input.cpp`, registered in `luma_services_host.cpp` provide_all
   after fs). A host that samples no controls (the headless host) leaves every action up. The actions themselves live
   in `runtime/src/input/mod_actions.cpp` (`galaxy::input::mod_actions`), sampled by `live_controls_engine.cpp` with
   the game's own controls.
2. An action is a `key` option of luma.json: `{"key": "jump2", "kind": "key", "name": ..., "default": "a"}`. `key` is
   the action's id (the id rule: 1 to 64 of a-z 0-9 . _ -), `name` what Controls shows (the form's text, in the
   player's language), `default` the game control it follows: one of input_bindings.cpp's kTargetIds. At most 64 a mod
   and 1024 in all; past them, a second option of one key, or a bad id: left out, one log line `input: <mod>'s action
   <id> is left out (<why>)`. A default that is no game control: left out the same way (the validator refuses it
   first). Key options are not on the Mods page.
3. Declared once, on the game thread when the first mod with code begins (the provider's `mod_begin`): the actions of
   the mods this session started with that are on, in the Mods list's order. They never change in the session. With no
   code mod nothing is declared and nothing is sampled.
4. Calls (game thread; registry 4's codes 100 Thread, 101 Argument, 102 Handle, 103 StructSize, no rows of its own):
   - `action(mod, id, out)`: the mod's action `id` → `*out` (never 0, the same all session). No such key option:
     `LUMA_E_NOT_FOUND`, `*out` 0, not reported. Null id or out: `LUMA_E_ARGUMENT`, reported.
   - `read(mod, action, out)`: this frame's state. A handle that is not one of this mod's actions (0, another mod's,
     past the list): `LUMA_E_HANDLE`, reported. Null out, or `out->size` below 24: `LUMA_E_ARGUMENT`, reported.
     `out->size` is kept; the rest of 1.0's 24 bytes are written.
5. The state. The controls are sampled once a game frame; each sample sets the action's level from its sources. `down`
   is the newest sample's; `pressed` / `released` are 1 for one game frame when the action went down / up in any
   sample since the last frame any mod read (a press shorter than a frame is never lost; both can be 1; a mod that
   skips a frame misses that frame's edges, as a game reading its pad would). The first read after a new sample takes
   it for every action, so all reads in one frame agree. `frames`: samples it has been down (0 while up). `value`: 0
   to 1 (a trigger or stick bound to it, as far as pressed; 1 or 0 for a key or button); down at 0.5.
6. Follow and bind. Bindings are per scheme (keyboard, each pad kind), the game's source text (`bind.` lines). In a
   sample, the keyboard (when the game reads it) and each connected pad add their own bindings of the action; when no
   scheme in use has one, the action follows its game control as the game gets it this sample (the player's bindings,
   the extensions' and the harness's buttons, shake). `bound` is 1 when a scheme in use had a binding.
7. Host menus. While Settings or the HOME menu is open every action reads up, and nothing pressed there reaches a mod
   after it closes (the sampler's `host_menu`, both windowed hosts).
8. Kept in the settings. `runtime_settings.ini` lines `modbind.<scheme>.<mod id>/<action id>=<sources>`, read with the
   settings, written whole at each save; kept while the mod is off or gone. A binding to a source the action's default
   does not allow (`binding_source_allowed`) is refused by `bind`.
9. No cost with no mod: with no action declared the sampler does one atomic load a frame (`mod_actions::any`).
10. Not in layer 1 (next layers, PLANS-UNSTARTED 2.5): the Controls rows to list and bind actions (layer 1b, rows in
    14 languages), the host call `binding_name(mod, action)`, a harness step for a physical key, ui.focus and menu
    entries.
11. Tests (mods from f/mods-src/input): 2501 follow (A held 30 frames: one press, one release, 25+ frames; the start's
    refusals and answers), 2502 own handles (only its own of handles 1 to 8 reads), 2503 host menu (A held with
    Settings open gives no press; after Esc, one).
12. Controls rows (layer 1b). CONTROLS lists each declared action after the game's bindings, a row each in the
    declared order (none before the first code mod begins), on every scheme the page edits but the phone's and the Wii
    Remote modes. Label: the action's name as the mod gave it. Value: its own sources on the edited scheme, else
    `Mods.ActionFollows` ("Same as {control}", the control's label in the played language); help `Mods.ActionHelp`.
    Enter/A waits for an input as a game binding does (a source its game control could not take: "THAT INPUT CANNOT
    DRIVE THIS ROW"); Backspace/X clears it back to following; RESET clears every action on that scheme. The edits are
    a draft applied with the page (Space/Y) through `mod_actions::bind`, and saved with the settings (item 8). Key
    options are not on the Mods page; `mods.option` still reads one as its default control's id (services-mods 8).
    Harness fact `controls.<mod id>` {row, action, scheme, label, value, inputs} as shown, written when it changes.

### Layers 2 to 6

Header `luma_input.h`: `input` 1.1, `input.inject` 1.0, `input.dev` 1.0, one provider file (`runtime/src/luma_input.cpp`),
host feeds in `runtime/include/galaxy/luma_input_service.h`. Catalog rows: `{"input",1,1}`, `{"input.inject",1,0,
"input.inject"}`, `{"input.dev",1,0,"input.devices"}`; the permission rows (`Permission.input.inject`,
`Permission.input.devices`, 14 languages) were already in NebulaText.tsv. No mod: one atomic load at each game read.

D2-1. What the game read (layer 2, `game`, `game_raw`). The host calls `game_read(player, sample, device, motor)`
   at each of the game's reads (`svc_input_read`: the controls path, the real Wii Remote, the co-star, headless).
   `LumaInputGame` (28 bytes) is the read after bindings and any injection; `game_raw` the player's own controller
   (the same unless a mod injects). Player 1 (the co-star) answers LUMA_E_ABSENT until the game has read it.
D2-2. Device kind (layer 2, `device`, `watch_device`). NONE, KEYBOARD_MOUSE, PAD, TOUCH, MOTION from the host's
   main device (Win32: the chosen device; SDL: pad or touch; the real Wii Remote: MOTION; headless: NONE). The watch
   event (kind 1) comes at a frame's start with the latest kind; one watch a mod.
D2-3. Rumble (layer 2). A mod's ask (0-1, at most 10 s of game time) merges with the game's motor as the
   stronger, scaled by the player's Vibration setting: Win32 controls path and SDL pads, the real Wii Remote's motor.
   LUMA_E_ABSENT on a keyboard, a phone's screen, with rumble output off, or in headless.
D2-4. Injection (layer 3, `input.inject`, permission). One injecting mod a player (else LUMA_E_BUSY). Each read
   takes what the mod set since the last read, or a controller at rest; NaN/out-of-range stick and pointer values
   are held (0, or the bound) and noted with 101, the call still OK. Taking the mod out ends its injections.
D2-5. Text entry (layer 4, `text_request`, `text_peek`, `text_cancel`). One request open in the game; a new one
   ends the open one CANCELLED. While open the game reads a controller at rest and every action reads up
   (`live_controls_engine` treats it as a host menu). Kinds ANY, NUMBER (digits, one leading minus, one point), NAME
   (no control characters). Hosts: Win32 window (WM_CHAR with surrogate pairs, so the system input method's
   results arrive as text; Enter, Escape, Backspace, Delete, Left, Right, Home, End as keys; a lost focus
   cancels); SDL (Linux, Android, macOS): SDL text input started exactly while a request is open (the phone's
   system keyboard, the desktop input method), SDL_EVENT_TEXT_INPUT as UTF-8, the same keys plus Android Back as
   Escape, focus lost cancels. Headless: LUMA_E_ABSENT. iOS is not built.
D2-6. Raw devices (layer 5, `input.dev`, permission `input.devices`). Windows: HID devices by SetupDi, the
   system's keyboards and mice left out; `open` only a path `list` gives (else NOT_FOUND), a reader thread a device,
   64 reports kept between frames (the oldest dropped and counted), at most 8 open a mod, LUMA_E_CLOSED once when
   unplugged; a mod taken out closes its own. Other platforms: LUMA_E_ABSENT (Linux hidraw and Android USB host are
   not built: missing general parts, PLANS-UNSTARTED).
D2-7. Harness step `text` (layer 4's proof): `{"text": "..."}` types through the host's text entry
   (`NebulaHostForExtension.type_text`, the same `text_utf8` SDL feeds); while a request is open, `key` enter, esc,
   backspace, left, right edit it.
D2-8. Tests (unrun; mods in.c CASE 3, 4, 6, 7): 2505 game (read, device, rumble absent
   on the keyboard, refusals), 2506 inject (120 frames of stick up move Mario, a dropped button, NaN noted, raw stays
   the player's, the end), 2507 inject declined, 2508 inject undeclared (L506), 2509 inject taken out, 2510 text (cut
   initial, peek, second request cancels the first, A held at rest, cancel, typed NUMBER "-12.59"), 2511 dev (list,
   refusals, unknown path; nothing opened), 2512 dev declined.
D2-9. Open: a device change cannot be made in a test (2505 asserts no event); rumble on a pad is not proven by a
   run (no motor to read back). Both need a missing general part: a harness step that sets the host's device kind.

### Pointer source (D; kit K4)

`input` 1.2 (catalog row `{"input",1,2}`). New `LumaInputPad` (uint32: 0 the main player, 1 the co-star) and
`LumaInputPointerSource` (PLAYER 0, INFRARED 1, GYRO 2, MOUSE 3, STICK 4).

D3-1. pointer_source(mod, pad, kind, sensitivity), game thread. From the next sample only `kind` aims the main
   player's pointer (the live controls' other sources are not read for it); the player's invert, speeds and
   recenter still apply. GYRO turns `sensitivity` (above 0, at most 8) times the setting. PLAYER gives the choice
   back; the mod's stop gives it back too. INFRARED is OK when the player's device is a Wii Remote (it aims itself;
   the call clears any choice). LUMA_E_ABSENT: no such device now (the live controls publish what could aim at each
   sample: mouse when absolute pointing and the keyboard are on, gyro when a pad's gyro points, stick when a pad is
   connected), or any kind but PLAYER for pad 1. LUMA_E_ARGUMENT, reported: pad past 1, an unknown kind, a GYRO
   sensitivity out of range.
D3-2. Kit: `input.Pad`, `input.PointerSource`, `input.pointer_source(pad, kind, sensitivity) -> Status`.
