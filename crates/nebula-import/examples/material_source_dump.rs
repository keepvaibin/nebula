//! Extract compatibility programs directly from original MAT3 source tables.
//! Usage: material_source_dump <original-game.pak> <programs.bin> <loader-flags-hex>
use anyhow::{bail, Context, Result};
use nebula_import::cook::{
    archive,
    material::{cooked_shader, Mat3},
    pak_files, pak_system_area,
};
use std::{
    env,
    fs::File,
    io::{Read, Seek, SeekFrom, Write},
};

fn scan(
    name: &str,
    bytes: &[u8],
    flags: u32,
    out: &mut File,
    counts: &mut [usize; 3],
) -> Result<()> {
    if archive::is_compressed(bytes) {
        return scan(name, &archive::expand(bytes)?, flags, out, counts);
    }
    if archive::is_archive(bytes) {
        for member in archive::members(bytes)? {
            scan(&format!("{name}/{}", member.path), &member.bytes, flags, out, counts)?;
        }
    } else if bytes.starts_with(b"J3D2") {
        if bytes.len() < 32 {
            bail!("short model header");
        }
        let mut at = 32usize;
        let blocks = u32::from_be_bytes(bytes[12..16].try_into()?) as usize;
        for _ in 0..blocks {
            let header = bytes
                .get(at..at + 8)
                .context("model block header outside source")?;
            let size = u32::from_be_bytes(header[4..8].try_into()?) as usize;
            if size < 8 {
                bail!("model block too short");
            }
            let block = bytes
                .get(at..at.checked_add(size).context("model block overflow")?)
                .context("model block outside source")?;
            if &header[..4] == b"MAT3" {
                let mat = Mat3::parse(block).with_context(|| name.to_string())?;
                counts[0] += 1;
                for i in 0..mat.len() {
                    match mat
                        .compatibility_program(i, flags, false)
                        .and_then(|p| Ok((cooked_shader(&p)?, p)))
                    {
                        Ok((shader, p)) => {
                            out.write_all(&p)?;
                            println!("{}\t{name}\t{i}\t{shader}", counts[1]);
                            counts[1] += 1;
                        }
                        Err(e) => {
                            counts[2] += 1;
                            eprintln!("{name} material {i}: {e}");
                            if let Ok(dir) = env::var("NEBULA_MATERIAL_SOURCE_DEBUG") {
                                std::fs::write(
                                    std::path::Path::new(&dir).join(format!("rejected-{i}.mat3")),
                                    block,
                                )?;
                            }
                        }
                    }
                }
            }
            at += size;
        }
    }
    Ok(())
}

fn main() -> Result<()> {
    let args: Vec<_> = env::args().skip(1).collect();
    if args.len() != 3 {
        bail!("expected original pak, output dump, loader flags");
    }
    let flags = u32::from_str_radix(args[2].trim_start_matches("0x"), 16)?;
    let mut pak = File::open(&args[0])?;
    let system = pak_system_area(&mut pak)?;
    let mut out = File::create(&args[1])?;
    let mut counts = [0; 3];
    for f in pak_files(system.fst())? {
        if !f.path.ends_with(".arc")
            && !f.path.ends_with(".bmd")
            && !f.path.ends_with(".bdl")
            && !f.path.ends_with(".bmt")
        {
            continue;
        }
        pak.seek(SeekFrom::Start(f.offset))?;
        let mut bytes = vec![0; f.length as usize];
        pak.read_exact(&mut bytes)?;
        scan(&f.path, &bytes, flags, &mut out, &mut counts)?;
    }
    println!(
        "MAT3 blocks {}, programs {}, rejected {}; flags {flags:08x}",
        counts[0], counts[1], counts[2]
    );
    if counts[2] != 0 {
        bail!("source materials were rejected; coverage is incomplete");
    }
    Ok(())
}
