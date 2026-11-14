//! The columns of each kind of game table (docs/modding/format/table-keys.md section 10): `data/table-columns.tsv`,
//! compiled in, made from the game's own tables by `tests/store_view_columns.rs`. A kind is a table-keys rule, and for
//! a rule whose last part is `*` (a stage's placement files...) also the file's name. Files of one kind do not all have
//! every column: an empty layer's table often has only a few. The kind says what a table of it may hold, in what form,
//! and the value most of the game's rows have in each column, which a row a mod adds takes for a column it leaves out
//! (Obj_arg0 -1, not 0). Rules that take files of unlike columns by place (`**`, parts animations, cutscene sheets)
//! have no kind.

use super::table::{Column, Kind};
use super::table_keys::{rule_for, Key};
use std::collections::HashMap;
use std::sync::OnceLock;

const TEXT: &str = include_str!("../../data/table-columns.tsv");
pub const KINDS_HEADER: &str = "kind\tcolumn\ttype\twidth\tmask\tusual";

/// A column of a kind, and the value most of the game's rows give it (as `patch::cell_text` writes values).
#[derive(Clone, Debug, PartialEq)]
pub struct KindColumn {
    pub column: Column,
    pub usual: String,
}

/// The kind of a game table, by its store path; None for a table of no kind.
pub fn kind_of(path: &str) -> Option<String> {
    let rule = rule_for(path);
    if rule.key == Key::Position && rule.pattern.contains("**") {
        return None;
    }
    if rule.pattern.rsplit('/').next() == Some("*") {
        let file = path.trim_end_matches('/').rsplit('/').next().unwrap_or("").to_ascii_lowercase();
        Some(format!("{}|{file}", rule.pattern))
    } else {
        Some(rule.pattern.to_string())
    }
}

fn all() -> &'static HashMap<&'static str, Vec<KindColumn>> {
    static KINDS: OnceLock<HashMap<&'static str, Vec<KindColumn>>> = OnceLock::new();
    KINDS.get_or_init(|| {
        let mut kinds: HashMap<&'static str, Vec<KindColumn>> = HashMap::new();
        let mut lines = TEXT.lines();
        // The file is the crate's own; the tests below hold it to this shape.
        assert_eq!(lines.next().map(|l| l.trim_end_matches('\r')), Some(KINDS_HEADER), "table-columns.tsv header");
        for (n, line) in lines.enumerate() {
            let line = line.trim_end_matches('\r');
            if line.is_empty() {
                continue;
            }
            let c: Vec<&'static str> = line.split('\t').collect();
            assert!(c.len() == 6, "table-columns.tsv line {}: six cells", n + 2);
            let hex = |s: &str| u32::from_str_radix(s.trim_start_matches("0x"), 16).unwrap_or_else(|_| panic!("table-columns.tsv line {}: {s}", n + 2));
            let kind = match c[2] {
                "int" => Kind::Int,
                "field" => Kind::Field,
                "float" => Kind::Float,
                "text" => Kind::Text,
                other => panic!("table-columns.tsv line {}: type {other}", n + 2),
            };
            let width = c[3].parse().unwrap_or_else(|_| panic!("table-columns.tsv line {}: width", n + 2));
            kinds.entry(c[0]).or_default().push(KindColumn { column: Column { hash: hex(c[1]), mask: hex(c[4]), kind, width }, usual: c[5].to_string() });
        }
        kinds
    })
}

/// The columns of a table's kind, when it has one.
pub fn columns_of(path: &str) -> Option<&'static [KindColumn]> {
    all().get(kind_of(path)?.as_str()).map(Vec::as_slice)
}

/// An empty table of a kind with every column of its kind (table-keys.md 10): what the applier gives a table no row has
/// filled before it applies patches, and what the validator checks a patch against.
pub fn widen_empty(path: &str, table: &mut super::table::TableIr) {
    if !table.rows.is_empty() {
        return;
    }
    for k in columns_of(path).unwrap_or(&[]) {
        if !table.columns.iter().any(|c| c.hash == k.column.hash) {
            table.columns.push(k.column.clone());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn kinds_by_rule_and_file() {
        assert_eq!(kind_of("StageData/AstroGalaxy/jmp/Placement/Common/ObjInfo").as_deref(), Some("stagedata/*/jmp/placement/*/*|objinfo"));
        assert_eq!(kind_of("StageData/X/jmp/GeneralPos/LayerA/GeneralPosInfo").as_deref(), Some("stagedata/*/jmp/generalpos/*/*|generalposinfo"));
        assert_eq!(kind_of("StageData/X/jmp/Path/CommonPathPointInfo.3").as_deref(), Some("stagedata/*/jmp/path/commonpathpointinfo.*"));
        assert_eq!(kind_of("StageData/AstroGalaxy/ZoneList.bcsv").as_deref(), Some("stagedata/*/zonelist.bcsv"));
        assert_eq!(kind_of("ObjectData/Kuribo/Kuribo.pa"), None);
        assert_eq!(kind_of("Anything/else.bcsv"), None);
    }

    #[test]
    fn the_file_reads_and_each_kind_names_a_column_once() {
        for (kind, columns) in all() {
            let mut seen = std::collections::HashSet::new();
            for c in columns {
                assert!(seen.insert(c.column.hash), "{kind}: 0x{:08x} twice", c.column.hash);
                assert!(c.column.kind != Kind::Text || c.usual.is_empty(), "{kind}");
            }
        }
        let general = columns_of("StageData/PoleUnizoZone/jmp/GeneralPos/Common/GeneralPosInfo").expect("generalpos has a kind");
        for name in ["PosName", "Obj_ID"] {
            assert!(general.iter().any(|c| c.column.hash == super::super::table::column_hash(name)), "{name}");
        }
    }
}
