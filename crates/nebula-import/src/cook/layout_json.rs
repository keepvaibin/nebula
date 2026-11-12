//! A mod's screen layout, `<Name>.layout.json` in an archive (plan 3.8, contract board/contracts/layout.md), as the
//! game's own layout file beside it: `<archive>/blyt/<Name>.brlyt`, which the store then cooks as it cooks the game's
//! layouts (ui_layout) and the game's layout loader reads (LayoutManager::initArc). Panes, pictures, text and
//! bounding panes, their tree, materials, fonts and groups are written as the game's layout tool wrote them (the
//! game's own layouts, e.g. LayoutData/CoinCounter.arc, are the reference for every field and default).
//!
//! The file is checked whole before anything is written: every refusal names the place (a JSON pointer) and what
//! it takes, with the effects cook's words for the shape of JSON (Cook.Effect.Kind, Range, Choice, Color...).

use super::particle_json::{as_f32, as_int, at_index, at_key, choice, floats_of, length, list, missing, not_kind, object, only, parse_rgba, range, text};
use super::refuse::refuse;
use anyhow::Result;
use serde_json::{Map, Value};

/// The game's fonts a text pane can name (every font the game's layouts use).
pub const FONTS: [&str; 5] = ["MessageFont26", "NumberFont", "MenuFont64", "PictureFont", "CinemaFont26"];
/// Pane kinds, in this layer.
pub const KINDS: [&str; 5] = ["pane", "picture", "text", "bounding", "window"];
/// A pane's anchor (which of its points stands at its position) and a text's place in its pane; the number is the
/// game's (column + 3 * row).
pub const ANCHORS: [&str; 9] = ["top_left", "top", "top_right", "left", "center", "right", "bottom_left", "bottom", "bottom_right"];
/// How a text's lines line up: as its place says, or left, center or right.
pub const ALIGNS: [&str; 4] = ["as_place", "left", "center", "right"];
/// A pane or group name: the game's name field.
pub const NAME_MAX: usize = 16;
/// Characters a text pane holds (the `ui` service cuts set_text at 255 too).
pub const TEXT_MAX: i64 = 255;
/// Panes in one layout: the game's layout file counts its blocks in 16 bits, and a pane with children takes three.
pub const PANES_MAX: usize = 16_000;
/// The game's layouts' screen (every one is 608 by 456, centred).
pub const SCREEN: [f32; 2] = [608.0, 456.0];
const ROOT: &str = "RootPane";

const PANE_PARTS: [&str; 12] = ["name", "type", "position", "size", "scale", "rotation", "anchor", "alpha", "visible", "alpha_to_children", "widen", "children"];
const PICTURE_PARTS: [&str; 3] = ["texture", "colour", "corners"];
const TEXT_PARTS: [&str; 10] = ["text", "length", "font", "font_size", "place", "align", "colour", "colours", "char_space", "line_space"];
/// A window (layer 4): a 9-slice frame from one corner texture (the game mirrors it into the other three corners and
/// stretches its edges), the content inside in vertex colours, `inflation` [left, right, top, bottom] how far the
/// content reaches past the frame's inner edge.
const WINDOW_PARTS: [&str; 4] = ["frame", "inflation", "colour", "corners"];
/// The least width of a character, in its font's width (`font_size`), for the overflow check: a full-width
/// character (CJK, kana, Hangul, full-width forms) is one; any other is at least a quarter (the narrowest Latin
/// letters of the game's fonts are wider). A line refused as too wide is too wide in every font.
const NARROWEST: f32 = 0.25;

/// What `cook.layout` says of a layout.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct LayoutFacts {
    /// Panes the mod wrote (the root the cook adds is not counted).
    pub panes: usize,
    pub pictures: usize,
    pub texts: usize,
    pub textures: usize,
    pub groups: usize,
    pub windows: usize,
    /// Animation files made (layer 3).
    pub animations: usize,
}

enum Body {
    Pane,
    Bounding,
    Picture { corners: [[u8; 4]; 4], texture: Option<u16> },
    Text { text: Vec<u16>, length: u16, font: u16, place: u8, align: u8, colours: [[u8; 4]; 2], font_size: [f32; 2], char_space: f32, line_space: f32 },
    Window { corners: [[u8; 4]; 4], inflation: [f32; 4], frame: u16 },
}

struct Pane {
    name: String,
    flag: u8,
    anchor: u8,
    alpha: u8,
    translate: [f32; 3],
    rotate: [f32; 3],
    scale: [f32; 2],
    size: [f32; 2],
    body: Body,
    material: Option<u16>,
    children: Vec<Pane>,
}

struct Reader<'a> {
    /// Every pane's name, in tree order (the root first).
    names: Vec<String>,
    textures: Vec<String>,
    /// The fonts' names as the layout file lists them (`MessageFont26.brfnt`, `Luma/<id>/<path>.brfnt`).
    fonts: Vec<String>,
    materials: Vec<(String, Option<u16>)>,
    facts: LayoutFacts,
    missing_texture: &'a dyn Fn(&str) -> Option<String>,
    missing_font: &'a dyn Fn(&str) -> Option<String>,
}

/// A text's font as the layout file names it: one of the game's (`FONTS`), or a mod's own, `Luma/<id>/<path>` (its
/// `assets/<path>.font.json`; a `.font.json` or `.brfnt` ending may be written), whose cooked font the game finds in
/// the store by that name (layout contract 2a).
fn font_name(v: &Value, at: &str) -> Result<String> {
    if let Some(text) = v.as_str().filter(|t| t.len() > 5 && t[..5].eq_ignore_ascii_case("luma/")) {
        let lower = text.to_ascii_lowercase();
        let stem = [".font.json", ".brfnt"].iter().find(|e| lower.ends_with(*e)).map_or(text, |e| &text[..text.len() - e.len()]);
        let parts: Vec<&str> = stem.split('/').collect();
        // A path a store holds: folders and a name, none empty, `.` or `..`, of no character a path cannot hold.
        let fits = parts.len() >= 3
            && parts.iter().all(|p| !p.is_empty() && *p != "." && *p != "..")
            && !stem.chars().any(|c| c.is_control() || matches!(c, '\\' | ':' | '<' | '>' | '"' | '|' | '?' | '*'));
        if fits && stem.len() + ".brfnt".len() <= MOST_FONT_NAME {
            return Ok(format!("Luma/{}.brfnt", &stem[5..]));
        }
    }
    let mut named: Vec<&str> = FONTS.to_vec();
    named.push(MOD_FONT);
    let n = choice(&named, v, at, FONTS.len() as i64 - 1)? as usize;
    // The pattern itself names no font.
    if n >= FONTS.len() {
        return Err(refuse("Cook.Effect.Choice", &[("where", at.to_string()), ("value", MOD_FONT.to_string()), ("expected", named.join(", "))]));
    }
    Ok(format!("{}.brfnt", FONTS[n]))
}

/// How a mod's font is named, in the words of a refusal.
const MOD_FONT: &str = "Luma/<id>/<path>";
/// The longest font name the game's font loader takes (LumaFont's kMostName, 200 with its end).
const MOST_FONT_NAME: usize = 199;

pub(crate) fn name_ok(name: &str) -> bool {
    !name.is_empty() && name.len() <= NAME_MAX && name.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_')
}

fn name_of(v: &Value, at: &str) -> Result<String> {
    let name = text(v, at)?;
    if !name_ok(name) {
        return Err(refuse("Cook.Layout.Name", &[("where", at.to_string()), ("name", name.to_string())]));
    }
    Ok(name.to_string())
}

/// One number for both, or a list of two.
fn pair(v: &Value, at: &str) -> Result<[f32; 2]> {
    if v.is_number() {
        let x = as_f32(v, at)?;
        return Ok([x, x]);
    }
    let f = floats_of(v, 2, at)?;
    Ok([f[0], f[1]])
}

impl Reader<'_> {
    fn texture(&mut self, v: &Value, at: &str) -> Result<u16> {
        let given = text(v, at)?;
        let lower = given.to_ascii_lowercase();
        let name = [".png", ".tpl"].iter().find_map(|e| lower.ends_with(e).then(|| &given[..given.len() - e.len()])).unwrap_or(given);
        if name.is_empty() || name.len() > 64 || !name.bytes().all(|b| b.is_ascii_alphanumeric() || b"_-.".contains(&b)) {
            return Err(refuse("Cook.Effect.TextureName", &[("where", at.to_string()), ("name", given.to_string())]));
        }
        if let Some(file) = (self.missing_texture)(name) {
            return Err(refuse("Cook.Layout.NoTexture", &[("where", at.to_string()), ("name", name.to_string()), ("uri", file)]));
        }
        let entry = format!("{name}.tpl");
        let index = match self.textures.iter().position(|t| t.eq_ignore_ascii_case(&entry)) {
            Some(i) => i,
            None => {
                self.textures.push(entry);
                self.textures.len() - 1
            }
        };
        Ok(index as u16)
    }

    fn pane(&mut self, v: &Value, at: &str) -> Result<Pane> {
        let map = object(v, at)?;
        let kind = choice(&KINDS, map.get("type").ok_or_else(|| missing(at, "type"))?, &at_key(at, "type"), KINDS.len() as i64 - 1)? as usize;
        let mut parts: Vec<&str> = PANE_PARTS.to_vec();
        match kind {
            1 => parts.extend(PICTURE_PARTS),
            2 => parts.extend(TEXT_PARTS),
            4 => parts.extend(WINDOW_PARTS),
            _ => {}
        }
        only(map, at, &parts)?;
        let name = name_of(map.get("name").ok_or_else(|| missing(at, "name"))?, &at_key(at, "name"))?;
        if self.names.iter().any(|n| n.eq_ignore_ascii_case(&name)) {
            return Err(refuse("Cook.Layout.Twice", &[("where", at_key(at, "name")), ("name", name)]));
        }
        self.names.push(name.clone());
        if self.names.len() > PANES_MAX + 1 {
            return Err(refuse("Cook.Layout.TooMany", &[("count", (self.names.len() - 1).to_string()), ("max", PANES_MAX.to_string())]));
        }
        let get = |key: &str| map.get(key).map(|v| (v, at_key(at, key)));
        let flag_of = |key: &str, default: bool| -> Result<bool> {
            match get(key) {
                Some((v, at)) => v.as_bool().ok_or_else(|| not_kind(&at, "Cook.Effect.Is.Bool")),
                None => Ok(default),
            }
        };
        let position = match get("position") {
            Some((v, at)) => pair_list(v, &at)?,
            None => [0.0, 0.0],
        };
        let size = match get("size") {
            Some((v, at)) => {
                let s = pair_list(v, &at)?;
                if s[0] < 0.0 || s[1] < 0.0 {
                    return Err(range(&at, format!("{} {}", s[0], s[1]), 0, "3.4e38"));
                }
                s
            }
            // A plain pane groups others and may have no size; what draws or bounds needs one.
            None if kind == 0 => [0.0, 0.0],
            None => return Err(missing(at, "size")),
        };
        let scale = match get("scale") {
            Some((v, at)) => pair(v, &at)?,
            None => [1.0, 1.0],
        };
        let rotate = match get("rotation") {
            Some((v, at)) if v.is_number() => [0.0, 0.0, as_f32(v, &at)?],
            Some((v, at)) => {
                let r = floats_of(v, 3, &at)?;
                [r[0], r[1], r[2]]
            }
            None => [0.0; 3],
        };
        let anchor = match get("anchor") {
            Some((v, at)) => choice(&ANCHORS, v, &at, 8)? as u8,
            None => 4,
        };
        let alpha = match get("alpha") {
            Some((v, at)) => as_int(v, &at, 0, 255)? as u8,
            None => 255,
        };
        let flag = u8::from(flag_of("visible", true)?) | u8::from(flag_of("alpha_to_children", true)?) << 1 | u8::from(flag_of("widen", false)?) << 2;
        let body = match kind {
            1 => self.picture(map, at)?,
            2 => self.text(map, at, size)?,
            3 => Body::Bounding,
            4 => self.window(&name, map, at)?,
            _ => Body::Pane,
        };
        let material = match &body {
            Body::Picture { texture, .. } => Some((name.clone(), *texture)),
            Body::Text { .. } | Body::Window { .. } => Some((name.clone(), None)),
            _ => None,
        }
        .map(|m| {
            self.materials.push(m);
            (self.materials.len() - 1) as u16
        });
        let mut children = Vec::new();
        if let Some((v, at)) = get("children") {
            for (i, child) in list(v, &at)?.iter().enumerate() {
                children.push(self.pane(child, &at_index(&at, i))?);
            }
        }
        self.facts.panes += 1;
        Ok(Pane { name, flag, anchor, alpha, translate: [position[0], position[1], 0.0], rotate, scale, size, body, material, children })
    }

    fn picture(&mut self, map: &Map<String, Value>, at: &str) -> Result<Body> {
        self.facts.pictures += 1;
        let texture = match map.get("texture") {
            Some(v) => Some(self.texture(v, &at_key(at, "texture"))?),
            None => None,
        };
        let corners = corners(map, at, "corners", 4)?;
        Ok(Body::Picture { corners: [corners[0], corners[1], corners[2], corners[3]], texture })
    }

    /// A window: its frame's material (`<name>_F`, the corner texture) made here; its content's is the pane's.
    fn window(&mut self, name: &str, map: &Map<String, Value>, at: &str) -> Result<Body> {
        self.facts.windows += 1;
        let frame = self.texture(map.get("frame").ok_or_else(|| missing(at, "frame"))?, &at_key(at, "frame"))?;
        self.materials.push((format!("{name}_F"), Some(frame)));
        let frame = (self.materials.len() - 1) as u16;
        let inflation = match map.get("inflation") {
            Some(v) => {
                let f = floats_of(v, 4, &at_key(at, "inflation"))?;
                [f[0], f[1], f[2], f[3]]
            }
            None => [0.0; 4],
        };
        let corners = corners(map, at, "corners", 4)?;
        Ok(Body::Window { corners: [corners[0], corners[1], corners[2], corners[3]], inflation, frame })
    }

    fn text(&mut self, map: &Map<String, Value>, at: &str, size: [f32; 2]) -> Result<Body> {
        self.facts.texts += 1;
        let words = match map.get("text") {
            Some(v) => text(v, &at_key(at, "text"))?,
            None => "",
        };
        let units: Vec<u16> = words.encode_utf16().collect();
        if words.chars().any(|c| (c as u32) < 0x20 && c != '\n') {
            return Err(refuse("Cook.Layout.Control", &[("where", at_key(at, "text"))]));
        }
        let length = match map.get("length") {
            Some(v) => as_int(v, &at_key(at, "length"), 1, TEXT_MAX)?,
            None => TEXT_MAX,
        };
        if units.len() as i64 > length {
            return Err(refuse("Cook.Layout.TextLong", &[("where", at_key(at, "text")), ("count", units.len().to_string()), ("max", length.to_string())]));
        }
        let font = match map.get("font") {
            Some(v) => font_name(v, &at_key(at, "font"))?,
            None => format!("{}.brfnt", FONTS[0]),
        };
        if let Some(stem) = font.strip_suffix(".brfnt").filter(|s| s.starts_with("Luma/")) {
            if let Some(file) = (self.missing_font)(stem) {
                return Err(refuse("Cook.Layout.NoFont", &[("where", at_key(at, "font")), ("name", stem.to_string()), ("uri", file)]));
            }
        }
        let font = match self.fonts.iter().position(|f| *f == font) {
            Some(i) => i,
            None => {
                self.fonts.push(font);
                self.fonts.len() - 1
            }
        } as u16;
        let font_size = match map.get("font_size") {
            Some(v) => pair(v, &at_key(at, "font_size"))?,
            None => [26.0, 26.0],
        };
        let place = match map.get("place") {
            Some(v) => choice(&ANCHORS, v, &at_key(at, "place"), 8)? as u8,
            None => 4,
        };
        let align = match map.get("align") {
            Some(v) => choice(&ALIGNS, v, &at_key(at, "align"), 3)? as u8,
            None => 0,
        };
        let colours = corners(map, at, "colours", 2)?;
        let number = |key: &str| map.get(key).map_or(Ok(0.0), |v| as_f32(v, &at_key(at, key)));
        // Layer 6, overflow: the text in the file must fit its pane, as the game draws it (lines of the font's
        // height and the line space; a line's least width from NARROWEST). The pane's size is where the text is
        // placed: text past it is drawn over its neighbours.
        let (char_space, line_space) = (number("char_space")?, number("line_space")?);
        if !words.is_empty() {
            let lines: Vec<&str> = words.split('\n').collect();
            let height = lines.len() as f32 * font_size[1] + (lines.len() - 1) as f32 * line_space;
            if height > size[1] + 0.5 {
                return Err(refuse("Cook.Layout.TextTall", &[("where", at_key(at, "text")), ("lines", lines.len().to_string()), ("height", format!("{height:.1}")), ("max", format!("{:.1}", size[1]))]));
            }
            for line in &lines {
                let count = line.chars().count();
                let least: f32 = line.chars().map(|c| if full_width(c) { 1.0 } else { NARROWEST }).sum::<f32>() * font_size[0] + count.saturating_sub(1) as f32 * char_space;
                if least > size[0] + 0.5 {
                    return Err(refuse("Cook.Layout.TextWide", &[("where", at_key(at, "text")), ("line", line.to_string()), ("width", format!("{least:.1}")), ("max", format!("{:.1}", size[0]))]));
                }
            }
        }
        let mut text = units;
        text.push(0);
        Ok(Body::Text {
            text,
            length: length as u16,
            font,
            place,
            align,
            colours: [colours[0], colours[1]],
            font_size,
            char_space,
            line_space,
        })
    }
}

/// Whether a character is full width in the game's fonts: CJK, kana, Hangul and the full-width forms.
fn full_width(c: char) -> bool {
    matches!(c as u32, 0x1100..=0x115F | 0x2E80..=0xA4CF | 0xAC00..=0xD7A3 | 0xF900..=0xFAFF | 0xFE30..=0xFE4F | 0xFF00..=0xFF60 | 0xFFE0..=0xFFE6)
}

/// A list of exactly two numbers.
fn pair_list(v: &Value, at: &str) -> Result<[f32; 2]> {
    let f = floats_of(v, 2, at)?;
    Ok([f[0], f[1]])
}

/// `colour` (one for all) or `key` (one each, `count` of them); white when neither is given.
fn corners(map: &Map<String, Value>, at: &str, key: &str, count: usize) -> Result<Vec<[u8; 4]>> {
    match (map.get("colour"), map.get(key)) {
        (Some(_), Some(_)) => Err(refuse("Cook.Layout.Both", &[("where", at.to_string()), ("key", "colour".into()), ("other", key.to_string())])),
        (Some(v), None) => Ok(vec![parse_rgba(v, &at_key(at, "colour"))?; count]),
        (None, Some(v)) => {
            let at = at_key(at, key);
            let items = list(v, &at)?;
            if items.len() != count {
                return Err(length(&at, count, items.len()));
            }
            items.iter().enumerate().map(|(i, c)| parse_rgba(c, &at_index(&at, i))).collect()
        }
        (None, None) => Ok(vec![[255; 4]; count]),
    }
}

/// Big-endian writing.
fn be16(out: &mut Vec<u8>, v: u16) {
    out.extend_from_slice(&v.to_be_bytes());
}
fn be32(out: &mut Vec<u8>, v: u32) {
    out.extend_from_slice(&v.to_be_bytes());
}
fn bef(out: &mut Vec<u8>, v: &[f32]) {
    for x in v {
        out.extend_from_slice(&x.to_be_bytes());
    }
}
fn fixed(out: &mut Vec<u8>, name: &str, len: usize) {
    let mut field = vec![0u8; len];
    field[..name.len()].copy_from_slice(name.as_bytes());
    out.extend_from_slice(&field);
}

struct Blocks {
    out: Vec<u8>,
    count: u16,
}

impl Blocks {
    fn add(&mut self, kind: &[u8; 4], body: &[u8]) {
        let size = (8 + body.len()).div_ceil(4) * 4;
        self.out.extend_from_slice(kind);
        be32(&mut self.out, size as u32);
        self.out.extend_from_slice(body);
        self.out.resize(self.out.len() + size - 8 - body.len(), 0);
        self.count += 1;
    }

    /// txl1 or fnl1: a count, an (offset, 0) per name from the entries' start, the names.
    fn names(&mut self, kind: &[u8; 4], names: &[String]) {
        let mut body = Vec::new();
        be16(&mut body, names.len() as u16);
        be16(&mut body, 0);
        let mut strings = Vec::new();
        for name in names {
            be32(&mut body, (8 * names.len() + strings.len()) as u32);
            be32(&mut body, 0);
            strings.extend_from_slice(name.as_bytes());
            strings.push(0);
        }
        body.extend_from_slice(&strings);
        self.add(kind, &body);
    }

    fn pane(&mut self, p: &Pane) {
        let mut b = Vec::new();
        b.extend_from_slice(&[p.flag, p.anchor, p.alpha, 0]);
        fixed(&mut b, &p.name, 16);
        b.extend_from_slice(&[0; 8]);
        bef(&mut b, &p.translate);
        bef(&mut b, &p.rotate);
        bef(&mut b, &p.scale);
        bef(&mut b, &p.size);
        let material = p.material.unwrap_or(0);
        let kind = match &p.body {
            Body::Pane => b"pan1",
            Body::Bounding => b"bnd1",
            Body::Picture { corners, texture } => {
                for c in corners {
                    b.extend_from_slice(c);
                }
                be16(&mut b, material);
                b.extend_from_slice(&[u8::from(texture.is_some()), 0]);
                if texture.is_some() {
                    // The whole picture: top left, top right, bottom left, bottom right.
                    bef(&mut b, &[0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0]);
                }
                b"pic1"
            }
            Body::Text { text, length, font, place, align, colours, font_size, char_space, line_space } => {
                be16(&mut b, (length + 1) * 2);
                be16(&mut b, (text.len() * 2) as u16);
                be16(&mut b, material);
                be16(&mut b, *font);
                b.extend_from_slice(&[*place, *align, 0, 0]);
                // The text follows the record: 8 + 0x44 + 40 bytes from the block's start.
                be32(&mut b, 0x74);
                b.extend_from_slice(&colours[0]);
                b.extend_from_slice(&colours[1]);
                bef(&mut b, font_size);
                bef(&mut b, &[*char_space, *line_space]);
                for unit in text {
                    be16(&mut b, *unit);
                }
                b"txt1"
            }
            Body::Window { corners, inflation, frame } => {
                // After the pane (0x4C from the block's start): inflation, the frame count, the content's and the
                // frame table's offsets; the content (colours, material, no coordinates), the table, the one frame.
                bef(&mut b, inflation);
                b.extend_from_slice(&[1, 0, 0, 0]);
                be32(&mut b, 0x68);
                be32(&mut b, 0x7C);
                for c in corners {
                    b.extend_from_slice(c);
                }
                be16(&mut b, material);
                b.extend_from_slice(&[0, 0]);
                be32(&mut b, 0x80);
                be16(&mut b, *frame);
                b.extend_from_slice(&[0, 0]);
                b"wnd1"
            }
        };
        self.add(kind, &b);
        if !p.children.is_empty() {
            self.add(b"pas1", &[]);
            for child in &p.children {
                self.pane(child);
            }
            self.add(b"pae1", &[]);
        }
    }
}

/// A material as the game's layouts write one for a picture or a text: black to white across the texture (or the
/// vertex colours alone), no stages of its own, so the game's default stages draw it; with a texture, one map
/// (clamped, linear), one identity transform and one coordinate generator.
fn material(name: &str, texture: Option<u16>) -> Vec<u8> {
    let mut m = Vec::new();
    fixed(&mut m, name, 20);
    for register in [[0i16; 4], [255; 4], [255; 4]] {
        for c in register {
            be16(&mut m, c as u16);
        }
    }
    m.extend_from_slice(&[255; 16]);
    match texture {
        Some(t) => {
            be32(&mut m, 0x111);
            be16(&mut m, t);
            m.extend_from_slice(&[0, 0]);
            bef(&mut m, &[0.0, 0.0, 0.0, 1.0, 1.0]);
            m.extend_from_slice(&[1, 4, 0x1E, 0]);
        }
        None => be32(&mut m, 0),
    }
    m
}

/// The layout source `bytes` as the game's layout file, and its facts. `missing_texture` answers for a texture name
/// the file the mod lacks for it, or None when the mod has it (or when that cannot be known here).
pub fn to_brlyt(bytes: &[u8], missing_texture: &dyn Fn(&str) -> Option<String>) -> Result<(Vec<u8>, LayoutFacts)> {
    to_brlyt_with(bytes, missing_texture, &|_| None)
}

/// As `to_brlyt`; `missing_font` answers for a mod font a text names (`Luma/<id>/<path>`) with the source the mod
/// lacks (`layers/<l>/assets/<path>.font.json`), None when it has it or this cannot be looked at here.
pub fn to_brlyt_with(bytes: &[u8], missing_texture: &dyn Fn(&str) -> Option<String>, missing_font: &dyn Fn(&str) -> Option<String>) -> Result<(Vec<u8>, LayoutFacts)> {
    to_files(bytes, "", missing_texture, missing_font).map(|(brlyt, _, facts)| (brlyt, facts))
}

/// As `to_brlyt_with`, with the layout's animations (layer 3): each `anim/<stem>_<animation>.brlan` and its bytes,
/// `stem` the layout's name.
pub fn to_files(bytes: &[u8], stem: &str, missing_texture: &dyn Fn(&str) -> Option<String>, missing_font: &dyn Fn(&str) -> Option<String>) -> Result<(Vec<u8>, Vec<(String, Vec<u8>)>, LayoutFacts)> {
    let root: Value = serde_json::from_slice(bytes).map_err(|e| refuse("Cook.Model.Json", &[("line", e.line().to_string()), ("column", e.column().to_string())]))?;
    let map = object(&root, "/")?;
    if map.contains_key("patch") {
        // A patch of a game layout by pane name is a merge (layer 5): layers/<l>/merge/<archive>/<Name>.layout.json.
        return Err(refuse("Cook.Layout.PatchPlace", &[("where", "/patch".to_string())]));
    }
    only(map, "/", &["size", "panes", "groups", "animations"])?;
    let size = match map.get("size") {
        Some(v) => {
            let s = pair_list(v, "/size")?;
            if s[0] <= 0.0 || s[1] <= 0.0 {
                return Err(range("/size", format!("{} {}", s[0], s[1]), 1, "3.4e38"));
            }
            s
        }
        None => SCREEN,
    };
    let mut reader = Reader { names: vec![ROOT.to_string()], textures: Vec::new(), fonts: Vec::new(), materials: Vec::new(), facts: LayoutFacts::default(), missing_texture, missing_font };
    let mut children = Vec::new();
    for (i, pane) in list(map.get("panes").ok_or_else(|| missing("/", "panes"))?, "/panes")?.iter().enumerate() {
        children.push(reader.pane(pane, &at_index("/panes", i))?);
    }
    let mut groups: Vec<(String, Vec<String>)> = Vec::new();
    if let Some(v) = map.get("groups") {
        for (i, group) in list(v, "/groups")?.iter().enumerate() {
            let at = at_index("/groups", i);
            let g = object(group, &at)?;
            only(g, &at, &["name", "panes"])?;
            let name = name_of(g.get("name").ok_or_else(|| missing(&at, "name"))?, &at_key(&at, "name"))?;
            if name.eq_ignore_ascii_case("RootGroup") || groups.iter().any(|(n, _)| n.eq_ignore_ascii_case(&name)) {
                return Err(refuse("Cook.Layout.Twice", &[("where", at_key(&at, "name")), ("name", name)]));
            }
            let at_panes = at_key(&at, "panes");
            let mut members = Vec::new();
            for (k, member) in list(g.get("panes").ok_or_else(|| missing(&at, "panes"))?, &at_panes)?.iter().enumerate() {
                let at_member = at_index(&at_panes, k);
                let member = text(member, &at_member)?;
                match reader.names.iter().skip(1).find(|n| n.eq_ignore_ascii_case(member)) {
                    Some(n) => members.push(n.clone()),
                    None => return Err(refuse("Cook.Layout.NoPane", &[("where", at_member), ("name", member.to_string())])),
                }
            }
            groups.push((name, members));
        }
    }
    let animations = match map.get("animations") {
        Some(v) => super::layout_anim::read(v, &reader.names[1..])?,
        None => Vec::new(),
    };
    reader.facts.textures = reader.textures.len();
    reader.facts.groups = groups.len();
    reader.facts.animations = animations.len();
    let anim_files: Vec<(String, Vec<u8>)> = animations.iter().map(|a| (format!("anim/{stem}_{}.brlan", a.name), super::layout_anim::to_brlan(a))).collect();
    let root_pane = Pane {
        name: ROOT.to_string(),
        flag: 1,
        anchor: 4,
        alpha: 255,
        translate: [0.0; 3],
        rotate: [0.0; 3],
        scale: [1.0, 1.0],
        size,
        body: Body::Pane,
        material: None,
        children,
    };

    let mut blocks = Blocks { out: Vec::new(), count: 0 };
    let mut lyt = vec![1, 0, 0, 0];
    bef(&mut lyt, &size);
    blocks.add(b"lyt1", &lyt);
    if !reader.textures.is_empty() {
        blocks.names(b"txl1", &reader.textures);
    }
    if !reader.fonts.is_empty() {
        blocks.names(b"fnl1", &reader.fonts);
    }
    if !reader.materials.is_empty() {
        let made: Vec<Vec<u8>> = reader.materials.iter().map(|(name, texture)| material(name, *texture)).collect();
        let mut body = Vec::new();
        be16(&mut body, made.len() as u16);
        be16(&mut body, 0);
        let mut at = 8 + 4 + 4 * made.len();
        for m in &made {
            be32(&mut body, at as u32);
            at += m.len();
        }
        for m in &made {
            body.extend_from_slice(m);
        }
        blocks.add(b"mat1", &body);
    }
    blocks.pane(&root_pane);
    let mut root_group = Vec::new();
    fixed(&mut root_group, "RootGroup", 16);
    be16(&mut root_group, 0);
    be16(&mut root_group, 0);
    blocks.add(b"grp1", &root_group);
    if !groups.is_empty() {
        blocks.add(b"grs1", &[]);
        for (name, members) in &groups {
            let mut g = Vec::new();
            fixed(&mut g, name, 16);
            be16(&mut g, members.len() as u16);
            be16(&mut g, 0);
            for m in members {
                fixed(&mut g, m, 16);
            }
            blocks.add(b"grp1", &g);
        }
        blocks.add(b"gre1", &[]);
    }

    let mut out = Vec::with_capacity(16 + blocks.out.len());
    out.extend_from_slice(b"RLYT");
    out.extend_from_slice(&[0xFE, 0xFF, 0x00, 0x0A]);
    be32(&mut out, (16 + blocks.out.len()) as u32);
    be16(&mut out, 16);
    be16(&mut out, blocks.count);
    out.extend_from_slice(&blocks.out);
    Ok((out, anim_files, reader.facts))
}

#[cfg(test)]
#[path = "layout_json_tests.rs"]
mod tests;
