//! Cook-time material identity. Legacy program bytes stay inside the importer.
//!
//! This is the compatibility key used to prove the MAT3 cooker against captured
//! game materials before replacing their runtime construction.

use anyhow::{bail, Context, Result};
use std::collections::BTreeMap;

pub const PROGRAM_BYTES: usize = 756;
#[path = "material_layout.rs"]
pub mod layout;

/// Put native shader/state descriptors in the converted material block itself,
/// so model and standalone material-table factories receive the same data.
pub fn cook_model(bytes: &[u8]) -> Result<Vec<u8>> {
    let cooked = super::native_data::j3d_model(bytes)?;
    if bytes.starts_with(b"2D3J") {
        return Ok(cooked);
    }
    if bytes.len() < 32 {
        bail!("material model header is incomplete");
    }
    let blocks = u32::from_be_bytes(bytes[12..16].try_into()?) as usize;
    let mut at = 32usize;
    let mut result = cooked[..32].to_vec();
    for _ in 0..blocks {
        let header = bytes
            .get(at..at.checked_add(8).context("material block overflow")?)
            .context("material block header missing")?;
        let size = u32::from_be_bytes(header[4..8].try_into()?) as usize;
        if size < 8 {
            bail!("material block too short");
        }
        let end = at
            .checked_add(size)
            .context("material block length overflow")?;
        // Older exporters can overstate the final padding; the existing model
        // conversion accepts that case, so use only bytes actually available.
        let block = bytes
            .get(at..end.min(bytes.len()))
            .context("material block outside model")?;
        let mut native_block = cooked[at..end.min(cooked.len())].to_vec();
        if &header[..4] == b"MAT2" {
            bail!("an old format material block: the game reads MAT3 only");
        }
        if &header[..4] == b"MAT3" {
            let mut descriptors = Vec::new();
            let mut bounded = block.to_vec();
            let actual = u32::try_from(bounded.len())?;
            bounded[4..8].copy_from_slice(&actual.to_be_bytes());
            let table = Mat3::parse(&bounded)?;
            for i in 0..table.len() {
                let p = table.compatibility_program(i, 0x11100000, false)?;
                let shader = cooked_shader(&p)?;
                // Record: generated shader ID, material index, native pipeline
                // state, then reserved space. No source combiner fields survive.
                descriptors.extend_from_slice(&shader.to_le_bytes());
                descriptors.extend_from_slice(&(i as u32).to_le_bytes());
                descriptors.extend_from_slice(&p[716..724]);
                descriptors.extend_from_slice(&[p[4], p[6], 0, 0]);
                descriptors.extend_from_slice(&coordinate_maps(&p));
                let (used_maps, used_coords) = used_masks(&p);
                descriptors.extend_from_slice(&[used_maps, used_coords, 0, 0]);
                for pass in 0..64 {
                    let id = pass_shader(&pass_program(&p, pass), pass);
                    descriptors.extend_from_slice(&id.to_le_bytes());
                }
            }
            while native_block.len() % 16 != 0 {
                native_block.push(0);
            }
            native_block.extend_from_slice(&descriptors);
            native_block.extend_from_slice(b"NMAT");
            native_block.extend_from_slice(&3u32.to_le_bytes());
            native_block.extend_from_slice(&u32::try_from(table.len())?.to_le_bytes());
            native_block.extend_from_slice(&288u32.to_le_bytes());
            let size = u32::try_from(native_block.len())?;
            native_block[4..8].copy_from_slice(&size.to_le_bytes());
        }
        result.extend_from_slice(&native_block);
        at = end;
    }
    if at < cooked.len() {
        result.extend_from_slice(&cooked[at..]);
    }
    let size = u32::try_from(result.len())?;
    result[8..12].copy_from_slice(&size.to_le_bytes());
    super::mesh::native(result)
}

/// Which texture each coordinate samples, for its scale: the last stage order or
/// indirect order naming it wins. 255: no order names it.
fn coordinate_maps(p: &[u8]) -> [u8; 8] {
    let mut maps = [255u8; 8];
    let stages = usize::from(p[0]);
    let pairs = if stages % 2 == 1 && stages < 16 && stages != 1 { stages + 1 } else { stages };
    for s in 0..pairs {
        let at = 16 + s * 34;
        maps[usize::from(p[at] & 7)] = p[at + 1] & 7;
    }
    let loads = u16::from_le_bytes([p[8], p[9]]);
    if loads & (1 << 10) != 0 {
        for i in 0..4 {
            let at = 576 + i * 4;
            maps[usize::from(p[at] & 7)] = p[at + 1] & 7;
        }
    }
    maps
}

/// The maps and coordinates the material's active stages and indirect stages use.
fn used_masks(p: &[u8]) -> (u8, u8) {
    let (mut maps, mut coords) = (0u8, 0u8);
    for s in 0..usize::from(p[0]) {
        let at = 16 + s * 34;
        if p[at] < 8 {
            coords |= 1 << p[at];
        }
        if p[at + 1] < 8 {
            maps |= 1 << p[at + 1];
        }
    }
    for i in 0..usize::from(p[3]).min(4) {
        let at = 576 + i * 4;
        if p[at] < 8 {
            coords |= 1 << p[at];
        }
    }
    (maps, coords)
}

/// Original MAT3 tables. Offsets are relative to this block, never host pointers.
/// This view is used only while cooking the source model, before native_data
/// changes byte order. Source tables may overlap: their offsets carry no lengths.
pub struct Mat3<'a> {
    bytes: &'a [u8],
    offsets: Vec<usize>,
    count: usize,
}

impl<'a> Mat3<'a> {
    pub fn parse(bytes: &'a [u8]) -> Result<Self> {
        if bytes.len() < 0x84 || &bytes[..4] != b"MAT3" {
            bail!("expected a complete MAT3 header");
        }
        let size = u32::from_be_bytes(bytes[4..8].try_into()?) as usize;
        if size < 0x84 || size > bytes.len() {
            bail!("MAT3 block size exceeds its source");
        }
        let bytes = &bytes[..size];
        let count = u16::from_be_bytes(bytes[8..10].try_into()?) as usize;
        let offsets: Vec<usize> = bytes[0xc..0x84]
            .chunks_exact(4)
            .map(|b| u32::from_be_bytes(b.try_into().unwrap()) as usize)
            .collect();
        if offsets
            .iter()
            .any(|&o| o != 0 && !(0x84..=size).contains(&o))
        {
            bail!("MAT3 table offset outside the block");
        }
        let view = Self {
            bytes,
            offsets,
            count,
        };
        // Each material ID resolves its own init record; do not infer the number
        // of unique entries from the largest ID without validating every entry.
        for i in 0..count {
            view.init(i)?;
        }
        Ok(view)
    }

    pub fn len(&self) -> usize {
        self.count
    }
    pub fn is_empty(&self) -> bool {
        self.count == 0
    }

    fn record(&self, field: usize, index: usize, stride: usize) -> Result<&'a [u8]> {
        if !(0xc..0x84).contains(&field) || field % 4 != 0 || stride == 0 {
            bail!("invalid MAT3 table field or stride");
        }
        let start = self.offsets[(field - 0xc) / 4];
        if start == 0 {
            bail!("material references an absent MAT3 table at {field:#x}");
        }
        let at = index
            .checked_mul(stride)
            .and_then(|o| start.checked_add(o))
            .context("MAT3 table index overflow")?;
        let stop = at
            .checked_add(stride)
            .context("MAT3 record size overflow")?;
        if stop > self.bytes.len() {
            bail!("MAT3 table {field:#x} index {index} exceeds its block");
        }
        Ok(&self.bytes[at..stop])
    }

    fn init(&self, material: usize) -> Result<&'a [u8]> {
        if material >= self.count {
            bail!("MAT3 material index out of range");
        }
        let id = self.record(0x10, material, 2)?;
        self.record(0xc, u16::from_be_bytes(id.try_into()?) as usize, 0x14c)
    }

    /// Resolve a byte selector (0xff means the caller's default).
    pub fn byte_property(
        &self,
        material: usize,
        selector: usize,
        table: usize,
        default: u8,
    ) -> Result<u8> {
        let index = *self
            .init(material)?
            .get(selector)
            .context("MAT3 byte selector outside init record")?;
        if index == 0xff {
            return Ok(default);
        }
        Ok(self.record(table, usize::from(index), 1)?[0])
    }

    /// Resolve a 16-bit selector (0xffff means an absent source property).
    pub fn property(
        &self,
        material: usize,
        selector: usize,
        table: usize,
        stride: usize,
    ) -> Result<Option<&'a [u8]>> {
        let init = self.init(material)?;
        let stop = selector.checked_add(2).context("MAT3 selector overflow")?;
        let index = u16::from_be_bytes(
            init.get(selector..stop)
                .context("MAT3 selector outside init record")?
                .try_into()?,
        );
        if index == 0xffff {
            return Ok(None);
        }
        Ok(Some(self.record(table, usize::from(index), stride)?))
    }

    /// Reconstruct a normal factory material for the compatibility proof. These
    /// source-format bytes never become a shipping material record. Loader flags
    /// choose block variants, so callers must supply the actual model flags.
    pub fn compatibility_program(
        &self,
        material: usize,
        flags: u32,
        post_matrices: bool,
    ) -> Result<[u8; PROGRAM_BYTES]> {
        let init = self.init(material)?;
        let stages = self.byte_property(material, 4, 0x58, 0xff)?;
        let texgens = self.byte_property(material, 3, 0x34, 0)?;
        let channels = self.byte_property(material, 2, 0x24, 0)?;
        if stages > 16 || texgens > 8 || channels > 2 {
            bail!("invalid material source counts");
        }
        let mut texture_count = 0;
        for i in 0..8 {
            if u16::from_be_bytes(init[0x84 + i * 2..0x86 + i * 2].try_into()?) != 0xffff {
                texture_count += 1;
            }
        }
        let required = usize::from(stages)
            .max(texture_count)
            .max(((flags >> 16) & 31) as usize);
        let capacity = match required {
            0..=1 => 1,
            2 => 2,
            3..=4 => 4,
            5..=16 => 16,
            _ => bail!("unsupported stage capacity"),
        };
        let color = flags & (3u32 << 30);
        if color == 3u32 << 30 {
            bail!("unsupported color block flags");
        }
        let mut p = [0u8; PROGRAM_BYTES];
        p[0] = if capacity == 1 { 1 } else { stages };
        p[1] = texgens;
        p[2] = channels;
        let cull = if init[1] == 255 {
            0
        } else {
            u32::from_be_bytes(self.record(0x1c, usize::from(init[1]), 4)?.try_into()?)
        };
        p[4] = match cull {
            1 => 2,
            2 => 1,
            3 => 3,
            _ => 0,
        };
        let mut loads: u16 = (1 << 8) | (1 << 9);
        if capacity > 1 {
            loads |= (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5);
        }
        if color != 0 {
            loads |= 1 << 6;
        }
        if color == 0x40000000 {
            loads |= 1 << 7;
        }
        let indirect = flags & 0x1000000 != 0;
        let ind_data =
            if self.offsets[3] != 0 && self.offsets[3].saturating_sub(self.offsets[2]) > 4 {
                Some(self.record(0x18, material, 0x138)?)
            } else {
                None
            };
        let enabled = ind_data.is_some_and(|b| b[0] != 0);
        if indirect {
            loads |= 1 << 10;
            p[3] = if enabled { ind_data.unwrap()[1] } else { 0 };
            if p[3] > 3 {
                bail!("indirect source has more than three stored orders");
            }
            for i in 0..4 {
                p[576 + 4 * i..578 + 4 * i].fill(0xff);
                if enabled && i < usize::from(p[3]) {
                    let source = ind_data.unwrap();
                    p[576 + 4 * i..578 + 4 * i].copy_from_slice(&source[4 + 4 * i..6 + 4 * i]);
                    p[578 + 4 * i..580 + 4 * i]
                        .copy_from_slice(&source[0x68 + 4 * i..0x6a + 4 * i]);
                }
            }
        }
        const DEFAULT_STAGE: [u8; 20] = [
            4, 10, 15, 15, 0, 0, 0, 0, 1, 0, 5, 7, 7, 0, 0, 0, 0, 1, 0, 0,
        ];
        for i in 0..usize::from(p[0]) {
            let b = 16 + 34 * i;
            let order = self
                .property(material, 0xbc + 2 * i, 0x4c, 4)?
                .unwrap_or(&[255, 255, 255, 0]);
            p[b..b + 3].copy_from_slice(&order[..3]);
            let stage = self
                .property(material, 0xe4 + 2 * i, 0x5c, 20)?
                .unwrap_or(&DEFAULT_STAGE);
            p[b + 3..b + 12].copy_from_slice(&stage[1..10]);
            p[b + 12..b + 21].copy_from_slice(&stage[10..19]);
            if let Some(swap) = self.property(material, 0x104 + 2 * i, 0x60, 4)? {
                p[b + 21..b + 23].copy_from_slice(&swap[..2]);
            }
            if enabled {
                let source = ind_data.unwrap();
                p[b + 25..b + 34].copy_from_slice(&source[0x78 + 12 * i..0x81 + 12 * i]);
            }
        }
        if p[0] & 1 != 0 && p[0] < 16 {
            let i = usize::from(p[0]);
            let order = if capacity == 1 {
                &[255, 255, 255, 0][..]
            } else {
                self.property(material, 0xbc + 2 * i, 0x4c, 4)?
                    .unwrap_or(&[255, 255, 255, 0])
            };
            p[16 + 34 * i..19 + 34 * i].copy_from_slice(&order[..3]);
        }
        if capacity > 1 {
            for i in 0..16 {
                let c = init[0x9c + i % capacity];
                let a = init[0xac + i % capacity];
                p[16 + 34 * i + 23] = if c == 255 { 12 } else { c };
                p[16 + 34 * i + 24] = if a == 255 { 28 } else { a };
            }
            for i in 0..4 {
                let swap = self
                    .property(material, 0x124 + i * 2, 0x64, 4)?
                    .unwrap_or(&[0, 1, 2, 3]);
                p[560 + i * 4..564 + i * 4].copy_from_slice(swap);
            }
        }
        for i in 0..usize::from(texgens) {
            let coord = self
                .property(material, 0x28 + i * 2, 0x38, 4)?
                .unwrap_or(&[1, 4, 60, 0]);
            let b = 592 + 8 * i;
            p[b..b + 3].copy_from_slice(&coord[..3]);
            p[b + 4] = if post_matrices && coord[2] != 60 {
                (3 * i) as u8
            } else {
                61
            };
        }
        for i in 0..4 {
            let c = self
                .property(material, 0xc + 2 * i, 0x28, 8)?
                .unwrap_or(&[0, 0, 0, 2, 2, 0, 255, 255]);
            let b = 656 + 8 * i;
            p[b..b + 6].copy_from_slice(&[
                c[0] & 1,
                c[1] & 1,
                if c[5] == 255 { 0 } else { c[5] & 1 },
                c[2],
                if c[4] == 0 { 0 } else { c[3] & 3 },
                c[4],
            ]);
        }
        let pe = flags & 0x30000000;
        let mode = init[0];
        if pe == 0 && mode & 7 != 0 {
            let edge = mode & 1 == 0 && mode & 2 != 0;
            let translucent = mode & 3 == 0 && mode & 4 != 0;
            p[712] = if edge { 6 } else { 7 };
            p[714] = if edge { 3 } else { 7 };
            p[716..720].copy_from_slice(&[
                u8::from(translucent),
                if translucent { 4 } else { 1 },
                if translucent { 5 } else { 0 },
                3,
            ]);
            p[720..723].copy_from_slice(&[1, 3, u8::from(!translucent)]);
            p[6] = u8::from(!edge);
        } else {
            if pe != 0x10000000 && pe != 0x20000000 {
                bail!("unsupported pixel block flags");
            }
            loads |= 1 << 1;
            if pe == 0x10000000 {
                loads |= 1;
                p[688] = self
                    .property(material, 0x144, 0x68, 0x2c)?
                    .map_or(0, |fog| fog[0]);
            }
            let a = self
                .property(material, 0x146, 0x6c, 8)?
                .context("alpha compare default requires source-table proof")?;
            p[712..715].copy_from_slice(&[a[0], a[2], a[3]]);
            let blend = self
                .property(material, 0x148, 0x70, 4)?
                .unwrap_or(&[1, 4, 5, 5]);
            p[716] = match blend[0] {
                1 => 1,
                2 => 3,
                3 => 2,
                _ => 0,
            };
            p[717] = match blend[1] {
                2 => 8,
                3 => 9,
                n => n,
            };
            p[718] = blend[2];
            p[719] = blend[3];
            if init[6] == 255 {
                bail!("depth mode default requires source-table proof");
            }
            let depth = self.record(0x74, usize::from(init[6]), 4)?;
            p[720..723].copy_from_slice(&depth[..3]);
            p[5] = self.byte_property(material, 7, 0x7c, 1)?;
            p[6] = self.byte_property(material, 5, 0x78, 0)?;
        }
        p[8..10].copy_from_slice(&loads.to_le_bytes());
        normalize_program(&p)
    }
}

/// Normalize captures made before native lighting and texgen encodings landed.
pub fn normalize_program(bytes: &[u8]) -> Result<[u8; PROGRAM_BYTES]> {
    let mut p: [u8; PROGRAM_BYTES] = bytes
        .try_into()
        .context("material program must contain 756 bytes")?;
    if p[0] > 16 || p[1] > 8 || p[2] > 2 || p[3] > 4 {
        bail!("material counts exceed the source format limits");
    }
    if p[662] == 0 {
        for c in 0..4 {
            let b = 656 + c * 8;
            let attenuation = p[b + 5];
            p[b + 4] = if attenuation == 0 { 0 } else { p[b + 4] & 3 };
            p[b + 5] = match attenuation {
                0 => 1,
                1 => 3,
                _ => 2,
            };
        }
        p[662] = 1;
    }
    for g in 0..usize::from(p[1]) {
        let b = 592 + g * 8;
        if p[b + 5] != 0 {
            continue;
        }
        let (kind, source) = (p[b], p[b + 1]);
        let mut row = 5;
        let mut emboss = 5;
        match source {
            0..=3 => row = [0, 1, 3, 4][usize::from(source)],
            4..=11 => row = source + 1,
            12..=18 => emboss = source - 12,
            19..=20 => row = 2,
            _ => (),
        }
        let (mode, light) = match kind {
            2..=9 => (1, kind - 2),
            10 => (if source == 19 { 2 } else { 3 }, 0),
            _ => (0, 0),
        };
        p[b] = mode | (u8::from(kind == 0) << 2) | (u8::from(source <= 3) << 3) | (light << 4);
        p[b + 1] = row | ((emboss & 7) << 4);
        p[b + 5] = 1;
    }
    Ok(p)
}

/// Exact compatibility with nativeShaderOf and gen_j3d_materials.lookup_bytes.
/// Do not reduce unused stages here: the existing table keys retain those bytes.
pub fn program_key(bytes: &[u8]) -> Result<u64> {
    let mut p = normalize_program(bytes)?;
    for (start, end) in [(4, 8), (12, 16), (689, 712), (716, 724), (724, 756)] {
        p[start..end].fill(0);
    }
    Ok(p.iter().fold(0xcbf29ce484222325u64, |h, &b| {
        (h ^ u64::from(b)).wrapping_mul(0x100000001b3)
    }))
}

/// Shader-relevant source fields, reduced exactly as the offline generator.
/// Lengths delimit optional swizzles and indirect data. State, source load masks,
/// matrix places and inactive table entries cannot change this identity.
pub fn shader_key(bytes: &[u8]) -> Result<Vec<u8>> {
    let p = normalize_program(bytes)?;
    let mut key = vec![p[0]];
    for i in 0..usize::from(p[0]) {
        let s = &p[16 + 34 * i..50 + 34 * i];
        let texture =
            s[1] != 255 && (s[3..7].contains(&8) || s[3..7].contains(&9) || s[12..16].contains(&4));
        let raster = s[2] != 255
            && (s[3..7].contains(&10) || s[3..7].contains(&11) || s[12..16].contains(&5));
        key.extend_from_slice(&[
            if texture || s[25..34].iter().any(|&b| b != 0) {
                s[0]
            } else {
                255
            },
            if texture { s[1] } else { 255 },
            if raster { s[2] } else { 255 },
        ]);
        key.extend_from_slice(&s[3..21]);
        for (used, selector) in [(raster, s[21]), (texture, s[22])] {
            key.push(if used { 4 } else { 0 });
            if used {
                let at = 560 + usize::from(selector & 3) * 4;
                key.extend_from_slice(&p[at..at + 4]);
            }
        }
        key.push(if s[3..7].contains(&14) { s[23] } else { 0 });
        key.push(if s[12..16].contains(&6) { s[24] } else { 0 });
        key.push(if p[3] != 0 { 9 } else { 0 });
        if p[3] != 0 {
            key.extend_from_slice(&s[25..34]);
        }
    }
    key.push(p[1]);
    for i in 0..usize::from(p[1]) {
        key.extend_from_slice(&p[592 + i * 8..594 + i * 8]);
    }
    key.push(p[3]);
    key.extend_from_slice(&p[576..576 + usize::from(p[3]) * 4]);
    key.push(p[2] * 2);
    for i in 0..usize::from(p[2]) * 2 {
        key.extend_from_slice(&p[656 + i * 8..662 + i * 8]);
    }
    key.extend_from_slice(&p[712..715]);
    Ok(key)
}

/// The base game's actor passes, in draw order: light mask, player lighting,
/// then the per-shape opacity/reflection/shadow pass. Only compiled shader IDs
/// are stored in the native asset; no runtime combiner changes are needed.
pub fn pass_program(source: &[u8], pass: u32) -> Vec<u8> {
    let mut p = source.to_vec();
    fn channel(p: &mut [u8], ch: usize, enabled: u8, mask: u8) {
        p[656 + ch * 8..662 + ch * 8].copy_from_slice(&[enabled, 0, 0, mask, 2, 2]);
    }
    fn alpha(p: &mut [u8], inputs: [u8; 4]) {
        p[28..32].copy_from_slice(&inputs);
        p[32..39].copy_from_slice(&[0, 0, 0, 1, 0, 0, 0]);
    }
    if pass & 4 != 0 {
        p[712..715].copy_from_slice(&[7, 0, 7]);
        channel(&mut p, 0, 0, 0);
        channel(&mut p, 1, 1, 1);
        alpha(&mut p, [6, 7, 5, 7]);
    }
    if pass & 1 != 0 { channel(&mut p, 2, 0, 0); }
    if pass & 8 != 0 {
        p[..3].copy_from_slice(&[1, 0, 1]);
        p[712..715].copy_from_slice(&[7, 1, 7]);
        p[41..50].fill(0);
        channel(&mut p, 0, 0, 0);
        channel(&mut p, 1, 0, 0);
        alpha(&mut p, [1, 7, 7, 7]);
    }
    if pass & 16 != 0 {
        p[..4].copy_from_slice(&[1, 1, 0, 0]);
        p[712..715].copy_from_slice(&[7, 0, 7]);
        for ch in 0..4 { channel(&mut p, ch, 0, 0); }
        alpha(&mut p, [3, 7, 7, 7]);
        p[19..28].copy_from_slice(&[8, 15, 15, 15, 0, 0, 0, 1, 0]);
        p[16..19].copy_from_slice(&[0, 0, 255]);
        p[50..53].copy_from_slice(&[1, 1, 255]);
    }
    if pass & 2 != 0 { alpha(&mut p, [3, 7, 7, 7]); }
    if pass & 32 != 0 {
        for coord in 0..3 {
            let at = 592 + coord * 8;
            p[at..at + 6].copy_from_slice(&[0, 85, 30, 0, 61, 1]);
        }
    }
    p
}

/// While set, a material this build has no shader for is noted here instead of refused: the tool that lists what mods
/// need (`nebula-import mod-materials`), never a game's cook.
pub static NOTE_MISSING: std::sync::Mutex<Option<Vec<[u8; PROGRAM_BYTES]>>> = std::sync::Mutex::new(None);
/// The same for a material's pass variants, with the pass (a file's refusal never comes from these: the game logs and
/// skips a pass with no shader).
pub static NOTE_PASSES: std::sync::Mutex<Option<Vec<(u8, [u8; PROGRAM_BYTES])>>> = std::sync::Mutex::new(None);

fn pass_shader(bytes: &[u8], pass: u32) -> u32 {
    match built_in_shader(bytes) {
        Ok(Some(id)) => id,
        Ok(None) => {
            if let (Some(noted), Ok(program)) = (NOTE_PASSES.lock().unwrap_or_else(|e| e.into_inner()).as_mut(), normalize_program(bytes)) {
                noted.push((pass as u8, program));
            }
            0
        }
        Err(_) => 0,
    }
}

fn built_in_shader(bytes: &[u8]) -> Result<Option<u32>> {
    const KEYS: &[(u64, u32)] = include!("material_shader_keys.inc");
    let hash = shader_key(bytes)?
        .iter()
        .fold(0xcbf29ce484222325u64, |h, &b| {
            (h ^ u64::from(b)).wrapping_mul(0x100000001b3)
        });
    Ok(KEYS.binary_search_by_key(&hash, |&(key, _)| key).ok().map(|index| KEYS[index].1))
}

/// Resolve a source material directly to this build's precompiled J3D shader.
/// The one-based ID stays independent of the runtime API enum's base value.
pub fn cooked_shader(bytes: &[u8]) -> Result<u32> {
    if let Some(id) = built_in_shader(bytes)? {
        return Ok(id);
    }
    if let Some(noted) = NOTE_MISSING.lock().unwrap_or_else(|e| e.into_inner()).as_mut() {
        noted.push(normalize_program(bytes)?);
        return Ok(1);
    }
    let hash = shader_key(bytes)?
        .iter()
        .fold(0xcbf29ce484222325u64, |h, &b| {
            (h ^ u64::from(b)).wrapping_mul(0x100000001b3)
        });
    bail!("source material shader {hash:016x} absent from this build");
}

/// The build's shader table, passed explicitly so a cook cannot silently use a
/// table from another build. IDs here are the generated, one-based J3D IDs.
pub struct ShaderTable(BTreeMap<u64, u32>);

impl ShaderTable {
    pub fn parse(text: &str) -> Result<Self> {
        let mut entries = BTreeMap::new();
        for line in text
            .lines()
            .map(str::trim)
            .filter(|l| !l.is_empty() && !l.starts_with("//"))
        {
            let (hash, id) = line
                .strip_prefix("{0x")
                .and_then(|s| s.strip_suffix("u},"))
                .and_then(|s| s.split_once("ull, "))
                .context("invalid shader table entry")?;
            let hash = u64::from_str_radix(hash, 16).context("invalid material key")?;
            let id: u32 = id.parse().context("invalid material shader id")?;
            if id == 0 || entries.insert(hash, id).is_some() {
                bail!("zero shader id or duplicate material key");
            }
        }
        if entries.is_empty() {
            bail!("empty material shader table");
        }
        Ok(Self(entries))
    }

    pub fn find(&self, program: &[u8]) -> Result<Option<u32>> {
        Ok(self.0.get(&program_key(program)?).copied())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn state_is_not_shader_identity() {
        let p = [0u8; PROGRAM_BYTES];
        let key = program_key(&p).unwrap();
        for (a, b) in [(4, 8), (12, 16), (689, 712), (716, 756)] {
            for i in a..b {
                let mut q = p;
                q[i] = 255;
                assert_eq!(program_key(&q).unwrap(), key, "byte {i}");
            }
        }
        let mut q = p;
        q[16] = 1;
        assert_ne!(program_key(&q).unwrap(), key);
    }

    #[test]
    fn shader_identity_ignores_inactive_fields_and_load_policy() {
        let mut p = [0u8; PROGRAM_BYTES];
        p[0] = 1;
        p[17] = 255;
        p[18] = 255;
        let key = shader_key(&p).unwrap();
        let mut q = p;
        q[8] = 255;
        q[16 + 34 + 3] = 8; // Inactive stage.
        q[656 + 8] = 1; // Inactive channel.
        q[16 + 23] = 12; // Constant selector without a constant input.
        assert_eq!(shader_key(&q).unwrap(), key);
        q[19] = 14; // Active stage now reads that constant.
        assert_ne!(shader_key(&q).unwrap(), key);
    }

    #[test]
    fn rejects_bad_inputs() {
        assert!(program_key(&[0; 755]).is_err());
        let mut p = [0; PROGRAM_BYTES];
        p[0] = 17;
        assert!(program_key(&p).is_err());
        for bad in [
            "",
            "{0x00ull, 0u},",
            "{0x00ull, 1u},\n{0x00ull, 2u},",
            "garbage",
        ] {
            assert!(ShaderTable::parse(bad).is_err());
        }
    }

    fn source_mat3() -> Vec<u8> {
        let mut b = vec![0; 0x84 + 0x14c + 2 + 4];
        b[..4].copy_from_slice(b"MAT3");
        let size = (b.len() as u32).to_be_bytes();
        b[4..8].copy_from_slice(&size);
        b[8..10].copy_from_slice(&1u16.to_be_bytes());
        b[0xc..0x10].copy_from_slice(&0x84u32.to_be_bytes());
        b[0x10..0x14].copy_from_slice(&0x1d0u32.to_be_bytes());
        b[0x20..0x24].copy_from_slice(&0x1d2u32.to_be_bytes());
        b[0x84..0x1d0].fill(0xff);
        b[0x84 + 8..0x84 + 10].copy_from_slice(&0u16.to_be_bytes());
        b[0x1d2..].copy_from_slice(&[1, 2, 3, 4]);
        b
    }

    #[test]
    fn mat3_resolves_source_indices_and_defaults() {
        let b = source_mat3();
        let m = Mat3::parse(&b).unwrap();
        assert_eq!(m.len(), 1);
        assert_eq!(m.property(0, 8, 0x20, 4).unwrap(), Some(&[1, 2, 3, 4][..]));
        assert_eq!(m.property(0, 10, 0x20, 4).unwrap(), None);
        assert_eq!(m.byte_property(0, 2, 0x24, 7).unwrap(), 7);
        assert!(m.property(1, 8, 0x20, 4).is_err());
        assert!(m.property(0, usize::MAX, 0x20, 4).is_err());
    }

    #[test]
    fn mat3_rejects_reads_outside_block() {
        let mut b = source_mat3();
        b[0x1d0..0x1d2].copy_from_slice(&0xffffu16.to_be_bytes());
        assert!(Mat3::parse(&b).is_err());
        let mut b = source_mat3();
        b[0x84 + 8..0x84 + 10].copy_from_slice(&1u16.to_be_bytes());
        let m = Mat3::parse(&b).unwrap();
        assert!(m.property(0, 8, 0x20, 4).is_err());
        for n in 0..0x84 {
            assert!(Mat3::parse(&b[..n]).is_err());
        }
    }
}
