//! Agent 20, developer-only static coverage probe (NOT part of the pipeline).
//!
//! Mirrors the eligibility predicates of `translate.rs`'s
//! `analyze_resident_integer_leaf` and `lower_resident_integer_direct_calls` so
//! the *coverage* of the `guest_resident_leaf` codegen path can be counted
//! without generating a module. Reads a DOL + the function map; executes no
//! guest code.
//!
//! Usage: residency_coverage <main.dol> <functions.csv>

use nebula_recomp_core::rmge01_function_map;
use powerpc::{Extensions, Ins, Opcode};
use std::collections::BTreeMap;
use std::fs;

fn be_u32(bytes: &[u8], offset: usize) -> u32 {
    u32::from_be_bytes([bytes[offset], bytes[offset + 1], bytes[offset + 2], bytes[offset + 3]])
}

/// DOL header: 7 text descriptors (offset 0x00, address 0x48, size 0x90).
fn text_sections(bytes: &[u8]) -> Vec<(u32, usize, usize)> {
    let mut out = Vec::new();
    for index in 0..7usize {
        let base = index * 4;
        let offset = be_u32(bytes, base) as usize;
        let address = be_u32(bytes, base + 0x48);
        let size = be_u32(bytes, base + 0x90) as usize;
        if size != 0 {
            out.push((address, offset, size));
        }
    }
    out
}

fn gpr_rt(word: u32) -> u32 { (word >> 21) & 0x1F }
fn gpr_ra(word: u32) -> u32 { (word >> 16) & 0x1F }
fn gpr_rs(word: u32) -> u32 { (word >> 21) & 0x1F }
fn spr_number(word: u32) -> u32 { ((word >> 16) & 0x1F) << 5 | ((word >> 11) & 0x1F) }

#[derive(Default)]
struct Stats {
    functions: u64,
    bytes: u64,
    leaf_ok: u64,
    leaf_bytes: u64,
    direct_ok: u64,
    direct_bytes: u64,
    too_big_leaf: u64,
    too_big_direct: u64,
    opcode_rejects: BTreeMap<&'static str, u64>,
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        eprintln!("usage: residency_coverage <main.dol> <functions.csv>");
        std::process::exit(2);
    }
    let data = fs::read(&args[1]).expect("read main.dol");
    let csv = fs::read_to_string(&args[2]).expect("read functions.csv");

    // The map embedded in the crate is the exact one the pipeline uses.
    let _ = csv;
    let functions = rmge01_function_map();

    let sections = text_sections(&data);
    let word_at = |address: u32| -> Option<u32> {
        for (base, off, size) in &sections {
            let end = base.checked_add(*size as u32)?;
            if address >= *base && address < end {
                let o = *off + (address - *base) as usize;
                if o + 4 <= data.len() {
                    return Some(u32::from_be_bytes(data[o..o + 4].try_into().unwrap()));
                }
            }
        }
        None
    };

    let mut stats = Stats::default();
    for function in &functions {
        let Some(_) = word_at(function.address) else { continue };
        stats.functions += 1;
        stats.bytes += function.size as u64;
        let words = (function.size / 4) as usize;
        if words == 0 {
            continue;
        }

        // ---- analyze_resident_integer_leaf predicate ----
        if function.size > 512 {
            stats.too_big_leaf += 1;
        } else {
            let mut ok = true;
            for index in 0..words {
                let pc = function.address.wrapping_add((index as u32) * 4);
                let Some(word) = word_at(pc) else { ok = false; break };
                match Ins::new(word, Extensions::gekko_broadway()).op {
                    Opcode::Addi | Opcode::Addis => {}
                    Opcode::Ori | Opcode::Oris | Opcode::Xori | Opcode::Xoris => {}
                    Opcode::Bclr if word == 0x4E80_0020 && index + 1 == words => {}
                    _ => {
                        ok = false;
                        break;
                    }
                }
            }
            if ok {
                stats.leaf_ok += 1;
                stats.leaf_bytes += function.size as u64;
            }
        }

        // ---- lower_resident_integer_direct_calls predicate ----
        if function.size > 2048 {
            stats.too_big_direct += 1;
            continue;
        }
        let end = match function.address.checked_add(function.size) {
            Some(v) => v,
            None => continue,
        };
        let mut ok = true;
        let mut saw_call = false;
        let mut reject: Option<&'static str> = None;
        for index in 0..words {
            let pc = function.address.wrapping_add((index as u32) * 4);
            let Some(word) = word_at(pc) else {
                reject = Some("outside-text");
                ok = false;
                break;
            };
            let op = Ins::new(word, Extensions::gekko_broadway()).op;
            match op {
                Opcode::Addi | Opcode::Addis => {}
                Opcode::Ori | Opcode::Oris | Opcode::Xori | Opcode::Xoris => {}
                Opcode::Mfspr | Opcode::Mtspr if spr_number(word) == 8 => {}
                Opcode::Lwz | Opcode::Stw | Opcode::Stwu if gpr_ra(word) == 1 => {}
                Opcode::B if word & 1 != 0 && index + 1 < words => {
                    let displacement = (word & 0x03FF_FFFC) as i32;
                    let signed = if displacement & 0x0200_0000 != 0 {
                        displacement | !0x03FF_FFFF
                    } else {
                        displacement
                    };
                    let target = (pc as i64 + signed as i64) as u32;
                    if target >= function.address && target < end {
                        reject = Some("in-range-branch");
                        ok = false;
                        break;
                    }
                    saw_call = true;
                }
                Opcode::Bclr if word == 0x4E80_0020 && index + 1 == words => {}
                other => {
                    reject = Some(opcode_name(other));
                    ok = false;
                    break;
                }
            }
        }
        if ok && saw_call {
            stats.direct_ok += 1;
            stats.direct_bytes += function.size as u64;
        } else if let Some(name) = reject {
            *stats.opcode_rejects.entry(name).or_insert(0) += 1;
        }
        let _ = gpr_rt;
        let _ = gpr_rs;
    }

    println!("== guest_resident_leaf coverage (static) ==");
    println!("functions in map with text: {}", stats.functions);
    println!("total catalogued bytes:      {}", stats.bytes);
    println!();
    println!("leaf path  (<=512 B, addi/ori/xori + final bclr only):");
    println!("   eligible functions: {}", stats.leaf_ok);
    println!("   eligible bytes:     {}", stats.leaf_bytes);
    println!("   rejected as >512 B: {}", stats.too_big_leaf);
    println!();
    println!("direct-call path (<=2048 B, r1 stack mem, LR spr, out-of-range b):");
    println!("   eligible functions: {}", stats.direct_ok);
    println!("   eligible bytes:     {}", stats.direct_bytes);
    println!("   rejected as >2048 B: {}", stats.too_big_direct);
    println!();
    println!("top rejection reasons on the direct-call path (first bad opcode):");
    let mut rejects: Vec<_> = stats.opcode_rejects.iter().collect();
    rejects.sort_by(|a, b| b.1.cmp(a.1));
    for (name, count) in rejects.iter().take(25) {
        println!("   {name:<16} {count}");
    }
}

fn opcode_name(op: Opcode) -> &'static str {
    match op {
        Opcode::Lwz => "lwz-not-r1",
        Opcode::Stw => "stw-not-r1",
        Opcode::Stwu => "stwu-not-r1",
        Opcode::Lbz => "lbz",
        Opcode::Stb => "stb",
        Opcode::Lhz => "lhz",
        Opcode::Sth => "sth",
        Opcode::Lfs => "lfs",
        Opcode::Stfs => "stfs",
        Opcode::Lfd => "lfd",
        Opcode::Stfd => "stfd",
        Opcode::Mfspr => "mfspr-nonlr",
        Opcode::Mtspr => "mtspr-nonlr",
        Opcode::B => "b",
        Opcode::Bc => "bc",
        Opcode::Bclr => "bclr-nonfinal",
        Opcode::Bcctr => "bcctr",
        Opcode::Cmpi => "cmpi",
        Opcode::Cmpli => "cmpli",
        Opcode::Cmp => "cmp",
        Opcode::Cmpl => "cmpl",
        Opcode::Add => "add",
        Opcode::Addic => "addic",
        Opcode::Subf => "subf",
        Opcode::Mullw => "mullw",
        Opcode::Or => "or",
        Opcode::And => "and",
        Opcode::Rlwinm => "rlwinm",
        Opcode::Fmr => "fmr",
        Opcode::Fmuls => "fmuls",
        Opcode::Fadds => "fadds",
        Opcode::PsqL => "psq_l",
        Opcode::PsqSt => "psq_st",
        Opcode::Rfi => "rfi",
        Opcode::Sync => "sync",
        Opcode::Dcbz => "dcbz",
        _ => "other",
    }
}
