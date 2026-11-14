//! A clip's frame events (docs/modding/content/animation.md section 2a): `<clip>.anim.json` in a
//! game archive (`ObjectData/Kuribo.arc/Walk.anim.json`) names events at frames of the archive's clip `<clip>`:
//!
//! ```json
//! { "events": [ { "frame": 12, "event": "step_left" }, { "frame": 30, "event": "step_right" } ] }
//! ```
//!
//! The cook writes `<archive>/<clip>.events`, a table (NTBL) of the columns `frame` (int) and `event` (text), its rows
//! sorted by frame (rows of one frame in the file's order). The anim service gives each row to a play of the clip as a
//! mark after the play's own (J, anim 1.1). Refused by name: a file outside a game archive, no events, more than 64,
//! a frame that is not a whole number from 0 to 65,534 or past the game clip's last frame, an event name that is not
//! 1 to 63 of `A-Z a-z 0-9 _ .`, the same event twice at one frame, any other key.

use super::particle_json::{as_int, at_index, at_key, list, missing, object, only, text};
use super::refuse::refuse;
use super::table::TableIr;
use anyhow::Result;
use serde_json::{json, Value};

/// The most rows of one clip (the anim service's limit for a clip's events).
pub const MOST_EVENTS: usize = 64;
/// The longest event name, in bytes.
pub const MOST_NAME: usize = 63;
/// The longest clip name the anim service plays, in bytes (its names are under 128).
pub const MOST_CLIP: usize = 127;
/// The last frame any clip has: clip files count frames in 16 bits (animation.md).
pub const LAST_FRAME: i64 = 65_534;
/// The clip kinds a play starts, in the order the first present leads (animation.md).
pub const CLIP_ENDINGS: [&str; 6] = ["bck", "btk", "brk", "bpk", "btp", "bva"];
const ENDING: &str = ".anim.json";

/// What `cook.events` says.
#[derive(Clone, Debug, PartialEq)]
pub struct EventFacts {
    /// The clip's name, as the file names it.
    pub clip: String,
    pub rows: usize,
    /// The game clip's frame count the frames were checked against; None when the game has no clip of that name.
    pub frames: Option<i32>,
}

/// Whether `path` (any case) is a clip's events file.
pub fn is_events_source(lower: &str) -> bool {
    lower.ends_with(ENDING)
}

/// `path`'s archive folder and clip name, and where the events go: `X.arc/Walk.anim.json` gives
/// (`X.arc`, `Walk`, `X.arc/Walk.events`). Refused outside a game archive.
pub fn place(path: &str) -> Result<(String, String, String)> {
    let (folder, file) = path.rsplit_once('/').unwrap_or(("", path));
    if !folder.to_ascii_lowercase().ends_with(".arc") {
        return Err(refuse("Cook.Events.NotInArchive", &[("file", path.to_string())]));
    }
    let clip = &file[..file.len() - ENDING.len()];
    if clip.is_empty() || clip.len() > MOST_CLIP {
        return Err(refuse("Cook.Events.ClipName", &[("file", path.to_string()), ("max", MOST_CLIP.to_string())]));
    }
    Ok((folder.to_string(), clip.to_string(), format!("{folder}/{clip}.events")))
}

/// The frame count of a clip file as the store keeps it (the cooked form, clip::MAGIC) or as the disc has it.
pub fn clip_frames(bytes: &[u8]) -> Option<i32> {
    if bytes.get(0..4) == Some(&super::clip::MAGIC.to_le_bytes()[..]) {
        return bytes.get(16..20).map(|b| i32::from_le_bytes(b.try_into().unwrap()));
    }
    super::clip::read(bytes).ok().map(|c| c.frames)
}

/// The lead clip of `clip` among an archive's members (name, bytes): the first present of CLIP_ENDINGS, by name
/// without case, as the anim service picks it; its frame count.
pub fn lead_frames(members: &[(String, Vec<u8>)], clip: &str) -> Option<i32> {
    CLIP_ENDINGS.iter().find_map(|ending| {
        let wanted = format!("{clip}.{ending}");
        members.iter().find(|(name, _)| name.eq_ignore_ascii_case(&wanted)).and_then(|(_, bytes)| clip_frames(bytes))
    })
}

fn is_event_name(name: &str) -> bool {
    !name.is_empty() && name.len() <= MOST_NAME && name.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'.')
}

/// The file's bytes as the NTBL `<clip>.events`. `frames`: the game clip's frame count, when the game has the clip;
/// a frame at or past it is refused (a play's frame stays under its count: a loop wraps there, a once clip ends).
pub fn cook(json: &[u8], path: &str, frames: Option<i32>) -> Result<(Vec<u8>, EventFacts)> {
    let (_, clip, _) = place(path)?;
    let root: Value = serde_json::from_slice(json).map_err(|e| refuse("Cook.Model.Json", &[("line", e.line().to_string()), ("column", e.column().to_string())]))?;
    let map = object(&root, "/")?;
    only(map, "/", &["events"])?;
    let events = list(map.get("events").ok_or_else(|| missing("/", "events"))?, "/events")?;
    if events.is_empty() {
        return Err(refuse("Cook.Events.None", &[("file", path.to_string())]));
    }
    if events.len() > MOST_EVENTS {
        return Err(refuse("Cook.Events.TooMany", &[("file", path.to_string()), ("count", events.len().to_string()), ("max", MOST_EVENTS.to_string())]));
    }
    let mut rows: Vec<(i64, String)> = Vec::with_capacity(events.len());
    for (i, event) in events.iter().enumerate() {
        let at = at_index("/events", i);
        let fields = object(event, &at)?;
        only(fields, &at, &["frame", "event"])?;
        let frame_at = at_key(&at, "frame");
        let frame = as_int(fields.get("frame").ok_or_else(|| missing(&at, "frame"))?, &frame_at, 0, LAST_FRAME)?;
        if let Some(count) = frames {
            let last = i64::from(count.max(1)) - 1;
            if frame > last {
                return Err(refuse("Cook.Events.PastEnd", &[("where", frame_at), ("frame", frame.to_string()), ("clip", clip.clone()), ("last", last.to_string())]));
            }
        }
        let name_at = at_key(&at, "event");
        let name = text(fields.get("event").ok_or_else(|| missing(&at, "event"))?, &name_at)?;
        if !is_event_name(name) {
            return Err(refuse("Cook.Events.Name", &[("where", name_at), ("name", name.to_string()), ("max", MOST_NAME.to_string())]));
        }
        if rows.iter().any(|(f, n)| *f == frame && n == name) {
            return Err(refuse("Cook.Events.Twice", &[("where", at), ("event", name.to_string()), ("frame", frame.to_string())]));
        }
        rows.push((frame, name.to_string()));
    }
    // Sorted by frame; rows of one frame keep the file's order (a stable sort).
    rows.sort_by_key(|(frame, _)| *frame);
    let table = json!({
        "columns": { "frame": "int", "event": "string" },
        "rows": rows.iter().map(|(frame, event)| json!({ "frame": frame, "event": event })).collect::<Vec<_>>(),
    });
    let ir = TableIr::from_json(table.to_string().as_bytes())?;
    Ok((ir.to_ntbl(), EventFacts { clip, rows: rows.len(), frames }))
}

#[cfg(test)]
mod tests {
    use super::super::refuse::said;
    use super::super::table::{column_hash, Cell, Kind, TableIr};
    use super::*;

    const PATH: &str = "ObjectData/Kuribo.arc/Walk.anim.json";

    fn cooked(text: &str, frames: Option<i32>) -> (TableIr, EventFacts) {
        let (ntbl, facts) = cook(text.as_bytes(), PATH, frames).unwrap();
        (TableIr::from_ntbl(&ntbl).unwrap(), facts)
    }

    fn column(table: &TableIr, name: &str) -> usize {
        table.columns.iter().position(|c| c.hash == column_hash(name)).unwrap_or_else(|| panic!("no column {name}"))
    }

    #[test]
    fn events_become_a_table_sorted_by_frame() {
        let text = r#"{"events": [{"frame": 30, "event": "step_right"}, {"frame": 12, "event": "step_left"}, {"frame": 12, "event": "dust.small"}]}"#;
        let (table, facts) = cooked(text, Some(40));
        assert_eq!(facts, EventFacts { clip: "Walk".into(), rows: 3, frames: Some(40) });
        let (f, e) = (column(&table, "frame"), column(&table, "event"));
        assert_eq!(table.columns[f].kind, Kind::Int);
        assert_eq!(table.columns[e].kind, Kind::Text);
        let rows: Vec<(u32, Vec<u8>)> = table
            .rows
            .iter()
            .map(|row| match (&row[f], &row[e]) {
                (Cell::Bits(frame), Cell::Text(name)) => (*frame, name.clone()),
                other => panic!("{other:?}"),
            })
            .collect();
        assert_eq!(rows, vec![(12, b"step_left".to_vec()), (12, b"dust.small".to_vec()), (30, b"step_right".to_vec())]);
    }

    #[test]
    fn the_place_is_beside_the_clip_in_its_archive() {
        assert_eq!(place(PATH).unwrap(), ("ObjectData/Kuribo.arc".into(), "Walk".into(), "ObjectData/Kuribo.arc/Walk.events".into()));
        assert_eq!(said(&place("ObjectData/Walk.anim.json").unwrap_err()).0, "Cook.Events.NotInArchive");
        assert_eq!(said(&place("ObjectData/Kuribo.arc/.anim.json").unwrap_err()).0, "Cook.Events.ClipName");
    }

    #[test]
    fn a_frame_on_the_last_is_kept_and_past_it_refused() {
        let at = |frame: i64| format!(r#"{{"events": [{{"frame": {frame}, "event": "a"}}]}}"#);
        assert_eq!(cooked(&at(39), Some(40)).1.rows, 1);
        let error = cook(at(40).as_bytes(), PATH, Some(40)).unwrap_err();
        assert_eq!(
            said(&error),
            ("Cook.Events.PastEnd", vec![("where", "/events/0/frame".into()), ("frame", "40".into()), ("clip", "Walk".into()), ("last", "39".into())])
        );
        // A clip the game lacks: only the 16-bit bound.
        assert_eq!(cooked(&at(65_534), None).1.frames, None);
        assert_eq!(said(&cook(at(65_535).as_bytes(), PATH, None).unwrap_err()).0, "Cook.Effect.Range");
        assert_eq!(said(&cook(at(-1).as_bytes(), PATH, None).unwrap_err()).0, "Cook.Effect.Range");
        assert_eq!(said(&cook(br#"{"events": [{"frame": 2.5, "event": "a"}]}"#, PATH, None).unwrap_err()).0, "Cook.Effect.Kind");
    }

    #[test]
    fn refusals_name_what_is_wrong() {
        let many = format!(r#"{{"events": [{}]}}"#, (0..65).map(|i| format!(r#"{{"frame": {i}, "event": "e"}}"#)).collect::<Vec<_>>().join(","));
        let sixty_four = format!(r#"{{"events": [{}]}}"#, (0..64).map(|i| format!(r#"{{"frame": {i}, "event": "e"}}"#)).collect::<Vec<_>>().join(","));
        assert_eq!(cooked(&sixty_four, None).1.rows, 64);
        for (text, why) in [
            (many.as_str(), "Cook.Events.TooMany"),
            (r#"{"events": []}"#, "Cook.Events.None"),
            (r#"{}"#, "Cook.Effect.Missing"),
            (r#"{"events": [], "loop": true}"#, "Cook.Effect.UnknownPart"),
            (r#"{"events": [{"frame": 1}]}"#, "Cook.Effect.Missing"),
            (r#"{"events": [{"frame": 1, "event": "a", "sound": "x"}]}"#, "Cook.Effect.UnknownPart"),
            (r#"{"events": [{"frame": 1, "event": ""}]}"#, "Cook.Events.Name"),
            (r#"{"events": [{"frame": 1, "event": "step left"}]}"#, "Cook.Events.Name"),
            (r#"{"events": [{"frame": 1, "event": "über"}]}"#, "Cook.Events.Name"),
            (r#"{"events": [{"frame": 1, "event": 3}]}"#, "Cook.Effect.Kind"),
            (r#"{"events": [{"frame": 1, "event": "a"}, {"frame": 1, "event": "a"}]}"#, "Cook.Events.Twice"),
            (r#"{"events": "#, "Cook.Model.Json"),
        ] {
            assert_eq!(said(&cook(text.as_bytes(), PATH, None).unwrap_err()).0, why, "{text}");
        }
        let long = "a".repeat(MOST_NAME);
        assert_eq!(cooked(&format!(r#"{{"events": [{{"frame": 0, "event": "{long}"}}]}}"#), None).1.rows, 1);
        let longer = "a".repeat(MOST_NAME + 1);
        let error = cook(format!(r#"{{"events": [{{"frame": 0, "event": "{longer}"}}]}}"#).as_bytes(), PATH, None).unwrap_err();
        assert_eq!(said(&error), ("Cook.Events.Name", vec![("where", "/events/0/event".into()), ("name", longer.clone()), ("max", "63".into())]));
        let error = cook(br#"{"events": [{"frame": 3, "event": "a"}, {"frame": 3, "event": "a"}]}"#, PATH, None).unwrap_err();
        assert_eq!(said(&error), ("Cook.Events.Twice", vec![("where", "/events/1".into()), ("event", "a".into()), ("frame", "3".into())]));
        let error = cook(many.as_bytes(), PATH, None).unwrap_err();
        assert_eq!(said(&error), ("Cook.Events.TooMany", vec![("file", PATH.into()), ("count", "65".into()), ("max", "64".into())]));
    }

    #[test]
    fn the_lead_clip_gives_the_frames() {
        let cooked_clip = |frames: i32| {
            let mut bytes = super::super::clip::MAGIC.to_le_bytes().to_vec();
            bytes.extend_from_slice(&[0u8; 12]);
            bytes.extend_from_slice(&frames.to_le_bytes());
            bytes
        };
        let members = vec![("walk.btk".to_string(), cooked_clip(20)), ("Walk.bck".to_string(), cooked_clip(40)), ("Run.bck".to_string(), cooked_clip(9))];
        assert_eq!(lead_frames(&members, "Walk"), Some(40));
        assert_eq!(lead_frames(&members[..1], "Walk"), Some(20));
        assert_eq!(lead_frames(&members, "Jump"), None);
    }
}
