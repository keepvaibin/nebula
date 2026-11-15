//! FLAC (RFC 9639) decoded to interleaved samples: every frame, with its CRC-16 checked, and the loop a Vorbis comment
//! names (LOOPSTART, LOOPEND or LOOPLENGTH), as sound tools write it. What the file breaks is said by its kind and the
//! byte where it was found, never guessed past.

use anyhow::Result;

/// A FLAC file's sound.
pub struct Flac {
    pub channels: usize,
    pub rate: u32,
    pub bits: u32,
    /// Interleaved, each sample as the file holds it (signed, `bits` wide).
    pub samples: Vec<i32>,
    pub loop_points: Option<(u64, u64)>,
}

/// What is wrong with a FLAC file, and the byte where it was found.
#[derive(Debug, PartialEq)]
pub enum FlacError {
    /// Not a FLAC file (no `fLaC`).
    NotFlac,
    /// Shorter than what it says it holds.
    Cut,
    /// Data that breaks the format, at a byte.
    Broken(usize),
}

impl std::fmt::Display for FlacError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            FlacError::NotFlac => write!(f, "not a FLAC file"),
            FlacError::Cut => write!(f, "the FLAC file is cut off"),
            FlacError::Broken(at) => write!(f, "the FLAC file is damaged at byte {at}"),
        }
    }
}

impl std::error::Error for FlacError {}

struct Bits<'a> {
    b: &'a [u8],
    /// Position in bits.
    at: usize,
}

impl Bits<'_> {
    fn byte(&self) -> usize {
        self.at / 8
    }

    fn read(&mut self, n: u32) -> Result<u64, FlacError> {
        debug_assert!(n <= 64);
        let mut v = 0u64;
        for _ in 0..n {
            let byte = *self.b.get(self.at / 8).ok_or(FlacError::Cut)?;
            v = v << 1 | u64::from(byte >> (7 - self.at % 8) & 1);
            self.at += 1;
        }
        Ok(v)
    }

    fn signed(&mut self, n: u32) -> Result<i64, FlacError> {
        if n == 0 {
            return Ok(0);
        }
        let v = self.read(n)?;
        Ok(if n < 64 && v >> (n - 1) & 1 == 1 { v as i64 - (1i64 << n) } else { v as i64 })
    }

    /// Zeros up to a one: their count.
    fn unary(&mut self) -> Result<u64, FlacError> {
        let mut n = 0u64;
        loop {
            // Whole zero bytes at once.
            if self.at % 8 == 0 {
                while self.b.get(self.at / 8) == Some(&0) {
                    self.at += 8;
                    n += 8;
                    if n > 1 << 32 {
                        return Err(FlacError::Broken(self.byte()));
                    }
                }
            }
            if self.read(1)? == 1 {
                return Ok(n);
            }
            n += 1;
        }
    }

    fn align(&mut self) {
        self.at = self.at.div_ceil(8) * 8;
    }
}

fn crc8(bytes: &[u8]) -> u8 {
    let mut c = 0u8;
    for &b in bytes {
        c ^= b;
        for _ in 0..8 {
            c = if c & 0x80 != 0 { c << 1 ^ 0x07 } else { c << 1 };
        }
    }
    c
}

fn crc16(bytes: &[u8]) -> u16 {
    let mut c = 0u16;
    for &b in bytes {
        c ^= u16::from(b) << 8;
        for _ in 0..8 {
            c = if c & 0x8000 != 0 { c << 1 ^ 0x8005 } else { c << 1 };
        }
    }
    c
}

/// Most samples a file may hold (an effect is far shorter: this only keeps a lying header from asking for memory).
const MOST_SAMPLES: u64 = 1 << 28;

pub fn decode(b: &[u8]) -> Result<Flac, FlacError> {
    if b.get(..4) != Some(b"fLaC") {
        return Err(FlacError::NotFlac);
    }
    let mut at = 4;
    let mut info: Option<(u32, usize, u32, u64)> = None;
    let mut comments: Vec<(String, String)> = Vec::new();
    loop {
        let header = b.get(at..at + 4).ok_or(FlacError::Cut)?;
        let last = header[0] & 0x80 != 0;
        let kind = header[0] & 0x7F;
        let size = (usize::from(header[1]) << 16) | (usize::from(header[2]) << 8) | usize::from(header[3]);
        let body = b.get(at + 4..at + 4 + size).ok_or(FlacError::Cut)?;
        match kind {
            0 => {
                if size < 34 {
                    return Err(FlacError::Broken(at));
                }
                let mut r = Bits { b: body, at: 0 };
                // Block sizes (16 + 16 bits) and frame sizes (24 + 24).
                r.read(32)?;
                r.read(48)?;
                let rate = r.read(20)? as u32;
                let channels = r.read(3)? as usize + 1;
                let bits = r.read(5)? as u32 + 1;
                let total = r.read(36)?;
                if rate == 0 || bits < 4 {
                    return Err(FlacError::Broken(at + 4));
                }
                info = Some((rate, channels, bits, total));
            }
            4 => comments = vorbis_comments(body).ok_or(FlacError::Broken(at + 4))?,
            127 => return Err(FlacError::Broken(at)),
            _ => {}
        }
        at += 4 + size;
        if last {
            break;
        }
    }
    let (rate, channels, bits, total) = info.ok_or(FlacError::Broken(4))?;
    if total > MOST_SAMPLES {
        return Err(FlacError::Broken(4));
    }
    let mut samples: Vec<i32> = Vec::with_capacity((total as usize).saturating_mul(channels).min(1 << 24));
    let mut r = Bits { b, at: at * 8 };
    while r.byte() + 2 <= b.len() {
        let start = r.byte();
        frame(&mut r, b, start, channels, bits, &mut samples)?;
        if samples.len() as u64 > MOST_SAMPLES * channels as u64 {
            return Err(FlacError::Broken(start));
        }
    }
    if total != 0 && (samples.len() / channels) as u64 != total {
        return Err(FlacError::Cut);
    }
    let comment = |name: &str| comments.iter().find(|(k, _)| k.eq_ignore_ascii_case(name)).and_then(|(_, v)| v.trim().parse::<u64>().ok());
    let loop_points = match (comment("LOOPSTART"), comment("LOOPEND"), comment("LOOPLENGTH")) {
        (Some(s), Some(e), _) => Some((s, e)),
        (Some(s), None, Some(n)) => Some((s, s.saturating_add(n))),
        _ => None,
    };
    Ok(Flac { channels, rate, bits, samples, loop_points })
}

/// A Vorbis comment block: (name, value) pairs.
fn vorbis_comments(b: &[u8]) -> Option<Vec<(String, String)>> {
    let le = |at: usize| b.get(at..at + 4).map(|s| u32::from_le_bytes([s[0], s[1], s[2], s[3]]) as usize);
    let vendor = le(0)?;
    let mut at = 4usize.checked_add(vendor)?;
    let count = le(at)?;
    at += 4;
    let mut out = Vec::new();
    for _ in 0..count.min(1 << 16) {
        let n = le(at)?;
        let text = String::from_utf8_lossy(b.get(at + 4..at.checked_add(4 + n)?)?).into_owned();
        at += 4 + n;
        if let Some((k, v)) = text.split_once('=') {
            out.push((k.to_string(), v.to_string()));
        }
    }
    Some(out)
}

fn frame(r: &mut Bits, b: &[u8], start: usize, channels: usize, stream_bits: u32, out: &mut Vec<i32>) -> Result<(), FlacError> {
    let broken = || FlacError::Broken(start);
    if r.read(15)? != 0x7FFC {
        return Err(broken());
    }
    r.read(1)?;
    let size_code = r.read(4)?;
    let rate_code = r.read(4)?;
    let assignment = r.read(4)?;
    let bits_code = r.read(3)?;
    r.read(1)?;
    // The frame or sample number, UTF-8 coded.
    let first = r.read(8)?;
    let more = (first as u8).leading_ones();
    if more == 1 || more > 7 {
        return Err(broken());
    }
    for _ in 1..more {
        if r.read(8)? >> 6 != 2 {
            return Err(broken());
        }
    }
    let block = match size_code {
        0 => return Err(broken()),
        1 => 192,
        2..=5 => 576 << (size_code - 2),
        6 => r.read(8)? as usize + 1,
        7 => r.read(16)? as usize + 1,
        _ => 256 << (size_code - 8),
    };
    match rate_code {
        12 => {
            r.read(8)?;
        }
        13 | 14 => {
            r.read(16)?;
        }
        15 => return Err(broken()),
        _ => {}
    }
    let header_end = r.byte();
    let crc = r.read(8)? as u8;
    if crc8(&b[start..header_end]) != crc {
        return Err(broken());
    }
    let bits = match bits_code {
        0 => stream_bits,
        1 => 8,
        2 => 12,
        4 => 16,
        5 => 20,
        6 => 24,
        7 => 32,
        _ => return Err(broken()),
    };
    let (count, kind) = match assignment {
        0..=7 => (assignment as usize + 1, 0),
        8..=10 => (2, assignment),
        _ => return Err(broken()),
    };
    if count != channels {
        return Err(broken());
    }
    let mut chans: Vec<Vec<i64>> = Vec::with_capacity(count);
    for c in 0..count {
        // The side channel holds one bit more.
        let extra = u32::from((kind == 8 && c == 1) || (kind == 9 && c == 0) || (kind == 10 && c == 1));
        chans.push(subframe(r, block, bits + extra, start)?);
    }
    match kind {
        8 => {
            for i in 0..block {
                chans[1][i] = chans[0][i] - chans[1][i];
            }
        }
        9 => {
            for i in 0..block {
                chans[0][i] += chans[1][i];
            }
        }
        10 => {
            for i in 0..block {
                let side = chans[1][i];
                let mid = chans[0][i] << 1 | (side & 1);
                chans[0][i] = (mid + side) >> 1;
                chans[1][i] = (mid - side) >> 1;
            }
        }
        _ => {}
    }
    r.align();
    let end = r.byte();
    let crc = r.read(16)? as u16;
    if crc16(&b[start..end]) != crc {
        return Err(broken());
    }
    let shift = 32 - bits;
    for i in 0..block {
        for ch in &chans {
            // Kept to the frame's width (a damaged predictor cannot make a wider value).
            out.push(((ch[i] as i32) << shift) >> shift);
        }
    }
    Ok(())
}

fn subframe(r: &mut Bits, block: usize, bits: u32, start: usize) -> Result<Vec<i64>, FlacError> {
    let broken = || FlacError::Broken(start);
    if r.read(1)? != 0 {
        return Err(broken());
    }
    let kind = r.read(6)?;
    let wasted = if r.read(1)? == 1 { r.unary()? + 1 } else { 0 };
    if wasted >= u64::from(bits) {
        return Err(broken());
    }
    let bits = bits - wasted as u32;
    let mut s: Vec<i64> = Vec::with_capacity(block);
    match kind {
        0 => {
            let v = r.signed(bits)?;
            s.resize(block, v);
        }
        1 => {
            for _ in 0..block {
                s.push(r.signed(bits)?);
            }
        }
        8..=12 => {
            let order = (kind - 8) as usize;
            if order > block {
                return Err(broken());
            }
            for _ in 0..order {
                s.push(r.signed(bits)?);
            }
            residual(r, block, order, &mut s, start)?;
            const FIXED: [&[i64]; 5] = [&[], &[1], &[2, -1], &[3, -3, 1], &[4, -6, 4, -1]];
            predict(&mut s, order, FIXED[order], 0);
        }
        32..=63 => {
            let order = (kind - 31) as usize;
            if order > block {
                return Err(broken());
            }
            for _ in 0..order {
                s.push(r.signed(bits)?);
            }
            let precision = r.read(4)? as u32 + 1;
            if precision == 16 {
                return Err(broken());
            }
            let shift = r.signed(5)?;
            if shift < 0 {
                return Err(broken());
            }
            let mut coefs = Vec::with_capacity(order);
            for _ in 0..order {
                coefs.push(r.signed(precision)?);
            }
            residual(r, block, order, &mut s, start)?;
            predict(&mut s, order, &coefs, shift as u32);
        }
        _ => return Err(broken()),
    }
    if wasted > 0 {
        for v in &mut s {
            *v <<= wasted;
        }
    }
    Ok(s)
}

/// Each sample after the warm-up: its residual plus the prediction from the samples before it.
fn predict(s: &mut [i64], order: usize, coefs: &[i64], shift: u32) {
    for i in order..s.len() {
        let mut sum = 0i64;
        for (j, c) in coefs.iter().enumerate() {
            sum = sum.wrapping_add(c.wrapping_mul(s[i - 1 - j]));
        }
        s[i] = s[i].wrapping_add(sum >> shift);
    }
}

fn residual(r: &mut Bits, block: usize, order: usize, s: &mut Vec<i64>, start: usize) -> Result<(), FlacError> {
    let broken = || FlacError::Broken(start);
    let method = r.read(2)?;
    let param_bits = match method {
        0 => 4,
        1 => 5,
        _ => return Err(broken()),
    };
    let escape = (1u64 << param_bits) - 1;
    let partitions_order = r.read(4)? as u32;
    let partitions = 1usize << partitions_order;
    if block % partitions != 0 || (block >> partitions_order) < order {
        return Err(broken());
    }
    for p in 0..partitions {
        let count = (block >> partitions_order) - if p == 0 { order } else { 0 };
        let param = r.read(param_bits)?;
        if param == escape {
            let raw = r.read(5)? as u32;
            for _ in 0..count {
                s.push(r.signed(raw)?);
            }
        } else {
            for _ in 0..count {
                let q = r.unary()?;
                let v = q.checked_shl(param as u32).ok_or_else(broken)? | r.read(param as u32)?;
                s.push(if v & 1 == 1 { -((v >> 1) as i64) - 1 } else { (v >> 1) as i64 });
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A bit writer for hand-made frames.
    #[derive(Default)]
    struct W {
        out: Vec<u8>,
        bits: usize,
    }
    impl W {
        fn put(&mut self, n: u32, v: u64) {
            for i in (0..n).rev() {
                if self.bits % 8 == 0 {
                    self.out.push(0);
                }
                let last = self.out.len() - 1;
                self.out[last] |= ((v >> i & 1) as u8) << (7 - self.bits % 8);
                self.bits += 1;
            }
        }
        fn align(&mut self) {
            self.bits = self.bits.div_ceil(8) * 8;
        }
    }

    fn rice(w: &mut W, v: i64, k: u32) {
        let z = if v < 0 { ((-v - 1) as u64) << 1 | 1 } else { (v as u64) << 1 };
        for _ in 0..z >> k {
            w.put(1, 0);
        }
        w.put(1, 1);
        w.put(k, z & ((1 << k) - 1));
    }

    /// A stereo 16-bit file at 44100 Hz: one frame of `block` samples, left verbatim, right fixed order 2 (as
    /// mid/side when `side`), with a loop comment.
    fn file(left: &[i16], right: &[i16], side: bool) -> Vec<u8> {
        let block = left.len();
        let mut f = b"fLaC".to_vec();
        let mut info = W::default();
        info.put(16, block as u64);
        info.put(16, block as u64);
        info.put(24, 0);
        info.put(24, 0);
        info.put(20, 44100);
        info.put(3, 1);
        info.put(5, 15);
        info.put(36, block as u64);
        info.put(64, 0);
        info.put(64, 0);
        f.extend_from_slice(&[0, 0, 0, 34]);
        f.extend_from_slice(&info.out);
        let mut c = Vec::new();
        c.extend_from_slice(&3u32.to_le_bytes());
        c.extend_from_slice(b"one");
        c.extend_from_slice(&1u32.to_le_bytes());
        let text = b"LOOPSTART=2";
        c.extend_from_slice(&(text.len() as u32).to_le_bytes());
        c.extend_from_slice(text);
        f.extend_from_slice(&[0x84, 0, 0, c.len() as u8]);
        f.extend_from_slice(&c);
        let start = f.len();
        let mut w = W::default();
        w.put(15, 0x7FFC);
        w.put(1, 0);
        w.put(4, 7);
        w.put(4, 9);
        w.put(4, if side { 10 } else { 1 });
        w.put(3, 4);
        w.put(1, 0);
        w.put(8, 0);
        w.put(16, block as u64 - 1);
        let crc = crc8(&w.out);
        w.put(8, u64::from(crc));
        let (a, b): (Vec<i64>, Vec<i64>) = if side {
            let mid: Vec<i64> = left.iter().zip(right).map(|(l, r)| (i64::from(*l) + i64::from(*r)) >> 1).collect();
            let side: Vec<i64> = left.iter().zip(right).map(|(l, r)| i64::from(*l) - i64::from(*r)).collect();
            (mid, side)
        } else {
            (left.iter().map(|v| i64::from(*v)).collect(), right.iter().map(|v| i64::from(*v)).collect())
        };
        // Channel 0 verbatim.
        w.put(1, 0);
        w.put(6, 1);
        w.put(1, 0);
        for v in &a {
            w.put(16, (*v as u64) & 0xFFFF);
        }
        // Channel 1 fixed order 2 (17 bits when it is the side).
        let width = if side { 17 } else { 16 };
        w.put(1, 0);
        w.put(6, 10);
        w.put(1, 0);
        for v in &b[..2] {
            w.put(width, (*v as u64) & ((1 << width) - 1));
        }
        w.put(2, 0);
        w.put(4, 0);
        w.put(4, 6);
        for i in 2..block {
            rice(&mut w, b[i] - (2 * b[i - 1] - b[i - 2]), 6);
        }
        w.align();
        let crc = crc16(&w.out);
        w.put(16, u64::from(crc));
        f.extend_from_slice(&w.out);
        let _ = start;
        f
    }

    #[test]
    fn a_flac_decodes_to_its_samples_and_loop() {
        let left: Vec<i16> = (0..300).map(|i| ((i as f32 * 0.07).sin() * 12000.0) as i16).collect();
        let right: Vec<i16> = (0..300).map(|i| ((i as f32 * 0.11).cos() * -9000.0) as i16).collect();
        for side in [false, true] {
            let got = decode(&file(&left, &right, side)).unwrap();
            assert_eq!((got.channels, got.rate, got.bits), (2, 44100, 16));
            let want: Vec<i32> = left.iter().zip(&right).flat_map(|(l, r)| [i32::from(*l), i32::from(*r)]).collect();
            assert_eq!(got.samples, want, "side {side}");
            assert_eq!(got.loop_points, None, "LOOPSTART alone is no loop");
        }
    }

    #[test]
    fn a_damaged_or_cut_flac_says_so() {
        let left = [1i16, 2, 3, 4, 5, 6, 7, 8];
        let good = file(&left, &left, false);
        assert_eq!(decode(b"RIFF....").err(), Some(FlacError::NotFlac));
        assert_eq!(decode(&good[..good.len() - 3]).err(), Some(FlacError::Cut));
        let mut bad = good.clone();
        let n = bad.len();
        bad[n - 5] ^= 0x10;
        assert!(matches!(decode(&bad).err(), Some(FlacError::Broken(_))));
        // A header claiming 2^36-1 samples is refused before memory is asked for.
        let mut liar = good.clone();
        for b in &mut liar[4 + 4 + 13..4 + 4 + 18] {
            *b |= 0x0F;
        }
        assert!(decode(&liar).is_err());
    }
}
