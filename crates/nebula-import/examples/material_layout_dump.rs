//! Export layout cook recipes for the build-time shader generator.
use anyhow::{bail, Result};
use nebula_import::cook::{archive, material::layout, pak_files, pak_system_area};
use std::{
    env,
    fs::File,
    io::{Read, Seek, SeekFrom, Write},
};

fn scan(bytes: &[u8], out: &mut File, count: &mut usize) -> Result<()> {
    if archive::is_compressed(bytes) {
        return scan(&archive::expand(bytes)?, out, count);
    }
    if archive::is_archive(bytes) {
        for member in archive::members(bytes)? {
            scan(&member.bytes, out, count)?;
        }
    } else if bytes.starts_with(b"RLYT") {
        for p in layout::file_programs(bytes)? {
            out.write_all(&p)?;
            *count += 1;
        }
    }
    Ok(())
}

fn main() -> Result<()> {
    let args: Vec<_> = env::args().skip(1).collect();
    if args.len() != 2 {
        bail!("expected original game.pak and output dump");
    }
    let mut pak = File::open(&args[0])?;
    let system = pak_system_area(&mut pak)?;
    let mut out = File::create(&args[1])?;
    let mut count = 0;
    for file in pak_files(system.fst())? {
        if !file.path.ends_with(".arc") && !file.path.ends_with(".brlyt") {
            continue;
        }
        pak.seek(SeekFrom::Start(file.offset))?;
        let mut bytes = vec![0; file.length as usize];
        pak.read_exact(&mut bytes)?;
        scan(&bytes, &mut out, &mut count)?;
    }
    println!("layout cook variants {count}");
    Ok(())
}
