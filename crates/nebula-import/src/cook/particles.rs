//! Particle files (JPC) to the store's cooked particles (native/petari/include/Game/Particle/ParticleData.hpp, NPTC).
//!
//! The source is read in host order after its textures became records (formats::jpc, then native_data::jpc_host), at
//! the positions the game's loader read: a colour table's builder reads one key past the last (the bytes after the
//! table), and reading the same bytes keeps every table as the game built it. Packed fields are split, the game's
//! blend, compare and factor codes become the host's, colour tables and the rates the loader derived are built here.

use super::{formats, native_data, Stats};
use anyhow::{bail, Context, Result};
use std::collections::HashMap;

pub const MAGIC: u32 = 0x4354_504E; // NPTC
pub const VERSION: u32 = 1;

const HEADER: usize = 32;
const RESOURCE: usize = 48;
const DYNAMICS: usize = 0x74;
const SHAPE: usize = 0x6C;
const SHAPE_TEXTURE_KEYS: usize = 81; // ParticleShapeData::mTexAnmKeyNum
const SHAPE_LAST_FRAME: usize = 86; // mColorAnmFrameMax
const EXTRA: usize = 0x78;
const CHILD: usize = 0x48;
const EXTEX: usize = 0x20;
const FIELD: usize = 0x4C;
const TEXTURE_INFO: usize = 64;
const TEXTURE_INFO_MAGIC: u32 = 0x4954_424E; // NBTI, nebula_cooked_texture.h

// nebula_game_api.h
const BLEND_REPLACE: u8 = 0;
const BLEND_ADD: u8 = 1;
const FACTOR_ZERO: u8 = 0;
const FACTOR_ONE: u8 = 1;
const FACTOR_SRC_COLOR: u8 = 2;
const FACTOR_INV_SRC_COLOR: u8 = 3;
const FACTOR_SRC_ALPHA: u8 = 4;
const FACTOR_INV_SRC_ALPHA: u8 = 5;
const FACTOR_DST_ALPHA: u8 = 6;
const FACTOR_INV_DST_ALPHA: u8 = 7;
const FACTOR_DST_COLOR: u8 = 8;
const FACTOR_INV_DST_COLOR: u8 = 9;
const COMPARE_NEVER: u8 = 0;
const COMPARE_LESS: u8 = 1;
const COMPARE_EQUAL: u8 = 2;
const COMPARE_LEQUAL: u8 = 3;
const COMPARE_GREATER: u8 = 4;
const COMPARE_NEQUAL: u8 = 5;
const COMPARE_GEQUAL: u8 = 6;
const COMPARE_ALWAYS: u8 = 7;

/// The particle file's blend operations by code.
const BLEND_OPS: [u8; 4] = [BLEND_REPLACE, BLEND_ADD, BLEND_REPLACE, BLEND_REPLACE];
/// Its comparisons by code: never, less, less or equal, equal, not equal, greater or equal, greater, always.
const COMPARES: [u8; 8] = [
    COMPARE_NEVER,
    COMPARE_LESS,
    COMPARE_LEQUAL,
    COMPARE_EQUAL,
    COMPARE_NEQUAL,
    COMPARE_GEQUAL,
    COMPARE_GREATER,
    COMPARE_ALWAYS,
];

/// A blend factor by code; a colour factor names the other side's colour.
fn blend_factor(code: u32, source: bool) -> u8 {
    match code {
        0 => FACTOR_ZERO,
        1 => FACTOR_ONE,
        2 | 4 => if source { FACTOR_DST_COLOR } else { FACTOR_SRC_COLOR },
        3 | 5 => if source { FACTOR_INV_DST_COLOR } else { FACTOR_INV_SRC_COLOR },
        6 => FACTOR_SRC_ALPHA,
        7 => FACTOR_INV_SRC_ALPHA,
        8 => FACTOR_DST_ALPHA,
        9 => FACTOR_INV_DST_ALPHA,
        _ => FACTOR_ZERO,
    }
}

/// A texture: its name and its native metadata (NebulaTextureInfo, image_at unset) with its record.
#[derive(Clone)]
pub struct Texture {
    pub name: Vec<u8>,
    pub info: Vec<u8>,
    pub record: Vec<u8>,
}

/// A shape: its record (table offsets unset) and its tables.
#[derive(Clone)]
pub struct Shape {
    pub record: Vec<u8>,
    pub prm_table: Option<Vec<u8>>,
    pub env_table: Option<Vec<u8>>,
    pub texture_table: Option<Vec<u8>>,
}

/// One resource, its records as cooked.
#[derive(Clone)]
pub struct Resource {
    pub user_index: u32,
    pub dynamics: Vec<u8>,
    pub shape: Shape,
    pub extra: Option<Vec<u8>>,
    pub child: Option<Vec<u8>>,
    pub extex: Option<Vec<u8>>,
    pub fields: Vec<Vec<u8>>,
    pub keys: Vec<Vec<u8>>,
    pub texture_indices: Vec<u32>,
}

#[derive(Clone, Default)]
pub struct ParticleIr {
    pub resources: Vec<Resource>,
    pub textures: Vec<Texture>,
}

/// The host-order source, little-endian, by absolute position.
struct Src<'a>(&'a [u8]);

impl Src<'_> {
    fn bytes(&self, at: usize, n: usize) -> Result<&[u8]> {
        self.0.get(at..at + n).with_context(|| format!("a particle field at {at:#x} past the file's end"))
    }
    fn u8(&self, at: usize) -> Result<u8> {
        Ok(self.bytes(at, 1)?[0])
    }
    fn i8(&self, at: usize) -> Result<i8> {
        Ok(self.u8(at)? as i8)
    }
    fn u16(&self, at: usize) -> Result<u16> {
        Ok(u16::from_le_bytes(self.bytes(at, 2)?.try_into().unwrap()))
    }
    fn i16(&self, at: usize) -> Result<i16> {
        Ok(self.u16(at)? as i16)
    }
    fn u32(&self, at: usize) -> Result<u32> {
        Ok(u32::from_le_bytes(self.bytes(at, 4)?.try_into().unwrap()))
    }
    fn f32(&self, at: usize) -> Result<f32> {
        Ok(f32::from_bits(self.u32(at)?))
    }
}

/// A record being written: little-endian, fields in order.
#[derive(Default)]
struct Rec(Vec<u8>);

impl Rec {
    fn u8(&mut self, v: u8) -> &mut Self {
        self.0.push(v);
        self
    }
    fn bool(&mut self, v: bool) -> &mut Self {
        self.u8(v as u8)
    }
    fn u16(&mut self, v: u16) -> &mut Self {
        self.0.extend_from_slice(&v.to_le_bytes());
        self
    }
    fn i16(&mut self, v: i16) -> &mut Self {
        self.u16(v as u16)
    }
    fn u32(&mut self, v: u32) -> &mut Self {
        self.0.extend_from_slice(&v.to_le_bytes());
        self
    }
    fn f32(&mut self, v: f32) -> &mut Self {
        self.u32(v.to_bits())
    }
    fn raw(&mut self, v: &[u8]) -> &mut Self {
        self.0.extend_from_slice(v);
        self
    }
    // `n` bytes of zero.
    fn zeros(&mut self, n: usize) -> &mut Self {
        self.0.resize(self.0.len() + n, 0);
        self
    }
}

fn floats(s: &Src, r: &mut Rec, at: usize, count: usize) -> Result<()> {
    for k in 0..count {
        r.f32(s.f32(at + 4 * k)?);
    }
    Ok(())
}

/// BEM1 (after its eight-byte header): EmitterDynamicsData.
fn dynamics(s: &Src, at: usize) -> Result<Vec<u8>> {
    let flags = s.u32(at + 8)?;
    let mut r = Rec::default();
    r.u32(flags & 0x1F).u32((flags >> 8) & 7);
    floats(s, &mut r, at + 0x10, 9 + 13)?; // scale, translation, direction; thirteen floats
    for o in [0x68, 0x6A, 0x6C, 0x6E, 0x70, 0x72] {
        r.i16(s.i16(at + o)?); // rotation, last frame, first frame, life
    }
    r.u16(s.u16(at + 0x74)?).u16(s.u16(at + 0x76)?).u8(s.u8(at + 0x78)?).zeros(3);
    debug_assert_eq!(r.0.len(), DYNAMICS);
    Ok(r.0)
}

/// A colour table: one colour per frame up to the last colour frame, statement for statement as the game's loader built
/// it (ParticleShape's makeColorTable before the cook): the key after the last is read as the bytes that follow.
fn color_table(s: &Src, keys_at: usize, key_count: u8, last_frame: i16) -> Result<Vec<u8>> {
    let index = |j: usize| s.i16(keys_at + 6 * j);
    let color = |j: usize| -> Result<[u8; 4]> { Ok(s.bytes(keys_at + 6 * j + 2, 4)?.try_into().unwrap()) };
    let entries = (last_frame as i32 + 1).max(0) as usize;
    let mut table = vec![[0u8; 4]; entries];
    let (mut r_step, mut g_step, mut b_step, mut a_step) = (0.0f32, 0.0f32, 0.0f32, 0.0f32);
    let first = color(0)?;
    let (mut r, mut g, mut b, mut a) = (first[0] as f32, first[1] as f32, first[2] as f32, first[3] as f32);
    let mut j = 0usize;
    for i in 0..entries {
        if i as i32 == index(j)? as i32 {
            let c = color(j)?;
            table[i] = c;
            r = c[0] as f32;
            g = c[1] as f32;
            b = c[2] as f32;
            a = c[3] as f32;
            j += 1;
            if j < key_count as usize {
                let next = color(j)?;
                let base_step = 1.0f32 / (index(j)? as i32 - index(j - 1)? as i32) as f32;
                r_step = base_step * (next[0] as f32 - r);
                g_step = base_step * (next[1] as f32 - g);
                b_step = base_step * (next[2] as f32 - b);
                a_step = base_step * (next[3] as f32 - a);
            } else {
                r_step = 0.0;
                g_step = 0.0;
                b_step = 0.0;
                a_step = 0.0;
            }
        } else {
            r += r_step;
            table[i][0] = r as i32 as u8;
            g += g_step;
            table[i][1] = g as i32 as u8;
            b += b_step;
            table[i][2] = b as i32 as u8;
            a += a_step;
            table[i][3] = a as i32 as u8;
        }
    }
    Ok(table.concat())
}

/// BSP1: ParticleShapeData and its tables.
fn shape(s: &Src, at: usize) -> Result<Shape> {
    let flags = s.u32(at + 8)?;
    let blend = s.u16(at + 0x18)? as u32;
    let alpha = s.u8(at + 0x1A)? as usize;
    let z = s.u8(at + 0x1D)? as usize;
    let tex = s.u8(at + 0x1E)?;
    let color_flags = s.u8(at + 0x21)?;
    let last_frame = s.i16(at + 0x24)?;
    let tex_crd = flags & 0x0100_0000 != 0;
    let tex_anm = tex & 1 != 0;
    let prm_anm = color_flags & 0x02 != 0;
    let env_anm = color_flags & 0x08 != 0;
    let tex_keys = s.u8(at + 0x1F)?;
    let mut r = Rec::default();
    r.f32(s.f32(at + 0x10)?).f32(s.f32(at + 0x14)?);
    if tex_crd {
        floats(s, &mut r, at + 0x34, 10)?;
    } else {
        r.zeros(40);
    }
    r.u8((flags & 0xF) as u8)
        .u8(((flags >> 4) & 7) as u8)
        .u8(((flags >> 7) & 7) as u8)
        .u8(((flags >> 10) & 1) as u8)
        .u8(((flags >> 25) & 1) as u8)
        .u8(((flags >> 26) & 1) as u8)
        .u8(((flags >> 15) & 7) as u8)
        .u8(((flags >> 18) & 1) as u8)
        .u8(BLEND_OPS[(blend & 3) as usize])
        .u8(blend_factor((blend >> 2) & 0xF, true))
        .u8(blend_factor((blend >> 6) & 0xF, false))
        .bool(z & 1 != 0)
        .u8(COMPARES[(z >> 1) & 7])
        .bool((z >> 4) & 1 != 0)
        .u8(COMPARES[alpha & 7])
        .u8(COMPARES[(alpha >> 5) & 7])
        .u8(((alpha >> 3) & 3) as u8)
        .u8(s.u8(at + 0x1B)?)
        .u8(s.u8(at + 0x1C)?)
        .bool(flags & 0x1000 != 0)
        .bool(flags & 0x4000 != 0)
        .bool(flags & 0x0010_0000 != 0)
        .bool(flags & 0x0020_0000 != 0)
        .bool(flags & 0x0040_0000 != 0)
        .bool(tex_crd)
        .bool((flags >> 27) & 1 != 0)
        .bool((flags >> 28) & 1 != 0)
        .bool(prm_anm)
        .bool(env_anm)
        .u8((color_flags >> 4) & 7)
        .bool(tex_anm)
        .u8((tex >> 2) & 7)
        .u8(s.u8(at + 0x20)?)
        .u8(tex_keys)
        .u8(s.u8(at + 0x2F)?)
        .u8(s.u8(at + 0x30)?)
        .u8(s.u8(at + 0x2E)?)
        .zeros(1)
        .i16(last_frame)
        .raw(s.bytes(at + 0x26, 8)?) // primary and environment colours
        .zeros(12); // the table offsets
    debug_assert_eq!(r.0.len(), SHAPE);
    let table = |on: bool, offset_at: usize, keys_at: usize| -> Result<Option<Vec<u8>>> {
        if !on {
            return Ok(None);
        }
        let offset = s.i16(at + offset_at)? as isize;
        let keys = (at as isize + offset) as usize;
        Ok(Some(color_table(s, keys, s.u8(at + keys_at)?, last_frame)?))
    };
    let texture_table = if tex_anm {
        let from = at + 0x34 + if tex_crd { 0x28 } else { 0 };
        Some(s.bytes(from, tex_keys as usize)?.to_vec())
    } else {
        None
    };
    Ok(Shape { record: r.0, prm_table: table(prm_anm, 0xC, 0x22)?, env_table: table(env_anm, 0xE, 0x23)?, texture_table })
}

/// ESP1: ExtraShapeData, with the rates ExtraShape::init derived.
fn extra(s: &Src, at: usize) -> Result<Vec<u8>> {
    let flags = s.u32(at + 8)?;
    let f = |o: usize| s.f32(at + o);
    let (scale_in, scale_out) = (f(0xC)?, f(0x10)?);
    let (in_x, out_x, in_y, out_y) = (f(0x14)?, f(0x18)?, f(0x1C)?, f(0x20)?);
    let (alpha_in, alpha_out, alpha_in_value, alpha_base, alpha_out_value) = (f(0x2C)?, f(0x30)?, f(0x34)?, f(0x38)?, f(0x3C)?);
    let mut r = Rec::default();
    r.bool(flags & 1 != 0)
        .bool(flags & 2 != 0)
        .u8(((flags >> 8) & 3) as u8)
        .u8(((flags >> 10) & 3) as u8)
        .u8(((flags >> 12) & 3) as u8)
        .u8(((flags >> 14) & 3) as u8)
        .bool(flags & 0x10000 != 0)
        .bool(flags & 0x20000 != 0)
        .bool(flags & 0x0100_0000 != 0)
        .zeros(1)
        .i16(s.i16(at + 0x28)?)
        .i16(s.i16(at + 0x2A)?)
        .zeros(2);
    floats(s, &mut r, at + 0xC, 7)?;
    floats(s, &mut r, at + 0x2C, 13)?;
    let alpha_inc = if alpha_in != 0.0 { (alpha_base - alpha_in_value) / alpha_in } else { 1.0 };
    let alpha_dec = if alpha_out != 1.0 { (alpha_out_value - alpha_base) / (1.0 - alpha_out) } else { 1.0 };
    let (inc_x, inc_y) = if scale_in != 0.0 { ((1.0 - in_x) / scale_in, (1.0 - in_y) / scale_in) } else { (1.0, 1.0) };
    let (dec_x, dec_y) =
        if scale_out != 1.0 { ((out_x - 1.0) / (1.0 - scale_out), (out_y - 1.0) / (1.0 - scale_out)) } else { (1.0, 1.0) };
    r.f32(alpha_inc).f32(alpha_dec).f32(inc_x).f32(inc_y).f32(dec_x).f32(dec_y);
    debug_assert_eq!(r.0.len(), EXTRA);
    Ok(r.0)
}

/// SSP1: ChildShapeData.
fn child(s: &Src, at: usize) -> Result<Vec<u8>> {
    let flags = s.u32(at + 8)?;
    let mut r = Rec::default();
    r.u8((flags & 0xF) as u8)
        .u8(((flags >> 4) & 7) as u8)
        .u8(((flags >> 7) & 7) as u8)
        .u8(((flags >> 10) & 1) as u8);
    for bit in [0x10000, 0x20000, 0x40000, 0x20_0000, 0x40_0000, 0x80_0000, 0x100_0000] {
        r.bool(flags & bit != 0);
    }
    r.zeros(1);
    floats(s, &mut r, at + 0xC, 10)?;
    r.raw(s.bytes(at + 0x34, 8)?);
    r.f32(s.f32(at + 0x3C)?).i16(s.i16(at + 0x40)?).i16(s.i16(at + 0x42)?).u8(s.u8(at + 0x44)?).u8(s.u8(at + 0x45)?);
    r.i16(s.i16(at + 0x46)?);
    debug_assert_eq!(r.0.len(), CHILD);
    Ok(r.0)
}

/// ETX1: ExTexShapeData.
fn extex(s: &Src, at: usize) -> Result<Vec<u8>> {
    let flags = s.u32(at + 8)?;
    let mut r = Rec::default();
    floats(s, &mut r, at + 0xC, 6)?;
    r.u8(s.i8(at + 0x24)? as u8)
        .u8(s.i8(at + 0x25)? as u8)
        .u8(s.i8(at + 0x26)? as u8)
        .bool(flags & 1 != 0)
        .bool(flags & 0x100 != 0)
        .zeros(3);
    debug_assert_eq!(r.0.len(), EXTEX);
    Ok(r.0)
}

/// FLD1: ParticleFieldData, with the fade rates ParticleField::init derived.
fn field(s: &Src, at: usize) -> Result<Vec<u8>> {
    let flags = s.u32(at + 8)?;
    let f = |o: usize| s.f32(at + o);
    let (fade_in, fade_out, enable, disable) = (f(0x30)?, f(0x34)?, f(0x38)?, f(0x3C)?);
    let mut r = Rec::default();
    r.u32(flags & 0xF).u32((flags >> 8) & 3).u32(flags >> 16);
    floats(s, &mut r, at + 0xC, 13)?;
    r.u32(s.u8(at + 0x40)? as u32);
    let in_rate = fade_in - enable;
    let in_rate = if in_rate == 0.0 { 1.0 } else { 1.0 / in_rate };
    let out_rate = disable - fade_out;
    let out_rate = if out_rate == 0.0 { 1.0 } else { 1.0 / out_rate };
    r.f32(in_rate).f32(out_rate);
    debug_assert_eq!(r.0.len(), FIELD);
    Ok(r.0)
}

/// KFA1: ParticleKeyData.
fn key(s: &Src, at: usize) -> Result<Vec<u8>> {
    let count = s.u8(at + 9)? as usize;
    let mut r = Rec::default();
    r.u32(s.u8(at + 8)? as u32).u32(count as u32).u32((s.u8(at + 0xB)? != 0) as u32).u32(0);
    floats(s, &mut r, at + 0xC, 4 * count)?;
    Ok(r.0)
}

/// A texture block (after its kind and size: four bytes, a 20-byte name, a ResTIMG and its record).
fn texture(s: &Src, at: usize) -> Result<Texture> {
    let name = s.bytes(at + 0xC, 0x14)?;
    let name = name[..name.iter().position(|&b| b == 0).unwrap_or(name.len())].to_vec();
    let h = at + 0x20;
    let image = s.u32(h + 0x1C)? as usize;
    let record_at = h + if image != 0 { image } else { 0x20 };
    let size = s.u32(record_at + 12)? as usize;
    let record = s.bytes(record_at, size)?.to_vec();
    let mut info = Rec::default();
    info.u32(TEXTURE_INFO_MAGIC).u32(1).u32(s.u16(h + 2)? as u32).u32(s.u16(h + 4)? as u32);
    info.u32(s.u8(h + 0x18)? as u32).u32(0);
    info.f32(s.i8(h + 0x16)? as f32 * 0.125).f32(s.i8(h + 0x17)? as f32 * 0.125).f32(s.i16(h + 0x1A)? as f32 * 0.01);
    info.u8(s.u8(h)?).u8(s.u8(h + 1)?).u8(s.u8(h + 6)?).u8(s.u8(h + 7)?);
    info.u8(s.u8(h + 0x10)?).u8(1u8 << s.u8(h + 0x13)?.min(2)).u8(s.u8(h + 0x14)?).u8(s.u8(h + 0x15)?);
    info.zeros(20);
    debug_assert_eq!(info.0.len(), TEXTURE_INFO);
    Ok(Texture { name, info: info.0, record })
}

/// A disc particle file (version 2-10).
pub fn read_jpc(bytes: &[u8], stats: &mut Stats) -> Result<ParticleIr> {
    let textured = formats::jpc(bytes, stats)?;
    let host = native_data::jpc_host(&textured)?;
    let s = Src(&host);
    let resources = s.u16(8)? as usize;
    let textures = s.u16(0xA)? as usize;
    let mut ir = ParticleIr::default();
    let mut at = 0x10;
    for _ in 0..resources {
        let user_index = s.u16(at)? as u32;
        let blocks = s.u16(at + 2)? as usize;
        at += 8;
        let (mut dyn_block, mut shape_block) = (None, None);
        let mut res = Resource {
            user_index,
            dynamics: Vec::new(),
            shape: Shape { record: Vec::new(), prm_table: None, env_table: None, texture_table: None },
            extra: None,
            child: None,
            extex: None,
            fields: Vec::new(),
            keys: Vec::new(),
            texture_indices: Vec::new(),
        };
        for _ in 0..blocks {
            let magic: [u8; 4] = s.bytes(at, 4)?.try_into().unwrap();
            let size = s.u32(at + 4)? as usize;
            match &magic {
                b"BEM1" => dyn_block = Some(dynamics(&s, at)?),
                b"BSP1" => shape_block = Some(shape(&s, at)?),
                b"ESP1" => res.extra = Some(extra(&s, at)?),
                b"SSP1" => res.child = Some(child(&s, at)?),
                b"ETX1" => res.extex = Some(extex(&s, at)?),
                b"FLD1" => res.fields.push(field(&s, at)?),
                b"KFA1" => res.keys.push(key(&s, at)?),
                b"TDB1" => {
                    res.texture_indices = (at + 8..at + (size & !1)).step_by(2).map(|p| s.u16(p).map(u32::from)).collect::<Result<_>>()?;
                }
                _ => bail!("unknown particle block {}", String::from_utf8_lossy(&magic)),
            }
            at += size;
        }
        res.dynamics = dyn_block.with_context(|| format!("particle {user_index} has no dynamics"))?;
        res.shape = shape_block.with_context(|| format!("particle {user_index} has no shape"))?;
        ir.resources.push(res);
    }
    let mut at = s.u32(0xC)? as usize;
    for _ in 0..textures {
        let size = s.u32(at + 4)? as usize;
        ir.textures.push(texture(&s, at)?);
        at += size;
    }
    Ok(ir)
}

fn align(out: &mut Vec<u8>, to: usize) {
    out.resize(out.len().div_ceil(to) * to, 0);
}

fn put32(out: &mut [u8], at: usize, value: u32) {
    out[at..at + 4].copy_from_slice(&value.to_le_bytes());
}

/// `bytes` at the end, 4-aligned; its offset.
fn append(out: &mut Vec<u8>, bytes: &[u8]) -> u32 {
    align(out, 4);
    let at = out.len() as u32;
    out.extend_from_slice(bytes);
    at
}

/// The cooked file.
pub fn write(ir: &ParticleIr) -> Vec<u8> {
    let mut out = vec![0u8; HEADER + RESOURCE * ir.resources.len()];
    for (i, res) in ir.resources.iter().enumerate() {
        let r = HEADER + RESOURCE * i;
        let dynamics = append(&mut out, &res.dynamics);
        let shape = append(&mut out, &res.shape.record);
        let tables = [&res.shape.prm_table, &res.shape.env_table, &res.shape.texture_table];
        for (k, table) in tables.iter().enumerate() {
            if let Some(t) = table {
                let at = append(&mut out, t);
                put32(&mut out, shape as usize + SHAPE - 12 + 4 * k, at);
            }
        }
        let optional = |out: &mut Vec<u8>, block: &Option<Vec<u8>>| block.as_ref().map_or(0, |b| append(out, b));
        let extra = optional(&mut out, &res.extra);
        let child = optional(&mut out, &res.child);
        let extex = optional(&mut out, &res.extex);
        let fields = if res.fields.is_empty() { 0 } else { append(&mut out, &res.fields.concat()) };
        let key_offsets: Vec<u32> = res.keys.iter().map(|k| append(&mut out, k)).collect();
        let keys = if key_offsets.is_empty() {
            0
        } else {
            append(&mut out, &key_offsets.iter().flat_map(|k| k.to_le_bytes()).collect::<Vec<_>>())
        };
        let indices = if res.texture_indices.is_empty() {
            0
        } else {
            append(&mut out, &res.texture_indices.iter().flat_map(|k| k.to_le_bytes()).collect::<Vec<_>>())
        };
        let words = [
            res.user_index,
            res.fields.len() as u32,
            res.keys.len() as u32,
            res.texture_indices.len() as u32,
            dynamics,
            shape,
            extra,
            child,
            extex,
            fields,
            keys,
            indices,
        ];
        for (k, w) in words.iter().enumerate() {
            put32(&mut out, r + 4 * k, *w);
        }
    }
    let names: Vec<u32> = ir
        .textures
        .iter()
        .map(|t| {
            let mut n = t.name.clone();
            n.push(0);
            append(&mut out, &n)
        })
        .collect();
    align(&mut out, 4);
    let entries = out.len();
    out.resize(entries + 8 * ir.textures.len(), 0);
    align(&mut out, 32);
    let infos = out.len();
    for t in &ir.textures {
        out.extend_from_slice(&t.info);
    }
    // Two textures with the same image share its record.
    let mut records: HashMap<&[u8], usize> = HashMap::new();
    for (i, t) in ir.textures.iter().enumerate() {
        let record = match records.get(t.record.as_slice()) {
            Some(&at) => at,
            None => {
                align(&mut out, 32);
                let at = out.len();
                out.extend_from_slice(&t.record);
                records.insert(t.record.as_slice(), at);
                at
            }
        };
        let info = infos + TEXTURE_INFO * i;
        put32(&mut out, info + 20, (record - info) as u32);
        put32(&mut out, entries + 8 * i, names[i]);
        put32(&mut out, entries + 8 * i + 4, info as u32);
    }
    let header = [MAGIC, VERSION, ir.resources.len() as u32, ir.textures.len() as u32, HEADER as u32, entries as u32, 0, 0];
    for (k, w) in header.iter().enumerate() {
        put32(&mut out, 4 * k, *w);
    }
    out
}

/// A cooked file, back to its records (a mod's particles are merged on these).
pub fn read_nptc(bytes: &[u8]) -> Result<ParticleIr> {
    let s = Src(bytes);
    if s.u32(0)? != MAGIC {
        bail!("not a cooked particle file");
    }
    if s.u32(4)? != VERSION {
        bail!("a cooked particle file of version {}, not {VERSION}", s.u32(4)?);
    }
    let block = |at: u32, size: usize| -> Result<Option<Vec<u8>>> {
        if at == 0 { Ok(None) } else { Ok(Some(s.bytes(at as usize, size)?.to_vec())) }
    };
    let mut ir = ParticleIr::default();
    let resources = s.u32(16)? as usize;
    for i in 0..s.u32(8)? as usize {
        let r = resources + RESOURCE * i;
        let w = |k: usize| s.u32(r + 4 * k);
        let (fields, keys, indices) = (w(1)? as usize, w(2)? as usize, w(3)? as usize);
        let shape_at = w(5)? as usize;
        let mut shape_record = s.bytes(shape_at, SHAPE)?.to_vec();
        let last_frame = s.i16(shape_at + SHAPE_LAST_FRAME)?;
        let table_entries = (last_frame as i32 + 1).max(0) as usize * 4;
        let tex_keys = s.u8(shape_at + SHAPE_TEXTURE_KEYS)? as usize;
        let table = |k: usize, size: usize| -> Result<Option<Vec<u8>>> { block(s.u32(shape_at + SHAPE - 12 + 4 * k)?, size) };
        let shape = Shape {
            prm_table: table(0, table_entries)?,
            env_table: table(1, table_entries)?,
            texture_table: table(2, tex_keys)?,
            record: {
                shape_record[SHAPE - 12..].fill(0);
                shape_record
            },
        };
        let mut key_list = Vec::with_capacity(keys);
        for k in 0..keys {
            let at = s.u32(w(10)? as usize + 4 * k)? as usize;
            let count = s.u32(at + 4)? as usize;
            key_list.push(s.bytes(at, 16 + 16 * count)?.to_vec());
        }
        let (fields_at, indices_at) = (w(9)? as usize, w(11)? as usize);
        let mut field_list = Vec::with_capacity(fields);
        for k in 0..fields {
            field_list.push(s.bytes(fields_at + FIELD * k, FIELD)?.to_vec());
        }
        let mut texture_indices = Vec::with_capacity(indices);
        for k in 0..indices {
            texture_indices.push(s.u32(indices_at + 4 * k)?);
        }
        ir.resources.push(Resource {
            user_index: w(0)?,
            dynamics: s.bytes(w(4)? as usize, DYNAMICS)?.to_vec(),
            shape,
            extra: block(w(6)?, EXTRA)?,
            child: block(w(7)?, CHILD)?,
            extex: block(w(8)?, EXTEX)?,
            fields: field_list,
            keys: key_list,
            texture_indices,
        });
    }
    let entries = s.u32(20)? as usize;
    for i in 0..s.u32(12)? as usize {
        let name_at = s.u32(entries + 8 * i)? as usize;
        let info_at = s.u32(entries + 8 * i + 4)? as usize;
        let name = bytes.get(name_at..).context("a particle texture name past the file's end")?;
        let name = name[..name.iter().position(|&b| b == 0).context("a particle texture name without its end")?].to_vec();
        let mut info = s.bytes(info_at, TEXTURE_INFO)?.to_vec();
        let record_at = info_at + s.u32(info_at + 20)? as usize;
        let size = s.u32(record_at + 12)? as usize;
        info[20..24].fill(0);
        ir.textures.push(Texture { name, info, record: s.bytes(record_at, size)?.to_vec() });
    }
    Ok(ir)
}

/// `base` with `extra`'s particles after its own: their user indices from `first_index`, their textures after its own.
pub fn merge(base: &ParticleIr, extra: &ParticleIr, first_index: u32) -> Result<ParticleIr> {
    let mut out = base.clone();
    let textures = base.textures.len() as u32;
    for res in &extra.resources {
        let mut res = res.clone();
        res.user_index = first_index.checked_add(res.user_index).context("too many particles")?;
        for t in &mut res.texture_indices {
            *t = t.checked_add(textures).context("too many particle textures")?;
        }
        out.resources.push(res);
    }
    out.textures.extend(extra.textures.iter().cloned());
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn resource(user_index: u32, texture: u32) -> Resource {
        Resource {
            user_index,
            dynamics: vec![1; DYNAMICS],
            shape: Shape { record: vec![0; SHAPE], prm_table: None, env_table: None, texture_table: None },
            extra: None,
            child: None,
            extex: None,
            fields: Vec::new(),
            keys: Vec::new(),
            texture_indices: vec![texture],
        }
    }

    fn texture(name: &str, fill: u8) -> Texture {
        let mut record = vec![fill; 80];
        record[12..16].copy_from_slice(&80u32.to_le_bytes());
        Texture { name: name.as_bytes().to_vec(), info: vec![0; TEXTURE_INFO], record }
    }

    #[test]
    fn a_cooked_file_reads_back_as_written() {
        let mut ir = ParticleIr { resources: vec![resource(3, 0), resource(7, 1)], textures: vec![texture("a", 0xA), texture("b", 0xA)] };
        ir.resources[1].shape.record[SHAPE_LAST_FRAME..SHAPE_LAST_FRAME + 2].copy_from_slice(&1i16.to_le_bytes());
        ir.resources[1].shape.prm_table = Some(vec![9; 8]);
        ir.resources[1].keys.push([1u32, 1, 0, 0].iter().flat_map(|w| w.to_le_bytes()).chain([0u8; 16]).collect());
        let bytes = write(&ir);
        let back = read_nptc(&bytes).unwrap();
        assert_eq!(write(&back), bytes);
        assert_eq!(back.resources[1].user_index, 7);
        assert_eq!(back.textures[1].name, b"b");
    }

    #[test]
    fn mod_particles_go_after_the_files_own_with_their_textures() {
        let base = ParticleIr { resources: vec![resource(0, 0), resource(1, 1)], textures: vec![texture("a", 1), texture("b", 2)] };
        let extra = ParticleIr { resources: vec![resource(0, 0), resource(1, 0)], textures: vec![texture("c", 3)] };
        let out = read_nptc(&write(&merge(&base, &extra, 100).unwrap())).unwrap();
        let seen: Vec<(u32, u32)> = out.resources.iter().map(|r| (r.user_index, r.texture_indices[0])).collect();
        assert_eq!(seen, [(0, 0), (1, 1), (100, 2), (101, 2)]);
        assert_eq!(out.textures[2].name, b"c");
    }

    #[test]
    fn a_colour_table_reads_the_key_after_the_last() {
        // Keys at frames 0 and 2; the bytes after them name frame 3, so the builder takes their colour there.
        let mut file = Vec::new();
        for (frame, color) in [(0i16, [0u8, 0, 0, 0]), (2, [20, 40, 60, 80]), (3, [1, 2, 3, 4]), (9, [0, 0, 0, 0])] {
            file.extend_from_slice(&frame.to_le_bytes());
            file.extend_from_slice(&color);
        }
        let table = color_table(&Src(&file), 0, 2, 4).unwrap();
        assert_eq!(&table[4..8], &[10, 20, 30, 40]);
        assert_eq!(&table[12..16], &[1, 2, 3, 4]);
        assert_eq!(&table[16..20], &[1, 2, 3, 4]);
    }
}
