//! What identifies a row of each game table (docs/modding/format/table-keys.md): `data/table-keys.tsv`, compiled in. A
//! game path (its store path, any case) takes the first pattern that matches; the last pattern takes every path, so no
//! table is without a rule. The patch applier (cook::patch), the validator's view of the game (store_view) and LumaLink
//! read keys only from here.

use std::sync::OnceLock;

const TEXT: &str = include_str!("../../data/table-keys.tsv");

/// How a table's rows are named.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Key {
    /// By these columns' values (names as the game spells them; tables keep their hashes).
    Columns(Vec<String>),
    /// By place: `#` 0 is the game's first row (table-keys.md section 3).
    Position,
}

#[derive(Debug)]
pub struct Rule {
    pub pattern: &'static str,
    pub key: Key,
    /// Keys compare without regard to case (the RT tables, RT-FC-2).
    pub case_insensitive: bool,
    pub why: &'static str,
    parts: Vec<&'static str>,
}

/// Every rule, in the file's order.
pub fn rules() -> &'static [Rule] {
    static RULES: OnceLock<Vec<Rule>> = OnceLock::new();
    RULES.get_or_init(|| {
        TEXT.lines()
            .skip(1)
            .filter(|l| !l.trim().is_empty())
            .enumerate()
            .map(|(n, line)| {
                let cells: Vec<&'static str> = line.split('\t').collect();
                // The file is the crate's own; the test below holds it to this shape.
                assert!(cells.len() == 4, "table-keys.tsv line {}: four cells", n + 2);
                let key = if cells[1] == "#" { Key::Position } else { Key::Columns(cells[1].split(',').map(str::to_string).collect()) };
                Rule { pattern: cells[0], key, case_insensitive: cells[2] == "nocase", why: cells[3], parts: cells[0].split('/').collect() }
            })
            .collect()
    })
}

/// The rule of a game path.
pub fn rule_for(path: &str) -> &'static Rule {
    let lower = path.trim_start_matches('/').to_ascii_lowercase();
    let parts: Vec<&str> = lower.split('/').collect();
    rules().iter().find(|r| matches(&r.parts, &parts)).expect("the last rule takes every path")
}

/// `*` is any part of one folder or file name; `**` is any number of them.
pub(crate) fn matches(pattern: &[&str], path: &[&str]) -> bool {
    match pattern.split_first() {
        None => path.is_empty(),
        Some((&"**", rest)) => (0..=path.len()).any(|skip| matches(rest, &path[skip..])),
        Some((first, rest)) => path.split_first().is_some_and(|(name, more)| name_matches(first.as_bytes(), name.as_bytes()) && matches(rest, more)),
    }
}

fn name_matches(pattern: &[u8], name: &[u8]) -> bool {
    match pattern.split_first() {
        None => name.is_empty(),
        Some((b'*', rest)) => (0..=name.len()).any(|skip| name_matches(rest, &name[skip..])),
        Some((c, rest)) => name.split_first().is_some_and(|(n, more)| n.eq_ignore_ascii_case(c) && name_matches(rest, more)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_file_reads_and_the_last_rule_takes_everything() {
        let rules = rules();
        assert!(rules.len() > 10);
        let last = rules.last().unwrap();
        assert_eq!((last.pattern, &last.key), ("**", &Key::Position));
        for r in rules {
            assert!(!r.why.is_empty() && !r.pattern.is_empty(), "{}", r.pattern);
            if let Key::Columns(c) = &r.key {
                assert!(c.iter().all(|c| !c.is_empty()), "{}", r.pattern);
            }
        }
    }

    #[test]
    fn paths_take_their_rule() {
        let key = |p: &str| rule_for(p).key.clone();
        let cols = |c: &[&str]| Key::Columns(c.iter().map(|s| s.to_string()).collect());
        assert_eq!(key("StageData/AstroGalaxy/jmp/Placement/Common/ObjInfo"), cols(&["l_id"]));
        assert_eq!(key("stagedata/x/jmp/start/layera/startinfo"), cols(&["MarioNo"]));
        assert_eq!(key("StageData/X/jmp/Path/CommonPathPointInfo.3"), Key::Position);
        assert_eq!(key("StageData/X/jmp/Path/CommonPathInfo"), cols(&["l_id"]));
        assert_eq!(key("ObjectData/LightData/LightData.bcsv"), cols(&["AreaLightName"]));
        assert_eq!(key("ObjectData/LightData/LightAstroGalaxy.bcsv"), cols(&["AreaLightName", "LightID"]));
        assert_eq!(key("ObjectData/Kuribo/Kuribo.banmt"), cols(&["name"]));
        assert_eq!(key("ObjectData/DemoX/DemoX/DemoXAction.bcsv"), Key::Position);
        assert_eq!(key("NebulaData/ErrorMessageArchive/UsEnglish/MessageData/System/MessageId.tbl"), cols(&["MessageId"]));
        assert_eq!(key("Anything/else.bcsv"), Key::Position);
        assert_eq!(rule_for("/StageData/A/zonelist.bcsv").pattern, "stagedata/*/zonelist.bcsv");
    }

    #[test]
    fn stars_match_within_one_name_and_double_stars_across() {
        assert!(name_matches(b"light*.bcsv", b"lightastrogalaxy.bcsv"));
        assert!(!name_matches(b"light*.bcsv", b"lightastrogalaxy.bcsvx"));
        assert!(matches(&["a", "**", "*.pa"], &["a", "b", "c", "d.pa"]));
        assert!(matches(&["a", "**", "*.pa"], &["a", "d.pa"]));
        assert!(!matches(&["a", "*", "*.pa"], &["a", "b", "c", "d.pa"]));
    }
}
