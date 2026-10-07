//! Developer-only disassembly probe for specific RMGE01 guest addresses.
//!
//! Prints the guest words at any address list, following the real DOL section
//! map, so a hot call target's actual body can be inspected next to the
//! translated C++ the recompiler emits for it.
//!
//! Usage:
//!   cargo run --release -p nebula-recomp-core --bin disasm_probe -- <main.dol> 0xADDR [count] [0xADDR ...]

use nebula_recomp_core::DolImage;
use powerpc::{Extensions, Ins};
use std::fs;

fn main() {
    let mut args = std::env::args().skip(1);
    let path = args.next().expect("dol path");
    let data = fs::read(&path).expect("read dol");
    let dol = DolImage::parse(&data).expect("parse dol");
    println!("entry 0x{:08X}", dol.entry_point);
    for section in &dol.sections {
        println!(
            "  {:?} {} va=0x{:08X} size=0x{:08X} file=0x{:08X}",
            section.kind, section.index, section.address, section.size, section.file_offset
        );
    }

    let word_at = |address: u32| -> Option<u32> {
        for section in &dol.sections {
            let start = section.address;
            let end = section.address.checked_add(section.size)?;
            if address >= start && address < end {
                let offset = section.file_offset as usize + (address - start) as usize;
                if offset + 4 <= data.len() {
                    return Some(u32::from_be_bytes(
                        data[offset..offset + 4].try_into().unwrap(),
                    ));
                }
            }
        }
        None
    };

    let rest: Vec<String> = args.collect();
    let mut index = 0;
    while index < rest.len() {
        let address = parse_hex(&rest[index]);
        let count: u32 = rest
            .get(index + 1)
            .and_then(|value| value.parse().ok())
            .unwrap_or(24);
        println!("\n=== 0x{address:08X} ({count} instructions) ===");
        for step in 0..count {
            let pc = address.wrapping_add(step * 4);
            let Some(word) = word_at(pc) else {
                println!("0x{pc:08X}  <outside text>");
                break;
            };
            let ins = Ins::new(word, Extensions::gekko_broadway());
            println!("0x{pc:08X}  {word:08X}  {ins:?}");
        }
        index += 2;
    }
}

fn parse_hex(text: &str) -> u32 {
    u32::from_str_radix(text.trim_start_matches("0x"), 16).expect("hex address")
}
