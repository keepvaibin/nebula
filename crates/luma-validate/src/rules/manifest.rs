//! L200-L219 and L806: `luma.json` (luma-format 4), `media/` (3, 4) and `saves/` (6.1).

use super::{p, seg, t, Ctx};
use crate::json::{self, Kind, Value};
use crate::semver::{Range, Version};
use crate::source::ReadError;
use crate::zip::EntryKind;
use crate::{Arg, LANGUAGES, PLATFORMS, REGIONS};
use std::collections::BTreeSet;

const FILE: &str = "luma.json";
pub const MAX_MANIFEST: u64 = 1 << 20;
const TOP_KEYS: [&str; 25] = [
    "format", "id", "version", "name", "summary", "text", "authors", "license", "links", "icon", "pictures", "game",
    "nebula", "needs", "conflicts", "load", "takes_over", "options", "permissions", "renderer", "layers", "code",
    "saves", "kit", "build",
];
const REQUIRED: [&str; 7] = ["format", "id", "version", "name", "summary", "authors", "game"];
const OPTION_KINDS: [&str; 5] = ["choice", "number", "toggle", "text", "key"];
pub const MAX_OPTIONS: usize = 64;
pub const MAX_PICTURES: usize = 8;

/// An id: 3 to 64 of lowercase letters, digits, `-` and `.`, starting with a letter, without `..` (luma-format 4).
pub fn id_ok(id: &str) -> bool {
    (3..=64).contains(&id.len())
        && id.starts_with(|c: char| c.is_ascii_lowercase())
        && id.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-' || b == b'.')
        && !id.contains("..")
        && !id.ends_with('.')
}

/// An option or layer key: as an id's characters, 1 to 64 long.
pub fn key_ok(key: &str) -> bool {
    (1..=64).contains(&key.len())
        && key.starts_with(|c: char| c.is_ascii_lowercase())
        && key.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-' || b == b'.' || b == b'_')
}

/// One line of text players read: no line breaks or other control characters, `min..=max` characters.
fn line_ok(s: &str, min: usize, max: usize) -> bool {
    let n = s.chars().count();
    (min..=max).contains(&n) && !s.chars().any(|c| c.is_control()) && s.trim() == s
}

/// A host name, `*.` and a domain, or an IP address, with an optional `:port`.
pub fn host_ok(host: &str) -> bool {
    let (name, port) = match host.rsplit_once(':') {
        Some((n, port)) if !n.contains(':') => (n, Some(port)),
        _ => (host, None),
    };
    if let Some(port) = port {
        if port.parse::<u16>().map_or(true, |p| p == 0) || port.starts_with('0') {
            return false;
        }
    }
    let name = name.strip_prefix("*.").unwrap_or(name);
    !name.is_empty()
        && name.len() <= 253
        && name.split('.').all(|l| {
            !l.is_empty() && l.len() <= 63 && !l.starts_with('-') && !l.ends_with('-') && l.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-')
        })
}

/// An `https` URL with a host and nothing a browser would refuse.
fn url_ok(url: &str) -> bool {
    let Some(rest) = url.strip_prefix("https://") else { return false };
    let host = rest.split(['/', '?', '#']).next().unwrap_or("");
    url.len() <= 2048 && !url.chars().any(|c| c.is_control() || c == ' ') && host_ok(host) && host.contains('.')
}

/// An SPDX license expression's syntax: ids and `LicenseRef-`s joined by AND, OR and WITH, with parentheses.
pub fn spdx_ok(expr: &str) -> bool {
    let spaced = expr.replace('(', " ( ").replace(')', " ) ");
    let words: Vec<&str> = spaced.split_whitespace().collect();
    let mut depth = 0i32;
    let mut want_term = true;
    let mut after_with = false;
    for w in &words {
        match *w {
            "(" if want_term => depth += 1,
            ")" if !want_term => {
                depth -= 1;
                if depth < 0 {
                    return false;
                }
            }
            "AND" | "OR" if !want_term => want_term = true,
            "WITH" if !want_term && !after_with => {
                want_term = true;
                after_with = true;
                continue;
            }
            term if want_term => {
                let term = term.strip_suffix('+').unwrap_or(term);
                if term.is_empty() || !term.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-' || b == b'.') {
                    return false;
                }
                want_term = false;
            }
            _ => return false,
        }
        after_with = false;
    }
    !words.is_empty() && !want_term && depth == 0
}

pub(crate) struct M<'c, 'i, 'a> {
    pub ctx: &'c mut Ctx<'i, 'a>,
}

impl M<'_, '_, '_> {
    fn at(&mut self, code: u16, v: &Value, ptr: &str, args: Vec<(&'static str, Arg)>) {
        self.ctx.add(code, FILE, Some(v.pos), Some(ptr), args);
    }
    fn wrong(&mut self, v: &Value, ptr: &str, key: &str, expected: &'static str) {
        let found = match &v.kind {
            Kind::String(s) if s.chars().count() <= 40 => t(format!("\"{s}\"")),
            Kind::Number(_, text) => t(text.clone()),
            _ => p(v.type_phrase()),
        };
        self.at(204, v, ptr, vec![("key", t(key)), ("found", found), ("expected", p(expected))]);
    }
    /// L202 for every key not in `allowed`.
    fn keys(&mut self, obj: &Value, ptr: &str, allowed: &[&str]) {
        for m in obj.members() {
            if !allowed.contains(&m.key.as_str()) {
                self.ctx.add(202, FILE, Some(m.key_pos), Some(&format!("{ptr}/{}", seg(&m.key))), vec![("key", t(m.key.clone()))]);
            }
        }
    }
    /// L203 for every key of `required` not there; `shown` is how the object is named (`options/2`).
    fn required(&mut self, obj: &Value, ptr: &str, required: &[&str]) {
        for key in required {
            if obj.get(key).is_none() {
                let shown = if ptr.is_empty() { key.to_string() } else { format!("{}/{key}", &ptr[1..]) };
                self.at(203, obj, ptr, vec![("key", t(shown))]);
            }
        }
    }
    fn object<'v>(&mut self, v: &'v Value, ptr: &str, key: &str) -> Option<&'v Value> {
        if v.is_object() {
            Some(v)
        } else {
            self.wrong(v, ptr, key, "Expect.Object");
            None
        }
    }
    fn array<'v>(&mut self, v: &'v Value, ptr: &str, key: &str, min: usize, max: usize) -> Option<&'v [Value]> {
        if !v.is_array() {
            self.wrong(v, ptr, key, "Expect.Array");
            return None;
        }
        let n = v.items().len();
        if n < min || n > max {
            self.at(204, v, ptr, vec![("key", t(key)), ("found", p("Found.Items")), ("count", t(n.to_string())), ("expected", p("Expect.Count")), ("min", t(min.to_string())), ("max", t(max.to_string()))]);
        }
        Some(v.items())
    }
    fn string<'v>(&mut self, v: &'v Value, ptr: &str, key: &str) -> Option<&'v str> {
        match v.as_str() {
            Some(s) => Some(s),
            None => {
                self.wrong(v, ptr, key, "Expect.String");
                None
            }
        }
    }
    /// A one-line string of `min..=max` characters.
    fn line<'v>(&mut self, v: &'v Value, ptr: &str, key: &str, min: usize, max: usize) -> Option<&'v str> {
        let s = self.string(v, ptr, key)?;
        if !line_ok(s, min, max) {
            self.at(204, v, ptr, vec![("key", t(key)), ("found", p("Found.ThisText")), ("expected", p("Expect.Line")), ("min", t(min.to_string())), ("max", t(max.to_string()))]);
            return None;
        }
        Some(s)
    }
    fn id<'v>(&mut self, v: &'v Value, ptr: &str, key: &str) -> Option<&'v str> {
        let s = self.string(v, ptr, key)?;
        if !id_ok(s) {
            self.wrong(v, ptr, key, "Expect.Id");
            return None;
        }
        Some(s)
    }
    fn range(&mut self, v: &Value, ptr: &str, key: &str) -> Option<Range> {
        let s = self.string(v, ptr, key)?;
        let r = Range::parse(s);
        if r.is_none() {
            self.wrong(v, ptr, key, "Expect.Range");
        }
        r
    }
    /// Strings from a fixed list, each once, at least one.
    fn choices<'v>(&mut self, v: &'v Value, ptr: &str, key: &str, allowed: &[&str], expected: &'static str) -> Vec<&'v str> {
        let mut out = Vec::new();
        let Some(items) = self.array(v, ptr, key, 1, 64) else { return out };
        for (i, item) in items.iter().enumerate() {
            let ip = format!("{ptr}/{i}");
            let Some(s) = self.string(item, &ip, key) else { continue };
            if !allowed.contains(&s) {
                self.wrong(item, &ip, key, expected);
            } else if out.contains(&s) {
                self.at(204, item, &ip, vec![("key", t(key)), ("found", t(format!("\"{s}\""))), ("expected", p("Expect.Once"))]);
            } else {
                out.push(s);
            }
        }
        out
    }
}

/// What the other blocks need from the manifest, read once here.
#[derive(Default, Debug)]
pub(crate) struct Facts {
    pub regions: Vec<String>,
    pub layers: Vec<(String, json::Pos, String)>,
    pub options: Vec<OptionFacts>,
    pub permissions: BTreeSet<String>,
    /// Each permission of the closed list asked for, once, with where (for L218, said after the code is read).
    pub permission_at: Vec<(String, json::Pos, String)>,
    /// The services any entry library imports (kind 5, optional or not), and how many entry libraries' imports were
    /// read (L218, said by the code block).
    pub services_imported: BTreeSet<String>,
    pub entries_read: usize,
    pub needs: Vec<(String, Option<Range>)>,
    pub code_platforms: Vec<String>,
    pub has_code_key: bool,
    pub touches: crate::Touches,
    /// A key this mod adds to a link group (group and key, joined by U+001F), and the tables (store paths, in the game's
    /// case) it adds it to.
    pub link_targets: std::collections::BTreeMap<String, Vec<String>>,
    /// The cells the galaxies' scenario tables get (table-keys T-4), lowest first: the installed mods' below this one,
    /// this one's (its patches and its new galaxies' own tables), then those above it (the higher wins).
    pub scenario_cells: Vec<crate::ScenarioCell>,
    /// A zone's layer sets (lowercase zone), once read; None inside when the store could not give them.
    pub layer_sets: std::collections::BTreeMap<String, Option<Vec<crate::LayerSet>>>,
}

#[derive(Debug, Clone)]
pub(crate) struct OptionFacts {
    pub key: String,
    pub kind: String,
    pub values: Vec<String>,
}

pub(crate) fn check(ctx: &mut Ctx) {
    let shown = ctx.m.shown.clone();
    let Some(index) = ctx.m.index_of(FILE) else {
        ctx.add(200, &shown, None, None, vec![]);
        return;
    };
    let bytes = match ctx.m.kept.get(FILE) {
        Some(b) if ctx.m.items[index].size <= MAX_MANIFEST => b.clone(),
        _ => match ctx.m.keep(index, MAX_MANIFEST) {
            Ok(b) => b.to_vec(),
            Err(ReadError::OverLimit) => {
                ctx.add(201, FILE, Some(json::Pos { line: 1, column: 1 }), None, vec![("found", p("Json.TooLarge"))]);
                return;
            }
            // A zip entry that cannot be unpacked is L106 already; a folder's file that cannot be read is said here.
            Err(ReadError::Broken) => {
                if !ctx.m.is_zip() {
                    ctx.add(201, FILE, Some(json::Pos { line: 1, column: 1 }), None, vec![("found", p("Json.Unreadable"))]);
                }
                return;
            }
        },
    };
    let root = match json::parse(&bytes) {
        Ok(v) => v,
        Err(e) => {
            ctx.add(201, FILE, Some(e.pos), None, vec![("found", p(e.what)), ("detail", t(e.detail))]);
            return;
        }
    };
    if let Some(id) = root.get("id").and_then(Value::as_str).filter(|id| id_ok(id)) {
        ctx.mod_id = Some(id.to_string());
    }
    let mut m = M { ctx };
    if !root.is_object() {
        m.wrong(&root, "", "luma.json", "Expect.Object");
        return;
    }
    m.keys(&root, "", &TOP_KEYS);
    m.required(&root, "", &REQUIRED);
    let mut facts = Facts::default();

    if let Some(v) = root.get("format") {
        if let Some(s) = m.string(v, "/format", "format") {
            let parsed = s.split_once('.').and_then(|(a, b)| {
                let ok = |x: &str| !x.is_empty() && x.bytes().all(|c| c.is_ascii_digit()) && (x == "0" || !x.starts_with('0'));
                (ok(a) && ok(b)).then(|| (a.parse::<u32>().ok(), b.parse::<u32>().ok()))
            });
            match parsed {
                Some((Some(major), Some(minor))) => {
                    if (major, minor) > crate::FORMAT {
                        let expected = format!("{}.{}", crate::FORMAT.0, crate::FORMAT.1);
                        m.at(205, v, "/format", vec![("found", t(s)), ("expected", t(expected))]);
                    } else if major < crate::FORMAT.0 {
                        m.wrong(v, "/format", "format", "Expect.Format");
                    }
                }
                _ => m.wrong(v, "/format", "format", "Expect.Format"),
            }
        }
    }
    if let Some(v) = root.get("id") {
        if let Some(s) = m.string(v, "/id", "id") {
            if !id_ok(s) {
                m.at(206, v, "/id", vec![("found", t(s))]);
            }
        }
    }
    if let Some(v) = root.get("version") {
        if let Some(s) = m.string(v, "/version", "version") {
            if Version::parse(s).is_none() {
                m.wrong(v, "/version", "version", "Expect.Version");
            }
        }
    }
    if let Some(v) = root.get("name") {
        m.line(v, "/name", "name", 1, 64);
    }
    if let Some(v) = root.get("summary") {
        m.line(v, "/summary", "summary", 1, 200);
    }
    if let Some(v) = root.get("authors") {
        if let Some(items) = m.array(v, "/authors", "authors", 1, 16) {
            for (i, a) in items.iter().enumerate() {
                m.line(a, &format!("/authors/{i}"), "authors", 1, 64);
            }
        }
    }
    if let Some(v) = root.get("license") {
        if let Some(s) = m.string(v, "/license", "license") {
            if !spdx_ok(s) {
                m.wrong(v, "/license", "license", "Expect.Spdx");
            }
        }
    }
    if let Some(v) = root.get("links") {
        if let Some(obj) = m.object(v, "/links", "links") {
            m.keys(obj, "/links", &["home", "source", "issues"]);
            for member in obj.members() {
                let ptr = format!("/links/{}", seg(&member.key));
                if let Some(s) = m.string(&member.value, &ptr, &member.key) {
                    if !url_ok(s) {
                        m.wrong(&member.value, &ptr, &member.key, "Expect.Https");
                    }
                }
            }
        }
    }
    game(&mut m, &root, &mut facts);
    if let Some(v) = root.get("nebula") {
        m.range(v, "/nebula", "nebula");
    }
    references(&mut m, &root, &mut facts);
    options(&mut m, &root, &mut facts);
    text(&mut m, &root, &facts);
    permissions(&mut m, &root, &mut facts);
    renderer(&mut m, &root);
    layers(&mut m, &root, &mut facts);
    code_key(&mut m, &root, &mut facts);
    media(&mut m, &root);
    saves(&mut m, &root, &facts);
    if let Some(v) = root.get("kit") {
        if let Some(obj) = m.object(v, "/kit", "kit") {
            for member in obj.members() {
                let ptr = format!("/kit/{}", seg(&member.key));
                if !member.key.split('.').all(key_ok) || !member.key.contains('.') {
                    m.wrong(&member.value, &ptr, &member.key, "Expect.ModuleId");
                }
                if member.value.as_i64().map_or(true, |n| !(1..=1_000_000).contains(&n)) {
                    m.wrong(&member.value, &ptr, &member.key, "Expect.SchemaVersion");
                }
            }
        }
    }
    if let Some(v) = root.get("build") {
        if let Some(obj) = m.object(v, "/build", "build") {
            m.keys(obj, "/build", &["lumalink", "self"]);
            m.required(obj, "/build", &["lumalink", "self"]);
            for key in ["lumalink", "self"] {
                if let Some(x) = obj.get(key) {
                    m.line(x, &format!("/build/{key}"), key, 1, 128);
                }
            }
        }
    }
    m.ctx.manifest = Some(root);
    m.ctx.facts = facts;
}

fn game(m: &mut M, root: &Value, facts: &mut Facts) {
    let Some(v) = root.get("game") else { return };
    let Some(obj) = m.object(v, "/game", "game") else { return };
    m.keys(obj, "/game", &["id", "regions"]);
    m.required(obj, "/game", &["id"]);
    if let Some(id) = obj.get("id") {
        if let Some(s) = m.string(id, "/game/id", "game/id") {
            if s != "RMG" {
                let found = match s.get(..3) {
                    Some("SB4") | Some("SB5") => p("Game.Galaxy2"),
                    _ => t(s),
                };
                m.at(207, id, "/game/id", vec![("found", found)]);
            }
        }
    }
    facts.regions = REGIONS.iter().map(|s| s.to_string()).collect();
    if let Some(regions) = obj.get("regions") {
        let chosen = m.choices(regions, "/game/regions", "game/regions", &REGIONS, "Expect.Region");
        facts.regions = chosen.iter().map(|s| s.to_string()).collect();
    }
    if let Some(region) = m.ctx.game().and_then(|g| g.region) {
        if !facts.regions.iter().any(|r| r == region) {
            let expected = facts.regions.join(", ");
            m.at(208, v, "/game", vec![("expected", t(expected)), ("found", t(region))]);
        }
    }
}

fn references(m: &mut M, root: &Value, facts: &mut Facts) {
    let own = m.ctx.mod_id.clone().unwrap_or_default();
    if let Some(v) = root.get("needs") {
        if let Some(items) = m.array(v, "/needs", "needs", 0, 64) {
            for (i, need) in items.iter().enumerate() {
                let ptr = format!("/needs/{i}");
                let Some(obj) = m.object(need, &ptr, "needs") else { continue };
                m.keys(obj, &ptr, &["id", "version"]);
                m.required(obj, &ptr, &["id"]);
                let id = obj.get("id").and_then(|x| m.id(x, &format!("{ptr}/id"), "needs/id")).map(str::to_string);
                let range = obj.get("version").and_then(|x| m.range(x, &format!("{ptr}/version"), "needs/version"));
                if let Some(id) = id {
                    if id == own {
                        m.wrong(obj.get("id").unwrap_or(obj), &format!("{ptr}/id"), "needs/id", "Expect.NotSelf");
                    } else {
                        facts.needs.push((id, range));
                    }
                }
            }
        }
    }
    if let Some(v) = root.get("conflicts") {
        if let Some(items) = m.array(v, "/conflicts", "conflicts", 0, 64) {
            for (i, c) in items.iter().enumerate() {
                let ptr = format!("/conflicts/{i}");
                let Some(obj) = m.object(c, &ptr, "conflicts") else { continue };
                m.keys(obj, &ptr, &["id", "why"]);
                m.required(obj, &ptr, &["id", "why"]);
                if let Some(x) = obj.get("id") {
                    if m.id(x, &format!("{ptr}/id"), "conflicts/id") == Some(own.as_str()) {
                        m.wrong(x, &format!("{ptr}/id"), "conflicts/id", "Expect.NotSelf");
                    }
                }
                if let Some(x) = obj.get("why") {
                    m.line(x, &format!("{ptr}/why"), "conflicts/why", 1, 200);
                }
            }
        }
    }
    if let Some(v) = root.get("load") {
        if let Some(obj) = m.object(v, "/load", "load") {
            m.keys(obj, "/load", &["after", "before"]);
            let mut lists: [Vec<String>; 2] = [Vec::new(), Vec::new()];
            for (n, key) in ["after", "before"].iter().enumerate() {
                let Some(list) = obj.get(key) else { continue };
                let ptr = format!("/load/{key}");
                let Some(items) = m.array(list, &ptr, &format!("load/{key}"), 0, 64) else { continue };
                for (i, x) in items.iter().enumerate() {
                    if let Some(id) = m.id(x, &format!("{ptr}/{i}"), &format!("load/{key}")) {
                        if id == own {
                            m.at(212, x, &format!("{ptr}/{i}"), vec![("found", t(format!("{own} → {own}")))]);
                        }
                        lists[n].push(id.to_string());
                    }
                }
            }
            for id in &lists[0] {
                if lists[1].contains(id) {
                    m.at(212, obj, "/load", vec![("found", t(format!("{own} → {id} → {own}")))]);
                }
            }
        }
    }
    if let Some(v) = root.get("takes_over") {
        if let Some(items) = m.array(v, "/takes_over", "takes_over", 0, 16) {
            for (i, x) in items.iter().enumerate() {
                let ptr = format!("/takes_over/{i}");
                let Some(id) = m.id(x, &ptr, "takes_over") else { continue };
                if id == own {
                    m.at(213, x, &ptr, vec![("found", t(id)), ("why", p("TakesOver.Self"))]);
                } else if m.ctx.input.installed.iter().any(|o| o.id == id && o.on) {
                    m.at(213, x, &ptr, vec![("found", t(id)), ("why", p("TakesOver.On"))]);
                }
            }
        }
    }
}

fn options(m: &mut M, root: &Value, facts: &mut Facts) {
    let Some(v) = root.get("options") else { return };
    let Some(items) = m.array(v, "/options", "options", 0, MAX_OPTIONS) else { return };
    for (i, o) in items.iter().enumerate() {
        let ptr = format!("/options/{i}");
        let Some(obj) = m.object(o, &ptr, "options") else { continue };
        let kind = obj.get("kind").and_then(Value::as_str).unwrap_or("");
        let by_kind: &[&str] = match kind {
            "choice" => &["values", "value_names"],
            "number" => &["min", "max", "step"],
            "text" => &["max_length"],
            _ => &[],
        };
        let allowed: Vec<&str> = ["key", "kind", "default", "name"].iter().chain(by_kind).copied().collect();
        m.keys(obj, &ptr, &allowed);
        let mut required = vec!["key", "kind", "name"];
        if kind != "key" {
            required.push("default");
        }
        required.extend_from_slice(by_kind);
        m.required(obj, &ptr, &required);
        let key = obj.get("key").and_then(|k| {
            let s = m.string(k, &format!("{ptr}/key"), "options/key")?;
            if key_ok(s) {
                Some(s.to_string())
            } else {
                m.wrong(k, &format!("{ptr}/key"), "options/key", "Expect.Key");
                None
            }
        });
        let shown = key.clone().unwrap_or_else(|| i.to_string());
        if let Some(k) = &key {
            if facts.options.iter().any(|o| &o.key == k) {
                m.at(210, obj, &ptr, vec![("key", t(k.clone())), ("why", p("Option.Twice"))]);
            }
        }
        if let Some(x) = obj.get("kind") {
            if let Some(s) = m.string(x, &format!("{ptr}/kind"), "options/kind") {
                if !OPTION_KINDS.contains(&s) {
                    m.wrong(x, &format!("{ptr}/kind"), "options/kind", "Expect.OptionKind");
                }
            }
        }
        if let Some(x) = obj.get("name") {
            m.line(x, &format!("{ptr}/name"), "options/name", 1, 64);
        }
        let default = obj.get("default");
        let mut values = Vec::new();
        match kind {
            "choice" => {
                if let Some(x) = obj.get("values") {
                    if let Some(list) = m.array(x, &format!("{ptr}/values"), "options/values", 1, 64) {
                        for (j, val) in list.iter().enumerate() {
                            let vp = format!("{ptr}/values/{j}");
                            let Some(s) = m.string(val, &vp, "options/values") else { continue };
                            if !key_ok(s) {
                                m.wrong(val, &vp, "options/values", "Expect.Key");
                            } else if values.iter().any(|v: &String| v == s) {
                                m.at(210, val, &vp, vec![("key", t(shown.clone())), ("why", p("Option.ValueTwice")), ("value", t(s))]);
                            } else {
                                values.push(s.to_string());
                            }
                        }
                    }
                }
                if let Some(x) = obj.get("value_names") {
                    let np = format!("{ptr}/value_names");
                    if let Some(names) = m.object(x, &np, "options/value_names") {
                        for member in names.members() {
                            let mp = format!("{np}/{}", seg(&member.key));
                            if !values.contains(&member.key) {
                                m.ctx.add(210, FILE, Some(member.key_pos), Some(&mp), vec![("key", t(shown.clone())), ("why", p("Option.NameOfNoValue")), ("value", t(member.key.clone()))]);
                            }
                            m.line(&member.value, &mp, "options/value_names", 1, 64);
                        }
                        for value in &values {
                            if names.get(value).is_none() {
                                m.at(210, x, &np, vec![("key", t(shown.clone())), ("why", p("Option.ValueUnnamed")), ("value", t(value.clone()))]);
                            }
                        }
                    }
                }
                if let Some(d) = default {
                    if let Some(s) = m.string(d, &format!("{ptr}/default"), "options/default") {
                        if !values.is_empty() && !values.iter().any(|v| v == s) {
                            m.at(210, d, &format!("{ptr}/default"), vec![("key", t(shown.clone())), ("why", p("Option.DefaultNotAValue")), ("value", t(s))]);
                        }
                    }
                }
            }
            "number" => {
                let num = |m: &mut M, key: &str| -> Option<f64> {
                    let x = obj.get(key)?;
                    match x.as_f64() {
                        Some(n) => Some(n),
                        None => {
                            m.wrong(x, &format!("{ptr}/{key}"), &format!("options/{key}"), "Expect.Number");
                            None
                        }
                    }
                };
                let (min, max, step, d) = (num(m, "min"), num(m, "max"), num(m, "step"), num(m, "default"));
                if let (Some(min), Some(max)) = (min, max) {
                    if min >= max {
                        m.at(210, obj, &ptr, vec![("key", t(shown.clone())), ("why", p("Option.MinNotBelowMax"))]);
                    }
                    if let Some(d) = d {
                        if d < min || d > max {
                            m.at(210, obj.get("default").unwrap_or(obj), &format!("{ptr}/default"), vec![("key", t(shown.clone())), ("why", p("Option.DefaultOutside"))]);
                        } else if let Some(step) = step.filter(|s| *s > 0.0) {
                            let steps = (d - min) / step;
                            if (steps - steps.round()).abs() > 1e-9 * steps.abs().max(1.0) {
                                m.at(210, obj.get("default").unwrap_or(obj), &format!("{ptr}/default"), vec![("key", t(shown.clone())), ("why", p("Option.DefaultOffStep"))]);
                            }
                        }
                    }
                }
                if let Some(step) = step {
                    if step <= 0.0 {
                        m.at(210, obj.get("step").unwrap_or(obj), &format!("{ptr}/step"), vec![("key", t(shown.clone())), ("why", p("Option.StepNotPositive"))]);
                    }
                }
            }
            "toggle" => {
                if let Some(d) = default {
                    if d.as_bool().is_none() {
                        m.wrong(d, &format!("{ptr}/default"), "options/default", "Expect.Bool");
                    }
                }
            }
            "text" => {
                let max = obj.get("max_length").and_then(|x| match x.as_i64() {
                    Some(n) if (1..=256).contains(&n) => Some(n as usize),
                    _ => {
                        m.wrong(x, &format!("{ptr}/max_length"), "options/max_length", "Expect.MaxLength");
                        None
                    }
                });
                if let (Some(d), Some(max)) = (default, max) {
                    if let Some(s) = m.string(d, &format!("{ptr}/default"), "options/default") {
                        if !line_ok(s, 0, max) {
                            m.at(210, d, &format!("{ptr}/default"), vec![("key", t(shown.clone())), ("why", p("Option.DefaultTooLong"))]);
                        }
                    }
                }
            }
            "key" => {
                if let Some(d) = default {
                    m.line(d, &format!("{ptr}/default"), "options/default", 1, 64);
                }
            }
            _ => {}
        }
        if let Some(k) = key {
            facts.options.push(OptionFacts { key: k, kind: kind.to_string(), values });
        }
    }
}

fn text(m: &mut M, root: &Value, facts: &Facts) {
    let Some(v) = root.get("text") else { return };
    let Some(obj) = m.object(v, "/text", "text") else { return };
    let extra: Vec<String> = m.ctx.game().map(|g| g.extra_languages.to_vec()).unwrap_or_default();
    for member in obj.members() {
        let ptr = format!("/text/{}", seg(&member.key));
        if !LANGUAGES.contains(&member.key.as_str()) && !extra.contains(&member.key) {
            m.ctx.add(214, FILE, Some(member.key_pos), Some(&ptr), vec![("found", t(member.key.clone()))]);
        }
        let Some(lang) = m.object(&member.value, &ptr, &member.key) else { continue };
        m.keys(lang, &ptr, &["name", "summary", "options"]);
        if let Some(x) = lang.get("name") {
            m.line(x, &format!("{ptr}/name"), "text/name", 1, 64);
        }
        if let Some(x) = lang.get("summary") {
            m.line(x, &format!("{ptr}/summary"), "text/summary", 1, 200);
        }
        let Some(opts) = lang.get("options") else { continue };
        let op = format!("{ptr}/options");
        let Some(opts) = m.object(opts, &op, "text/options") else { continue };
        for o in opts.members() {
            let p2 = format!("{op}/{}", seg(&o.key));
            let Some(known) = facts.options.iter().find(|f| f.key == o.key) else {
                m.ctx.add(210, FILE, Some(o.key_pos), Some(&p2), vec![("key", t(o.key.clone())), ("why", p("Option.TextOfNoOption"))]);
                continue;
            };
            let Some(entry) = m.object(&o.value, &p2, &o.key) else { continue };
            m.keys(entry, &p2, &["name", "value_names"]);
            if let Some(x) = entry.get("name") {
                m.line(x, &format!("{p2}/name"), "text/options/name", 1, 64);
            }
            if let Some(x) = entry.get("value_names") {
                let np = format!("{p2}/value_names");
                if let Some(names) = m.object(x, &np, "value_names") {
                    for n in names.members() {
                        if !known.values.contains(&n.key) {
                            m.ctx.add(210, FILE, Some(n.key_pos), Some(&format!("{np}/{}", seg(&n.key))), vec![("key", t(o.key.clone())), ("why", p("Option.NameOfNoValue")), ("value", t(n.key.clone()))]);
                        }
                        m.line(&n.value, &format!("{np}/{}", seg(&n.key)), "value_names", 1, 64);
                    }
                }
            }
        }
    }
}

/// The permissions a mod may ask for (luma-format revision 3, services-registry 5.1): a closed list. A permissioned
/// service's permission is its own name (registry 9), so these are also the names of the services that need one,
/// `native` aside (not a service: extra libraries, F-1).
pub const PERMISSIONS: [&str; 11] = [
    "native",
    "fs.exchange",
    "net.local",
    "net.internet",
    "input.inject",
    "input.devices",
    "audio.microphone",
    "capture.camera",
    "system.clipboard",
    "system.links",
    "save.game",
];

/// The most hosts `net.internet` may list (services-registry 5.1).
pub const MAX_HOSTS: usize = 32;

/// A host `net.internet` may reach (services-registry 5.1, G R-2): `*` alone, a name (`api.example.org`), a name
/// under a domain (`*.example.org`), or an IPv4 address in dotted digits. No port, no IPv6 literal. Letters of either
/// case: the host lowercases them (consent 1.2).
pub fn permission_host_ok(host: &str) -> bool {
    if host == "*" {
        return true;
    }
    if ipv4_ok(host) {
        return true;
    }
    let (wild, name) = match host.strip_prefix("*.") {
        Some(rest) => (true, rest),
        None => (false, host),
    };
    let labels: Vec<&str> = name.split('.').collect();
    let label_ok = |l: &str| l.len() <= 63 && !l.is_empty() && !l.starts_with('-') && !l.ends_with('-') && l.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-');
    // A last label of digits only is an address, not a name: `1.2.3` is neither, and `*.` takes a domain, not an address.
    !name.is_empty()
        && name.len() <= 253
        && labels.iter().all(|l| label_ok(l))
        && !labels.last().is_some_and(|l| l.bytes().all(|b| b.is_ascii_digit()))
        && (!wild || !ipv4_ok(name))
}

/// Four decimal numbers from 0 to 255 joined by `.`, without leading zeros (`010` reads as octal in some resolvers).
fn ipv4_ok(s: &str) -> bool {
    let parts: Vec<&str> = s.split('.').collect();
    parts.len() == 4
        && parts.iter().all(|x| !x.is_empty() && x.len() <= 3 && x.bytes().all(|b| b.is_ascii_digit()) && (x.len() == 1 || !x.starts_with('0')) && x.parse::<u16>().is_ok_and(|n| n <= 255))
}

fn permissions(m: &mut M, root: &Value, facts: &mut Facts) {
    let Some(v) = root.get("permissions") else { return };
    let Some(items) = m.array(v, "/permissions", "permissions", 0, PERMISSIONS.len()) else { return };
    for (i, x) in items.iter().enumerate() {
        let ptr = format!("/permissions/{i}");
        let Some(obj) = m.object(x, &ptr, "permissions") else { continue };
        m.keys(obj, &ptr, &["name", "why", "hosts"]);
        m.required(obj, &ptr, &["name"]);
        let Some(name) = obj.get("name").and_then(|n| m.string(n, &format!("{ptr}/name"), "permissions/name")).map(str::to_string) else {
            continue;
        };
        if facts.permissions.contains(&name) {
            m.at(211, obj, &ptr, vec![("key", t(name.clone())), ("why", p("Perm.Twice"))]);
        }
        // The closed list; the `net` of revision 1 became two permissions (revision 3). A `test.` name is a test
        // service's (services-registry 14), which only a test run provides: a Note, L219.
        let test = name.starts_with("test.") && crate::names::service_name_ok(&name);
        let known = test || PERMISSIONS.contains(&name.as_str());
        if name == "net" {
            m.at(211, obj, &ptr, vec![("key", t(name.clone())), ("why", p("Perm.NetSplit"))]);
        } else if test {
            m.at(219, obj, &ptr, vec![("key", t(name.clone()))]);
        } else if !known {
            m.at(211, obj, &ptr, vec![("key", t(name.clone())), ("why", p("Perm.Unknown"))]);
        }
        match obj.get("why") {
            None => m.at(211, obj, &ptr, vec![("key", t(name.clone())), ("why", p("Perm.NoWhy"))]),
            Some(w) => {
                m.line(w, &format!("{ptr}/why"), "permissions/why", 1, 200);
            }
        }
        match (name.as_str(), obj.get("hosts")) {
            ("net.internet", None) => m.at(211, obj, &ptr, vec![("key", t(name.clone())), ("why", p("Perm.NetHosts"))]),
            ("net.internet", Some(h)) => {
                let hp = format!("{ptr}/hosts");
                if !h.is_array() {
                    m.wrong(h, &hp, "permissions/hosts", "Expect.Array");
                } else if h.items().is_empty() || h.items().len() > MAX_HOSTS {
                    m.at(211, h, &hp, vec![("key", t(name.clone())), ("why", p("Perm.HostCount"))]);
                }
                let mut seen: Vec<String> = Vec::new();
                for (j, host) in h.items().iter().enumerate() {
                    let at = format!("{hp}/{j}");
                    let Some(s) = m.string(host, &at, "permissions/hosts") else { continue };
                    if !permission_host_ok(s) {
                        m.wrong(host, &at, "permissions/hosts", "Expect.Host");
                        continue;
                    }
                    let lower = s.to_ascii_lowercase();
                    if seen.contains(&lower) {
                        m.at(211, host, &at, vec![("key", t(name.clone())), ("why", p("Perm.HostTwice"))]);
                    } else {
                        seen.push(lower);
                    }
                }
            }
            (_, Some(h)) => m.at(211, h, &format!("{ptr}/hosts"), vec![("key", t(name.clone())), ("why", p("Perm.HostsOnlyNet"))]),
            _ => {}
        }
        if known && !facts.permissions.contains(&name) {
            facts.permission_at.push((name.clone(), obj.pos, ptr.clone()));
        }
        facts.permissions.insert(name);
    }
}

/// `renderer` (luma-format revision 4): `{"features": [...]}`, the renderer features the mod cannot do without, 1 to 64
/// names, each once. With the game's list (the self-description's `renderer.features`) a name not on it is L204; with
/// it and this device's features (start-check 3), a feature the device lacks is L806 and the mod is off. Without the
/// list L806 is not checked; without the device's features (LumaLink) it is not checked either: the game checks it
/// at every start.
fn renderer(m: &mut M, root: &Value) {
    let Some(v) = root.get("renderer") else { return };
    let Some(obj) = m.object(v, "/renderer", "renderer") else { return };
    m.keys(obj, "/renderer", &["features"]);
    m.required(obj, "/renderer", &["features"]);
    let Some(list) = obj.get("features") else { return };
    let Some(items) = m.array(list, "/renderer/features", "renderer/features", 1, crate::names::MAX_FEATURES) else { return };
    let known = m.ctx.game().and_then(|g| g.names).and_then(|n| n.renderer_features.clone());
    let device: Option<Vec<String>> = m.ctx.game().and_then(|g| g.device_features).map(<[String]>::to_vec);
    let mut seen: BTreeSet<&str> = BTreeSet::new();
    let mut wanted: Vec<(&Value, String, &str)> = Vec::new();
    for (i, item) in items.iter().enumerate() {
        let ip = format!("/renderer/features/{i}");
        let Some(s) = m.string(item, &ip, "renderer/features") else { continue };
        if !crate::names::feature_name_ok(s) {
            m.wrong(item, &ip, "renderer/features", "Expect.FeatureName");
        } else if !seen.insert(s) {
            m.at(204, item, &ip, vec![("key", t("renderer/features")), ("found", t(format!("\"{s}\""))), ("expected", p("Expect.Once"))]);
        } else if known.as_ref().is_some_and(|k| !k.contains(s)) {
            m.wrong(item, &ip, "renderer/features", "Expect.Feature");
        } else {
            wanted.push((item, ip, s));
        }
    }
    if wanted.is_empty() {
        return;
    }
    match (&known, &device) {
        (None, _) => m.ctx.not_checked(806, "Missing.RendererFeatures"),
        (Some(_), None) => m.ctx.not_checked(806, "Missing.Device"),
        (Some(_), Some(have)) => {
            for (item, ip, s) in wanted {
                if !have.iter().any(|h| h == s) {
                    m.at(806, item, &ip, vec![("feature", t(s))]);
                }
            }
        }
    }
}

fn layers(m: &mut M, root: &Value, facts: &mut Facts) {
    let Some(v) = root.get("layers") else {
        // Default: one layer "base" when layers/base exists (luma-format 4).
        if m.ctx.m.items.iter().any(|i| i.name.starts_with("layers/base/") || i.name == "layers/base") {
            facts.layers.push(("base".into(), root.pos, String::new()));
        }
        return;
    };
    let Some(items) = m.array(v, "/layers", "layers", 0, 64) else { return };
    for (i, x) in items.iter().enumerate() {
        let ptr = format!("/layers/{i}");
        let Some(obj) = m.object(x, &ptr, "layers") else { continue };
        m.keys(obj, &ptr, &["name", "when"]);
        m.required(obj, &ptr, &["name"]);
        let Some(name) = obj.get("name").and_then(|n| {
            let s = m.string(n, &format!("{ptr}/name"), "layers/name")?;
            if key_ok(s) {
                Some(s.to_string())
            } else {
                m.wrong(n, &format!("{ptr}/name"), "layers/name", "Expect.Key");
                None
            }
        }) else {
            continue;
        };
        if facts.layers.iter().any(|(l, _, _)| *l == name) {
            m.at(300, obj, &ptr, vec![("layer", t(name.clone())), ("why", p("Layer.Twice"))]);
            continue;
        }
        facts.layers.push((name.clone(), obj.pos, ptr.clone()));
        let Some(when) = obj.get("when") else { continue };
        let wp = format!("{ptr}/when");
        let Some(when) = m.object(when, &wp, "when") else { continue };
        m.keys(when, &wp, &["option", "region", "language", "platform", "code"]);
        if when.members().is_empty() {
            m.at(301, when, &wp, vec![("layer", t(name.clone())), ("why", p("Layer.EmptyWhen"))]);
        }
        if let Some(o) = when.get("option") {
            let op = format!("{wp}/option");
            if let Some(o) = m.object(o, &op, "when/option") {
                for member in o.members() {
                    let mp = format!("{op}/{}", seg(&member.key));
                    let Some(option) = facts.options.iter().find(|f| f.key == member.key).cloned() else {
                        m.ctx.add(301, FILE, Some(member.key_pos), Some(&mp), vec![("layer", t(name.clone())), ("why", p("Layer.NoSuchOption")), ("value", t(member.key.clone()))]);
                        continue;
                    };
                    let wanted: Vec<&Value> = match &member.value.kind {
                        Kind::Array(list) => list.iter().collect(),
                        _ => vec![&member.value],
                    };
                    if wanted.is_empty() {
                        m.at(301, &member.value, &mp, vec![("layer", t(name.clone())), ("why", p("Layer.EmptyList"))]);
                    }
                    for w in wanted {
                        let ok = match option.kind.as_str() {
                            "choice" => w.as_str().is_some_and(|s| option.values.iter().any(|v| v == s)),
                            "toggle" => w.as_bool().is_some(),
                            "number" => w.as_f64().is_some(),
                            "text" => w.as_str().is_some(),
                            _ => false,
                        };
                        if !ok {
                            m.at(301, w, &mp, vec![("layer", t(name.clone())), ("why", p("Layer.ValueNever")), ("value", t(member.key.clone()))]);
                        }
                    }
                }
            }
        }
        for (key, list, expected) in [("region", &REGIONS[..], "Expect.Region"), ("platform", &PLATFORMS[..], "Expect.Platform")] {
            if let Some(x) = when.get(key) {
                let chosen = m.choices(x, &format!("{wp}/{key}"), &format!("when/{key}"), list, expected);
                if key == "region" {
                    for r in chosen {
                        if !facts.regions.iter().any(|f| f == r) {
                            m.at(301, x, &format!("{wp}/{key}"), vec![("layer", t(name.clone())), ("why", p("Layer.RegionNotOfMod")), ("value", t(r))]);
                        }
                    }
                }
            }
        }
        if let Some(x) = when.get("language") {
            let extra: Vec<String> = m.ctx.game().map(|g| g.extra_languages.to_vec()).unwrap_or_default();
            let all: Vec<&str> = LANGUAGES.iter().copied().chain(extra.iter().map(String::as_str)).collect();
            m.choices(x, &format!("{wp}/language"), "when/language", &all, "Expect.Language");
        }
        if let Some(x) = when.get("code") {
            if x.as_bool() != Some(true) {
                m.wrong(x, &format!("{wp}/code"), "when/code", "Expect.True");
            }
        }
    }
}

fn code_key(m: &mut M, root: &Value, facts: &mut Facts) {
    let Some(v) = root.get("code") else { return };
    facts.has_code_key = true;
    let Some(obj) = m.object(v, "/code", "code") else { return };
    m.keys(obj, "/code", &["platforms", "libraries"]);
    m.required(obj, "/code", &["platforms"]);
    if let Some(x) = obj.get("platforms") {
        facts.code_platforms = m.choices(x, "/code/platforms", "code/platforms", &PLATFORMS, "Expect.Platform").iter().map(|s| s.to_string()).collect();
    }
    if let Some(x) = obj.get("libraries") {
        if let Some(libs) = m.object(x, "/code/libraries", "code/libraries") {
            for member in libs.members() {
                let lp = format!("/code/libraries/{}", seg(&member.key));
                if !facts.code_platforms.contains(&member.key) {
                    m.ctx.add(204, FILE, Some(member.key_pos), Some(&lp), vec![("key", t(member.key.clone())), ("found", t(member.key.clone())), ("expected", p("Expect.ListedPlatform"))]);
                }
                if let Some(list) = m.array(&member.value, &lp, &member.key, 1, 16) {
                    for (j, f) in list.iter().enumerate() {
                        let fp = format!("{lp}/{j}");
                        if let Some(s) = m.string(f, &fp, "code/libraries") {
                            if s.contains('/') || s.starts_with("mod.") || super::container::name_problem(s).is_some() {
                                m.wrong(f, &fp, "code/libraries", "Expect.LibraryName");
                            }
                        }
                    }
                }
            }
        }
    }
}

fn media(m: &mut M, root: &Value) {
    let mut named: Vec<String> = Vec::new();
    let mut entries: Vec<(String, &Value, String, bool)> = Vec::new();
    if let Some(v) = root.get("icon") {
        if let Some(s) = m.string(v, "/icon", "icon") {
            entries.push((s.to_string(), v, "/icon".into(), true));
        }
    }
    if let Some(v) = root.get("pictures") {
        if let Some(items) = m.array(v, "/pictures", "pictures", 0, MAX_PICTURES) {
            for (i, x) in items.iter().enumerate() {
                if let Some(s) = m.string(x, &format!("/pictures/{i}"), "pictures") {
                    entries.push((s.to_string(), x, format!("/pictures/{i}"), false));
                }
            }
        }
    }
    for (path, v, ptr, is_icon) in entries {
        let key = if is_icon { "icon" } else { "pictures" };
        named.push(path.clone());
        let fail = |m: &mut M, why: &'static str| m.at(209, v, &ptr, vec![("key", t(key)), ("entry", t(path.clone())), ("why", p(why))]);
        if !path.starts_with("media/") || path.matches('/').count() != 1 {
            fail(m, "Media.NotInMedia");
            continue;
        }
        let Some(index) = m.ctx.m.index_of(&path) else {
            fail(m, "Media.Missing");
            continue;
        };
        let data = match m.ctx.m.keep(index, 64 << 20) {
            Ok(d) => d.to_vec(),
            Err(_) => {
                fail(m, "Media.Unreadable");
                continue;
            }
        };
        match image_size(&data) {
            None => fail(m, if is_icon { "Media.NotPng" } else { "Media.NotPicture" }),
            Some((kind, w, h)) => {
                if is_icon && kind != "png" {
                    fail(m, "Media.NotPng");
                } else if is_icon && (w != h || !(64..=1024).contains(&w)) {
                    fail(m, "Media.IconSize");
                } else if !is_icon && (w == 0 || h == 0 || w > 4096 || h > 4096) {
                    fail(m, "Media.PictureSize");
                }
            }
        }
    }
    for item in m.ctx.m.items.clone() {
        if item.kind == EntryKind::File && item.name.starts_with("media/") && !named.contains(&item.name) {
            m.ctx.add(215, &item.name, None, None, vec![("entry", t(item.name.clone()))]);
        }
    }
}

/// The kind and size of a PNG or JPEG from its header (no decoding).
pub fn image_size(data: &[u8]) -> Option<(&'static str, u32, u32)> {
    if data.len() >= 24 && data.starts_with(b"\x89PNG\r\n\x1a\n") && &data[12..16] == b"IHDR" {
        let w = u32::from_be_bytes(data[16..20].try_into().ok()?);
        let h = u32::from_be_bytes(data[20..24].try_into().ok()?);
        return Some(("png", w, h));
    }
    if data.starts_with(&[0xff, 0xd8]) {
        let mut at = 2usize;
        while at + 4 <= data.len() {
            if data[at] != 0xff {
                return None;
            }
            let marker = data[at + 1];
            if marker == 0xff {
                at += 1;
                continue;
            }
            let len = u16::from_be_bytes([data[at + 2], data[at + 3]]) as usize;
            // SOF0..SOF15 except DHT (C4), JPG (C8) and DAC (CC).
            if (0xc0..=0xcf).contains(&marker) && !matches!(marker, 0xc4 | 0xc8 | 0xcc) {
                let s = data.get(at + 5..at + 9)?;
                let h = u16::from_be_bytes([s[0], s[1]]) as u32;
                let w = u16::from_be_bytes([s[2], s[3]]) as u32;
                return Some(("jpeg", w, h));
            }
            if len < 2 {
                return None;
            }
            at += 2 + len;
        }
    }
    None
}

fn saves(m: &mut M, root: &Value, facts: &Facts) {
    let mut named: Vec<String> = Vec::new();
    if let Some(v) = root.get("saves") {
        if let Some(items) = m.array(v, "/saves", "saves", 0, 16) {
            for (i, x) in items.iter().enumerate() {
                let ptr = format!("/saves/{i}");
                let Some(obj) = m.object(x, &ptr, "saves") else { continue };
                m.keys(obj, &ptr, &["region", "file", "why"]);
                m.required(obj, &ptr, &["region", "file", "why"]);
                if let Some(w) = obj.get("why") {
                    m.line(w, &format!("{ptr}/why"), "saves/why", 1, 200);
                }
                let region = obj.get("region").and_then(|r| m.string(r, &format!("{ptr}/region"), "saves/region")).map(str::to_string);
                let Some(file) = obj.get("file").and_then(|f| m.string(f, &format!("{ptr}/file"), "saves/file")).map(str::to_string) else { continue };
                named.push(file.clone());
                let fail = |m: &mut M, why: &'static str| m.at(216, obj, &ptr, vec![("entry", t(file.clone())), ("why", p(why))]);
                let Some(region) = region else { continue };
                if !REGIONS.contains(&region.as_str()) {
                    fail(m, "Save.UnknownRegion");
                    continue;
                }
                if !facts.regions.contains(&region) {
                    fail(m, "Save.RegionNotOfMod");
                }
                if !file.starts_with(&format!("saves/{region}/")) || file.matches('/').count() != 2 {
                    fail(m, "Save.NotInItsFolder");
                    continue;
                }
                let Some(index) = m.ctx.m.index_of(&file) else {
                    fail(m, "Save.Missing");
                    continue;
                };
                match m.ctx.m.keep(index, 4 << 20) {
                    Ok(data) => {
                        if let Some(why) = save_problem(data) {
                            fail(m, why);
                        }
                    }
                    Err(_) => fail(m, "Save.Unreadable"),
                }
            }
        }
    }
    for item in m.ctx.m.items.clone() {
        if item.kind == EntryKind::File && item.name.starts_with("saves/") && !named.contains(&item.name) {
            m.ctx.add(217, &item.name, None, None, vec![("entry", t(item.name.clone()))]);
        }
    }
}

/// Why bytes are not a save the game reads, as a phrase, or None when they are. A faithful port of the game's own
/// check (`native/petari/include/Game/System/SaveFileFormat.hpp`, `NebulaSaveFormat::validate`), so a save this
/// accepts is one the game's import accepts; the two are tested with the same files (validator contract 7). A save
/// file holds no region, so its region is the folder it is in.
pub fn save_problem(data: &[u8]) -> Option<&'static str> {
    let read16 = |at: usize, big: bool| -> u16 {
        let (a, b) = (data[at], data[at + 1]);
        if big { u16::from_be_bytes([a, b]) } else { u16::from_le_bytes([a, b]) }
    };
    let read32 = |at: usize, big: bool| -> u32 {
        if big {
            (read16(at, true) as u32) << 16 | read16(at + 2, true) as u32
        } else {
            (read16(at + 2, false) as u32) << 16 | read16(at, false) as u32
        }
    };
    if data.len() == 111_360 {
        return Some("Save.Encrypted");
    }
    if data.len() < 16 {
        return Some("Save.Short");
    }
    let little = read32(4, false);
    let big = read32(4, true) == 2;
    if !big && little != 2 && little != 3 {
        return Some("Save.Version");
    }
    let entries = read32(8, big);
    let size = read32(12, big);
    if !(16..=u32::MAX - 31).contains(&size) || ((size as usize + 31) & !31usize) != data.len() {
        return Some("Save.Length");
    }
    if entries == 0 || entries >= 24 || entries > (size - 16) / 16 {
        return Some("Save.Table");
    }
    let table_end = 16 + entries * 16;
    for i in 0..entries {
        let entry = 16 + i as usize * 16;
        let mut name_bytes = 0;
        while name_bytes < 12 && data[entry + name_bytes] != 0 {
            let c = data[entry + name_bytes];
            name_bytes += 1;
            if !(32..=126).contains(&c) || c == b'/' || c == b'\\' || c == b':' {
                return Some("Save.Table");
            }
        }
        if name_bytes == 0 || name_bytes == 12 {
            return Some("Save.Table");
        }
        let start = read32(entry + 12, big);
        let end = if i + 1 < entries { read32(entry + 28, big) } else { size };
        if start < table_end || start > end || end > size || end - start < 4 {
            return Some("Save.Table");
        }
        let file = start as usize;
        if data[file] != 1 {
            return Some("Save.Chunk");
        }
        let mut position = 4usize;
        for _ in 0..data[file + 1] {
            let remaining = (end - start) as usize - position;
            if remaining < 12 {
                return Some("Save.Chunk");
            }
            let chunk = read32(file + position + 8, big) as usize;
            if chunk < 12 || chunk > remaining {
                return Some("Save.Chunk");
            }
            position += chunk;
        }
    }
    let mut sum = 0u32;
    let mut inverse = 0u32;
    let body = &data[4..size as usize];
    for i in (0..body.len().saturating_sub(1)).step_by(2) {
        let word = if big { u16::from_be_bytes([body[i], body[i + 1]]) } else { u16::from_le_bytes([body[i], body[i + 1]]) };
        sum = sum.wrapping_add(word as u32);
        inverse = inverse.wrapping_add((!word) as u32);
    }
    if read32(0, big) != ((sum & 0xffff) << 16 | (inverse & 0xffff)) {
        return Some("Save.Checksum");
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn small_checks() {
        assert!(id_ok("gold-flower") && id_ok("a.b-c") && !id_ok("ab") && !id_ok("Gold") && !id_ok("1abc") && !id_ok("a..b") && !id_ok("abc."));
        assert!(host_ok("api.example.org") && host_ok("*.example.org") && host_ok("127.0.0.1:8080") && !host_ok("a b") && !host_ok("x:0") && !host_ok("-a.org"));
        assert!(["*", "api.example.org", "API.Example.org", "*.example.org", "localhost", "10.0.0.1", "255.255.255.255", "0.0.0.0"].iter().all(|h| permission_host_ok(h)));
        assert!(!["", "*.", "**", "*.*.org", "a.org:80", "10.0.0.1:80", "::1", "[::1]", "1.2.3", "256.0.0.1", "01.2.3.4", "*.10.0.0.1", "-a.org", "a b", "a..org", "a.org."]
            .iter()
            .any(|h| permission_host_ok(h)));
        assert!(url_ok("https://example.org/x?y") && !url_ok("http://example.org") && !url_ok("https://localhost"));
        assert!(spdx_ok("GPL-3.0-only") && spdx_ok("MIT OR (Apache-2.0 AND BSD-3-Clause)") && spdx_ok("GPL-2.0-or-later WITH Classpath-exception-2.0"));
        assert!(spdx_ok("LicenseRef-Mine") && !spdx_ok("MIT OR") && !spdx_ok("(MIT") && !spdx_ok("") && !spdx_ok("MIT AND AND BSD"));
    }

    #[test]
    fn images() {
        let mut png = b"\x89PNG\r\n\x1a\n\0\0\0\rIHDR".to_vec();
        png.extend_from_slice(&256u32.to_be_bytes());
        png.extend_from_slice(&128u32.to_be_bytes());
        assert_eq!(image_size(&png), Some(("png", 256, 128)));
        let jpeg = [0xff, 0xd8, 0xff, 0xe0, 0, 4, 0, 0, 0xff, 0xc0, 0, 11, 8, 0, 100, 0, 200, 3];
        assert_eq!(image_size(&jpeg), Some(("jpeg", 200, 100)));
    }
}
