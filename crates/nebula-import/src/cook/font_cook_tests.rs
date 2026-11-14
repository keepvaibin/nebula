use super::*;
use crate::cook::refuse::{said, said_notes};

/// A glyph of a test font: its advance and its contours (on-curve points, font units).
struct TestGlyph {
    advance: u16,
    contours: Vec<Vec<(i16, i16)>>,
}

fn be16(out: &mut Vec<u8>, v: u16) {
    out.extend_from_slice(&v.to_be_bytes());
}

fn be32(out: &mut Vec<u8>, v: u32) {
    out.extend_from_slice(&v.to_be_bytes());
}

/// A TrueType font of 1000 units an em (ascender 800, descender -200, gap 0): `glyphs` (glyph 0 is .notdef), `map`
/// (code, glyph), and kern-table pairs (left glyph, right glyph, value).
fn test_font(glyphs: &[TestGlyph], map: &[(u32, u16)], kern: &[(u16, u16, i16)]) -> Vec<u8> {
    let n = glyphs.len() as u16;
    let mut glyf = Vec::new();
    let mut loca = Vec::new();
    for g in glyphs {
        be32(&mut loca, glyf.len() as u32);
        if g.contours.is_empty() {
            continue;
        }
        let points: Vec<(i16, i16)> = g.contours.iter().flatten().copied().collect();
        let (x0, x1) = (points.iter().map(|p| p.0).min().unwrap(), points.iter().map(|p| p.0).max().unwrap());
        let (y0, y1) = (points.iter().map(|p| p.1).min().unwrap(), points.iter().map(|p| p.1).max().unwrap());
        be16(&mut glyf, g.contours.len() as u16);
        for v in [x0, y0, x1, y1] {
            be16(&mut glyf, v as u16);
        }
        let mut end = 0;
        for c in &g.contours {
            end += c.len();
            be16(&mut glyf, (end - 1) as u16);
        }
        be16(&mut glyf, 0); // no instructions
        glyf.extend(std::iter::repeat_n(1u8, points.len())); // on curve, long deltas
        let (mut px, mut py) = (0i16, 0i16);
        for p in &points {
            be16(&mut glyf, (p.0 - px) as u16);
            px = p.0;
        }
        for p in &points {
            be16(&mut glyf, (p.1 - py) as u16);
            py = p.1;
        }
        glyf.resize(glyf.len().next_multiple_of(4), 0);
    }
    be32(&mut loca, glyf.len() as u32);

    let mut head = Vec::new();
    for v in [0x0001_0000u32, 0x0001_0000, 0, 0x5F0F_3CF5] {
        be32(&mut head, v);
    }
    be16(&mut head, 0); // flags
    be16(&mut head, 1000); // units an em
    head.extend_from_slice(&[0; 16]); // created, modified
    for v in [0i16, -200, 1000, 800] {
        be16(&mut head, v as u16);
    }
    for v in [0u16, 8, 2, 1, 0] {
        be16(&mut head, v); // mac style, lowest ppem, direction, long offsets, glyph data format
    }

    let mut hhea = Vec::new();
    be32(&mut hhea, 0x0001_0000);
    for v in [800i16, -200, 0] {
        be16(&mut hhea, v as u16);
    }
    be16(&mut hhea, glyphs.iter().map(|g| g.advance).max().unwrap_or(0));
    hhea.extend_from_slice(&[0; 22]);
    be16(&mut hhea, n);

    let mut maxp = Vec::new();
    be32(&mut maxp, 0x0000_5000);
    be16(&mut maxp, n);

    let mut hmtx = Vec::new();
    for g in glyphs {
        be16(&mut hmtx, g.advance);
        be16(&mut hmtx, 0);
    }

    let mut cmap = Vec::new();
    for v in [0u16, 1, 0, 4] {
        be16(&mut cmap, v); // version, one table: Unicode, full repertoire
    }
    be32(&mut cmap, 12);
    be16(&mut cmap, 12);
    be16(&mut cmap, 0);
    be32(&mut cmap, 16 + 12 * map.len() as u32);
    be32(&mut cmap, 0);
    be32(&mut cmap, map.len() as u32);
    let mut sorted = map.to_vec();
    sorted.sort_unstable();
    for (code, glyph) in sorted {
        for v in [code, code, u32::from(glyph)] {
            be32(&mut cmap, v);
        }
    }

    let mut tables: Vec<(&[u8; 4], Vec<u8>)> = vec![(b"cmap", cmap), (b"glyf", glyf), (b"head", head), (b"hhea", hhea), (b"hmtx", hmtx), (b"loca", loca), (b"maxp", maxp)];
    if !kern.is_empty() {
        let mut k = Vec::new();
        be16(&mut k, 0);
        be16(&mut k, 1);
        be16(&mut k, 0);
        be16(&mut k, (14 + 6 * kern.len()) as u16);
        be16(&mut k, 1); // horizontal, format 0
        be16(&mut k, kern.len() as u16);
        k.extend_from_slice(&[0; 6]);
        let mut pairs = kern.to_vec();
        pairs.sort_unstable();
        for (l, r, v) in pairs {
            be16(&mut k, l);
            be16(&mut k, r);
            be16(&mut k, v as u16);
        }
        tables.push((b"kern", k));
    }
    tables.sort_by_key(|t| *t.0);
    let mut out = Vec::new();
    be32(&mut out, 0x0001_0000);
    be16(&mut out, tables.len() as u16);
    out.extend_from_slice(&[0; 6]);
    let mut at = 12 + 16 * tables.len();
    let mut body = Vec::new();
    for (tag, data) in &tables {
        out.extend_from_slice(*tag);
        be32(&mut out, 0);
        be32(&mut out, at as u32);
        be32(&mut out, data.len() as u32);
        body.extend_from_slice(data);
        body.resize(body.len().next_multiple_of(4), 0);
        at = 12 + 16 * tables.len() + body.len();
    }
    out.extend_from_slice(&body);
    out
}

/// .notdef, a square "A" (100-500 x 0-700), a triangle "V" and a space; "A" before "V" kerns by -80.
pub(crate) fn plain_font() -> Vec<u8> {
    let glyphs = [
        TestGlyph { advance: 500, contours: vec![] },
        TestGlyph { advance: 600, contours: vec![vec![(100, 0), (100, 700), (500, 700), (500, 0)]] },
        TestGlyph { advance: 600, contours: vec![vec![(50, 700), (550, 700), (300, 0)]] },
        TestGlyph { advance: 250, contours: vec![] },
    ];
    test_font(&glyphs, &[(0x41, 1), (0x56, 2), (0x20, 3)], &[(1, 2, -80)])
}

fn cook_with(json: &str, font: Vec<u8>) -> Result<(Made, FontFacts)> {
    let file = move |name: &str| (name == "Title.ttf").then(|| font.clone());
    cook(json.as_bytes(), "Luma/my.mod/fonts/Title.font.json", &file)
}

/// The NTBL of a table given as JSON.
fn table(json: Value) -> Vec<u8> {
    crate::cook::table::TableIr::from_json(json.to_string().as_bytes()).unwrap().to_ntbl()
}

#[test]
fn a_fonts_glyphs_become_the_games_font_form() {
    let (files, facts) = cook_with(r#"{"file":"Title.ttf","size":40}"#, plain_font()).unwrap();
    let names: Vec<(&str, bool)> = files.iter().map(|(p, _, done)| (p.as_str(), *done)).collect();
    assert_eq!(names, [("Luma/my.mod/fonts/Title.brfnt", false), ("Luma/my.mod/fonts/Title.kern", true), ("Luma/my.mod/fonts/Title.fallback", true)]);
    assert_eq!(facts, FontFacts { glyphs: 3, sheets: 1, kern: 1, size: 40, missing: 0, fallback: "MessageFont26".into() });

    // The store cooks it as it cooks the game's fonts: distance sheets, then NFNT.
    let brfnt = &files[0].1;
    let mut stats = crate::cook::Stats::default();
    let textured = crate::cook::formats::font(brfnt, "Title", &mut stats).unwrap();
    let n = crate::cook::native_data::font_native(&textured).unwrap();
    assert_eq!(&n[0..4], b"NFNT");
    // 40 pixels an em: ascent 32, descent 8, line 40; as wide as high; a distance font.
    assert_eq!((n[0xD], n[0x14], n[0x15], n[0x16], n[0x17]), (40, 40, 40, 32, crate::cook::distance::RANGE as u8));
    // Cells: the widest ink (the triangle, 20 px) and the margin; the tallest (28 px) above the baseline and none below.
    assert_eq!((n[0x18], n[0x19], n[0x1A]), (24, 32, 30));
    assert_eq!(u32::from_le_bytes(n[0x2C..0x30].try_into().unwrap()), 3);

    // Codes in order: space 0, A 1, V 2; "A" sits 4 px right of the cursor less the margin, 16 + 4 wide, 24 on.
    let glyphs_at = u32::from_le_bytes(n[0x38..0x3C].try_into().unwrap()) as usize;
    let a = &n[glyphs_at + 10..glyphs_at + 20];
    assert_eq!((a[6] as i8, a[7], a[8] as i8), (2, 20, 24));
    let space = &n[glyphs_at..glyphs_at + 10];
    assert_eq!((space[6] as i8, space[7], space[8] as i8), (0, 0, 10));

    // The square's middle is ink, the cell's corner is not (read from the BRFNT's I8 sheet).
    let g = 0x10 + 0x20 + 8;
    let (width, image) = (u16::from_be_bytes([brfnt[g + 16], brfnt[g + 17]]) as usize, u32::from_be_bytes(brfnt[g + 20..g + 24].try_into().unwrap()) as usize);
    let pixel = |x: usize, y: usize| brfnt[image + (y / 4) * (width / 8) * 32 + (x / 8) * 32 + (y % 4) * 8 + x % 8];
    let (cell_x, cell_y) = (1 + 25, 1); // the second cell
    assert_eq!(pixel(cell_x + 10, cell_y + 16), 255);
    assert_eq!(pixel(cell_x, cell_y), 0);
    // Its edges fall on pixel edges: 2 px of margin, then full ink.
    assert_eq!((pixel(cell_x + 1, cell_y + 16), pixel(cell_x + 2, cell_y + 16)), (0, 255));

    // -80 units at 40 px an em: -3.2, as whole pixels -3.
    let kern = serde_json::json!({ "columns": { "left": "int", "right": "int", "x": "int" }, "rows": [{ "left": 65, "right": 86, "x": -3 }] });
    assert_eq!(files[1].1, table(kern));
    assert_eq!(files[2].1, b"MessageFont26
");
}

#[test]
fn characters_the_font_lacks_are_noted_and_left_to_the_fallback() {
    // The test takes what the cook notes (refuse::reading).
    let _reading = crate::cook::refuse::reading();
    said_notes();
    let json = r#"{"file":"Title.ttf","glyphs":{"chars":"AЖ","ranges":[["U+0400","U+0401"]]},"fallback":"MenuFont64"}"#;
    let (files, facts) = cook_with(json, plain_font()).unwrap();
    assert_eq!((facts.missing, facts.fallback.as_str()), (3, "MenuFont64"));
    let notes = said_notes();
    assert_eq!(notes.len(), 1);
    assert_eq!(notes[0].0, "Cook.Font.Missing");
    assert!(notes[0].1.contains(&("chars", "U+0400 U+0401 U+0416".into())), "{:?}", notes[0].1);
    assert_eq!(files[2].1, b"MenuFont64
");
}

#[test]
fn every_character_the_font_maps_with_all() {
    let glyphs: Vec<TestGlyph> = (0..4).map(|i| TestGlyph { advance: 500, contours: if i == 0 { vec![] } else { vec![vec![(0, 0), (0, 500), (400, 500)]] } }).collect();
    let font = test_font(&glyphs, &[(0x41, 1), (0x4E00, 2), (0xAC00, 3)], &[]);
    let (_, facts) = cook_with(r#"{"file":"Title.ttf","glyphs":{"all":true}}"#, font.clone()).unwrap();
    assert_eq!(facts.glyphs, 3);
    let (_, facts) = cook_with(r#"{"file":"Title.ttf"}"#, font).unwrap();
    assert_eq!(facts.glyphs, 1);
}

#[test]
fn a_font_too_large_for_the_games_form_says_the_largest_size() {
    // An advance of 1500 units is 144 px at 96: past the 127 a width holds; 84 is the most (126 px).
    let glyphs = [TestGlyph { advance: 500, contours: vec![] }, TestGlyph { advance: 1500, contours: vec![vec![(0, 0), (0, 500), (400, 500)]] }];
    let error = cook_with(r#"{"file":"Title.ttf","size":96}"#, test_font(&glyphs, &[(0x41, 1)], &[])).unwrap_err();
    let (why, args) = said(&error);
    assert_eq!(why, "Cook.Font.TooLarge");
    assert!(args.contains(&("most", "84".into())), "{args:?}");
}

#[test]
fn a_font_source_is_refused_with_where_and_why() {
    let cases = [
        (r#"{"file":"Title.ttf","colour":1}"#, "Cook.Effect.UnknownPart"),
        (r#"{"size":40}"#, "Cook.Effect.Missing"),
        (r#"{"file":"../Title.ttf"}"#, "Cook.Effect.FilePath"),
        (r#"{"file":"Other.ttf"}"#, "Cook.Effect.FileMissing"),
        (r#"{"file":"Title.ttf","size":200}"#, "Cook.Effect.Range"),
        (r#"{"file":"Title.ttf","fallback":"Arial"}"#, "Cook.Effect.Choice"),
        (r#"{"file":"Title.ttf","glyphs":{"ranges":[["U+0041","U+0030"]]}}"#, "Cook.Font.RangeOrder"),
        (r#"{"file":"Title.ttf","glyphs":{"ranges":[["A","Z"]]}}"#, "Cook.Font.Code"),
        (r#"{"file":"Title.ttf","glyphs":{"chars":"😀"}}"#, "Cook.Font.Beyond"),
        (r#"{"file":"Title.ttf","glyphs":{"ranges":[[65536,65537]]}}"#, "Cook.Font.Beyond"),
    ];
    for (json, why) in cases {
        let error = cook_with(json, plain_font()).unwrap_err();
        assert_eq!(said(&error).0, why, "{json}");
    }
    let error = cook_with(r#"{"file":"Title.ttf"}"#, b"not a font".to_vec()).unwrap_err();
    assert_eq!(said(&error).0, "Cook.Font.Unreadable");
    // A font that maps none of the characters asked for, nor Latin.
    let glyphs = [TestGlyph { advance: 500, contours: vec![] }, TestGlyph { advance: 500, contours: vec![vec![(0, 0), (0, 500), (400, 500)]] }];
    let error = cook_with(r#"{"file":"Title.ttf"}"#, test_font(&glyphs, &[(0x4E00, 1)], &[])).unwrap_err();
    assert_eq!(said(&error).0, "Cook.Font.NoGlyph");
}

#[test]
fn a_fonts_files_are_its_assets() {
    assert!(is_asset_source("Luma/my.mod/fonts/Title.TTF") && is_asset_source("/Luma/m/a.otf"));
    assert!(!is_asset_source("LayoutData/Font/a.ttf") && !is_asset_source("Luma/m/a.font.json"));
    assert!(is_font_source("luma/m/a.font.json") && !is_font_source("luma/m/a.json"));
}

#[test]
fn coverage_is_exact_on_pixel_edges_and_halves_a_split_pixel() {
    // A square from x 1 to 2.5, y 0 to 2: pixel 1 full, pixel 2 half.
    let lines = [[1.0, 0.0, 1.0, 2.0], [1.0, 2.0, 2.5, 2.0], [2.5, 2.0, 2.5, 0.0], [2.5, 0.0, 1.0, 0.0]];
    let c = coverage(&lines, 4, 2);
    assert_eq!(&c[0..4], &[0.0, 1.0, 0.5, 0.0]);
}
