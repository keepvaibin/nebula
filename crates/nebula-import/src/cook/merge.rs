//! Files many mods add to at once: each mod brings only what it adds, in a layer's `merge/` folder, and the cooked file
//! gets all of it. Works on cooked files, as the game's own store holds them.
//!
//! - `<table>.json`: rows for a table: `{ "rows": [ { "column": value, ... } ] }`. A column left out is zero or "".
//!   Tables merge in cook/table.rs (TableIr).
//! - `Particles.jpc`, with `ParticleNames.bcsv.json` beside it: new particles. Each resource's user index in the file
//!   is its place in that list of names.

use anyhow::Result;

/// A cooked particle file with `extra`'s particles after its own, their user indices from `first_index`, their
/// textures after its own (super::particles, on their records).
pub fn add_particles(base: &[u8], extra: &[u8], first_index: u16) -> Result<Vec<u8>> {
    let base = super::particles::read_nptc(base)?;
    let extra = super::particles::read_nptc(extra)?;
    let merged = super::particles::merge(&base, &extra, u32::from(first_index))?;
    super::pipeline_list::add_particles(&merged);
    Ok(super::particles::write(&merged))
}
