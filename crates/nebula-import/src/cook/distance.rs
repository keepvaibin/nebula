//! A font's intensity sheets as signed distance fields, so text keeps its shape at any size.
//!
//! A glyph's shape is the one text took when magnified before distance sheets (font_text.hlsl): the cubic B-spline
//! surface of its cell's coverage, cut at one half. Its signed distance is measured exactly (a squared Euclidean
//! distance transform) on a grid `REFERENCE` times finer than the sheet's pixels and stored at `SCALE` texels a
//! pixel, `SPREAD` pixels each way mapped onto 0-255 (the edge at the middle, inside above). Measured against the
//! reference at 8x magnification on menufont64 and messagefont26, `SCALE` 2 is within a fiftieth of a pixel and 3 or 4
//! come no closer; 1 is twice as far.
//!
//! Each cell is measured alone: a neighbour's shape never reaches into it, and the gap pixel between cells gives half
//! its texels to each side. An intensity font (I4, I8) keeps one field. An intensity-and-alpha font (IA4, IA8) keeps
//! two, the fill's (its intensity) as L and the outline's (its alpha) as A: menufont64's letters are a white fill
//! inside a black outline.

use super::texture::Format;

/// Sheet texels a source pixel.
pub const SCALE: u32 = 2;
/// Source pixels from the edge to the field's ends.
pub const SPREAD: u32 = 2;
/// The field's whole range in sheet texels (what a sample's 0-1 spans), as the game's font reads it.
pub const RANGE: u32 = 2 * SPREAD * SCALE;
/// Reference pixels a source pixel. A multiple of `SCALE` by an odd number, so every sheet texel's centre is a
/// reference pixel's.
const REFERENCE: usize = 14;
/// Source pixels measured around a cell: the spread and the spline's reach.
const PAD: usize = 3;
const FAR: f32 = 1.0e20;

/// Whether a sheet format becomes distance fields (the colour formats stay images).
pub fn is_field_format(format: Format) -> bool {
    matches!(format, Format::I4 | Format::I8 | Format::Ia4 | Format::Ia8)
}

/// Whether a field format keeps two fields (fill and outline).
pub fn two_fields(format: Format) -> bool {
    matches!(format, Format::Ia4 | Format::Ia8)
}

/// How a sheet's cells lie: each `width` x `height`, `across` by `down`, one gap pixel before each.
#[derive(Clone, Copy, Debug)]
pub struct Cells {
    pub width: u32,
    pub height: u32,
    pub across: u32,
    pub down: u32,
}

/// One decoded sheet (RGBA texels, `width` x `height`) as fields: `width * SCALE` x `height * SCALE` texels, each
/// [fill, fill, fill, outline] with two fields or [d, d, d, d] with one.
pub fn sheet(texels: &[[u8; 4]], width: u32, height: u32, cells: Cells, two: bool) -> Vec<[u8; 4]> {
    let (w, h) = (width as usize, height as usize);
    let s = SCALE as usize;
    let (ow, oh) = (w * s, h * s);
    let mut out = vec![[0u8; 4]; ow * oh];
    let (cw, ch) = (cells.width as usize, cells.height as usize);
    if cw == 0 || ch == 0 {
        return out;
    }
    let mut work = Work::default();
    for down in 0..cells.down as usize {
        for across in 0..cells.across as usize {
            let (x0, y0) = (across * (cw + 1) + 1, down * (ch + 1) + 1);
            if x0 + cw > w || y0 + ch > h {
                continue;
            }
            // The cell's coverage with PAD empty pixels around it.
            let (rw, rh) = (cw + 2 * PAD, ch + 2 * PAD);
            let coverage = |channel: usize| {
                let mut c = vec![0f32; rw * rh];
                for y in 0..ch {
                    for x in 0..cw {
                        c[(y + PAD) * rw + x + PAD] = texels[(y0 + y) * w + x0 + x][channel] as f32 / 255.0;
                    }
                }
                c
            };
            let outline = work.field(&coverage(3), rw, rh);
            let fill = if two { Some(work.field(&coverage(0), rw, rh)) } else { None };
            // The cell's texels, with half the gap on each side.
            let half = s / 2;
            let (tx0, ty0) = ((x0 * s).saturating_sub(half), (y0 * s).saturating_sub(half));
            let (tx1, ty1) = (((x0 + cw) * s + half).min(ow), ((y0 + ch) * s + half).min(oh));
            let step = REFERENCE / s;
            let fw = rw * REFERENCE;
            for ty in ty0..ty1 {
                // The reference pixel whose centre is this texel's.
                // (PAD added before the cell's place is taken away: the first cell starts inside the pad.)
                let fy = ty * step + step / 2 + PAD * REFERENCE - y0 * REFERENCE;
                for tx in tx0..tx1 {
                    let fx = tx * step + step / 2 + PAD * REFERENCE - x0 * REFERENCE;
                    let a = quantize(outline[fy * fw + fx]);
                    let l = fill.as_ref().map_or(a, |f| quantize(f[fy * fw + fx]));
                    out[ty * ow + tx] = [l, l, l, a];
                }
            }
        }
    }
    out
}

/// A distance (source pixels, inside positive) as a texel.
fn quantize(d: f32) -> u8 {
    (127.5 + d * (127.5 / SPREAD as f32)).round().clamp(0.0, 255.0) as u8
}

/// Scratch reused across cells.
#[derive(Default)]
struct Work {
    rows: Vec<f32>,
    surface: Vec<f32>,
    inside: Vec<f32>,
    outside: Vec<f32>,
    line: Vec<f32>,
    result: Vec<f32>,
    parabolas: Vec<usize>,
    bounds: Vec<f32>,
}

impl Work {
    /// The signed distance (source pixels) of the shape `coverage` (rw x rh) makes, at every reference pixel.
    fn field(&mut self, coverage: &[f32], rw: usize, rh: usize) -> Vec<f32> {
        let (fw, fh) = (rw * REFERENCE, rh * REFERENCE);
        self.spline(coverage, rw, rh);
        // Squared distances to the nearest pixel outside (for those in) and inside (for those out).
        self.inside.clear();
        self.outside.clear();
        for &v in &self.surface {
            let shape = v > 0.5;
            self.inside.push(if shape { FAR } else { 0.0 });
            self.outside.push(if shape { 0.0 } else { FAR });
        }
        let mut inside = std::mem::take(&mut self.inside);
        let mut outside = std::mem::take(&mut self.outside);
        self.transform(&mut inside, fw, fh);
        self.transform(&mut outside, fw, fh);
        let field = inside
            .iter()
            .zip(&outside)
            .map(|(&i, &o)| if i > 0.0 { (i.sqrt() - 0.5) / REFERENCE as f32 } else { -(o.sqrt() - 0.5) / REFERENCE as f32 })
            .collect();
        self.inside = inside;
        self.outside = outside;
        field
    }

    /// The cubic B-spline surface of `coverage` at every reference pixel's centre, one axis at a time.
    fn spline(&mut self, coverage: &[f32], rw: usize, rh: usize) {
        let (fw, fh) = (rw * REFERENCE, rh * REFERENCE);
        let taps: Vec<(isize, [f32; 4])> = (0..REFERENCE).map(weights).collect();
        let sample = |line: &dyn Fn(isize) -> f32, q: usize| {
            let (i, w) = taps[q % REFERENCE];
            let i = i + (q / REFERENCE) as isize;
            w[0] * line(i - 1) + w[1] * line(i) + w[2] * line(i + 1) + w[3] * line(i + 2)
        };
        self.rows.clear();
        self.rows.resize(rh * fw, 0.0);
        for y in 0..rh {
            let row = &coverage[y * rw..(y + 1) * rw];
            let at = |x: isize| if x >= 0 && (x as usize) < rw { row[x as usize] } else { 0.0 };
            for q in 0..fw {
                self.rows[y * fw + q] = sample(&at, q);
            }
        }
        self.surface.clear();
        self.surface.resize(fh * fw, 0.0);
        for x in 0..fw {
            let rows = &self.rows;
            let at = |y: isize| if y >= 0 && (y as usize) < rh { rows[y as usize * fw + x] } else { 0.0 };
            for q in 0..fh {
                self.surface[q * fw + x] = sample(&at, q);
            }
        }
    }

    /// Squared Euclidean distance transform in place (0 at the targets, FAR elsewhere): columns, then rows.
    fn transform(&mut self, grid: &mut [f32], w: usize, h: usize) {
        for x in 0..w {
            self.line.clear();
            self.line.extend((0..h).map(|y| grid[y * w + x]));
            self.pass(h);
            for y in 0..h {
                grid[y * w + x] = self.result[y];
            }
        }
        for y in 0..h {
            self.line.clear();
            self.line.extend_from_slice(&grid[y * w..(y + 1) * w]);
            self.pass(w);
            grid[y * w..(y + 1) * w].copy_from_slice(&self.result[..w]);
        }
    }

    /// One dimension of the transform: the lower envelope of the parabolas rooted at `line` (Felzenszwalb and
    /// Huttenlocher, "Distance Transforms of Sampled Functions").
    fn pass(&mut self, n: usize) {
        let f = &self.line;
        self.result.clear();
        self.result.resize(n, FAR);
        self.parabolas.clear();
        self.bounds.clear();
        let first = (0..n).find(|&q| f[q] < FAR);
        let Some(first) = first else { return };
        self.parabolas.push(first);
        self.bounds.push(f32::NEG_INFINITY);
        self.bounds.push(f32::INFINITY);
        for q in first + 1..n {
            if f[q] >= FAR {
                continue;
            }
            loop {
                let v = *self.parabolas.last().unwrap();
                let s = ((f[q] + (q * q) as f32) - (f[v] + (v * v) as f32)) / (2.0 * (q as f32 - v as f32));
                let k = self.parabolas.len() - 1;
                if s <= self.bounds[k] {
                    self.parabolas.pop();
                    self.bounds.pop();
                    if self.parabolas.is_empty() {
                        self.parabolas.push(q);
                        *self.bounds.last_mut().unwrap() = f32::NEG_INFINITY;
                        self.bounds.push(f32::INFINITY);
                        break;
                    }
                } else {
                    *self.bounds.last_mut().unwrap() = s;
                    self.parabolas.push(q);
                    self.bounds.push(f32::INFINITY);
                    break;
                }
            }
        }
        let mut k = 0;
        for q in 0..n {
            while self.bounds[k + 1] < q as f32 {
                k += 1;
            }
            let v = self.parabolas[k];
            let d = q as f32 - v as f32;
            self.result[q] = d * d + f[v];
        }
    }
}

/// The spline's taps for the reference pixel at phase `q` in a source pixel: the first source pixel it reads (less
/// one) relative to that pixel and its four weights.
fn weights(q: usize) -> (isize, [f32; 4]) {
    // The reference pixel's centre in source texel coordinates (texel i's centre at i).
    let p = (q as f32 + 0.5) / REFERENCE as f32 - 0.5;
    let i = p.floor();
    let f = p - i;
    let w0 = (1.0 - f) * (1.0 - f) * (1.0 - f) / 6.0;
    let w1 = (4.0 + f * f * (3.0 * f - 6.0)) / 6.0;
    let w3 = f * f * f / 6.0;
    let w2 = 1.0 - w0 - w1 - w3;
    (i as isize, [w0, w1, w2, w3])
}

#[cfg(test)]
mod tests {
    use super::*;

    // One 8x8 cell (a 4x4 square of full coverage in its middle) in a 16x16 sheet; a second cell beside it empty.
    fn square(intensity: u8) -> (Vec<[u8; 4]>, Cells) {
        let mut t = vec![[0u8; 4]; 16 * 16];
        for y in 3..7 {
            for x in 3..7 {
                t[y * 16 + x] = [intensity, intensity, intensity, 255];
            }
        }
        (t, Cells { width: 6, height: 6, across: 2, down: 2 })
    }

    #[test]
    fn a_square_is_inside_above_the_middle() {
        let (t, cells) = square(255);
        let out = sheet(&t, 16, 16, cells, false);
        let at = |x: usize, y: usize| out[y * 32 + x][3];
        // The square spans source pixels 3-6 (texels 6-13): its centre is deep inside, far outside is zero.
        assert!(at(10, 10) > 200, "{}", at(10, 10));
        assert_eq!(at(1, 10), 0);
        // Across its edge the field falls through the middle.
        assert!(at(7, 10) > 128 && at(4, 10) < 128, "{} {}", at(7, 10), at(4, 10));
        // The other cells (empty) stay outside.
        assert_eq!(out[10 * 32 + 26][3], 0);
        // One field: every channel the same.
        assert!(out.iter().all(|p| p[0] == p[3] && p[1] == p[3] && p[2] == p[3]));
    }

    #[test]
    fn two_fields_keep_the_fill_apart_from_the_outline() {
        let (t, cells) = square(0);
        let out = sheet(&t, 16, 16, cells, true);
        // A black fill: the fill's field is outside everywhere, the outline's is the square.
        assert_eq!(out[10 * 32 + 10][0], 0);
        assert!(out[10 * 32 + 10][3] > 200);
    }

    #[test]
    fn texel_centres_land_on_reference_pixels() {
        assert_eq!(REFERENCE % SCALE as usize, 0);
        assert_eq!((REFERENCE / SCALE as usize) % 2, 1);
    }

    #[test]
    fn the_transform_is_exact_on_a_line() {
        let mut work = Work::default();
        let mut grid = vec![FAR; 9];
        grid[4] = 0.0;
        work.transform(&mut grid, 9, 1);
        assert_eq!(grid, vec![16.0, 9.0, 4.0, 1.0, 0.0, 1.0, 4.0, 9.0, 16.0]);
    }
}
