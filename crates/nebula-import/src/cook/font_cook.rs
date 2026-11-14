//! A mod's font (docs/modding/content/screens.md section 2a): `Luma/<id>/<path>.font.json` names a TrueType or OpenType
//! file beside it. The cook draws the characters it asks for into the game's own font form, a BRFNT with intensity
//! sheets, which the store then cooks as it cooks the game's fonts (single-channel distance sheets, NFNT). Beside it go
//! `<path>.kern`, the font's pair kerning, and `<path>.fallback`, the game font that draws what the font lacks (the
//! files the run time reads, docs/modding/services/ui.md).
//!
//! How a cell sits (CustomTagProcessor's PrintImpl and CharWriter::PrintGlyph): the cursor stands on the line's
//! baseline, the font's ascent below the line's top; a cell is drawn with its top its baseline row above the cursor,
//! `left` right of it, and the cursor moves on by `charWidth`. The scale is the text's font size over the font's
//! `height` (and `width` across), so a layout's `font_size` N makes the font's ascent plus descent N high.

use super::particle_json::{as_int, at_index, at_key, choice, list, missing, object, only, text};
use super::refuse::{note, refuse};
use anyhow::Result;
use serde_json::Value;
use std::collections::BTreeMap;
use ttf_parser::{Face, GlyphId, OutlineBuilder};

/// The size a font is drawn at (pixels an em), least, most and when the source gives none.
pub const SIZE_LEAST: i64 = 8;
pub const SIZE_MOST: i64 = 96;
pub const SIZE_DEFAULT: i64 = 48;
/// Empty pixels kept around a glyph's ink inside its cell: the distance field's reach (distance::SPREAD).
const MARGIN: i32 = super::distance::SPREAD as i32;
/// A sheet's most pixels each way (its distance sheet is distance::SCALE times that).
const SHEET_MOST: usize = 1024;
/// The most sheets a font has: 32 sheets of 1024 x 1024 are 128 MiB of distance sheets.
pub const MOST_SHEETS: usize = 32;
/// Sub-scanlines a pixel row when a glyph's coverage is measured.
const SUB: usize = 16;
/// Characters a font draws with no `glyphs` given (and with it, too): Latin, its punctuation and the euro sign. The ones
/// the font lacks are drawn by the fallback.
const LATIN: [(u32, u32); 5] = [(0x20, 0x7E), (0xA0, 0x24F), (0x2010, 0x2027), (0x2030, 0x203A), (0x20AC, 0x20AC)];
/// Kerning is read between characters below this (the scripts fonts kern; CJK fonts keep their cells).
const KERN_BELOW: u32 = 0x3000;
/// Characters a Missing note lists.
const LISTED: usize = 20;
/// The most kerning pairs the run time reads (docs/modding/services/ui.md).
pub const MOST_KERNS: usize = 65_536;
const PARTS: [&str; 4] = ["file", "size", "glyphs", "fallback"];
const GLYPH_PARTS: [&str; 3] = ["chars", "ranges", "all"];

/// Files made: each one's store path, bytes, and whether they are the store's already (else the store's cook reads them).
pub type Made = Vec<(String, Vec<u8>, bool)>;

/// What `cook.font` says of one font.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct FontFacts {
    pub glyphs: usize,
    pub sheets: usize,
    /// Kerning pairs in the `.kern` file.
    pub kern: usize,
    pub size: u32,
    /// Characters the source asks for by `chars` or `ranges` that the font does not draw.
    pub missing: usize,
    pub fallback: String,
}

/// A font's source: `<path>.font.json`.
pub fn is_font_source(lower: &str) -> bool {
    lower.ends_with(".font.json")
}

/// A TrueType or OpenType file in a mod's assets (`Luma/<id>/...`): what a `.font.json` reads, not a game file.
pub fn is_asset_source(path: &str) -> bool {
    let lower = path.trim_start_matches('/').to_ascii_lowercase();
    lower.starts_with("luma/") && [".ttf", ".otf", ".ttc", ".otc"].iter().any(|e| lower.ends_with(e))
}

/// The files a font source makes: (`<path>.brfnt`, the BRFNT for the store's font cook), (`<path>.kern`, NTBL),
/// (`<path>.fallback`, the fallback's name and a line end), each with whether it is the store's already.
pub fn cook(json: &[u8], path: &str, file: &dyn Fn(&str) -> Option<Vec<u8>>) -> Result<(Made, FontFacts)> {
    let root: Value = serde_json::from_slice(json).map_err(|e| refuse("Cook.Model.Json", &[("line", e.line().to_string()), ("column", e.column().to_string())]))?;
    let map = object(&root, "/")?;
    only(map, "/", &PARTS)?;
    let uri = text(map.get("file").ok_or_else(|| missing("/", "file"))?, "/file")?;
    if uri.is_empty() || uri.contains("..") || uri.contains(':') || uri.contains('\\') || uri.starts_with('/') {
        return Err(refuse("Cook.Effect.FilePath", &[("where", "/file".into()), ("uri", uri.to_string())]));
    }
    let size = match map.get("size") {
        Some(v) => as_int(v, "/size", SIZE_LEAST, SIZE_MOST)?,
        None => SIZE_DEFAULT,
    } as u32;
    let fallback = match map.get("fallback") {
        Some(v) => super::layout_json::FONTS[choice(&super::layout_json::FONTS, v, "/fallback", super::layout_json::FONTS.len() as i64 - 1)? as usize],
        None => super::layout_json::FONTS[0],
    };
    let (asked, all) = asked_codes(map.get("glyphs"))?;
    let bytes = file(uri).ok_or_else(|| refuse("Cook.Effect.FileMissing", &[("where", "/file".into()), ("uri", uri.to_string())]))?;
    let face = Face::parse(&bytes, 0).map_err(|_| refuse("Cook.Font.Unreadable", &[("uri", uri.to_string())]))?;

    // The characters: Latin and what the source asks for (or every one the font maps), each the font draws.
    let mut wanted: BTreeMap<u16, GlyphId> = BTreeMap::new();
    for &(first, last) in &LATIN {
        for code in first..=last {
            if let Some(g) = char::from_u32(code).and_then(|c| face.glyph_index(c)) {
                wanted.insert(code as u16, g);
            }
        }
    }
    if all {
        if let Some(cmap) = face.tables().cmap {
            for table in cmap.subtables.into_iter().filter(|t| t.is_unicode()) {
                table.codepoints(|code| {
                    if code <= 0xFFFF && !is_surrogate(code) {
                        if let Some(g) = char::from_u32(code).and_then(|c| face.glyph_index(c)) {
                            wanted.insert(code as u16, g);
                        }
                    }
                });
            }
        }
    }
    let mut lacking = Vec::new();
    for &code in &asked {
        match char::from_u32(code).and_then(|c| face.glyph_index(c)) {
            Some(g) => {
                wanted.insert(code as u16, g);
            }
            None => lacking.push(code),
        }
    }
    if wanted.is_empty() {
        return Err(refuse("Cook.Font.NoGlyph", &[("uri", uri.to_string())]));
    }
    if !lacking.is_empty() {
        let mut chars: String = lacking.iter().take(LISTED).map(|&c| format!("U+{c:04X}")).collect::<Vec<_>>().join(" ");
        if lacking.len() > LISTED {
            chars.push_str(" ...");
        }
        note("Cook.Font.Missing", &[("uri", uri.to_string()), ("count", lacking.len().to_string()), ("chars", chars), ("fallback", fallback.to_string())]);
    }

    let codes: Vec<(u16, GlyphId)> = wanted.into_iter().collect();
    let metrics = match Metrics::of(&face, &codes, size) {
        Ok(m) => m,
        Err(()) => {
            let most = (SIZE_LEAST as u32..size).rev().find(|&s| Metrics::of(&face, &codes, s).is_ok());
            return Err(refuse("Cook.Font.TooLarge", &[("uri", uri.to_string()), ("size", size.to_string()), ("most", most.map_or_else(|| "none".into(), |m| m.to_string()))]));
        }
    };
    let layout = SheetLayout::of(metrics.cell_width, metrics.cell_height, codes.len());
    if layout.sheets > MOST_SHEETS {
        let fits = MOST_SHEETS * layout.across * layout.down;
        return Err(refuse("Cook.Font.TooMany", &[("uri", uri.to_string()), ("count", codes.len().to_string()), ("max", fits.to_string()), ("size", size.to_string())]));
    }
    let brfnt = write_brfnt(&face, &codes, &metrics, &layout);
    let mut kern = kerning(&face, &codes, f64::from(size) / f64::from(face.units_per_em().max(1)));
    if kern.len() > MOST_KERNS {
        // The pairs that move most are kept.
        let found = kern.len();
        kern.sort_by_key(|&(l, r, x)| (std::cmp::Reverse(x.unsigned_abs()), l, r));
        kern.truncate(MOST_KERNS);
        kern.sort_unstable();
        note("Cook.Font.KernCut", &[("uri", uri.to_string()), ("count", found.to_string()), ("max", MOST_KERNS.to_string())]);
    }
    let stem = &path[..path.len() - ".font.json".len()];
    let facts = FontFacts { glyphs: codes.len(), sheets: layout.sheets, kern: kern.len(), size, missing: lacking.len(), fallback: fallback.to_string() };
    Ok((
        vec![
            (format!("{stem}.brfnt"), brfnt, false),
            (format!("{stem}.kern"), kern_table(&kern)?, true),
            (format!("{stem}.fallback"), format!("{fallback}
").into_bytes(), true),
        ],
        facts,
    ))
}

fn is_surrogate(code: u32) -> bool {
    (0xD800..=0xDFFF).contains(&code)
}

/// A character code of `glyphs.ranges`: a number or "U+hhhh".
fn code_of(v: &Value, at: &str) -> Result<u32> {
    let code = match v {
        Value::Number(_) => as_int(v, at, 0, 0x10FFFF)? as u32,
        Value::String(s) => {
            let hex = s.strip_prefix("U+").or_else(|| s.strip_prefix("u+"));
            hex.filter(|h| (1..=6).contains(&h.len())).and_then(|h| u32::from_str_radix(h, 16).ok()).ok_or_else(|| refuse("Cook.Font.Code", &[("where", at.to_string()), ("value", s.clone())]))?
        }
        _ => return Err(refuse("Cook.Font.Code", &[("where", at.to_string()), ("value", super::particle_json::shown(v))])),
    };
    if code > 0xFFFF || is_surrogate(code) {
        return Err(refuse("Cook.Font.Beyond", &[("where", at.to_string()), ("char", format!("U+{code:04X}"))]));
    }
    Ok(code)
}

/// The characters `glyphs` asks for by `chars` and `ranges`, and whether it asks for all the font maps.
fn asked_codes(glyphs: Option<&Value>) -> Result<(Vec<u32>, bool)> {
    let Some(glyphs) = glyphs else { return Ok((Vec::new(), false)) };
    let at = "/glyphs";
    let map = object(glyphs, at)?;
    only(map, at, &GLYPH_PARTS)?;
    let mut codes = Vec::new();
    if let Some(chars) = map.get("chars") {
        let where_ = at_key(at, "chars");
        for c in text(chars, &where_)?.chars() {
            let code = c as u32;
            if code > 0xFFFF {
                return Err(refuse("Cook.Font.Beyond", &[("where", where_), ("char", format!("U+{code:04X}"))]));
            }
            if !c.is_control() {
                codes.push(code);
            }
        }
    }
    if let Some(ranges) = map.get("ranges") {
        let where_ = at_key(at, "ranges");
        for (i, range) in list(ranges, &where_)?.iter().enumerate() {
            let at_range = at_index(&where_, i);
            let pair = list(range, &at_range)?;
            if pair.len() != 2 {
                return Err(super::particle_json::length(&at_range, 2, pair.len()));
            }
            let first = code_of(&pair[0], &at_index(&at_range, 0))?;
            let last = code_of(&pair[1], &at_index(&at_range, 1))?;
            if first > last {
                return Err(refuse("Cook.Font.RangeOrder", &[("where", at_range)]));
            }
            codes.extend((first..=last).filter(|&c| !is_surrogate(c) && !char::from_u32(c).is_some_and(char::is_control)));
        }
    }
    let all = match map.get("all") {
        Some(v) => v.as_bool().ok_or_else(|| super::particle_json::not_kind(&at_key(at, "all"), "Cook.Effect.Is.Bool"))?,
        None => false,
    };
    codes.sort_unstable();
    codes.dedup();
    Ok((codes, all))
}

/// A font's sizes in pixels at one size, each within what the game's font form holds.
struct Metrics {
    scale: f32,
    /// The line's top to its baseline, and its baseline to its bottom (the font's ascender and descender).
    ascent: u8,
    descent: u8,
    linefeed: i8,
    cell_width: u8,
    cell_height: u8,
    /// The cell's baseline row: the highest ink's top above it, and the margin.
    baseline: u8,
    widest: u8,
    /// Each glyph's (left, glyph width, char width), in the order of the codes.
    widths: Vec<[u8; 3]>,
    /// Each glyph's left edge in source pixels: where its cell's column 0 is.
    origin_x: Vec<i32>,
}

impl Metrics {
    fn of(face: &Face, codes: &[(u16, GlyphId)], size: u32) -> std::result::Result<Self, ()> {
        let scale = size as f32 / f32::from(face.units_per_em().max(1));
        let px = |v: i16| f32::from(v) * scale;
        let ascent = px(face.ascender()).round();
        let descent = (-px(face.descender())).round().max(0.0);
        let linefeed = (ascent + descent + px(face.line_gap()).max(0.0)).round();
        let (mut top, mut bottom, mut widest_ink) = (0i32, 0i32, 0i32);
        let mut boxes = Vec::with_capacity(codes.len());
        for &(_, glyph) in codes {
            let b = face.glyph_bounding_box(glyph).map(|r| {
                let (x0, x1) = ((px(r.x_min)).floor() as i32, (px(r.x_max)).ceil() as i32);
                top = top.max(px(r.y_max).ceil() as i32);
                bottom = bottom.max((-px(r.y_min)).ceil() as i32);
                widest_ink = widest_ink.max(x1 - x0);
                (x0, x1)
            });
            boxes.push(b);
        }
        let cell_width = widest_ink + 2 * MARGIN;
        let baseline = top.max(0) + MARGIN;
        let cell_height = baseline + bottom.max(0) + MARGIN;
        let mut widths = Vec::with_capacity(codes.len());
        let mut origin_x = Vec::with_capacity(codes.len());
        let mut widest = 0;
        for (&(_, glyph), b) in codes.iter().zip(&boxes) {
            let advance = (f32::from(face.glyph_hor_advance(glyph).unwrap_or(0)) * scale).round() as i32;
            let (left, width) = match b {
                Some((x0, x1)) => (x0 - MARGIN, x1 - x0 + 2 * MARGIN),
                None => (0, 0),
            };
            if !(-128..=127).contains(&left) || !(0..=127).contains(&advance) {
                return Err(());
            }
            widest = widest.max(advance);
            widths.push([left as i8 as u8, width as u8, advance as u8]);
            origin_x.push(left);
        }
        let fits = |v: f32, most: f32| v >= 0.0 && v <= most;
        if !fits(ascent + descent, 255.0) || !fits(ascent, 255.0) || !fits(linefeed, 127.0) || cell_width > 255 || cell_height > 255 || baseline > 127 {
            return Err(());
        }
        // A cell must fit a sheet with its gap pixel.
        if cell_width as usize + 2 > SHEET_MOST || cell_height as usize + 2 > SHEET_MOST {
            return Err(());
        }
        Ok(Metrics {
            scale,
            ascent: ascent as u8,
            descent: descent as u8,
            linefeed: linefeed as i8,
            cell_width: cell_width.max(1) as u8,
            cell_height: cell_height.max(1) as u8,
            baseline: baseline as u8,
            widest: widest as u8,
            widths,
            origin_x,
        })
    }
}

/// How cells lie on sheets: `across` by `down` cells a sheet, one gap pixel before each (as the game's fonts).
struct SheetLayout {
    across: usize,
    down: usize,
    width: usize,
    height: usize,
    sheets: usize,
}

impl SheetLayout {
    fn of(cell_width: u8, cell_height: u8, glyphs: usize) -> Self {
        let (cw, ch) = (cell_width as usize + 1, cell_height as usize + 1);
        let across = ((SHEET_MOST - 1) / cw).clamp(1, glyphs.max(1));
        let rows = glyphs.div_ceil(across);
        let down = ((SHEET_MOST - 1) / ch).clamp(1, rows.max(1));
        // I8 tiles are 8 x 4 pixels.
        let width = (across * cw + 1).next_multiple_of(8);
        let height = (down * ch + 1).next_multiple_of(4);
        SheetLayout { across, down, width, height, sheets: glyphs.div_ceil(across * down).max(1) }
    }
}

/// A glyph's outline as lines in its cell's pixels (y down from the cell's top).
struct Outline {
    lines: Vec<[f32; 4]>,
    start: (f32, f32),
    at: (f32, f32),
    scale: f32,
    origin_x: f32,
    baseline: f32,
}

impl Outline {
    fn map(&self, x: f32, y: f32) -> (f32, f32) {
        (x * self.scale - self.origin_x, self.baseline - y * self.scale)
    }
    fn line(&mut self, to: (f32, f32)) {
        if to != self.at {
            self.lines.push([self.at.0, self.at.1, to.0, to.1]);
        }
        self.at = to;
    }
    /// Segments for a curve whose control polygon is `length` pixels long: about one each half pixel, 2 to 64.
    fn steps(length: f32) -> usize {
        ((length * 2.0).sqrt().ceil() as usize).clamp(2, 64)
    }
}

impl OutlineBuilder for Outline {
    fn move_to(&mut self, x: f32, y: f32) {
        self.close();
        let p = self.map(x, y);
        self.start = p;
        self.at = p;
    }
    fn line_to(&mut self, x: f32, y: f32) {
        let p = self.map(x, y);
        self.line(p);
    }
    fn quad_to(&mut self, x1: f32, y1: f32, x: f32, y: f32) {
        let (p0, p1, p2) = (self.at, self.map(x1, y1), self.map(x, y));
        let length = dist(p0, p1) + dist(p1, p2);
        let n = Self::steps(length);
        for i in 1..=n {
            let t = i as f32 / n as f32;
            let u = 1.0 - t;
            let p = (u * u * p0.0 + 2.0 * u * t * p1.0 + t * t * p2.0, u * u * p0.1 + 2.0 * u * t * p1.1 + t * t * p2.1);
            self.line(if i == n { p2 } else { p });
        }
    }
    fn curve_to(&mut self, x1: f32, y1: f32, x2: f32, y2: f32, x: f32, y: f32) {
        let (p0, p1, p2, p3) = (self.at, self.map(x1, y1), self.map(x2, y2), self.map(x, y));
        let length = dist(p0, p1) + dist(p1, p2) + dist(p2, p3);
        let n = Self::steps(length);
        for i in 1..=n {
            let t = i as f32 / n as f32;
            let u = 1.0 - t;
            let (a, b, c, d) = (u * u * u, 3.0 * u * u * t, 3.0 * u * t * t, t * t * t);
            let p = (a * p0.0 + b * p1.0 + c * p2.0 + d * p3.0, a * p0.1 + b * p1.1 + c * p2.1 + d * p3.1);
            self.line(if i == n { p3 } else { p });
        }
    }
    fn close(&mut self) {
        let start = self.start;
        self.line(start);
    }
}

fn dist(a: (f32, f32), b: (f32, f32)) -> f32 {
    ((a.0 - b.0).powi(2) + (a.1 - b.1).powi(2)).sqrt()
}

/// How much of each pixel (`width` x `height`) the outline covers, 0 to 1, by the nonzero rule: `SUB` sub-scanlines
/// a row, each span's ends to the exact fraction of their pixel.
fn coverage(lines: &[[f32; 4]], width: usize, height: usize) -> Vec<f32> {
    let mut cover = vec![0f32; width * height];
    let mut crossings: Vec<(f32, i32)> = Vec::new();
    let weight = 1.0 / SUB as f32;
    for row in 0..height {
        let (top, bottom) = (row as f32, row as f32 + 1.0);
        let active: Vec<&[f32; 4]> = lines.iter().filter(|l| l[1].min(l[3]) < bottom && l[1].max(l[3]) > top).collect();
        if active.is_empty() {
            continue;
        }
        let cells = &mut cover[row * width..(row + 1) * width];
        for k in 0..SUB {
            let y = top + (k as f32 + 0.5) * weight;
            crossings.clear();
            for l in &active {
                let (x0, y0, x1, y1) = (l[0], l[1], l[2], l[3]);
                if (y0 <= y) != (y1 <= y) {
                    let t = (y - y0) / (y1 - y0);
                    crossings.push((x0 + t * (x1 - x0), if y1 > y0 { 1 } else { -1 }));
                }
            }
            crossings.sort_by(|a, b| a.0.total_cmp(&b.0));
            let (mut winding, mut from) = (0, 0.0);
            for &(x, direction) in &crossings {
                let before = winding;
                winding += direction;
                if before == 0 && winding != 0 {
                    from = x;
                } else if before != 0 && winding == 0 {
                    span(cells, from, x, weight);
                }
            }
        }
    }
    for c in &mut cover {
        *c = c.min(1.0);
    }
    cover
}

/// Adds `weight` times how much of each pixel [`from`, `to`) covers.
fn span(cells: &mut [f32], from: f32, to: f32, weight: f32) {
    let w = cells.len() as f32;
    let (from, to) = (from.clamp(0.0, w), to.clamp(0.0, w));
    if to <= from {
        return;
    }
    let (first, last) = (from.floor() as usize, (to.ceil() as usize).min(cells.len()));
    for (x, cell) in cells.iter_mut().enumerate().take(last).skip(first) {
        let covered = (to.min(x as f32 + 1.0) - from.max(x as f32)).max(0.0);
        *cell += covered * weight;
    }
}

fn put16(out: &mut [u8], at: usize, v: u16) {
    out[at..at + 2].copy_from_slice(&v.to_be_bytes());
}

fn put32(out: &mut [u8], at: usize, v: u32) {
    out[at..at + 4].copy_from_slice(&v.to_be_bytes());
}

fn block(out: &mut Vec<u8>, kind: &[u8; 4], data: &[u8]) -> usize {
    let at = out.len();
    out.extend_from_slice(kind);
    out.extend_from_slice(&[0; 4]);
    out.extend_from_slice(data);
    out.resize(out.len().next_multiple_of(4), 0);
    let size = (out.len() - at) as u32;
    put32(out, at + 4, size);
    at + 8
}

/// The font as the game's BRFNT: FINF, TGLP (I8 sheets in GX tiles), CWDH and one CMAP that lists its codes.
fn write_brfnt(face: &Face, codes: &[(u16, GlyphId)], m: &Metrics, l: &SheetLayout) -> Vec<u8> {
    let (cw, ch) = (m.cell_width as usize, m.cell_height as usize);
    let mut pixels = vec![vec![0u8; l.width * l.height]; l.sheets];
    for (index, &(_, glyph)) in codes.iter().enumerate() {
        let mut outline = Outline { lines: Vec::new(), start: (0.0, 0.0), at: (0.0, 0.0), scale: m.scale, origin_x: m.origin_x[index] as f32, baseline: f32::from(m.baseline) };
        if face.outline_glyph(glyph, &mut outline).is_none() {
            continue;
        }
        outline.close();
        let cover = coverage(&outline.lines, cw, ch);
        let cells = l.across * l.down;
        let (sheet, cell) = (index / cells, index % cells);
        let (x0, y0) = ((cell % l.across) * (cw + 1) + 1, (cell / l.across) * (ch + 1) + 1);
        for y in 0..ch {
            for x in 0..cw {
                pixels[sheet][(y0 + y) * l.width + x0 + x] = (cover[y * cw + x] * 255.0).round() as u8;
            }
        }
    }
    let sheet_size = l.width * l.height;
    let mut images = Vec::with_capacity(sheet_size * l.sheets);
    for sheet in &pixels {
        for ty in 0..l.height / 4 {
            for tx in 0..l.width / 8 {
                for py in 0..4 {
                    let at = (ty * 4 + py) * l.width + tx * 8;
                    images.extend_from_slice(&sheet[at..at + 8]);
                }
            }
        }
    }

    let alternate = ['\u{FFFD}', '?', ' ']
        .iter()
        .find_map(|&c| codes.iter().position(|&(code, _)| code == c as u16))
        .unwrap_or(0) as u16;
    let mut out = vec![0u8; 0x10];
    out[0..4].copy_from_slice(b"RFNT");
    put16(&mut out, 4, 0xFEFF);
    put16(&mut out, 6, 0x0104);
    put16(&mut out, 0xC, 0x10);
    put16(&mut out, 0xE, 4);
    let finf = block(&mut out, b"FINF", &[0; 0x18]);

    let mut tglp = vec![0u8; 0x18];
    tglp[0] = m.cell_width;
    tglp[1] = m.cell_height;
    tglp[2] = m.baseline;
    tglp[3] = m.widest;
    put32(&mut tglp, 4, sheet_size as u32);
    put16(&mut tglp, 8, l.sheets as u16);
    put16(&mut tglp, 10, 1); // I8
    put16(&mut tglp, 12, l.across as u16);
    put16(&mut tglp, 14, l.down as u16);
    put16(&mut tglp, 16, l.width as u16);
    put16(&mut tglp, 18, l.height as u16);
    // The sheets start 32-aligned after the block's own fields.
    let tglp_at = out.len();
    let images_at = (tglp_at + 8 + 0x18).next_multiple_of(32);
    put32(&mut tglp, 20, images_at as u32);
    tglp.resize(images_at - tglp_at - 8, 0);
    tglp.extend_from_slice(&images);
    let glyph_data = block(&mut out, b"TGLP", &tglp);

    let mut cwdh = vec![0u8; 8];
    put16(&mut cwdh, 0, 0);
    put16(&mut cwdh, 2, (codes.len() - 1) as u16);
    for w in &m.widths {
        cwdh.extend_from_slice(w);
    }
    let width_data = block(&mut out, b"CWDH", &cwdh);

    let mut cmap = vec![0u8; 0xC];
    put16(&mut cmap, 0, codes[0].0);
    put16(&mut cmap, 2, codes[codes.len() - 1].0);
    put16(&mut cmap, 4, 2); // a scan: (code, glyph) pairs
    cmap.extend_from_slice(&(codes.len() as u16).to_be_bytes());
    for (index, &(code, _)) in codes.iter().enumerate() {
        cmap.extend_from_slice(&code.to_be_bytes());
        cmap.extend_from_slice(&(index as u16).to_be_bytes());
    }
    let map_data = block(&mut out, b"CMAP", &cmap);

    let f = finf;
    out[f] = 1; // a font of glyph sheets
    out[f + 1] = m.linefeed as u8;
    put16(&mut out, f + 2, alternate);
    out[f + 4..f + 7].copy_from_slice(&m.widths[alternate as usize]);
    out[f + 7] = 1; // UTF-16
    put32(&mut out, f + 8, glyph_data as u32);
    put32(&mut out, f + 0xC, width_data as u32);
    put32(&mut out, f + 0x10, map_data as u32);
    let height = m.ascent + m.descent;
    out[f + 0x14] = height;
    // As wide as high: a font size of [n, n] keeps the font's shapes.
    out[f + 0x15] = height;
    out[f + 0x16] = m.ascent;
    let size = out.len() as u32;
    put32(&mut out, 8, size);
    out
}

/// The font's pair kerning between its characters below KERN_BELOW, in whole source pixels: its GPOS `kern` feature's
/// pair adjustments, or (with none) its `kern` table's horizontal pairs. Pairs of 0 are left out.
fn kerning(face: &Face, codes: &[(u16, GlyphId)], scale: f64) -> Vec<(u16, u16, i32)> {
    let kerned: Vec<(u16, GlyphId)> = codes.iter().copied().filter(|&(c, _)| u32::from(c) < KERN_BELOW).collect();
    let mut pairs: BTreeMap<(u16, u16), i32> = BTreeMap::new();
    let mut from_gpos = false;
    if let Some(gpos) = face.tables().gpos {
        let mut lookups: Vec<u16> = Vec::new();
        for i in 0..gpos.features.len() {
            if let Some(feature) = gpos.features.get(i) {
                if feature.tag == ttf_parser::Tag::from_bytes(b"kern") {
                    lookups.extend(feature.lookup_indices);
                }
            }
        }
        lookups.sort_unstable();
        lookups.dedup();
        for index in lookups {
            let Some(lookup) = gpos.lookups.get(index) else { continue };
            from_gpos = true;
            let subtables: Vec<ttf_parser::gpos::PairAdjustment> = lookup
                .subtables
                .into_iter::<ttf_parser::gpos::PositioningSubtable>()
                .filter_map(|s| match s {
                    ttf_parser::gpos::PositioningSubtable::Pair(p) => Some(p),
                    _ => None,
                })
                .collect();
            if subtables.is_empty() {
                continue;
            }
            for &(left, first) in &kerned {
                for &(right, second) in &kerned {
                    // The first subtable that holds the pair decides it within a lookup.
                    for sub in &subtables {
                        let value = match sub {
                            ttf_parser::gpos::PairAdjustment::Format1 { coverage, sets } => {
                                let Some(set) = coverage.get(first).and_then(|i| sets.get(i)) else { continue };
                                let Some((v, _)) = set.get(second) else { continue };
                                v.x_advance
                            }
                            ttf_parser::gpos::PairAdjustment::Format2 { coverage, classes, matrix } => {
                                if !coverage.contains(first) {
                                    continue;
                                }
                                matrix.get((classes.0.get(first), classes.1.get(second))).map_or(0, |(v, _)| v.x_advance)
                            }
                        };
                        *pairs.entry((left, right)).or_default() += i32::from(value);
                        break;
                    }
                }
            }
        }
    }
    if !from_gpos {
        if let Some(kern) = face.tables().kern {
            for sub in kern.subtables.into_iter().filter(|s| s.horizontal && !s.variable && !s.has_cross_stream && !s.has_state_machine) {
                for &(left, first) in &kerned {
                    for &(right, second) in &kerned {
                        if let Some(v) = sub.glyphs_kerning(first, second) {
                            *pairs.entry((left, right)).or_default() += i32::from(v);
                        }
                    }
                }
            }
        }
    }
    pairs
        .into_iter()
        .map(|((l, r), v)| (l, r, (f64::from(v) * scale).round() as i32))
        .filter(|&(_, _, x)| x != 0)
        .collect()
}

/// `<path>.kern`: int columns left, right (character codes) and x (source pixels added between them), by left then
/// right.
fn kern_table(pairs: &[(u16, u16, i32)]) -> Result<Vec<u8>> {
    let rows: Vec<Value> = pairs.iter().map(|&(l, r, x)| serde_json::json!({ "left": l, "right": r, "x": x })).collect();
    let json = serde_json::json!({ "columns": { "left": "int", "right": "int", "x": "int" }, "rows": rows });
    Ok(super::table::TableIr::from_json(json.to_string().as_bytes())?.to_ntbl())
}

#[cfg(test)]
#[path = "font_cook_tests.rs"]
pub(crate) mod tests;
