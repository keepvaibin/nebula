//! The disc's layouts, layout animations and fonts through ui_layout (an ignored test: it needs the disc's files).
//! NEBULA_DISC_FILES: an extracted disc's files folder.
//! cargo test -p nebula-import --lib ui_layout_disc -- --ignored --nocapture
use super::*;
use crate::cook::archive;

fn le32(b: &[u8], at: usize) -> u32 {
    u32::from_le_bytes(b[at..at + 4].try_into().unwrap())
}

fn le16(b: &[u8], at: usize) -> u16 {
    u16::from_le_bytes(b[at..at + 2].try_into().unwrap())
}

fn pool_string(file: &[u8], pool: usize, at: u32) -> String {
    let s = &file[pool + at as usize..];
    String::from_utf8_lossy(&s[..s.iter().position(|&c| c == 0).unwrap()]).into_owned()
}

/// Every pane, material and group read back from the cooked bytes equals the IR, and every name is found through
/// its hash table at its first index.
fn check_layout(name: &str, ir: &UiLayoutIr, file: &[u8]) {
    assert_eq!(le32(file, 0), LAYOUT_MAGIC, "{name}");
    let pool = le32(file, 64) as usize;
    let panes = le32(file, 24) as usize;
    assert_eq!(le32(file, 20) as usize, ir.panes.len(), "{name}");
    for (i, p) in ir.panes.iter().enumerate() {
        let r = panes + PANE * i;
        assert_eq!(le16(file, r), p.parent.map_or(NONE16, |q| q as u16), "{name} pane {i} parent");
        assert_eq!(pool_string(file, pool, le32(file, r + 16)), p.name, "{name} pane {i}");
        let end = le16(file, r + 6) as usize;
        for (j, q) in ir.panes.iter().enumerate() {
            let mut a = q.parent;
            let mut inside = j == i;
            while let Some(x) = a {
                inside |= x == i;
                a = ir.panes[x].parent;
            }
            assert_eq!(inside, (i..end).contains(&j), "{name}: pane {j} in {i}'s subtree");
        }
        for (k, v) in p.translate.iter().chain(&p.rotate).chain(&p.scale).chain(&p.size).enumerate() {
            assert_eq!(le32(file, r + 28 + 4 * k), v.to_bits(), "{name} pane {i} transform {k}");
        }
    }
    let lookup = |slots_at: usize, table_at: usize, names: &[&str], what: &str| {
        let slots = le32(file, slots_at) as usize;
        let table = le32(file, table_at) as usize;
        for (i, n) in names.iter().enumerate() {
            let first = names.iter().position(|m| m == n).unwrap();
            let mut s = name_hash(n) as usize & (slots - 1);
            loop {
                let v = le32(file, table + 4 * s) as usize;
                assert_ne!(v, 0, "{name}: {what} {n} not in its table");
                if names[v - 1] == *n {
                    assert_eq!(v - 1, first, "{name}: {what} {i}");
                    break;
                }
                s = (s + 1) & (slots - 1);
            }
        }
    };
    let pane_names: Vec<&str> = ir.panes.iter().map(|p| p.name.as_str()).collect();
    let material_names: Vec<&str> = ir.materials.iter().map(|m| m.name.as_str()).collect();
    let group_names: Vec<&str> = ir.groups.iter().map(|g| g.name.as_str()).collect();
    lookup(72, 76, &pane_names, "pane");
    lookup(80, 84, &material_names, "material");
    lookup(88, 92, &group_names, "group");
    let materials = le32(file, 32) as usize;
    let stride = le32(file, 36) as usize;
    assert!(ir.materials.is_empty() || stride == 1216, "{name}: material stride {stride}");
    for (i, m) in ir.materials.iter().enumerate() {
        let r = materials + stride * i;
        assert_eq!(r % 8, 0);
        assert_eq!(pool_string(file, pool, le32(file, r)), m.name, "{name} material {i}");
        assert_eq!(&file[r + 432..r + 432 + m.forms.len()], &m.forms[..], "{name} material {i} forms");
    }
}

#[test]
#[ignore]
fn ui_layout_disc() {
    let files = std::env::var("NEBULA_DISC_FILES").expect("NEBULA_DISC_FILES");
    let dir = std::path::Path::new(&files).join("LayoutData");
    let (mut layouts, mut animations, mut panes, mut materials, mut fonts) = (0, 0, 0, 0, 0);
    let mut pane_kinds = [0usize; 5];
    let mut duplicate_names = 0;
    let mut entries: Vec<_> = std::fs::read_dir(&dir).unwrap().map(|e| e.unwrap().path()).collect();
    entries.sort();
    for path in entries {
        let raw = std::fs::read(&path).unwrap();
        let bytes = if archive::is_compressed(&raw) { archive::expand(&raw).unwrap() } else { raw };
        for member in archive::members(&bytes).unwrap_or_default() {
            let name = format!("{}/{}", path.file_name().unwrap().to_string_lossy(), member.path);
            let b = &member.bytes;
            if b.starts_with(b"RLYT") {
                let ir = read_layout(b).unwrap_or_else(|e| panic!("{name}: {e:#}"));
                let file = write_layout(&ir).unwrap_or_else(|e| panic!("{name}: {e:#}"));
                check_layout(&name, &ir, &file);
                layouts += 1;
                panes += ir.panes.len();
                materials += ir.materials.len();
                for p in &ir.panes {
                    pane_kinds[match p.payload {
                        UiPayload::Pane => 0,
                        UiPayload::Picture(_) => 1,
                        UiPayload::TextBox(_) => 2,
                        UiPayload::Window(_) => 3,
                        UiPayload::Bounding => 4,
                    }] += 1;
                }
                let mut seen = std::collections::HashSet::new();
                duplicate_names += ir.panes.iter().filter(|p| !seen.insert(p.name.as_str())).count();
            } else if b.starts_with(b"RLAN") {
                let ir = read_animation(b).unwrap_or_else(|e| panic!("{name}: {e:#}"));
                let file = write_animation(&ir).unwrap();
                assert_eq!(le32(&file, 0), ANIMATION_MAGIC);
                assert_eq!(le32(&file, 24) as usize, ir.contents.len());
                animations += 1;
            } else if b.starts_with(b"RFNT") {
                // TGLP: the sheets' format (GX texture format code) at +10 of the block body.
                let header = u16::from_be_bytes([b[12], b[13]]) as usize;
                let mut at = header;
                for _ in 0..u16::from_be_bytes([b[14], b[15]]) {
                    let size = u32::from_be_bytes(b[at + 4..at + 8].try_into().unwrap()) as usize;
                    if &b[at..at + 4] == b"TGLP" {
                        let g = at + 8;
                        println!(
                            "font {name}: sheets {} format {:#x} {}x{} cell {}x{}",
                            u16::from_be_bytes([b[g + 8], b[g + 9]]),
                            u16::from_be_bytes([b[g + 10], b[g + 11]]),
                            u16::from_be_bytes([b[g + 16], b[g + 17]]),
                            u16::from_be_bytes([b[g + 18], b[g + 19]]),
                            b[g],
                            b[g + 1]
                        );
                    }
                    at += size;
                }
                fonts += 1;
            }
        }
    }
    println!(
        "layouts {layouts}: panes {panes} (plain {}, pictures {}, text boxes {}, windows {}, boundings {}), \
         materials {materials}, repeated pane names {duplicate_names}; animations {animations}; fonts {fonts}",
        pane_kinds[0], pane_kinds[1], pane_kinds[2], pane_kinds[3], pane_kinds[4]
    );
    assert!(layouts > 0 && animations > 0);
}
