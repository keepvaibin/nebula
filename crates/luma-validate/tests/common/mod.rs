//! What the tests share: a zip writer that can also lie, the base luma.json, and the check that every message is
//! filled in all 14 languages.
#![allow(dead_code)]

use luma_validate::{message, Finding, LANGUAGES};

// ---------------------------------------------------------------- a zip writer that can also lie

#[derive(Clone)]
pub struct E {
    pub name: String,
    pub data: Vec<u8>,
    pub method: u16,
    pub flags: u16,
    pub unix_mode: Option<u32>,
    pub crc: Option<u32>,
    pub size: Option<u64>,
    pub local_name: Option<String>,
}

pub fn file(name: &str, data: &[u8]) -> E {
    E { name: name.into(), data: data.to_vec(), method: 0, flags: 0, unix_mode: None, crc: None, size: None, local_name: None }
}

pub fn deflated(name: &str, data: &[u8]) -> E {
    E { method: 8, ..file(name, data) }
}

pub fn put16(v: &mut Vec<u8>, x: u16) {
    v.extend_from_slice(&x.to_le_bytes());
}
pub fn put32(v: &mut Vec<u8>, x: u32) {
    v.extend_from_slice(&x.to_le_bytes());
}

/// A zip of the entries in order. A `size` over 4 GiB is written as Zip64 in the central directory.
pub fn zip(entries: &[E]) -> Vec<u8> {
    let mut out = Vec::new();
    let mut central = Vec::new();
    for e in entries {
        let packed = if e.method == 8 { miniz_oxide::deflate::compress_to_vec(&e.data, 6) } else { e.data.clone() };
        let crc = e.crc.unwrap_or_else(|| crc32fast::hash(&e.data));
        let size = e.size.unwrap_or(e.data.len() as u64);
        let offset = out.len() as u32;
        let local = e.local_name.as_deref().unwrap_or(&e.name);
        put32(&mut out, 0x0403_4b50);
        put16(&mut out, 20);
        put16(&mut out, e.flags);
        put16(&mut out, e.method);
        put32(&mut out, 0);
        put32(&mut out, crc);
        put32(&mut out, packed.len() as u32);
        put32(&mut out, size.min(0xffff_ffff) as u32);
        put16(&mut out, local.len() as u16);
        put16(&mut out, 0);
        out.extend_from_slice(local.as_bytes());
        out.extend_from_slice(&packed);

        let zip64 = size >= 0xffff_ffff;
        put32(&mut central, 0x0201_4b50);
        put16(&mut central, if e.unix_mode.is_some() { 0x031e } else { 20 });
        put16(&mut central, if zip64 { 45 } else { 20 });
        put16(&mut central, e.flags);
        put16(&mut central, e.method);
        put32(&mut central, 0);
        put32(&mut central, crc);
        put32(&mut central, packed.len() as u32);
        put32(&mut central, if zip64 { 0xffff_ffff } else { size as u32 });
        put16(&mut central, e.name.len() as u16);
        put16(&mut central, if zip64 { 12 } else { 0 });
        put16(&mut central, 0);
        put16(&mut central, 0);
        put16(&mut central, 0);
        put32(&mut central, e.unix_mode.map_or(0, |m| m << 16));
        put32(&mut central, offset);
        central.extend_from_slice(e.name.as_bytes());
        if zip64 {
            put16(&mut central, 1);
            put16(&mut central, 8);
            central.extend_from_slice(&size.to_le_bytes());
        }
    }
    let cd_offset = out.len() as u32;
    out.extend_from_slice(&central);
    put32(&mut out, 0x0605_4b50);
    put16(&mut out, 0);
    put16(&mut out, 0);
    put16(&mut out, entries.len() as u16);
    put16(&mut out, entries.len() as u16);
    put32(&mut out, central.len() as u32);
    put32(&mut out, cd_offset);
    put16(&mut out, 0);
    out
}

// ---------------------------------------------------------------- luma.json

pub const BASE: [(&str, &str); 7] = [
    ("format", r#""1.0""#),
    ("id", r#""test.mod""#),
    ("version", r#""1.0.0""#),
    ("name", r#""Test""#),
    ("summary", r#""A mod made to break one rule.""#),
    ("authors", r#"["G"]"#),
    ("game", r#"{"id": "RMG"}"#),
];

/// The base luma.json with keys replaced or added; a value of "-" removes the key.
pub fn manifest(over: &[(&str, &str)]) -> Vec<u8> {
    let mut keys: Vec<(String, String)> = BASE.iter().map(|(k, v)| (k.to_string(), v.to_string())).collect();
    for (k, v) in over {
        match keys.iter_mut().find(|(kk, _)| kk == k) {
            Some(e) => e.1 = v.to_string(),
            None => keys.push((k.to_string(), v.to_string())),
        }
    }
    keys.retain(|(_, v)| v != "-");
    let body: Vec<String> = keys.iter().map(|(k, v)| format!("  \"{k}\": {v}")).collect();
    format!("{{\n{}\n}}\n", body.join(",\n")).into_bytes()
}

pub fn luma(over: &[(&str, &str)], more: &[E]) -> Vec<u8> {
    let mut entries = vec![file("luma.json", &manifest(over))];
    entries.extend_from_slice(more);
    zip(&entries)
}

/// A message left with a `{name}` unfilled in any language.
pub fn unfilled(f: &Finding) -> Option<String> {
    for lang in LANGUAGES {
        let text = message(f, lang);
        let mut rest = text.as_str();
        while let Some(open) = rest.find('{') {
            let after = &rest[open + 1..];
            if let Some(close) = after.find('}') {
                let name = &after[..close];
                if !name.is_empty() && name.chars().all(|c| c.is_ascii_lowercase() || c == '_') {
                    return Some(format!("L{} in {lang}: {text}", f.code));
                }
            }
            rest = after;
        }
    }
    None
}
