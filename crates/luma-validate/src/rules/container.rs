//! L100-L107: the container (luma-format 2) and its top level (3).

use super::{p, t, Ctx};
use crate::source::Backing;
use crate::zip::{self, EntryKind, Unpacked};
use std::collections::BTreeMap;

pub const MAX_NAME_BYTES: usize = 240;
pub const MAX_FILE: u64 = 4 << 30;
pub const MAX_TOTAL: u64 = 64 << 30;

const TOP: [(&str, bool); 7] =
    [("luma.json", false), ("layers", true), ("code", true), ("media", true), ("saves", true), ("kit", true), ("source", true)];

/// The phrase for what is wrong with an entry name, if anything (luma-format 2, "Names").
pub fn name_problem(name: &str) -> Option<&'static str> {
    if name.len() > MAX_NAME_BYTES {
        return Some("Name.TooLong");
    }
    if name.starts_with('/') {
        return Some("Name.Absolute");
    }
    if name.contains('\\') {
        return Some("Name.Backslash");
    }
    for c in name.chars() {
        if (c as u32) < 0x20 || c == '\u{7f}' {
            return Some("Name.Control");
        }
        if matches!(c, '<' | '>' | ':' | '"' | '|' | '?' | '*') {
            return Some("Name.Forbidden");
        }
        if is_combining(c) {
            return Some("Name.Combining");
        }
    }
    for part in name.split('/') {
        if part.is_empty() {
            return Some("Name.EmptyPart");
        }
        if part == "." || part == ".." {
            return Some("Name.DotPart");
        }
        if part.ends_with('.') || part.ends_with(' ') {
            return Some("Name.TrailingDotOrSpace");
        }
        let stem = part.split('.').next().unwrap_or("").to_ascii_uppercase();
        let stem = stem.trim_end_matches(' ');
        let reserved = matches!(stem, "CON" | "PRN" | "AUX" | "NUL" | "CONIN$" | "CONOUT$")
            || ((stem.starts_with("COM") || stem.starts_with("LPT"))
                && stem.chars().count() == 4
                && stem.chars().nth(3).is_some_and(|c| c.is_ascii_digit() || matches!(c, '¹' | '²' | '³')));
        if reserved {
            return Some("Name.Reserved");
        }
    }
    let parts: Vec<&str> = name.splitn(4, '/').collect();
    if parts.len() >= 2 && parts[0] == "layers" {
        let own_path = parts.len() == 4 && matches!(parts[2], "files" | "assets") && parts[3].starts_with('@');
        if parts[1].starts_with('@') || own_path {
            return Some("Name.LeadingAt");
        }
    }
    None
}

/// Combining marks and conjoining jamo: a name with one has a decomposed twin that macOS treats as the same name,
/// so names must be written precomposed (luma-format 2, revision 1 draft 3).
fn is_combining(c: char) -> bool {
    matches!(c as u32,
        0x0300..=0x036f | 0x0483..=0x0489 | 0x0591..=0x05bd | 0x0610..=0x061a | 0x064b..=0x065f | 0x0e31 | 0x0e34..=0x0e3a
        | 0x1100..=0x11ff | 0x1ab0..=0x1aff | 0x1dc0..=0x1dff | 0x20d0..=0x20ff | 0x302a..=0x302f | 0x3099..=0x309a
        | 0xa960..=0xa97f | 0xd7b0..=0xd7ff | 0xfe20..=0xfe2f)
}

/// Whether the other rules will read this entry in full (kept while it is streamed, so it is read once).
fn wanted(name: &str) -> bool {
    name == "luma.json"
        || name.starts_with("code/")
        || name.starts_with("media/")
        || name.starts_with("saves/")
        || name.starts_with("kit/")
        || (name.starts_with("layers/") && name.split('/').nth(2) == Some("patches"))
}

pub(crate) fn check(ctx: &mut Ctx) {
    let file = ctx.m.shown.clone();
    let count = ctx.m.items.len();
    // An archive written with "\" between folders throughout is read with "/" (zip::backslash_form), never silently:
    // every rule below runs on the names as read.
    if matches!(&ctx.m.backing, Backing::Zip { entries, .. } if zip::backslash_form(entries)) {
        ctx.add(108, &file, None, None, vec![]);
    }
    // Names, kinds, methods and sizes, entry by entry.
    let mut total: u64 = 0;
    for i in 0..count {
        let item = ctx.m.items[i].clone();
        if !item.utf8 {
            ctx.add(102, &file, None, None, vec![("entry", t(item.name.clone())), ("why", p("Name.NotUtf8"))]);
        } else if let Some(why) = name_problem(&item.name) {
            ctx.add(102, &file, None, None, vec![("entry", t(item.name.clone())), ("why", p(why))]);
        }
        match item.kind {
            EntryKind::Link => ctx.add(104, &file, None, None, vec![("entry", t(item.name.clone())), ("what", p("Kind.Link"))]),
            EntryKind::Device => ctx.add(104, &file, None, None, vec![("entry", t(item.name.clone())), ("what", p("Kind.Device"))]),
            _ => {}
        }
        let zip_entry = match &ctx.m.backing {
            Backing::Zip { entries, .. } => Some(entries[i].clone()),
            Backing::Folder(_) => None,
        };
        if let Some(e) = zip_entry {
            if e.encrypted {
                ctx.add(101, &file, None, None, vec![("entry", t(item.name.clone())), ("found", p("Zip.Encrypted"))]);
            } else if e.method.is_none() {
                ctx.add(101, &file, None, None, vec![("entry", t(item.name.clone())), ("found", p("Zip.Method")), ("method", t(e.method_found.to_string()))]);
            }
            if e.local_name_differs {
                ctx.add(102, &file, None, None, vec![("entry", t(item.name.clone())), ("why", p("Name.LocalDiffers"))]);
            }
            if e.data_at.is_none() && item.kind == EntryKind::File {
                ctx.add(106, &file, None, None, vec![("entry", t(item.name.clone())), ("why", p("Unpack.Broken"))]);
            }
        }
        if item.kind == EntryKind::File {
            if item.size > MAX_FILE {
                ctx.add(105, &file, None, None, vec![
                    ("what", p("Limit.FileSize")), ("entry", t(item.name.clone())), ("found", t(item.size.to_string())), ("expected", t(MAX_FILE.to_string())),
                ]);
            }
            total = total.saturating_add(item.size);
        }
    }
    if total > MAX_TOTAL {
        ctx.add(105, &file, None, None, vec![("what", p("Limit.TotalSize")), ("entry", t(file.clone())), ("found", t(total.to_string())), ("expected", t(MAX_TOTAL.to_string()))]);
    }

    // The same name twice, two names differing only in case, a name that is both a file and a folder.
    let mut seen: BTreeMap<String, String> = BTreeMap::new();
    for item in ctx.m.items.clone() {
        if item.kind == EntryKind::Folder {
            continue;
        }
        let folded = item.name.to_lowercase();
        if let Some(other) = seen.get(&folded) {
            let other = other.clone();
            ctx.add(103, &file, None, None, vec![("entry", t(item.name.clone())), ("other", t(other))]);
        } else {
            seen.insert(folded, item.name.clone());
        }
    }
    let files: BTreeMap<String, String> = seen.clone();
    for item in ctx.m.items.clone() {
        let mut parent = item.name.as_str();
        while let Some(cut) = parent.rfind('/') {
            parent = &parent[..cut];
            if let Some(file_name) = files.get(&parent.to_lowercase()) {
                ctx.add(103, &file, None, None, vec![("entry", t(item.name.clone())), ("other", t(file_name.clone()))]);
                break;
            }
        }
    }

    // The top level (luma-format 3), each unknown name once.
    let mut unknown: BTreeMap<String, ()> = BTreeMap::new();
    for item in &ctx.m.items {
        let top = item.name.split('/').next().unwrap_or("");
        let is_folder = item.name.contains('/') || item.kind == EntryKind::Folder;
        let known = TOP.iter().any(|&(name, folder)| name == top && folder == is_folder);
        if !known {
            unknown.insert(if is_folder { format!("{top}/") } else { top.to_string() }, ());
        }
    }
    for name in unknown.into_keys() {
        ctx.add(107, &file, None, None, vec![("entry", t(name))]);
    }

    // Every entry's data, unpacked once in a stream: its size and checksum against the stated ones, and overlaps. At a
    // start these findings come from the kept report of the same `.luma`.
    if ctx.start {
        return;
    }
    let overlaps = match &ctx.m.backing {
        Backing::Zip { entries, .. } => zip::overlaps(entries),
        Backing::Folder(_) => Vec::new(),
    };
    for (a, b) in overlaps {
        let (na, nb) = (ctx.m.items[a].name.clone(), ctx.m.items[b].name.clone());
        ctx.add(106, &file, None, None, vec![("entry", t(nb)), ("why", p("Unpack.Overlap")), ("other", t(na))]);
    }
    if !ctx.m.is_zip() {
        return;
    }
    for i in 0..count {
        let item = ctx.m.items[i].clone();
        if item.kind != EntryKind::File || item.size > MAX_FILE {
            continue;
        }
        let keep = wanted(&item.name);
        let Backing::Zip { bytes, entries } = &mut ctx.m.backing else { return };
        if entries[i].method.is_none() || entries[i].encrypted || entries[i].data_at.is_none() {
            continue;
        }
        match zip::unpack(bytes.as_mut(), &entries[i], keep, item.size) {
            Unpacked::Done { bytes: data, size, crc_ok } => {
                if size != item.size {
                    ctx.add(106, &file, None, None, vec![
                        ("entry", t(item.name.clone())), ("why", p("Unpack.SizeDiffers")), ("found", t(size.to_string())), ("expected", t(item.size.to_string())),
                    ]);
                } else if !crc_ok {
                    ctx.add(106, &file, None, None, vec![("entry", t(item.name.clone())), ("why", p("Unpack.CrcDiffers"))]);
                } else if let Some(data) = data {
                    ctx.m.kept.insert(item.name.clone(), data);
                }
            }
            Unpacked::OverLimit => ctx.add(106, &file, None, None, vec![
                ("entry", t(item.name.clone())), ("why", p("Unpack.Larger")), ("expected", t(item.size.to_string())),
            ]),
            Unpacked::Broken => ctx.add(106, &file, None, None, vec![("entry", t(item.name.clone())), ("why", p("Unpack.Broken"))]),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::name_problem;

    #[test]
    fn names() {
        for (name, why) in [
            ("a/../b", Some("Name.DotPart")),
            ("a//b", Some("Name.EmptyPart")),
            ("/a", Some("Name.Absolute")),
            ("a\\b", Some("Name.Backslash")),
            ("layers/base/files/CON.txt", Some("Name.Reserved")),
            ("layers/base/assets/@other/a.png", Some("Name.LeadingAt")),
            ("layers/base/files/@x", Some("Name.LeadingAt")),
            ("layers/@base/files/a", Some("Name.LeadingAt")),
            ("layers/base/patches/@other.mod/a.bcsv.json", None),
            ("layers/base/assets/a@b.png", None),
            ("layers/base/files/com1", Some("Name.Reserved")),
            ("layers/base/files/COM¹", Some("Name.Reserved")),
            ("layers/base/files/COMPUTER.txt", None),
            ("a/b. ", Some("Name.TrailingDotOrSpace")),
            ("a/b.", Some("Name.TrailingDotOrSpace")),
            ("a/b?", Some("Name.Forbidden")),
            ("a/\u{1}", Some("Name.Control")),
            ("a/か\u{3099}", Some("Name.Combining")),
            ("a/が", None),
            ("media/icon.png", None),
        ] {
            assert_eq!(name_problem(name), why, "{name:?}");
        }
        assert_eq!(name_problem(&"a".repeat(241)), Some("Name.TooLong"));
    }
}
