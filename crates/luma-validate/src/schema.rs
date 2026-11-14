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
//! Any other keyword, a value of the wrong form, a `$ref` to no definition, or `$ref`/`oneOf`/`anyOf` that lead back to
//! themselves without reading into the value make the schema one the validator does not read (L700 on the library).

use crate::json::{Kind, Pos, Value};
use crate::Arg;
use std::collections::BTreeMap;

const NOTES: [&str; 6] = ["$schema", "$id", "$comment", "title", "description", "default"];
const TYPES: [&str; 7] = ["object", "array", "string", "number", "integer", "boolean", "null"];
const MOST_REPEAT: usize = 1000;
const MOST_PATTERN: usize = 256;
/// The most characters of a value shown in a message.
const SHOWN: usize = 64;

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
    properties: Vec<(String, usize)>,
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
    defs: BTreeMap<String, usize>,
    refs: Vec<(usize, String, String)>,
}

impl Schema {
    pub fn read(root: &Value) -> Result<Schema, Unread> {
        let mut r = Reader { nodes: Vec::new(), defs: BTreeMap::new(), refs: Vec::new() };
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
        // `$ref`, `oneOf` and `anyOf` read the same value: a loop of them would never end.
        let n = r.nodes.len();
        for start in 0..n {
            let mut stack = vec![start];
            let mut seen = vec![false; n];
            while let Some(at) = stack.pop() {
                let node = &r.nodes[at];
                for &next in node.reference.iter().chain(&node.one_of).chain(&node.any_of) {
                    if next == start {
                        return Err(Unread { pointer: String::new(), what: "$ref".into() });
                    }
                    if !seen[next] {
                        seen[next] = true;
                        stack.push(next);
                    }
                }
            }
        }
        Ok(Schema { nodes: r.nodes, top })
    }

    /// Every fault of `value` against the schema, in the order met.
    pub fn check(&self, value: &Value) -> Vec<Problem> {
        let mut out = Vec::new();
        self.at(self.top, value, "", &mut out);
        out
    }

    fn passes(&self, node: usize, value: &Value, ptr: &str) -> bool {
        let mut out = Vec::new();
        self.at(node, value, ptr, &mut out);
        out.is_empty()
    }

    fn at(&self, index: usize, value: &Value, ptr: &str, out: &mut Vec<Problem>) {
        let node = &self.nodes[index];
        let shown_ptr = if ptr.is_empty() { "/".to_string() } else { ptr.to_string() };
        let say = |out: &mut Vec<Problem>, phrase: &'static str, mut args: Vec<(&'static str, Arg)>| {
            args.insert(0, ("key", Arg::Text(shown_ptr.clone())));
            out.push(Problem { pointer: ptr.to_string(), pos: value.pos, phrase, args });
        };
        if let Some(types) = &node.types {
            if !types.iter().any(|t| is_type(value, t)) {
                say(out, "Kit.Type", vec![("found", Arg::Phrase(value.type_phrase())), ("expected", Arg::Text(types.join(" | ")))]);
                return;
            }
        }
        if let Some(allowed) = &node.allowed {
            if !allowed.iter().any(|a| same(a, value)) {
                let list: Vec<String> = allowed.iter().map(show).collect();
                say(out, "Kit.Enum", vec![("found", Arg::Text(show(value))), ("expected", Arg::Text(list.join(", ")))]);
            }
        }
        let bound = |what: &'static str, found: String| move |phrase: &'static str, limit: (&'static str, String)| {
            (phrase, vec![("what", Arg::Phrase(what)), ("found", Arg::Text(found.clone())), (limit.0, Arg::Text(limit.1))])
        };
        match &value.kind {
            Kind::Object(members) => {
                for name in &node.required {
                    if !members.iter().any(|m| &m.key == name) {
                        say(out, "Kit.Required", vec![("name", Arg::Text(name.clone()))]);
                    }
                }
                let n = members.len() as u64;
                let b = bound("Kit.What.Keys", n.to_string());
                if let Some(min) = node.min_props.filter(|m| n < *m) {
                    let (ph, a) = b("Kit.Least", ("min", min.to_string()));
                    say(out, ph, a);
                }
                if let Some(max) = node.max_props.filter(|m| n > *m) {
                    let (ph, a) = b("Kit.Most", ("max", max.to_string()));
                    say(out, ph, a);
                }
                for m in members {
                    let child = format!("{ptr}/{}", esc(&m.key));
                    if let Some(names) = node.property_names {
                        let as_value = Value { kind: Kind::String(m.key.clone()), pos: m.key_pos };
                        self.at(names, &as_value, &child, out);
                    }
                    match node.properties.iter().find(|(k, _)| *k == m.key) {
                        Some((_, schema)) => self.at(*schema, &m.value, &child, out),
                        None => match node.extra {
                            Extra::Any => {}
                            Extra::No => out.push(Problem {
                                pointer: child.clone(),
                                pos: m.key_pos,
                                phrase: "Kit.NotAllowed",
                                args: vec![("key", Arg::Text(shown_ptr.clone())), ("name", Arg::Text(m.key.clone()))],
                            }),
                            Extra::Each(schema) => self.at(schema, &m.value, &child, out),
                        },
                    }
                }
            }
            Kind::Array(items) => {
                let n = items.len() as u64;
                let b = bound("Kit.What.Items", n.to_string());
                if let Some(min) = node.min_items.filter(|m| n < *m) {
                    let (ph, a) = b("Kit.Least", ("min", min.to_string()));
                    say(out, ph, a);
                }
                if let Some(max) = node.max_items.filter(|m| n > *m) {
                    let (ph, a) = b("Kit.Most", ("max", max.to_string()));
                    say(out, ph, a);
                }
                if node.unique {
                    if let Some(j) = (1..items.len()).find(|&j| items[..j].iter().any(|x| same(x, &items[j]))) {
                        say(out, "Kit.Repeated", vec![("found", Arg::Text(show(&items[j])))]);
                    }
                }
                if let Some(schema) = node.items {
                    for (i, item) in items.iter().enumerate() {
                        self.at(schema, item, &format!("{ptr}/{i}"), out);
                    }
                }
            }
            Kind::String(s) => {
                let n = s.chars().count() as u64;
                let b = bound("Kit.What.Length", n.to_string());
                if let Some(min) = node.min_len.filter(|m| n < *m) {
                    let (ph, a) = b("Kit.Least", ("min", min.to_string()));
                    say(out, ph, a);
                }
                if let Some(max) = node.max_len.filter(|m| n > *m) {
                    let (ph, a) = b("Kit.Most", ("max", max.to_string()));
                    say(out, ph, a);
                }
                if let Some((text, pattern)) = &node.pattern {
                    if !pattern.finds(s) {
                        say(out, "Kit.Pattern", vec![("found", Arg::Text(show(value))), ("expected", Arg::Text(text.clone()))]);
                    }
                }
            }
            Kind::Number(x, written) => {
                let b = bound("Kit.What.Value", written.clone());
                if let Some(min) = node.minimum.filter(|m| x < m) {
                    let (ph, a) = b("Kit.Least", ("min", number(min)));
                    say(out, ph, a);
                }
                if let Some(max) = node.maximum.filter(|m| x > m) {
                    let (ph, a) = b("Kit.Most", ("max", number(max)));
                    say(out, ph, a);
                }
                if let Some(min) = node.ex_min.filter(|m| x <= m) {
                    let (ph, a) = b("Kit.MoreThan", ("min", number(min)));
                    say(out, ph, a);
                }
                if let Some(max) = node.ex_max.filter(|m| x >= m) {
                    let (ph, a) = b("Kit.LessThan", ("max", number(max)));
                    say(out, ph, a);
                }
            }
            _ => {}
        }
        if let Some(target) = node.reference {
            self.at(target, value, ptr, out);
        }
        if !node.one_of.is_empty() {
            let matched = node.one_of.iter().filter(|b| self.passes(**b, value, ptr)).count();
            if matched != 1 {
                out.push(Problem {
                    pointer: ptr.to_string(),
                    pos: value.pos,
                    phrase: "Kit.OneOf",
                    args: vec![("key", Arg::Text(shown_ptr.clone())), ("found", Arg::Text(matched.to_string())), ("count", Arg::Text(node.one_of.len().to_string()))],
                });
            }
        }
        if !node.any_of.is_empty() && !node.any_of.iter().any(|b| self.passes(*b, value, ptr)) {
            out.push(Problem {
                pointer: ptr.to_string(),
                pos: value.pos,
                phrase: "Kit.AnyOf",
                args: vec![("key", Arg::Text(shown_ptr)), ("count", Arg::Text(node.any_of.len().to_string()))],
            });
        }
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
        (Kind::Object(x), Kind::Object(y)) => x.len() == y.len() && x.iter().all(|m| y.iter().any(|n| n.key == m.key && same(&m.value, &n.value))),
        _ => false,
    }
}

fn number(x: f64) -> String {
    if x.fract() == 0.0 && x.abs() < 1e15 {
        format!("{}", x as i64)
    } else {
        format!("{x}")
    }
}

/// A value as compact JSON, cut at SHOWN characters.
fn show(v: &Value) -> String {
    fn write(v: &Value, out: &mut String) {
        match &v.kind {
            Kind::Null => out.push_str("null"),
            Kind::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
            Kind::Number(_, text) => out.push_str(text),
            Kind::String(s) => {
                out.push('"');
                for c in s.chars() {
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
                        node.properties.push((p.key.clone(), child));
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
}
