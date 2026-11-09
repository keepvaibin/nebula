//! Layouts (BRLYT) and their animations (BRLAN) to the store's cooked layouts: `NLYT` and `NLAN`
//! (native/petari/include/Game/Ui/UiData.hpp), flat, host order, read in place.
//!
//! The source is read from the disc's bytes at the positions the game's loader read (Layout::Build, the pane, picture,
//! text box, window, group and material constructors, AnimTransformBasic). Bit fields become plain fields, a pane's
//! place in the tree becomes indices, names become a string pool with a hash table per kind, group members become
//! pane indices, a material's stages become its four cooked forms. An animation keeps only its animation block, the
//! one the game binds (the tag and share blocks are not read).

use super::material::layout as material_layout;
use anyhow::{bail, Context, Result};
use std::collections::HashMap;

pub const LAYOUT_MAGIC: u32 = 0x5459_4C4E; // NLYT
pub const ANIMATION_MAGIC: u32 = 0x4E41_4C4E; // NLAN
pub const VERSION: u32 = 3;

pub const KIND_PANE: u8 = 0;
pub const KIND_PICTURE: u8 = 1;
pub const KIND_TEXT_BOX: u8 = 2;
pub const KIND_WINDOW: u8 = 3;
pub const KIND_BOUNDING: u8 = 4;

pub const NONE16: u16 = 0xFFFF;

const LAYOUT_HEADER: usize = 92;
const PANE: usize = 72;
// A material: fixed fields, five counted lists, four forms (8-aligned: they hold 64-bit hashes). A form's three lists
// are counted too.
const MATERIAL: usize = 296;
const FORM: usize = 48;
const FORMS: usize = 4;
const MAP: usize = 6;

/// Material flags (UiMaterialFlag_* in UiData.hpp).
const MATERIAL_CHANNEL: u8 = 1;
const MATERIAL_COLOR: u8 = 2;
const MATERIAL_ALPHA_COMPARE: u8 = 4;
const MATERIAL_BLEND: u8 = 8;
const MATERIAL_STAGES: u8 = 16;

fn be16(b: &[u8], at: usize) -> Result<u16> {
    Ok(u16::from_be_bytes(b.get(at..at + 2).context("a field past the file's end")?.try_into()?))
}

fn be32(b: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_be_bytes(b.get(at..at + 4).context("a field past the file's end")?.try_into()?))
}

fn bef32(b: &[u8], at: usize) -> Result<f32> {
    Ok(f32::from_bits(be32(b, at)?))
}

fn floats<const N: usize>(b: &[u8], at: usize) -> Result<[f32; N]> {
    let mut v = [0.0f32; N];
    for (i, x) in v.iter_mut().enumerate() {
        *x = bef32(b, at + 4 * i)?;
    }
    Ok(v)
}

fn bytes4(b: &[u8], at: usize) -> Result<[u8; 4]> {
    Ok(b.get(at..at + 4).context("a field past the file's end")?.try_into()?)
}

/// A fixed-size name field: the bytes before the first zero.
fn name_field(b: &[u8], at: usize, len: usize) -> Result<String> {
    let field = b.get(at..at + len).context("a name past the file's end")?;
    let end = field.iter().position(|&c| c == 0).unwrap_or(len);
    Ok(String::from_utf8_lossy(&field[..end]).into_owned())
}

/// A zero-terminated string at `at`.
fn c_string(b: &[u8], at: usize) -> Result<String> {
    let rest = b.get(at..).context("a string past the file's end")?;
    let end = rest.iter().position(|&c| c == 0).context("an unterminated string")?;
    Ok(String::from_utf8_lossy(&rest[..end]).into_owned())
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct UiMap {
    pub texture: u16,
    pub wrap_s: u8,
    pub wrap_t: u8,
    pub min_filter: u8,
    pub mag_filter: u8,
}

#[derive(Clone, Debug, PartialEq)]
pub struct UiMaterial {
    pub name: String,
    pub registers: [[i16; 4]; 3],
    pub constants: [[u8; 4]; 4],
    pub maps: Vec<UiMap>,
    pub srts: Vec<[f32; 5]>,
    pub generators: Vec<[u8; 3]>,
    pub channel: Option<[u8; 2]>,
    pub material_color: Option<[u8; 4]>,
    pub indirect_srts: Vec<[f32; 5]>,
    /// comp (comp0 | comp1 << 4), op, ref0, ref1, as the file holds them.
    pub alpha_compare: Option<[u8; 4]>,
    /// type, source factor, destination factor, op.
    pub blend: Option<[u8; 4]>,
    /// The four cooked forms (raster stage, remap).
    pub forms: Vec<material_layout::LayoutForm>,
    /// Each coordinate's map, 255 none.
    pub coord_map: Vec<u8>,
    pub has_stages: bool,
}

#[derive(Clone, Debug, PartialEq)]
pub struct UiPicture {
    pub vertex_colors: [[u8; 4]; 4],
    pub material: u16,
    /// Per coordinate set, its four corners' (s, t).
    pub coords: Vec<[[f32; 2]; 4]>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct UiTextBox {
    pub buffer_bytes: u16,
    pub text_bytes: u16,
    pub material: u16,
    pub font: u16,
    pub position: u8,
    pub alignment: u8,
    /// The text's UTF-16 units up to `text_bytes`, in host order; a message tag's (0x1A) length and group stay two
    /// bytes in file order (the tag reader reads them as bytes).
    pub text: Vec<u16>,
    pub colors: [[u8; 4]; 2],
    pub font_size: [f32; 2],
    pub char_space: f32,
    pub line_space: f32,
}

#[derive(Clone, Debug, PartialEq)]
pub struct UiWindow {
    pub inflation: [f32; 4],
    pub content: UiPicture,
    /// Each frame's material and texture flip.
    pub frames: Vec<(u16, u8)>,
}

#[derive(Clone, Debug, PartialEq)]
pub enum UiPayload {
    Pane,
    Picture(UiPicture),
    TextBox(UiTextBox),
    Window(UiWindow),
    Bounding,
}

#[derive(Clone, Debug, PartialEq)]
pub struct UiPane {
    pub parent: Option<usize>,
    pub visible: bool,
    pub influenced_alpha: bool,
    pub location_adjust: bool,
    pub base_position: u8,
    pub alpha: u8,
    pub name: String,
    pub user_data: [u8; 8],
    pub translate: [f32; 3],
    pub rotate: [f32; 3],
    pub scale: [f32; 2],
    pub size: [f32; 2],
    pub payload: UiPayload,
}

#[derive(Clone, Debug, PartialEq)]
pub struct UiGroup {
    pub name: String,
    /// Pane indices, in the file's order; a name with no pane is left out (Group::Group skips it).
    pub panes: Vec<usize>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct UiLayoutIr {
    pub centered: bool,
    /// The file has a root group (grp1), so the layout has a group container even with no groups in it.
    pub has_group_container: bool,
    pub size: [f32; 2],
    pub textures: Vec<String>,
    pub fonts: Vec<String>,
    pub materials: Vec<UiMaterial>,
    /// Tree order: a pane's children follow it, in the file's order.
    pub panes: Vec<UiPane>,
    pub groups: Vec<UiGroup>,
}

/// A string table block (txl1, fnl1): a count, then per entry a name offset (from the entries' start) and a type.
fn name_table(block: &[u8]) -> Result<Vec<String>> {
    let count = be16(block, 8)? as usize;
    let base = 12;
    (0..count).map(|i| c_string(block, base + be32(block, base + 8 * i)? as usize)).collect()
}

fn read_material(block: &[u8], at: usize) -> Result<UiMaterial> {
    let m = block.get(at..).context("a material past its block")?;
    let name = name_field(m, 0, 20)?;
    let mut registers = [[0i16; 4]; 3];
    for (r, reg) in registers.iter_mut().enumerate() {
        for (c, v) in reg.iter_mut().enumerate() {
            *v = be16(m, 20 + r * 8 + c * 2)? as i16;
        }
    }
    let mut constants = [[0u8; 4]; 4];
    for (k, c) in constants.iter_mut().enumerate() {
        *c = bytes4(m, 44 + k * 4)?;
    }
    let bits = be32(m, 60)?;
    let map_count = (bits & 15) as usize;
    let srt_count = ((bits >> 4) & 15) as usize;
    let generator_count = ((bits >> 8) & 15) as usize;
    let has_swaps = (bits >> 12) & 1 != 0;
    let indirect_srt_count = ((bits >> 13) & 3) as usize;
    let indirect_stage_count = ((bits >> 15) & 7) as usize;
    let stage_count = ((bits >> 18) & 31) as usize;
    let has_alpha = (bits >> 23) & 1 != 0;
    let has_blend = (bits >> 24) & 1 != 0;
    let has_channel = (bits >> 25) & 1 != 0;
    let has_color = (bits >> 27) & 1 != 0;

    let mut o = 64;
    let mut maps = Vec::new();
    for i in 0..map_count {
        let t = o + 4 * i;
        let wrap_s_filter = *m.get(t + 2).context("a texture map past its material")?;
        let wrap_t_filter = *m.get(t + 3).context("a texture map past its material")?;
        // res::TexMap: wrap in bits 0-1; min filter (bits 2-4) and mag filter (bit 2) stored minus one.
        maps.push(UiMap {
            texture: be16(m, t)?,
            wrap_s: wrap_s_filter & 3,
            wrap_t: wrap_t_filter & 3,
            min_filter: (((wrap_s_filter >> 2) & 7) + 1) & 7,
            mag_filter: (((wrap_t_filter >> 2) & 1) + 1) & 1,
        });
    }
    o += 4 * map_count;
    let srts = (0..srt_count).map(|i| floats::<5>(m, o + 20 * i)).collect::<Result<Vec<_>>>()?;
    o += 20 * srt_count;
    let generators = (0..generator_count)
        .map(|i| -> Result<[u8; 3]> { Ok(m.get(o + 4 * i..o + 4 * i + 3).context("a generator past its material")?.try_into()?) })
        .collect::<Result<Vec<_>>>()?;
    o += 4 * generator_count;
    let channel = if has_channel {
        let c = bytes4(m, o)?;
        o += 4;
        Some([c[0], c[1]])
    } else {
        None
    };
    let material_color = if has_color {
        let c = bytes4(m, o)?;
        o += 4;
        Some(c)
    } else {
        None
    };
    if has_swaps {
        o += 4;
    }
    let indirect_srts = (0..indirect_srt_count).map(|i| floats::<5>(m, o + 20 * i)).collect::<Result<Vec<_>>>()?;
    o += 20 * indirect_srt_count + 4 * indirect_stage_count + 16 * stage_count;
    let alpha_compare = if has_alpha {
        let a = bytes4(m, o)?;
        o += 4;
        Some(a)
    } else {
        None
    };
    let blend = if has_blend { Some(bytes4(m, o)?) } else { None };

    let source = &m[..material_layout::source_size(m)?];
    let forms = material_layout::programs(source)?
        .iter()
        .map(material_layout::form)
        .collect::<Result<Vec<_>>>()?;
    let (coord_map, has_stages) = material_layout::coord_map(source)?;
    Ok(UiMaterial {
        name,
        registers,
        constants,
        maps,
        srts,
        generators,
        channel,
        material_color,
        indirect_srts,
        alpha_compare,
        blend,
        forms,
        coord_map,
        has_stages,
    })
}

/// Vertex colours (four RGBA words), a material index and coordinate sets, as res::Picture and res::WindowContent
/// hold them from `at`; the sets follow the record.
fn read_picture(b: &[u8], at: usize) -> Result<(UiPicture, usize)> {
    let mut vertex_colors = [[0u8; 4]; 4];
    for (i, c) in vertex_colors.iter_mut().enumerate() {
        *c = bytes4(b, at + 4 * i)?;
    }
    let material = be16(b, at + 16)?;
    let count = *b.get(at + 18).context("a coordinate count past its block")? as usize;
    let mut o = at + 20;
    let mut coords = Vec::with_capacity(count);
    for _ in 0..count {
        let f = floats::<8>(b, o)?;
        coords.push([[f[0], f[1]], [f[2], f[3]], [f[4], f[5]], [f[6], f[7]]]);
        o += 32;
    }
    Ok((UiPicture { vertex_colors, material, coords }, o))
}

/// UTF-16 text as the game's tag reader reads it: each unit in host order but a message tag's (0x1A) length and group
/// bytes, which stay bytes in file order.
fn read_text(b: &[u8], at: usize, end: usize) -> Result<Vec<u16>> {
    let mut out = b.get(at..end).context("text past its block")?.to_vec();
    let mut host16 = |p: usize| -> Result<u16> {
        let v = be16(b, p)?;
        out[p - at..p - at + 2].copy_from_slice(&v.to_le_bytes());
        Ok(v)
    };
    let mut p = at;
    while p + 2 <= end {
        let unit = host16(p)?;
        p += 2;
        if unit != 0x1A || p + 2 > end {
            continue;
        }
        let tag_end = (p - 2 + *b.get(p).context("a text tag past its block")? as usize).min(end);
        let mut q = p + 2;
        while q + 2 <= tag_end {
            host16(q)?;
            q += 2;
        }
        if tag_end < p {
            bail!("text: a tag at {:#x} shorter than its own header", p - 2);
        }
        p = tag_end;
    }
    Ok(out.chunks_exact(2).map(|c| u16::from_le_bytes([c[0], c[1]])).collect())
}

fn read_pane(block: &[u8], kind: &[u8; 4], parent: Option<usize>) -> Result<UiPane> {
    let flag = *block.get(8).context("a short pane")?;
    let mut pane = UiPane {
        parent,
        visible: flag & 1 != 0,
        influenced_alpha: flag & 2 != 0,
        location_adjust: flag & 4 != 0,
        base_position: block[9],
        alpha: block[10],
        name: name_field(block, 12, 16)?,
        user_data: block.get(28..36).context("a short pane")?.try_into()?,
        translate: floats::<3>(block, 0x24)?,
        rotate: floats::<3>(block, 0x30)?,
        scale: floats::<2>(block, 0x3C)?,
        size: floats::<2>(block, 0x44)?,
        payload: UiPayload::Pane,
    };
    const BODY: usize = 0x4C;
    pane.payload = match kind {
        b"pan1" => UiPayload::Pane,
        b"bnd1" => UiPayload::Bounding,
        b"pic1" => UiPayload::Picture(read_picture(block, BODY)?.0),
        b"txt1" => {
            let text_bytes = be16(block, BODY + 2)?;
            let text_at = be32(block, BODY + 12)? as usize;
            let text = read_text(block, text_at, text_at + text_bytes as usize)?;
            UiPayload::TextBox(UiTextBox {
                buffer_bytes: be16(block, BODY)?,
                text_bytes,
                material: be16(block, BODY + 4)?,
                font: be16(block, BODY + 6)?,
                position: block[BODY + 8],
                alignment: block[BODY + 9],
                text,
                colors: [bytes4(block, BODY + 16)?, bytes4(block, BODY + 20)?],
                font_size: floats::<2>(block, BODY + 24)?,
                char_space: bef32(block, BODY + 32)?,
                line_space: bef32(block, BODY + 36)?,
            })
        }
        b"wnd1" => {
            let inflation = floats::<4>(block, BODY)?;
            let frame_count = *block.get(BODY + 16).context("a short window")? as usize;
            let content_at = be32(block, BODY + 20)? as usize;
            let table_at = be32(block, BODY + 24)? as usize;
            let content = read_picture(block, content_at)?.0;
            let frames = (0..frame_count)
                .map(|i| -> Result<(u16, u8)> {
                    let f = be32(block, table_at + 4 * i)? as usize;
                    Ok((be16(block, f)?, *block.get(f + 2).context("a short window frame")?))
                })
                .collect::<Result<Vec<_>>>()?;
            UiPayload::Window(UiWindow { inflation, content, frames })
        }
        _ => bail!("pane block {}", String::from_utf8_lossy(kind)),
    };
    Ok(pane)
}

/// The first pane named `name` in tree order (Pane::FindPaneByName from the root, recursive).
fn find_pane(panes: &[UiPane], name: &str) -> Option<usize> {
    panes.iter().position(|p| p.name == name)
}

/// A source layout as the game's loader reads it.
pub fn read_layout(bytes: &[u8]) -> Result<UiLayoutIr> {
    if bytes.len() < 16 || !bytes.starts_with(b"RLYT") || bytes[4..6] != [0xFE, 0xFF] {
        bail!("not a source layout");
    }
    let header = be16(bytes, 0xC)? as usize;
    let blocks = be16(bytes, 0xE)? as usize;
    let mut ir = UiLayoutIr::default();
    let mut parent: Option<usize> = None;
    let mut last: Option<usize> = None;
    let mut root_group_read = false;
    let mut group_level = 0;
    // Layout::Build: panes appended to the parent the last pas1 opened; the first pane is the root.
    let mut at = header;
    for _ in 0..blocks {
        let kind: [u8; 4] = bytes.get(at..at + 4).context("a block past the file's end")?.try_into()?;
        let size = be32(bytes, at + 4)? as usize;
        let block = bytes.get(at..at + size).context("a block past the file's end")?;
        match &kind {
            b"lyt1" => {
                ir.centered = block[8] != 0;
                ir.size = floats::<2>(block, 12)?;
            }
            b"txl1" => ir.textures = name_table(block)?,
            b"fnl1" => ir.fonts = name_table(block)?,
            b"mat1" => {
                let count = be16(block, 8)? as usize;
                for i in 0..count {
                    ir.materials.push(read_material(block, be32(block, 12 + 4 * i)? as usize)?);
                }
            }
            b"pan1" | b"pic1" | b"txt1" | b"wnd1" | b"bnd1" => {
                if !ir.panes.is_empty() && parent.is_none() {
                    bail!("a second root pane");
                }
                ir.panes.push(read_pane(block, &kind, parent)?);
                last = Some(ir.panes.len() - 1);
            }
            b"pas1" => parent = last,
            b"pae1" => {
                last = parent;
                parent = parent.and_then(|p| ir.panes[p].parent);
            }
            b"grp1" => {
                if !root_group_read {
                    root_group_read = true;
                    ir.has_group_container = true;
                } else if group_level == 1 {
                    let name = name_field(block, 8, 16)?;
                    let count = be16(block, 24)? as usize;
                    let panes = (0..count)
                        .map(|i| name_field(block, 28 + 16 * i, 16))
                        .collect::<Result<Vec<_>>>()?
                        .iter()
                        .filter_map(|n| find_pane(&ir.panes, n))
                        .collect();
                    ir.groups.push(UiGroup { name, panes });
                }
            }
            b"grs1" => group_level += 1,
            b"gre1" => group_level -= 1,
            _ => {}
        }
        at += size;
    }
    // The tree order above holds only while every child block follows its parent's pas1: check it.
    for (i, p) in ir.panes.iter().enumerate() {
        if p.parent.is_some_and(|q| q >= i) {
            bail!("pane {} before its parent", p.name);
        }
    }
    Ok(ir)
}

/// Text in a pool, deduplicated; offsets from the pool's start.
#[derive(Default)]
struct Pool {
    bytes: Vec<u8>,
    at: HashMap<String, u32>,
}

impl Pool {
    fn add(&mut self, s: &str) -> u32 {
        if let Some(&at) = self.at.get(s) {
            return at;
        }
        let at = self.bytes.len() as u32;
        self.bytes.extend_from_slice(s.as_bytes());
        self.bytes.push(0);
        self.at.insert(s.to_owned(), at);
        at
    }
}

/// FNV-1a over a name's bytes: the game's lookup hashes the same way (Ui::nameHash).
pub fn name_hash(name: &str) -> u32 {
    let mut h = 0x811C_9DC5u32;
    for &b in name.as_bytes() {
        h = (h ^ u32::from(b)).wrapping_mul(0x0100_0193);
    }
    h
}

/// An open-addressed table over `names`: a power of two of slots, at least twice the count, each the first index
/// with that name plus one (0 empty); probing is linear. The same name later in the list is reached through the
/// record's next-same-name link, so the table holds a name once.
fn hash_table(names: &[&str]) -> Vec<u32> {
    let slots = (names.len() * 2).next_power_of_two().max(4);
    let mut table = vec![0u32; slots];
    let mut seen: HashMap<&str, ()> = HashMap::new();
    for (i, &n) in names.iter().enumerate() {
        if seen.insert(n, ()).is_some() {
            continue;
        }
        let mut s = name_hash(n) as usize & (slots - 1);
        while table[s] != 0 {
            s = (s + 1) & (slots - 1);
        }
        table[s] = i as u32 + 1;
    }
    table
}

/// For each name, the next index with the same name (NONE16 at the last).
fn same_name_links(names: &[&str]) -> Vec<u16> {
    let mut next = vec![NONE16; names.len()];
    let mut last: HashMap<&str, usize> = HashMap::new();
    for (i, &n) in names.iter().enumerate() {
        if let Some(&p) = last.get(n) {
            next[p] = i as u16;
        }
        last.insert(n, i);
    }
    next
}

fn put16(out: &mut [u8], at: usize, v: u16) {
    out[at..at + 2].copy_from_slice(&v.to_le_bytes());
}

fn put32(out: &mut [u8], at: usize, v: u32) {
    out[at..at + 4].copy_from_slice(&v.to_le_bytes());
}

fn putf(out: &mut [u8], at: usize, v: &[f32]) {
    for (i, x) in v.iter().enumerate() {
        put32(out, at + 4 * i, x.to_bits());
    }
}

fn align(out: &mut Vec<u8>, n: usize) {
    while out.len() % n != 0 {
        out.push(0);
    }
}

fn append(out: &mut Vec<u8>, bytes: &[u8]) -> u32 {
    align(out, 4);
    let at = out.len() as u32;
    out.extend_from_slice(bytes);
    at
}

fn words16(v: &[u16]) -> Vec<u8> {
    v.iter().flat_map(|x| x.to_le_bytes()).collect()
}

fn words32(v: &[u32]) -> Vec<u8> {
    v.iter().flat_map(|x| x.to_le_bytes()).collect()
}

/// A counted list in a record: its length and, past 0 entries, the offset of its table.
fn put_list(out: &mut [u8], at: usize, count: usize, offset: u32) {
    put32(out, at, count as u32);
    put32(out, at + 4, if count == 0 { 0 } else { offset });
}

/// A table's bytes appended, its offset; 0 for none.
fn table(out: &mut Vec<u8>, bytes: &[u8]) -> u32 {
    if bytes.is_empty() {
        0
    } else {
        append(out, bytes)
    }
}

/// A form's record (UiFormData): its hashes, its three lists (the tables appended to `out`), the rows that are the
/// vertex program's, flags.
fn form_record(out: &mut Vec<u8>, f: &material_layout::LayoutForm) -> [u8; FORM] {
    let mut r = [0u8; FORM];
    r[0..8].copy_from_slice(&f.vertex.to_le_bytes());
    r[8..16].copy_from_slice(&f.pixel.to_le_bytes());
    put_list(&mut r, 16, f.rows.len(), table(out, &words32(&f.rows)));
    put_list(&mut r, 24, f.textures.len(), table(out, &f.textures));
    put_list(&mut r, 32, f.inputs.len(), table(out, &f.inputs));
    put32(&mut r, 40, f.vertex_rows as u32);
    r[44] = f.flags;
    r
}

fn picture_record(p: &UiPicture, out: &mut Vec<u8>) -> u32 {
    let mut r = vec![0u8; 24];
    for (i, c) in p.vertex_colors.iter().enumerate() {
        r[4 * i..4 * i + 4].copy_from_slice(c);
    }
    put16(&mut r, 16, p.material);
    r[18] = p.coords.len() as u8;
    let coords: Vec<f32> = p.coords.iter().flat_map(|q| q.iter().flat_map(|c| c.iter().copied())).collect();
    let coords_at = if coords.is_empty() { 0 } else { append(out, &words32(&coords.iter().map(|f| f.to_bits()).collect::<Vec<_>>())) };
    put32(&mut r, 20, coords_at);
    append(out, &r)
}

/// The cooked layout.
///
/// Header (LAYOUT_HEADER bytes): magic, version, flags (bit 0 centred, bit 1 a group container), width, height; then (count, offset) for panes,
/// materials, textures, fonts, groups; the string pool's offset and size; the pane, material and group hash tables'
/// (slots, offset).
/// Material (MATERIAL bytes, in one run): name, next same name, flags, reserved, registers[3][4] (s16), constants[4][4], channel[2],
/// material colour[4], alpha compare[4], blend[4], reserved, then the lists (count, offset): maps (6 bytes each), SRTs
/// (5 floats), generators (3 bytes), indirect SRTs, coordinate map (a byte per coordinate); four forms (FORM bytes: shader
/// pair hashes, lists of rows (u32, the model's row codes), textures (u8) and inputs (u8), the count of rows that are the vertex program's, flags).
/// Pane (PANE bytes): parent, first child, next sibling, subtree end (one past its last descendant), next same name;
/// kind, base position, alpha, visible, influenced alpha, location adjust; name, user data[8], translate[3],
/// rotate[3], scale[2], size[2], payload offset.
pub fn write_layout(ir: &UiLayoutIr) -> Result<Vec<u8>> {
    let n = ir.panes.len();
    if n >= NONE16 as usize || ir.materials.len() >= NONE16 as usize {
        bail!("{n} panes, {} materials: past a 16-bit index", ir.materials.len());
    }
    let mut pool = Pool::default();
    let mut out = vec![0u8; LAYOUT_HEADER];

    // Tree links: children in file order.
    let mut first_child = vec![NONE16; n];
    let mut next_sibling = vec![NONE16; n];
    let mut last_child: Vec<Option<usize>> = vec![None; n];
    for (i, p) in ir.panes.iter().enumerate() {
        if let Some(q) = p.parent {
            match last_child[q] {
                Some(prev) => next_sibling[prev] = i as u16,
                None => first_child[q] = i as u16,
            }
            last_child[q] = Some(i);
        }
    }
    let mut subtree_end = vec![0u16; n];
    for i in (0..n).rev() {
        let mut end = i + 1;
        let mut c = first_child[i];
        while c != NONE16 {
            end = end.max(subtree_end[c as usize] as usize);
            c = next_sibling[c as usize];
        }
        subtree_end[i] = end as u16;
    }
    let pane_names: Vec<&str> = ir.panes.iter().map(|p| p.name.as_str()).collect();
    let pane_next = same_name_links(&pane_names);

    let mut payloads = Vec::with_capacity(n);
    for p in &ir.panes {
        let at = match &p.payload {
            UiPayload::Pane | UiPayload::Bounding => 0,
            UiPayload::Picture(pic) => picture_record(pic, &mut out),
            UiPayload::TextBox(t) => {
                let text_at = append(&mut out, &words16(&t.text));
                let mut r = vec![0u8; 44];
                put16(&mut r, 0, t.buffer_bytes);
                put16(&mut r, 2, t.text_bytes);
                put16(&mut r, 4, t.material);
                put16(&mut r, 6, t.font);
                r[8] = t.position;
                r[9] = t.alignment;
                put32(&mut r, 12, text_at);
                r[16..20].copy_from_slice(&t.colors[0]);
                r[20..24].copy_from_slice(&t.colors[1]);
                putf(&mut r, 24, &t.font_size);
                putf(&mut r, 32, &[t.char_space, t.line_space]);
                put16(&mut r, 40, t.text.len() as u16);
                append(&mut out, &r)
            }
            UiPayload::Window(w) => {
                let content = picture_record(&w.content, &mut out);
                let frames: Vec<u8> = w.frames.iter().flat_map(|&(m, flip)| {
                    let mut f = m.to_le_bytes().to_vec();
                    f.extend_from_slice(&[flip, 0]);
                    f
                }).collect();
                let frames_at = if frames.is_empty() { 0 } else { append(&mut out, &frames) };
                let mut r = vec![0u8; 28];
                putf(&mut r, 0, &w.inflation);
                r[16] = w.frames.len() as u8;
                put32(&mut r, 20, content);
                put32(&mut r, 24, frames_at);
                append(&mut out, &r)
            }
        };
        payloads.push(at);
    }

    align(&mut out, 16);
    let panes_at = out.len();
    out.resize(panes_at + PANE * n, 0);
    for (i, p) in ir.panes.iter().enumerate() {
        let r = panes_at + PANE * i;
        put16(&mut out, r, p.parent.map_or(NONE16, |q| q as u16));
        put16(&mut out, r + 2, first_child[i]);
        put16(&mut out, r + 4, next_sibling[i]);
        put16(&mut out, r + 6, subtree_end[i]);
        put16(&mut out, r + 8, pane_next[i]);
        out[r + 10] = match p.payload {
            UiPayload::Pane => KIND_PANE,
            UiPayload::Picture(_) => KIND_PICTURE,
            UiPayload::TextBox(_) => KIND_TEXT_BOX,
            UiPayload::Window(_) => KIND_WINDOW,
            UiPayload::Bounding => KIND_BOUNDING,
        };
        out[r + 11] = p.base_position;
        out[r + 12] = p.alpha;
        out[r + 13] = u8::from(p.visible);
        out[r + 14] = u8::from(p.influenced_alpha);
        out[r + 15] = u8::from(p.location_adjust);
        put32(&mut out, r + 16, pool.add(&p.name));
        out[r + 20..r + 28].copy_from_slice(&p.user_data);
        putf(&mut out, r + 28, &p.translate);
        putf(&mut out, r + 40, &p.rotate);
        putf(&mut out, r + 52, &p.scale);
        putf(&mut out, r + 60, &p.size);
        put32(&mut out, r + 68, payloads[i]);
    }

    let material_names: Vec<&str> = ir.materials.iter().map(|m| m.name.as_str()).collect();
    let material_next = same_name_links(&material_names);
    // Each material's tables first (maps, SRTs, generators, indirect SRTs, coordinate map, and per form its rows,
    // textures and inputs), then the records in one run.
    let mut records = Vec::with_capacity(ir.materials.len());
    for (i, m) in ir.materials.iter().enumerate() {
        if m.forms.len() != FORMS {
            bail!("material {}: {} forms, not {FORMS}", m.name, m.forms.len());
        }
        let mut r = [0u8; MATERIAL];
        put32(&mut r, 0, pool.add(&m.name));
        put16(&mut r, 4, material_next[i]);
        r[6] = u8::from(m.channel.is_some()) * MATERIAL_CHANNEL
            | u8::from(m.material_color.is_some()) * MATERIAL_COLOR
            | u8::from(m.alpha_compare.is_some()) * MATERIAL_ALPHA_COMPARE
            | u8::from(m.blend.is_some()) * MATERIAL_BLEND
            | u8::from(m.has_stages) * MATERIAL_STAGES;
        for (a, reg) in m.registers.iter().enumerate() {
            for (c, v) in reg.iter().enumerate() {
                put16(&mut r, 8 + a * 8 + c * 2, *v as u16);
            }
        }
        for (k, c) in m.constants.iter().enumerate() {
            r[32 + 4 * k..36 + 4 * k].copy_from_slice(c);
        }
        if let Some(c) = m.channel {
            r[48..50].copy_from_slice(&c);
        }
        if let Some(c) = m.material_color {
            r[50..54].copy_from_slice(&c);
        }
        if let Some(a) = m.alpha_compare {
            r[54..58].copy_from_slice(&a);
        }
        if let Some(b) = m.blend {
            r[58..62].copy_from_slice(&b);
        }
        let maps: Vec<u8> = m
            .maps
            .iter()
            .flat_map(|map| {
                let mut b = map.texture.to_le_bytes().to_vec();
                b.extend_from_slice(&[map.wrap_s, map.wrap_t, map.min_filter, map.mag_filter]);
                b
            })
            .collect();
        debug_assert_eq!(maps.len(), MAP * m.maps.len());
        let srts: Vec<u8> = words32(&m.srts.iter().flat_map(|s| s.iter().map(|f| f.to_bits())).collect::<Vec<_>>());
        let generators: Vec<u8> = m.generators.iter().flat_map(|g| g.iter().copied()).collect();
        let indirect: Vec<u8> = words32(&m.indirect_srts.iter().flat_map(|s| s.iter().map(|f| f.to_bits())).collect::<Vec<_>>());
        put_list(&mut r, 64, m.maps.len(), table(&mut out, &maps));
        put_list(&mut r, 72, m.srts.len(), table(&mut out, &srts));
        put_list(&mut r, 80, m.generators.len(), table(&mut out, &generators));
        put_list(&mut r, 88, m.indirect_srts.len(), table(&mut out, &indirect));
        put_list(&mut r, 96, m.coord_map.len(), table(&mut out, &m.coord_map));
        for (k, f) in m.forms.iter().enumerate() {
            let form = form_record(&mut out, f);
            r[104 + FORM * k..104 + FORM * (k + 1)].copy_from_slice(&form);
        }
        records.push(r);
    }
    align(&mut out, 16);
    let materials_at = out.len();
    for r in &records {
        out.extend_from_slice(r);
    }

    let textures: Vec<u32> = ir.textures.iter().map(|t| pool.add(t)).collect();
    let fonts: Vec<u32> = ir.fonts.iter().map(|f| pool.add(f)).collect();
    let textures_at = append(&mut out, &words32(&textures));
    let fonts_at = append(&mut out, &words32(&fonts));

    let group_names: Vec<&str> = ir.groups.iter().map(|g| g.name.as_str()).collect();
    let mut group_records = Vec::new();
    for g in &ir.groups {
        let members = append(&mut out, &words16(&g.panes.iter().map(|&p| p as u16).collect::<Vec<_>>()));
        group_records.push((pool.add(&g.name), g.panes.len() as u32, members));
    }
    let groups_at = append(&mut out, &group_records.iter().flat_map(|&(a, b, c)| words32(&[a, b, c])).collect::<Vec<_>>());

    let pane_table = hash_table(&pane_names);
    let material_table = hash_table(&material_names);
    let group_table = hash_table(&group_names);
    let pane_table_at = append(&mut out, &words32(&pane_table));
    let material_table_at = append(&mut out, &words32(&material_table));
    let group_table_at = append(&mut out, &words32(&group_table));
    let pool_at = append(&mut out, &pool.bytes);
    align(&mut out, 4);

    let header: [u32; 23] = [
        LAYOUT_MAGIC,
        VERSION,
        u32::from(ir.centered) | u32::from(ir.has_group_container) << 1,
        ir.size[0].to_bits(),
        ir.size[1].to_bits(),
        n as u32,
        panes_at as u32,
        ir.materials.len() as u32,
        materials_at as u32,
        ir.textures.len() as u32,
        textures_at,
        ir.fonts.len() as u32,
        fonts_at,
        ir.groups.len() as u32,
        groups_at,
        pool_at,
        pool.bytes.len() as u32,
        pane_table.len() as u32,
        pane_table_at,
        material_table.len() as u32,
        material_table_at,
        group_table.len() as u32,
        group_table_at,
    ];
    for (k, w) in header.iter().enumerate() {
        put32(&mut out, 4 * k, *w);
    }
    Ok(out)
}

#[derive(Clone, Debug, PartialEq)]
pub enum UiKeys {
    /// frame, value, slope.
    Hermite(Vec<[f32; 3]>),
    /// frame, value.
    Step(Vec<(f32, u16)>),
}

#[derive(Clone, Debug, PartialEq)]
pub struct UiTarget {
    pub id: u8,
    pub target: u8,
    pub keys: UiKeys,
}

#[derive(Clone, Debug, PartialEq)]
pub struct UiAnimInfo {
    /// 'RLPA', 'RLVI', 'RLVC', 'RLMC', 'RLTS', 'RLTP', 'RLIM' as the file's four characters.
    pub kind: [u8; 4],
    pub targets: Vec<UiTarget>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct UiAnimContent {
    pub name: String,
    pub material: bool,
    pub infos: Vec<UiAnimInfo>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct UiAnimationIr {
    pub frame_size: u16,
    pub looped: bool,
    /// The texture-pattern files, by name.
    pub files: Vec<String>,
    pub contents: Vec<UiAnimContent>,
}

/// Step-key kinds (visibility and texture pattern); the rest are hermite (lyt_animation.cpp).
fn is_step(kind: &[u8; 4]) -> bool {
    kind == b"RLVI" || kind == b"RLTP"
}

/// A source layout animation's animation block (pai1), as AnimTransformBasic reads it.
pub fn read_animation(bytes: &[u8]) -> Result<UiAnimationIr> {
    if bytes.len() < 16 || !bytes.starts_with(b"RLAN") || bytes[4..6] != [0xFE, 0xFF] {
        bail!("not a source layout animation");
    }
    let header = be16(bytes, 0xC)? as usize;
    let blocks = be16(bytes, 0xE)? as usize;
    let mut at = header;
    for _ in 0..blocks {
        let size = be32(bytes, at + 4)? as usize;
        let block = bytes.get(at..at + size).context("a block past the file's end")?;
        if block.starts_with(b"pai1") {
            let frame_size = be16(block, 8)?;
            let looped = block[10] != 0;
            let file_count = be16(block, 12)? as usize;
            let content_count = be16(block, 14)? as usize;
            let contents_at = be32(block, 16)? as usize;
            // The file names: offsets from the table that follows the block header (detail::GetStrTableStr).
            let table = 20;
            let files = (0..file_count)
                .map(|i| c_string(block, table + be32(block, table + 4 * i)? as usize))
                .collect::<Result<Vec<_>>>()?;
            let mut contents = Vec::with_capacity(content_count);
            for c in 0..content_count {
                let ca = be32(block, contents_at + 4 * c)? as usize;
                let name = name_field(block, ca, 20)?;
                let info_count = *block.get(ca + 20).context("a short animation content")? as usize;
                let material = block[ca + 21] != 0;
                let mut infos = Vec::with_capacity(info_count);
                for k in 0..info_count {
                    let ia = ca + be32(block, ca + 24 + 4 * k)? as usize;
                    let kind: [u8; 4] = bytes4(block, ia)?;
                    let target_count = *block.get(ia + 4).context("a short animation info")? as usize;
                    let mut targets = Vec::with_capacity(target_count);
                    for t in 0..target_count {
                        let ta = ia + be32(block, ia + 8 + 4 * t)? as usize;
                        let key_count = be16(block, ta + 4)? as usize;
                        let keys_at = ta + be32(block, ta + 8)? as usize;
                        let keys = if is_step(&kind) {
                            UiKeys::Step(
                                (0..key_count)
                                    .map(|i| -> Result<(f32, u16)> { Ok((bef32(block, keys_at + 8 * i)?, be16(block, keys_at + 8 * i + 4)?)) })
                                    .collect::<Result<Vec<_>>>()?,
                            )
                        } else {
                            UiKeys::Hermite((0..key_count).map(|i| floats::<3>(block, keys_at + 12 * i)).collect::<Result<Vec<_>>>()?)
                        };
                        if key_count == 0 {
                            bail!("{name}: a curve with no keys");
                        }
                        targets.push(UiTarget { id: block[ta], target: block[ta + 1], keys });
                    }
                    infos.push(UiAnimInfo { kind, targets });
                }
                contents.push(UiAnimContent { name, material, infos });
            }
            return Ok(UiAnimationIr { frame_size, looped, files, contents });
        }
        at += size;
    }
    Ok(UiAnimationIr::default())
}

/// The cooked layout animation.
///
/// Header (40 bytes): magic, version, frame size, loop, file count and offset (string offsets), content count and
/// offset, pool offset and size. Content (16 bytes): name, material (0 pane, 1 material), info count, info offset.
/// Info (12 bytes): kind (the four characters as one word, 'RLPA' as C++ spells it), target count, target offset.
/// Target (12 bytes): id, target, step (0 hermite, 1 step), pad, key count, key offset. Hermite keys are
/// (frame, value, slope) floats; step keys (frame float, value u16, pad u16).
pub fn write_animation(ir: &UiAnimationIr) -> Result<Vec<u8>> {
    let mut pool = Pool::default();
    let mut out = vec![0u8; 40];
    let mut content_records = Vec::new();
    for c in &ir.contents {
        let mut info_records = Vec::new();
        for info in &c.infos {
            let mut target_records = Vec::new();
            for t in &info.targets {
                let (step, count, keys) = match &t.keys {
                    UiKeys::Hermite(k) => {
                        let flat: Vec<u32> = k.iter().flat_map(|x| x.iter().map(|f| f.to_bits())).collect();
                        (0u8, k.len(), append(&mut out, &words32(&flat)))
                    }
                    UiKeys::Step(k) => {
                        let flat: Vec<u8> = k
                            .iter()
                            .flat_map(|&(f, v)| {
                                let mut b = f.to_bits().to_le_bytes().to_vec();
                                b.extend_from_slice(&v.to_le_bytes());
                                b.extend_from_slice(&[0, 0]);
                                b
                            })
                            .collect();
                        (1u8, k.len(), append(&mut out, &flat))
                    }
                };
                let mut r = vec![t.id, t.target, step, 0];
                r.extend_from_slice(&(count as u32).to_le_bytes());
                r.extend_from_slice(&keys.to_le_bytes());
                target_records.extend_from_slice(&r);
            }
            let targets_at = append(&mut out, &target_records);
            info_records.extend_from_slice(&words32(&[u32::from_be_bytes(info.kind), info.targets.len() as u32, targets_at]));
        }
        let infos_at = append(&mut out, &info_records);
        content_records.extend_from_slice(&words32(&[pool.add(&c.name), u32::from(c.material), c.infos.len() as u32, infos_at]));
    }
    let contents_at = append(&mut out, &content_records);
    let files: Vec<u32> = ir.files.iter().map(|f| pool.add(f)).collect();
    let files_at = append(&mut out, &words32(&files));
    let pool_at = append(&mut out, &pool.bytes);
    align(&mut out, 4);
    let header: [u32; 10] = [
        ANIMATION_MAGIC,
        VERSION,
        u32::from(ir.frame_size),
        u32::from(ir.looped),
        ir.files.len() as u32,
        files_at,
        ir.contents.len() as u32,
        contents_at,
        pool_at,
        pool.bytes.len() as u32,
    ];
    for (k, w) in header.iter().enumerate() {
        put32(&mut out, 4 * k, *w);
    }
    Ok(out)
}

#[cfg(test)]
#[path = "ui_layout_tests.rs"]
mod disc_tests;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hash_table_finds_every_first_name() {
        let names = ["RootPane", "PicA", "TxtB", "PicA", "N_Win"];
        let table = hash_table(&names);
        assert!(table.len().is_power_of_two() && table.len() >= 2 * names.len());
        for (i, n) in names.iter().enumerate() {
            let first = names.iter().position(|m| m == n).unwrap();
            let mut s = name_hash(n) as usize & (table.len() - 1);
            while table[s] != 0 && names[table[s] as usize - 1] != *n {
                s = (s + 1) & (table.len() - 1);
            }
            assert_eq!(table[s] as usize - 1, first, "{i} {n}");
        }
        assert_eq!(same_name_links(&names), vec![NONE16, 3, NONE16, NONE16, NONE16]);
    }

    #[test]
    fn name_hash_is_fnv1a() {
        assert_eq!(name_hash(""), 0x811C_9DC5);
        assert_eq!(name_hash("a"), 0xE40C_292C);
    }
}
