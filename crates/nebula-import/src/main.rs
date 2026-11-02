//! Windows Setup's importer.
//!
//! Every command takes `--key <file or 32 hexadecimal digits>`: the Wii common key a disc image opens with, which this program
//! does not hold. A folder needs none. Built with the `folders-only` feature it refuses disc images.
//!
//!   nebula-import identify <disc image or folder>
//!       One JSON line (InputIdentity::to_json). Exit code 0 when Nebula supports the game, 2 when it does not.
//!   nebula-import import <disc image or folder> --output <content folder>
//!       Writes the content folder. Prints `STEP <n>/<count> <text>` before each step and `PROGRESS <done> <total>`
//!       (bytes) while it copies. The game's files are cooked (nebula_import::cook) as they are packed.
//!   nebula-import cook <content folder> --output <content folder>
//!       Cooks a content folder packed before cooking (an update's migration, D-07), with the same output lines.
//!
//! A failure prints `Error: <reason>` on stderr and exits with code 1.

use anyhow::{bail, Result};
use nebula_import::{cook, identify_input, Partition};
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
    // --key <file or hex>: anywhere in the command, before the command's own arguments are matched.
    let mut rest = arguments.to_vec();
    if let Some(at) = rest.iter().position(|argument| argument == "--key") {
        let Some(key) = rest.get(at + 1) else {
            bail!("--key needs a file or 32 hexadecimal digits");
        };
        nebula_import::set_disc_key(key)?;
        rest.drain(at..at + 2);
    }
    let arguments = rest.as_slice();
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
        [command, input, option, output] if command == "cook" && option == "--output" => {
            println!("STEP 1/1 Cooking the game files");
            let mut reported = 0u64;
            let stats = cook::cook_content(Path::new(input), Path::new(output), &mut |done, total| {
                if done == 0 || done == total || done - reported >= PROGRESS_STEP {
                    println!("PROGRESS {done} {total}");
                    reported = done;
                }
                true
            })?;
            println!("{}", stats.summary());
            Ok(ExitCode::SUCCESS)
        }
        _ => bail!(
            "usage: nebula-import [--key <file or hex>] identify <image> | nebula-import import <image> --output <content folder> | \
             nebula-import cook <content folder> --output <content folder>"
        ),
    }
}
