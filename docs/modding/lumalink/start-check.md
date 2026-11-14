# What is checked at each start

## More rules

### 1. The order of a start with mods

On the thread that starts mods today (`start_plugins` and the mod scan in mods.cpp), before the first scene:

1. The game's own parts (section 2). Any missing: every mod is off this session (section 2's message); the game starts
   without mods. Nothing below runs.
1a. Consent. Mods whose consent does not hold (consent 2.1, start case) are off this session, named (source LOADER,
`Consent.Waiting`; `Consent.OffDeclined` a Note when the stored answer is Declined, consent 1.4), before the start
check and the cook; their content is not cooked. `ModInfo::required_permissions` is filled before this step, at the
scan, from the mod's kept report read by `luma_report_read_kept` (no rule runs): the names
`luma_report_required_service(report, luma::this_platform(), i)` gives (i from 0 to the first null) that equal a
permission the manifest declares, in that order; a mod with no kept report that reads has none (consent 1.1: only
`native` is required). A kept report that does not read: the scan writes one line to the developer's log with the
mod's file name and `luma_report_invalid`'s text, and step 2 checks every rule for it as today.
2. The start check of the validator for each mod on: findings to the mod log (source VALIDATOR). A mod with an Error
   finding of the start rules (region, newer format, a need missing, a conflict, a renderer feature, section 3) is off
   this session, named.
3. The cook of the mods' content (the device cooker, `nebula-import`), as today. Its facts file is read whole.
4. Any cook failure of a mod's file (a `failed` fact, L804; the shader compiler's error for a mod's program; a file
   the cook left out): that mod is off this session, with each failure named (mod log, source COOK, Error), and the
   content is cooked again without it, so nothing of a half-cooked mod is used.
5. References: placeholders decided from the start check's findings.
6. Code of the mods still on, through the consent store and `luma_load_mod`. Only a refusal at bind turns a mod off
   here (consent was settled at 1a), named (source LOADER). A mod turned off here after its content was cooked:
   - every store entry that mod alone made is dropped from the store's search at once (the overlay's `<store>.mods`:
     each entry and the mods that made it);
   - so nothing of the mod is used this session either way, and its content is not in the next start's cook.
7. `luma_refs_check_after_start` (the host's call).
8. The title: the mod log's summary notice.

A mod is either fully on (content cooked, code bound, after-start names checked) or off with its reasons on the Mods
page and in the mod log. There is no third state.

### 2. The game's own parts

| Part | Platforms | Checked how | Missing means |
|---|---|---|---|
| The importer library (`nebula_import.dll`, `libnebula_import.so`, linked statically on iOS) with the validator, and its version equal to the game's | all | loaded and its version export compared | no mod can be checked or cooked |
| The shader compiler the platform's cook uses (DXC on Windows and Linux for D3D12 and Vulkan, its Android build, the Metal route of 3.4's decision) | as 3.4 decides | present, runs `--version` once per install, cached by file size and time | a mod's programs cannot be built |
| The placeholder data (`nebula:placeholder/*`) | all | the store holds each | a missing reference would have nothing to stand in |
| Writable space for the mods' cook | all | the cook folder writable, free space at least the cook's estimate + 64 MiB | the cook would fail half way |

- Checked only when at least one mod is on. No mod on: nothing is checked, nothing loaded (no cost).
- One mod-log entry for the session (mod `nebula`, source COOK, Error) and the title's notice:
  `Start.PartMissing` "{part} is missing or damaged, so mods are off this time." The Mods page shows the same line at
  its top. The installer (`installer/`) installs every part on every platform; its own check covers them, and a
  harness run without one is `setup-failed` (rootcause 0.7 5).
- As written: `galaxy::startcheck::missing_part(store,
  cook_folder, cook_bytes)` (mod_start_check.h) checks the four in the table's order and returns the first missing
  as `{row, needed, why}` (`why` in English, for `nebula.log`); `report(part)` adds the entry, worded by
  `Start.PartMissing` with `part` = `R:<row>`. The compiler's check is
  `galaxy::render::mod_shader_compiler_missing(cache_folder)`: D3D12 looks for `dxc\dxc.exe`, `dxcompiler.dll`,
  `dxil.dll` and runs `dxc.exe --version` once a stamp of their sizes and times (`compiler.checked` in the cook
  folder); Vulkan's front end is linked in (""); Metal compiles no mod programs yet, each refused (""). F calls it at
  step 1 with a mod on, logs `why`, and on a missing part turns every mod off for the start.

### 3. What a mod states it needs from the device

- Renderer features (a key `renderer`; rule L806):
  `"renderer": {"features": ["ray_tracing"]}` with names from the renderer's capability list (the `draw.native`
  capability query of 2.4, H; the list is in the self-description's `renderer.features`). The validator's start
  check refuses a mod needing a feature this device lacks: L806,
  `{mod} needs {feature}, which this device's graphics do not have.` The mod is off, named. When the
  self-description has no `renderer.features` list, L806 is not-checked. A mod that can do without
  a feature does not list it and asks at run time.
- Programs. A mod's hand-written shaders (3.4) and material programs are built by the cook for this device's
  renderer; one that fails to build is a cook failure (1.4). Building for the other renderer of the same platform
  (Direct3D 12 and Vulkan on Windows) happens at that renderer's first start with the mod, through the same check.
- Effects that cannot be seen: an Error at load from the cooked record,
  not a turning-off: the mod stays on with that effect drawn as the effect placeholder.

### 4. Text (14 languages)

| Id | English | Args |
|---|---|---|
| `Start.PartMissing` | `{part} is missing or damaged, so mods are off this time.` | part |
| `Start.Part.Importer` | `The mod importer` | |
| `Start.Part.Compiler` | `The shader compiler` | |
| `Start.Part.Placeholders` | `Nebula's placeholder files` | |
| `Start.Part.Space` | `Space for mod files ({needed} free needed)` | needed |
| `Start.ModOffCook` | `Off: its files could not be prepared (see Mods > Log)` | |

### 5. Where the game does it

- `start_plugins` and the mod scan follow section 1's order; the installer checks section 2's parts; the harness
  reports `setup-failed` when a part is missing.
- `draw.native`'s capability names are the list of 3.

### 6. Tests

| Test | Setup | Must show |
|---|---|---|
| 3141 | a mod with a program, the compiler moved away | the start: one `Start.PartMissing` (compiler) entry and notice; every mod off; the game reaches the title |
| 3142 | a mod with a file the cooker fails on (a glTF whose buffer is cut) | that mod off with L804 naming the file and the cooker's words; a second mod on and its content present; the overlay holds nothing of the first |
| 3143 | the same twice | the second start cooks once (the cook's facts show a cache hit) |
| 3144 | a mod needing `ray_tracing` on a device without it (the harness's renderer option) | off with L806's text, English and Japanese |
| 3145 | no mod | none of section 2's checks runs (no `startcheck` fact) |
| 3146 | a mod whose code start refuses after its content cooked | content not used this session (an object it adds is absent, its placement's facts show no actor); at the next start its content is cooked out |
| 3147 | as 3146, the mod also patching a message file another mod patches | the cook runs again without it before the first scene (the cook facts show two cooks this start, one the next); the other mod's message lines present, the refused mod's absent |
