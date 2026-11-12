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
/// textures and clips. `table` is the file's place in the game, `file` the mod's file it came from.
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
        };
        out.push_str(&line.iter().map(|f| field(f)).collect::<Vec<_>>().join("\t"));
        out.push('\n');
    }
    out
}
