//! A cook's refusals and notes as the validator names them (models contract 9, G-M15): L804 with a `why` phrase
//! (`Cook.Model.<Kind>`, rows `Validator.Cook.*` in localization/NebulaText.tsv) and the phrase's arguments, so LumaLink
//! and the Mods screen say them in the player's language. A refusal is an error that stops the file's cook (never a
//! half model); a note is said and the cook goes on. Notes are gathered for the file being cooked, on its thread.

use super::patch::Refusal;
use luma_validate::{Arg, Finding, Level};
use std::cell::RefCell;

/// The validator's rule for a file the game expects to cook that did not cook.
pub const COOK_FAILED: u16 = 804;

fn finding(why: &'static str, level: Level, args: &[(&'static str, String)]) -> Finding {
    let mut all = vec![("why", Arg::Phrase(why))];
    all.extend(args.iter().map(|(name, value)| (*name, Arg::Text(value.clone()))));
    Finding { code: COOK_FAILED, level, mod_id: None, file: String::new(), line: None, column: None, pointer: None, args: all }
}

/// The file is refused: `why` is the phrase (`Cook.Model.MissingJoint`), `args` its arguments by name.
pub fn refuse(why: &'static str, args: &[(&'static str, String)]) -> anyhow::Error {
    anyhow::Error::new(Refusal(finding(why, Level::Error, args)))
}

/// As `refuse`, with arguments that may be phrases of their own (`expected` = `Cook.Effect.Is.Number`).
pub fn refuse_with(why: &'static str, args: Vec<(&'static str, Arg)>) -> anyhow::Error {
    let mut all = vec![("why", Arg::Phrase(why))];
    all.extend(args);
    anyhow::Error::new(Refusal(Finding { code: COOK_FAILED, level: Level::Error, mod_id: None, file: String::new(), line: None, column: None, pointer: None, args: all }))
}

/// The file breaks glTF 2.0 at `pointer` (`/accessors/3/count`): missing, or not what glTF says it is.
pub fn invalid(pointer: String) -> anyhow::Error {
    refuse("Cook.Model.Invalid", &[("where", pointer)])
}

thread_local! {
    static NOTES: RefCell<Vec<Finding>> = const { RefCell::new(Vec::new()) };
}

/// Said about the file, which still cooks.
pub fn note(why: &'static str, args: &[(&'static str, String)]) {
    NOTES.with(|notes| notes.borrow_mut().push(finding(why, Level::Note, args)));
}

/// The notes said since the last take, in order.
pub fn take_notes() -> Vec<Finding> {
    NOTES.with(|notes| std::mem::take(&mut *notes.borrow_mut()))
}

/// The refusal an error is, when it is one (a cook's own or a patch's).
pub fn refusal_of(error: &anyhow::Error) -> Option<&Finding> {
    error.downcast_ref::<Refusal>().map(|r| &r.0)
}

/// An option as a refusal: `None` is `invalid(pointer)`.
pub trait OrInvalid<T> {
    fn or_invalid(self, pointer: impl FnOnce() -> String) -> anyhow::Result<T>;
}

impl<T> OrInvalid<T> for Option<T> {
    fn or_invalid(self, pointer: impl FnOnce() -> String) -> anyhow::Result<T> {
        self.ok_or_else(|| invalid(pointer()))
    }
}

/// What the cook says about one file of a mod, for the facts file beside the store (mod-log 9: COOK entries).
#[derive(Clone, Debug, PartialEq)]
pub enum CookFact {
    /// The file was refused: nothing of it is in the store.
    Refused { path: String, source: String, finding: Finding },
    /// Said about the file, which cooked.
    Noted { path: String, source: String, finding: Finding },
    /// A model cooked (models contract 9's `cook.model`).
    Model { path: String, source: String, facts: super::gltf::ModelFacts },
    /// An effect of a mod cooked, or a game particle it replaced (effects contract 7's `cook.effect`).
    Effect { path: String, source: String, facts: EffectFacts },
    /// A layout of a mod cooked (layout contract's `cook.layout`).
    Layout { path: String, source: String, facts: super::layout_json::LayoutFacts },
}

/// What `cook.effect` says of one effect (`<id>:<name>`) or one replaced game particle (its name).
#[derive(Clone, Debug, PartialEq)]
pub struct EffectFacts {
    pub name: String,
    /// Its particles' numbers (ParticleNames' id).
    pub particles: Vec<u32>,
    /// Draw group, 0 to 5 (effects contract 4).
    pub group: u8,
    /// The texture its first particle's sprite role names ("" for none).
    pub sprite: String,
    /// Textures the file adds to the game's particle file.
    pub textures: usize,
    pub replaced: bool,
}

thread_local! {
    static FACTS: RefCell<Vec<CookFact>> = const { RefCell::new(Vec::new()) };
}

pub fn record(fact: CookFact) {
    FACTS.with(|facts| facts.borrow_mut().push(fact));
}

/// The facts recorded since the last take, in order.
pub fn take_facts() -> Vec<CookFact> {
    FACTS.with(|facts| std::mem::take(&mut *facts.borrow_mut()))
}

fn field(text: &str) -> String {
    text.replace('\\', "\\\\").replace('\t', "\\t").replace('\n', "\\n").replace('\r', "\\r")
}

/// The facts as lines of the facts file, in its form (overlay_patches::facts_text): `kind table mod file other row
/// column selector rows code why`, then parameters. `failed` (Error) and `noted` (Note) carry rule 804, the phrase's
/// English for the log and its parameters (`why=P:Cook.Model.<Kind>`, then `name=T:<text>`); `cook.model` carries the
/// replaced game model in `other`, then `name=<number>` for joints, extra_joints, meshes, vertices, materials,
/// textures and clips; `cook.effect` carries the effect (`<id>:<name>`, or the replaced game particle) in `other`, then
/// `particles=<numbers, comma between>`, `group`, `sprite`, `textures` and `replaced` (0 or 1); `cook.layout` carries the
/// layout file it made in `other`, then `panes`, `pictures`, `texts`, `textures` and `groups`. `table` is the file's
/// place in the game, `file` the mod's file it came from.
pub fn cook_facts_text(facts: &[CookFact]) -> String {
    let mut out = String::new();
    for fact in facts {
        let line: Vec<String> = match fact {
            CookFact::Refused { path, source, finding } | CookFact::Noted { path, source, finding } => {
                let kind = if matches!(fact, CookFact::Refused { .. }) { "failed" } else { "noted" };
                let fixed = [kind, path, "", source, "", "", "", "", ""].map(str::to_string);
                fixed
                    .into_iter()
                    .chain([finding.code.to_string(), luma_validate::reason(finding, "UsEnglish")])
                    .chain(luma_validate::args_text(finding))
                    .collect()
            }
            CookFact::Model { path, source, facts } => {
                let fixed = ["cook.model", path, "", source, &facts.replaced, "", "", "", "", "", ""].map(str::to_string);
                let counts = [
                    ("joints", facts.joints),
                    ("extra_joints", facts.extra_joints),
                    ("meshes", facts.meshes),
                    ("vertices", facts.vertices),
                    ("materials", facts.materials),
                    ("textures", facts.textures),
                    ("clips", facts.clips),
                ];
                fixed.into_iter().chain(counts.iter().map(|(name, n)| format!("{name}={n}"))).collect()
            }
            CookFact::Effect { path, source, facts } => {
                let fixed = ["cook.effect", path, "", source, &facts.name, "", "", "", "", "", ""].map(str::to_string);
                let numbers = facts.particles.iter().map(u32::to_string).collect::<Vec<_>>().join(",");
                fixed
                    .into_iter()
                    .chain([
                        format!("particles={numbers}"),
                        format!("group={}", facts.group),
                        format!("sprite={}", facts.sprite),
                        format!("textures={}", facts.textures),
                        format!("replaced={}", u8::from(facts.replaced)),
                    ])
                    .collect()
            }
            CookFact::Layout { path, source, facts } => {
                let fixed = ["cook.layout", path, "", source, path, "", "", "", "", "", ""].map(str::to_string);
                let counts = [("panes", facts.panes), ("pictures", facts.pictures), ("texts", facts.texts), ("textures", facts.textures), ("groups", facts.groups)];
                fixed.into_iter().chain(counts.iter().map(|(name, n)| format!("{name}={n}"))).collect()
            }
        };
        out.push_str(&line.iter().map(|f| field(f)).collect::<Vec<_>>().join("\t"));
        out.push('\n');
    }
    out
}

/// A refusal's phrase and arguments, a phrase argument as `P:<id>`; every phrase has its row and every placeholder of
/// its words is given, in four languages.
#[cfg(test)]
pub fn said(error: &anyhow::Error) -> (&'static str, Vec<(&'static str, String)>) {
    let finding = refusal_of(error).unwrap_or_else(|| panic!("not a refusal: {error:#}"));
    assert_eq!(finding.code, COOK_FAILED);
    said_finding(finding)
}

#[cfg(test)]
fn said_finding(finding: &Finding) -> (&'static str, Vec<(&'static str, String)>) {
    let mut args = finding.args.iter();
    let Some(("why", Arg::Phrase(why))) = args.next() else { panic!("no why: {finding:?}") };
    for language in ["UsEnglish", "UsFrench", "JpJapanese", "KrKorean"] {
        let words = luma_validate::reason(finding, language);
        assert!(!words.contains('{') && !words.contains("Cook."), "{why} in {language}: {words}");
    }
    let rest = args
        .map(|(name, value)| match value {
            Arg::Text(text) => (*name, text.clone()),
            Arg::Phrase(id) => (*name, format!("P:{id}")),
        })
        .collect();
    (why, rest)
}

/// The notes said so far on this thread, as `said` gives a refusal.
#[cfg(test)]
pub fn said_notes() -> Vec<(&'static str, Vec<(&'static str, String)>)> {
    take_notes().iter().map(said_finding).collect()
}
