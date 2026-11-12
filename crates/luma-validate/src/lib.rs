//! The one validator of a `.luma` (contracts `luma-format` and `validator`, revision 1; plan 3.1, rule 7).
//! LumaLink runs it before packing and the game runs it at import and at each start; what passes one passes the
//! other, with the same findings. It reads, never writes, never touches the network, and never panics on any input.

pub mod json;
pub mod names;
pub mod semver;
pub mod zip;

mod binary;
mod rules;
mod saved;
mod source;
mod text;

use std::collections::BTreeMap;
use std::path::Path;

pub use names::Names;
pub use rules::save_problem;
pub use saved::{About, Saved};

/// The validator's version: `<format major>.<rules minor>` (validator contract 6).
pub const VERSION: (u32, u32) = (1, 0);
/// The highest format minor this validator reads (luma-format 8).
pub const FORMAT: (u32, u32) = (1, 0);

/// The game's 14 languages, in the order of `localization/NebulaText.tsv`.
pub const LANGUAGES: [&str; 14] = [
    "UsEnglish", "EuEnglish", "UsFrench", "EuFrench", "UsSpanish", "EuSpanish", "EuGerman", "EuItalian", "EuDutch",
    "UsPortuguese", "JpJapanese", "KrKorean", "CnSimpChinese", "TwTradChinese",
];

/// The platforms a mod's code may be built for (luma-format 6).
pub const PLATFORMS: [&str; 9] = [
    "windows-x64", "windows-arm64", "linux-x64", "linux-arm64", "macos-arm64", "macos-x64", "android-arm64",
    "android-x64", "ios-arm64",
];

/// The three programs of the game (luma-format 4, `game.regions`).
pub const REGIONS: [&str; 3] = ["RMGE01", "RMGP01", "RMGJ01"];

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Level {
    /// The mod is refused, or off.
    Error,
    /// Shown, not refused.
    Note,
}

/// A message parameter: a literal (a name, a number, a path) or a phrase, which is itself translated.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Arg {
    Text(String),
    Phrase(&'static str),
}

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Finding {
    /// The rule's number: 203 is L203.
    pub code: u16,
    pub level: Level,
    /// None only when `luma.json` cannot be read; then `file` names the `.luma`.
    pub mod_id: Option<String>,
    /// The entry inside the mod (`luma.json`, `layers/base/patches/...`); for the container's rules, the `.luma`.
    pub file: String,
    pub line: Option<u32>,
    pub column: Option<u32>,
    /// A JSON pointer for findings inside JSON (`/options/2/default`).
    pub pointer: Option<String>,
    /// The message's parameters, by name.
    pub args: Vec<(&'static str, Arg)>,
}

/// A rule that could not be checked because an input it needs is absent (LumaLink with no imported game): named,
/// never skipped silently (validator contract 2). `pack` refuses while any remain.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct NotChecked {
    pub code: u16,
    /// What was absent, as a phrase id.
    pub because: &'static str,
    /// How many places the rule would have been checked.
    pub places: u32,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Report {
    /// Every finding, sorted by file, line, column, code, then parameters.
    pub findings: Vec<Finding>,
    pub not_checked: Vec<NotChecked>,
    /// What the mod changes (for the other mods' overlap Notes).
    pub touches: Touches,
    pub validator: (u32, u32),
    pub format: (u32, u32),
    /// The mod's id, version and conflicts, when luma.json gives them.
    pub about: Option<About>,
    /// The `.luma`'s fingerprint: SHA-1 over its zip directory (each entry's name, method, flags, checksum, sizes and
    /// place), which changes with any entry's data. None for a folder.
    pub fingerprint: Option<String>,
    /// Whether every rule ran (false: a start's check, with the whole-file rules' findings from the kept report).
    pub full: bool,
}

impl Report {
    pub fn errors(&self) -> usize {
        self.findings.iter().filter(|f| f.level == Level::Error).count()
    }
    pub fn passed(&self) -> bool {
        self.errors() == 0
    }
}

/// Where the mod is.
pub enum Source<'a> {
    /// A `.luma` file.
    File(&'a Path),
    /// A `.luma`'s bytes, with the name to show for it.
    Bytes(&'a [u8], &'a str),
    /// A folder laid out as a `.luma` is inside.
    Folder(&'a Path),
}

/// A game path's rows, as the content system defines them (luma-format 5.1; contract table-keys).
#[derive(Default)]
pub struct Table {
    /// The columns that make a row's key, in order; `["#"]` for a table whose rows are named by place (table-keys 3).
    pub key: Vec<String>,
    /// Every column and its type. A column whose name is not known is `0x` and the 8 hex digits of its hash (the
    /// game's x31 of the name): a patch's name for it then matches by hash.
    pub columns: Vec<(String, ColumnType)>,
    /// The key values of each row the game has, joined by U+001F, when known; for `#`, "0" to the last row's place.
    pub rows: Option<std::collections::HashSet<String>>,
    /// Keys the game itself gives more than one row (table-keys 4), with how many.
    pub repeated: std::collections::HashMap<String, usize>,
    /// Each row's values as text (the same text as `rows`' parts), in `columns`' order, when known: for L407.
    pub cells: Option<Vec<Vec<String>>>,
    /// Whether a key matches a row whatever its case (the table's own rule: RtEffects, RtMaterials, RtWater, RtGlow).
    pub case_insensitive: bool,
}

/// A column name's hash, as the game's tables keep it (x31 of the bytes, each byte signed as MR::getHashCode takes it).
pub fn column_hash(name: &str) -> u32 {
    name.bytes().fold(0u32, |hash, byte| hash.wrapping_mul(31).wrapping_add(byte as i8 as u32))
}

/// Whether a patch's column name names a table's column: the same name, or the same hash (a name the store does not
/// know is `0x` and its hash; a patch may also write a column as its hash).
pub fn same_column(table_name: &str, patch_name: &str) -> bool {
    if table_name == patch_name {
        return true;
    }
    let hash_of = |n: &str| match n.strip_prefix("0x").filter(|h| h.len() == 8) {
        Some(hex) => u32::from_str_radix(hex, 16).ok(),
        None => Some(column_hash(n)),
    };
    hash_of(table_name).is_some() && hash_of(table_name) == hash_of(patch_name)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ColumnType {
    Int { min: i64, max: i64 },
    Float,
    Text,
}

/// The player's game files: which paths exist and how files with rows are keyed. The cooker's store index provides
/// it in the game; LumaLink's imported game provides it there.
pub trait Store {
    /// The path in the game's own case, when the game has it (`lower` is the path in lowercase).
    fn path(&self, lower: &str) -> Option<String>;
    /// A path's rows, when the file has rows.
    fn table(&self, path: &str) -> Option<Table>;
    /// Whether a file at a path the game does not have may be new (a kind the game finds by name).
    fn may_be_new(&self, path: &str) -> bool;
    /// The names directly in a folder of the store (`lower`, a store path in lowercase), each in the game's case and
    /// whether it is a folder: what the game lists to find its galaxies (`/StageData`, ScenarioDataParser). None when
    /// the store cannot list it or the path is no folder.
    fn folder(&self, _lower: &str) -> Option<Vec<(String, bool)>> {
        None
    }
    /// A table a mod brings whole as a new file, from its bytes, read as the store reads its own tables: the game's
    /// form, or a source form (`.json`). `path` is the game path it stands at (the mod's spelling, without a source
    /// ending). None when the store cannot read tables from bytes, or the bytes are no table.
    fn read_table(&self, _path: &str, _bytes: &[u8]) -> Option<Table> {
        None
    }
}

/// What the validator knows of the game it checks against.
pub struct Game<'a> {
    /// This Nebula release.
    pub nebula: semver::Version,
    /// The player's program (`RMGE01`...), absent in LumaLink.
    pub region: Option<&'a str>,
    /// Languages beyond the 14 that installed language mods add.
    pub extra_languages: &'a [String],
    /// The self-description: rows, fields, classes, services and their permissions, the LumaCore version.
    pub names: Option<&'a Names>,
    /// The renderer features this device's graphics have (`draw.native`'s capability query, start-check 3), absent in
    /// LumaLink: L806 reads them against a mod's `renderer.features`.
    pub device_features: Option<&'a [String]>,
    pub store: Option<&'a dyn Store>,
}

/// Another installed mod, for the rules that read the mod list (L8xx).
#[derive(Clone, Debug)]
pub struct Installed {
    pub id: String,
    pub version: semver::Version,
    pub on: bool,
    /// Its file name, to name it in messages.
    pub file: String,
    /// The ids it declares conflicts with.
    pub conflicts: Vec<String>,
    /// The ids its `needs` names, for a cycle of needs through the checked mod (L800).
    pub needs: Vec<String>,
    /// What it changes, from its own report (`Report::touches`), for the overlap Notes L305 and L404.
    pub touches: Touches,
}

/// What a mod changes in the game, so overlaps between mods can be named (L305, L404) without reading both again.
/// The game keeps each mod's with its stored report.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Touches {
    /// Game paths replaced whole, in lowercase.
    pub replaces: Vec<String>,
    /// Cells set: path, the key values and the column, joined by U+001F.
    pub cells: Vec<String>,
    /// Rows added and removed: path and the key values, joined by U+001F.
    pub adds: Vec<String>,
    pub removes: Vec<String>,
    /// The mod's own assets, by mod path.
    pub assets: Vec<String>,
    /// The game's functions its code replaces (LUMA_REPLACE): name and signature, joined by U+001F (L805).
    pub functions: Vec<String>,
    /// Keys added to a link group's tables (`link_group`): the group and the key values, joined by U+001F (L404).
    pub links: Vec<String>,
    /// The cells it gives the galaxies' scenario tables, by patch or in a new galaxy's own table (table-keys T-4):
    /// galaxy (lowercase), ScenarioNo, column and value, joined by U+001F. Another mod's check reads them for the
    /// layers that are on together.
    pub scenarios: Vec<String>,
    /// The services each platform's entry library imports without LUMA_OPTIONAL (kind 5): platform, service name and
    /// "major.minor", joined by U+001F, in import order, each once. The host's consent question reads which declared
    /// permissions are required from them (consent 1.3: a permissioned service's permission is its name).
    pub services: Vec<String>,
    /// The classes only code can make that a placement uses (references 3, G-2): class, file and line, joined by
    /// U+001F, one per placement. The host gives them to the module after the last mod's start (luma_refs.h), which
    /// looks each up in the factory. Filled by the placement rules of the stages' contract.
    pub after_start: Vec<String>,
}

/// The layers of a zone, and the folders whose tables the game links by key across them (contract table-keys 13).
pub const LINK_LAYERS: [&str; 17] = [
    "common", "layera", "layerb", "layerc", "layerd", "layere", "layerf", "layerg", "layerh", "layeri", "layerj", "layerk", "layerl", "layerm",
    "layern", "layero", "layerp",
];
const LINK_FOLDERS: [&str; 5] = ["placement", "mapparts", "childobj", "start", "generalpos"];
/// The kinds whose key the game finds only among the layers one scenario turns on (`start`: MarioNo; `generalpos`:
/// PosName and Obj_ID), not across the whole group as `l_id` (contract table-keys, T-4).
const SCENARIO_FOLDERS: [&str; 2] = ["start", "generalpos"];

/// A link group table's zone, folder, layer (0 common, 1 to 16 LayerA to LayerP) and file, in lowercase.
fn link_parts(path: &str) -> Option<(String, String, usize, String)> {
    let lower = path.to_lowercase();
    let mut parts: Vec<&str> = lower.split('/').collect();
    if parts.len() == 7 && parts[2].strip_suffix(".arc") == Some(parts[1]) {
        parts.remove(2);
    }
    if parts.len() != 6 || parts[0] != "stagedata" || parts[2] != "jmp" || !LINK_FOLDERS.contains(&parts[3]) {
        return None;
    }
    let layer = LINK_LAYERS.iter().position(|l| *l == parts[4])?;
    Some((parts[1].to_string(), parts[3].to_string(), layer, parts[5].to_string()))
}

/// A table's link group, in lowercase with the layer as `*` (`stagedata/astrogalaxy/jmp/placement/*/objinfo`): one
/// zone's tables of one kind, whose key (`l_id`, `MarioNo`, `PosName`+`Obj_ID`) the game finds across its layers.
/// None for a table in no group. `path` is a store path, or a disc path with the zone's archive (`<zone>/<zone>.arc/`).
pub fn link_group(path: &str) -> Option<String> {
    link_parts(path).map(|(zone, folder, _, file)| format!("stagedata/{zone}/jmp/{folder}/*/{file}"))
}

/// A link group table's layer: 0 for common, 1 to 16 for LayerA to LayerP.
pub fn link_layer(path: &str) -> Option<usize> {
    link_parts(path).map(|(_, _, layer, _)| layer)
}

/// Whether a link group's key is unique only among the layers a scenario turns on together (start, generalpos),
/// rather than across the whole group (placement, mapparts, childobj).
pub fn link_by_scenario(path: &str) -> bool {
    link_parts(path).is_some_and(|(_, folder, _, _)| SCENARIO_FOLDERS.contains(&folder.as_str()))
}

/// The layers one scenario of a galaxy turns on in a zone: bit 0 common (always on), bit n LayerA + n - 1, as
/// `ScenarioDataFunction::getCurrentScenarioLayers` makes them (the zone's column of the galaxy's scenario table, times
/// 2, with common).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LayerSet {
    /// The galaxy, in lowercase (`astrogalaxy`).
    pub galaxy: String,
    /// The row's `ScenarioNo`, as text.
    pub scenario: String,
    pub layers: u32,
}

/// A cell of a galaxy's scenario table that a mod gives (a patch's `set` or `add`, or a row of a new galaxy's own
/// table), read before the stage tables' patches (the game patches the scenario tables first).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ScenarioCell {
    /// The galaxy, in lowercase.
    pub galaxy: String,
    pub scenario: String,
    /// The column as the patch names it (a zone's name, or its hash as `0x` and 8 hex digits).
    pub column: String,
    pub value: i64,
    /// A row of a table the mod brings whole (the base: the highest mod's whole table wins), not a patch's cell
    /// (which edits whatever base there is). `scenario_order` (V4-1).
    pub whole: bool,
}

impl ScenarioCell {
    /// As `Touches::scenarios` keeps it: galaxy, ScenarioNo, column, value and `whole` or `patch`, joined by U+001F.
    pub fn kept(&self) -> String {
        let from = if self.whole { "whole" } else { "patch" };
        format!("{}\u{1f}{}\u{1f}{}\u{1f}{}\u{1f}{from}", self.galaxy, self.scenario, self.column, self.value)
    }
    /// From `Touches::scenarios`; None for a value that is not one.
    pub fn from_kept(text: &str) -> Option<ScenarioCell> {
        let mut parts = text.split('\u{1f}');
        let (galaxy, scenario, column, value) = (parts.next()?, parts.next()?, parts.next()?, parts.next()?.parse().ok()?);
        let whole = match parts.next()? {
            "whole" => true,
            "patch" => false,
            _ => return None,
        };
        parts.next().is_none().then(|| ScenarioCell { galaxy: galaxy.to_string(), scenario: scenario.to_string(), column: column.to_string(), value, whole })
    }
}

/// The scenario cells of the mods that are on, as the cook applies tables (V4-1, DECISIONS): `lists` holds each mod's
/// cells, the lowest in the list first. A galaxy's whole table is the base, the highest mod's that brings one; then
/// every mod's patch cells edit it, lowest first, whoever brought the table. Whole cells come first, each galaxy's
/// from one mod only, so `zone_layer_sets` (a later cell wins) gives the game's sets.
pub fn scenario_order(lists: Vec<Vec<ScenarioCell>>) -> Vec<ScenarioCell> {
    let mut out: Vec<ScenarioCell> = Vec::new();
    let mut taken: Vec<String> = Vec::new();
    for list in lists.iter().rev() {
        let mine: Vec<String> = list.iter().filter(|c| c.whole && !taken.contains(&c.galaxy)).map(|c| c.galaxy.clone()).collect();
        out.extend(list.iter().filter(|c| c.whole && mine.contains(&c.galaxy)).cloned());
        taken.extend(mine);
    }
    out.extend(lists.into_iter().flatten().filter(|c| !c.whole));
    out
}

/// The galaxy whose scenario table a store path is (`stagedata/<galaxy>scenario/scenariodata.bcsv`), in lowercase.
pub fn scenario_table_galaxy(store_path: &str) -> Option<String> {
    let lower = store_path.to_lowercase();
    let parts: Vec<&str> = lower.split('/').collect();
    if parts.len() != 3 || parts[0] != "stagedata" || parts[2] != "scenariodata.bcsv" {
        return None;
    }
    parts[1].strip_suffix("scenario").filter(|g| !g.is_empty()).map(str::to_string)
}

/// Every layer set the game can turn on in a zone (`zone` in the game's case: its column in a scenario table is named
/// by it): one per scenario row of each galaxy the store lists whose table has the zone's column, with the mods'
/// `cells` over the game's in order (a later cell wins), and one per row of a galaxy only the mods' cells give (a new
/// galaxy's own table). None when the store cannot list its galaxies or give a scenario table's cells.
pub fn zone_layer_sets(store: &dyn Store, zone: &str, cells: &[ScenarioCell]) -> Option<Vec<LayerSet>> {
    let bits = |mask: i64| ((mask as u32 & 0xffff) << 1) | 1;
    let mut out = Vec::new();
    let mut listed: Vec<String> = Vec::new();
    // As ScenarioDataParser: a folder whose name ends in "Scenario" (in that case) is a galaxy's.
    for (name, is_folder) in store.folder("stagedata")? {
        let Some(galaxy) = name.strip_suffix("Scenario").filter(|g| is_folder && !g.is_empty()) else { continue };
        let galaxy = galaxy.to_lowercase();
        listed.push(galaxy.clone());
        let Some(path) = store.path(&format!("stagedata/{}/scenariodata.bcsv", name.to_lowercase())) else { continue };
        let table = store.table(&path)?;
        let rows = table.cells.as_ref()?;
        let number = table.columns.iter().position(|(c, _)| same_column(c, "ScenarioNo"))?;
        let mut sets: Vec<(String, u32)> = Vec::new();
        if let Some(column) = table.columns.iter().position(|(c, _)| same_column(c, zone)) {
            for row in rows {
                let (Some(no), Some(mask)) = (row.get(number), row.get(column)) else { continue };
                sets.push((no.clone(), bits(mask.parse::<i64>().unwrap_or(0))));
            }
        }
        for c in cells.iter().filter(|c| c.galaxy == galaxy && same_column(zone, &c.column)) {
            match sets.iter_mut().find(|(no, _)| *no == c.scenario) {
                Some(set) => set.1 = bits(c.value),
                None => sets.push((c.scenario.clone(), bits(c.value))),
            }
        }
        out.extend(sets.into_iter().map(|(scenario, layers)| LayerSet { galaxy: galaxy.to_string(), scenario, layers }));
    }
    // A galaxy the store does not list: a new one, whose table a mod brings.
    for c in cells.iter().filter(|c| !listed.contains(&c.galaxy) && same_column(zone, &c.column)) {
        match out.iter_mut().find(|s| s.galaxy == c.galaxy && s.scenario == c.scenario) {
            Some(set) => set.layers = bits(c.value),
            None => out.push(LayerSet { galaxy: c.galaxy.clone(), scenario: c.scenario.clone(), layers: bits(c.value) }),
        }
    }
    Some(out)
}

/// Whether the game meets a key of the link group table at `layer` among the group's table at `other` (table-keys 13.3,
/// 13.6): always for `l_id` kinds (`by_scenario` false); for start and generalpos when either is common, both are one
/// layer, or some scenario turns both on (`sets`; None: the scenario tables are not known, so common and the same
/// layer only). The validator and the applier's cook both ask this.
pub fn layers_meet(by_scenario: bool, layer: usize, other: usize, sets: Option<&[LayerSet]>) -> bool {
    if !by_scenario || layer == 0 || other == 0 || other == layer {
        return true;
    }
    sets.is_some_and(|sets| sets.iter().any(|s| s.layers >> layer & 1 == 1 && s.layers >> other & 1 == 1))
}

/// The other tables of a table's link group, as store paths in lowercase (empty for a table in no group).
pub fn link_paths(path: &str) -> Vec<String> {
    let Some(group) = link_group(path) else { return Vec::new() };
    let own = path.to_lowercase();
    LINK_LAYERS.iter().map(|l| group.replacen("/*/", &format!("/{l}/"), 1)).filter(|p| *p != own).collect()
}

pub struct Input<'a> {
    pub source: Source<'a>,
    pub game: Option<&'a Game<'a>>,
    /// The other installed mods (LumaLink: those in its mod folder).
    pub installed: &'a [Installed],
    /// The platforms the mod is checked for: the game's own, or every platform LumaLink builds.
    pub platforms: &'a [&'a str],
    /// The player's option values; None means the defaults.
    pub options: Option<&'a BTreeMap<String, String>>,
    /// How many of `installed` (in list order, the top first) are above this mod; None: below all of them. The mod
    /// higher in the list wins a file, a cell and a replace (L305, L404, L805).
    pub position: Option<usize>,
}

/// Validates a mod: every rule, every finding, in one pass. Never panics.
pub fn validate(input: &Input) -> Report {
    finish(rules::run(input, None))
}

/// The check at each start (validator contract 2): when `saved` is this `.luma`'s kept report, made by this validator
/// against the game `game` identifies, the rules that read the whole `.luma` are not run again and their findings are
/// taken from it; every other rule runs. Otherwise every rule runs, as `validate`. Never panics.
pub fn recheck(input: &Input, saved: Option<&Saved>, game: &str) -> Report {
    finish(rules::run(input, saved.map(|s| (s, game))))
}

fn finish(mut report: Report) -> Report {
    report.findings.sort();
    report.findings.dedup();
    report.not_checked.sort();
    report.not_checked.dedup();
    for list in [&mut report.touches.replaces, &mut report.touches.cells, &mut report.touches.adds, &mut report.touches.removes, &mut report.touches.assets, &mut report.touches.functions, &mut report.touches.links] {
        list.sort();
        list.dedup();
    }
    report
}

/// The finding's message in one of the game's languages (an unknown language gives `UsEnglish`).
pub fn message(f: &Finding, language: &str) -> String {
    let lang = LANGUAGES.iter().position(|&l| l == language).unwrap_or(0);
    fill(&text::get(&format!("L{}", f.code), lang), f, lang, 0)
}

/// Why the finding was made, without where (its `why` or `found` phrase, filled with its parameters), in one of the
/// game's languages: what a screen that names the mod and the file itself puts after them (the Mods screen's
/// Mods.Patch.Failed, F INBOX-G 18). A finding with neither phrase gives its whole message.
pub fn reason(f: &Finding, language: &str) -> String {
    let lang = LANGUAGES.iter().position(|&l| l == language).unwrap_or(0);
    match f.args.iter().find(|(n, _)| *n == "why" || *n == "found") {
        Some((_, Arg::Phrase(id))) => fill(&text::get(id, lang), f, lang, 1),
        _ => message(f, language),
    }
}

/// A finding's own parameters as the facts file and the C functions carry them, in its order: `name=T:<text>` for a
/// literal, `name=P:<phrase id>` for a phrase (the kept report's form). The place (`mod`, `file`, `line`, `column`)
/// is not among them; whoever says the message gives it.
pub fn args_text(f: &Finding) -> Vec<String> {
    f.args
        .iter()
        .map(|(name, arg)| match arg {
            Arg::Text(t) => format!("{name}=T:{t}"),
            Arg::Phrase(id) => format!("{name}=P:{id}"),
        })
        .collect()
}

/// A parameter given by value: `T:<text>` a literal, `P:<phrase id>` a phrase, anything else a literal as written.
fn given(value: &str) -> Val {
    match (value.strip_prefix("T:"), value.strip_prefix("P:")) {
        (Some(t), _) => Val::Text(t.to_string()),
        (_, Some(id)) => Val::Phrase(id.to_string()),
        _ => Val::Text(value.to_string()),
    }
}

/// The message of rule `code` with its parameters by name, values as `args_text` writes them, in one of the game's
/// languages (an unknown one gives `UsEnglish`): what the loader and the cook refuse with, said as the validator says
/// its own findings (loader contract 3). The place is given by the names `mod`, `file`, `line` and `column`. A name
/// the template has and the parameters lack stays in the text as `{name}`, so a missing one is seen, never dropped.
pub fn message_of(code: u16, args: &[(&str, &str)], language: &str) -> String {
    let lang = LANGUAGES.iter().position(|&l| l == language).unwrap_or(0);
    let look = |name: &str| args.iter().find(|(n, _)| *n == name).map(|(_, v)| given(v));
    fill_by(&text::get(&format!("L{code}"), lang), &look, lang, 0)
}

/// As `reason` for a finding: the `why` or `found` phrase filled with the parameters, else the whole message.
pub fn reason_of(code: u16, args: &[(&str, &str)], language: &str) -> String {
    let lang = LANGUAGES.iter().position(|&l| l == language).unwrap_or(0);
    let look = |name: &str| args.iter().find(|(n, _)| *n == name).map(|(_, v)| given(v));
    // The first `why` or `found`, as `reason` takes it.
    match args.iter().find(|(n, _)| *n == "why" || *n == "found").map(|(_, v)| given(v)) {
        Some(Val::Phrase(id)) => fill_by(&text::get(&id, lang), &look, lang, 1),
        _ => message_of(code, args, language),
    }
}

/// The words for a phrase id in one language (for reasons of not-checked rules, and screens that list them).
pub fn phrase(id: &str, language: &str) -> String {
    let lang = LANGUAGES.iter().position(|&l| l == language).unwrap_or(0);
    text::get(id, lang)
}

/// A template parameter's value: a literal, or a phrase id (itself a template).
enum Val {
    Text(String),
    Phrase(String),
}

fn fill(template: &str, f: &Finding, lang: usize, depth: u32) -> String {
    // The finding's own arguments first (a table's {column} in L403, L404, L406), then the place.
    let look = |name: &str| match f.args.iter().find(|(n, _)| *n == name) {
        Some((_, Arg::Text(t))) => Some(Val::Text(t.clone())),
        Some((_, Arg::Phrase(id))) => Some(Val::Phrase(id.to_string())),
        None => match name {
            "mod" => Some(Val::Text(f.mod_id.clone().unwrap_or_else(|| f.file.clone()))),
            "file" => Some(Val::Text(f.file.clone())),
            "line" => f.line.map(|l| Val::Text(l.to_string())),
            "column" => f.column.map(|c| Val::Text(c.to_string())),
            _ => None,
        },
    };
    fill_by(template, &look, lang, depth)
}

fn fill_by(template: &str, look: &dyn Fn(&str) -> Option<Val>, lang: usize, depth: u32) -> String {
    let mut out = String::with_capacity(template.len() + 32);
    let mut rest = template;
    while let Some(open) = rest.find('{') {
        out.push_str(&rest[..open]);
        let after = &rest[open + 1..];
        let Some(close) = after.find('}') else {
            out.push_str(&rest[open..]);
            return out;
        };
        let name = &after[..close];
        let value = match look(name) {
            Some(Val::Text(t)) => Some(t),
            Some(Val::Phrase(id)) if depth < 2 => Some(fill_by(&text::get(&id, lang), look, lang, depth + 1)),
            Some(Val::Phrase(id)) => Some(id),
            None => None,
        };
        match value {
            // An empty value takes the space before it with it ("{other} {expected}: ..." with no version range).
            Some(v) if v.is_empty() => {
                if out.ends_with(' ') {
                    out.pop();
                }
            }
            Some(v) => out.push_str(&v),
            None => {
                out.push('{');
                out.push_str(name);
                out.push('}');
            }
        }
        rest = &after[close + 1..];
    }
    out.push_str(rest);
    out
}

/// The level of each code (validator contract 5): the Notes, everything else an Error.
pub fn level_of(code: u16) -> Level {
    match code {
        208 | 218 | 219 | 305 | 404 | 407 | 507 | 510 => Level::Note,
        _ => Level::Error,
    }
}

/// Every code this validator can report, for the suite's "one broken mod per code" check.
pub const CODES: &[u16] = &[
    100, 101, 102, 103, 104, 105, 106, 107, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213,
    214, 215, 216, 217, 218, 219, 300, 301, 302, 303, 304, 305, 306, 307, 400, 401, 402, 403, 404, 405, 406, 407, 500, 501,
    502, 503, 504, 505, 506, 507, 508, 509, 510, 511, 512, 700, 701, 702, 800, 801, 802, 803, 804, 805, 806,
];
