//! A game layout patched by pane name (plan 3.8 layer 5, layout contract 5): `layers/<l>/patches/<archive>/blyt/
//! <Name>.brlyt.json` holding `{"patch": 1, "panes": {"<pane>": {...}}}`. The game start applies every mod's patch of
//! one layout in the mods' order (overlay_patches) to the layout as the store holds it (the cooked NLYT, ui_layout),
//! so two mods that change different panes of one screen both take effect and a later mod's value for one field wins.
//! Only the named panes' named fields change; every other byte stays.
//!
//! A pane takes: `position` [x, y], `size` [w, h], `scale` (a number or [x, y]), `rotation` (degrees about the
//! screen's axis, or [x, y, z]), `alpha` 0-255, `visible`; a text pane `text` (no longer than the game's pane holds:
//! its buffer) and `colour` / `colours` [top, bottom]; a picture `colour` or `corners`.

use super::particle_json::{as_f32, as_int, at_index, at_key, floats_of, length, list, missing, not_kind, object, only, parse_rgba, range, text};
use super::refuse::refuse;
use super::ui_layout::{KIND_PICTURE, KIND_TEXT_BOX, LAYOUT_MAGIC};
use anyhow::Result;
use serde_json::{Map, Value};

const PARTS: [&str; 6] = ["position", "size", "scale", "rotation", "alpha", "visible"];
const TEXT_PARTS: [&str; 3] = ["text", "colour", "colours"];
const PICTURE_PARTS: [&str; 2] = ["colour", "corners"];
/// The cooked layout's pane record (ui_layout::write_layout).
const PANE: usize = 72;

fn le16(b: &[u8], at: usize) -> Option<usize> {
    Some(usize::from(u16::from_le_bytes(b.get(at..at + 2)?.try_into().ok()?)))
}

fn le32(b: &[u8], at: usize) -> Option<usize> {
    Some(u32::from_le_bytes(b.get(at..at + 4)?.try_into().ok()?) as usize)
}

fn put_f32s(b: &mut [u8], at: usize, v: &[f32]) {
    for (i, x) in v.iter().enumerate() {
        b[at + 4 * i..at + 4 * i + 4].copy_from_slice(&x.to_bits().to_le_bytes());
    }
}

/// The cooked layout's panes: (record offset, kind, name).
fn panes(file: &[u8]) -> Option<Vec<(usize, u8, String)>> {
    if le32(file, 0)? != LAYOUT_MAGIC as usize {
        return None;
    }
    let (count, at, pool) = (le32(file, 20)?, le32(file, 24)?, le32(file, 60)?);
    let mut out = Vec::with_capacity(count);
    for i in 0..count {
        let r = at.checked_add(PANE.checked_mul(i)?)?;
        let record = file.get(r..r.checked_add(PANE)?)?;
        let name_at = pool.checked_add(le32(record, 16)?)?;
        let name = file.get(name_at..)?;
        let end = name.iter().position(|b| *b == 0)?;
        out.push((r, record[10], String::from_utf8_lossy(&name[..end]).into_owned()));
    }
    Some(out)
}

/// The cooked layout `file` with the patch `source` applied.
pub fn patch(file: &[u8], source: &[u8]) -> Result<Vec<u8>> {
    let root: Value = serde_json::from_slice(source).map_err(|e| refuse("Cook.Model.Json", &[("line", e.line().to_string()), ("column", e.column().to_string())]))?;
    let map = object(&root, "/")?;
    only(map, "/", &["patch", "panes"])?;
    as_int(map.get("patch").ok_or_else(|| missing("/", "patch"))?, "/patch", 1, 1)?;
    let found = panes(file).ok_or_else(|| refuse("Cook.Layout.NotLayout", &[]))?;
    let mut out = file.to_vec();
    for (name, change) in object(map.get("panes").ok_or_else(|| missing("/", "panes"))?, "/panes")? {
        let at = at_key("/panes", name);
        // Pane names are matched exactly, as the game's FindPaneByName does.
        let Some((record, kind, _)) = found.iter().find(|p| p.2 == *name) else {
            return Err(refuse("Cook.Layout.NoPane", &[("where", at), ("name", name.clone())]));
        };
        let fields = object(change, &at)?;
        let mut parts = PARTS.to_vec();
        match *kind {
            KIND_TEXT_BOX => parts.extend(TEXT_PARTS),
            KIND_PICTURE => parts.extend(PICTURE_PARTS),
            _ => {}
        }
        only(fields, &at, &parts)?;
        apply(&mut out, *record, *kind, fields, &at)?;
    }
    Ok(out)
}

fn apply(file: &mut Vec<u8>, r: usize, kind: u8, fields: &Map<String, Value>, at: &str) -> Result<()> {
    let get = |key: &str| fields.get(key).map(|v| (v, at_key(at, key)));
    if let Some((v, at)) = get("visible") {
        file[r + 13] = u8::from(v.as_bool().ok_or_else(|| not_kind(&at, "Cook.Effect.Is.Bool"))?);
    }
    if let Some((v, at)) = get("alpha") {
        file[r + 12] = as_int(v, &at, 0, 255)? as u8;
    }
    if let Some((v, at)) = get("position") {
        put_f32s(file, r + 28, &floats_of(v, 2, &at)?);
    }
    if let Some((v, at)) = get("rotation") {
        let rotation = if v.is_number() { vec![0.0, 0.0, as_f32(v, &at)?] } else { floats_of(v, 3, &at)? };
        put_f32s(file, r + 40, &rotation);
    }
    if let Some((v, at)) = get("scale") {
        let scale = if v.is_number() { vec![as_f32(v, &at)?; 2] } else { floats_of(v, 2, &at)? };
        put_f32s(file, r + 52, &scale);
    }
    if let Some((v, at)) = get("size") {
        let size = floats_of(v, 2, &at)?;
        if size[0] < 0.0 || size[1] < 0.0 {
            return Err(range(&at, format!("{} {}", size[0], size[1]), 0, "3.4e38"));
        }
        put_f32s(file, r + 60, &size);
    }
    let bad = || refuse("Cook.Layout.NotLayout", &[]);
    let payload = le32(file, r + 68).ok_or_else(bad)?;
    let colours = |file: &mut Vec<u8>, at_colours: usize, key: &str, count: usize| -> Result<()> {
        let (one, each) = (get("colour"), get(key));
        let values: Vec<[u8; 4]> = match (one, each) {
            (Some(_), Some(_)) => return Err(refuse("Cook.Layout.Both", &[("where", at.to_string()), ("key", "colour".into()), ("other", key.to_string())])),
            (Some((v, at)), None) => vec![parse_rgba(v, &at)?; count],
            (None, Some((v, at))) => {
                let items = list(v, &at)?;
                if items.len() != count {
                    return Err(length(&at, count, items.len()));
                }
                items.iter().enumerate().map(|(i, c)| parse_rgba(c, &at_index(&at, i))).collect::<Result<_>>()?
            }
            (None, None) => return Ok(()),
        };
        for (i, c) in values.iter().enumerate() {
            file.get_mut(at_colours + 4 * i..at_colours + 4 * i + 4).ok_or_else(bad)?.copy_from_slice(c);
        }
        Ok(())
    };
    match kind {
        KIND_TEXT_BOX => {
            if let Some((v, at)) = get("text") {
                let words = text(v, &at)?;
                if words.chars().any(|c| (c as u32) < 0x20 && c != '\n') {
                    return Err(refuse("Cook.Layout.Control", &[("where", at)]));
                }
                let mut units: Vec<u16> = words.encode_utf16().collect();
                units.push(0);
                // The game's pane holds `buffer` bytes with the terminating 0: the text cannot grow past it.
                let buffer = le16(file, payload).ok_or_else(bad)?;
                if 2 * units.len() > buffer {
                    return Err(refuse("Cook.Layout.TextLong", &[("where", at), ("count", (units.len() - 1).to_string()), ("max", (buffer / 2).saturating_sub(1).to_string())]));
                }
                // The cooked file keeps only the text it had: the new one goes at the end, the record pointing at it.
                while file.len() % 4 != 0 {
                    file.push(0);
                }
                let text_at = file.len();
                for u in &units {
                    file.extend_from_slice(&u.to_le_bytes());
                }
                file[payload + 2..payload + 4].copy_from_slice(&((2 * units.len()) as u16).to_le_bytes());
                file[payload + 12..payload + 16].copy_from_slice(&(text_at as u32).to_le_bytes());
                file[payload + 40..payload + 42].copy_from_slice(&(units.len() as u16).to_le_bytes());
            }
            colours(file, payload + 16, "colours", 2)?;
        }
        KIND_PICTURE => colours(file, payload, "corners", 4)?,
        _ => {}
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn why(e: anyhow::Error) -> &'static str {
        match crate::cook::refuse::refusal_of(&e).expect("a refusal").args[0].1 {
            luma_validate::Arg::Phrase(p) => p,
            _ => panic!("no phrase"),
        }
    }

    /// A game layout as the store holds it.
    fn game() -> Vec<u8> {
        let source = br#"{"panes": [
            {"name": "Count", "type": "text", "size": [200, 40], "text": "x 0", "length": 8},
            {"name": "Icon", "type": "picture", "size": [32, 32], "position": [10, 10]}
        ]}"#;
        let brlyt = crate::cook::layout_json::to_brlyt(source, &|_| None).unwrap().0;
        crate::cook::ui_layout::write_layout(&crate::cook::ui_layout::read_layout(&brlyt).unwrap()).unwrap()
    }

    fn record<'a>(file: &'a [u8], name: &str) -> (&'a [u8], usize) {
        let (r, _, _) = panes(file).unwrap().into_iter().find(|p| p.2 == name).unwrap();
        (&file[r..r + PANE], le32(file, r + 68).unwrap())
    }

    fn f32s(b: &[u8], at: usize, n: usize) -> Vec<f32> {
        (0..n).map(|i| f32::from_le_bytes(b[at + 4 * i..at + 4 * i + 4].try_into().unwrap())).collect()
    }

    #[test]
    fn two_patches_change_their_own_panes_and_keep_the_rest() {
        let one = patch(&game(), br#"{"patch": 1, "panes": {"Count": {"text": "x 99", "position": [5, 6]}}}"#).unwrap();
        let both = patch(&one, br#"{"patch": 1, "panes": {"Icon": {"visible": false, "colour": "ff0000ff"}}}"#).unwrap();
        let (count, text) = record(&both, "Count");
        assert_eq!(f32s(count, 28, 2), vec![5.0, 6.0]);
        let at = le32(&both, text + 12).unwrap();
        let units: Vec<u16> = (0..4).map(|i| u16::from_le_bytes([both[at + 2 * i], both[at + 2 * i + 1]])).collect();
        assert_eq!(String::from_utf16_lossy(&units), "x 99");
        let (icon, picture) = record(&both, "Icon");
        assert_eq!((icon[13], f32s(icon, 28, 2)), (0, vec![10.0, 10.0]));
        assert_eq!(&both[picture..picture + 4], &[255, 0, 0, 255]);
    }

    #[test]
    fn a_pane_the_game_lacks_and_text_past_its_buffer_are_refused() {
        let refusal = |s: &[u8]| why(patch(&game(), s).err().unwrap());
        assert_eq!(refusal(br#"{"patch": 1, "panes": {"Nope": {"alpha": 0}}}"#), "Cook.Layout.NoPane");
        assert_eq!(refusal(br#"{"patch": 1, "panes": {"Count": {"text": "far too long"}}}"#), "Cook.Layout.TextLong");
        assert_eq!(refusal(br#"{"patch": 1, "panes": {"Icon": {"text": "x"}}}"#), "Cook.Effect.UnknownPart");
        assert_eq!(why(patch(b"RLYT", br#"{"patch": 1, "panes": {}}"#).err().unwrap()), "Cook.Layout.NotLayout");
    }
}
