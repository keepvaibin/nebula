# The mod log

## More rules

### 1. One entry

```c
/* native/include/luma_modlog.h (new; this contract) */
#define LUMA_MODLOG_ARGS 8u

typedef enum LumaModLogLevel {
    LUMA_MODLOG_ERROR = 0,  /* something the mod meant does not happen: refused, off, a placeholder stands in, a call did nothing */
    LUMA_MODLOG_NOTE = 1    /* shown, nothing lost: an overlap the order settled, a selector that matched nothing */
} LumaModLogLevel;

typedef enum LumaModLogSource {
    LUMA_MODLOG_VALIDATOR = 0,  /* a finding of luma_validate at import or start (validator.md) */
    LUMA_MODLOG_LOADER = 1,     /* a refusal of luma_load_mod (loader.md section 3) or the host's start order */
    LUMA_MODLOG_COOK = 2,       /* a fact of the device cooker: a failed patch or file, an overlap (L804, L305, L404) */
    LUMA_MODLOG_CODE = 3,       /* LumaCore.report: the kernel's codes 1-99, a service's 100-899, lumart's 900-9999 */
    LUMA_MODLOG_CONTENT = 4,    /* the game's content at run time: a reference not found, a placeholder, an effect
                                   that draws nothing (contracts references, effects) */
    LUMA_MODLOG_GUARD = 5       /* a fault caught in the mod's code (lumalink/guard.md) */
} LumaModLogSource;

typedef struct LumaModLogEntry {
    uint32_t size;               /* sizeof(LumaModLogEntry) as the caller knows it */
    uint32_t source;             /* LumaModLogSource */
    uint32_t level;              /* LumaModLogLevel */
    uint32_t code;               /* the source's own code: 203 for L203, 1 for Hook.OutOfMemory, 101 for Service.Argument */
    const char* mod;             /* the mod's id; for a .luma whose manifest cannot be read, its file name. Never null */
    const char* file;            /* the entry inside the mod ("luma.json", "layers/base/assets/aura.json"), a script
                                    file, a function ("Mario::update") for the kernel's run-time lines,
                                    "<service>.<function>" for a service's report of a mistake (line 0), or
                                    "<service> event <kind>" for the guard in a service's event; "" if none */
    uint32_t line, column;       /* 0 when the place has none */
    const char* names[LUMA_MODLOG_ARGS];   /* the message's parameter names, in the row's order */
    const char* values[LUMA_MODLOG_ARGS];  /* VALIDATOR, LOADER, COOK: "T:<text>" a literal, "P:<phrase id>" a phrase
                                              (validator.md rev 2 item 1). CODE, CONTENT, GUARD: the values as given,
                                              plain UTF-8 (loader D3-1's args); nothing is stripped from them */
    uint32_t count;              /* how many of names/values; at most LUMA_MODLOG_ARGS */
    const char* text;            /* the line in the played language, already rendered by the source; null: the log renders
                                    it (section 3) */
    const char* english;         /* the same line in English for the game's log; null: rendered as above */
} LumaModLogEntry;

/* Adds an entry, or counts a repeat of one already kept (section 2). Any thread. Copies everything it is given. */
void luma_modlog_add(const LumaModLogEntry* entry);
/* Once a session, from the host's start before the first scene: how many of the player's mods are on (the harness's
   own game half is not counted), and the harness's fact writer (null outside a harness run). */
void luma_modlog_session(uint32_t mods_on, void (*fact)(const char* key, const char* json));
```

Both are exported by the game module and found by name, as `luma_load_mod` is; `luma_modlog.h` holds their
types (`LumaModLogAdd`, `LumaModLogSession`), so `luma_host.h` does not change. Inside the module `add` is
`luma::modlog::add(entry, rowId)`: the kernel and the content pass the NebulaText row of the code (section 3).

### 2. Keeping

- Key. An entry is (mod, source, code, file, line, column). An entry with a key already kept is a repeat: its count
  grows, nothing else changes, and it is not shown again (section 4) except as its count. Args are the first time's.
- The kernel's reports. `record` in LumaLoader.cpp keeps its per-mod list as now. For a new (file, line, code) it
  calls `luma::modlog::add` with source CODE after it releases the mod's lock, and keeps the entry pointer `add`
  returns beside its report (null when the log's limits kept none). A repeat calls `luma::modlog::counted(entry,
  count)` with record's own count, also after the lock: one atomic (the entry's count becomes `count` when that is
  more), no lock, no lookup, nothing copied; null does nothing. Its log lines ("[mod x] ...", powers of ten) stay as
  they are; the mod log writes none for CODE entries (one line per fault in the game's log, not two). (F-S1; The held
  pointer makes a repeat one atomic.)
- Limits. At most 256 entries a mod (as the kernel's `record` keeps) and 1,024 a session of all mods together; each
  mod's first 16 entries are kept even past the session's 1,024 (so at most 1,024 + 16 a mod in all), so one mod's
  many problems never hide another mod's first ones. Past either limit for a mod, one Note for that mod
  `ModLog.TooMany` ("{mod}: {count} more problems; only the first are kept") is kept and counted (it is kept beyond
  both limits: one a mod), so the page says how many were not kept. A value is kept whole (no cut); `text` and
  `english` are kept whole. A `count` over 8 is the caller's bug: the ninth on are dropped and the entry gains the
  Note `ModLog.ArgsCut`, said once.
- Order. Entries are kept in the order they were added; the page shows them by mod (list order), each mod's in added
  order.
- Lifetime. The session. A mod taken out (reload, 1.11) keeps its entries, marked with the reload's number, so what
  went wrong before the reload stays readable. Nothing is written to disk by the log: the game's log (`nebula.log`)
  has every line in English already (section 3).
- Threads. `add` takes a lock held only to append or count (no allocation under it: the copies are made before); the
  screen reads by a revision number (section 4).
- The store is made once. The store is reached through one atomic pointer, null until the first `add`. The first `add`
  that finds it null allocates a store and publishes it with a compare-and-swap from null; an `add` that loses the
  swap frees its own store and uses the winner's. Every later `add` reads the pointer (acquire) and never allocates
  the store again. The pointer is freed only at the module's unload, after the host has stopped every thread that
  adds.

### 3. Text

The session's language is fixed for the session (a change applies at the next start: `LanguageRestart`), so each
entry is rendered once, when it is added, in the played language and in English.

| Source | Rendered by | How |
|---|---|---|
| VALIDATOR, LOADER, COOK (L-codes) | the host, before `add` | `luma_validate_message(code, names, values, count, language)` (loader 3, validator rev 2) for `text` in the played language and for `english` in UsEnglish |
| LOADER, code 0 (a host sentence) | the module, in add | the entry's `host` is the host's text in capitals as the Mods page has it ("ITS CODE DID NOT START. IT IS NOT A MOD'S CODE"); `text` and `english` null; the module renders the played text as the settings menu renders a host text (its S. rows, sentences and labels joined as the language joins them: `NebulaSettingsMenu::translateHost`) and `english` is `host` as given (those rows' English). The key's code is 0 and `file` the library's name. `host` is read only when the entry's `size` covers it |
| CODE | the module, in `add` | the row of the code (kernel rows 1-99, a service's rows through `luma::services::rowOf`: the row id and its names, lumart's `Script.S%04u`) through NebulaText in the played language, `{mod}` and the row's names filled with the values as given; `english` from `NebulaText::english` |
| CONTENT | the module, in `add` | the rows of the references and effects pages, as CODE |
| GUARD | the module, in `add` | the guard's own rows (their ids named when 1.6 is drafted), as CODE |

- A row with a `{name}` the entry does not give shows `{name}`: visible, never blank.
- When the host has no `luma_validate_message` (the importer library missing), `text` and `english` are the code and
  every parameter as given (`L505 kind=P:Name.Function found=T:Mario::foo sig=T:void()`), and the log keeps one more
  entry, `ModLog.NoText` (an Error, mod `nebula`, said once): the player sees that the words are missing, not a blank
  line.
- Every glyph is drawn with the language's font; a character the font lacks is the font's own missing-glyph mark, and
  `nebula_text.py --fonts` checks every row of section 7 against each disc's font.

### 3.1 Level of each source's entries

| Source | Level |
|---|---|
| VALIDATOR | the finding's own: Error for an Error finding, Note for a Warning or an Info |
| LOADER | Error (the mod or a library did not start), except `Consent.OffDeclined` (consent 2.3: the player's answer), a Note |
| COOK | Error for `failed`, Note for an overlap the order settled |
| CODE | Error, every code: the kernel's 1-99 (2 and 3 included: the game's function ran instead of the mod's result, so what the mod meant did not happen), a service's 100-899 (each "it did nothing"), lumart's 900-9999; except a service's Note the host writes with its row: `System.LinkOpened` (services-system 6, code 140) |
| CONTENT | Error (a placeholder stands in, an effect draws nothing) |
| GUARD | Error |

The caller passes `level`; the module checks it against this table for CODE, CONTENT and GUARD (it makes those
itself) and the host for the rest; an entry that gives its row (luma_modlog.h `row`, the host's) keeps the level it
gives (the kernel's `record` never sets `row`). A Note is not a notice (4.1), except `System.LinkOpened`.

### 4.1 The notice (while playing)

- When. An Error entry that is new (not a repeat) is shown once as a notice; Notes are not, except a page a mod
  opened in the browser (`System.LinkOpened`). Entries added before
  the first scene exists (the start check, the loader, the cook) are not shown one by one: when the title appears,
  one notice says `ModLog.Summary` ("{count} mod problems: Mods > Log") if there are any.
- What. One line: the mod's id, a colon, and the entry's `text`; the text alone when it already begins with the
  id (the content rows and the kernel's do, `{mod}: ...`). The id, not the manifest's name: the module has only the id,
  and the Mods page, where the player meets the mod's name, shows the same id under it. A line longer than the notice's width is cut at a character boundary and ends in
  "…"; the page has it whole.
- Where and how long. The bottom-left of the screen, above the game's own bottom HUD row, in the game's message
  font at the achievement toast's scale; at most 3 lines at once, newest at the bottom; each held 6 s (a tunable,
  `NEBULA_TUNABLE`), then fades over 0.3 s. A fourth waits; when more than 3 wait, the oldest waiting are folded into
  one line `ModLog.More` ("{count} more: Mods > Log").
- Never blocks. No input is taken, no pause, no sound. It is drawn on top of the game and under the settings menu
  and the game's own pause screens (so it never covers a choice the player must read).
- Scenes. It runs in the game's scenes (title, play, the map), as the achievement pop-up does. The lines being
  shown and the log's notice cursor outlive a scene, so a scene change neither drops a line already taken nor shows
  it twice; in a scene of another kind (the star select) the lines wait and go on in the next game scene.
- Pictures and video recording. It is part of the frame (the player sees what a recording shows); the harness's
  pictures include it.

### 4.2 The page (Mods > Log)

- The MODS page gets one button row above its buttons, "LOG (n)" (n: the entries kept) or "LOG" with the help line
  `ModLog.Empty` when none, and one button OPEN that opens the log page the same way a mod's own page opens
  (`s_mod_log_open` beside `s_mod_options`); Done, Escape or B goes back to the LOG row. The rows are the entries,
  grouped under each mod's name
  (a heading row), each with:
  - label: the `text` (one line, cut where it does not fit, as every row);
  - value: `file:line` (or `file`, or nothing), and "×N" when the count is over 1;
  - the help line under the page: the source in words (`ModLog.Source.Validator` "found when checking the mod" ...,
    section 7), the code (`L203`, `S0904`, `Hook.OutOfMemory`) and the text, for the selected row: one line,
    narrowed and then cut with an ellipsis where it does not fit (the menu's status line);
  - the entry whole: A or Space on an entry row opens it in the ABOUT window (the mod
    page's, `NativeSettingsStatus::ModAbout`): the source and code, the place, then the whole text, a page at a
    time (Previous, Next; Escape or B back to the row). A refusal listing the game's signatures (L505) is read here
    in full; the 160-character cut of the Mods page's status help stays one line.
  - after a mod's entries, `ModLog.TooMany` when some were not kept.
- A mod's own page (6.2) shows that mod's entries the same way, under its options.
- The page is read with the menu's own input: pad (D-pad and A/B), keyboard (arrows, Enter, Escape), mouse (wheel,
  click), touch (drag, tap); nothing on it changes anything.
- It is built from the same `NativeSettingsView` rows the game's menu and the overlay already draw
  (`render_api.h`); the help line is the row's own help, marked as already in the played language (`names` 4); no new
  status and no new drawing code in the renderers.

### 4.3 What the harness reads

The code that shows a line writes it as a fact as it shows it (`modlog`):
- each entry when it is kept: `modlog {"mod", "source", "level", "code", "file", "line", "column", "text", "english",
  "count"}` and, for a repeat, the same with the new count;
- each notice when it is drawn the first time: `modlog.notice {"mod", "code", "text"}` (the text as cut on screen);
- each row of the log page once each time the page opens: `modlog.page {"row", "label", "value"}` (the host's; not `modscreen.log`, which is the facts of a mod whose id is `log`).
- each entry opened whole: `modlog.entry {"mod", "pages", "text"}` (the host's; the whole text the window holds).
This adds fields to harness `modlog` (source, level, code, column, english, count) and the fact `modlog.notice`:
a revision request to F, section 9.

### 5. No mod, no cost

- No mod in the session's list: the store is never allocated, the notice actor is never created (it is made in the
  scene's layout set-up only when `luma_modlog_session` said mods are on, or something was said anyway), the MODS
  page's LOG row reads "LOG" (no count) from an empty walk. Per frame: nothing. In a harness run the module writes, at each game
  scene's set-up, `modlog.state {"allocated", "notice"}`, so 3121 reads it.
- Mods but nothing said: per frame one 32-bit read of the revision by the notice actor.
- `luma_modlog_add` with no mod in the session is still valid (the host may report about a file it could not read):
  the store is made on the first `add` (2, "made once").

### 6. Errors of the log itself

- `entry` null, `size` smaller than the struct up to `english`, `mod` null, an unknown source or level: one line in the
  game's log naming the caller's mistake and the values it gave; nothing is kept (it has no mod to name). A bug in
  Nebula, never in a mod.
- No memory for a copy: `NEBULA_PANIC` naming the mod and the code (the log cannot be silent).

### 7. Rows (NebulaText.tsv, 14 languages)

| Id | English | Args |
|---|---|---|
| `ModLog.Summary` | `{count} mod problems: Mods > Log` | count |
| `ModLog.More` | `{count} more: Mods > Log` | count |
| `ModLog.TooMany` | `{mod}: {count} more problems; only the first are kept` | mod, count |
| `ModLog.ArgsCut` | `{mod}: a message had more than 8 values; the rest are not shown` | mod |
| `ModLog.NoText` | `Mod messages are shown as codes: the importer library is missing` | |
| `S.LOG` | `Log` (the host's label "LOG", translated as host labels are) | |
| `ModLog.Empty` | `Nothing to report.` | |
| `ModLog.Source.Validator` | `Found when checking the mod` | |
| `ModLog.Source.Loader` | `Found when starting the mod's code` | |
| `ModLog.Source.Cook` | `Found when preparing the mod's files` | |
| `ModLog.Source.Code` | `Reported by the mod's code` | |
| `ModLog.Source.Content` | `Found while playing` | |
| `ModLog.Source.Guard` | `The mod's code failed and was stopped` | |

Content rows (references, placeholders, effects) are in contract `references` and `effects`, block 620-699 of
`report`'s codes ("free).

### 8. How it is tested

| Test | Setup | Must show |
|---|---|---|
| 3121 | no mod | no `modlog` fact; the LOG row "Log" with no count; the store not allocated (`luma::modlog::allocated()` fact 0) |
| 3122 | a `.luma` with a broken `luma.json` (L201: not JSON, validator 4), installed placed | one `modlog` entry source 0, code 201, file `luma.json`, line; at the title one `modlog.notice` with `ModLog.Summary` count 1; English and Japanese runs (`language`) give the validator's own two texts |
| 3123 | a mod whose code reports the same (file, line, code) 1,000 times | one entry, count 1000; one notice; the game's log has the kernel's lines at 1, 10, 100, 1000 and no mod-log line |
| 3124 | four mods each reporting one error in one frame while playing | three notices, then one; the fourth's notice after the first is gone; a fifth and sixth fold into `ModLog.More` |
| 3125 | a mod that adds 300 distinct entries, then a second mod one | the first: 256 kept and one `ModLog.TooMany` with count 44; the second's entry kept and noticed. Five mods adding 300 each, one after another: 1,024 + 16 kept (four mods' 256, the fifth's first 16), one `ModLog.TooMany` a mod (44, 44, 44, 44, 284) |
| 3126 | the importer library removed from the run's folder | entries' text is the code and values; one `ModLog.NoText` |
| 3127 | the log page opened by pad, by keyboard, by mouse, by touch | the same `modlog.page` rows each time; pictures in English and in Japanese of the page and of a notice, on Direct3D 12 and Vulkan |
| 3129 | the log page opened, space on its first entry | `modlog.entry` once, jl-repeat's, 1 page, the whole text |
| 3128 | a service report from another thread, and in the same frame the game thread reporting the same (file, line, code) for the same mod; also two threads making the first `add` of a session at once | one entry, count 2; one store (`luma::modlog::allocated()` fact 1); no lock-order fault under the harness's thread check |

Each failing case of section 6 is a unit test of the store (host-side C++ test like `asset_store_tests.cpp`, no game
run): null entry, short size, null mod, unknown source; each gives its log line and keeps nothing.

### 10. What the code settled

- Store. Entries and mods are lists that only grow; each entry is one block (the entry, its played text in UTF-16 for
  the screen, its mod, file, played text in UTF-8 and English). Published by one release store; the screen walks
  without the lock. `ModLog.TooMany` is a count on the mod's record, not an entry: the page shows it as the last row
  of that mod's group, the harness reads it as `modlog {"mod", "row": "ModLog.TooMany", "level": "note", "count"}`.
- The log's own lines (`ModLog.ArgsCut`, `ModLog.NoText`) are entries of code 0 (no source uses 0) whose `file` is
  their row id, so each is said once a mod and source. Their log line is written by the module; CODE's are the
  kernel's, the host's are the host's, CONTENT's are the module's: one line in `nebula.log` a fault.
- Level check (3.1): a CODE, CONTENT or GUARD entry given as a Note, without a `row`, is kept as an Error, with a log
  line naming the caller's bug.
- Row casing follows the settings menu's (sentence case): `S.LOG` "Log"; consent's `Code`, `Allowed`, `Not allowed`;
  "Off:" in `Consent.Waiting`, `Consent.OffDeclined`, `Start.ModOffCook`.
- Facts written now by the module: `modlog` (each keep and repeat), `modlog.notice` (once a line, as cut on screen),
  `modlog.state`. `modlog.page` by the host's page.
- Page. The module exports three reads for it (luma_modlog.h), found by name in `galaxy::modlog::begin`:
  `luma_modlog_next(after, LumaModLogView*)` (the kept entry after `after`, in added order, copied whole field by
  whole field up to the view's `size`), `luma_modlog_next_mod(after, &id, &past)` (the mods in the order their first
  entry came, with how many were not kept), `luma_modlog_words(row, mod, count, out, size)` (a `ModLog.*` row in the
  played language, {mod} and {count} filled). Each entry keeps its code as the page names it (`code_name`): `L<code>`
  for VALIDATOR, LOADER, COOK; the module's row id for the rest, a script's without `Script.` (`S0904`); the number
  when there is no row; "" for code 0. The page groups mods in the MODS lists' order, then the mods the lists lack (a
  .luma that could not be read), in the log's order. A mod's own page lists its entries under a LOG heading before its
  buttons. `ModLog.Row` was renamed `S.LOG`: the row is the host's label.
- Host rows (for start-check 2). `LumaModLogEntry` gains `const char* row;` after `host`, read when `size` covers it:
  a row of the game's text table that words a host entry whose `text` and `host` are null, whatever its source (the
  host's own lines that no validator code words, e.g. `Start.PartMissing`). Its {names} come from the entry; a value
  `R:<row>` is that row's words in the same language, its own {names} from the same entry, one level deep (`{part}` =
  `R:Start.Part.Space`, whose `{needed}` the entry also gives). Host side: `galaxy::modlog::add_row(source, level,
  mod, code, file, row, values)` (mod_log.h).
