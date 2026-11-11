//! The report the game keeps beside each installed mod (validator contract 2): written after the full check at
//! import, read at each start. It holds what the start's check reuses (the findings of the rules that read the whole
//! `.luma`) and what the other mods' checks need of this one (its id, version, conflicts, needs and what it changes), with
//! the fingerprint of the `.luma` and of the game it was checked against. A different fingerprint, a different game or
//! a report that cannot be read means a full check, never a partial one on stale data.
//!
//! The form is UTF-8 text, one record per line, fields separated by tabs, `%`, tab, CR and LF written as `%25`,
//! `%09`, `%0D`, `%0A`:
//!
//! ```text
//! luma-saved	2
//! validator	<major>.<minor>
//! fingerprint	<hex>|-
//! game	<the game's key>
//! about	<id>	<version>
//! conflict	<id>
//! need	<id>
//! touch	replaces|cells|adds|removes|assets|functions|links|scenarios	<value>
//! finding	<code>	<file>	<line>|-	<column>|-	<pointer>|-	<mod id>|-	(<name>=T:<text> | <name>=P:<phrase id>)...
//! not-checked	<code>	<phrase id>	<places>
//! end	<SHA-1 of every byte before this line, hex>
//! ```
//!
//! A report cut short or changed in any byte does not read (then the mod is checked in full).

use crate::{level_of, text, Arg, Finding, Installed, NotChecked, Report, Touches};

/// What the mod is, from its luma.json, for the other mods' checks.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct About {
    pub id: String,
    pub version: crate::semver::Version,
    /// The ids its `conflicts` names.
    pub conflicts: Vec<String>,
    /// The ids its `needs` names, in its order (L800's cycle).
    pub needs: Vec<String>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Saved {
    pub validator: (u32, u32),
    /// The `.luma`'s fingerprint (`Report::fingerprint`); None for a folder, which is always checked in full.
    pub fingerprint: Option<String>,
    /// What the game says identifies itself for the check (release, region, platform, its names, its store).
    pub game: String,
    pub about: Option<About>,
    pub touches: Touches,
    pub findings: Vec<Finding>,
    pub not_checked: Vec<NotChecked>,
}

const MAGIC: &str = "luma-saved";
// 2: `need` lines. A form 1 report does not read, so a mod checked before them is checked in full again.
const FORM: u32 = 2;
const MAX_SAVED: usize = 64 << 20;

fn esc(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '%' => out.push_str("%25"),
            '\t' => out.push_str("%09"),
            '\r' => out.push_str("%0D"),
            '\n' => out.push_str("%0A"),
            c => out.push(c),
        }
    }
    out
}

fn unesc(s: &str) -> Option<String> {
    let mut out = String::with_capacity(s.len());
    let mut rest = s;
    while let Some(at) = rest.find('%') {
        out.push_str(&rest[..at]);
        let code = rest.get(at + 1..at + 3)?;
        out.push(match code {
            "25" => '%',
            "09" => '\t',
            "0D" => '\r',
            "0A" => '\n',
            _ => return None,
        });
        rest = &rest[at + 3..];
    }
    out.push_str(rest);
    Some(out)
}

fn sha1_hex(bytes: &[u8]) -> String {
    use sha1::{Digest, Sha1};
    Sha1::digest(bytes).iter().map(|b| format!("{b:02x}")).collect()
}

fn opt(v: Option<&str>) -> String {
    v.map_or_else(|| "-".to_string(), esc)
}

fn touch_lists(t: &Touches) -> [(&'static str, &Vec<String>); 8] {
    [
        ("replaces", &t.replaces),
        ("cells", &t.cells),
        ("adds", &t.adds),
        ("removes", &t.removes),
        ("assets", &t.assets),
        ("functions", &t.functions),
        ("links", &t.links),
        ("scenarios", &t.scenarios),
    ]
}

impl Saved {
    /// What to keep of a full check's report, against the game identified by `game`.
    pub fn from_report(report: &Report, game: &str) -> Saved {
        Saved {
            validator: report.validator,
            fingerprint: report.fingerprint.clone(),
            game: game.to_string(),
            about: report.about.clone(),
            touches: report.touches.clone(),
            findings: report.findings.clone(),
            not_checked: report.not_checked.clone(),
        }
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut out = format!("{MAGIC}\t{FORM}\nvalidator\t{}.{}\n", self.validator.0, self.validator.1);
        out += &format!("fingerprint\t{}\ngame\t{}\n", opt(self.fingerprint.as_deref()), esc(&self.game));
        if let Some(a) = &self.about {
            out += &format!("about\t{}\t{}\n", esc(&a.id), esc(&a.version.to_string()));
            for c in &a.conflicts {
                out += &format!("conflict\t{}\n", esc(c));
            }
            for n in &a.needs {
                out += &format!("need\t{}\n", esc(n));
            }
        }
        for (kind, list) in touch_lists(&self.touches) {
            for v in list {
                out += &format!("touch\t{kind}\t{}\n", esc(v));
            }
        }
        for f in &self.findings {
            out += &format!(
                "finding\t{}\t{}\t{}\t{}\t{}\t{}",
                f.code,
                esc(&f.file),
                f.line.map_or_else(|| "-".into(), |l| l.to_string()),
                f.column.map_or_else(|| "-".into(), |c| c.to_string()),
                opt(f.pointer.as_deref()),
                opt(f.mod_id.as_deref())
            );
            for (name, arg) in &f.args {
                match arg {
                    Arg::Text(s) => out += &format!("\t{name}=T:{}", esc(s)),
                    Arg::Phrase(id) => out += &format!("\t{name}=P:{id}"),
                }
            }
            out.push('\n');
        }
        for n in &self.not_checked {
            out += &format!("not-checked\t{}\t{}\t{}\n", n.code, n.because, n.places);
        }
        out += &format!("end\t{}\n", sha1_hex(out.as_bytes()));
        out.into_bytes()
    }

    /// Reads a kept report; None when it is not one this validator wrote (then the mod is checked in full).
    pub fn parse(bytes: &[u8]) -> Option<Saved> {
        if bytes.len() > MAX_SAVED {
            return None;
        }
        let text = std::str::from_utf8(bytes).ok()?;
        // The last line is `end` with the SHA-1 of everything before it.
        let body = text.strip_suffix('\n')?;
        let at = body.rfind('\n')? + 1;
        if body[at..].strip_prefix("end\t")? != sha1_hex(&bytes[..at]) {
            return None;
        }
        let mut lines = body[..at].lines();
        if lines.next()? != format!("{MAGIC}\t{FORM}") {
            return None;
        }
        let mut s = Saved { validator: (0, 0), fingerprint: None, game: String::new(), about: None, touches: Touches::default(), findings: Vec::new(), not_checked: Vec::new() };
        let mut seen_validator = false;
        let mut seen_game = false;
        for line in lines {
            let f: Vec<&str> = line.split('\t').collect();
            match (f[0], f.len()) {
                ("validator", 2) => {
                    let (a, b) = f[1].split_once('.')?;
                    s.validator = (a.parse().ok()?, b.parse().ok()?);
                    seen_validator = true;
                }
                ("fingerprint", 2) => s.fingerprint = (f[1] != "-").then(|| f[1].to_string()),
                ("game", 2) => {
                    s.game = unesc(f[1])?;
                    seen_game = true;
                }
                ("about", 3) => {
                    s.about = Some(About { id: unesc(f[1])?, version: crate::semver::Version::parse(&unesc(f[2])?)?, conflicts: Vec::new(), needs: Vec::new() });
                }
                ("conflict", 2) => s.about.as_mut()?.conflicts.push(unesc(f[1])?),
                ("need", 2) => s.about.as_mut()?.needs.push(unesc(f[1])?),
                ("touch", 3) => {
                    let v = unesc(f[2])?;
                    let t = &mut s.touches;
                    match f[1] {
                        "replaces" => t.replaces.push(v),
                        "cells" => t.cells.push(v),
                        "adds" => t.adds.push(v),
                        "removes" => t.removes.push(v),
                        "assets" => t.assets.push(v),
                        "functions" => t.functions.push(v),
                        "links" => t.links.push(v),
                        "scenarios" => t.scenarios.push(v),
                        _ => return None,
                    }
                }
                ("finding", n) if n >= 7 => {
                    let code: u16 = f[1].parse().ok()?;
                    let num = |x: &str| -> Option<Option<u32>> { if x == "-" { Some(None) } else { x.parse().ok().map(Some) } };
                    let text_opt = |x: &str| -> Option<Option<String>> { if x == "-" { Some(None) } else { unesc(x).map(Some) } };
                    let mut args = Vec::new();
                    for a in &f[7..] {
                        let (name, value) = a.split_once('=')?;
                        let name = text::static_name(name)?;
                        let arg = if let Some(v) = value.strip_prefix("T:") {
                            Arg::Text(unesc(v)?)
                        } else {
                            Arg::Phrase(text::static_id(value.strip_prefix("P:")?)?)
                        };
                        args.push((name, arg));
                    }
                    s.findings.push(Finding {
                        code,
                        level: level_of(code),
                        mod_id: text_opt(f[6])?,
                        file: unesc(f[2])?,
                        line: num(f[3])?,
                        column: num(f[4])?,
                        pointer: text_opt(f[5])?,
                        args,
                    });
                }
                ("not-checked", 4) => {
                    s.not_checked.push(NotChecked { code: f[1].parse().ok()?, because: text::static_id(f[2])?, places: f[3].parse().ok()? });
                }
                _ => return None,
            }
        }
        (seen_validator && seen_game).then_some(s)
    }

    /// This mod as another mod's check sees it.
    pub fn installed(&self, file: &str, on: bool) -> Option<Installed> {
        let a = self.about.as_ref()?;
        Some(Installed { id: a.id.clone(), version: a.version.clone(), on, file: file.to_string(), conflicts: a.conflicts.clone(), needs: a.needs.clone(), touches: self.touches.clone() })
    }
}

/// Whether a finding comes from a rule that reads the whole `.luma` (every entry's data: L106; every patch: each finding
/// the patches block makes names its patch file, L4xx and L302). At a start these are taken from the kept report; every
/// other rule runs again.
pub(crate) fn kept_at_start(f: &Finding) -> bool {
    f.code == 106 || (f.code != 404 && is_patch(&f.file))
}

fn is_patch(file: &str) -> bool {
    let mut parts = file.splitn(4, '/');
    parts.next() == Some("layers") && parts.next().is_some() && parts.next() == Some("patches") && parts.next().is_some()
}

/// The rules the patches block reports as not checked (with no file to tell them by).
pub(crate) const NOT_CHECKED_AT_START: [u16; 3] = [401, 402, 407];
