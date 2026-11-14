//! A mod's lines and talk flows, cooked into a language's messages (message.bmg and messageid.tbl of
//! `<Language>/MessageData/Message.arc`), which the game then reads as its own: MessageData finds a new line by its id,
//! TalkNodeCtrl walks a new flow, nothing is looked up from mods while playing.
//!
//! The words are the game's text with its message tags in brackets, as Nebula reads them everywhere: the words of
//! [`TAG_WORDS`], the SDK's table (native/include/luma_text_tags.h), and `[tag G T u...]` for any other tag; `[[` is one
//! `[`. [`decode_text`] writes a
//! game message in those words, so one read out and put back is the game's own.
//!
//! A merge file (`merge/<Language>/MessageData/Message.arc/message.bmg.json` for one language,
//! `merge/MessageData/messages.json` for every language the game has):
//!
//! ```json
//! {
//!   "lines": {
//!     "AstroGalaxy_Tico034": "New words for a line of the game: its talk settings stay",
//!     "MyNpc_Hello": { "default": "Hi, [player]!", "fr": "Salut, [player] !", "like": "AstroGalaxy_Tico034" },
//!     "MyNpc_Ask": { "default": "Want a star?", "talk": "flow", "balloon": 0 }
//!   },
//!   "flows": {
//!     "MyNpc_Hello": "MyNpc_Ask",
//!     "MyNpc_Ask": { "ask": "select", "param": 0, "yes": { "event": "func", "value": 1, "next": "MyNpc_Yes" }, "no": "MyNpc_No" }
//!   }
//! }
//! ```
//!
//! A line's words: a string for every language, or an object of words by language as luma.json's `text` names them
//! (`fr-CA`, then `fr`: luma.cpp language_code), by the game's language folder (`UsFrench`), and `default`. A new line takes the talk settings of the game line it is `like`, else an ordinary talk;
//! `talk` (normal, short, event, compose, flow, null), `balloon`, `camera` (normal, event, none), `camera_set`, `sound`,
//! `area` and `already_read` set them one by one.
//!
//! A flow says what comes after a line: another line, `null` (the talk ends), a branch (`ask`: one of the game's
//! conditions, its `param`, then `yes` and `no`; a `select` the player answers takes its choices' words in `choices`, or
//! one of the game's 19 by number in `param`), or an event (`event`: func, func_stop, none, skip, anime, switch_a,
//! switch_b, kill, its `value`, then `next`). The conditions: select (the player chooses), func (the talker's own test),
//! near_player, switch_a, switch_b, player_normal, player_bee, player_boo, power_star_appeared, already_done, player_luigi,
//! galaxy_result, time_keep_demo, message_read (TalkNodeCtrl's BranchKind and EventKind, in their order).

use super::message::{Message, Nmsg, Node, NONE};
use super::table::{column_hash, Cell, Kind, TableIr};
use anyhow::{bail, Context, Result};
use serde_json::{Map, Value};
use std::collections::HashMap;

const TAG: u16 = 0x1A;
const TAG_UNITS_MAX: usize = 124;

/// A message's units from `start` to its end: the first 0 that is not inside a tag (a tag's units hold 0s: its tag
/// number, a colour reset), as the game's text walk reads them (MessageEditorMessageTag::getSkipLength).
pub fn units_at(text: &[u16], start: usize) -> &[u16] {
    let mut at = start.min(text.len());
    while at < text.len() && text[at] != 0 {
        at += if text[at] == TAG && at + 1 < text.len() && text[at + 1] != 0 { usize::from(text[at + 1] & 0xFF).max(2) / 2 } else { 1 };
    }
    &text[start.min(text.len())..at.min(text.len())]
}

/// One message's units (to its 0, without it) as mod text.
pub fn decode_text(units: &[u16]) -> String {
    let mut out = String::new();
    let mut i = 0;
    while i < units.len() {
        let unit = units[i];
        if unit == TAG && i + 2 < units.len() {
            let length = usize::from(units[i + 1] & 0xFF);
            let group = units[i + 1] >> 8;
            if length >= 6 && length % 2 == 0 && i + length / 2 <= units.len() {
                out.push_str(&tag_words(group, units[i + 2], &units[i + 3..i + length / 2]));
                i += length / 2;
                continue;
            }
        }
        // A character, or a surrogate pair.
        let end = if (0xD800..0xDC00).contains(&unit) && units.get(i + 1).is_some_and(|u| (0xDC00..0xE000).contains(u)) { i + 2 } else { i + 1 };
        let text = String::from_utf16_lossy(&units[i..end]);
        out.push_str(if text == "[" { "[[" } else { &text });
        i = end;
    }
    out
}

/// What follows a tag's word, and what it becomes (luma_text_tags.h LumaTextArg).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Arg {
    /// No word, no parameter.
    None,
    /// A number: one parameter unit.
    Unit,
    /// A number: the tag number itself.
    Tag,
    /// A name of printable ASCII without brackets: one unit a character.
    Name,
    /// A number: one unit, the value in its high byte.
    High,
    /// No word; one parameter unit 0.
    Zero,
    /// Two numbers: the tag number, then which of the game's arguments (units 0, 0, 0, n).
    Index,
}

/// One row of the message tags' table: word, group, tag number, argument, the argument's largest value, what it does.
pub struct TagWord {
    pub word: &'static str,
    pub group: u16,
    pub tag: u16,
    pub arg: Arg,
    pub most: u16,
    pub means: &'static str,
}

const fn row(word: &'static str, group: u16, tag: u16, arg: Arg, most: u16, means: &'static str) -> TagWord {
    TagWord { word, group, tag, arg, most, means }
}

/// The SDK's table (native/include/luma_text_tags.h), row for row; a test reads the header and fails on any difference.
pub const TAG_WORDS: [TagWord; 17] = [
    row("wait", 1, 0, Arg::Unit, 0xFFFF, "stops the text for n frames"),
    row("page", 1, 1, Arg::None, 0, "ends the box; the rest comes on the next press"),
    row("middle", 1, 2, Arg::None, 0, "centres the text in the box from top to bottom"),
    row("center", 1, 3, Arg::None, 0, "centres the line"),
    row("sound", 2, 0, Arg::Name, TAG_UNITS_MAX as u16, "plays the system sound NAME when the text reaches it"),
    row("icon", 3, 0, Arg::Tag, 0xFFFF, "picture n of the game's picture font (buttons, stars, coins)"),
    row("small", 4, 0, Arg::None, 0, "text three quarters of its size from here"),
    row("normal", 4, 1, Arg::None, 0, "text at its own size from here"),
    row("large", 4, 2, Arg::None, 0, "text one and a half times its size from here"),
    row("player", 5, 0, Arg::Zero, 0, "the player's name (Mario or Luigi)"),
    row("player_name", 5, 0, Arg::High, 0xFF, "the player's name message n (System_PlayerName0n, 1n for Luigi)"),
    row("number", 6, 0, Arg::Index, 0xFFFF, "the game's number argument i; tag t 5 to 9 pads it to 2 to 6 digits"),
    row("string", 7, 0, Arg::Index, 0xFFFF, "the game's text argument i"),
    row("race_time", 9, 5, Arg::None, 0, "the time of the race being run"),
    row("best_time", 9, 0, Arg::Tag, 0xFFFF, "the best time of race n"),
    row("font", 10, 0, Arg::Name, TAG_UNITS_MAX as u16, "the text in the game's font NAME from here"),
    row("color", 255, 0, Arg::High, 0xFF, "the game's text colour n from here; 0 goes back to the text's own"),
];

fn name_unit(unit: u16) -> bool {
    (0x21..0x7F).contains(&unit) && unit != u16::from(b'[') && unit != u16::from(b']')
}

/// The table's row of a tag (luma_text_tags.h luma_text_tag_row): an exact row first, else the group's row whose
/// argument is the tag number.
fn row_of(group: u16, id: u16, params: &[u16]) -> Option<&'static TagWord> {
    let mut by_tag = None;
    for t in TAG_WORDS.iter().filter(|t| t.group == group) {
        let exact = match t.arg {
            Arg::None => t.tag == id && params.is_empty(),
            Arg::Zero => t.tag == id && params == [0],
            Arg::Unit => t.tag == id && params.len() == 1 && params[0] <= t.most,
            Arg::High => t.tag == id && params.len() == 1 && params[0] & 0xFF == 0 && params[0] >> 8 <= t.most,
            Arg::Name => t.tag == id && !params.is_empty() && params.len() <= usize::from(t.most) && params.iter().all(|u| name_unit(*u)),
            Arg::Index => params.len() == 4 && params[..3] == [0, 0, 0] && params[3] <= t.most,
            Arg::Tag => {
                if params.is_empty() && id <= t.most {
                    by_tag = Some(t);
                }
                false
            }
        };
        if exact {
            return Some(t);
        }
    }
    by_tag
}

fn tag_words(group: u16, id: u16, params: &[u16]) -> String {
    let Some(t) = row_of(group, id, params) else {
        let mut words = format!("[tag {group} {id}");
        for unit in params {
            words.push_str(&format!(" {unit}"));
        }
        words.push(']');
        return words;
    };
    match t.arg {
        Arg::None | Arg::Zero => format!("[{}]", t.word),
        Arg::Unit => format!("[{} {}]", t.word, params[0]),
        Arg::High => format!("[{} {}]", t.word, params[0] >> 8),
        Arg::Tag => format!("[{} {id}]", t.word),
        Arg::Index => format!("[{} {id} {}]", t.word, params[3]),
        Arg::Name => format!("[{} {}]", t.word, String::from_utf16_lossy(params)),
    }
}

fn number(word: &str) -> Option<u16> {
    let value = match word.strip_prefix("0x").or_else(|| word.strip_prefix("0X")) {
        Some(hex) if !hex.is_empty() => u32::from_str_radix(hex, 16).ok()?,
        _ if !word.is_empty() && word.bytes().all(|b| b.is_ascii_digit()) => word.parse::<u32>().ok()?,
        _ => return None,
    };
    u16::try_from(value).ok()
}

/// The tag a bracket's words name: (group, tag, parameter units); None where they name none.
fn tag_of(words: &[&str]) -> Option<(u16, u16, Vec<u16>)> {
    let first = words.first()?.to_ascii_lowercase();
    if first == "tag" {
        let [_, group, id, units @ ..] = words else { return None };
        let group = number(group)?;
        if group > 0xFF || units.len() > TAG_UNITS_MAX - 1 {
            return None;
        }
        return Some((group, number(id)?, units.iter().map(|u| number(u)).collect::<Option<Vec<_>>>()?));
    }
    let t = TAG_WORDS.iter().find(|t| t.word == first)?;
    Some(match (t.arg, &words[1..]) {
        (Arg::None, []) => (t.group, t.tag, vec![]),
        (Arg::Zero, []) => (t.group, t.tag, vec![0]),
        (Arg::Name, [name]) if name.len() <= usize::from(t.most) && name.bytes().all(|b| name_unit(u16::from(b))) => {
            (t.group, t.tag, name.bytes().map(u16::from).collect())
        }
        (Arg::Index, [tag, index]) => (t.group, number(tag)?, vec![0, 0, 0, number(index).filter(|i| *i <= t.most)?]),
        (Arg::Unit | Arg::Tag | Arg::High, [value]) => {
            let value = number(value).filter(|v| *v <= t.most)?;
            match t.arg {
                Arg::Tag => (t.group, value, vec![]),
                Arg::High => (t.group, t.tag, vec![value << 8]),
                _ => (t.group, t.tag, vec![value]),
            }
        }
        _ => return None,
    })
}

/// How a tag's word is written, for a refusal that names it: `[wait n]`, `[number t i]`.
pub fn tag_syntax(t: &TagWord) -> String {
    match t.arg {
        Arg::None | Arg::Zero => format!("[{}]", t.word),
        Arg::Unit | Arg::Tag | Arg::High => format!("[{} n]", t.word),
        Arg::Name => format!("[{} NAME]", t.word),
        Arg::Index => format!("[{} t i]", t.word),
    }
}

/// A line's words checked as the cook reads them: every `[` opens a tag of the table (or `[tag G T u...]`) or is
/// written `[[`. `key` names the line in the refusal; a column counts characters from 1.
pub fn check_words(key: &str, text: &str) -> Result<()> {
    let column = |at: usize| (text[..at].chars().count() + 1).to_string();
    let mut at = 0;
    while let Some(found) = text[at..].find('[') {
        let open = at + found;
        if text[open + 1..].starts_with('[') {
            at = open + 2;
            continue;
        }
        let Some(end) = text[open + 1..].find(['[', ']']).map(|e| open + 1 + e).filter(|e| text.as_bytes()[*e] == b']') else {
            return Err(super::refuse::refuse("Cook.Text.Unclosed", &[("key", key.to_string()), ("column", column(open))]));
        };
        let words: Vec<&str> = text[open + 1..end].split([' ', '\t']).filter(|w| !w.is_empty()).collect();
        if tag_of(&words).is_none() {
            let first = words.first().copied().unwrap_or("").to_ascii_lowercase();
            let args = |more: Vec<(&'static str, String)>| {
                let mut all = vec![("key", key.to_string()), ("column", column(open)), ("word", first.clone())];
                all.extend(more);
                all
            };
            if first == "tag" {
                return Err(super::refuse::refuse("Cook.Text.TagArguments", &args(vec![("syntax", "[tag G T u...]".to_string())])));
            }
            let Some(t) = TAG_WORDS.iter().find(|t| t.word == first) else {
                return Err(super::refuse::refuse("Cook.Text.UnknownTag", &args(vec![])));
            };
            let wanted = match t.arg {
                Arg::None | Arg::Zero => 0,
                Arg::Index => 2,
                _ => 1,
            };
            let numbers = matches!(t.arg, Arg::Unit | Arg::Tag | Arg::High | Arg::Index);
            if words.len() - 1 == wanted && numbers {
                if let Some(bad) = words[1..].iter().find(|w| number(w).map_or(true, |v| v > t.most)) {
                    return Err(super::refuse::refuse("Cook.Text.TagNumber", &args(vec![("value", bad.to_string()), ("most", t.most.to_string())])));
                }
            }
            return Err(super::refuse::refuse("Cook.Text.TagArguments", &args(vec![("syntax", tag_syntax(t))])));
        }
        at = end + 1;
    }
    Ok(())
}

/// Mod text as the game's units, as Nebula reads it (luma_text_tags.h luma_text_tag_encode): without the 0.
pub fn encode_text(text: &str) -> Vec<u16> {
    let mut out = Vec::with_capacity(text.len());
    let mut rest = text;
    while let Some(c) = rest.chars().next() {
        if c == '[' {
            if rest[1..].starts_with('[') {
                out.push(u16::from(b'['));
                rest = &rest[2..];
                continue;
            }
            let end = rest[1..].find(['[', ']']).map(|at| at + 1);
            if let Some(end) = end.filter(|at| rest.as_bytes()[*at] == b']') {
                let words: Vec<&str> = rest[1..end].split([' ', '\t']).filter(|w| !w.is_empty()).collect();
                if words.len() <= 2 + TAG_UNITS_MAX {
                    if let Some((group, id, units)) = tag_of(&words) {
                        out.push(TAG);
                        out.push((6 + 2 * units.len() as u16) | (group << 8));
                        out.push(id);
                        out.extend(units);
                        rest = &rest[end + 1..];
                        continue;
                    }
                }
            }
        }
        let mut buffer = [0u16; 2];
        out.extend_from_slice(c.encode_utf16(&mut buffer));
        rest = &rest[c.len_utf8()..];
    }
    out
}

const BRANCH_KINDS: [&str; 14] = [
    "select",
    "func",
    "near_player",
    "switch_a",
    "switch_b",
    "player_normal",
    "player_bee",
    "player_boo",
    "power_star_appeared",
    "already_done",
    "player_luigi",
    "galaxy_result",
    "time_keep_demo",
    "message_read",
];
const EVENT_KINDS: [&str; 8] = ["func", "func_stop", "none", "skip", "anime", "switch_a", "switch_b", "kill"];
const TALK_TYPES: [&str; 6] = ["normal", "short", "event", "compose", "flow", "null"];
const CAMERA_TYPES: [&str; 3] = ["normal", "event", "none"];
/// The game's branch nodes all have this group.
const BRANCH_GROUP: u8 = 2;
/// A select's parameter for choices of its own, named by the branch's node: Select_Nebula<node>_Yes and _No
/// (TalkNodeCtrl.hpp cSelectOwnChoices). The game's own selects are 0 to 18 (TalkMessageCtrl.cpp sSelectBranchID).
pub const SELECT_OWN_CHOICES: u32 = 0x100;
const GAME_SELECTS: u32 = 19;

/// A line's settings past its words (MessageRecord's bytes after the camera set): sound, camera type, talk type, balloon
/// type, area, already-read id.
const SOUND: usize = 0;
const CAMERA: usize = 1;
const TALK: usize = 2;
const BALLOON: usize = 3;
const AREA: usize = 4;
const ALREADY_READ: usize = 5;

/// An ordinary talk, as TalkMessageInfo starts: sound 0, no camera, a normal talk, the plain balloon, no area, not read.
fn ordinary_talk() -> Message {
    Message { text: 0, camera_set: 0, bytes: [0, 2, 0, 0, 0xFF, 0xFF] }
}

fn named(list: &[&str], value: &Value, what: &str) -> Result<u8> {
    let name = value.as_str().with_context(|| format!("{what} is one of {}", list.join(", ")))?;
    list.iter().position(|n| n.eq_ignore_ascii_case(name)).map(|i| i as u8).with_context(|| format!("{what} {name:?} is not one of {}", list.join(", ")))
}

fn small_number(value: &Value, what: &str, low: i64, high: i64) -> Result<i64> {
    let number = value.as_i64().with_context(|| format!("{what} is a whole number"))?;
    if !(low..=high).contains(&number) {
        bail!("{what} {number} is outside {low} to {high}");
    }
    Ok(number)
}

/// A language folder's code, as luma.json's `text` rows name languages (runtime luma.cpp language_code).
fn language_code(folder: &str) -> &'static str {
    const CODES: [(&str, &str); 14] = [
        ("UsEnglish", "en"),
        ("EuEnglish", "en-GB"),
        ("UsFrench", "fr-CA"),
        ("EuFrench", "fr"),
        ("UsSpanish", "es-MX"),
        ("EuSpanish", "es"),
        ("EuGerman", "de"),
        ("EuItalian", "it"),
        ("EuDutch", "nl"),
        ("UsPortuguese", "pt"),
        ("JpJapanese", "ja"),
        ("KrKorean", "ko"),
        ("CnSimpChinese", "zh-Hans"),
        ("TwTradChinese", "zh-Hant"),
    ];
    CODES.iter().find(|(name, _)| name.eq_ignore_ascii_case(folder)).map_or("en", |(_, code)| code)
}

/// A language folder's words of a line: its code (`fr-CA`), the code's language (`fr`), the folder (`UsFrench`), then
/// `default`, as a luma.json `text` row reads them.
fn words_in<'a>(value: &'a Value, language: &str) -> Result<Option<&'a str>> {
    match value {
        Value::String(words) => Ok(Some(words)),
        Value::Object(fields) => {
            let code = language_code(language);
            let base = code.split('-').next().unwrap_or(code);
            for key in [code, base, language, "default"] {
                if let Some(found) = fields.iter().find(|(k, _)| k.eq_ignore_ascii_case(key)) {
                    return found.1.as_str().map(Some).with_context(|| format!("{key}'s words are text"));
                }
            }
            Ok(None)
        }
        _ => bail!("a line is its words, or an object of words and talk settings"),
    }
}

struct Ids {
    table: TableIr,
    id_at: usize,
    index_at: usize,
    by_id: HashMap<String, u32>,
}

impl Ids {
    fn read(bytes: &[u8]) -> Result<Ids> {
        let table = TableIr::from_ntbl(bytes).context("the message id table")?;
        let column = |name: &str| table.columns.iter().position(|c| c.hash == column_hash(name)).with_context(|| format!("the id table has no {name}"));
        let (id_at, index_at) = (column("MessageId")?, column("Index")?);
        let mut by_id = HashMap::new();
        for row in &table.rows {
            if let (Cell::Text(id), Cell::Bits(index)) = (&row[id_at], &row[index_at]) {
                by_id.insert(String::from_utf8_lossy(id).into_owned(), *index);
            }
        }
        Ok(Ids { table, id_at, index_at, by_id })
    }

    fn add(&mut self, id: &str, index: u32) {
        let mut row: Vec<Cell> = self.table.columns.iter().map(|c| if c.kind == Kind::Text { Cell::Text(Vec::new()) } else { Cell::Bits(0) }).collect();
        row[self.id_at] = Cell::Text(id.as_bytes().to_vec());
        row[self.index_at] = Cell::Bits(index);
        self.table.rows.push(row);
        self.by_id.insert(id.to_string(), index);
    }

    /// The table as the game searches it: rows in byte order of their ids (Table::findElementBinary).
    fn to_ntbl(mut self) -> Vec<u8> {
        let at = self.id_at;
        self.table.rows.sort_by(|a, b| match (&a[at], &b[at]) {
            (Cell::Text(x), Cell::Text(y)) => x.cmp(y),
            _ => std::cmp::Ordering::Equal,
        });
        self.table.to_ntbl()
    }
}

struct Flows<'a> {
    nmsg: &'a mut Nmsg,
    ids: &'a mut Ids,
    language: &'a str,
}

impl Flows<'_> {
    /// A line's message node: the game's first one for it, or a new one ending the talk.
    /// A new line of ordinary talk.
    fn add_line(&mut self, id: &str, words: &str) {
        let mut record = ordinary_talk();
        record.text = self.nmsg.text.len() as u32;
        self.nmsg.text.extend(encode_text(words));
        self.nmsg.text.push(0);
        self.nmsg.messages.push(record);
        self.ids.add(id, self.nmsg.messages.len() as u32 - 1);
    }

    fn message_node(&mut self, id: &str) -> Result<u32> {
        let index = *self.ids.by_id.get(id).with_context(|| format!("the flow names {id}, which is no line"))?;
        if let Some(at) = self.nmsg.nodes.iter().position(|n| n.kind == 1 && n.index == index) {
            return Ok(at as u32);
        }
        self.nmsg.nodes.push(Node { kind: 1, group: 0, index, next: NONE, next_group: 0 });
        Ok(self.nmsg.nodes.len() as u32 - 1)
    }

    /// What a step is as a node: NONE for the end.
    fn step(&mut self, step: &Value, depth: usize) -> Result<u32> {
        if depth > 64 {
            bail!("a flow nested past 64 steps");
        }
        match step {
            Value::Null => Ok(NONE),
            Value::String(id) => self.message_node(id),
            Value::Object(fields) => {
                if let Some(ask) = fields.get("ask") {
                    let condition = named(&BRANCH_KINDS, ask, "ask")?;
                    let mut param = fields.get("param").map(|p| small_number(p, "param", 0, i64::from(u32::MAX - 1))).transpose()?.unwrap_or(0) as u32;
                    let choices = fields.get("choices");
                    if condition == 0 {
                        if choices.is_some() && fields.contains_key("param") {
                            bail!("a select has choices of its own or the game's (param), not both");
                        }
                        if choices.is_none() && param >= GAME_SELECTS {
                            bail!("select {param}: the game's selects are 0 to {}; give choices for words of your own", GAME_SELECTS - 1);
                        }
                    } else if choices.is_some() {
                        bail!("only a select has choices");
                    }
                    let first = self.nmsg.branches.len() as u32;
                    self.nmsg.branches.extend([NONE, NONE]);
                    let node = self.nmsg.nodes.len() as u32;
                    if let Some(choices) = choices {
                        let choices = choices.as_object().context("choices are the yes and no words")?;
                        for (key, answer) in [("yes", "Yes"), ("no", "No")] {
                            let words = choices.get(key).with_context(|| format!("choices need {key}"))?;
                            let words = words_in(words, self.language)?.with_context(|| format!("choices: {key} has no words for {}: give it a default", self.language))?;
                            check_words(&format!("choices {key} ({})", self.language), words)?;
                            self.add_line(&format!("Select_Nebula{node}_{answer}"), words);
                        }
                        param = SELECT_OWN_CHOICES;
                    }
                    self.nmsg.nodes.push(Node { kind: 2, group: BRANCH_GROUP, index: u32::from(condition), next: param, next_group: first });
                    let yes = self.step(fields.get("yes").unwrap_or(&Value::Null), depth + 1)?;
                    let no = self.step(fields.get("no").unwrap_or(&Value::Null), depth + 1)?;
                    self.nmsg.branches[first as usize] = yes;
                    self.nmsg.branches[first as usize + 1] = no;
                    Ok(node)
                } else if let Some(event) = fields.get("event") {
                    let kind = named(&EVENT_KINDS, event, "event")?;
                    let value = fields.get("value").map(|v| small_number(v, "value", 0, i64::from(u32::MAX))).transpose()?.unwrap_or(0) as u32;
                    let slot = self.nmsg.branches.len() as u32;
                    self.nmsg.branches.push(NONE);
                    self.nmsg.nodes.push(Node { kind: 3, group: kind, index: slot, next: value, next_group: 0 });
                    let node = self.nmsg.nodes.len() as u32 - 1;
                    let next = self.step(fields.get("next").unwrap_or(&Value::Null), depth + 1)?;
                    self.nmsg.branches[slot as usize] = next;
                    Ok(node)
                } else {
                    bail!("a step is a line's id, null, an ask or an event")
                }
            }
            _ => bail!("a step is a line's id, null, an ask or an event"),
        }
    }
}

/// The ids a flow step names.
fn step_ids(step: &Value, out: &mut Vec<String>) {
    match step {
        Value::String(id) => out.push(id.clone()),
        Value::Object(fields) => {
            for key in ["yes", "no", "next"] {
                if let Some(next) = fields.get(key) {
                    step_ids(next, out);
                }
            }
        }
        _ => {}
    }
}

/// A merge checked without the game (an author's tools): everything [`merge`] checks but whether the game has the
/// lines it names (`like`, a flow's lines, a line it gives settings and no words), which stand in for the game's here.
pub fn check_shape(json: &[u8], language: &str) -> Result<()> {
    let root: Value = serde_json::from_slice(json).context("not JSON")?;
    let lines = root.get("lines").and_then(Value::as_object);
    let mut named = Vec::new();
    for (id, value) in lines.into_iter().flatten() {
        if let Some(like) = value.get("like").and_then(Value::as_str) {
            named.push(like.to_string());
        }
        if matches!(words_in(value, language), Ok(None)) {
            named.push(id.clone());
        }
    }
    for (id, step) in root.get("flows").and_then(Value::as_object).into_iter().flatten() {
        named.push(id.clone());
        step_ids(step, &mut named);
    }
    let mut game = Nmsg::default();
    let mut ids = TableIr::from_json(br#"{"columns":{"MessageId":"string","Index":"int"},"rows":[]}"#)?;
    // The columns as the table has them (in name order), not as written.
    let id_at = ids.columns.iter().position(|c| c.hash == column_hash("MessageId")).context("no MessageId")?;
    let mut seen = std::collections::HashSet::new();
    for id in named {
        let new_line = lines.is_some_and(|l| l.get(&id).is_some_and(|v| matches!(words_in(v, language), Ok(Some(_)))));
        if new_line || !seen.insert(id.clone()) {
            continue;
        }
        game.messages.push(Message { text: game.text.len() as u32, ..ordinary_talk() });
        game.text.push(0);
        let mut row = vec![Cell::Bits(game.messages.len() as u32 - 1); 2];
        row[id_at] = Cell::Text(id.into_bytes());
        ids.rows.push(row);
    }
    merge(&game.to_bytes(), &ids.to_ntbl(), json, language).map(|_| ())
}

/// A language's messages with a mod's merge applied: (message.bmg, messageid.tbl).
pub fn merge(message_file: &[u8], id_table: &[u8], json: &[u8], language: &str) -> Result<(Vec<u8>, Vec<u8>)> {
    let root: Value = serde_json::from_slice(json).context("not JSON")?;
    let root = root.as_object().context("a messages merge is an object of lines and flows")?;
    for key in root.keys() {
        if key != "lines" && key != "flows" {
            bail!("{key}: a messages merge has lines and flows");
        }
    }
    let mut nmsg = Nmsg::from_nmsg(message_file).context("the message file")?;
    let mut ids = Ids::read(id_table)?;
    let empty = Map::new();
    let lines = match root.get("lines") {
        Some(lines) => lines.as_object().context("lines is an object of ids")?,
        None => &empty,
    };
    for (id, value) in lines {
        if id.is_empty() || !id.is_ascii() || id.contains('\0') {
            bail!("{id:?}: a line's id is ASCII text");
        }
        let words = words_in(value, language).with_context(|| id.clone())?;
        let settings = value.as_object();
        let existing = ids.by_id.get(id).copied();
        let mut record = match existing {
            Some(index) => *nmsg.messages.get(index as usize).with_context(|| format!("{id}: its number is past the messages"))?,
            None => match settings.and_then(|s| s.get("like")) {
                Some(like) => {
                    let like = like.as_str().with_context(|| format!("{id}: like names a line"))?;
                    let index = *ids.by_id.get(like).with_context(|| format!("{id}: like names {like}, which is no line"))?;
                    *nmsg.messages.get(index as usize).with_context(|| format!("{like}: its number is past the messages"))?
                }
                None => ordinary_talk(),
            },
        };
        if let Some(settings) = settings {
            for (key, value) in settings {
                let what = format!("{id}: {key}");
                match key.as_str() {
                    "talk" => record.bytes[TALK] = named(&TALK_TYPES, value, &what)?,
                    "camera" => record.bytes[CAMERA] = named(&CAMERA_TYPES, value, &what)?,
                    "balloon" => record.bytes[BALLOON] = small_number(value, &what, 0, 255)? as u8,
                    "camera_set" => record.camera_set = small_number(value, &what, 0, 0xFFFF)? as u16,
                    "sound" => record.bytes[SOUND] = small_number(value, &what, -128, 127)? as i8 as u8,
                    "area" => record.bytes[AREA] = small_number(value, &what, -128, 127)? as i8 as u8,
                    "already_read" => record.bytes[ALREADY_READ] = small_number(value, &what, -128, 127)? as i8 as u8,
                    _ => {} // words by language, and like
                }
            }
        }
        match (words, existing) {
            (Some(words), _) => {
                check_words(&format!("{id} ({language})"), words)?;
                record.text = nmsg.text.len() as u32;
                nmsg.text.extend(encode_text(words));
                nmsg.text.push(0);
            }
            (None, Some(_)) => {} // the game's words stay in this language
            (None, None) => bail!("{id}: a new line has no words for {language}: give it a default"),
        }
        match existing {
            Some(index) => nmsg.messages[index as usize] = record,
            None => {
                nmsg.messages.push(record);
                ids.add(id, nmsg.messages.len() as u32 - 1);
            }
        }
    }
    if let Some(flows) = root.get("flows") {
        let flows = flows.as_object().context("flows is an object of line ids")?;
        let mut walker = Flows { nmsg: &mut nmsg, ids: &mut ids, language };
        for (id, step) in flows {
            let node = walker.message_node(id)?;
            let next = walker.step(step, 0).with_context(|| format!("the flow after {id}"))?;
            walker.nmsg.nodes[node as usize].next = next;
        }
    }
    Ok((nmsg.to_bytes(), ids.to_ntbl()))
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    /// Two game lines, the first with a node.
    pub(crate) fn game() -> (Vec<u8>, Vec<u8>) {
        let mut nmsg = Nmsg::default();
        for (i, words) in ["First", "Second"].iter().enumerate() {
            nmsg.messages.push(Message { text: nmsg.text.len() as u32, camera_set: 7, bytes: [3, 0, 4, 5, 1, i as u8] });
            nmsg.text.extend(words.encode_utf16());
            nmsg.text.push(0);
        }
        nmsg.nodes.push(Node { kind: 1, group: 0, index: 0, next: NONE, next_group: 0 });
        let table = TableIr::from_json(br#"{"columns":{"MessageId":"string","Index":"int"},"rows":[{"MessageId":"Game_A","Index":0},{"MessageId":"Game_B","Index":1}]}"#).unwrap();
        (nmsg.to_bytes(), table.to_ntbl())
    }

    pub(crate) fn words(nmsg: &Nmsg, index: usize) -> String {
        decode_text(units_at(&nmsg.text, nmsg.messages[index].text as usize))
    }

    #[test]
    fn lines_change_and_join_the_game_with_their_talk() {
        let (file, table) = game();
        let json = br#"{"lines":{"Game_B":"Other [color 1]words[color 0]","Mod_New":{"default":"Hi [player]!","fr":"Salut [player] !","like":"Game_A","balloon":9},"AAA_First":"x"}}"#;
        let (file, table) = merge(&file, &table, json, "UsFrench").unwrap();
        let nmsg = Nmsg::from_nmsg(&file).unwrap();
        let ids = Ids::read(&table).unwrap();
        assert_eq!(nmsg.messages.len(), 4);
        assert_eq!(words(&nmsg, 1), "Other [color 1]words[color 0]");
        assert_eq!(nmsg.messages[1].camera_set, 7, "a game line keeps its talk");
        let new = ids.by_id["Mod_New"] as usize;
        assert_eq!(words(&nmsg, new), "Salut [player] !");
        assert_eq!(nmsg.messages[new].bytes, [3, 0, 4, 9, 1, 0], "like Game_A, its balloon set");
        let order: Vec<_> = ids.table.rows.iter().map(|r| if let Cell::Text(t) = &r[ids.id_at] { String::from_utf8_lossy(t).into_owned() } else { String::new() }).collect();
        assert_eq!(order, ["AAA_First", "Game_A", "Game_B", "Mod_New"], "in the byte order the game searches");
        assert_eq!(nmsg.messages[ids.by_id["AAA_First"] as usize].bytes, ordinary_talk().bytes);
    }

    #[test]
    fn flows_walk_as_the_game_walks_them() {
        let (file, table) = game();
        let json = br#"{"lines":{"Ask":"Want one?","Yes":"Here!","No":"Fine."},
            "flows":{"Game_A":"Ask","Ask":{"ask":"select","choices":{"yes":"Sure","no":{"default":"Nope","fr":"Non"}},
                "yes":{"event":"func","value":2,"next":"Yes"},"no":"No"},"No":null}}"#;
        let (file, table) = merge(&file, &table, json, "UsEnglish").unwrap();
        let nmsg = Nmsg::from_nmsg(&file).unwrap();
        let ids = Ids::read(&table).unwrap();
        let node_of = |id: &str| nmsg.nodes.iter().position(|n| n.kind == 1 && n.index == ids.by_id[id]).unwrap();
        // Game_A's own node now goes on to Ask; Ask to a select branch.
        assert_eq!(nmsg.nodes[0].next as usize, node_of("Ask"));
        let branch_at = nmsg.nodes[node_of("Ask")].next;
        let branch = nmsg.nodes[branch_at as usize];
        assert_eq!((branch.kind, branch.group, branch.index, branch.next), (2, BRANCH_GROUP, 0, SELECT_OWN_CHOICES));
        // Its choices: lines the game's selector reads by the branch's node.
        assert_eq!(words(&nmsg, ids.by_id[&format!("Select_Nebula{branch_at}_Yes")] as usize), "Sure");
        assert_eq!(words(&nmsg, ids.by_id[&format!("Select_Nebula{branch_at}_No")] as usize), "Nope");
        // Yes: an event whose next (its branch slot) is the Yes line; no: the No line, which ends the talk.
        let event = nmsg.nodes[nmsg.branches[branch.next_group as usize] as usize];
        assert_eq!((event.kind, event.group, event.next), (3, 0, 2));
        assert_eq!(nmsg.branches[event.index as usize] as usize, node_of("Yes"));
        assert_eq!(nmsg.branches[branch.next_group as usize + 1] as usize, node_of("No"));
        assert_eq!(nmsg.nodes[node_of("No")].next, NONE);
    }

    #[test]
    fn what_cannot_be_is_said() {
        let (file, table) = game();
        for (json, says) in [
            (r#"{"lines":{"New":{"fr":"x","UsFrench":"y"}}}"#, "no words for UsEnglish"),
            (r#"{"lines":{"New":{"default":"x","like":"Nope"}}}"#, "which is no line"),
            (r#"{"flows":{"Game_A":"Nope"}}"#, "which is no line"),
            (r#"{"flows":{"Game_A":{"ask":"maybe"}}}"#, "not one of"),
            (r#"{"flows":{"Game_A":{"ask":"select","param":19}}}"#, "0 to 18"),
            (r#"{"flows":{"Game_A":{"ask":"select","param":1,"choices":{"yes":"a","no":"b"}}}}"#, "not both"),
            (r#"{"flows":{"Game_A":{"ask":"near_player","choices":{"yes":"a","no":"b"}}}}"#, "only a select"),
            (r#"{"flows":{"Game_A":{"ask":"select","choices":{"yes":"a"}}}}"#, "choices need no"),
            (r#"{"lines":{"New":{"default":"x","talk":"loud"}}}"#, "not one of"),
            (r#"{"rows":[]}"#, "lines and flows"),
        ] {
            let error = format!("{:#}", merge(&file, &table, json.as_bytes(), "UsEnglish").unwrap_err());
            assert!(error.contains(says), "{json}: {error}");
        }
    }

    #[test]
    fn words_go_by_language_as_text_rows_do() {
        let words = serde_json::json!({"default": "d", "fr": "f", "fr-CA": "q", "EuGerman": "g", "es": "s"});
        for (folder, expected) in [("UsFrench", "q"), ("EuFrench", "f"), ("EuGerman", "g"), ("UsSpanish", "s"), ("JpJapanese", "d")] {
            assert_eq!(words_in(&words, folder).unwrap(), Some(expected), "{folder}");
        }
    }

    #[test]
    fn a_merge_is_checked_without_the_game() {
        let good = br#"{"lines":{"New":{"default":"Hi","like":"Game_X"},"Game_Y":{"talk":"short"}},
            "flows":{"Game_Z":"New","New":{"ask":"select","choices":{"yes":"a","no":"b"},"yes":"Game_W","no":null}}}"#;
        check_shape(good, "UsEnglish").unwrap();
        for (json, says) in [
            (r#"{"flows":{"A":{"ask":"maybe"}}}"#, "not one of"),
            (r#"{"lines":{"A":{"default":"x","balloon":300}}}"#, "outside"),
            (r#"{"lines":{"A":{"default":"x","camera":"wide"}}}"#, "not one of"),
            (r#"{"flows":{"A":{"ask":"select","choices":{"yes":{"fr":"x"},"no":"b"}}}}"#, "no words for UsEnglish"),
            (r#"{"rows":[]}"#, "lines and flows"),
        ] {
            let error = format!("{:#}", check_shape(json.as_bytes(), "UsEnglish").unwrap_err());
            assert!(error.contains(says), "{json}: {error}");
        }
    }

    #[test]
    fn mod_text_reads_as_nebula_reads_it() {
        assert_eq!(encode_text("A[wait 30]B"), [u16::from(b'A'), 0x1A, 8 | (1 << 8), 0, 30, u16::from(b'B')]);
        assert_eq!(encode_text("[color 1]x[color 0]"), [0x1A, 8 | (255 << 8), 0, 0x100, u16::from(b'x'), 0x1A, 8 | (255 << 8), 0, 0]);
        assert_eq!(encode_text("[icon 7][LARGE]"), [0x1A, 6 | (3 << 8), 7, 0x1A, 6 | (4 << 8), 2]);
        assert_eq!(encode_text("[tag 6 5 0 0x1 2]"), [0x1A, 12 | (6 << 8), 5, 0, 1, 2]);
        let plain = "[a] [nope 1] [wait] [wait x] [color 256]";
        assert_eq!(encode_text(&format!("[{plain}")), plain.encode_utf16().collect::<Vec<_>>());
        let text = "Hi [player]! [[sic] [color 3]Star[color 0][wait 20][page][icon 12][tag 6 5 0 1 0 0][sound SE_SV_TICO]";
        assert_eq!(decode_text(&encode_text(text)), text);
        // A message ends at its first 0 outside a tag: tags hold 0s.
        let mut file = encode_text("[color 1]A[color 0]");
        file.push(0);
        file.extend(encode_text("B"));
        assert_eq!(decode_text(units_at(&file, 0)), "[color 1]A[color 0]");
        assert_eq!(units_at(&file, file.len()), &[] as &[u16]);
    }

    /// The game's own messages, every language on the disc, read out as words and cooked back: the same units, so
    /// the same file; and every tag they hold has a word of the table. NEBULA_DISC_FILES: an extracted disc's files
    /// folder. cargo test -p nebula-import --lib disc_messages_read_out_and_cook_back -- --ignored --nocapture
    #[test]
    #[ignore]
    fn disc_messages_read_out_and_cook_back() {
        use crate::cook::archive;
        let files = std::env::var("NEBULA_DISC_FILES").expect("NEBULA_DISC_FILES");
        let mut languages = 0;
        let mut entries: Vec<_> = std::fs::read_dir(&files).unwrap().map(|e| e.unwrap().path()).collect();
        entries.sort();
        for dir in entries {
            let arc = dir.join("MessageData/Message.arc");
            let Ok(raw) = std::fs::read(&arc) else { continue };
            let bytes = if archive::is_compressed(&raw) { archive::expand(&raw).unwrap() } else { raw };
            let member = archive::members(&bytes).unwrap().into_iter().find(|m| m.path.ends_with("message.bmg")).unwrap();
            let nmsg = Nmsg::from_disc(&member.bytes).unwrap();
            let mut text = Vec::with_capacity(nmsg.text.len());
            let (mut tags, mut unnamed) = (0, Vec::new());
            for (index, message) in nmsg.messages.iter().enumerate() {
                let units = units_at(&nmsg.text, message.text as usize);
                let words = decode_text(units);
                tags += units.iter().filter(|u| **u == TAG).count();
                unnamed.extend(words.match_indices("[tag ").map(|(at, _)| words[at..].split(']').next().unwrap_or("").to_string()));
                let again = encode_text(&words);
                assert_eq!(again, units, "{} message {index}: {words}", dir.display());
                if text.len() == message.text as usize {
                    text.extend(again);
                    text.push(0);
                }
            }
            assert_eq!(text, nmsg.text[..text.len()], "{}", dir.display());
            unnamed.sort();
            unnamed.dedup();
            assert!(unnamed.is_empty(), "{}: tags of no word: {unnamed:?}", dir.display());
            println!("{}: {} messages, {} tags, the same units", dir.display(), nmsg.messages.len(), tags);
            languages += 1;
        }
        assert!(languages > 0, "no MessageData/Message.arc under {files}");
    }

    /// A line's brackets that are no tag are refused, each with its cause, the line and the column; `[[` writes a
    /// bracket of the text.
    #[test]
    fn a_wrong_tag_is_named() {
        let said = |text: &str| -> Option<Vec<String>> {
            let error = check_words("Mod_Line (UsEnglish)", text).err()?;
            let refusal = error.downcast_ref::<super::super::patch::Refusal>().expect("a named refusal");
            Some(
                refusal
                    .0
                    .args
                    .iter()
                    .map(|(name, value)| match value {
                        luma_validate::Arg::Phrase(p) => format!("{name}={p}"),
                        luma_validate::Arg::Text(t) => format!("{name}={t}"),
                    })
                    .collect(),
            )
        };
        assert_eq!(said("Hi [player]! [[sic] [color 3]Star[color 0][number 5 1][tag 6 5 0 1 2]"), None);
        assert_eq!(said("Hi [player"), Some(vec!["why=Cook.Text.Unclosed".into(), "key=Mod_Line (UsEnglish)".into(), "column=4".into()]));
        assert_eq!(said("é [wait [page]").unwrap()[2], "column=3");
        assert_eq!(said("[plyer]").unwrap(), ["why=Cook.Text.UnknownTag", "key=Mod_Line (UsEnglish)", "column=1", "word=plyer"]);
        assert_eq!(said("a []").unwrap()[0], "why=Cook.Text.UnknownTag");
        assert_eq!(said("[wait]").unwrap()[4], "syntax=[wait n]");
        assert_eq!(said("[player 1]").unwrap()[4], "syntax=[player]");
        assert_eq!(said("[number 5]").unwrap()[4], "syntax=[number t i]");
        assert_eq!(said("[sound A[B]").unwrap()[0], "why=Cook.Text.Unclosed");
        assert_eq!(said("[tag 300 1]").unwrap()[4], "syntax=[tag G T u...]");
        assert_eq!(said("[color 256]").unwrap()[3..], ["word=color".to_string(), "value=256".into(), "most=255".into()]);
        assert_eq!(said("[wait x]").unwrap()[4], "value=x");
        assert_eq!(said("[WAIT 0x10000]").unwrap()[4], "value=0x10000");
        // In a merge the line's id and language are named.
        let (file, ids) = game();
        let error = merge(&file, &ids, br#"{"lines":{"Mod_New":"Hi [plyer]"}}"#, "UsEnglish").unwrap_err();
        let refusal = error.downcast_ref::<super::super::patch::Refusal>().expect("a named refusal");
        assert!(refusal.0.args.iter().any(|(n, v)| *n == "key" && *v == luma_validate::Arg::Text("Mod_New (UsEnglish)".into())));
    }

    /// The cook's words are the SDK's: native/include/luma_text_tags.h's table, which the game's mod API and
    /// LumaScript's runtime use, row for row (word, group, tag, argument, largest value, words); and each row's word
    /// cooks to its tag, reads back as written, and past its largest value stays as written.
    #[test]
    fn tag_words_are_the_sdk_table() {
        let header = std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../../native/include/luma_text_tags.h")).unwrap();
        let body = header.split("luma_text_tags[] = {").nth(1).unwrap().split("};").next().unwrap();
        let rows: Vec<&str> = body.lines().map(str::trim).filter(|l| l.starts_with("{\"")).collect();
        assert_eq!(rows.len(), TAG_WORDS.len(), "the header has {} rows, the cook {}", rows.len(), TAG_WORDS.len());
        for (line, t) in rows.iter().zip(TAG_WORDS.iter()) {
            let inner = line.trim_start_matches('{').trim_end_matches("},");
            let (fields, means) = inner.rsplit_once(", \"").unwrap();
            let f: Vec<&str> = fields.split(", ").collect();
            let arg = match f[3] {
                "LUMA_TEXT_ARG_NONE" => Arg::None,
                "LUMA_TEXT_ARG_UNIT" => Arg::Unit,
                "LUMA_TEXT_ARG_TAG" => Arg::Tag,
                "LUMA_TEXT_ARG_NAME" => Arg::Name,
                "LUMA_TEXT_ARG_HIGH" => Arg::High,
                "LUMA_TEXT_ARG_ZERO" => Arg::Zero,
                "LUMA_TEXT_ARG_INDEX" => Arg::Index,
                other => panic!("{other} is no argument kind"),
            };
            let most = match f[4] {
                "LUMA_TEXT_TAG_UNITS_MOST" => TAG_UNITS_MAX as u16,
                hex if hex.starts_with("0x") => u16::from_str_radix(&hex[2..], 16).unwrap(),
                n => n.parse().unwrap(),
            };
            let want = (f[0].trim_matches('"'), f[1].parse::<u16>().unwrap(), f[2].parse::<u16>().unwrap(), arg, most, means.trim_end_matches('"'));
            assert_eq!(want, (t.word, t.group, t.tag, t.arg, t.most, t.means), "row {line}");

            let unit = |count: u16| (6 + 2 * count) | (t.group << 8);
            let (text, units): (String, Vec<u16>) = match t.arg {
                Arg::None => (format!("[{}]", t.word), vec![0x1A, unit(0), t.tag]),
                Arg::Zero => (format!("[{}]", t.word), vec![0x1A, unit(1), t.tag, 0]),
                Arg::Unit => (format!("[{} 7]", t.word), vec![0x1A, unit(1), t.tag, 7]),
                Arg::High => (format!("[{} 3]", t.word), vec![0x1A, unit(1), t.tag, 3 << 8]),
                Arg::Tag => (format!("[{} 7]", t.word), vec![0x1A, unit(0), 7]),
                Arg::Name => (format!("[{} AB_1]", t.word), vec![0x1A, unit(4), t.tag, 0x41, 0x42, 0x5F, 0x31]),
                Arg::Index => (format!("[{} 5 2]", t.word), vec![0x1A, unit(4), 5, 0, 0, 0, 2]),
            };
            assert_eq!(encode_text(&text), units, "{text}");
            assert_eq!(decode_text(&units), text, "{text}");
            if matches!(t.arg, Arg::Unit | Arg::High | Arg::Tag) && t.most < 0xFFFF {
                let past = format!("[{} {}]", t.word, u32::from(t.most) + 1);
                assert_eq!(encode_text(&past), past.encode_utf16().collect::<Vec<_>>(), "{past}");
            }
            if !matches!(t.arg, Arg::None | Arg::Zero) {
                let bare = format!("[{}]", t.word);
                assert_eq!(encode_text(&bare), bare.encode_utf16().collect::<Vec<_>>(), "{bare}");
            }
        }
    }
}
