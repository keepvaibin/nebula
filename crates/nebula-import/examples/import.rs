//! import <disc image> <game.pak>: the importer from the command line (tests; the Windows setup's pipeline remains
//! nebula-recomp-cli's extract and build-pak).

use nebula_import::{build_pak, identify, DiscFile};
use std::fs::File;
use std::path::PathBuf;

fn main() -> anyhow::Result<()> {
    let arguments: Vec<String> = std::env::args().collect();
    if arguments.len() != 3 {
        anyhow::bail!("usage: import <disc image> <game.pak>");
    }
    let identity = identify(DiscFile::new(File::open(&arguments[1])?))?;
    println!("{} [{}] {}: {}", identity.title, identity.game_id, identity.format, identity.message);
    let started = std::time::Instant::now();
    let mut last = 0u64;
    let size = build_pak(DiscFile::new(File::open(&arguments[1])?), &PathBuf::from(&arguments[2]), &mut |done, total| {
        if done - last >= 256 << 20 || done == total {
            println!("{:5.1}%  {} / {} MB", done as f64 * 100.0 / total.max(1) as f64, done >> 20, total >> 20);
            last = done;
        }
        true
    })?;
    println!("game.pak: {size} bytes logical in {:.1} s", started.elapsed().as_secs_f64());
    Ok(())
}
