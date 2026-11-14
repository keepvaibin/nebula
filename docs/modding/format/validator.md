# The validator

## Decisions after the first text

### 1. Where it lives and who links it

- A Rust library crate in Nebula: `crates/luma-validate`. No unsafe code, no panics on any input, no network, no
  writing to disk.
- LumaLink links it as a Rust dependency, at the same Nebula commit as the cooker (DECISIONS 2).
- The game reaches it through the cooker library it already carries (`crates/nebula-import`, built as `cdylib` with a
  C interface `c_api.rs` and `jni.rs` for Android), which links `luma-validate` and exports the C interface of section
  3. One build of the library per platform; the game's import screen and loader call it. On iOS the cooker library
  (with the validator in it) is linked into the game statically, no `cdylib`, with the same C interface.
- A build check fails if the game and LumaLink of one release were built from different `luma-validate` sources.

### 2. What it is given

| Input | From LumaLink | From the game |
|---|---|---|
| The mod | a project folder or a `.luma` | the `.luma` being imported, or installed |
| The game's description | the self-description file of the game it builds against, and the player's store index (paths, table schemas, languages) | its own self-description (compiled in) and its store |
| The other mods | the project's `needs` as found in LumaLink's mod folder | every installed mod's manifest |
| The platform | every platform the project builds for | its own |
| Options | the defaults | the player's values |

Checks that need an input that is absent (LumaLink with no imported game) are reported as **not checked**, each
named, never silently skipped; `pack` refuses until they are checked.

**When the game runs it.** Two calls of the same library (rule 7), split by what each check depends on:

| When | What | Kept |
|---|---|---|
| Import (6.1) | **Every** rule, in full, over the whole `.luma` | The report, with the `.luma`'s hash, stored beside the installed mod |
| Each start, and each change of the mod list or options | The `.luma`'s hash against the stored one (a different hash: a full check, as at import), then only the rules that depend on the game, the platform, the options and the other mods: `L2xx` that read the player's region and languages (L208, L214), `L301` with the player's options, `L5xx` names against this game's self-description (L505, L506, L507, L508), `L7xx` module versions, `L8xx` | The new findings replace the stored ones of those codes |

So a 1 GiB `.luma` is read in full once, at import; a start reads its manifest, its import list and the stored report.

### 3. Interface

Rust (LumaLink, cooker):

```rust
pub fn validate(input: &Input) -> Report;          // never panics; every finding, sorted
pub struct Report { pub findings: Vec<Finding>, pub validator: Version, pub format: (u32, u32) }
pub struct Finding {
    pub code: Code,                 // e.g. L203
    pub level: Level,               // Error (the mod is refused or off) | Note (shown, not refused)
    pub mod_id: Option<String>,     // None only when luma.json cannot be read; then `file` names the .luma
    pub file: String,               // the entry inside the mod ("luma.json", "layers/base/patches/...")
    pub line: Option<u32>, pub column: Option<u32>,   // for text files
    pub pointer: Option<String>,    // JSON pointer, "/options/2/default"
    pub args: Vec<(String, String)>,// the message's parameters, by name
}
pub fn message(f: &Finding, language: &str) -> String;   // the text in one of the game's languages
```

C (the game), through the cooker library:

```c
typedef struct LumaFinding { uint32_t code; uint32_t level; const char* mod_id; const char* file;
                             uint32_t line; uint32_t column; const char* pointer; const char* text; } LumaFinding;
/* Validates the mod at `path` against the running game. Returns a report handle; never null. */
LumaReport* luma_validate(const LumaValidateInput* input);
uint32_t luma_report_count(const LumaReport*);
const LumaFinding* luma_report_at(const LumaReport*, uint32_t index, const char* language);
void luma_report_free(LumaReport*);
```

The two produce the same findings for the same mod and game, field for field (tested, section 7).

### 4. Messages

- Every code has one message template, short and plain, with named parameters: `{mod}`, `{file}`, `{line}`, `{found}`,
  `{expected}`, and per-rule ones. The location (`{mod}`, `{file}`, `{line}`) is always shown with the message: a
  message never needs the reader to guess where.
- Every template exists in all 14 of the game's languages (protocol 9), in `localization/NebulaText.tsv` under ids
  `Validator.L<code>`, compiled into the library. A template missing in any language fails the build.
- Translations are added with the rule, reviewed by the other agent like any text players read.

### 5. Rules

Codes: `L1xx` container, `L2xx` manifest, `L3xx` layers and paths, `L4xx` patches, `L5xx` code, `L6xx` content kinds
(one block per kind, filled by each kind's contract), `L7xx` module data (`kit/`), `L8xx` the game and other mods.
A code is never reused for another meaning.

| Code | Level | Rule (luma-format section) | Message (English template) |
|---|---|---|---|
| L100 | Error | Not a readable zip (2) | `{file} is not a .luma: it cannot be opened as a zip file ({found}).` |
| L101 | Error | Compression other than stored/deflate; encrypted; multi-disk (2) | `{file}: {entry} uses {found}; a .luma may only be stored or deflated.` |
| L102 | Error | A bad entry name (2) | `{file}: the entry "{entry}" has a name that is not allowed: {why}.` |
| L103 | Error | Two entries differ only in case, or one entry twice (2) | `{file}: "{entry}" and "{other}" are the same name on some systems.` |
| L104 | Error | A link or device entry (2) | `{file}: "{entry}" is a link; a .luma holds only files and folders.` |
| L105 | Error | A limit passed (2) | `{file}: {what} is {found}; the most allowed is {expected}.` |
| L106 | Error | Unpacked size differs from the stated (2) | `{file}: "{entry}" unpacks to {found} bytes, not the {expected} it states.` |
| L107 | Error | An unknown top-level entry (3) | `{mod}: "{entry}" is not part of a .luma (expected luma.json, layers/, code/, media/, saves/, kit/ or source/).` |
| L108 | Note | The archive writes `\` between folders throughout (no name has `/`): read as `/`, every name rule on the names as read; a mix stays L102 | `{file} was made with a tool that writes backslashes between folders; they are read as /. lumalink pack writes the standard form` |
| L200 | Error | `luma.json` missing (3) | `{file} has no luma.json.` |
| L201 | Error | Not UTF-8 JSON, a comment, a duplicate key, over 1 MiB (4) | `{mod}: luma.json line {line}: {found}.` |
| L202 | Error | An unknown key (4) | `{mod}: luma.json line {line}: "{key}" is not a key of luma.json.` |
| L203 | Error | A required key missing (4) | `{mod}: luma.json has no "{key}".` |
| L204 | Error | A value of the wrong type or out of its range (4) | `{mod}: luma.json line {line}: "{key}" is {found}; it must be {expected}.` |
| L205 | Error | `format` newer than this game or validator (1, 8) | `{mod} was made for a newer Nebula (format {found}); this one reads up to {expected}.` |
| L206 | Error | `id` malformed (4) | `{mod}: the id "{found}" may only use lowercase letters, digits, - and ., start with a letter, and be 3 to 64 long.` |
| L207 | Error | `game.id` not RMG (4) | `{mod} is for {found}, not Super Mario Galaxy.` (SMG2's `SB4` is named "Super Mario Galaxy 2") |
| L208 | Note | The player's region not in `game.regions` (4) | `{mod} is made for {expected}; your game is {found}, so it is off.` |
| L209 | Error | A `media` file missing, not PNG/JPEG, or outside its size (4) | `{mod}: {key} names "{entry}", which {why}.` |
| L210 | Error | An option malformed: kind, default outside range, duplicate key (4) | `{mod}: option "{key}": {why}.` |
| L211 | Error | A permission name unknown, or `net` without `hosts`, or no `why` (4) | `{mod}: permission "{key}": {why}.` |
| L212 | Error | `load` makes a cycle (4) | `{mod}: the load order cannot be met: {found}.` |
| L213 | Error | `takes_over` names this mod's own id or an installed mod's id that is on (4) | `{mod}: takes_over "{found}": {why}.` |
| L214 | Error | A `text` language that is not one of the game's (or of an installed language mod) (4) | `{mod}: luma.json line {line}: "{found}" is not a language of the game.` |
| L215 | Error | A file under `media/` that neither `icon` nor `pictures` names (3, 4) | `{mod}: "{entry}" is in media/ but luma.json does not name it.` |
| L216 | Error | A `saves` entry malformed, its file missing, not a save of its region, or a region not in `game.regions` (4, 6.1) | `{mod}: save "{entry}": {why}.` |
| L217 | Error | A file under `saves/` that `saves` does not name (3, 6.1) | `{mod}: "{entry}" is in saves/ but luma.json does not name it.` |
| L300 | Error | A layer listed without its folder, or a folder not listed (4, 5) | `{mod}: layer "{layer}" {why}.` |
| L301 | Error | A `when` that can never hold (an option value not in its values, an unknown region/language/platform) (4) | `{mod}: layer "{layer}": {why}.` |
| L307 | Error | A layer with `when.code` on a platform the mod has no code for (4, F-2) | `{mod}: layer "{layer}" is chosen by the mod's code, which does not run on {platform}.` |
| L302 | Error | A game path that matches nothing, for a kind the game does not find by name (5) | `{mod}: {file}: the game has no "{found}", and this kind of file cannot be new.` |
| L303 | Error | A whole file at a path whose kind has rows (5) | `{mod}: {file} replaces "{found}", which has rows: change it with a patch by key instead.` |
| L304 | Error | A source file in `files/` of a kind the cook does not make at a game path, which no model of the layer names (5; item 15) | `{mod}: {file}: a {kind} cannot stand in for "{found}" ({expected}).` |
| L305 | Note | Two mods replace the same file; the later is used (5) | `{mod} and {other} both replace "{found}"; {winner}'s is used (it loads later).` |
| L306 | Error | An entry under `layers/<layer>/` outside `files/`, `patches/`, `merge/`, `assets/`, or not a `.png`/`.dds` in `textures/` (5) | `{mod}: "{entry}" is not in files/, patches/, merge/ or assets/, nor a .png or .dds in textures/.` |
| L309 | Error | A file in `merge/` of a kind the import cannot add to (5) | `{mod}: "{entry}" is in merge/, which adds only to tables (.json), messages (.json), particles (.jpc, .jpc.json) and ray tracing rows (NebulaData/Rt*.tsv).` |
| L400 | Error | A patch file that is not valid JSON or has unknown keys (5.1) | `{mod}: {file} line {line}: {found}.` |
| L401 | Error | `key` is not the key this kind of file uses (5.1) | `{mod}: {file}: rows of "{target}" are keyed by {expected}, not {found}.` |
| L402 | Error | A `where` or `remove` that finds no row; an `add` whose key exists (5.1) | `{mod}: {file} line {line}: {why}.` |
| L403 | Error | A value that does not fit its column's type (5.1) | `{mod}: {file} line {line}: column "{column}" is {expected}; {found} does not fit.` |
| L404 | Note | Two mods set the same cell, or one adds a row another removes (5.1) | `{mod} and {other} both change {target} row {row} column {column}; {winner}'s value is used.` |
| L405 | Error | A patch on another mod's file without needing that mod (5.1, F-3) | `{mod}: {file} changes "{target}" from {other}; add {other} to needs first.` |
| L406 | Error | A `select` naming a column the file's kind does not have, or a match kind other than equals/prefix/glob (5.1, F-5) | `{mod}: {file} line {line}: {why}.` |
| L407 | Note | A `select` that matched no row, at load (5.1, F-5) | `{mod}: {file} line {line}: the selector matched no row of "{target}".` |
| L500 | Error | `code/` without `code` in luma.json, or the reverse (4, 6) | `{mod}: {why}.` |
| L501 | Error | An unknown platform folder, or a listed platform without its library (6) | `{mod}: code for "{found}": {why}.` |
| L502 | Error | A library that is not a library of its platform (wrong format or machine) (6) | `{mod}: {file} is not a {expected} library ({found}).` |
| L503 | Error | No `luma_mod` export (6) | `{mod}: {file} does not export luma_mod.` |
| L504 | Error | An import from the system the platform's loader contract does not allow (6) | `{mod}: {file} needs "{found}" from {library}, which mods may not use.` |
| L505 | Error | A name the game does not have (6) | `{mod}: the game has no {kind} "{found}"{signature}.` |
| L506 | Error | A permissioned service used without its permission (4, 6) | `{mod} uses {service} without asking for the "{permission}" permission.` |
| L507 | Note | No code for the player's platform | `{mod} has no code for {platform}, so it is off here.` |
| L508 | Error | A `luma_api` newer than the game's | `{mod} needs LumaCore {found}; this game has {expected}.` |
| L509 | Error | An extra library (`code.libraries`) without the permission `native`, or a library file not named there (6, F-1) | `{mod}: {file}: {why}.` |
| L510 | Note | Each system import of each extra library, listed (6, F-1) | `{mod}: {file} uses {found} from {library}.` |
| L511 | Error | A library of an iOS mod (its own or an extra one) that iOS will not load once the device signs it: built for another platform, or encrypted (6, F-1; item 17) | `{mod}: {file} cannot run on iOS: {why}.` |
| L700 | Error | `kit/<module>` without that module in `kit`, or the reverse; a `kit` key naming no library the mod needs; a kit file the library cannot read; a library's schema file in the wrong place, not JSON, or outside the subset (item 16) | `{mod}: {why}.` |
| L701 | Error | Module data failing the schema its library gives for the version named (item 16) | `{mod}: {file} line {line}: {module}: {text}.` |
| L702 | Error | A `kit` version the installed library gives no schema for (item 16) | `{mod} needs {module} data version {found}; the installed {library} gives: {expected}. Update {library}, or install a version of {mod} made for it.` |
| L800 | Error | A need missing, off, or of a version outside its range | `{mod} needs {other} {expected}; {found}.` |
| L801 | Error | A conflict on | `{mod} cannot be on with {other}: {why}.` |
| L802 | Error | Two installed mods with one id | `Two mods have the id "{found}": {file} and {other}.` |
| L803 | Error | `nebula` range excludes this release | `{mod} needs Nebula {expected}; this is {found}.` |
| L804 | Error | A file of `files/` or `assets/` the cook refuses, with the cook's own reason; a Note for what the cook says of a file it cooks (item 18) | `{mod}: {file}: {text}.` |

`L6xx` is reserved: `L600-L619` models and skeletons (3.2), `L620-L639` materials (3.3), `L640-L649` shaders (3.4),
`L650-L669` effects (3.5), `L670-L679` animation (3.6), `L680-L689` sound (3.7), `L690-L699` screens, fonts and text
(3.8). Stages (3.9) and movies (3.10) take `L900-L949`. Each block is filled by its kind's contract, signed with it.

### 6. Versioning

- The validator's version is `<format major>.<rules minor>`. A minor adds rules (codes) for additions of the format or
  new kinds; it never makes a mod that passed an earlier minor's rules fail, unless the mod uses something that minor
  added.
- Message text may be corrected in any version; the code and the parameters never change.

### 7. How it is tested

1. One broken mod per code, in `crates/luma-validate/tests/broken/L<code>/`: each produces exactly that code (and no
   other), at the stated file, line and pointer. A code with no broken mod fails the suite.
2. The same in the game: each broken mod imported through the harness (`install: "import"`) gives the same findings as
   LumaLink `check` on it, text compared in English and in one other language per run.
3. Every template in 14 languages: build-time check.
4. Fuzzing: the container, the manifest and every patch: no panic, no hang beyond a stated time, every rejection a
   finding.
5. Determinism: two runs on the same input give identical reports, byte for byte.
6. Speed: a 1 GiB `.luma` of 2,000 files is validated in a stated time on this machine, without unpacking to disk.

### Section 3 (interface)

1. A finding's parameters are a literal or a phrase that is itself translated, so no English reaches a player inside a
   translated template: `Finding.args: Vec<(&'static str, Arg)>`, `Arg::Text(String) | Arg::Phrase(&'static str)`.
   `Finding.level` is `Level::Error | Level::Note`; `Finding.code` is `u16` (203 is L203).
2. `Report` is `{ findings, not_checked: Vec<NotChecked>, touches: Touches, validator, format, about: Option<About>,
   fingerprint: Option<String>, full: bool }`. `NotChecked { code, because: phrase id, places }` names each rule an
   absent input kept from being checked (section 2's rule, now a field). `About { id, version, conflicts }`.
3. `Input { source: Source::File | Bytes | Folder, game: Option<&Game>, installed: &[Installed], platforms, options:
   Option<&BTreeMap>, position: Option<usize> }`; `Game { nebula, region, extra_languages, names: Option<&Names>,
   store: Option<&dyn Store> }`; `Store { path(lower) -> Option<String>, table(path) -> Option<Table>,
   may_be_new(path) -> bool }`; `Table { key, columns: (name, Int{min,max} | Float | Text), rows: Option<HashSet>,
   case_insensitive }`. `position` is how many of `installed` (load order) load before this mod; an update leaves the
   mod it replaces out of `installed`.
4. `Installed { id, version, on, file, conflicts, touches }` and `Touches { replaces, cells, adds, removes, assets,
   functions }` (files replaced, cells set, rows added and removed, the mod's assets, the functions its code
   replaces), so L305, L404 and L805 are found without reading the other mods again. The game keeps each mod's
   touches from its import, read against the store and the platforms; `lumalink check` reads each mod of its mod
   folder against the same game and platforms as the checked mod, so a clash only the store shows (two starts in
   layers one scenario turns on together) is found by both.
5. `pub fn recheck(input, saved: Option<&Saved>, game: &str) -> Report`, and `Saved` (section 2 below).
6. C: Beyond them: `LumaValidateInput` (size-checked; path, platform, release, region, extra languages, the names as
   `.rows` text with LUMA_API until 1.8's file, the services' permissions, the store as three functions plus a
   `store_key`, the other mods by their kept reports in load order with `position`, the options, this mod's kept
   report); `luma_report_state` (0 full, 1 a start's check, -1 the input was not valid) with `luma_report_invalid`
   (why, English, for the developer's log); `luma_report_errors`; `luma_report_not_checked_count/at`;
   `luma_report_kept`; `luma_report_id/version`. `luma_report_at` returns a finding valid until the next call on that
   report. A faulty input (a null, a wrong size, an unknown platform, names that do not read, a store missing a
   function, an installed mod whose kept report does not read) is refused with its reason, never a crash and never a
   mod skipped.

### Section 2 (when the game runs it)

7. The kept report. After every check the game keeps `luma_report_kept`'s bytes beside the mod. Form: UTF-8 lines
   (`luma-saved 1`, validator version, the `.luma`'s fingerprint, the game key, about, conflicts, touches, every
   finding with its parameters, every not-checked), the last line `end` with the SHA-1 of every byte before it: cut or
   changed in any byte, it does not read, and then every rule runs.
8. A start's check uses the kept report only when it is for the same `.luma`, by this validator version, against the
   same game key. Then the rules that read the whole `.luma` are not run and their findings are taken from it: L106,
   and every finding the patches block makes. Every other rule runs again, including L305, L404 and L805 from the kept
   touches. The game's own reading of a `.luma` checks each entry's CRC as it unpacks, which is what makes the
   fingerprint enough.

### Section 5 (rules)

9. L805 (Error, new): a function this mod's code replaces (LUMA_REPLACE) that a mod loading before it, and on,
   replaces already. Which of the two loads first is 1.1's one order rule; L805 follows it.
10. L512 (Error) is in the table: `luma_mod` is not a readable LumaModInfo, or an import row is malformed, or a hook
    other than before/after is on a constructor or destructor. With L503, L505, L508 and L805 it covers every refusal
    of the loader at bind time; the loader uses these codes and templates.
11. L102 also covers Windows' forbidden characters `< > : " | ? *`, combining marks and conjoining jamo (a decomposed
    name is the same file as its precomposed twin on macOS), and a layer, or a path under `files/` or `assets/`,
    starting with `@` (it names another mod's files). L104 covers devices as well as links. L106 covers a size, a
    checksum or data that is damaged, and two entries sharing data.
12. L403 covers a column the table does not have as well as a value that does not fit (`Patch.NoSuchColumn`,
    `Patch.ValueType`). Keys of a table marked `case_insensitive` match whatever their case (the ray tracing tables).
13. L216: a save is checked by a port of the game's own `NebulaSaveFormat::validate`; one corpus of save files (good,
    damaged in each way the check names, each region) is run through the game's import and the validator in one test
    with identical answers.
14. Not checked, each named, until the input exists: L506 (`Missing.ServiceImports`, until `LUMA_IMPORT_SERVICE = 5`
    in 1.1). L304, L504 on Apple, L505 for another mod's function, L511, L701 and L702 are checked (items 15 to 17, 20
    and 21). The rows `Missing.KindContract`, `Missing.KitSchemas`, `Missing.LoaderContract` and `Missing.ModExports`
    are deleted. The bounds rule (0.7's class of fix) joins the models block L600-L619 with 3.2: a model's bounds in
    game units against the object it stands for, off by a factor of 10 or more, is an error; it needs per-kind sizes
    from 1.9.
15. L304, a source in `files/` that the cook does not make at a game path. The cook makes these at a game path:
    `.glb`, `.gltf`, `.png`, `.wav`, `.ogg`, `.bcsv.json`, `.bcam.json`, `.layout.json`, `.anim.json`, `.model.json`,
    and a stage's placement `.arc/jmp/.json`. `.sound.json`, `.font.json` and `.movie.json` are cooked too, but the
    cook refuses them in `files/` by name (L804). For any other source ending:
   - A file that a `.gltf` or `.glb` in the same layer's `files/` names by a `buffers` or `images` `uri` gets no
     finding. The uri is read by `luma_validate::uri::model_uri`, the one reading the cook also uses: percent escapes
     decoded, `/` between parts, `.` and empty parts dropped; a `..` part, a leading `/`, a `:` or a `\` leaves the
     model's folder and names no file. The model's cook reads that file, and any fault in it is L804 on the model.
   - Anything else is L304. `kind` is the file's ending, and `expected` says where that kind belongs:
     - `.bin`, `.jpg`, `.jpeg`, `.ktx2`: `Where.ModelPart` (a model's buffer or image, named by the model);
     - `.dds`: `Where.Texture` (a texture pack's `textures/`);
     - `.mid`: `Where.Music` (`assets/`, read by a `.sound.json`);
     - `.json`: `Where.Table` (`patches/`, or the table's own form such as `.bcsv.json`);
     - `.ttf`, `.otf`: `Where.Font` (`assets/`, read by a `.font.json`);
     - `.webm`, `.mp4`: `Where.Movie` (`assets/`, read by a `.movie.json`).

   A test holds that every source ending is either cooked or listed here.
16. **L700-L702, a library's data** (`kit`):
   - A library mod gives one JSON schema per module and data version, at `code/<id>.kit/<module>/<version>.json`.
     `<module>` is a kit name (1 to 64 of `a-z 0-9 _ - .`, not starting with a dot, no `..`). `<version>` is a whole
     number from 1 to 1000000 with no leading zero. Schemas are no code: a library that gives only them needs no
     `code` key in luma.json (L500 counts no file under its own `code/<id>.kit/`).
   - The library's own check reads each schema and keeps it in its report (the `kit_schemas` touch of the kept report,
     form 3 and later). A schema file in the wrong place, not JSON, or using a keyword outside the subset is L700 on the library.
   - A mod that uses the library declares `"kit": {"<library id>.<module>": <version>}` and needs the library. Its data
     is `kit/<module>/<name>.json`, where `<name>` is a kit name.
     - A key naming no library the mod needs (nor the mod itself) is L700 `Kit.NotNeeded`.
     - Any other file under `kit/` is L700 `Kit.NotData`.
     - A version the library gives no schema for is L702, naming the versions it does give (or `Kit.NoModuleData`).
     - Each data file is checked against the schema of that version. Each fault is L701 at its line and JSON
       pointer. A file over 4 MiB, or one that is not JSON, is also L701.
   - A library that is not installed is L800's, and its users' data is not judged without its schema.
   - The game reads the library's schemas from its kept report. `lumalink check` reads them from the library's own
     check in the mod folder. Both use the same code, so they say the same.
   - **The schema subset** (`luma_validate::schema`). Every other keyword is refused by name (L700
     `Kit.SchemaKeyword`):
     - not checked, notes only: `$schema`, `$id`, `$comment`, `title`, `description`, `default`;
     - `type` (one name or a list);
     - objects: `properties`, `required`, `additionalProperties` (boolean or schema), `propertyNames`,
       `minProperties`, `maxProperties`;
     - arrays: `items`, `minItems`, `maxItems`, `uniqueItems`;
     - strings: `minLength`, `maxLength`, `pattern`;
     - numbers: `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`;
     - any value: `enum`, `const`, `oneOf`, `anyOf`;
     - references: `$ref` to `#/$defs/<name>`, with `$defs` at the top only. A loop of references that never steps
       into the value is refused.
     - A `pattern` is a regular expression without groups, `|` or lazy quantifiers: anchors, literals, `.`, classes,
       `\d \w \s` (and their capitals), `\n \t`, escaped punctuation, and quantifiers up to 1000. It is unanchored
       unless `^` or `$` is given.
17. **L511 on iOS**, for every library of an iOS mod. Its own `mod.dylib` and every extra library must say they are
   built for iOS devices: `LC_BUILD_VERSION` platform 2, or `LC_VERSION_MIN_IPHONEOS`. Another platform is
   `Lib.NotIos`, naming that platform (macOS, the iOS Simulator and so on). No platform at all is `Lib.NoIos`. A library
   with a non-zero `cryptid` (`LC_ENCRYPTION_INFO`, `LC_ENCRYPTION_INFO_64`) is `Lib.Encrypted`: the device cannot sign
   it again.
18. **L804: every file goes through the cook the same way.** At the game's start and in `lumalink check`, each file of
   a layer's `files/` and `assets/` runs through one function, nebula-import's `cook_one`:
   - It skips an asset that another file reads: a sound, font or movie in `assets/`, or a screen pass's HLSL.
   - It runs the modern source cooks (models, pictures, tables, layouts, sounds, fonts, movies).
   - Otherwise it stores the file as the game's own are stored. A file only the console reads is refused
     (`Cook.ConsoleOnly`), as is a `.arc` that is no archive (`Cook.NotArchive`) and an empty file (`Cook.EmptyFile`).

   Any other fault is `Cook.Failed` with the cook's words. A member of an archive that the store leaves out (a file of
   the tools that made the game) is a Note, `Cook.ToolFile`. No file is dropped without a finding. The camera,
   collision and table cooks name each cause (`Cook.Camera.*`, `Cook.Model.NoPositions`, `Cook.Collision.Attribute`,
   `Cook.Table.*`). A damaged glTF is refused by name and never panics the cook: every accessor, sparse and index read
   goes through one checked element reader (`Cook.Model.AccessorPast`, `ViewPast`, `Cut`), solid ground walks the
   scene as a model does (a cycle is `Cook.Model.Invalid`, a vertex past the mesh's `Cook.Model.VertexPast`), a
   sampler's keys must be numbers that rise with a value each, and a camera's clip is at most a clip's length. A joint
   or material name outside ASCII is `Cook.Model.JointName` or `Cook.Model.MaterialName`: the model keeps names one
   byte a character.

   A layer's `merge/` files go through one function too, nebula-import's `check_merges`, which the game's start
   applies them with. L309 lets in exactly what the start reads (`luma_validate::mergeable`). The refusals:
   `Cook.Merge.NoBase` (no table to add to, in the game or the mod), `Cook.Merge.NoColumn`, `Cook.Merge.RemoveForm`,
   `Cook.Merge.ChangeForm`, `Cook.Cell.Text`, `Cook.Cell.Number`, `Cook.Cell.Whole` (a value of another kind than its
   column) and `Cook.Table.Json`. A `remove` or `change` that matches no row is a Note (`Cook.Merge.RemoveNoRow`,
   `Cook.Merge.ChangeNoRow`) and the rest of the file is added.
20. L502, L504 and L512 read a library with checked arithmetic. Every offset, size and count of a PE, ELF or Mach-O
    goes through one set of helpers in `binary.rs`: a sum past the range or a range past the file is the library not
    reading (L502 by its format) or `luma_mod` not reading (L512 `Lib.InfoUnreadable`). L504 is raised on them as on
    PE and ELF: a mod's library imports nothing from the system on any platform. The evidence is `ld64.lld` with
    `-nostdlib` on the five Apple targets; a library linked by Apple's own linker is not covered by it.
21. L505 for another mod's function (kind 6, `"<mod id>/<export>"`). A library mod's LumaExport rows are read from its
    `luma_mod` (when its `size` reaches `export_count` and `exports` is not null, as the kernel reads them) and kept
    in its report as the `exports` touch (platform, name, signature; kept report form 4). An import is bound as the
    kernel's `modExport` binds it: the first export of the name, with the same signature text. No such export is
    `Name.ModFunction`, another signature `Name.FunctionSig`; an optional import is left unbound. The game reads the
    exports from the needed mod's kept report, `lumalink check` from its check in the mod folder. A needed mod that is
    not installed is L800's.
22. Phrases (`Validator.<Group>.<Name>`) fill `{why}`, `{what}`, `{found}` and the like in every language. The game's
    own text table leaves the `Validator.` rows out (no cost with no mod).

| Code | English template |
|---|---|
| L100 | `{file} is not a .luma: {found}.` |
| L101 | `{file}: {found}.` |
| L102 | `{file}: the entry "{entry}" has a name that is not allowed: {why}.` |
| L103 | `{file}: "{entry}" clashes with "{other}" (the same name on some systems, or a file where a folder is).` |
| L104 | `{file}: "{entry}" is {what}; a .luma holds only files and folders.` |
| L105 | `{file}: {what} is {found}; the most allowed is {expected}.` |
| L106 | `{file}: "{entry}": {why}.` |
| L107 | `{mod}: "{entry}" is not part of a .luma (the top level holds luma.json, layers/, code/, media/, saves/, kit/ and source/).` |
| L108 | `{file} was made with a tool that writes backslashes between folders; they are read as /. lumalink pack writes the standard form` |
| L200 | `{file} has no luma.json.` |
| L201 | `{mod}: luma.json line {line}: {found}.` |
| L202 | `{mod}: luma.json line {line}: "{key}" is not a key that can be here.` |
| L203 | `{mod}: luma.json line {line}: "{key}" is missing.` |
| L204 | `{mod}: luma.json line {line}: "{key}" is {found}; it must be {expected}.` |
| L205 | `{mod} was made for a newer Nebula (format {found}); this one reads format {expected} and older.` |
| L206 | `{mod}: the id "{found}" may only use lowercase letters, digits, - and ., start with a letter, and be 3 to 64 long.` |
| L207 | `{mod} is for {found}, not Super Mario Galaxy.` |
| L208 | `{mod} is made for {expected}; your game is {found}, so it is off.` |
| L209 | `{mod}: {key} names "{entry}": {why}.` |
| L210 | `{mod}: option "{key}": {why}.` |
| L211 | `{mod}: permission "{key}": {why}.` |
| L212 | `{mod}: the load order cannot be met: {found}.` |
| L213 | `{mod}: takes_over "{found}": {why}.` |
| L214 | `{mod}: luma.json line {line}: "{found}" is not a language of the game.` |
| L215 | `{mod}: "{entry}" is in media/ but luma.json does not name it.` |
| L216 | `{mod}: save "{entry}": {why}.` |
| L217 | `{mod}: "{entry}" is in saves/ but luma.json does not name it.` |
| L300 | `{mod}: layer "{layer}": {why}.` |
| L301 | `{mod}: layer "{layer}": {why}.` |
| L302 | `{mod}: {file}: the game has no "{found}", and this kind of file cannot be new.` |
| L303 | `{mod}: {file} replaces "{found}", which has rows: change it with a patch by key instead.` |
| L304 | `{mod}: {file}: a {kind} cannot stand in for "{found}" ({expected}).` |
| L305 | `{mod} and {other} both replace "{found}"; {winner}'s is used.` |
| L306 | `{mod}: "{entry}" is not in files/, patches/, merge/ or assets/, nor a .png or .dds in textures/.` |
| L309 | `{mod}: "{entry}" is in merge/, which adds only to tables (.json), messages (.json), particles (.jpc, .jpc.json) and ray tracing rows (NebulaData/Rt*.tsv).` |
| L307 | `{mod}: layer "{layer}" is chosen by the mod's code, which does not run on {platform}.` |
| L400 | `{mod}: {file} line {line}: {found}.` |
| L401 | `{mod}: {file}: rows of "{target}" are keyed by {expected}, not {found}.` |
| L402 | `{mod}: {file} line {line}: {why}.` |
| L403 | `{mod}: {file} line {line}: {why}.` |
| L404 | `{mod} and {other} both change "{target}" row {row}: {what}; {winner}'s change is used.` |
| L405 | `{mod}: {file} changes "{target}" of {other}; add {other} to needs first.` |
| L406 | `{mod}: {file} line {line}: {why}.` |
| L407 | `{mod}: {file} line {line}: the selector matched no row of "{target}".` |
| L500 | `{mod}: {why}.` |
| L501 | `{mod}: code for "{found}": {why}.` |
| L502 | `{mod}: {file} is not a {expected} library: it is {found}.` |
| L503 | `{mod}: {file} does not export luma_mod.` |
| L504 | `{mod}: {file} needs {library} ({found}); a mod's own library may need nothing from the system.` |
| L505 | `{mod}: {file}: the game has no {kind}.` |
| L506 | `{mod} uses {service} without asking for the "{permission}" permission.` |
| L507 | `{mod} has no code for {platform}, so it is off there.` |
| L508 | `{mod}: {file} needs LumaCore {found}; this game has {expected}.` |
| L509 | `{mod}: {file}: {why}.` |
| L510 | `{mod}: {file} needs {library} ({found}).` |
| L511 | `{mod}: {file} cannot run on iOS: {why}.` |
| L512 | `{mod}: {file}: luma_mod cannot be used: {why}.` |
| L700 | `{mod}: {why}.` |
| L701 | `{mod}: {file} line {line}: {module}: {text}.` |
| L702 | `{mod} needs {module} data version {found}; the installed {library} gives: {expected}. Update {library}, or install a version of {mod} made for it.` |
| L800 | `{mod} needs {other} {expected}: {found}.` |
| L801 | `{mod} cannot be on with {other}: {why}.` |
| L802 | `{mod}: another installed mod, {other}, has the id "{found}".` |
| L803 | `{mod} needs Nebula {expected}; this is {found}.` |
| L804 | `{mod}: {file}: {text}.` |
| L805 | `{mod}: {file} replaces "{found}" ({sig}), which {other} replaces already; only one mod may replace a function.` |

### Section 7 (how it is tested)

16. One broken mod per code is a case in `crates/luma-validate/tests/rules.rs` (made in code by a zip writer that can
   also lie, not folders); the code rules on libraries are in `tests/libraries.rs` over real libraries built by
   `tests/data/build.sh` (PE x64/arm64, ELF x64 RELA/RELR and arm64, Mach-O arm64 rebase/chained and x64). Each case
   also checks: no `{name}` left in any of the 14 languages, the same report twice, the kept report read back equal,
   and a start's check giving the full check's findings. A code with no case fails the suite unless listed with why.
17. The C interface against Rust, field for field and text for text, in `crates/nebula-import/tests/validate_c.rs`;
   its layout asserted in Rust and in C (`tests/luma_validate_layout.c`) for x64 and arm64 Windows, Linux, Android,
   macOS and iOS.
18. **Speed (7.6), measured**: a 1 GiB `.luma` of 2,000 stored files on this machine, file in the OS cache, release
   build: full check 0.15-0.17 s; a start's check 0.011 s with an 80 KB kept report. One byte changed at 600 MiB is
   found (L106). A cold read is bounded by the disk.
