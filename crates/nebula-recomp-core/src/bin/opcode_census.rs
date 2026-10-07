//! Developer-only RMGE01 instruction-mix and call-site census.
//!
//! Not part of the installer pipeline and not shipped. It answers a question
//! the runtime cannot: which guest opcodes and which guest call sites actually
//! dominate the translated corpus, so codegen work targets the real hot set.
//!
//! Usage:
//!   cargo run --release -p nebula-recomp-core --bin opcode_census -- \
//!       <main.dol> <functions.csv> [top]
//!
//! The opcode names come from the same `powerpc` decoder the translator uses,
//! so the histogram cannot drift from lowering behaviour.

use powerpc::{Extensions, Ins, Opcode};
use std::collections::HashMap;
use std::env;
use std::fs;
use std::fmt::Write as _;

fn be_u32(bytes: &[u8], offset: usize) -> u32 {
    u32::from_be_bytes([
        bytes[offset],
        bytes[offset + 1],
        bytes[offset + 2],
        bytes[offset + 3],
    ])
}

/// DOL header: 7 text section descriptors, then 11 data descriptors.
fn dol_text_sections(bytes: &[u8]) -> Vec<(u32, u32, u32)> {
    let mut text = Vec::new();
    for index in 0..7usize {
        let base = index * 4;
        let offset = be_u32(bytes, base);
        let address = be_u32(bytes, base + 0x48);
        let size = be_u32(bytes, base + 0x90);
        if size != 0 {
            text.push((address, offset, size));
        }
    }
    text
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 3 {
        eprintln!("usage: opcode_census <main.dol> <functions.csv> [top]");
        std::process::exit(2);
    }
    let dol = fs::read(&args[1]).expect("read main.dol");
    let csv = fs::read_to_string(&args[2]).expect("read functions.csv");
    let top: usize = args.get(3).and_then(|value| value.parse().ok()).unwrap_or(40);

    let sections = dol_text_sections(&dol);
    let mut text: Vec<(u32, &[u8])> = Vec::new();
    for (address, offset, size) in sections {
        let start = offset as usize;
        let end = start + size as usize;
        if end > dol.len() {
            eprintln!("section 0x{address:08X} runs past the end of the file");
            continue;
        }
        text.push((address, &dol[start..end]));
    }
    let total_words: usize = text.iter().map(|(_, bytes)| bytes.len() / 4).sum();
    println!("== RMGE01 text ==");
    for (address, bytes) in &text {
        println!(
            "  0x{address:08X}..0x{:08X}  {} bytes",
            address + bytes.len() as u32,
            bytes.len()
        );
    }
    println!("  total instructions: {total_words}");

    let mut hist: HashMap<&'static str, u64> = HashMap::new();
    let mut primary: HashMap<u32, u64> = HashMap::new();
    for (_, bytes) in &text {
        for chunk in bytes.chunks_exact(4) {
            let word = u32::from_be_bytes(chunk.try_into().unwrap());
            let ins = Ins::new(word, Extensions::gekko_broadway());
            *hist.entry(opcode_label(ins.op)).or_default() += 1;
            *primary.entry(word >> 26).or_default() += 1;
        }
    }
    let sum: u64 = hist.values().sum();
    let mut hist_sorted: Vec<(&str, u64)> = hist.iter().map(|(k, v)| (*k, *v)).collect();
    hist_sorted.sort_by(|a, b| b.1.cmp(&a.1));
    println!("\n== opcode histogram (whole text) ==");
    for (name, count) in hist_sorted.iter().take(top * 2) {
        println!(
            "  {:<16} {:>9}  {:>6.3}%",
            name,
            count,
            *count as f64 * 100.0 / sum as f64
        );
    }

    let mut functions: Vec<(u32, u32)> = Vec::new();
    for line in csv.lines().skip(1) {
        let mut parts = line.split(',');
        let (Some(address), Some(size)) = (parts.next(), parts.next()) else {
            continue;
        };
        let (Ok(address), Ok(size)) = (
            u32::from_str_radix(address.trim(), 16),
            u32::from_str_radix(size.trim(), 16),
        ) else {
            continue;
        };
        functions.push((address, size));
    }
    println!("\n== functions ==");
    println!("  count: {}", functions.len());
    let mut by_size = functions.clone();
    by_size.sort_by(|a, b| b.1.cmp(&a.1));
    let total_function_bytes: u64 = functions.iter().map(|(_, size)| u64::from(*size)).sum();
    println!("  catalogued bytes: {total_function_bytes}");
    for (address, size) in by_size.iter().take(top) {
        println!("  0x{address:08X} {:>7} bytes  {:>6} instructions", size, size / 4);
    }

    let read = |address: u32, size: u32| -> Option<&[u8]> {
        for (base, bytes) in &text {
            if address >= *base && (address - *base) as usize + size as usize <= bytes.len() {
                let start = (address - *base) as usize;
                return Some(&bytes[start..start + size as usize]);
            }
        }
        None
    };

    let mut direct_targets: HashMap<u32, u64> = HashMap::new();
    let mut bctrl: u64 = 0;
    let mut bclrl: u64 = 0;
    let mut total_direct: u64 = 0;
    // Per-opcode count restricted to the catalogued function ranges, so a
    // difference against the whole-text histogram is real padding/data.
    let mut function_hist: HashMap<&'static str, u64> = HashMap::new();
    for (address, size) in &functions {
        let Some(bytes) = read(*address, *size) else {
            continue;
        };
        for (index, chunk) in bytes.chunks_exact(4).enumerate() {
            let pc = address + index as u32 * 4;
            let word = u32::from_be_bytes(chunk.try_into().unwrap());
            let ins = Ins::new(word, Extensions::gekko_broadway());
            *function_hist.entry(opcode_label(ins.op)).or_default() += 1;
            // The decoder reports one variant per primary opcode; the link bit
            // is bit 0 of the raw word, exactly as the translator reads it.
            match ins.op {
                Opcode::B | Opcode::Bc => {
                    if word & 1 != 0 {
                        total_direct += 1;
                        // Same sign-extended LI decode as translate.rs
                        // branch_target: bit 1 selects absolute addressing.
                        let displacement = ((word & 0x03FF_FFFC) as i32) << 6 >> 6;
                        let target = if word & 2 != 0 {
                            displacement as u32
                        } else {
                            pc.wrapping_add(displacement as u32)
                        };
                        *direct_targets.entry(target).or_default() += 1;
                    }
                }
                Opcode::Bcctr => {
                    if word & 1 != 0 {
                        bctrl += 1;
                    }
                }
                Opcode::Bclr => {
                    if word & 1 != 0 {
                        bclrl += 1;
                    }
                }
                _ => {}
            }
        }
    }
    let mut targets: Vec<(u32, u64)> = direct_targets.into_iter().collect();
    targets.sort_by(|a, b| b.1.cmp(&a.1));
    let mut starts: std::collections::HashSet<u32> = std::collections::HashSet::new();
    for (address, _) in &functions {
        starts.insert(*address);
    }
    println!("\n== guest call sites ==");
    println!("  direct linked call instructions: {total_direct}");
    println!("  unique direct call targets:      {}", targets.len());
    println!("  bcctrl sites:                    {bctrl}");
    println!("  bclr/bclrl sites:                {bclrl}");
    let unresolved = targets
        .iter()
        .filter(|(target, _)| !starts.contains(target))
        .count();
    println!("  targets that are not catalogued function starts: {unresolved}");
    println!("  most-called direct targets:");
    for (target, count) in targets.iter().take(top) {
        let mark = if starts.contains(target) { " " } else { "?" };
        println!("   {mark} 0x{target:08X} {count} call sites");
    }

    let mut fh: Vec<(&str, u64)> = function_hist.into_iter().collect();
    fh.sort_by(|a, b| b.1.cmp(&a.1));
    let fh_sum: u64 = fh.iter().map(|(_, count)| *count).sum();
    println!("\n== opcode histogram (catalogued functions only) ==");
    for (name, count) in fh.iter().take(top * 2) {
        println!(
            "  {:<16} {:>9}  {:>6.3}%",
            name,
            count,
            *count as f64 * 100.0 / fh_sum as f64
        );
    }

    let mut out = String::new();
    let _ = writeln!(out, "name,count");
    for (name, count) in &fh {
        let _ = writeln!(out, "{name},{count}");
    }
    // The repository content policy forbids generated artifacts in the tree, so
    // this diagnostic writes to the temporary directory unless asked otherwise.
    fs::remove_file("opcode_census.csv").ok();
    let csv_path = match env::var_os("NEBULA_CENSUS_OUT") {
        Some(path) => std::path::PathBuf::from(path),
        None => env::temp_dir().join("nebula_opcode_census.csv"),
    };
    match fs::write(&csv_path, out) {
        Ok(()) => println!("\nwrote {}", csv_path.display()),
        Err(error) => eprintln!(
            "could not write {}: {error} (set NEBULA_CENSUS_OUT to a writable path)",
            csv_path.display()
        ),
    }
}

const fn opcode_label(op: Opcode) -> &'static str {
    match op {
        Opcode::Add => "add",
        Opcode::Addc => "addc",
        Opcode::Adde => "adde",
        Opcode::Addi => "addi",
        Opcode::Addis => "addis",
        Opcode::Addme => "addme",
        Opcode::Addze => "addze",
        Opcode::And => "and",
        Opcode::Andc => "andc",
        Opcode::Andi_ => "andi_",
        Opcode::Andis_ => "andis_",
        Opcode::B => "b",
        Opcode::Bc => "bc",
        Opcode::Bcctr => "bcctr",
        Opcode::Bclr => "bclr",
        Opcode::Cmp => "cmp",
        Opcode::Cmpi => "cmpi",
        Opcode::Cmpl => "cmpl",
        Opcode::Cmpli => "cmpli",
        Opcode::Cntlzw => "cntlzw",
        Opcode::Crand => "crand",
        Opcode::Cror => "cror",
        Opcode::Divw => "divw",
        Opcode::Divwu => "divwu",
        Opcode::Eqv => "eqv",
        Opcode::Extsb => "extsb",
        Opcode::Extsh => "extsh",
        Opcode::Lbz => "lbz",
        Opcode::Lbzu => "lbzu",
        Opcode::Lbzx => "lbzx",
        Opcode::Lbzux => "lbzux",
        Opcode::Lfd => "lfd",
        Opcode::Lfdu => "lfdu",
        Opcode::Lfs => "lfs",
        Opcode::Lfsu => "lfsu",
        Opcode::Lha => "lha",
        Opcode::Lhau => "lhau",
        Opcode::Lhax => "lhax",
        Opcode::Lhz => "lhz",
        Opcode::Lhzu => "lhzu",
        Opcode::Lhzx => "lhzx",
        Opcode::Lhzux => "lhzux",
        Opcode::Lmw => "lmw",
        Opcode::Lwz => "lwz",
        Opcode::Lwzu => "lwzu",
        Opcode::Lwzx => "lwzx",
        Opcode::Lwzux => "lwzux",
        Opcode::Mfspr => "mfspr",
        Opcode::Mftb => "mftb",
        Opcode::Mtspr => "mtspr",
        Opcode::Mulli => "mulli",
        Opcode::Mullw => "mullw",
        Opcode::Nand => "nand",
        Opcode::Neg => "neg",
        Opcode::Nor => "nor",
        Opcode::Or => "or",
        Opcode::Orc => "orc",
        Opcode::Ori => "ori",
        Opcode::Oris => "oris",
        Opcode::Rlwimi => "rlwimi",
        Opcode::Rlwinm => "rlwinm",
        Opcode::Rlwnm => "rlwnm",
        Opcode::Slw => "slw",
        Opcode::Sraw => "sraw",
        Opcode::Srawi => "srawi",
        Opcode::Srw => "srw",
        Opcode::Stb => "stb",
        Opcode::Stbu => "stbu",
        Opcode::Stbx => "stbx",
        Opcode::Stbux => "stbux",
        Opcode::Stfd => "stfd",
        Opcode::Stfdu => "stfdu",
        Opcode::Stfs => "stfs",
        Opcode::Stfsu => "stfsu",
        Opcode::Sth => "sth",
        Opcode::Sthu => "sthu",
        Opcode::Sthx => "sthx",
        Opcode::Sthux => "sthux",
        Opcode::Stmw => "stmw",
        Opcode::Stw => "stw",
        Opcode::Stwu => "stwu",
        Opcode::Stwx => "stwx",
        Opcode::Stwux => "stwux",
        Opcode::Subf => "subf",
        Opcode::Subfc => "subfc",
        Opcode::Subfe => "subfe",
        Opcode::Subfic => "subfic",
        Opcode::Xor => "xor",
        Opcode::Xori => "xori",
        Opcode::Xoris => "xoris",
        Opcode::Fadd => "fadd",
        Opcode::Fadds => "fadds",
        Opcode::Fcmpo => "fcmpo",
        Opcode::Fcmpu => "fcmpu",
        Opcode::Fctiw => "fctiw",
        Opcode::Fctiwz => "fctiwz",
        Opcode::Fdiv => "fdiv",
        Opcode::Fdivs => "fdivs",
        Opcode::Fmadd => "fmadd",
        Opcode::Fmadds => "fmadds",
        Opcode::Fmr => "fmr",
        Opcode::Fmsub => "fmsub",
        Opcode::Fmsubs => "fmsubs",
        Opcode::Fmul => "fmul",
        Opcode::Fmuls => "fmuls",
        Opcode::Fnmsub => "fnmsub",
        Opcode::Fnmsubs => "fnmsubs",
        Opcode::Frsp => "frsp",
        Opcode::Fsub => "fsub",
        Opcode::Fsubs => "fsubs",
        Opcode::PsAdd => "ps_add",
        Opcode::PsDiv => "ps_div",
        Opcode::PsMadd => "ps_madd",
        Opcode::PsMerge00 => "ps_merge00",
        Opcode::PsMul => "ps_mul",
        Opcode::PsSel => "ps_sel",
        Opcode::PsSub => "ps_sub",
        Opcode::PsSum0 => "ps_sum0",
        Opcode::PsqL => "psq_l",
        Opcode::PsqLu => "psq_lu",
        Opcode::PsqLx => "psq_lx",
        Opcode::PsqSt => "psq_st",
        Opcode::PsqStu => "psq_stu",
        Opcode::Dcbf => "dcbf",
        Opcode::Dcbi => "dcbi",
        Opcode::Dcbt => "dcbt",
        Opcode::Dcbz => "dcbz",
        Opcode::Sync => "sync",
        Opcode::Rfi => "rfi",
        _ => "other",
    }
}
