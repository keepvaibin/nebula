# Permissions and consent

## More rules

### 1.1 What the host knows of a mod's permissions

`galaxy::luma::Manifest` (runtime/include/galaxy/luma.h) gains, read from `luma.json` `permissions` by whatever
reader 6.1 settles on (the validator's parse, so rule 7 holds):

```cpp
struct Permission {
    std::string name;                // one of services-registry 5.1's names
    std::string why;                 // the manifest's line, shown as it is (the mod's own words, its language)
    std::vector<std::string> hosts;  // net.internet only: as written, in order
};
std::vector<Permission> permissions;  // in the manifest's order; the validator refused anything else
```

```cpp
namespace galaxy::mods {

enum class Answer : std::uint8_t { NotAsked, Granted, Declined };

struct PermissionFacts {
    std::string name;                // "net.internet"
    std::string row;                 // "Permission.net.internet": the short name's row
    std::string text_row;            // "Permission.net.internet.Text": the line's row; {hosts} to fill
    std::string hosts;               // net.internet: the hosts joined by ", "; "*" -> the row Permission.net.internet.Any
    std::string why;                 // the mod's own line
    Answer answer = Answer::NotAsked;
    bool changed = false;            // answered before, for a declaration that differs now (asked again)
    bool required = false;           // the entry library imports its service without LUMA_OPTIONAL; `native` always
};

// One question per mod: its code and its permissions together.
struct ModConsent {
    bool code = false;               // it has code for this platform
    bool code_changed = false;       // answered before, for other libraries than these
    Answer code_answer = Answer::NotAsked;
    std::vector<PermissionFacts> permissions;  // in the manifest's order, `native` first when declared
};

// 16 lowercase hex digits: the digest of 1.2 (the only C++ writer of it).
[[nodiscard]] std::string permission_digest(const galaxy::luma::Permission& permission);
// The mod's code and declared permissions with their answers.
[[nodiscard]] ModConsent consent_facts(const ModInfo& mod);
// True when it has code whose answer is NotAsked for its current token, or code Granted with a declared permission
// NotAsked (changed ones included): the screen asks before the mod starts. Never for a mod without code; never for
// code the player said Not now to (the mod is off until the player turns it on, which asks again).
[[nodiscard]] bool consent_needed(const ModInfo& mod);
// Records the player's answer for the mod's code (its current token).
void record_code_answer(ModInfo& mod, bool granted);
// Records the player's answer for one permission (the digest of the declaration as shown). False: the mod does not
// declare it (a caller bug, logged). The caller saves the list with save_mod_settings after recording, as the Mods
// page does for every setting (record_* cannot: the file is the whole list).
bool record_answer(ModInfo& mod, std::string_view permission, bool granted);
// The names to pass to luma_load_mod: Granted answers only, in the manifest's order.
[[nodiscard]] std::vector<std::string> granted_permissions(const ModInfo& mod);

}  // namespace galaxy::mods
```

Threads: there is no `s_mods_mutex` in mods.cpp; what exists is used. `ModInfo` is changed only on the
thread that owns the mod list (the Mods page's, as `code_allowed` is today), and the file is written through
`save_mod_settings`, which takes mods.cpp's settings lock (`settings().mutex`). `start_plugins` does not read
`ModInfo`: the host fills `PluginFile.granted` (a new `std::vector<std::string>`, beside `code`) from
`granted_permissions` when it builds the plugin list at start, once, before the first `luma_load_mod`; a later change
on the Mods page applies at the next start.

`ModInfo` gains `code_declined`, `code_changed`, `permission_answers` (name -> {granted, digest} as stored) and
`required_permissions` (below); `PluginFile` gains `granted`. Code: mod_consent.h/.cpp, mods.cpp
(settings lines), luma.cpp (Manifest.permissions).

`required` is read from the validator's kept report of the mod's entry library for this platform (its
kind 5 rows and their flags), matched to permissions through the catalog
(services-registry 9: one list, read by the validator, LumaScript and this). The host never loads a library to find
out. The host code that reads the kept report fills `ModInfo::required_permissions`; until it
does, only `native` is required. A mod whose kept report has no import rows (refused before the check) has no permission `required` but
`native`.

### 1.5 Tests

- The store: grant, decline, a changed hosts list (asked again), a reworded `why` (not asked again), a removed
  permission (line dropped), `takes_over` (answers move), a removed mod (answers go). A host unit test
  (runtime/tests), no game needed.
- The loader: a mod with `net.internet` declined starts not, with L506 on the Mods page in two languages; granted,
  it starts; an optional import of it starts with the slot null (harness, through the `grant` key of
  services-registry 14).

### 2.0 Two additions to the facts half (the screen needs them)

- Code is consented like a permission. "Consent for code and permissions; asked again when an update changes either."
  Today's host keeps it apart (`code_ok=<token>` in the mod settings; `code_allowed`). Fold it into this interface so
  the screen asks one question per mod: `ModConsent consent_facts(const ModInfo&)` returning `{ bool code; bool
  code_changed; Answer code_answer; std::vector<PermissionFacts> permissions; }`, and `record_code_answer(ModInfo&,
  bool)`. The store keeps `code_ok=<token>` as it is (no migration); a declined code is `code_no=<token>`, so "Not
  now" is not asked again at every start, only when the token changes or the player turns the mod on again.
- `required` per permission. `PermissionFacts.required`: true when the mod's entry library imports a service of that
  permission without `LUMA_OPTIONAL`. `native` is always required (the extra libraries load before the entry). The
  screen says what declining does (2.3) and must not guess.

### 2.1 When the question is asked

The question is asked for a mod when `consent_needed` holds: it has code whose answer is not Granted for its current
token, or a declared permission that is NotAsked (including `changed`). Never for a mod without code (a content-only
mod cannot use a permission: the validator's Note, facts 1.4).

| Moment | What happens |
|---|---|
| Import (6.1), a mod that passes the check | The import's result screen ends with the question (2.2). The mod is installed either way; Allow turns it on, Not now installs it off. |
| Turned on on the Mods page | As today's `ModsCodeAsk`: the question comes before the change is applied; Not now leaves it off. |
| Start, a mod on whose answer no longer holds (an update changed the code token or a permission's digest; a mod copied in by hand) | Its code does not start this session (the host passes it to no `luma_load_mod`, so nothing of it binds) and its content is not used (a mod with code is one thing: its content may need its code). When the title appears the menu opens on the question by itself. Allow applies at the next start, through the existing restart question (`ModsRestartAsk`). |
| A mod asks | Never the question directly: the request goes through the import screen, which asks as at import. |

No other moment. A mod's code cannot open, answer or skip the question (the screen is host UI; no service reaches it).

### 2.2 What it shows

One question per mod, a page at a time, in the settings menu's own frame: a new `NativeSettingsStatus::ModConsent`
with the mod's rows, drawn by NebulaSettingsMenu as the other questions are.

1. Title row: the mod's name (its manifest's text in the played language, else `name`), its version, and its icon
   (`NativeSettingsPicture`, as `ModAbout` shows it).
2. If it has code: `SettingsModsCodeAsk` (exists in 14 languages: "This mod runs its own code and can do anything on
   this device. Only allow mods you trust."). If `code_changed`: `Consent.CodeChanged` above it.
3. Each declared permission, in the manifest's order, one row each:
   - label: the short name (`Permission.<name>`), with `Consent.New` when `changed`, or NotAsked on a mod whose code
     was allowed before;
   - text: `Permission.<name>.Text` with `{hosts}` filled;
   - under it the mod's own `why` in quotation marks after `Consent.Why`, drawn in the game's font as given (the mod's
     words, in the mod's language; a character the font lacks shows the font's missing mark, never dropped);
   - `Consent.Required` when `required`, else `Consent.Optional`.
4. `native`, when declared, is always first, and its row is drawn in the colour the menu uses for a destructive
   choice (as `ModRemoveAsk`).
5. Buttons: `Consent.Allow`, `Consent.Choose` (only when it has an optional permission), `SettingsChoiceNotNow`.
   The focus starts on Not now, so an A pressed through the screen grants nothing.
6. Choose: each permission row becomes a toggle (allow / don't allow); required ones stay allowed with the
   `Consent.Required` text; `SettingsDone` (exists) records them.

Long text wraps as the menu's help line wraps; the page scrolls with the menu's own scrolling (pad, keys, wheel, drag).

### 2.4 Input, languages, no cost

- Pad, keyboard, mouse and touch drive it as they drive the menu's other questions (the menu's input path; no new
  input code). Escape and B are Not now.
- Every new row is in the 14 languages; `--fonts` checks them.
- No mod with code installed: `consent_facts` is never called and the status never shown; nothing per frame.

### 2.5 Rows (14 languages)

| Id | English |
|---|---|
| `Consent.Allow` | `Allow` |
| `Consent.Choose` | `Choose` |
| `Consent.CodeChanged` | `This version changes the mod's code.` |
| `Consent.New` | `new` |
| `Consent.Why` | `The mod says:` |
| `Consent.Required` | `Needed to start` |
| `Consent.Optional` | `It starts without this` |
| `Consent.NotAllowed` | `Not allowed: {list}` |
| `Consent.Waiting` | `Off: waiting for your answer` |
| `Consent.OffDeclined` | `Off: you did not allow {permission}` |
| `Consent.Code` | `Code` (the row on a mod's page) |
| `Consent.Allowed` | `Allowed` |
| `Consent.Denied` | `Not allowed` |

### 2.6 What the harness reads, and the tests

The screen writes as it shows: `consent.shown.<id> {"code", "code_changed", "rows": [{"name", "label", "text", "why",
"required", "new"}], "focus"}`; each answer: `consent.answer.<id> {"code": "yes"|"no", "permissions": {"<name>":
"yes"|"no"|"not asked"}}`. The harness's `permissions` key (harness section 2) sets answers in the store before start
and is recorded the same way, so tests that are not about the screen never see it.

| Test | Must show |
|---|---|
| 6401 | import of a mod with code and `net.internet` (two hosts): the question's facts; the focus on Not now; Allow: the mod on, both answers stored with their digests, its service import binds |
| 6402 | the same, Not now: the mod off, `Consent.Waiting` on its row; no question at the next start; turning it on asks again |
| 6403 | an update adding a host: at start its code does not start and its content is not used; the title opens the question with `Consent.New` on `net.internet`; Allow, then restart: it binds |
| 6404 | an update changing only `why`: not asked |
| 6405 | an update changing the library: `Consent.CodeChanged`; not started until answered |
| 6406 | Choose with an optional `system.clipboard` declined: the mod on; the slot null; `Consent.NotAllowed` lists it |
| 6407 | `native` declared: the first row, `Consent.Required`, its help saying full access (no colour: 2.7) |
| 6408 | each of pad, keyboard, mouse and touch answers the question; pictures of it in English and Japanese on Direct3D 12 and Vulkan |
| 6409 | a mod without code that declares a permission: never asked; the validator's Note in the mod log |

### 2.7 Screen half as written

- A page of rows, not a new status. The question is the Mods tab's page while a mod waits
  (`first_code_to_allow_locked` = on, no problem, `consent_needed`), built by the host as the log page is, so the
  menu's rows, help line, scrolling and input carry it unchanged (2.4): no `NativeSettingsStatus::ModConsent`, no
  module drawing code, no change to render_api.h or nebula_game_api.h. The old one-line `ModsCodeAsk` question is no
  longer raised (its status stays in the enums, unused).
- What it shows, every word through the module's text (new export `luma_modlog_row_words`: any row with named values):
  a heading with the mod's name and version; `Consent.CodeChanged`, `SettingsModsCodeAsk`; a row a permission: label
  `Permission.<name>`, value `Consent.Required` / `Consent.Optional` (choosing: `Consent.Allowed` / `Consent.Denied`,
  left and right), help `Permission.<name>.Text` with {hosts} and `Consent.Why` with the mod's words in quotation
  marks; buttons ALLOW, CHOOSE (an optional permission declared), NOT NOW (choosing: DONE, NOT NOW), the focus on NOT
  NOW. Rows S.ALLOW, S.CHOOSE, S.NOT_NOW.
- Not drawn: the icon (the question has no picture slot; the mod's page ABOUT shows it) and `native`'s warning colour
  (rows have no colour); `native` stays first and its text says full access.
- Facts as 2.6: `consent.shown.<id>` once a showing, `consent.answer.<id>` at each answer ("not asked" for Not now).
  Keys asked of F for the harness table (host half).
- Later changes (2.3) on the mod's page: `Consent.Code` and each answered permission, Allowed / Not allowed by left
  and right, help the permission's text (+ `Consent.Required`), then `Consent.NotAllowed` {list}; a changed answer
  counts as a draft change (code_allowed, code_declined, permission_answers compared), so Apply and `ModsRestartAsk`
  follow. A mod with `code_declined` reads `Consent.Waiting` on its list row.
