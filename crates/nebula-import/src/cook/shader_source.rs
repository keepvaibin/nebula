//! A material's native shader as source, from its program record: what tools/dev/gen_j3d_materials.py writes for the built-in
//! set (compared on every record the owner's mods hold), for materials a mod brings. LumaLink compiles the result.

use anyhow::{bail, Result};
use std::fmt::Write as _;

const CC: [&str; 16] = [
    "prev.rgb", "prev.aaa", "r0.rgb", "r0.aaa", "r1.rgb", "r1.aaa", "r2.rgb", "r2.aaa", "t.rgb", "t.aaa", "ras.rgb", "ras.aaa",
    "float3(1.0, 1.0, 1.0)", "float3(128.0, 128.0, 128.0) / 255.0", "k.rgb", "float3(0.0, 0.0, 0.0)",
];
const CA: [&str; 8] = ["prev.a", "r0.a", "r1.a", "r2.a", "t.a", "ras.a", "ka", "0.0"];
/// The fixed constant selections, as the combiner's 8-bit values on the 0-1 scale.
const FRACTION: [&str; 8] = ["1.0", "223.0 / 255.0", "191.0 / 255.0", "159.0 / 255.0", "128.0 / 255.0", "96.0 / 255.0", "64.0 / 255.0", "32.0 / 255.0"];
const BIAS: [&str; 3] = ["", " + 128.0 / 255.0", " - 128.0 / 255.0"];
const SCALE: [&str; 4] = ["", " * 2.0", " * 4.0", " * 0.5"];
const DEST: [&str; 4] = ["prev", "r0", "r1", "r2"];
const COMPARE: [&str; 8] = ["false", "a8 < r8", "a8 == r8", "a8 <= r8", "a8 > r8", "a8 != r8", "a8 >= r8", "true"];
const RGBA: [char; 4] = ['r', 'g', 'b', 'a'];

const STAGES_AT: usize = 16;
const STAGE_BYTES: usize = 34;

struct Stage<'a>(&'a [u8]);

impl Stage<'_> {
    fn coord(&self) -> u8 { self.0[0] }
    fn map(&self) -> u8 { self.0[1] }
    fn channel(&self) -> u8 { self.0[2] }
    fn color_in(&self) -> [u8; 4] { [self.0[3], self.0[4], self.0[5], self.0[6]] }
    fn color_op(&self) -> u8 { self.0[7] }
    fn color_bias(&self) -> u8 { self.0[8] }
    fn color_scale(&self) -> u8 { self.0[9] }
    fn color_clamp(&self) -> u8 { self.0[10] }
    fn color_reg(&self) -> u8 { self.0[11] }
    fn alpha_in(&self) -> [u8; 4] { [self.0[12], self.0[13], self.0[14], self.0[15]] }
    fn alpha_op(&self) -> u8 { self.0[16] }
    fn alpha_bias(&self) -> u8 { self.0[17] }
    fn alpha_scale(&self) -> u8 { self.0[18] }
    fn alpha_clamp(&self) -> u8 { self.0[19] }
    fn alpha_reg(&self) -> u8 { self.0[20] }
    fn ras_swap(&self) -> u8 { self.0[21] }
    fn tex_swap(&self) -> u8 { self.0[22] }
    fn konst_color(&self) -> u8 { self.0[23] }
    fn konst_alpha(&self) -> u8 { self.0[24] }
    fn ind(&self) -> &[u8] { &self.0[25..34] }
    fn uses_ind(&self) -> bool { self.ind().iter().any(|&b| b != 0) }
    fn uses_tex(&self) -> bool {
        self.map() != 0xFF && (self.color_in().contains(&8) || self.color_in().contains(&9) || self.alpha_in().contains(&4))
    }
}

struct Program<'a>(&'a [u8]);

impl Program<'_> {
    fn stage_count(&self) -> usize { self.0[0] as usize }
    fn tex_gens(&self) -> usize { (self.0[1] as usize).min(8) }
    fn channels(&self) -> usize { self.0[2] as usize }
    fn ind_stages(&self) -> usize { (self.0[3] as usize).min(4) }
    fn stages(&self) -> Vec<Stage<'_>> {
        (0..self.stage_count().min(16)).map(|i| Stage(&self.0[STAGES_AT + i * STAGE_BYTES..STAGES_AT + (i + 1) * STAGE_BYTES])).collect()
    }
    fn swap(&self, table: u8) -> &[u8] {
        let at = 560 + 4 * (table as usize & 3);
        &self.0[at..at + 4]
    }
    fn ind_order(&self, n: usize) -> &[u8] {
        &self.0[576 + 4 * n..580 + 4 * n]
    }
    fn tex_gen(&self, n: usize) -> &[u8] {
        &self.0[592 + 8 * n..597 + 8 * n]
    }
    fn channel(&self, n: usize) -> &[u8] {
        &self.0[656 + 8 * n..662 + 8 * n]
    }
    fn alpha_compare(&self) -> [u8; 3] {
        [self.0[712], self.0[713], self.0[714]]
    }
}

fn konst_color(sel: u8) -> Result<String> {
    if sel < 8 {
        let f = FRACTION[sel as usize];
        return Ok(format!("float3({f}, {f}, {f})"));
    }
    if (0x0C..=0x0F).contains(&sel) {
        return Ok(format!("material.kcolor[{}].rgb", sel - 0x0C));
    }
    if (0x10..=0x1F).contains(&sel) {
        let comp = RGBA[((sel - 0x10) / 4) as usize];
        return Ok(format!("material.kcolor[{}].{comp}{comp}{comp}", (sel - 0x10) % 4));
    }
    bail!("constant colour selection {sel}")
}

fn konst_alpha(sel: u8) -> Result<String> {
    if sel < 8 {
        return Ok(FRACTION[sel as usize].to_string());
    }
    if (0x10..=0x1F).contains(&sel) {
        return Ok(format!("material.kcolor[{}].{}", (sel - 0x10) % 4, RGBA[((sel - 0x10) / 4) as usize]));
    }
    bail!("constant alpha selection {sel}")
}

fn swizzle(table: &[u8]) -> String {
    table.iter().map(|&c| RGBA[(c & 3) as usize]).collect()
}

/// A combiner input read from a register is its low 8 bits; others as they are.
fn input(x: &str) -> String {
    let first = x.split('.').next().unwrap_or(x);
    if DEST.contains(&first) {
        format!("j3d_in({x})")
    } else {
        x.to_string()
    }
}

fn emit_channels(p: &Program, out: &mut Vec<String>) {
    let chans = p.channels();
    let lit_any = (0..2 * chans.min(2)).any(|k| p.channel(k)[0] != 0);
    if lit_any {
        out.push("    const uint nq = v.pn_row & 31u;".into());
        out.push("    const float3 nt = float3(dot(native_normal_row(nq).xyz, v.nrm), dot(native_normal_row(nq + 1u).xyz, v.nrm), dot(native_normal_row(nq + 2u).xyz, v.nrm));".into());
        out.push("    const float3 nrm = dot(nt, nt) > 0.0 ? normalize(nt) : nt;".into());
    }
    for ch in 0..2usize {
        let target = if ch == 0 { "o.color" } else { "o.color1" };
        let vtx = format!("v.color{ch}");
        if ch >= chans {
            out.push(format!("    {target} = {vtx};"));
            continue;
        }
        for (part, c) in [("rgb", p.channel(2 * ch)), ("a", p.channel(2 * ch + 1))] {
            let (enable, mat_src, amb_src, mask, diffuse, atten) = (c[0], c[1], c[2], c[3], c[4], c[5]);
            let mat = if mat_src != 0 { format!("{vtx}.{part}") } else { format!("material.mat_color[{ch}].{part}") };
            if enable == 0 {
                out.push(format!("    {target}.{part} = {mat};"));
                continue;
            }
            let amb = if amb_src != 0 { format!("{vtx}.{part}") } else { format!("material.amb_color[{ch}].{part}") };
            let terms: Vec<String> = (0..8)
                .filter(|n| (mask >> n) & 1 != 0)
                .map(|n| format!("j3d_light({n}u, {atten}u, {diffuse}u, v.eye, nrm).{part}"))
                .collect();
            let terms = if terms.is_empty() { "0.0".to_string() } else { terms.join(" + ") };
            out.push(format!("    {target}.{part} = {mat} * saturate(({amb} * 255.0 + {terms}) / 255.0);"));
        }
    }
}

fn emit_vertex(p: &Program, out: &mut Vec<String>) -> Result<()> {
    out.push("void material_vertex(NativeVertex v, inout NativeVaryings o) {".into());
    emit_channels(p, out);
    for i in 0..p.tex_gens() {
        let gen = p.tex_gen(i);
        let (gen_kind, source) = (gen[0], gen[1]);
        let (kind, projective, row) = (gen_kind & 3, (gen_kind >> 2) & 1, source & 15);
        if kind == 2 || kind == 3 {
            out.push(format!("    o.uv[{i}] = float4(o.color.rg, 1.0, 0.0);"));
            continue;
        }
        if kind != 0 {
            bail!("texgen kind {kind} (emboss)");
        }
        let coord = match row {
            0 => "float4(v.pos, 1.0)".to_string(),
            1 => "float4(v.nrm, 1.0)".to_string(),
            3 => "float4(v.tangent, 1.0)".to_string(),
            4 => "float4(v.binormal, 1.0)".to_string(),
            5..=12 => format!("float4(v.uv[{}], 1.0, 1.0)", row - 5),
            _ => bail!("texgen source row {row}"),
        };
        let three = projective != 0;
        out.push(format!("    {{  // coordinate {i}: kind {kind}, projective {projective}, row {row}"));
        out.push(format!("        const float4 c = {coord};"));
        out.push("        float3 res;".into());
        out.push(format!("        if (((vs_header.w >> {i}u) & 1u) != 0u) {{"));
        out.push(format!("            const uint tm = native_tex_matrix(v, {i}u);"));
        let third = if three { "dot(c, native_matrix_row(tm + 2u))" } else { "1.0" };
        out.push(format!("            res = float3(dot(c, native_matrix_row(tm + 0u)), dot(c, native_matrix_row(tm + 1u)), {third});"));
        out.push("        } else {".into());
        let (a, b, c) = (3 * i, 3 * i + 1, 3 * i + 2);
        let third = if three { format!("dot(c, native_tex_row({c}))") } else { "1.0".to_string() };
        out.push(format!("            res = float3(dot(c, native_tex_row({a})), dot(c, native_tex_row({b})), {third});"));
        out.push("        }".into());
        out.push(format!("        const uint pt = native_post({i}u);"));
        out.push("        const bool posted = (pt & 0x3Fu) != 61u;".into());
        out.push("        if (posted) {".into());
        out.push("            if ((pt & 0x100u) != 0u) res = dot(res, res) > 0.0 ? normalize(res) : res;".into());
        out.push(format!(
            "            res = float3(dot(float4(res, 1.0), native_coord_post_row({a})), dot(float4(res, 1.0), native_coord_post_row({b})), dot(float4(res, 1.0), native_coord_post_row({c})));"
        ));
        out.push("        }".into());
        let projected = if three { "true" } else { "posted" };
        out.push(format!("        if ({projected} && res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);"));
        out.push(format!("        o.uv[{i}] = float4(res, {projected} ? 1.0 : 0.0);"));
        out.push("    }".into());
    }
    out.push("    j3d_vertex_override(v, o);".into());
    out.push("}".into());
    Ok(())
}

fn alpha_expression(s: &Stage) -> Result<String> {
    let inputs = s.alpha_in();
    let names = [CA[inputs[0] as usize & 7], CA[inputs[1] as usize & 7], CA[inputs[2] as usize & 7], CA[inputs[3] as usize & 7]];
    let (a, b, c, d) = (input(names[0]), input(names[1]), input(names[2]), names[3]);
    let op = s.alpha_op();
    let expr = if op <= 1 {
        let sign = if op == 1 { "-" } else { "+" };
        format!("({d} {sign} lerp({a}, {b}, {c}){}){}", BIAS[s.alpha_bias() as usize % 3], SCALE[s.alpha_scale() as usize & 3])
    } else if op == 14 || op == 15 {
        let cmp = if op == 14 { ">" } else { "==" };
        format!("{d} + (round({a} * 255.0) {cmp} round({b} * 255.0) ? {c} : 0.0)")
    } else {
        bail!("alpha compare op {op} on colour channels");
    };
    Ok(if s.alpha_clamp() != 0 { format!("saturate({expr})") } else { expr })
}

fn emit_pixel(p: &Program, out: &mut Vec<String>) -> Result<()> {
    out.push("float4 material_color(NativeVaryings i) {".into());
    let stages = p.stages();
    let mut t0 = if p.tex_gens() > 0 {
        "tex[0].Sample(samp[0], j3d_texel(i.uv[0], 0u) / material.tex_scale[0].zw)".to_string()
    } else {
        "float4(0.0, 0.0, 0.0, 0.0)".to_string()
    };
    let k0 = match stages.first() {
        Some(s) => konst_alpha(s.konst_alpha())?,
        None => "1.0".to_string(),
    };
    out.push("    [branch] if (material.ovr.x != 0.0) {".into());
    let mut a0 = "1.0".to_string();
    if let Some(first) = stages.first() {
        out.push(format!("        const float4 t = {t0};"));
        out.push("        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);".into());
        out.push("        const float4 prev = material.reg[0];".into());
        out.push("        const float4 r0 = material.reg[1];".into());
        out.push("        const float4 r1 = material.reg[2];".into());
        out.push("        const float4 r2 = material.reg[3];".into());
        out.push(format!("        const float ka = {k0};"));
        a0 = alpha_expression(first)?;
        t0 = "t".to_string();
    }
    out.push(format!("        return j3d_override({t0}, i.color, {k0}, {a0}, i.pos);"));
    out.push("    }".into());
    out.push("    float4 prev = material.reg[0];".into());
    out.push("    float4 r0 = material.reg[1];".into());
    out.push("    float4 r1 = material.reg[2];".into());
    out.push("    float4 r2 = material.reg[3];".into());
    let ind_used = p.ind_stages() > 0 && stages.iter().any(Stage::uses_ind);
    if ind_used {
        out.push("    float2 tevcoord = float2(0.0, 0.0);".into());
        for n in 0..p.ind_stages() {
            let order = p.ind_order(n);
            let (coord, imap, scale_s, scale_t) = (order[0], order[1], order[2], order[3]);
            out.push(format!(
                "    const float3 ind{n} = round(tex[{imap}].Sample(samp[{imap}], j3d_texel(i.uv[{coord}], {coord}u) / float2({}.0, {}.0) / material.tex_scale[{imap}].zw).abg * 255.0);",
                1u32 << scale_s,
                1u32 << scale_t
            ));
        }
    }
    for (n, s) in stages.iter().enumerate() {
        out.push(format!("    {{  // stage {n}"));
        if s.uses_tex() {
            let (c, map) = (s.coord(), s.map());
            let mut coord = format!("j3d_texel(i.uv[{c}], {c}u) / material.tex_scale[{map}].zw");
            if ind_used && s.uses_ind() {
                let ind = s.ind();
                let (ind_stage, fmt, bias, matrix, wrap_s, wrap_t, add_prev, ind_alpha) = (ind[0], ind[1], ind[2], ind[3], ind[4], ind[5], ind[6], ind[8]);
                if ind_alpha != 0 {
                    bail!("bump alpha");
                }
                out.push(format!("        float2 texel = j3d_texel(i.uv[{c}], {c}u);"));
                for (axis, wrap) in [("x", wrap_s), ("y", wrap_t)] {
                    if wrap == 6 {
                        out.push(format!("        texel.{axis} = 0.0;"));
                    } else if wrap != 0 {
                        let size = 256u32 >> (wrap - 1);
                        out.push(format!("        texel.{axis} = texel.{axis} - {size}.0 * floor(texel.{axis} / {size}.0);"));
                    }
                }
                let shift = [0u32, 3, 4, 5][fmt as usize & 3];
                let add = if fmt == 0 { -128 } else { 1 };
                out.push(format!("        float3 sv = floor(ind{ind_stage} / {}.0);", 1u32 << shift));
                for (b, comp) in ["x", "y", "z"].iter().enumerate() {
                    if bias & (1 << b) != 0 {
                        out.push(format!("        sv.{comp} += {add}.0;"));
                    }
                }
                match matrix {
                    1..=3 => {
                        let m = matrix as usize - 1;
                        out.push(format!(
                            "        const float2 off = float2(dot(material.ind_mtx[{}].xyz, sv), dot(material.ind_mtx[{}].xyz, sv));",
                            2 * m,
                            2 * m + 1
                        ));
                    }
                    5..=7 => out.push("        const float2 off = texel * sv.x / 256.0;".into()),
                    9..=11 => out.push("        const float2 off = texel * sv.y / 256.0;".into()),
                    _ => out.push("        const float2 off = float2(0.0, 0.0);".into()),
                }
                let previous = if add_prev != 0 { "tevcoord + " } else { "" };
                out.push(format!("        tevcoord = {previous}texel + off;"));
                coord = format!("tevcoord / material.tex_scale[{map}].zw");
            }
            out.push(format!("        const float4 t = tex[{map}].Sample(samp[{map}], {coord}).{};", swizzle(p.swap(s.tex_swap()))));
        } else {
            out.push("        const float4 t = float4(0.0, 0.0, 0.0, 0.0);".into());
        }
        let ras = match s.channel() {
            4 => "i.color",
            5 => "i.color1",
            7 | 8 => bail!("bump alpha raster"),
            _ => "float4(0.0, 0.0, 0.0, 0.0)",
        };
        out.push(format!("        const float4 ras = {ras}.{};", swizzle(p.swap(s.ras_swap()))));
        if s.color_in().contains(&14) {
            out.push(format!("        const float3 k = float4({}, 0.0).rgb;", konst_color(s.konst_color())?));
        }
        if s.alpha_in().contains(&6) {
            out.push(format!("        const float ka = {};", konst_alpha(s.konst_alpha())?));
        }
        let inputs = s.color_in();
        let names = [CC[inputs[0] as usize & 15], CC[inputs[1] as usize & 15], CC[inputs[2] as usize & 15], CC[inputs[3] as usize & 15]];
        let (a, b, c, d) = (input(names[0]), input(names[1]), input(names[2]), names[3]);
        let op = s.color_op();
        let mut expr = if op <= 1 {
            let sign = if op == 1 { "-" } else { "+" };
            format!("({d} {sign} lerp({a}, {b}, {c}){}){}", BIAS[s.color_bias() as usize % 3], SCALE[s.color_scale() as usize & 3])
        } else if op == 14 || op == 15 {
            let cmp = if op == 14 { ">" } else { "==" };
            format!("{d} + float3(round({a} * 255.0) {cmp} round({b} * 255.0)) * {c}")
        } else {
            let pack = match op & !1 {
                8 => "round(x.r * 255.0)",
                10 => "round(x.g * 255.0) * 256.0 + round(x.r * 255.0)",
                12 => "dot(round(x * 255.0), float3(1.0, 256.0, 65536.0))",
                _ => bail!("colour op {op}"),
            };
            let cmp = if op % 2 == 0 { ">" } else { "==" };
            // Every x in the pattern is the operand, as a plain text replacement.
            let (pa, pb) = (pack.replace('x', &format!("({a})")), pack.replace('x', &format!("({b})")));
            format!("{d} + (({pa}) {cmp} ({pb}) ? {c} : float3(0.0, 0.0, 0.0))")
        };
        if s.color_clamp() != 0 {
            expr = format!("saturate({expr})");
        }
        out.push(format!("        const float3 rc = {expr};"));
        out.push(format!("        const float ra = {};", alpha_expression(s)?));
        out.push(format!("        {}.rgb = rc;", DEST[s.color_reg() as usize & 3]));
        out.push(format!("        {}.a = ra;", DEST[s.alpha_reg() as usize & 3]));
        out.push("    }".into());
    }
    match stages.last() {
        Some(last) => out.push(format!("    prev = j3d_out(float4({}.rgb, {}.a));", DEST[last.color_reg() as usize & 3], DEST[last.alpha_reg() as usize & 3])),
        None => out.push("    prev = j3d_out(prev);".into()),
    }
    let [comp0, op, comp1] = p.alpha_compare();
    if !(comp0 == 7 && comp1 == 7) && !(op == 1 && (comp0 == 7 || comp1 == 7)) {
        let logic = ["&&", "||", "!=", "=="][op as usize & 3];
        let c0 = COMPARE[comp0 as usize & 7].replace("r8", "round(material.alpha_ref.x * 255.0)");
        let c1 = COMPARE[comp1 as usize & 7].replace("r8", "round(material.alpha_ref.y * 255.0)");
        out.push("    const float a8 = round(prev.a * 255.0);".into());
        out.push(format!("    if (!(({c0}) {logic} ({c1}))) {{"));
        out.push("        discard;".into());
        out.push("    }".into());
    }
    out.push("    prev.rgb = j3d_fog(prev.rgb, i.pos);".into());
    out.push("    return prev;".into());
    out.push("}".into());
    Ok(())
}

const CHANNEL_NORMAL_ROWS: &str = "    const uint nq = v.pn_row & 31u;\n    const float3 nt = float3(dot(native_normal_row(nq).xyz, v.nrm), dot(native_normal_row(nq + 1u).xyz, v.nrm), dot(native_normal_row(nq + 2u).xyz, v.nrm));\n    const float3 nrm = dot(nt, nt) > 0.0 ? normalize(nt) : nt;\n";

fn is_channel_line(line: &str) -> bool {
    let Some(rest) = line.strip_prefix("    o.color") else {
        return false;
    };
    let rest = rest.strip_prefix('1').unwrap_or(rest);
    let rest = match rest.strip_prefix('.') {
        Some(fields) => fields.trim_start_matches(|c: char| c.is_ascii_lowercase()),
        None => rest,
    };
    rest.starts_with(" = ") && line.ends_with(';')
}

fn names(line: &str) -> String {
    line.replace("v.eye", "eye").replace("v.color0", "vtx0").replace("v.color1", "vtx1").replace("o.color1", "lit1").replace("o.color", "lit0")
}

/// `    o.color.rgb = M * saturate((A * 255.0 + TERMS) / 255.0);`
fn lit_parts(line: &str) -> Option<(&str, &str, &str)> {
    let rest = line.strip_prefix("    o.color.rgb = ")?.strip_suffix(") / 255.0);")?;
    let (mat, rest) = rest.split_once(" * saturate((")?;
    let (amb, terms) = rest.split_once(" * 255.0 + ")?;
    Some((mat, amb, terms))
}

/// Every `j3d_light(n, atten, diffuse, v.eye, nrm).rgb` in the terms.
fn light_terms(terms: &str) -> Vec<(&str, &str, &str)> {
    let mut out = Vec::new();
    let mut rest = terms;
    while let Some(at) = rest.find("j3d_light(") {
        let tail = &rest[at + "j3d_light(".len()..];
        let Some(end) = tail.find(", v.eye, nrm).rgb") else {
            break;
        };
        let mut parts = tail[..end].split(", ").map(|p| p.trim_end_matches('u'));
        if let (Some(n), Some(atten), Some(diffuse)) = (parts.next(), parts.next(), parts.next()) {
            out.push((n, atten, diffuse));
        }
        rest = &tail[end..];
    }
    out
}

fn enhanced_channel(line: &str, metal: &str) -> Vec<String> {
    let Some((mat, amb, terms)) = lit_parts(line) else {
        return vec![format!("    {}", names(line).trim())];
    };
    let mut out = vec![
        "    {".to_string(),
        format!("        const float3 enh_mat = {};", names(mat)),
        format!("        const float3 enh_amb = {};", names(amb)),
        "        const float3 enh_amb_lin = enh_lin(enh_amb);".to_string(),
        "        float3 enh_acc = enh_amb_lin * enh_sky(g_enh);".to_string(),
    ];
    for (i, (n, atten, diffuse)) in light_terms(terms).into_iter().enumerate() {
        out.push(format!(
            "        enh_add_light({n}u, {}, {diffuse}u, {}, {metal}, eye, enh_amb, enh_amb_lin, enh_acc);",
            if atten == "3" { "true" } else { "false" },
            if i == 0 { "true" } else { "false" }
        ));
    }
    out.push("        enh_acc *= enh_sky_reflection(g_enh, enh_amb_lin);".to_string());
    out.push("        lit0.rgb = enh_mat * enh_srgb(enh_shoulder(max(enh_acc, 0.0)));".to_string());
    out.push("    }".to_string());
    out
}

/// Coordinates generated from the normal: what an environment map reads.
fn env_coordinates(text: &str) -> Vec<String> {
    let mut found = Vec::new();
    let lines: Vec<&str> = text.lines().collect();
    for pair in lines.windows(2) {
        if let Some(rest) = pair[0].trim_start().strip_prefix("{  // coordinate ") {
            if pair[1].trim_start() == "const float4 c = float4(v.nrm, 1.0);" {
                if let Some((n, _)) = rest.split_once(':') {
                    if !found.iter().any(|f| f == n) {
                        found.push(n.to_string());
                    }
                }
            }
        }
    }
    found.sort();
    found
}

/// The per-pixel lighting variant over a material's text (tools/dev/j3d_pixel_lit.py).
fn pixel_lit(mut text: String) -> String {
    let env = env_coordinates(&text);
    let lit = text.contains("j3d_light(");
    if !lit && env.is_empty() {
        return text;
    }
    let metal = if env.is_empty() { "false" } else { "true" };
    let head = text.find("void material_vertex(").unwrap_or(0);
    let mut block = String::new();
    if lit {
        let lines: Vec<String> = text.lines().filter(|l| is_channel_line(l)).map(str::to_string).collect();
        let first = text.find(lines[0].as_str()).unwrap_or(0);
        let last_line = &lines[lines.len() - 1];
        let last = text.find(last_line.as_str()).unwrap_or(0) + last_line.len() + 1;
        let body: String = lines.iter().map(|l| format!("    {}\n", names(l).trim())).collect();
        let enhanced: String = lines.iter().flat_map(|l| enhanced_channel(l, metal)).map(|r| r + "\n").collect();
        let _ = write!(
            block,
            "void j3d_channels(float3 eye, float3 nrm, float4 vtx0, float4 vtx1, out float4 lit0, out float4 lit1) {{\n    lit0 = vtx0;\n    lit1 = vtx1;\n{body}}}\n#if NATIVE_PIXEL_LIT\nvoid j3d_channels_enhanced(float3 eye, float3 nrm, float4 vtx0, float4 vtx1, out float4 lit0, out float4 lit1) {{\n    lit0 = vtx0;\n    lit1 = vtx1;\n{enhanced}}}\n#endif\n"
        );
        let vertex = "#if NATIVE_PIXEL_LIT\n    o.color = v.color0;\n    o.color1 = v.color1;\n    o.lit_eye = v.eye;\n    o.lit_nrm = nrm;\n#else\n    j3d_channels(v.eye, nrm, v.color0, v.color1, o.color, o.color1);\n#endif\n";
        text.replace_range(first..last, vertex);
    } else {
        // An unlit environment map: only the variant needs the normal, unless a channel with no lights already has it.
        let varyings = "    o.lit_eye = v.eye;\n    o.lit_nrm = nrm;\n#endif\n";
        match text.find(CHANNEL_NORMAL_ROWS) {
            Some(at) => text.insert_str(at + CHANNEL_NORMAL_ROWS.len(), &format!("#if NATIVE_PIXEL_LIT\n{varyings}")),
            None => {
                let at = text[head..].find("{\n").map_or(head, |o| head + o + 2);
                text.insert_str(at, &format!("#if NATIVE_PIXEL_LIT\n{CHANNEL_NORMAL_ROWS}{varyings}"));
            }
        }
    }
    text.insert_str(head, &block);
    let opening = "float4 material_color(NativeVaryings i) {\n";
    let begin = format!(
        "#if NATIVE_PIXEL_LIT\n    j3d_pixel_begin(i);\n{}#endif\n",
        if lit { "    j3d_pixel_channels(i);\n" } else { "    if (j3d_enhanced()) enh_key_highlight(g_enh, i.lit_eye);\n" }
    );
    if let Some(at) = text.find(opening) {
        text.insert_str(at + opening.len(), &begin);
    }
    for coordinate in &env {
        let marker = format!("j3d_texel(i.uv[{coordinate}], {coordinate}u)");
        let mut out = String::with_capacity(text.len());
        for line in text.split_inclusive('\n') {
            let wrapped = line
                .find("const float4 t = tex[")
                .filter(|&at| line[at..].contains(&marker) && line[at + "const float4 t = ".len()..].starts_with("tex["))
                .and_then(|at| {
                    let body_at = at + "const float4 t = ".len();
                    let end = line[body_at..].find(';')? + body_at;
                    let body = &line[body_at..end];
                    // The sample's own coordinate: the marker comes right after the sampler.
                    let after_sampler = body.find("], ").map(|o| o + 3)?;
                    body[after_sampler..].starts_with(&marker).then(|| format!("{}J3D_ENV({body}){}", &line[..body_at], &line[end..]))
                });
            out.push_str(wrapped.as_deref().unwrap_or(line));
        }
        text = out;
    }
    if let Some(at) = text.rfind("    prev.rgb = j3d_fog(prev.rgb, i.pos);\n") {
        text.insert_str(at, "#if NATIVE_PIXEL_LIT\n    if (j3d_enhanced()) prev.rgb = enh_finish(prev.rgb, g_enh);\n#endif\n");
    }
    text
}

/// The headers every material includes, by the name it includes them under.
pub const HEADERS: [(&str, &str); 2] = [
    ("j3d_material.hlsli", include_str!("../../../../runtime/src/render/materials/j3d_material.hlsli")),
    ("native_material.hlsli", include_str!("../../../../runtime/src/render/materials/native_material.hlsli")),
];

/// What a mod's compiled shaders must match: the SHA-1 of the headers, line endings aside, in hexadecimal.
pub fn interface_id() -> String {
    use sha1::{Digest, Sha1};
    let mut hash = Sha1::new();
    for (_, text) in HEADERS {
        hash.update(text.replace('\r', "").as_bytes());
    }
    hash.finalize().iter().map(|b| format!("{b:02x}")).collect()
}

/// A program record's shader, from after its comment line: `#include` on. An effect it can't express is an error naming it.
pub fn material_source(record: &[u8]) -> Result<String> {
    if record.len() != 756 {
        bail!("a material record of {} bytes", record.len());
    }
    let p = Program(record);
    let mut lines = Vec::new();
    emit_vertex(&p, &mut lines)?;
    emit_pixel(&p, &mut lines)?;
    compact(&pixel_lit(format!("#include \"j3d_material.hlsli\"\n\n{}\n", lines.join("\n"))))
}

const SLOTS: [&str; 30] = [
    "reg1", "reg2", "reg3", "kcolor0", "kcolor1", "kcolor2", "kcolor3", "mat_color0", "mat_color1", "amb_color0", "amb_color1",
    "ind_mtx0", "ind_mtx1", "ind_mtx2", "ind_mtx3", "ind_mtx4", "ind_mtx5", "fog", "fog_color", "alpha_ref", "tex_scale0",
    "tex_scale1", "tex_scale2", "tex_scale3", "tex_scale4", "tex_scale5", "tex_scale6", "tex_scale7", "ovr", "ovr_coord",
];
const WHITE: &str = "float4(1.0, 1.0, 1.0, 1.0)";
const REGISTERS: [&str; 4] = ["prev", "r0", "r1", "r2"];

fn has_word(line: &str, word: &str) -> bool {
    let is_name = |b: u8| b.is_ascii_alphanumeric() || b == b'_';
    let bytes = line.as_bytes();
    let mut from = 0;
    while let Some(found) = line[from..].find(word) {
        let at = from + found;
        let end = at + word.len();
        if (at == 0 || !is_name(bytes[at - 1])) && (end == bytes.len() || !is_name(bytes[end])) {
            return true;
        }
        from = end;
    }
    false
}

/// A register's declaration: its name.
fn register_declaration(line: &str) -> Option<&'static str> {
    let rest = line.trim_start();
    let rest = rest.strip_prefix("const ").unwrap_or(rest).strip_prefix("float4 ")?;
    let (name, value) = rest.split_once(" = material.reg[")?;
    let value = value.strip_suffix("];")?;
    if value.len() != 1 || !value.as_bytes()[0].is_ascii_digit() {
        return None;
    }
    REGISTERS.iter().find(|r| **r == name).copied()
}

/// Register declarations no line of their scope reads: the replacing pass's block, then the rest.
fn drop_unused_registers(lines: Vec<&str>) -> Vec<&str> {
    let begin = lines.iter().position(|x| x.contains("if (material.ovr.x != 0.0) {"));
    let end = begin.and_then(|b| (b..lines.len()).find(|&n| lines[n].trim() == "}"));
    let mut out = Vec::with_capacity(lines.len());
    for (n, line) in lines.iter().enumerate() {
        if let Some(name) = register_declaration(line) {
            let scope: &[&str] = match (begin, end) {
                (Some(b), Some(e)) if b < n && n < e => &lines[b..e],
                (_, Some(e)) => &lines[e..],
                _ => &lines[..],
            };
            if scope.iter().filter(|x| has_word(x, name)).count() <= 1 {
                continue;
            }
        }
        out.push(*line);
    }
    out
}

/// At `text`: `j3d_texel(i.uv[C], Cu)`: its coordinate and length.
fn texel_call(text: &str) -> Option<(char, usize)> {
    let rest = text.strip_prefix("j3d_texel(i.uv[")?;
    let coord = rest.chars().next()?;
    let end = rest.find("u)")?;
    Some((coord, "j3d_texel(i.uv[".len() + end + 2))
}

/// After a texel call: ` / float2(A, B)` if it has one, then ` / material.tex_scale[M].zw`: that first part, the map and
/// the whole length.
fn sample_tail(text: &str) -> Option<(&str, char, usize)> {
    let mut at = 0;
    if let Some(rest) = text.strip_prefix(" / float2(") {
        at = " / float2(".len() + rest.find(')')? + 1;
    }
    let rest = text[at..].strip_prefix(" / material.tex_scale[")?;
    let map = rest.chars().next()?;
    let rest = rest[map.len_utf8()..].strip_prefix("].zw")?;
    Some((&text[..at], map, text.len() - rest.len()))
}

/// A coordinate's scale over the size of the one map it samples cancels: a plain coordinate. The others in texels.
fn cancel_scales(text: &str) -> String {
    let mut maps: std::collections::BTreeMap<char, std::collections::BTreeSet<char>> = Default::default();
    let mut texels = std::collections::BTreeSet::new();
    let mut from = 0;
    while let Some(found) = text[from..].find("j3d_texel(") {
        let at = from + found;
        let Some((coord, len)) = texel_call(&text[at..]) else {
            from = at + 1;
            continue;
        };
        match sample_tail(&text[at + len..]) {
            Some((_, map, _)) => {
                maps.entry(coord).or_default().insert(map);
            }
            None => {
                texels.insert(coord);
            }
        }
        from = at + len;
    }
    let mut out = String::with_capacity(text.len());
    let mut from = 0;
    while let Some(found) = text[from..].find("j3d_texel(") {
        let at = from + found;
        out.push_str(&text[from..at]);
        let Some((coord, len)) = texel_call(&text[at..]) else {
            out.push_str("j3d_texel(");
            from = at + "j3d_texel(".len();
            continue;
        };
        from = at + len;
        if let Some((part, _, whole)) = sample_tail(&text[from..]) {
            if !texels.contains(&coord) && maps[&coord].len() == 1 {
                out.push_str(&format!("j3d_coord(i.uv[{coord}]){part}"));
                from += whole;
                continue;
            }
        }
        out.push_str(&format!("(j3d_coord(i.uv[{coord}]) * material.tex_scale[{coord}].xy)"));
    }
    out.push_str(&text[from..]);
    out
}

/// A register read through its low 8 bits needs that only if it can hold more: whether a clamped stage wrote it.
fn drop_wraps(text: &str) -> String {
    let mut in_range: std::collections::BTreeMap<String, bool> = Default::default();
    for (name, value) in [("prev", true), ("r0", false), ("r1", false), ("r2", false)] {
        in_range.insert(format!("{name}.rgb"), value);
        in_range.insert(format!("{name}.a"), value);
    }
    let (mut rc_clamped, mut ra_clamped) = (false, false);
    let mut main = false;
    let mut out = Vec::new();
    for line in text.split('\n') {
        let s = line.trim();
        let mut line = line.to_string();
        if s == format!("float4 prev = {WHITE};") {
            main = true;
        }
        if main {
            let reads = s.starts_with("const float3 rc = ") || s.starts_with("const float ra = ");
            if reads {
                for name in REGISTERS {
                    for (field, part) in [("rgb", "rgb"), ("aaa", "a"), ("a", "a")] {
                        if in_range[&format!("{name}.{part}")] {
                            line = line.replace(&format!("j3d_in({name}.{field})"), &format!("{name}.{field}"));
                        }
                    }
                }
            }
            if s.starts_with("const float3 rc = ") {
                rc_clamped = s.starts_with("const float3 rc = saturate(");
            }
            if s.starts_with("const float ra = ") {
                ra_clamped = s.starts_with("const float ra = saturate(");
            }
            if let Some(name) = REGISTERS.iter().find(|r| s == format!("{r}.rgb = rc;")) {
                in_range.insert(format!("{name}.rgb"), rc_clamped);
            }
            if let Some(name) = REGISTERS.iter().find(|r| s == format!("{r}.a = ra;")) {
                in_range.insert(format!("{name}.a"), ra_clamped);
            }
            if let Some(rest) = s.strip_prefix("prev = j3d_out(float4(").and_then(|r| r.strip_suffix("));")) {
                if let Some((color, alpha)) = rest.split_once(", ") {
                    if in_range.get(color) == Some(&true) && in_range.get(alpha) == Some(&true) {
                        line = line.replace("j3d_out(float4(", "(float4(");
                    }
                }
            }
            if s == "prev = j3d_out(prev);" {
                line = line.replace("j3d_out(prev)", "prev");
            }
        }
        out.push(line);
    }
    out.join("\n")
}

/// The constants a material reads, from its text: `material.kcolor[2]` becomes `material.kcolor2`.
fn name_constants(text: &str) -> String {
    let mut out = text.to_string();
    for (array, count) in [("reg", 4), ("kcolor", 4), ("mat_color", 2), ("amb_color", 2), ("ind_mtx", 6), ("tex_scale", 8)] {
        for i in 0..count {
            out = out.replace(&format!("material.{array}[{i}]"), &format!("material.{array}{i}"));
        }
    }
    out
}

/// A generated material reduced to the constants it reads, with its own MaterialConstants (tools/dev/j3d_compact.py is the
/// same in Python): coordinates lose the scale that cancels, registers the wrap they cannot need.
fn compact(text: &str) -> Result<String> {
    let text = text.replace("material.reg[0]", WHITE);
    let text = drop_unused_registers(text.split('\n').collect()).join("\n");
    let text = drop_wraps(&cancel_scales(&text));
    let text = name_constants(&text);
    let mut mask = 0u32;
    let mut members = String::new();
    for (n, slot) in SLOTS.iter().enumerate() {
        let always = matches!(*slot, "fog" | "fog_color" | "ovr" | "ovr_coord");
        if always || has_word(&text, &format!("material.{slot}")) {
            mask |= 1 << n;
            members.push_str(&format!("    float4 {slot};\n"));
        }
    }
    const INCLUDE: &str = "#include \"j3d_material.hlsli\"";
    if !text.contains(INCLUDE) {
        bail!("a material without the framework");
    }
    Ok(text.replacen(INCLUDE, &format!("// constants 0x{mask:08x}\nstruct MaterialConstants {{\n{members}}};\n{INCLUDE}"), 1))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn record() -> Vec<u8> {
        let mut r = vec![0u8; 756];
        r[0] = 1; // one stage
        r[1] = 1; // one coordinate
        r[2] = 1; // one channel
        let s = STAGES_AT;
        r[s] = 0; // coordinate
        r[s + 1] = 0; // map
        r[s + 2] = 4; // channel
        r[s + 3..s + 7].copy_from_slice(&[15, 8, 10, 15]);
        r[s + 12..s + 16].copy_from_slice(&[7, 4, 5, 7]);
        r[592] = 0;
        r[593] = 5; // from texture coordinate 0
        r[712..715].copy_from_slice(&[7, 0, 7]);
        r
    }

    #[test]
    fn a_plain_material_has_its_vertex_and_pixel_stage() {
        let text = material_source(&record()).unwrap();
        assert!(text.starts_with("// constants 0x"));
        assert!(text.contains("#include \"j3d_material.hlsli\"\n\nvoid material_vertex("));
        assert!(text.contains("float4 fog_color;"));
        assert!(text.contains("const float4 c = float4(v.uv[0], 1.0, 1.0);"));
        assert!(text.contains("const float3 rc = (float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), t.rgb, ras.rgb));"));
        assert!(text.contains("prev.rgb = j3d_fog(prev.rgb, i.pos);"));
        assert!(!text.contains("NATIVE_PIXEL_LIT"));
    }

    #[test]
    fn a_lit_material_gets_the_per_pixel_variant() {
        let mut r = record();
        r[656..662].copy_from_slice(&[1, 0, 0, 1, 2, 3]);
        let text = material_source(&r).unwrap();
        assert!(text.contains("void j3d_channels(float3 eye"));
        assert!(text.contains("enh_add_light(0u, true, 2u, true, false, eye"));
        assert!(text.contains("j3d_pixel_channels(i);"));
    }

    #[test]
    fn the_interface_is_a_hash_of_the_headers() {
        let id = interface_id();
        assert_eq!(id.len(), 40);
        assert!(id.bytes().all(|b| b.is_ascii_hexdigit()));
    }

    #[test]
    fn an_effect_with_no_shader_form_is_named() {
        let mut r = record();
        r[592] = 1;
        assert!(material_source(&r).unwrap_err().to_string().contains("emboss"));
        assert!(material_source(&[0u8; 10]).is_err());
    }
}
