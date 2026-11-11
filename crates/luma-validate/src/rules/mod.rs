//! The rules, by block of codes. Each block reports every finding it can; none stops at the first.

mod code;
mod container;
mod kit;
mod layers;
mod manifest;
mod mods;
mod patches;

use crate::json::{self, Pos};
use crate::source::{Mod, OpenError};
use crate::{level_of, Arg, Finding, Input, NotChecked, Report};
use std::collections::BTreeMap;

pub(crate) struct Ctx<'i, 'a> {
    pub input: &'i Input<'a>,
    pub m: Mod<'a>,
    pub mod_id: Option<String>,
    pub findings: Vec<Finding>,
    pub not_checked: BTreeMap<(u16, &'static str), u32>,
    /// `luma.json`, when it is JSON (its rules may still have found errors in it).
    pub manifest: Option<json::Value>,
    /// What the other blocks need from the manifest.
    pub facts: manifest::Facts,
}

pub(crate) fn t(text: impl Into<String>) -> Arg {
    Arg::Text(text.into())
}
pub(crate) fn p(id: &'static str) -> Arg {
    Arg::Phrase(id)
}

impl<'i, 'a> Ctx<'i, 'a> {
    /// Adds a finding of `code` in `file`, at `at` (JSON line and column) and `pointer` when given.
    pub fn add(&mut self, code: u16, file: &str, at: Option<Pos>, pointer: Option<&str>, args: Vec<(&'static str, Arg)>) {
        self.findings.push(Finding {
            code,
            level: level_of(code),
            mod_id: self.mod_id.clone(),
            file: file.to_string(),
            line: at.map(|p| p.line),
            column: at.map(|p| p.column),
            pointer: pointer.map(str::to_string),
            args,
        });
    }
    /// A rule not checked at one place because `because` (a phrase) is absent.
    pub fn not_checked(&mut self, code: u16, because: &'static str) {
        *self.not_checked.entry((code, because)).or_insert(0) += 1;
    }
    /// The game, borrowed from the input (not from the context, so findings can be added while it is held).
    pub fn game(&self) -> Option<&'a crate::Game<'a>> {
        self.input.game
    }
}

pub(crate) fn run(input: &Input) -> Report {
    let mut report = Report { findings: Vec::new(), not_checked: Vec::new(), touches: Default::default(), validator: crate::VERSION, format: crate::FORMAT };
    let m = match Mod::open(&input.source) {
        Ok(m) => m,
        Err((shown, error)) => {
            let (code, args) = match error {
                OpenError::Zip(crate::zip::Problem::NotZip(why)) => (100, vec![("found", p(why))]),
                OpenError::Zip(crate::zip::Problem::MultiDisk) => (101, vec![("entry", t(shown.clone())), ("found", p("Zip.MultiDisk"))]),
                OpenError::Zip(crate::zip::Problem::TooManyEntries(n)) => (
                    105,
                    vec![("what", p("Limit.Entries")), ("found", t(n.to_string())), ("expected", t(crate::zip::MAX_ENTRIES.to_string()))],
                ),
                OpenError::Unreadable(why, detail) => (100, vec![("found", p(why)), ("detail", t(detail))]),
            };
            report.findings.push(Finding {
                code,
                level: level_of(code),
                mod_id: None,
                file: shown,
                line: None,
                column: None,
                pointer: None,
                args,
            });
            return report;
        }
    };
    let mut ctx = Ctx { input, m, mod_id: None, findings: Vec::new(), not_checked: BTreeMap::new(), manifest: None, facts: Default::default() };
    container::check(&mut ctx);
    manifest::check(&mut ctx);
    if ctx.manifest.is_some() {
        layers::check(&mut ctx);
        patches::check(&mut ctx);
        code::check(&mut ctx);
        kit::check(&mut ctx);
        mods::check(&mut ctx);
    }
    // Findings made before the id was known (container rules) carry it too once it is.
    if let Some(id) = &ctx.mod_id {
        for f in &mut ctx.findings {
            if f.mod_id.is_none() && f.code >= 107 {
                f.mod_id = Some(id.clone());
            }
        }
    }
    report.touches = std::mem::take(&mut ctx.facts.touches);
    report.findings = ctx.findings;
    report.not_checked = ctx.not_checked.into_iter().map(|((code, because), places)| NotChecked { code, because, places }).collect();
    report
}

/// A JSON pointer segment, escaped (RFC 6901).
pub(crate) fn seg(key: &str) -> String {
    key.replace('~', "~0").replace('/', "~1")
}
