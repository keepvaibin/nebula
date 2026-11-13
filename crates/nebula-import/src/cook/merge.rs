//! Files many mods add to at once: each mod brings only what it adds, in a layer's `merge/` folder, and the cooked file
//! gets all of it. Works on cooked files, as the game's own store holds them.
//!
//! - `<table>.json`: rows for a table: `{ "rows": [ { "column": value, ... } ] }`. A column left out is zero or "".
//!   Tables merge in cook/table.rs (TableIr).
//! - `Particles.jpc`, with `ParticleNames.bcsv.json` beside it: new particles. Each resource's user index in the file
//!   is its place in that list of names.

use anyhow::{Context, Result};

/// A table of text rows (ray tracing's NebulaData/RtStages.tsv and RtEffects.tsv: tab-separated, a header row, `#` a
/// comment) with `extra`'s rows after its own, placed in its columns by their names: a column the game's table lacks
/// is refused; one `extra` leaves out is empty. The host reads a later row of the same name over an earlier one.
pub fn add_rows(base: &[u8], extra: &[u8]) -> Result<Vec<u8>> {
    let base = std::str::from_utf8(base).context("the game's table is not text")?;
    let extra = std::str::from_utf8(extra).context("the table is not UTF-8 text")?;
    let rows = |text: &str| -> Vec<String> {
        text.lines()
            .map(|l| l.trim_end_matches('\r').to_string())
            .filter(|l| !l.trim().is_empty() && !l.starts_with('#'))
            .collect()
    };
    let name = |column: &str| column.trim().to_ascii_lowercase().replace('_', " ");
    let (base_rows, extra_rows) = (rows(base), rows(extra));
    let head: Vec<String> = base_rows.first().context("the game's table has no header row")?.split('\t').map(name).collect();
    let Some((mine, added)) = extra_rows.split_first() else { return Ok(base.as_bytes().to_vec()) };
    let mut place = Vec::new();
    for column in mine.split('\t').map(name) {
        let at = head.iter().position(|h| *h == column).with_context(|| format!("the column {column:?} is not one of the game's ({})", head.join(", ")))?;
        place.push(at);
    }
    let mut out = base.trim_end_matches(|c: char| c == '\r' || c == '\n').to_string();
    out.push('\n');
    for row in added {
        let mut fields = vec![String::new(); head.len()];
        for (value, &at) in row.split('\t').zip(&place) {
            fields[at] = value.trim().to_string();
        }
        out.push_str(&fields.join("\t"));
        out.push('\n');
    }
    Ok(out.into_bytes())
}

/// A cooked particle file with `extra`'s particles after its own, their user indices from `first_index`, their
/// textures after its own (super::particles, on their records).
pub fn add_particles(base: &[u8], extra: &[u8], first_index: u16) -> Result<Vec<u8>> {
    let base = super::particles::read_nptc(base)?;
    let extra = super::particles::read_nptc(extra)?;
    let merged = super::particles::merge(&base, &extra, u32::from(first_index))?;
    super::pipeline_list::add_particles(&merged);
    Ok(super::particles::write(&merged))
}

#[cfg(test)]
mod tests {
    use super::add_rows;

    #[test]
    fn rows_join_the_games_by_column_name() {
        let game = "# a comment\nstage\tsun\tsun gain\tnote\nA\tkey\t1.0\tx\n";
        let pack = "sun_gain\tstage\r\n2.0\tB\r\n";
        let out = String::from_utf8(add_rows(game.as_bytes(), pack.as_bytes()).unwrap()).unwrap();
        assert_eq!(out, "# a comment\nstage\tsun\tsun gain\tnote\nA\tkey\t1.0\tx\nB\t\t2.0\t\n");
        assert!(add_rows(game.as_bytes(), b"stage\tcolour\nB\tred\n").is_err());
        assert_eq!(add_rows(game.as_bytes(), b"# nothing\n").unwrap(), game.as_bytes());
    }
}
