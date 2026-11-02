//! Dumps cook-time keys and IDs for the independent material identity check.
use anyhow::{bail, Context, Result};
use nebula_import::cook::material::{program_key, shader_key, ShaderTable, PROGRAM_BYTES};
use std::{env, fs};

fn main() -> Result<()> {
    let mut args = env::args().skip(1);
    let table = ShaderTable::parse(&fs::read_to_string(
        args.next().context("shader table path required")?,
    )?)?;
    for path in args {
        let bytes = fs::read(&path)?;
        if bytes.len() % PROGRAM_BYTES != 0 {
            bail!("partial material program in {path}");
        }
        for p in bytes.chunks_exact(PROGRAM_BYTES) {
            let semantic: String = shader_key(p)?.iter().map(|b| format!("{b:02x}")).collect();
            println!(
                "{:016x} {} {}",
                program_key(p)?,
                table.find(p)?.unwrap_or(0),
                semantic
            );
        }
    }
    Ok(())
}
