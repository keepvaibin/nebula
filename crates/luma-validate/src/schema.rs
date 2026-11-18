//! The data schemas a library mod gives for its modules' data (the `kit/<module>/<name>.json` files of the mods that use
//! it): one fixed subset of JSON Schema (2020-12 spelling), read as data and never run. A library ships each schema at
//! `code/<library id>.kit/<module>/<version>.json`; its check reads them and keeps them in its report
//! (`Touches::kit_schemas`), so another mod's check, in LumaLink and in the game, finds them in the library's report.
//!
//! The subset, every keyword with the meaning JSON Schema gives it:
//! - notes, not checked: `$schema`, `$id`, `$comment`, `title`, `description`, `default`;
//! - any value: `type` (one name or a list of `object`, `array`, `string`, `number`, `integer`, `boolean`, `null`),
//!   `enum`, `const`, `oneOf`, `anyOf`, `$ref` (only `#/$defs/<name>`; `$defs` only at the top);
//! - objects: `properties`, `required`, `additionalProperties` (true, false or a schema), `propertyNames`,
//!   `minProperties`, `maxProperties`;
//! - lists: `items` (one schema), `minItems`, `maxItems`, `uniqueItems`;
//! - text: `minLength`, `maxLength` (in characters), `pattern` (anchors `^` and `$` at the ends, literals, `.`,
//!   classes `[...]` with ranges and `^`, `\d \w \s` and their capitals, quantifiers `* + ? {n} {n,} {n,m}` up to
//!   1000; no groups and no `|`);
//! - numbers: `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`.
//!
//! Any other keyword, a value of the wrong form, a `$ref` to no definition, `$ref`/`oneOf`/`anyOf` that lead back to
//! themselves without reading into the value, or a run of more than `MOST_RUN` of them on one value make the schema one
//! the validator does not read (L700 on the library).
//!
//! A check costs at most the schema's nodes times the data's values: each (node, value) pair is checked once, however
//! many `oneOf`, `anyOf` and `$ref` paths reach it, and its faults are said once.

use crate::json::{Kind, Member, Pos, Value};
use crate::Arg;
use std::collections::{BTreeMap, HashMap, HashSet};

const NOTES: [&str; 6] = ["$schema", "$id", "$comment", "title", "description", "default"];
const TYPES: [&str; 7] = ["object", "array", "string", "number", "integer", "boolean", "null"];
const MOST_REPEAT: usize = 1000;
const MOST_PATTERN: usize = 256;
/// The most characters of a value shown in a message.
const SHOWN: usize = 64;
/// The longest run of `$ref`, `oneOf` and `anyOf` steps on one value. Each step is a call on the checker's stack, as each
/// level of the data's nesting (at most json::MAX_DEPTH) is, so the two bound its depth.
const MOST_RUN: usize = 32;
/// The stack of the thread a check runs on: four times what the deepest walk a readable schema makes (json::MAX_DEPTH
/// levels, each with a run of MOST_RUN steps) needed in a build without optimisation (a_deepest_walk_fits_its_stack).
/// Only what is used is taken from the system.
const CHECK_STACK: usize = 16 << 20;

#[derive(Default, Clone, Copy, PartialEq, Eq)]
enum Extra {
    #[default]
    Any,
    No,
    Each(usize),
}

#[derive(Default)]
struct Node {
    types: Option<Vec<&'static str>>,
    properties: BTreeMap<String, usize>,
    required: Vec<String>,
    extra: Extra,
    property_names: Option<usize>,
    min_props: Option<u64>,
    max_props: Option<u64>,
    items: Option<usize>,
    min_items: Option<u64>,
    max_items: Option<u64>,
    unique: bool,
    min_len: Option<u64>,
    max_len: Option<u64>,
    pattern: Option<(String, Pattern)>,
    minimum: Option<f64>,
    maximum: Option<f64>,
    ex_min: Option<f64>,
    ex_max: Option<f64>,
    allowed: Option<Vec<Value>>,
    one_of: Vec<usize>,
    any_of: Vec<usize>,
    reference: Option<usize>,
}

/// A schema read and ready to check data against.
pub struct Schema {
    nodes: Vec<Node>,
    top: usize,
}

/// Why a file is not a schema of the subset: the place in the schema (a JSON pointer) and the keyword there.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Unread {
    pub pointer: String,
    pub what: String,
}

/// One fault of data against a schema: where (pointer, line and column), the phrase that says it (`Kit.*`, the text
/// of L701) and its parameters.
#[derive(Debug, Clone, PartialEq)]
pub struct Problem {
    pub pointer: String,
    pub pos: Pos,
    pub phrase: &'static str,
    pub args: Vec<(&'static str, Arg)>,
}

fn esc(key: &str) -> String {
    key.replace('~', "~0").replace('/', "~1")
}

struct Reader {
    nodes: Vec<Node>,
    /// Each node's place in the schema (a JSON pointer).
    ptrs: Vec<String>,
    defs: BTreeMap<String, usize>,
    refs: Vec<(usize, String, String)>,
}

impl Schema {
    pub fn read(root: &Value) -> Result<Schema, Unread> {
        let mut r = Reader { nodes: Vec::new(), ptrs: Vec::new(), defs: BTreeMap::new(), refs: Vec::new() };
        // Every definition gets its place first, so a `$ref` can name one written after it.
        let mut defs = Vec::new();
        if let Some(value) = root.get("$defs") {
            let Kind::Object(members) = &value.kind else { return Err(Unread { pointer: "/$defs".into(), what: "$defs".into() }) };
            for m in members {
                let at = r.new_node();
                r.defs.insert(m.key.clone(), at);
                defs.push((at, format!("/$defs/{}", esc(&m.key)), &m.value));
            }
        }
        let top = r.new_node();
        r.fill(top, root, "", true)?;
        for (at, ptr, value) in defs {
            r.fill(at, value, &ptr, false)?;
        }
        for (node, name, ptr) in std::mem::take(&mut r.refs) {
            let Some(&target) = r.defs.get(&name) else { return Err(Unread { pointer: ptr, what: "$ref".into() }) };
            r.nodes[node].reference = Some(target);
        }
        // `$ref`, `oneOf` and `anyOf` read the same value: a loop of them would never end, and each is a call on the
        // checker's stack, so a run of them is as deep as it is long. The longest run from each node, each found once.
        let steps: Vec<Vec<usize>> = r.nodes.iter().map(|n| n.reference.iter().chain(&n.one_of).chain(&n.any_of).copied().collect()).collect();
        let mut run: Vec<Option<usize>> = vec![None; steps.len()];
        let mut open = vec![false; steps.len()];
        for start in 0..steps.len() {
            if run[start].is_some() {
                continue;
            }
            open[start] = true;
            let mut path = vec![(start, 0usize)];
            while let Some((at, next)) = path.last_mut() {
                let at = *at;
                if let Some(&step) = steps[at].get(*next) {
                    *next += 1;
                    if open[step] {
                        return Err(Unread { pointer: r.ptrs[step].clone(), what: "$ref".into() });
                    }
                    if run[step].is_none() {
                        open[step] = true;
                        path.push((step, 0));
                    }
                } else {
                    let longest = steps[at].iter().map(|s| run[*s].unwrap_or(0) + 1).max().unwrap_or(0);
                    if longest > MOST_RUN {
                        return Err(Unread { pointer: r.ptrs[at].clone(), what: "$ref".into() });
                    }
                    run[at] = Some(longest);
                    open[at] = false;
                    path.pop();
                }
            }
        }
        Ok(Schema { nodes: r.nodes, top })
    }

    /// Every fault of `value` against the schema, in the order met. The walk runs on a thread of its own whose stack
    /// holds the deepest walk a schema that reads can make, whichever thread asks (if no thread can start, on the
    /// caller's).
    pub fn check(&self, value: &Value) -> Vec<Problem> {
        let walk = || {
            let mut walk = Walk { nodes: &self.nodes, verdicts: HashMap::new(), said: HashSet::new() };
            let mut out = Vec::new();
            walk.step(self.top, value, place(value, false), "", &mut Some(&mut out));
            out
        };
        std::thread::scope(|scope| match std::thread::Builder::new().name("luma-schema-check".into()).stack_size(CHECK_STACK).spawn_scoped(scope, walk) {
            Ok(worker) => worker.join().unwrap_or_else(|panic| std::panic::resume_unwind(panic)),
            Err(_) => walk(),
        })
    }
}

/// Which value a check is of: its address in the data checked, and whether it is the key of the member whose value is
/// there (a key is checked as a string made for the check, with no address of its own).
type Id = (usize, bool);

fn place(value: &Value, key: bool) -> Id {
    (value as *const Value as usize, key)
}

/// One check of data against a schema. Each (node, value) pair is checked at most once, whatever the number of paths
/// that reach it: `oneOf`, `anyOf` and `$ref` ask about the same value again, and asked afresh the work doubled with
/// each level of the data (M-160).
struct Walk<'s> {
    nodes: &'s [Node],
    /// Whether each pair asked about passes.
    verdicts: HashMap<(usize, Id), bool>,
    /// The pairs whose faults are in the list already.
    said: HashSet<(usize, Id)>,
}

/// A bound's arguments: what is counted, how many there are, and the bound.
fn limit(what: &'static str, found: String, name: &'static str, bound: String) -> Vec<(&'static str, Arg)> {
    vec![("what", Arg::Phrase(what)), ("found", Arg::Text(found)), (name, Arg::Text(bound))]
}

/// A fault of the value at `ptr`, into the list when there is one; its words are made only for the list.
fn say(out: &mut Option<&mut Vec<Problem>>, ptr: &str, pointer: &str, pos: Pos, phrase: &'static str, args: impl FnOnce() -> Vec<(&'static str, Arg)>) {
    if let Some(list) = out.as_deref_mut() {
        let mut args = args();
        args.insert(0, ("key", Arg::Text(if ptr.is_empty() { "/".to_string() } else { ptr.to_string() })));
        list.push(Problem { pointer: pointer.to_string(), pos, phrase, args });
    }
}

/// The faults a node finds in a value by itself, without a step to another node: whether there are none, or None when
/// the value is not of the node's type (then nothing more is checked). Without a list it stops at the first. Kept out
/// of the walk's recursion so that its frames stay small.
#[inline(never)]
fn own_faults(node: &Node, value: &Value, ptr: &str, out: &mut Option<&mut Vec<Problem>>) -> Option<bool> {
    let mut clean = true;
    macro_rules! fault {
        ($phrase:expr, $args:expr) => {{
            clean = false;
            if out.is_none() {
                return Some(false);
            }
            say(out, ptr, ptr, value.pos, $phrase, || $args);
        }};
    }
    if let Some(types) = &node.types {
        if !types.iter().any(|t| is_type(value, t)) {
            say(out, ptr, ptr, value.pos, "Kit.Type", || vec![("found", Arg::Phrase(value.type_phrase())), ("expected", Arg::Text(types.join(" | ")))]);
            return None;
        }
    }
    if let Some(allowed) = &node.allowed {
        if !allowed.iter().any(|a| same(a, value)) {
            fault!("Kit.Enum", vec![("found", Arg::Text(show(value))), ("expected", Arg::Text(allowed.iter().map(show).collect::<Vec<_>>().join(", ")))]);
        }
    }
    match &value.kind {
        Kind::Object(members) => {
            if !node.required.is_empty() {
                let keys: HashSet<&str> = members.iter().map(|m| m.key.as_str()).collect();
                for name in &node.required {
                    if !keys.contains(name.as_str()) {
                        fault!("Kit.Required", vec![("name", Arg::Text(name.clone()))]);
                    }
                }
            }
            let n = members.len() as u64;
            if let Some(min) = node.min_props.filter(|m| n < *m) {
                fault!("Kit.Least", limit("Kit.What.Keys", n.to_string(), "min", min.to_string()));
            }
            if let Some(max) = node.max_props.filter(|m| n > *m) {
                fault!("Kit.Most", limit("Kit.What.Keys", n.to_string(), "max", max.to_string()));
            }
        }
        Kind::Array(items) => {
            let n = items.len() as u64;
            if let Some(min) = node.min_items.filter(|m| n < *m) {
                fault!("Kit.Least", limit("Kit.What.Items", n.to_string(), "min", min.to_string()));
            }
            if let Some(max) = node.max_items.filter(|m| n > *m) {
                fault!("Kit.Most", limit("Kit.What.Items", n.to_string(), "max", max.to_string()));
            }
            if node.unique {
                // The first item equal to one before it, each written once in a form equal values share.
                let mut seen = HashSet::new();
                if let Some(item) = items.iter().find(|x| !seen.insert(canonical(x))) {
                    fault!("Kit.Repeated", vec![("found", Arg::Text(show(item)))]);
                }
            }
        }
        Kind::String(s) => {
            let n = s.chars().count() as u64;
            if let Some(min) = node.min_len.filter(|m| n < *m) {
                fault!("Kit.Least", limit("Kit.What.Length", n.to_string(), "min", min.to_string()));
            }
            if let Some(max) = node.max_len.filter(|m| n > *m) {
                fault!("Kit.Most", limit("Kit.What.Length", n.to_string(), "max", max.to_string()));
            }
            if let Some((text, pattern)) = &node.pattern {
                if !pattern.finds(s) {
                    fault!("Kit.Pattern", vec![("found", Arg::Text(show(value))), ("expected", Arg::Text(text.clone()))]);
                }
            }
        }
        Kind::Number(x, written) => {
            if let Some(min) = node.minimum.filter(|m| x < m) {
                fault!("Kit.Least", limit("Kit.What.Value", written.clone(), "min", number(min)));
            }
            if let Some(max) = node.maximum.filter(|m| x > m) {
                fault!("Kit.Most", limit("Kit.What.Value", written.clone(), "max", number(max)));
            }
            if let Some(min) = node.ex_min.filter(|m| x <= m) {
                fault!("Kit.MoreThan", limit("Kit.What.Value", written.clone(), "min", number(min)));
            }
            if let Some(max) = node.ex_max.filter(|m| x >= m) {
                fault!("Kit.LessThan", limit("Kit.What.Value", written.clone(), "max", number(max)));
            }
        }
        _ => {}
    }
    Some(clean)
}

impl Walk<'_> {
    /// A step to a node for a value: with a list, the pair's faults go into it unless they are there already (what it
    /// answers then is not used); without, whether the pair passes.
    fn step(&mut self, node: usize, value: &Value, id: Id, ptr: &str, out: &mut Option<&mut Vec<Problem>>) -> bool {
        match out {
            Some(out) => !self.said.insert((node, id)) || self.at(node, value, id, ptr, Some(&mut **out)),
            None => self.passes(node, value, id),
        }
    }

    /// Whether a pair passes, found once.
    fn passes(&mut self, node: usize, value: &Value, id: Id) -> bool {
        if let Some(&known) = self.verdicts.get(&(node, id)) {
            return known;
        }
        let verdict = self.at(node, value, id, "", None);
        self.verdicts.insert((node, id), verdict);
        verdict
    }

    /// The faults of `value` against node `index`: into `out` when given; without, it stops at the first and answers
    /// whether there was none.
    fn at(&mut self, index: usize, value: &Value, id: Id, ptr: &str, mut out: Option<&mut Vec<Problem>>) -> bool {
        let nodes = self.nodes;
        let node = &nodes[index];
        let collecting = out.is_some();
        let Some(mut clean) = own_faults(node, value, ptr, &mut out) else { return false };
        if !clean && !collecting {
            return false;
        }
        // A step to another node, or a fault of this one's: when there is one, this pair does not pass.
        macro_rules! failed {
            () => {{
                clean = false;
                if !collecting {
                    return false;
                }
            }};
        }
        match &value.kind {
            Kind::Object(members) => {
                for m in members {
                    let child = if collecting { format!("{ptr}/{}", esc(&m.key)) } else { String::new() };
                    if let Some(names) = node.property_names {
                        let as_value = Value { kind: Kind::String(m.key.clone()), pos: m.key_pos };
                        if !self.step(names, &as_value, place(&m.value, true), &child, &mut out) {
                            failed!();
                        }
                    }
                    let schema = match (node.properties.get(&m.key), node.extra) {
                        (Some(&schema), _) | (None, Extra::Each(schema)) => schema,
                        (None, Extra::Any) => continue,
                        (None, Extra::No) => {
                            say(&mut out, ptr, &child, m.key_pos, "Kit.NotAllowed", || vec![("name", Arg::Text(m.key.clone()))]);
                            failed!();
                            continue;
                        }
                    };
                    if !self.step(schema, &m.value, place(&m.value, false), &child, &mut out) {
                        failed!();
                    }
                }
            }
            Kind::Array(items) => {
                if let Some(schema) = node.items {
                    for (i, item) in items.iter().enumerate() {
                        let child = if collecting { format!("{ptr}/{i}") } else { String::new() };
                        if !self.step(schema, item, place(item, false), &child, &mut out) {
                            failed!();
                        }
                    }
                }
            }
            _ => {}
        }
        if let Some(target) = node.reference {
            if !self.step(target, value, id, ptr, &mut out) {
                failed!();
            }
        }
        if !node.one_of.is_empty() {
            let mut matched = 0usize;
            for &branch in &node.one_of {
                matched += usize::from(self.passes(branch, value, id));
            }
            if matched != 1 {
                say(&mut out, ptr, ptr, value.pos, "Kit.OneOf", || vec![("found", Arg::Text(matched.to_string())), ("count", Arg::Text(node.one_of.len().to_string()))]);
                failed!();
            }
        }
        if !node.any_of.is_empty() && !node.any_of.iter().any(|&branch| self.passes(branch, value, id)) {
            say(&mut out, ptr, ptr, value.pos, "Kit.AnyOf", || vec![("count", Arg::Text(node.any_of.len().to_string()))]);
            failed!();
        }
        clean
    }
}

fn is_type(value: &Value, name: &str) -> bool {
    match (&value.kind, name) {
        (Kind::Object(_), "object") | (Kind::Array(_), "array") | (Kind::String(_), "string") | (Kind::Bool(_), "boolean") | (Kind::Null, "null") => true,
        (Kind::Number(..), "number") => true,
        (Kind::Number(x, _), "integer") => x.is_finite() && x.fract() == 0.0,
        _ => false,
    }
}

/// Two JSON values equal as JSON Schema compares them: numbers by value, objects whatever their keys' order.
fn same(a: &Value, b: &Value) -> bool {
    match (&a.kind, &b.kind) {
        (Kind::Null, Kind::Null) => true,
        (Kind::Bool(x), Kind::Bool(y)) => x == y,
        (Kind::Number(x, _), Kind::Number(y, _)) => x == y,
        (Kind::String(x), Kind::String(y)) => x == y,
        (Kind::Array(x), Kind::Array(y)) => x.len() == y.len() && x.iter().zip(y).all(|(p, q)| same(p, q)),
        (Kind::Object(x), Kind::Object(y)) => {
            // Keys are unique: the members in key order pair up when the objects are equal.
            fn by_key(list: &[Member]) -> Vec<&Member> {
                let mut sorted: Vec<&Member> = list.iter().collect();
                sorted.sort_unstable_by(|a, b| a.key.cmp(&b.key));
                sorted
            }
            x.len() == y.len() && by_key(x).iter().zip(by_key(y)).all(|(m, n)| m.key == n.key && same(&m.value, &n.value))
        }
        _ => false,
    }
}

/// A value written so that two values are `same` exactly when they are written alike: numbers by value (0 and -0 as
/// one), members in key order.
fn canonical(v: &Value) -> String {
    fn write(v: &Value, out: &mut String) {
        match &v.kind {
            Kind::Null => out.push('n'),
            Kind::Bool(b) => out.push(if *b { 't' } else { 'f' }),
            Kind::Number(x, _) => out.push_str(&format!("{:?}", if *x == 0.0 { 0.0 } else { *x })),
            Kind::String(s) => out.push_str(&format!("{s:?}")),
            Kind::Array(items) => {
                out.push('[');
                for x in items {
                    write(x, out);
                    out.push(',');
                }
                out.push(']');
            }
            Kind::Object(members) => {
                let mut sorted: Vec<&Member> = members.iter().collect();
                sorted.sort_unstable_by(|a, b| a.key.cmp(&b.key));
                out.push('{');
                for m in sorted {
                    out.push_str(&format!("{:?}:", m.key));
                    write(&m.value, out);
                    out.push(',');
                }
                out.push('}');
            }
        }
    }
    let mut out = String::new();
    write(v, &mut out);
    out
}

fn number(x: f64) -> String {
    if x.fract() == 0.0 && x.abs() < 1e15 {
        format!("{}", x as i64)
    } else {
        format!("{x}")
    }
}

/// A value as compact JSON, cut at SHOWN characters; written only as far as that.
fn show(v: &Value) -> String {
    // Past this many bytes there are more than SHOWN characters.
    const ENOUGH: usize = SHOWN * 4 + 4;
    fn write(v: &Value, out: &mut String) {
        if out.len() > ENOUGH {
            return;
        }
        match &v.kind {
            Kind::Null => out.push_str("null"),
            Kind::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
            Kind::Number(_, text) => out.push_str(text),
            Kind::String(s) => {
                out.push('"');
                for c in s.chars() {
                    if out.len() > ENOUGH {
                        break;
                    }
                    match c {
                        '"' => out.push_str("\\\""),
                        '\\' => out.push_str("\\\\"),
                        c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
                        c => out.push(c),
                    }
                }
                out.push('"');
            }
            Kind::Array(items) => {
                out.push('[');
                for (i, x) in items.iter().enumerate() {
                    if out.len() > ENOUGH {
                        break;
                    }
                    if i > 0 {
                        out.push(',');
                    }
                    write(x, out);
                }
                out.push(']');
            }
            Kind::Object(members) => {
                out.push('{');
                for (i, m) in members.iter().enumerate() {
                    if out.len() > ENOUGH {
                        break;
                    }
                    if i > 0 {
                        out.push(',');
                    }
                    write(&Value { kind: Kind::String(m.key.clone()), pos: m.key_pos }, out);
                    out.push(':');
                    write(&m.value, out);
                }
                out.push('}');
            }
        }
    }
    let mut out = String::new();
    write(v, &mut out);
    if out.chars().count() > SHOWN {
        out = out.chars().take(SHOWN).collect::<String>() + "...";
    }
    out
}

impl Reader {
    fn new_node(&mut self) -> usize {
        self.nodes.push(Node::default());
        self.ptrs.push(String::new());
        self.nodes.len() - 1
    }

    fn child(&mut self, value: &Value, ptr: &str) -> Result<usize, Unread> {
        let at = self.new_node();
        self.fill(at, value, ptr, false)?;
        Ok(at)
    }

    fn fill(&mut self, at: usize, value: &Value, ptr: &str, top: bool) -> Result<(), Unread> {
        let Kind::Object(members) = &value.kind else {
            return Err(Unread { pointer: ptr.to_string(), what: "schema".into() });
        };
        self.ptrs[at] = ptr.to_string();
        let mut node = Node::default();
        for m in members {
            let key = m.key.as_str();
            let v = &m.value;
            let here = format!("{ptr}/{}", esc(key));
            let bad = || Unread { pointer: here.clone(), what: key.to_string() };
            let count = |v: &Value| v.as_i64().filter(|n| *n >= 0).map(|n| n as u64).ok_or_else(bad);
            let number = |v: &Value| v.as_f64().ok_or_else(bad);
            match key {
                k if NOTES.contains(&k) => {}
                "$defs" if top => {}
                "type" => {
                    let names: Vec<&Value> = match &v.kind {
                        Kind::String(_) => vec![v],
                        Kind::Array(list) if !list.is_empty() => list.iter().collect(),
                        _ => return Err(bad()),
                    };
                    let mut types = Vec::new();
                    for n in names {
                        let found = n.as_str().and_then(|s| TYPES.iter().find(|t| **t == s)).ok_or_else(bad)?;
                        if types.contains(found) {
                            return Err(bad());
                        }
                        types.push(*found);
                    }
                    node.types = Some(types);
                }
                "properties" => {
                    let Kind::Object(props) = &v.kind else { return Err(bad()) };
                    for p in props {
                        let child = self.child(&p.value, &format!("{here}/{}", esc(&p.key)))?;
                        node.properties.insert(p.key.clone(), child);
                    }
                }
                "required" => {
                    let Kind::Array(list) = &v.kind else { return Err(bad()) };
                    for x in list {
                        let name = x.as_str().ok_or_else(bad)?.to_string();
                        if node.required.contains(&name) {
                            return Err(bad());
                        }
                        node.required.push(name);
                    }
                }
                "additionalProperties" => {
                    node.extra = match v.kind {
                        Kind::Bool(true) => Extra::Any,
                        Kind::Bool(false) => Extra::No,
                        _ => Extra::Each(self.child(v, &here)?),
                    }
                }
                "propertyNames" => node.property_names = Some(self.child(v, &here)?),
                "minProperties" => node.min_props = Some(count(v)?),
                "maxProperties" => node.max_props = Some(count(v)?),
                "items" => node.items = Some(self.child(v, &here)?),
                "minItems" => node.min_items = Some(count(v)?),
                "maxItems" => node.max_items = Some(count(v)?),
                "uniqueItems" => node.unique = v.as_bool().ok_or_else(bad)?,
                "minLength" => node.min_len = Some(count(v)?),
                "maxLength" => node.max_len = Some(count(v)?),
                "pattern" => {
                    let text = v.as_str().ok_or_else(bad)?;
                    let pattern = Pattern::read(text).ok_or_else(bad)?;
                    node.pattern = Some((text.to_string(), pattern));
                }
                "minimum" => node.minimum = Some(number(v)?),
                "maximum" => node.maximum = Some(number(v)?),
                "exclusiveMinimum" => node.ex_min = Some(number(v)?),
                "exclusiveMaximum" => node.ex_max = Some(number(v)?),
                "enum" => match &v.kind {
                    Kind::Array(list) if !list.is_empty() => node.allowed = Some(list.clone()),
                    _ => return Err(bad()),
                },
                "const" => node.allowed = Some(vec![v.clone()]),
                "oneOf" | "anyOf" => {
                    let list = match &v.kind {
                        Kind::Array(list) if !list.is_empty() => list,
                        _ => return Err(bad()),
                    };
                    let mut branches = Vec::new();
                    for (i, x) in list.iter().enumerate() {
                        branches.push(self.child(x, &format!("{here}/{i}"))?);
                    }
                    if key == "oneOf" {
                        node.one_of = branches;
                    } else {
                        node.any_of = branches;
                    }
                }
                "$ref" => {
                    let name = v.as_str().and_then(|s| s.strip_prefix("#/$defs/")).ok_or_else(bad)?;
                    self.refs.push((at, name.replace("~1", "/").replace("~0", "~"), here.clone()));
                }
                _ => return Err(bad()),
            }
        }
        self.nodes[at] = node;
        Ok(())
    }
}

// ---------------------------------------------------------------- `pattern`

#[derive(Clone)]
enum Atom {
    Any,
    One(char),
    /// Ranges, and whether the class is negated.
    Class(Vec<(char, char)>, bool),
}

impl Atom {
    fn takes(&self, c: char) -> bool {
        match self {
            Atom::Any => c != '\n' && c != '\r',
            Atom::One(x) => *x == c,
            Atom::Class(ranges, negated) => ranges.iter().any(|(a, b)| *a <= c && c <= *b) != *negated,
        }
    }
}

struct Piece {
    atom: Atom,
    min: usize,
    max: usize,
}

/// A `pattern` of the subset: found anywhere in the text unless anchored, as JSON Schema's patterns are.
struct Pattern {
    pieces: Vec<Piece>,
    start: bool,
    end: bool,
}

const DIGIT: [(char, char); 1] = [('0', '9')];
const WORD: [(char, char); 4] = [('a', 'z'), ('A', 'Z'), ('0', '9'), ('_', '_')];
const SPACE: [(char, char); 6] = [(' ', ' '), ('\t', '\t'), ('\n', '\n'), ('\r', '\r'), ('\u{b}', '\u{b}'), ('\u{c}', '\u{c}')];

fn escaped(c: char) -> Option<Atom> {
    Some(match c {
        'd' => Atom::Class(DIGIT.to_vec(), false),
        'D' => Atom::Class(DIGIT.to_vec(), true),
        'w' => Atom::Class(WORD.to_vec(), false),
        'W' => Atom::Class(WORD.to_vec(), true),
        's' => Atom::Class(SPACE.to_vec(), false),
        'S' => Atom::Class(SPACE.to_vec(), true),
        'n' => Atom::One('\n'),
        't' => Atom::One('\t'),
        c if c.is_ascii_punctuation() => Atom::One(c),
        _ => return None,
    })
}

impl Pattern {
    fn read(text: &str) -> Option<Pattern> {
        let c: Vec<char> = text.chars().collect();
        if c.len() > MOST_PATTERN {
            return None;
        }
        let mut i = 0;
        let start = c.first() == Some(&'^');
        if start {
            i = 1;
        }
        let mut end = false;
        let mut pieces = Vec::new();
        while i < c.len() {
            let atom = match c[i] {
                '$' if i + 1 == c.len() => {
                    end = true;
                    break;
                }
                '.' => Atom::Any,
                '\\' => {
                    i += 1;
                    escaped(*c.get(i)?)?
                }
                '[' => {
                    i += 1;
                    let negated = c.get(i) == Some(&'^');
                    if negated {
                        i += 1;
                    }
                    let mut ranges = Vec::new();
                    let mut first = true;
                    loop {
                        let x = *c.get(i)?;
                        if x == ']' && !first {
                            break;
                        }
                        first = false;
                        let low = if x == '\\' {
                            i += 1;
                            match escaped(*c.get(i)?)? {
                                Atom::One(y) => y,
                                Atom::Class(r, false) => {
                                    ranges.extend(r);
                                    i += 1;
                                    continue;
                                }
                                _ => return None,
                            }
                        } else {
                            x
                        };
                        if c.get(i + 1) == Some(&'-') && c.get(i + 2).is_some_and(|y| *y != ']') {
                            let mut high = c[i + 2];
                            i += 2;
                            if high == '\\' {
                                i += 1;
                                high = match escaped(*c.get(i)?)? {
                                    Atom::One(y) => y,
                                    _ => return None,
                                };
                            }
                            if high < low {
                                return None;
                            }
                            ranges.push((low, high));
                        } else {
                            ranges.push((low, low));
                        }
                        i += 1;
                    }
                    Atom::Class(ranges, negated)
                }
                '(' | ')' | '|' | '{' | '}' | '*' | '+' | '?' | '^' | '$' | ']' => return None,
                x => Atom::One(x),
            };
            i += 1;
            let (min, max) = match c.get(i) {
                Some('*') => {
                    i += 1;
                    (0, usize::MAX)
                }
                Some('+') => {
                    i += 1;
                    (1, usize::MAX)
                }
                Some('?') => {
                    i += 1;
                    (0, 1)
                }
                Some('{') => {
                    let close = c[i..].iter().position(|x| *x == '}')? + i;
                    let inside: String = c[i + 1..close].iter().collect();
                    i = close + 1;
                    let num = |s: &str| s.parse::<usize>().ok().filter(|n| *n <= MOST_REPEAT && (s == "0" || !s.starts_with('0')));
                    match inside.split_once(',') {
                        None => {
                            let n = num(&inside)?;
                            (n, n)
                        }
                        Some((a, "")) => (num(a)?, usize::MAX),
                        Some((a, b)) => {
                            let (a, b) = (num(a)?, num(b)?);
                            if a > b {
                                return None;
                            }
                            (a, b)
                        }
                    }
                }
                _ => (1, 1),
            };
            // A lazy or possessive mark, or a quantifier on a quantifier, is outside the subset.
            if matches!(c.get(i), Some('*' | '+' | '?' | '{')) {
                return None;
            }
            pieces.push(Piece { atom, min, max });
        }
        Some(Pattern { pieces, start, end })
    }

    /// Whether the pattern is found in `text`: the positions it can have reached after each piece, each piece taking
    /// from min to max characters it accepts (linear in the text for each piece).
    fn finds(&self, text: &str) -> bool {
        let s: Vec<char> = text.chars().collect();
        let n = s.len();
        let mut reach = vec![!self.start; n + 1];
        reach[0] = true;
        for piece in &self.pieces {
            // run[p]: how many characters from p the atom takes in a row.
            let mut run = vec![0usize; n + 1];
            for p in (0..n).rev() {
                run[p] = if piece.atom.takes(s[p]) { run[p + 1] + 1 } else { 0 };
            }
            let mut marks = vec![0i64; n + 2];
            for p in 0..=n {
                if !reach[p] || run[p] < piece.min {
                    continue;
                }
                let high = p + run[p].min(piece.max);
                marks[p + piece.min] += 1;
                marks[high + 1] -= 1;
            }
            let mut open = 0i64;
            for p in 0..=n {
                open += marks[p];
                reach[p] = open > 0;
            }
        }
        if self.end {
            reach[n]
        } else {
            reach.iter().any(|r| *r)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn schema(text: &str) -> Result<Schema, Unread> {
        Schema::read(&crate::json::parse(text.as_bytes()).unwrap())
    }

    fn problems(s: &Schema, data: &str) -> Vec<(&'static str, String)> {
        s.check(&crate::json::parse(data.as_bytes()).unwrap()).into_iter().map(|p| (p.phrase, p.pointer)).collect()
    }

    #[test]
    fn patterns_of_the_subset() {
        let p = |t: &str| Pattern::read(t).unwrap();
        assert!(p("^[A-Za-z0-9_]{1,47}$").finds("start_1"));
        assert!(!p("^[A-Za-z0-9_]{1,47}$").finds(""));
        assert!(!p("^[A-Za-z0-9_]{1,47}$").finds("a b"));
        assert!(p("^[A-Za-z0-9_.]{0,63}$").finds(""));
        assert!(p("b+c").finds("aabbbcd"));
        assert!(!p("^b+c").finds("aabbbcd"));
        assert!(p("^a\\.b?$").finds("a."));
        assert!(p("^[^x]*$").finds("abc") && !p("^[^x]*$").finds("axc"));
        assert!(p("^\\d{3}$").finds("123") && !p("^\\d{3}$").finds("12"));
        for bad in ["(a)", "a|b", "a{2000}", "a**", "a+?", "[z-a]", "[abc", "\\q", "a{3,1}"] {
            assert!(Pattern::read(bad).is_none(), "{bad}");
        }
    }

    #[test]
    fn data_is_checked_by_each_keyword() {
        let s = schema(
            r##"{"type": "object", "additionalProperties": false, "required": ["look"],
                "properties": {
                  "look": {"type": "string", "minLength": 1, "maxLength": 4, "pattern": "^[a-z]+$"},
                  "scale": {"type": "number", "minimum": 0.1, "maximum": 10, "exclusiveMaximum": 9},
                  "timer": {"type": ["integer", "null"]},
                  "base": {"enum": ["Normal", "Bee"]},
                  "moves": {"type": "array", "maxItems": 2, "uniqueItems": true, "items": {"$ref": "#/$defs/move"}},
                  "drops": {"oneOf": [{"type": "null"}, {"type": "object", "required": ["Coins"]}]},
                  "nodes": {"type": "object", "propertyNames": {"pattern": "^[a-z]+$"}, "minProperties": 1}
                },
                "$defs": {"move": {"type": "string", "minLength": 1}}}"##,
        )
        .unwrap();
        assert!(problems(&s, r#"{"look": "ab", "scale": 1, "timer": null, "base": "Bee", "moves": ["a"], "drops": null, "nodes": {"a": 1}}"#).is_empty());
        let got = problems(
            &s,
            r#"{"look": "AB", "scale": 9.5, "timer": 1.5, "base": "Boo", "moves": ["a", "a", ""], "drops": {"Stars": 1}, "nodes": {"B": 1}, "x": 1}"#,
        );
        let want: Vec<(&str, &str)> = vec![
            ("Kit.Pattern", "/look"),
            ("Kit.LessThan", "/scale"),
            ("Kit.Type", "/timer"),
            ("Kit.Enum", "/base"),
            ("Kit.Most", "/moves"),
            ("Kit.Repeated", "/moves"),
            ("Kit.Least", "/moves/2"),
            ("Kit.OneOf", "/drops"),
            ("Kit.Pattern", "/nodes/B"),
            ("Kit.NotAllowed", "/x"),
        ];
        assert_eq!(got.iter().map(|(a, b)| (*a, b.as_str())).collect::<Vec<_>>(), want);
        assert_eq!(problems(&s, "{}"), vec![("Kit.Required", String::new())]);
    }

    #[test]
    fn only_the_subset_reads() {
        for (text, at) in [
            (r#"{"if": {}}"#, "/if"),
            (r#"{"properties": {"a": {"type": "word"}}}"#, "/properties/a/type"),
            (r##"{"$ref": "#/$defs/none"}"##, "/$ref"),
            (r#"{"pattern": "(a|b)"}"#, "/pattern"),
            (r#"{"items": {"$defs": {}}}"#, "/items/$defs"),
            (r#"{"minLength": -1}"#, "/minLength"),
        ] {
            assert_eq!(schema(text).err().map(|u| u.pointer), Some(at.to_string()), "{text}");
        }
        // A loop of references that never reads into the value.
        assert!(schema(r##"{"$ref": "#/$defs/a", "$defs": {"a": {"oneOf": [{"$ref": "#/$defs/a"}]}}}"##).is_err());
        // A tree is no loop: each step reads a value inside.
        assert!(schema(r##"{"$ref": "#/$defs/n", "$defs": {"n": {"type": "object", "additionalProperties": {"$ref": "#/$defs/n"}}}}"##).is_ok());
    }

    /// M-160: branches that reach the same node again, checked against nested data: each (node, value) pair is checked
    /// once and its faults said once. Asked afresh, each of these cost about 2^62 checks.
    #[test]
    fn each_pair_is_checked_once() {
        let started = std::time::Instant::now();
        let nested = |n: usize, leaf: &str| format!("{}{leaf}{}", "[".repeat(n), "]".repeat(n));
        // The finding's: two branches that both read on.
        let s = schema(r##"{"$defs": {"x": {"oneOf": [{"items": {"$ref": "#/$defs/x"}}, {"items": {"$ref": "#/$defs/x"}}]}}, "$ref": "#/$defs/x"}"##).unwrap();
        assert_eq!(problems(&s, &nested(62, "")), vec![("Kit.OneOf", String::new())]);
        // A first branch that reads on, then fails.
        let s = schema(
            r##"{"$defs": {"x": {"anyOf": [{"items": {"$ref": "#/$defs/x"}, "$ref": "#/$defs/no"}, {"items": {"$ref": "#/$defs/x"}}]}, "no": {"type": "null"}},
                "$ref": "#/$defs/x"}"##,
        )
        .unwrap();
        assert!(problems(&s, &nested(62, "")).is_empty());
        // No branches: two `$ref` paths to each level's items, and one fault at the bottom, said once.
        let s = schema(
            r##"{"$defs": {"x": {"type": "array", "items": {"$ref": "#/$defs/x"}, "$ref": "#/$defs/y"}, "y": {"items": {"$ref": "#/$defs/x"}}},
                "$ref": "#/$defs/x"}"##,
        )
        .unwrap();
        assert_eq!(problems(&s, &nested(62, "1")), vec![("Kit.Type", "/0".repeat(62))]);
        assert!(started.elapsed() < std::time::Duration::from_secs(5), "{:?}", started.elapsed());
    }

    /// A run of `$ref`, `oneOf` and `anyOf` on one value reads up to MOST_RUN steps; one more is refused, as a loop is.
    #[test]
    fn a_run_on_one_value_is_bounded() {
        let run = |steps: usize| {
            let defs: Vec<String> = (1..steps).map(|i| format!("\"d{i}\": {{\"$ref\": \"#/$defs/d{}\"}}", i + 1)).chain([format!("\"d{steps}\": {{}}")]).collect();
            schema(&format!("{{\"$defs\": {{{}}}, \"$ref\": \"#/$defs/d1\"}}", defs.join(", ")))
        };
        assert!(run(MOST_RUN).is_ok());
        assert_eq!(run(MOST_RUN + 1).err(), Some(Unread { pointer: String::new(), what: "$ref".into() }));
        let looped = schema(r##"{"$ref": "#/$defs/a", "$defs": {"a": {"anyOf": [{"type": "null"}, {"$ref": "#/$defs/a"}]}}}"##);
        assert_eq!(looped.err().map(|u| u.pointer), Some("/$defs/a".to_string()));
    }

    /// The deepest walk a schema that reads can make: a run of MOST_RUN steps on each level of the deepest data that
    /// reads. It fits the stack the check gives itself (an overflow would end the test's process). On the caller's
    /// stack it needed 2 to 4 MiB in a build without optimisation, 1 to 2 MiB with it (measured 11-18).
    #[test]
    fn a_deepest_walk_fits_its_stack() {
        let k = MOST_RUN / 2;
        let mut defs: Vec<String> = (1..k).map(|i| format!("\"d{i}\": {{\"oneOf\": [{{\"$ref\": \"#/$defs/d{}\"}}]}}", i + 1)).collect();
        defs.push(format!("\"d{k}\": {{\"items\": {{\"$ref\": \"#/$defs/d1\"}}, \"$ref\": \"#/$defs/end\"}}, \"end\": {{}}"));
        let s = schema(&format!("{{\"$defs\": {{{}}}, \"$ref\": \"#/$defs/d1\"}}", defs.join(", "))).unwrap();
        let deepest = (1..=crate::json::MAX_DEPTH).rev().map(|n| format!("{}{}", "[".repeat(n), "]".repeat(n))).find(|d| crate::json::parse(d.as_bytes()).is_ok()).unwrap();
        assert!(deepest.len() >= 2 * (crate::json::MAX_DEPTH - 1));
        assert!(problems(&s, &deepest).is_empty());
    }

    /// `uniqueItems` and `enum` compare as JSON Schema does (numbers by value, members whatever their order), and
    /// `uniqueItems` costs one pass: 200 000 items at once.
    #[test]
    fn values_compare_by_what_they_are() {
        let s = schema(r#"{"uniqueItems": true}"#).unwrap();
        for repeated in ["[1, 1.0]", "[0, -0]", r#"[{"a": 1, "b": [2]}, {"b": [2], "a": 1}]"#, r#"["x", "y", "x"]"#] {
            assert_eq!(problems(&s, repeated), vec![("Kit.Repeated", String::new())], "{repeated}");
        }
        for unique in [r#"["1", 1]"#, "[[1, 2], [2, 1]]", r#"[{"a": 1}, {"a": 1, "b": 1}]"#, "[null, false, 0, \"\", [], {}]"] {
            assert!(problems(&s, unique).is_empty(), "{unique}");
        }
        let many = format!("[{}]", (0..200_000).map(|i| i.to_string()).collect::<Vec<_>>().join(","));
        let started = std::time::Instant::now();
        assert!(problems(&s, &many).is_empty());
        assert!(started.elapsed() < std::time::Duration::from_secs(5), "{:?}", started.elapsed());
        let s = schema(r#"{"enum": [{"a": 1, "b": 2}]}"#).unwrap();
        assert!(problems(&s, r#"{"b": 2, "a": 1}"#).is_empty());
        assert_eq!(problems(&s, r#"{"b": 2}"#), vec![("Kit.Enum", String::new())]);
    }
}
