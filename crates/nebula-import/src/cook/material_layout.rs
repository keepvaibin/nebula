//! Cook layout source materials, including the tint/remap choices animations use.
use super::{cooked_shader, normalize_program, PROGRAM_BYTES};
use anyhow::{bail, Context, Result};

fn source_size(source: &[u8]) -> Result<usize> {
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

pub fn file_programs(bytes: &[u8]) -> Result<Vec<[u8; PROGRAM_BYTES]>> {
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

/// A material without the stages its shaders were cooked from: the bytes kept, each coordinate's texture (the map of
/// the last stage using it, 255 none) and whether it had stages of its own.
fn without_stages(source: &[u8], native: &[u8]) -> Result<(Vec<u8>, [u8; 8], bool)> {
    let bits = u32::from_be_bytes(source.get(60..64).context("short layout material")?.try_into()?);
    let textures = (bits & 15) as usize;
    let tex_srt = ((bits >> 4) & 15) as usize;
    let generators = ((bits >> 8) & 15) as usize;
    let swaps = ((bits >> 12) & 1) as usize * 4;
    let indirect_srt = ((bits >> 13) & 3) as usize * 20;
    let indirect = ((bits >> 15) & 7) as usize;
    let stages = ((bits >> 18) & 31) as usize;
    let head = 64
        + textures * 4
        + tex_srt * 20
        + generators * 4
        + ((bits >> 25) & 1) as usize * 4
        + ((bits >> 27) & 1) as usize * 4;
    let srt_at = head + swaps;
    let orders_at = srt_at + indirect_srt;
    let stages_at = orders_at + indirect * 4;
    let rest_at = stages_at + stages * 16;
    if native.len() < rest_at || source.len() < rest_at {
        bail!("layout material truncated");
    }
    let mut kept = native[..head].to_vec();
    kept.extend_from_slice(&native[srt_at..orders_at]);
    kept.extend_from_slice(&native[rest_at..]);
    let mut coord_map = [255u8; 8];
    let mut set = |coord: u8, map: u8, high: u8| {
        if coord < 8 && map < 8 && high == 0 {
            coord_map[coord as usize] = map;
        }
    };
    for s in source[stages_at..rest_at].chunks_exact(16) {
        set(s[0], s[2], s[3] & 1);
    }
    for o in source[orders_at..stages_at].chunks_exact(4) {
        set(o[0], o[1], 0);
    }
    if stages == 0 {
        for i in 0..textures.min(8) {
            coord_map[i] = i as u8;
        }
    }
    Ok((kept, coord_map, stages != 0))
}

pub fn cook(bytes: &[u8]) -> Result<Vec<u8>> {
    let converted = super::super::native_data::layout(bytes)?;
    if !bytes.starts_with(b"RLYT") || bytes[4..6] == [255, 254] {
        return Ok(converted);
    }
    let mut at = u16::from_be_bytes(bytes[12..14].try_into()?) as usize;
    let blocks = u16::from_be_bytes(bytes[14..16].try_into()?) as usize;
    let mut result = converted[..at].to_vec();
    for _ in 0..blocks {
        let size = u32::from_be_bytes(bytes[at + 4..at + 8].try_into()?) as usize;
        let source = &bytes[at..at + size];
        let native = &converted[at..at + size];
        if source.starts_with(b"mat1") {
            let ranges = material_ranges(source)?;
            let mut block = native[..12 + ranges.len() * 4].to_vec();
            for (i, range) in ranges.into_iter().enumerate() {
                let offset = u32::try_from(block.len())?;
                block[12 + i * 4..16 + i * 4].copy_from_slice(&offset.to_le_bytes());
                let (kept, coord_map, stages) = without_stages(&source[range.clone()], &native[range.clone()])?;
                block.extend_from_slice(&kept);
                block.extend_from_slice(b"NSID");
                for program in programs(&source[range])? {
                    block.extend_from_slice(&cooked_shader(&program)?.to_le_bytes());
                }
                block.extend_from_slice(&coord_map);
                block.extend_from_slice(&[u8::from(stages), 0, 0, 0]);
            }
            let size = u32::try_from(block.len())?;
            block[4..8].copy_from_slice(&size.to_le_bytes());
            result.extend_from_slice(&block);
        } else {
            result.extend_from_slice(native);
        }
        at += size;
    }
    result.extend_from_slice(&converted[at..]);
    let size = u32::try_from(result.len())?;
    result[8..12].copy_from_slice(&size.to_le_bytes());
    Ok(result)
}

pub fn programs(source: &[u8]) -> Result<[[u8; PROGRAM_BYTES]; 4]> {
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
    if textures > 8 || generators > 8 || indirect > 4 || stages > 16 {
        bail!("invalid layout source counts");
    }
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
    let mut result = [[0u8; PROGRAM_BYTES]; 4];
    for (variant, p) in result.iter_mut().enumerate() {
        p[1] = generators as u8;
        p[2] = 1;
        p[3] = indirect as u8;
        p[657] = channels[0];
        p[665] = channels[1];
        for i in 0..16 {
            let b = 16 + i * 34;
            p[b..b + 3].fill(255);
            p[b + 23] = 2;
        }
        for (i, g) in gens.chunks_exact(4).enumerate() {
            p[592 + i * 8..595 + i * 8].copy_from_slice(&g[..3]);
            p[596 + i * 8] = 61;
        }
        for (i, s) in swaps.iter().enumerate() {
            p[560 + i * 4..564 + i * 4].copy_from_slice(s);
        }
        p[576..576 + orders.len()].copy_from_slice(orders);
        p[712..715].copy_from_slice(&alpha);
        if stages != 0 {
            p[0] = stages as u8;
            for (i, s) in stage_data.chunks_exact(16).enumerate() {
                let b = 16 + i * 34;
                p[b..b + 3].copy_from_slice(&[s[0], s[2], s[1]]);
                p[b + 21] = (s[3] >> 1) & 3;
                p[b + 22] = (s[3] >> 3) & 3;
                for (from, to) in [(4, b + 3), (8, b + 12)] {
                    p[to..to + 9].copy_from_slice(&[
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
                p[b + 23] = s[7] >> 3;
                p[b + 24] = s[11] >> 3;
                p[b + 25..b + 34].copy_from_slice(&[
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
            }
        } else {
            let mut n = 0usize;
            let mut add = |coord: u8,
                           map: u8,
                           channel: u8,
                           color: [u8; 4],
                           alpha: [u8; 4],
                           constant: Option<u8>| {
                let b = 16 + n * 34;
                p[b..b + 3].copy_from_slice(&[coord, map, channel]);
                p[b + 3..b + 7].copy_from_slice(&color);
                p[b + 12..b + 16].copy_from_slice(&alpha);
                p[b + 10] = 1;
                p[b + 19] = 1;
                if let Some(k) = constant {
                    p[b + 23] = k;
                    p[b + 24] = k;
                }
                n += 1;
            };
            if textures == 0 {
                add(255, 255, 4, [15, 4, 10, 15], [7, 2, 5, 7], None);
            } else {
                if textures == 1 {
                    add(0, 0, 255, [2, 4, 8, 15], [1, 2, 4, 7], None);
                } else {
                    if textures == 2 {
                        add(0, 0, 255, [15, 15, 15, 8], [7, 7, 7, 4], None);
                        add(1, 1, 255, [8, 0, 14, 15], [4, 0, 6, 7], Some(31));
                    } else {
                        for i in 0..textures {
                            add(
                                i as u8,
                                i as u8,
                                255,
                                [15, 8, 14, if i == 0 { 15 } else { 0 }],
                                [7, 4, 6, if i == 0 { 7 } else { 0 }],
                                Some([31, 27, 23, 19, 30, 26, 22, 18][i]),
                            );
                        }
                    }
                    if variant & 2 != 0 {
                        add(255, 255, 255, [2, 4, 0, 15], [1, 2, 0, 7], None);
                    }
                }
                if variant & 1 != 0 {
                    add(255, 255, 4, [15, 0, 10, 15], [7, 0, 5, 7], None);
                }
            }
            p[0] = n as u8;
        }
        *p = normalize_program(p)?;
    }
    Ok(result)
}
