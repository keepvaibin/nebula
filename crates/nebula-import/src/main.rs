//! Windows Setup's importer.
//!
//! Built with the `folders-only` feature it refuses disc images.
//!
//!   nebula-import identify <disc image or folder>
//!       One JSON line (InputIdentity::to_json). Exit code 0 when Nebula supports the game, 2 when it does not.
//!   nebula-import import <disc image or folder> --output <content folder>
//!       Writes the content folder. Prints `STEP <n>/<count> <text>` before each step and `PROGRESS <done> <total>`
//!       (bytes) while it copies. The game's files are cooked (nebula_import::cook) as they are packed.
//!   nebula-import cook <content folder> --output <content folder>
//!       Cooks a content folder packed before cooking (an update's migration, D-07), with the same output lines.
//!   nebula-import verify <game.pak>
//!       Reads the store's index back and checks it as the game loads it, then further, and prints a line per failure.
//!       Exit code 3 when a check fails.
//!   nebula-import list <game.pak> [prefix] | --long
//!       Every path in the store's index, under `prefix` when one is given; `--long` adds a file's size or a folder's
//!       mark.
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
        // A list of 31,000 paths is read with `head` as often as not: a closed pipe is the reader's goodbye, not a failure.
        Err(error) if broken_pipe(&error) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("Error: {error:#}");
            ExitCode::from(1)
        }
    }
}

/// Whether the failure at the root of a chain is a reader that stopped listening.
fn broken_pipe(error: &anyhow::Error) -> bool {
    error.chain().any(|cause| {
        cause
            .downcast_ref::<std::io::Error>()
            .map_or(false, |io| io.kind() == std::io::ErrorKind::BrokenPipe)
    })
}

fn run(arguments: &[OsString]) -> Result<ExitCode> {
    match arguments {
        #[cfg(feature = "proof-tools")]
        [command, input, output] if command == "model-census" => {
            cook::model_census::run(Path::new(input), Path::new(output))?;
            Ok(ExitCode::SUCCESS)
        }
        #[cfg(feature = "proof-tools")]
        [command, input, output] if command == "clip-census" => {
            cook::clip_census::run(Path::new(input), Path::new(output))?;
            Ok(ExitCode::SUCCESS)
        }
        #[cfg(feature = "proof-tools")]
        [command, input, output] if command == "curve-proof" => {
            cook::model_census::run_curves(Path::new(input), Path::new(output))?;
            Ok(ExitCode::SUCCESS)
        }
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
        [command, plan, option, output] if command == "overlay" && option == "--output" => {
            let (stats, failed) = cook::overlay::cook_plan(Path::new(plan), Path::new(output))?;
            for (path, reason) in &failed {
                println!("LEFT OUT {path}: {reason}");
            }
            println!("{}", stats.summary());
            Ok(ExitCode::SUCCESS)
        }
        [command, input, output] if command == "rt-materials" => {
            cook::hit_record::audit(Path::new(input), Path::new(output))?;
            Ok(ExitCode::SUCCESS)
        }
        [command, input, output] if command == "programs" => {
            cook::programs::write_set(Path::new(input), Path::new(output))?;
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
            // Each file the store leaves out, named with why, so each can be checked against what the game opens.
            let mut left_out = stats.left_out.clone();
            left_out.sort();
            for item in &left_out {
                println!("LEFT OUT {}: {}", item.path, luma_validate::phrase(item.why, "UsEnglish"));
            }
            println!("{}", stats.summary());
            Ok(ExitCode::SUCCESS)
        }
        [command, input] if command == "verify" => {
            let report = cook::store_index::verify(Path::new(input))?;
            cook::store_index::print_report(Path::new(input), &report);
            Ok(if report.problems.is_empty() { ExitCode::SUCCESS } else { ExitCode::from(3) })
        }
        [command, input] if command == "list" => {
            cook::store_index::list(Path::new(input), "", false)?;
            Ok(ExitCode::SUCCESS)
        }
        [command, input, option] if command == "list" && option == "--long" => {
            cook::store_index::list(Path::new(input), "", true)?;
            Ok(ExitCode::SUCCESS)
        }
        [command, input, prefix] if command == "list" => {
            cook::store_index::list(Path::new(input), prefix.to_str().unwrap_or_default(), false)?;
            Ok(ExitCode::SUCCESS)
        }
        _ => bail!(
            "usage: nebula-import identify <image> | nebula-import import <image> --output <content folder> | \
             nebula-import cook <content folder> --output <content folder> | nebula-import verify <game.pak> | \
             nebula-import list <game.pak> [prefix]"
        ),
    }
}
