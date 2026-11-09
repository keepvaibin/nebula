//! Cook layout source materials, including the tint/remap choices animations use.
use super::{LayoutProgram, STAGE_BYTES};
use anyhow::{bail, Context, Result};

pub fn source_size(source: &[u8]) -> Result<usize> {
    let bits = u32::from_be_bytes(
        source
            .get(60..64)
            .context("short layout material")?
            .try_into()?,
    );
    Ok(64
        + (bits & 15) as usize * 4
        + ((bits >> 4) & 15) as usize * 20
        + ((bits >> 8) & 15) as usize * 4
        + ((bits >> 25) & 1) as usize * 4
        + ((bits >> 27) & 1) as usize * 4
        + ((bits >> 12) & 1) as usize * 4
        + ((bits >> 13) & 3) as usize * 20
        + ((bits >> 15) & 7) as usize * 4
        + ((bits >> 18) & 31) as usize * 16
        + ((bits >> 23) & 1) as usize * 4
        + ((bits >> 24) & 1) as usize * 4)
}

fn material_ranges(block: &[u8]) -> Result<Vec<std::ops::Range<usize>>> {
    let count =
        u16::from_be_bytes(block.get(8..10).context("short mat1 header")?.try_into()?) as usize;
    let mut result = Vec::with_capacity(count);
    for i in 0..count {
        let at = 12 + i * 4;
        let start = u32::from_be_bytes(
            block
                .get(at..at + 4)
                .context("short mat1 index table")?
                .try_into()?,
        ) as usize;
        if start < 12 + count * 4 {
            bail!("mat1 material overlaps index table");
        }
        let len = source_size(block.get(start..).context("mat1 material outside block")?)?;
        let end = start.checked_add(len).context("mat1 size overflow")?;
        if end > block.len() {
            bail!("mat1 material exceeds block");
        }
        result.push(start..end);
    }
    Ok(result)
}

pub fn file_programs(bytes: &[u8]) -> Result<Vec<LayoutProgram>> {
    if bytes.len() < 16 || !bytes.starts_with(b"RLYT") {
        bail!("expected source layout");
    }
    let mut at = u16::from_be_bytes(bytes[12..14].try_into()?) as usize;
    let blocks = u16::from_be_bytes(bytes[14..16].try_into()?) as usize;
    let mut result = Vec::new();
    for _ in 0..blocks {
        let size = u32::from_be_bytes(
            bytes
                .get(at + 4..at + 8)
                .context("short layout block")?
                .try_into()?,
        ) as usize;
        if size < 8 {
            bail!("short layout block size");
        }
        let block = bytes
            .get(at..at.checked_add(size).context("layout size overflow")?)
            .context("layout block outside file")?;
        if block.starts_with(b"mat1") {
            for range in material_ranges(block)? {
                result.extend(programs(&block[range])?);
            }
        }
        at += size;
    }
    Ok(result)
}

/// Each coordinate's texture (the map of the last stage or indirect order using it, 255 none) and whether the
/// material has stages of its own; with none, coordinate i reads map i. The table runs to the highest coordinate the
/// generators, the maps or the stages and orders name.
pub fn coord_map(source: &[u8]) -> Result<(Vec<u8>, bool)> {
    let bits = u32::from_be_bytes(source.get(60..64).context("short layout material")?.try_into()?);
    let textures = (bits & 15) as usize;
    let generators = ((bits >> 8) & 15) as usize;
    let (orders_at, stages_at, rest_at) = stage_ranges(bits);
    let source = source.get(..rest_at).context("layout material truncated")?;
    let stages = ((bits >> 18) & 31) as usize;
    let mut named: Vec<(u8, u8)> = Vec::new();
    for s in source[stages_at..rest_at].chunks_exact(16) {
        if s[3] & 1 == 0 && s[0] != 255 && s[2] != 255 {
            named.push((s[0], s[2]));
        }
    }
    for o in source[orders_at..stages_at].chunks_exact(4) {
        if o[0] != 255 && o[1] != 255 {
            named.push((o[0], o[1]));
        }
    }
    let mut len = generators.max(textures);
    for &(coord, _) in &named {
        len = len.max(coord as usize + 1);
    }
    let mut coord_map = vec![255u8; len];
    for &(coord, map) in &named {
        if (map as usize) < textures {
            coord_map[coord as usize] = map;
        }
    }
    if stages == 0 {
        for (i, c) in coord_map.iter_mut().enumerate().take(textures) {
            *c = i as u8;
        }
    }
    Ok((coord_map, stages != 0))
}

/// Where a material's indirect orders, stages and what follows them start.
fn stage_ranges(bits: u32) -> (usize, usize, usize) {
    let textures = (bits & 15) as usize;
    let tex_srt = ((bits >> 4) & 15) as usize;
    let generators = ((bits >> 8) & 15) as usize;
    let swaps = ((bits >> 12) & 1) as usize * 4;
    let indirect_srt = ((bits >> 13) & 3) as usize * 20;
    let indirect = ((bits >> 15) & 7) as usize;
    let stages = ((bits >> 18) & 31) as usize;
    let head = 64 + textures * 4 + tex_srt * 20 + generators * 4 + ((bits >> 25) & 1) as usize * 4 + ((bits >> 27) & 1) as usize * 4;
    let orders_at = head + swaps + indirect_srt;
    let stages_at = orders_at + indirect * 4;
    (orders_at, stages_at, stages_at + stages * 16)
}

/// One of a layout material's four programs as the game draws it (UiFormData in Game/Ui/UiData.hpp): the hashes of its
/// shader pair, and its rows, texture slots and inputs, all of them.
#[derive(Clone, Debug, PartialEq)]
pub struct LayoutForm {
    pub vertex: u64,
    pub pixel: u64,
    /// The vertex program's rows, then the pixel program's.
    pub rows: Vec<u32>,
    pub vertex_rows: usize,
    pub textures: Vec<u8>,
    pub inputs: Vec<u8>,
    pub flags: u8,
}

pub fn form(program: &LayoutProgram) -> Result<LayoutForm> {
    use crate::cook::programs as gen;
    let m = gen::material_from_program(program);
    let mut t = gen::generate(&m, gen::Form::default(), false, &gen::layout_registers())?;
    gen::finish_text("", gen::Form { replace: 0, normal_coord: true, ..gen::Form::default() }, &mut t);
    let rows: Vec<u32> = t.vertex_rows.iter().chain(t.pixel_rows.iter()).copied().collect();
    Ok(LayoutForm {
        vertex: gen::text_hash(&t.vertex),
        pixel: gen::text_hash(&t.pixel),
        rows,
        vertex_rows: t.vertex_rows.len(),
        textures: t.textures.clone(),
        inputs: t.inputs.iter().map(gen::input_code).collect(),
        flags: u8::from(t.alpha_test) << 2,
    })
}

pub fn programs(source: &[u8]) -> Result<[LayoutProgram; 4]> {
    if source.len() < 64 {
        bail!("layout material header is incomplete");
    }
    let bits = u32::from_be_bytes(source[60..64].try_into()?);
    let textures = (bits & 15) as usize;
    let tex_srt = ((bits >> 4) & 15) as usize;
    let generators = ((bits >> 8) & 15) as usize;
    let indirect_srt = ((bits >> 13) & 3) as usize;
    let indirect = ((bits >> 15) & 7) as usize;
    let stages = ((bits >> 18) & 31) as usize;
    let mut at = 64 + textures * 4 + tex_srt * 20;
    let gens = source
        .get(at..at + generators * 4)
        .context("layout generators truncated")?;
    at += generators * 4;
    let channels = if bits & (1 << 25) != 0 {
        let c = source
            .get(at..at + 4)
            .context("layout channel controls truncated")?;
        at += 4;
        [c[0], c[1]]
    } else {
        [1, 1]
    };
    if bits & (1 << 27) != 0 {
        at += 4;
    }
    let mut swaps = [[0, 1, 2, 3], [0, 0, 0, 3], [1, 1, 1, 3], [2, 2, 2, 3]];
    if bits & (1 << 12) != 0 {
        for s in &mut swaps {
            let v = *source.get(at).context("layout swaps truncated")?;
            for (i, c) in s.iter_mut().enumerate() {
                *c = (v >> (2 * i)) & 3;
            }
            at += 1;
        }
    }
    at += indirect_srt * 20;
    let orders = source
        .get(at..at + indirect * 4)
        .context("layout indirect orders truncated")?;
    at += indirect * 4;
    let stage_data = source
        .get(at..at + stages * 16)
        .context("layout stages truncated")?;
    at += stages * 16;
    let alpha = if bits & (1 << 23) != 0 {
        let a = source
            .get(at..at + 4)
            .context("layout alpha comparison truncated")?;
        [a[0] & 15, a[1], a[0] >> 4]
    } else {
        [7, 0, 7]
    };
    let mut result: [LayoutProgram; 4] = Default::default();
    for (variant, p) in result.iter_mut().enumerate() {
        p.channel_count = 1;
        p.channel_sources = channels;
        p.swaps = swaps;
        p.orders = orders.chunks_exact(4).map(|o| [o[0], o[1], o[2], o[3]]).collect();
        p.alpha = alpha;
        for g in gens.chunks_exact(4) {
            p.generators.push([g[0], g[1], g[2], 0, 61, 0, 0, 0]);
        }
        if stages != 0 {
            for s in stage_data.chunks_exact(16) {
                let mut st = LayoutProgram::new_stage();
                st[..3].copy_from_slice(&[s[0], s[2], s[1]]);
                st[21] = (s[3] >> 1) & 3;
                st[22] = (s[3] >> 3) & 3;
                for (from, to) in [(4, 3), (8, 12)] {
                    st[to..to + 9].copy_from_slice(&[
                        s[from] & 15,
                        s[from] >> 4,
                        s[from + 1] & 15,
                        s[from + 1] >> 4,
                        s[from + 2] & 15,
                        (s[from + 2] >> 4) & 3,
                        s[from + 2] >> 6,
                        s[from + 3] & 1,
                        (s[from + 3] >> 1) & 3,
                    ]);
                }
                st[23] = s[7] >> 3;
                st[24] = s[11] >> 3;
                st[25..STAGE_BYTES].copy_from_slice(&[
                    s[12],
                    s[15] & 3,
                    s[13] & 7,
                    (s[13] >> 3) & 15,
                    s[14] & 7,
                    (s[14] >> 3) & 7,
                    (s[15] >> 2) & 1,
                    (s[15] >> 3) & 1,
                    (s[15] >> 4) & 3,
                ]);
                p.stages.push(st);
            }
        } else {
            let add = |stages: &mut Vec<[u8; STAGE_BYTES]>,
                       coord: u8,
                       map: u8,
                       channel: u8,
                       color: [u8; 4],
                       alpha: [u8; 4],
                       constant: Option<u8>| {
                let mut st = LayoutProgram::new_stage();
                st[..3].copy_from_slice(&[coord, map, channel]);
                st[3..7].copy_from_slice(&color);
                st[12..16].copy_from_slice(&alpha);
                st[10] = 1;
                st[19] = 1;
                if let Some(k) = constant {
                    st[23] = k;
                    st[24] = k;
                }
                stages.push(st);
            };
            if textures == 0 {
                add(&mut p.stages, 255, 255, 4, [15, 4, 10, 15], [7, 2, 5, 7], None);
            } else {
                if textures == 1 {
                    add(&mut p.stages, 0, 0, 255, [2, 4, 8, 15], [1, 2, 4, 7], None);
                } else {
                    if textures == 2 {
                        add(&mut p.stages, 0, 0, 255, [15, 15, 15, 8], [7, 7, 7, 4], None);
                        add(&mut p.stages, 1, 1, 255, [8, 0, 14, 15], [4, 0, 6, 7], Some(31));
                    } else {
                        // The constant selectors cycle through the four constant colours' colour and alpha forms.
                        const SELECTORS: [u8; 8] = [31, 27, 23, 19, 30, 26, 22, 18];
                        for i in 0..textures {
                            add(
                                &mut p.stages,
                                i as u8,
                                i as u8,
                                255,
                                [15, 8, 14, if i == 0 { 15 } else { 0 }],
                                [7, 4, 6, if i == 0 { 7 } else { 0 }],
                                Some(SELECTORS[i % SELECTORS.len()]),
                            );
                        }
                    }
                    if variant & 2 != 0 {
                        add(&mut p.stages, 255, 255, 255, [2, 4, 0, 15], [1, 2, 0, 7], None);
                    }
                }
                if variant & 1 != 0 {
                    add(&mut p.stages, 255, 255, 4, [15, 0, 10, 15], [7, 0, 5, 7], None);
                }
            }
        }
        p.normalize();
    }
    Ok(result)
}
