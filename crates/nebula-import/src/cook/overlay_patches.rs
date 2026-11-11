//! Patches by key in the mods' store (plan 3.12, contract table-keys 9, 12, 13): the plan's `%mods` line gives every
//! mod whose content is used, from the bottom of the list up, and each `%patch` line one patch file
//! (`mod<TAB>entry in the mod<TAB>game path<TAB>file`), bottom of the list first, then layer and file order. Each
//! patched table starts from what the game would read without the patches (a mod's file of it in this store, else the
//! game's) and is written whole into the store with every patch applied by the applier (cook::patch), the scenario
//! tables first, so a start or generalpos table meets its key among the layers the patched scenario tables turn on
//! together (13.6). What the applier says goes in the facts, one line each in `<store>.facts`.

use super::patch::{apply_linked, unreadable, Fact, Link, ModPatch};
use super::table::{TableIr, MAGIC};
use super::{place, store, Stats, StoreItem};
use crate::store_view::{table_view, PakStore};
use anyhow::{bail, Context, Result};
use luma_validate::{Store, Table};
use std::collections::BTreeMap;
use std::fs::{self, File};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

/// A patch read: its mod, its file in the mod, its JSON.
type OwnedPatch = (String, String, serde_json::Value);

/// One `%patch` line.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PatchLine {
    pub mod_id: String,
    /// The patch file inside the mod (`layers/base/patches/<game path>.json`), to name it.
    pub entry: String,
    /// The game path it patches, as the mod writes it.
    pub path: String,
    /// The file on this machine.
    pub source: PathBuf,
}

/// The plan's patches: the mod order, bottom up, and each patch in the order it applies.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct PlanPatches {
    pub mods: Vec<String>,
    pub patches: Vec<PatchLine>,
}

/// The plan's `%mods` and `%patch` lines. Any other `%` line is a plan of another cooker, refused by name; a patch of a
/// mod `%mods` does not list is refused (its order is not known).
pub fn read_patches(text: &str) -> Result<PlanPatches> {
    let mut out = PlanPatches::default();
    let mut listed = false;
    for line in text.lines().skip(1).filter(|l| l.starts_with('%')) {
        let fields: Vec<&str> = line.split('\t').collect();
        match fields[0] {
            "%mods" => {
                if listed {
                    bail!("the plan has two %mods lines");
                }
                listed = true;
                out.mods = fields[1..].iter().map(|s| s.to_string()).collect();
            }
            "%patch" => {
                let [_, mod_id, entry, path, source] = fields[..] else {
                    bail!("the plan's line {line:?}: %patch takes mod, entry, game path and file");
                };
                if [mod_id, entry, path, source].iter().any(|f| f.is_empty()) {
                    bail!("the plan's line {line:?}: a field is empty");
                }
                out.patches.push(PatchLine {
                    mod_id: mod_id.to_string(),
                    entry: entry.to_string(),
                    path: path.trim_start_matches('/').to_string(),
                    source: PathBuf::from(source),
                });
            }
            other => bail!("the plan's line {line:?}: {other} is no line this cooker knows (a plan of another version)"),
        }
    }
    if let Some(p) = out.patches.iter().find(|p| !out.mods.contains(&p.mod_id)) {
        bail!("the plan patches {} for {}, which its %mods line does not list", p.path, p.mod_id);
    }
    Ok(out)
}

/// The game's tables as the store being written will give them: a table this cook has patched, else a mod's file in
/// this store, else the game's. Folders list both stores. For the layer sets of 13.6, and the tables patches start
/// from.
struct CookView<'a> {
    base: Option<&'a PakStore>,
    temporary: &'a Path,
    placed: &'a [store::Placed],
    /// Tables patched so far (the scenario tables), by lowercase store path: the store path in the game's case and the
    /// table.
    patched: BTreeMap<String, (String, TableIr)>,
}

impl CookView<'_> {
    fn mine(&self, path: &str) -> Option<&store::Placed> {
        self.placed.iter().find(|p| !p.folder && store::same_path(&p.path, path))
    }

    /// The table a path holds before this cook's patches of it; Err names why it cannot be read.
    fn original(&self, path: &str) -> Option<Result<TableIr>> {
        let bytes = if let Some(mine) = self.mine(path) {
            let read = || -> Result<Vec<u8>> {
                let mut file = File::open(self.temporary)?;
                file.seek(SeekFrom::Start(mine.position))?;
                let mut bytes = vec![0u8; usize::try_from(mine.size)?];
                file.read_exact(&mut bytes)?;
                Ok(bytes)
            };
            match read() {
                Ok(bytes) => bytes,
                Err(e) => return Some(Err(e.context("the mods' store being written cannot be read"))),
            }
        } else {
            let base = self.base?;
            let game_path = base.path(&path.to_ascii_lowercase())?;
            match base.table_ir(&game_path) {
                Ok(Some(ir)) => return Some(Ok(ir)),
                Ok(None) => return None,
                Err(e) => return Some(Err(e)),
            }
        };
        if !bytes.starts_with(MAGIC) {
            return Some(Err(anyhow::anyhow!("the file a mod gives for it is no table")));
        }
        Some(TableIr::from_ntbl(&bytes).context("the table a mod gives for it"))
    }
}

impl Store for CookView<'_> {
    fn path(&self, lower: &str) -> Option<String> {
        let key = store::store_path(lower.trim_start_matches('/')).to_ascii_lowercase();
        if let Some((cased, _)) = self.patched.get(&key) {
            return Some(cased.clone());
        }
        if let Some(mine) = self.mine(lower) {
            return Some(store::store_path(&mine.path));
        }
        self.base?.path(lower)
    }
    fn table(&self, path: &str) -> Option<Table> {
        let at = store::store_path(path.trim_start_matches('/'));
        if let Some((cased, ir)) = self.patched.get(&at.to_ascii_lowercase()) {
            return Some(table_view(cased, ir));
        }
        self.original(&at)?.ok().map(|ir| table_view(&at, &ir))
    }
    fn may_be_new(&self, _path: &str) -> bool {
        false
    }
    fn read_table(&self, path: &str, bytes: &[u8]) -> Option<Table> {
        crate::store_view::view_of_bytes(path, bytes)
    }
    fn folder(&self, lower: &str) -> Option<Vec<(String, bool)>> {
        let folder = store::store_path(lower.trim_start_matches('/')).trim_end_matches('/').to_ascii_lowercase();
        let mut names: BTreeMap<String, (String, bool)> = BTreeMap::new();
        let mut any = false;
        if let Some(listed) = self.base.and_then(|b| b.folder(&folder)) {
            any = true;
            for (name, is_folder) in listed {
                names.insert(name.to_ascii_lowercase(), (name, is_folder));
            }
        }
        // The mods' files: a name under the folder is in the listing as the game's asset store merges them.
        let prefix = format!("{folder}/");
        for p in self.placed {
            let at = store::store_path(&p.path);
            let Some(rest) = at.to_ascii_lowercase().strip_prefix(&prefix).map(str::to_string) else { continue };
            any = true;
            let cased = &at[prefix.len()..];
            let (first, deeper) = match cased.split_once('/') {
                Some((first, _)) => (first, true),
                None => (cased, p.folder),
            };
            let key = rest.split('/').next().unwrap_or_default().to_string();
            let slot = names.entry(key).or_insert_with(|| (first.to_string(), false));
            slot.1 |= deeper;
        }
        any.then(|| names.into_values().collect())
    }
}

/// The path to place a patched table under: inside its archive (`StageData/AstroGalaxy.arc/jmp/...`), so the store
/// marks the archive and the rest of it comes from the game (overlay::complete_archives), as for a mod's file in one.
fn placement_path(store_path: &str, base: Option<&PakStore>, placed: &[store::Placed]) -> String {
    let lower = store_path.to_ascii_lowercase();
    // An archive this store already has a member of, as the mod named it.
    for p in placed {
        let path_lower = p.path.to_ascii_lowercase();
        let Some(at) = path_lower.find(".arc/") else { continue };
        let archive = &p.path[..at + 4];
        let folder = store::store_path(archive).to_ascii_lowercase();
        if lower.starts_with(&format!("{folder}/")) {
            return format!("{archive}{}", &store_path[folder.len()..]);
        }
    }
    match base.and_then(|b| b.archive_of(store_path)) {
        Some(folder) => format!("{folder}.arc{}", &store_path[folder.len()..]),
        None => store_path.to_string(),
    }
}

/// Applies the plan's patches into the store being written. A table no store holds, or a patch file that cannot be
/// read from this machine, is named in `failed` (the `.left_out` file); every applier fact goes in `facts`.
#[allow(clippy::too_many_arguments)]
pub fn apply_patches(plan: &PlanPatches, base: Option<&Path>, pak: &mut File, temporary: &Path, at: &mut u64, placed: &mut Vec<store::Placed>,
                     failed: &mut Vec<(String, String)>, facts: &mut Vec<Fact>, stats: &mut Stats) -> Result<()> {
    if plan.patches.is_empty() {
        return Ok(());
    }
    pak.flush()?;
    let base_store = match base {
        Some(path) => Some(PakStore::open(path).with_context(|| format!("the game's store {}", path.display()))?),
        None => None,
    };
    // Each patched table once, by its place in the store, with its patches in the plan's order.
    let mut tables: Vec<(String, Vec<&PatchLine>)> = Vec::new();
    for p in &plan.patches {
        let key = store::store_path(&p.path).to_ascii_lowercase();
        match tables.iter_mut().find(|(k, _)| *k == key) {
            Some((_, list)) => list.push(p),
            None => tables.push((key, vec![p])),
        }
    }
    // The scenario tables first: the layers they turn on decide the groups of start and generalpos (13.6).
    tables.sort_by_key(|(key, _)| luma_validate::scenario_table_galaxy(key).is_none());
    // Each patch file read once; one that is not JSON is a fact of its table.
    let mut json: BTreeMap<PathBuf, Option<serde_json::Value>> = BTreeMap::new();
    let mut view = CookView { base: base_store.as_ref(), temporary, placed: placed.as_slice(), patched: BTreeMap::new() };
    for p in &plan.patches {
        if json.contains_key(&p.source) {
            continue;
        }
        match fs::read(&p.source) {
            Ok(bytes) => {
                let value = serde_json::from_slice(&bytes).ok();
                if value.is_none() {
                    // Named as the store holds the table, as every other fact (C-2); the mod's case when no store has it.
                    let path = store::store_path(&p.path);
                    let table = view.path(&path.to_ascii_lowercase()).unwrap_or(path);
                    facts.push(unreadable(&table, &p.mod_id, &p.entry, &bytes));
                }
                json.insert(p.source.clone(), value);
            }
            Err(e) => {
                failed.push((format!("patch/{}/{}", p.mod_id, p.entry), format!("{} cannot be read: {e}", p.source.display())));
                json.insert(p.source.clone(), None);
            }
        }
    }
    let patches_of = |list: &[&PatchLine]| -> Vec<OwnedPatch> {
        list.iter().filter_map(|p| Some((p.mod_id.clone(), p.entry.clone(), json.get(&p.source)?.clone()?))).collect()
    };
    let mods: Vec<&str> = plan.mods.iter().map(String::as_str).collect();
    let mut layer_sets: BTreeMap<String, Option<Vec<luma_validate::LayerSet>>> = BTreeMap::new();
    let mut done: Vec<(String, Vec<u8>)> = Vec::new();
    for (key, list) in &tables {
        let cased = view.path(key).unwrap_or_else(|| store::store_path(&list[0].path));
        let name = |p: &PatchLine| format!("patch/{}/{}", p.mod_id, p.entry);
        let mut table = match view.original(&cased) {
            Some(Ok(table)) => table,
            Some(Err(e)) => {
                for p in list {
                    failed.push((name(p), format!("{cased}: {e:#}")));
                }
                continue;
            }
            None => {
                for p in list {
                    failed.push((name(p), format!("the game has no table {cased}, and no mod brings one")));
                }
                continue;
            }
        };
        let owned = patches_of(list);
        let patches: Vec<ModPatch> = owned.iter().map(|(m, f, v)| ModPatch { mod_id: m, file: f, patch: v }).collect();
        // The other tables of its link group the game meets its key among (13.3, 13.6), each with its own patches.
        let mut link_owned: Vec<(String, TableIr, Vec<OwnedPatch>)> = Vec::new();
        if luma_validate::link_group(&cased).is_some() {
            let layer = luma_validate::link_layer(&cased).unwrap_or(0);
            let by_scenario = luma_validate::link_by_scenario(&cased);
            let sets = if by_scenario {
                let zone = cased.split('/').nth(1).unwrap_or_default().to_string();
                layer_sets
                    .entry(zone.to_ascii_lowercase())
                    .or_insert_with(|| luma_validate::zone_layer_sets(&view, &zone, &[]))
                    .clone()
            } else {
                None
            };
            for other in luma_validate::link_paths(&cased) {
                if !luma_validate::layers_meet(by_scenario, layer, luma_validate::link_layer(&other).unwrap_or(0), sets.as_deref()) {
                    continue;
                }
                let Some(other_cased) = view.path(&other) else { continue };
                let Some(Ok(other_table)) = view.original(&other_cased) else { continue };
                let other_patches = tables.iter().find(|(k, _)| *k == other).map(|(_, l)| patches_of(l)).unwrap_or_default();
                link_owned.push((other_cased, other_table, other_patches));
            }
        }
        let link_patches: Vec<Vec<ModPatch>> =
            link_owned.iter().map(|(_, _, ps)| ps.iter().map(|(m, f, v)| ModPatch { mod_id: m, file: f, patch: v }).collect()).collect();
        let links: Vec<Link> = link_owned.iter().zip(&link_patches).map(|((p, t, _), ps)| Link { path: p, table: t, patches: ps }).collect();
        match apply_linked(&cased, &mut table, &patches, &links, &mods) {
            Ok(said) => facts.extend(said),
            Err(e) => {
                // The game's table is one its rule cannot read: no patch of it applies, each named.
                for p in list {
                    failed.push((name(p), format!("{cased}: {e:#}")));
                }
                continue;
            }
        }
        if luma_validate::scenario_table_galaxy(key).is_some() {
            view.patched.insert(key.clone(), (cased.clone(), table.clone()));
        }
        done.push((cased, table.to_ntbl()));
    }
    drop(view);
    for (cased, bytes) in done {
        let path = placement_path(&cased, base_store.as_ref(), placed);
        placed.retain(|p| !store::same_path(&p.path, &path));
        placed.extend(place(pak, at, &path, vec![StoreItem { path: String::new(), order: 0, bytes: Some(bytes) }], stats)?);
    }
    Ok(())
}

/// A field of a facts line: tab, line ends and backslash written as `\t`, `\n`, `\r`, `\\`.
fn field(text: &str) -> String {
    text.replace('\\', "\\\\").replace('\t', "\\t").replace('\n', "\\n").replace('\r', "\\r")
}

/// The facts file's text: one fact a line, `kind table mod file other row column selector rows code why`,
/// tab-separated, a field empty where the fact has none (luma_patch.h's LumaPatchFact, with the table). Kinds:
/// conflict (mod is the higher, used; other the lower), missing (other removed the row, or empty), selected, failed
/// (code is the validator's rule, why in English for the log). A `failed` line goes on with the refusal's parameters,
/// one a field, `name=T:<text>` or `name=P:<phrase id>` (audit 3.12 C-1): with code, mod and file they are what
/// luma_validate_reason says in the player's language.
pub fn facts_text(facts: &[Fact]) -> String {
    let mut out = String::new();
    for fact in facts {
        let line: Vec<String> = match fact {
            Fact::Conflict { table, row, column, lower, higher } => vec![
                "conflict".into(),
                table.clone(),
                higher.clone(),
                String::new(),
                lower.clone(),
                row.clone(),
                column.clone().unwrap_or_default(),
                String::new(),
                String::new(),
                String::new(),
                String::new(),
            ],
            Fact::Missing { table, row, mod_id, file, removed_by } => vec![
                "missing".into(),
                table.clone(),
                mod_id.clone(),
                file.clone(),
                removed_by.clone().unwrap_or_default(),
                row.clone(),
                String::new(),
                String::new(),
                String::new(),
                String::new(),
                String::new(),
            ],
            Fact::Selected { table, mod_id, file, selector, rows } => vec![
                "selected".into(),
                table.clone(),
                mod_id.clone(),
                file.clone(),
                String::new(),
                String::new(),
                String::new(),
                selector.to_string(),
                rows.to_string(),
                String::new(),
                String::new(),
            ],
            Fact::Failed { table, mod_id, file, refusal, why } => vec![
                "failed".into(),
                table.clone(),
                mod_id.clone(),
                file.clone(),
                String::new(),
                String::new(),
                String::new(),
                String::new(),
                String::new(),
                refusal.code.to_string(),
                why.clone(),
            ]
            .into_iter()
            .chain(luma_validate::args_text(refusal))
            .collect(),
        };
        out.push_str(&line.iter().map(|f| field(f)).collect::<Vec<_>>().join("\t"));
        out.push('\n');
    }
    out
}

#[cfg(test)]
mod tests {
    use super::super::overlay::cook_plan;
    use super::*;

    const SHADOW: &str = "ObjectData/Things/Shadow.bcsv";
    const SCENARIO: &str = "StageData/TestGalaxy/TestGalaxyScenario.arc/ScenarioData.bcsv";
    fn start(layer: &str) -> String {
        format!("StageData/Zone/Zone.arc/jmp/Start/{layer}/StartInfo")
    }

    /// A game store: a plain table, a galaxy's scenario table (scenario 1 turns LayerA and LayerB on in Zone), and the
    /// zone's start tables in its archive (Common 9, LayerA 0, LayerB 5).
    fn game(dir: &Path) -> PathBuf {
        let table = |json: &str| TableIr::from_json(json.as_bytes()).unwrap().to_ntbl();
        let files = [
            (SHADOW.to_string(), table(r#"{"rows":[{"Name":"Game","count":1}]}"#)),
            (SCENARIO.to_string(), table(r#"{"rows":[{"ScenarioNo":1,"Zone":3}]}"#)),
            (start("Common"), table(r#"{"rows":[{"MarioNo":9}]}"#)),
            (start("LayerA"), table(r#"{"rows":[{"MarioNo":0}]}"#)),
            (start("LayerB"), table(r#"{"rows":[{"MarioNo":5}]}"#)),
        ];
        let base = dir.join("game.pak");
        let mut pak = File::create(&base).unwrap();
        pak.write_all(&[0u8; store::HEADER as usize]).unwrap();
        let mut placed = Vec::new();
        for (path, bytes) in &files {
            let position = pak.stream_position().unwrap();
            pak.write_all(bytes).unwrap();
            placed.push(store::Placed { path: path.clone(), position, size: bytes.len() as u64, folder: false });
        }
        let end = pak.stream_position().unwrap();
        let end = store::write_index(&mut pak, end, &placed, b"RMGE01\0\0").unwrap();
        pak.set_len(end).unwrap();
        base
    }

    fn rows(store_file: &Path, path: &str) -> Vec<Vec<String>> {
        let ir = TableIr::from_ntbl(&store::read_file(store_file, path).unwrap()).unwrap();
        ir.rows.iter().map(|row| ir.columns.iter().zip(row).map(|(c, cell)| super::super::patch::cell_text(c, cell)).collect()).collect()
    }

    /// Writes each patch file and the plan; cooks it; gives the facts file's lines and the left-out list.
    fn cook(dir: &Path, base: &Path, patches: &[(&str, &str, &str)]) -> (PathBuf, Vec<String>, String) {
        let mut plan = format!("RMGE01\nbase\t{}\n%mods\tlow\ttop\n", base.display());
        for (i, (mod_id, path, json)) in patches.iter().enumerate() {
            let file = dir.join(format!("patch{i}.json"));
            fs::write(&file, json).unwrap();
            plan.push_str(&format!("%patch\t{mod_id}\tlayers/base/patches/{path}.json\t{path}\t{}\n", file.display()));
        }
        let plan_file = dir.join("mods.plan");
        fs::write(&plan_file, plan).unwrap();
        let out = dir.join("mods.pak");
        let (_, failed) = cook_plan(&plan_file, &out).unwrap();
        let facts = fs::read_to_string(out.with_extension("facts")).unwrap();
        let left_out = failed.iter().map(|(p, w)| format!("{p}: {w}")).collect::<Vec<_>>().join("\n");
        (out, facts.lines().map(str::to_string).collect(), left_out)
    }

    #[test]
    fn patches_apply_in_the_cook_with_their_facts() {
        let dir = tempfile::tempdir().unwrap();
        let base = game(dir.path());
        fs::write(dir.path().join("mods.facts"), "an older cook's fact\n").unwrap();
        let add0 = r#"{"patch": 1, "key": ["MarioNo"], "add": [{"MarioNo": 0}]}"#;
        let (out, facts, left_out) = cook(
            dir.path(),
            &base,
            &[
                ("low", SHADOW, r#"{"patch": 1, "key": ["Name"], "set": [{"where": {"Name": "Game"}, "values": {"count": 2}}]}"#),
                ("low", &start("LayerB"), add0),
                ("low", "ObjectData/Nothing/Shadow.bcsv", r#"{"patch": 1, "key": ["Name"], "add": [{"Name": "X"}]}"#),
                ("top", SHADOW, r#"{"patch": 1, "key": ["Name"], "set": [{"where": {"Name": "Game"}, "values": {"count": 3}}]}"#),
                ("top", SHADOW, r#"{"patch": 1, // a comment"#),
            ],
        );
        // The higher mod's cell stands, and the conflict is a fact; the older cook's facts are gone.
        assert_eq!(rows(&out, SHADOW), vec![vec!["Game".to_string(), "3".into()]]);
        assert!(!facts.iter().any(|f| f.contains("older")), "{facts:?}");
        // The column is named as the higher mod's patch wrote it (audits/3.11.md P-2).
        let conflict = "conflict\tObjectData/Things/Shadow.bcsv\ttop\t\tlow\tGame\tcount\t";
        assert!(facts.iter().any(|f| f.starts_with(&conflict)), "{facts:?}");
        // MarioNo 0 is in LayerA, which scenario 1 turns on with LayerB: that patch is left out (L402), as the
        // validator says.
        assert!(
            facts.iter().any(|f| f.starts_with("failed\tStageData/Zone/jmp/Start/LayerB/StartInfo\tlow\tlayers/base/patches/") && f.contains("\t402\t")),
            "{facts:?}"
        );
        // Its parameters follow (C-1), and the validator says it from them in every language.
        let failed = facts.iter().find(|f| f.starts_with("failed\tStageData/Zone/jmp/Start/LayerB/")).unwrap();
        let fields: Vec<&str> = failed.split('\t').collect();
        assert_eq!(fields[11], "why=P:Patch.KeyInGroup", "{failed}");
        let args: Vec<(&str, &str)> = fields[11..].iter().filter_map(|a| a.split_once('=')).collect();
        assert_eq!(luma_validate::reason_of(402, &args, "UsEnglish"), fields[10], "{failed}");
        for language in luma_validate::LANGUAGES {
            assert!(!luma_validate::reason_of(402, &args, language).contains('{'), "{language}: {failed}");
        }
        assert_eq!(rows(&out, &start("LayerB")), vec![vec!["5".to_string()]]);
        // Not JSON: L400, named by its file.
        assert!(facts.iter().any(|f| f.starts_with("failed\tObjectData/Things/Shadow.bcsv\ttop\t") && f.contains("\t400\t") && f.contains("\tfound=P:Json.")),
                "{facts:?}");
        // A table no store has: in the left-out list, by mod and file.
        assert!(left_out.contains("patch/low/layers/base/patches/ObjectData/Nothing/Shadow.bcsv.json: the game has no table"), "{left_out}");
        // The zone's archive is whole in the store: its other tables come from the game.
        assert_eq!(rows(&out, &start("Common")), vec![vec!["9".to_string()]]);
        assert_eq!(rows(&out, &start("LayerA")), vec![vec!["0".to_string()]]);

        // The same add, with a mod's scenario patch that turns LayerA on alone: LayerB never meets LayerA (13.6), so
        // the key is new among the tables the game meets it in.
        let (out, facts, _) = cook(
            dir.path(),
            &base,
            &[
                ("low", SCENARIO, r#"{"patch": 1, "key": ["ScenarioNo"], "set": [{"where": {"ScenarioNo": 1}, "values": {"Zone": 1}}]}"#),
                ("low", &start("LayerB"), add0),
            ],
        );
        assert!(!facts.iter().any(|f| f.starts_with("failed")), "{facts:?}");
        assert_eq!(rows(&out, &start("LayerB")), vec![vec!["5".to_string()], vec!["0".into()]]);
        assert_eq!(rows(&out, SCENARIO), vec![vec!["1".to_string(), "1".into()]]);

        // Facts only where there are some: a set by key that matches is no fact.
        let (_, facts, _) = cook(dir.path(), &base, &[("low", SHADOW, r#"{"patch": 1, "key": ["Name"], "set": [{"where": {"Name": "Game"}, "values": {"count": 4}}]}"#)]);
        assert!(facts.iter().all(|f| f.starts_with("selected")), "{facts:?}");
    }

    #[test]
    fn a_plan_s_patch_lines_are_read_or_refused_by_name() {
        let plan = "RMGE01\n%mods\ta\tb\n%patch\tb\tlayers/x/patches/T.bcsv.json\t/T.bcsv\tC:/m/t.json\n";
        let got = read_patches(plan).unwrap();
        assert_eq!(got.mods, vec!["a".to_string(), "b".into()]);
        assert_eq!(got.patches[0].path, "T.bcsv");
        assert!(read_patches("RMGE01\n%mods\ta\n%patch\tb\te\tT\tf\n").unwrap_err().to_string().contains("does not list"));
        assert!(read_patches("RMGE01\n%mods\tb\n%patch\tb\te\tT\n").unwrap_err().to_string().contains("%patch takes"));
        assert!(read_patches("RMGE01\n%order\tb\n").unwrap_err().to_string().contains("another version"));
        assert_eq!(read_patches("RMGE01\nA\tB\n").unwrap(), PlanPatches::default());
        // The file lines leave them out.
        assert_eq!(super::super::overlay::read_plan(plan).unwrap().1.len(), 0);
    }

    #[test]
    fn a_fact_line_keeps_its_fields_whatever_they_hold() {
        let line = facts_text(&[Fact::Conflict { table: "T".into(), row: "a\tb".into(), column: None, lower: "l".into(), higher: "h\\".into() }]);
        assert_eq!(line, "conflict\tT\th\\\\\t\tl\ta\\tb\t\t\t\t\t\n");
    }
}
