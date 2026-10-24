//! Windows Setup's importer.
//!
//!   nebula-import identify <disc image or folder>
//!       One JSON line (InputIdentity::to_json). Exit code 0 when Nebula supports the game, 2 when it does not.
//!   nebula-import import <disc image or folder> --output <content folder>
//!       Writes the content folder. Prints `STEP <n>/<count> <text>` before each step and `PROGRESS <done> <total>`
//!       (bytes) while it copies.
//!
//! A failure prints `Error: <reason>` on stderr and exits with code 1.

use anyhow::{bail, Result};
use nebula_import::{identify_input, Partition};
use std::ffi::OsString;
use std::path::Path;
use std::process::ExitCode;

/// Bytes between PROGRESS lines.
const PROGRESS_STEP: u64 = 64 << 20;

fn main() -> ExitCode {
    let arguments: Vec<OsString> = std::env::args_os().skip(1).collect();
    match run(&arguments) {
        Ok(code) => code,
        Err(error) => {
            eprintln!("Error: {error:#}");
            ExitCode::from(1)
        }
    }
}

fn run(arguments: &[OsString]) -> Result<ExitCode> {
    match arguments {
        [command, input] if command == "identify" => {
            let identity = identify_input(Path::new(input))?;
            println!("{}", identity.to_json());
            Ok(if identity.supported { ExitCode::SUCCESS } else { ExitCode::from(2) })
        }
        [command, input, option, output] if command == "import" && option == "--output" => {
            println!("STEP 1/2 Validating the game");
            let mut partition = Partition::open(Path::new(input))?;
            println!("STEP 2/2 Packing the game files");
            let mut reported = 0u64;
            let size = partition.write_content(Path::new(output), &mut |done, total| {
                if done == 0 || done == total || done - reported >= PROGRESS_STEP {
                    println!("PROGRESS {done} {total}");
                    reported = done;
                }
                true
            })?;
            println!("game.pak: {size} bytes");
            Ok(ExitCode::SUCCESS)
        }
        _ => bail!("usage: nebula-import identify <image> | nebula-import import <image> --output <content folder>"),
    }
}
