//! A mod layout's animations (docs/modding/content/screens.md section 2b): `animations` in `<Name>.layout.json`, each a
//! named set of tracks on its panes, written as the game's own layout animation file
//! `<archive>/anim/<Name>_<animation>.brlan` (pai1: pane transforms RLPA, vertex colours and alpha RLVC, visibility
//! RLVI), which the store cooks as the game's (ui_layout::read_animation) and `ui.play` plays by name.
//!
//! Keys are `[frame, value]` or `[frame, value, slope]`. Without a slope a key's is the Catmull-Rom one (the line
//! through its neighbours), 0 at the first and last key, so a curve passes through every key smoothly and rests at
//! its ends; visibility keys are steps.

use super::particle_json::{as_f32, as_int, at_index, at_key, choice, list, missing, not_kind, object, only, parse_rgba, range, text};
use super::refuse::refuse;
use anyhow::Result;
use serde_json::Value;

/// What a track moves: its name in the file, the animation kind and the target in it.
const WHATS: [(&str, &[u8; 4], u8); 12] = [
    ("position_x", b"RLPA", 0),
    ("position_y", b"RLPA", 1),
    ("position_z", b"RLPA", 2),
    ("rotation_x", b"RLPA", 3),
    ("rotation_y", b"RLPA", 4),
    ("rotation_z", b"RLPA", 5),
    ("scale_x", b"RLPA", 6),
    ("scale_y", b"RLPA", 7),
    ("width", b"RLPA", 8),
    ("height", b"RLPA", 9),
    ("alpha", b"RLVC", 0x10),
    ("visible", b"RLVI", 0),
];

/// `colour` keys a colour ("rrggbbaa") for all four corners: the sixteen vertex-colour targets of RLVC.
const COLOUR: &str = "colour";

/// The most frames an animation file counts (16 bits).
pub const FRAMES_MAX: i64 = 65_535;

/// One curve: its kind, target and keys (frame, value, slope); a step curve's value is 0 or 1 (or a byte).
struct Curve {
    kind: [u8; 4],
    target: u8,
    keys: Vec<[f32; 3]>,
}

/// An animation read from the layout file: its name, frames, loop and per pane the curves.
pub struct Animation {
    pub name: String,
    frames: u16,
    looped: bool,
    panes: Vec<(String, Vec<Curve>)>,
}

/// Each key's slope where none was given: Catmull-Rom between neighbours, 0 at the ends.
fn smooth(keys: &mut [[f32; 3]], given: &[bool]) {
    let n = keys.len();
    for i in 0..n {
        if given[i] || i == 0 || i + 1 == n {
            continue;
        }
        let (a, b) = (keys[i - 1], keys[i + 1]);
        let span = b[0] - a[0];
        keys[i][2] = if span > 0.0 { (b[1] - a[1]) / span } else { 0.0 };
    }
}

/// The `animations` list of a layout file: `panes` are the layout's pane names (any case) for the tracks to name.
pub fn read(v: &Value, panes: &[String]) -> Result<Vec<Animation>> {
    let mut out: Vec<Animation> = Vec::new();
    for (i, item) in list(v, "/animations")?.iter().enumerate() {
        let at = at_index("/animations", i);
        let map = object(item, &at)?;
        only(map, &at, &["name", "frames", "loop", "tracks"])?;
        let name_at = at_key(&at, "name");
        let name = text(map.get("name").ok_or_else(|| missing(&at, "name"))?, &name_at)?.to_string();
        if !super::layout_json::name_ok(&name) {
            return Err(refuse("Cook.Layout.Name", &[("where", name_at), ("name", name)]));
        }
        if out.iter().any(|a| a.name.eq_ignore_ascii_case(&name)) {
            return Err(refuse("Cook.Layout.Twice", &[("where", name_at), ("name", name)]));
        }
        let frames = as_int(map.get("frames").ok_or_else(|| missing(&at, "frames"))?, &at_key(&at, "frames"), 1, FRAMES_MAX)? as u16;
        let looped = match map.get("loop") {
            Some(v) => v.as_bool().ok_or_else(|| not_kind(&at_key(&at, "loop"), "Cook.Effect.Is.Bool"))?,
            None => false,
        };
        let mut by_pane: Vec<(String, Vec<Curve>)> = Vec::new();
        let tracks_at = at_key(&at, "tracks");
        for (t, track) in list(map.get("tracks").ok_or_else(|| missing(&at, "tracks"))?, &tracks_at)?.iter().enumerate() {
            let at = at_index(&tracks_at, t);
            let map = object(track, &at)?;
            only(map, &at, &["pane", "what", "keys"])?;
            let pane_at = at_key(&at, "pane");
            let given = text(map.get("pane").ok_or_else(|| missing(&at, "pane"))?, &pane_at)?;
            let Some(pane) = panes.iter().find(|p| p.eq_ignore_ascii_case(given)) else {
                return Err(refuse("Cook.Layout.NoPane", &[("where", pane_at), ("name", given.to_string())]));
            };
            let mut names: Vec<&str> = WHATS.iter().map(|w| w.0).collect();
            names.push(COLOUR);
            let what_at = at_key(&at, "what");
            let what = choice(&names, map.get("what").ok_or_else(|| missing(&at, "what"))?, &what_at, names.len() as i64 - 1)? as usize;
            let keys_at = at_key(&at, "keys");
            let items = list(map.get("keys").ok_or_else(|| missing(&at, "keys"))?, &keys_at)?;
            if items.is_empty() {
                return Err(missing(&at, "keys"));
            }
            let mut keys: Vec<(f32, Value, Option<f32>)> = Vec::with_capacity(items.len());
            for (k, key) in items.iter().enumerate() {
                let at = at_index(&keys_at, k);
                let parts = list(key, &at)?;
                if parts.len() != 2 && parts.len() != 3 {
                    return Err(super::particle_json::length(&at, 2, parts.len()));
                }
                let frame = as_f32(&parts[0], &at_index(&at, 0))?;
                if !(0.0..=f32::from(frames)).contains(&frame) || keys.last().is_some_and(|(f, _, _)| frame < *f) {
                    return Err(range(&at_index(&at, 0), frame.to_string(), 0, &frames.to_string()));
                }
                let slope = parts.get(2).map(|s| as_f32(s, &at_index(&at, 2))).transpose()?;
                keys.push((frame, parts[1].clone(), slope));
            }
            let given: Vec<bool> = keys.iter().map(|k| k.2.is_some()).collect();
            let mut curves = Vec::new();
            if what == WHATS.len() {
                // A colour per key: the same frames on each corner's red, green, blue and alpha.
                let colours: Vec<[u8; 4]> = keys.iter().enumerate().map(|(k, (_, v, _))| parse_rgba(v, &at_index(&at_index(&keys_at, k), 1))).collect::<Result<_>>()?;
                for corner in 0..4u8 {
                    for channel in 0..4u8 {
                        let mut ks: Vec<[f32; 3]> = keys.iter().zip(&colours).map(|((f, _, s), c)| [*f, f32::from(c[channel as usize]), s.unwrap_or(0.0)]).collect();
                        smooth(&mut ks, &given);
                        curves.push(Curve { kind: *b"RLVC", target: corner * 4 + channel, keys: ks });
                    }
                }
            } else {
                let (_, kind, target) = WHATS[what];
                let step = kind == b"RLVI";
                let mut ks = Vec::with_capacity(keys.len());
                for (k, (frame, value, slope)) in keys.iter().enumerate() {
                    let at = at_index(&at_index(&keys_at, k), 1);
                    let v = if step {
                        f32::from(u8::from(value.as_bool().ok_or_else(|| not_kind(&at, "Cook.Effect.Is.Bool"))?))
                    } else if kind == b"RLVC" {
                        as_int(value, &at, 0, 255)? as f32
                    } else {
                        as_f32(value, &at)?
                    };
                    ks.push([*frame, v, slope.unwrap_or(0.0)]);
                }
                if !step {
                    smooth(&mut ks, &given);
                }
                curves.push(Curve { kind: *kind, target, keys: ks });
            }
            // One curve per pane and target: a second track on it is a mistake, not a merge.
            let entry = match by_pane.iter_mut().position(|(p, _)| p == pane) {
                Some(i) => &mut by_pane[i].1,
                None => {
                    by_pane.push((pane.clone(), Vec::new()));
                    &mut by_pane.last_mut().expect("pushed").1
                }
            };
            for c in curves {
                if entry.iter().any(|e| e.kind == c.kind && e.target == c.target) {
                    return Err(refuse("Cook.Layout.Twice", &[("where", what_at.clone()), ("name", format!("{pane} {}", names[what]))]));
                }
                entry.push(c);
            }
        }
        out.push(Animation { name, frames, looped, panes: by_pane });
    }
    Ok(out)
}

fn be16(out: &mut Vec<u8>, v: u16) {
    out.extend_from_slice(&v.to_be_bytes());
}
fn be32(out: &mut Vec<u8>, v: u32) {
    out.extend_from_slice(&v.to_be_bytes());
}
fn put32(out: &mut [u8], at: usize, v: u32) {
    out[at..at + 4].copy_from_slice(&v.to_be_bytes());
}

/// The animation as the game's layout animation file (RLAN with one pai1 block), as the game's reader takes it.
pub fn to_brlan(a: &Animation) -> Vec<u8> {
    // The pai1 block, offsets from its start: header (20), the contents' offsets, then each content.
    let mut block = Vec::new();
    block.extend_from_slice(b"pai1");
    be32(&mut block, 0);
    be16(&mut block, a.frames);
    block.extend_from_slice(&[u8::from(a.looped), 0]);
    be16(&mut block, 0);
    be16(&mut block, a.panes.len() as u16);
    be32(&mut block, 20);
    let table = block.len();
    block.resize(table + 4 * a.panes.len(), 0);
    for (p, (name, curves)) in a.panes.iter().enumerate() {
        let ca = block.len();
        put32(&mut block, table + 4 * p, ca as u32);
        let mut field = [0u8; 20];
        field[..name.len()].copy_from_slice(name.as_bytes());
        block.extend_from_slice(&field);
        let mut kinds: Vec<[u8; 4]> = Vec::new();
        for c in curves {
            if !kinds.contains(&c.kind) {
                kinds.push(c.kind);
            }
        }
        block.extend_from_slice(&[kinds.len() as u8, 0, 0, 0]);
        let infos = block.len();
        block.resize(infos + 4 * kinds.len(), 0);
        for (k, kind) in kinds.iter().enumerate() {
            let ia = block.len();
            put32(&mut block, infos + 4 * k, (ia - ca) as u32);
            let of_kind: Vec<&Curve> = curves.iter().filter(|c| c.kind == *kind).collect();
            block.extend_from_slice(kind);
            block.extend_from_slice(&[of_kind.len() as u8, 0, 0, 0]);
            let targets = block.len();
            block.resize(targets + 4 * of_kind.len(), 0);
            for (t, c) in of_kind.iter().enumerate() {
                let ta = block.len();
                put32(&mut block, targets + 4 * t, (ta - ia) as u32);
                let step = c.kind == *b"RLVI";
                block.extend_from_slice(&[0, c.target, if step { 1 } else { 2 }, 0]);
                be16(&mut block, c.keys.len() as u16);
                be16(&mut block, 0);
                be32(&mut block, 12);
                for key in &c.keys {
                    block.extend_from_slice(&key[0].to_be_bytes());
                    if step {
                        be16(&mut block, key[1] as u16);
                        be16(&mut block, 0);
                    } else {
                        block.extend_from_slice(&key[1].to_be_bytes());
                        block.extend_from_slice(&key[2].to_be_bytes());
                    }
                }
            }
        }
    }
    block.resize(block.len().div_ceil(4) * 4, 0);
    let size = block.len() as u32;
    put32(&mut block, 4, size);
    let mut out = Vec::with_capacity(16 + block.len());
    out.extend_from_slice(b"RLAN");
    out.extend_from_slice(&[0xFE, 0xFF, 0x00, 0x08]);
    be32(&mut out, 16 + size);
    be16(&mut out, 16);
    be16(&mut out, 1);
    out.extend_from_slice(&block);
    out
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
    use crate::cook::ui_layout::{read_animation, UiKeys};

    fn panes() -> Vec<String> {
        vec!["Icon".into(), "Count".into()]
    }

    #[test]
    fn an_animation_is_the_games_file_and_reads_back() {
        let v: Value = serde_json::json!([{ "name": "In", "frames": 30, "loop": true, "tracks": [
            { "pane": "icon", "what": "position_x", "keys": [[0, -100], [15, -20], [30, 0]] },
            { "pane": "Icon", "what": "visible", "keys": [[0, false], [1, true]] },
            { "pane": "Count", "what": "colour", "keys": [[0, "ff000000"], [30, "ffffffff", 0.5]] }
        ]}]);
        let a = read(&v, &panes()).unwrap();
        let back = read_animation(&to_brlan(&a[0])).unwrap();
        assert_eq!((back.frame_size, back.looped, back.contents.len()), (30, true, 2));
        let icon = &back.contents[0];
        assert_eq!((icon.name.as_str(), icon.infos.len()), ("Icon", 2));
        let UiKeys::Hermite(keys) = &icon.infos[0].targets[0].keys else { panic!("hermite") };
        // The middle key's slope runs through its neighbours; the ends rest.
        assert_eq!(keys[1], [15.0, -20.0, 100.0 / 30.0]);
        assert_eq!((keys[0][2], keys[2][2]), (0.0, 0.0));
        let UiKeys::Step(steps) = &icon.infos[1].targets[0].keys else { panic!("step") };
        assert_eq!(steps, &vec![(0.0, 0), (1.0, 1)]);
        let count = &back.contents[1];
        assert_eq!(count.infos[0].targets.len(), 16);
        let UiKeys::Hermite(red) = &count.infos[0].targets[0].keys else { panic!("hermite") };
        assert_eq!(red[1], [30.0, 255.0, 0.5]);
    }

    #[test]
    fn a_track_on_no_pane_a_key_past_the_end_and_a_track_twice_are_refused() {
        let refusal = |v: Value| why(read(&v, &panes()).err().unwrap());
        assert_eq!(refusal(serde_json::json!([{ "name": "A", "frames": 5, "tracks": [{ "pane": "Nope", "what": "alpha", "keys": [[0, 1]] }] }])), "Cook.Layout.NoPane");
        assert_eq!(refusal(serde_json::json!([{ "name": "A", "frames": 5, "tracks": [{ "pane": "Icon", "what": "alpha", "keys": [[9, 1]] }] }])), "Cook.Effect.Range");
        assert_eq!(
            refusal(serde_json::json!([{ "name": "A", "frames": 5, "tracks": [
                { "pane": "Icon", "what": "alpha", "keys": [[0, 1]] }, { "pane": "Icon", "what": "alpha", "keys": [[0, 2]] }] }])),
            "Cook.Layout.Twice"
        );
    }
}
