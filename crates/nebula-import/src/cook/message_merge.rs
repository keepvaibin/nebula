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
#[path = "message_merge_tests.rs"]
pub(crate) mod tests;
