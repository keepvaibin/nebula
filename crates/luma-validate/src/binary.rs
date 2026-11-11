//! Reads a mod's library without loading it (luma-format 6): its format and machine, its exports, what it imports
//! from where, and the words of its data with the loader's fixups applied, so `luma_mod` and the import list it
//! points at can be read as the game's loader will see them. PE (Windows), ELF (Linux, Android), Mach-O (macOS, iOS).
//! Every read is bounds-checked; a malformed file gives a phrase, never a panic.

use std::collections::HashMap;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Format {
    Pe,
    Elf,
    MachO,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Machine {
    X64,
    Arm64,
    Other(u32),
}

/// One thing the library takes from outside it.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct Import {
    /// The library it names (a DLL, a DT_NEEDED, a Mach-O dylib); empty when the format does not say (an ELF
    /// symbol is found in any library loaded).
    pub library: String,
    /// The symbol; `#<n>` for a Windows import by ordinal.
    pub symbol: String,
}

pub struct Library<'d> {
    pub format: Format,
    pub machine: Machine,
    /// A shared library (DLL, ET_DYN, MH_DYLIB), not a program or object.
    pub shared: bool,
    pub exports: HashMap<String, u64>,
    pub imports: Vec<Import>,
    /// Every library it names (with or without symbols).
    pub needed: Vec<String>,
    /// What the system's loader would run in it when it loads, before LumaCore's `begin` (loader contract D2-6): a
    /// PE entry point or TLS callbacks, ELF DT_INIT, DT_INIT_ARRAY or DT_PREINIT_ARRAY, a Mach-O init section or
    /// LC_ROUTINES_64. Each named as the format names it.
    pub runs_at_load: Vec<String>,
    /// What the system would run when it closes the library, after LumaCore has freed the mod's memory (F's audit of
    /// R3-A, A-1): ELF DT_FINI or DT_FINI_ARRAY, a Mach-O term section. A PE's entry point and TLS callbacks run at
    /// both and are in `runs_at_load`.
    pub runs_at_close: Vec<String>,
    data: &'d [u8],
    /// Address ranges to file offsets: (address, size in file, file offset).
    maps: Vec<(u64, u64, u64)>,
    /// Pointer values the loader writes at an address (relocations), when they differ from what is in the file.
    fixups: HashMap<u64, u64>,
    /// How a Mach-O chained-fixup pointer is decoded (None: the word is the address).
    chained: Option<u16>,
    /// The address the library is linked at (PE ImageBase, Mach-O __TEXT vmaddr); ELF is linked at 0.
    base: u64,
    is_64: bool,
}

fn u16_at(d: &[u8], at: usize) -> Option<u16> {
    Some(u16::from_le_bytes(d.get(at..at.checked_add(2)?)?.try_into().ok()?))
}
fn u32_at(d: &[u8], at: usize) -> Option<u32> {
    Some(u32::from_le_bytes(d.get(at..at.checked_add(4)?)?.try_into().ok()?))
}
fn u64_at(d: &[u8], at: usize) -> Option<u64> {
    Some(u64::from_le_bytes(d.get(at..at.checked_add(8)?)?.try_into().ok()?))
}
fn cstr(d: &[u8], at: usize, max: usize) -> Option<String> {
    let rest = d.get(at..)?;
    let end = rest.iter().take(max).position(|&b| b == 0)?;
    String::from_utf8(rest[..end].to_vec()).ok()
}

/// Reads a library, or says why it cannot (a phrase).
pub fn read(data: &[u8]) -> Result<Library<'_>, &'static str> {
    if data.starts_with(b"MZ") {
        read_pe(data).ok_or("Lib.BadPe")
    } else if data.starts_with(b"\x7fELF") {
        read_elf(data).ok_or("Lib.BadElf")
    } else if data.starts_with(&[0xcf, 0xfa, 0xed, 0xfe]) {
        read_macho(data).ok_or("Lib.BadMachO")
    } else if data.starts_with(&[0xca, 0xfe, 0xba, 0xbe]) || data.starts_with(&[0xbe, 0xba, 0xfe, 0xca]) {
        Err("Lib.Universal")
    } else if data.starts_with(&[0xce, 0xfa, 0xed, 0xfe]) {
        Err("Lib.ThirtyTwoBit")
    } else {
        Err("Lib.NotALibrary")
    }
}

impl Library<'_> {
    fn offset_of(&self, address: u64, len: u64) -> Option<usize> {
        self.maps
            .iter()
            .find(|&&(start, size, _)| address >= start && address.checked_add(len).is_some_and(|end| end <= start.saturating_add(size)))
            .and_then(|&(start, _, off)| usize::try_from(off + (address - start)).ok())
    }
    pub fn u32_at_address(&self, address: u64) -> Option<u32> {
        u32_at(self.data, self.offset_of(address, 4)?)
    }
    /// A pointer stored at `address`, as the loader leaves it, as an address of this library (base subtracted).
    pub fn pointer_at(&self, address: u64) -> Option<u64> {
        if !self.is_64 {
            return None;
        }
        if let Some(&v) = self.fixups.get(&address) {
            return Some(v);
        }
        let raw = u64_at(self.data, self.offset_of(address, 8)?)?;
        if raw == 0 {
            return Some(0); // a null pointer has no fixup and is not moved by the base
        }
        match self.chained {
            // DYLD_CHAINED_PTR_64 (2) and _64_OFFSET (6): a rebase keeps its target in the low 36 bits and its top
            // byte in bits 36..44; bit 63 set is a bind (an import), never a pointer into the library.
            Some(format @ (2 | 6)) => {
                if raw >> 63 != 0 {
                    return None;
                }
                let target = (raw & 0xf_ffff_ffff) | ((raw >> 36) & 0xff) << 56;
                Some(if format == 6 { target } else { target.wrapping_sub(self.base) })
            }
            Some(_) => None,
            None => Some(raw.wrapping_sub(self.base)),
        }
    }
    /// A NUL-terminated UTF-8 string at `address`, at most `max` bytes.
    pub fn string_at(&self, address: u64, max: usize) -> Option<String> {
        let off = self.offset_of(address, 1)?;
        cstr(self.data, off, max)
    }
}

fn read_pe(d: &[u8]) -> Option<Library<'_>> {
    let pe = u32_at(d, 0x3c)? as usize;
    if d.get(pe..pe + 4)? != b"PE\0\0" {
        return None;
    }
    let coff = pe + 4;
    let machine = match u16_at(d, coff)? {
        0x8664 => Machine::X64,
        0xaa64 => Machine::Arm64,
        m => Machine::Other(m as u32),
    };
    let sections = u16_at(d, coff + 2)? as usize;
    let opt_size = u16_at(d, coff + 16)? as usize;
    let characteristics = u16_at(d, coff + 18)?;
    let opt = coff + 20;
    let is_64 = u16_at(d, opt)? == 0x20b;
    let (base, dirs) = if is_64 { (u64_at(d, opt + 24)?, opt + 112) } else { (u32_at(d, opt + 28)? as u64, opt + 96) };
    let dir_count = u32_at(d, dirs - 4)? as usize;
    let dir = |i: usize| -> Option<(u32, u32)> {
        if i >= dir_count {
            return Some((0, 0));
        }
        Some((u32_at(d, dirs + i * 8)?, u32_at(d, dirs + i * 8 + 4)?))
    };
    let table = opt + opt_size;
    let mut maps = Vec::new();
    for s in 0..sections {
        let h = table + s * 40;
        let vsize = u32_at(d, h + 8)? as u64;
        let va = u32_at(d, h + 12)? as u64;
        let raw_size = u32_at(d, h + 16)? as u64;
        let raw_at = u32_at(d, h + 20)? as u64;
        maps.push((va, raw_size.min(vsize.max(raw_size)), raw_at));
    }
    let mut lib = Library {
        format: Format::Pe,
        machine,
        shared: characteristics & 0x2000 != 0,
        exports: HashMap::new(),
        imports: Vec::new(),
        needed: Vec::new(),
        runs_at_load: Vec::new(),
        runs_at_close: Vec::new(),
        data: d,
        maps,
        fixups: HashMap::new(),
        chained: None,
        base,
        is_64,
    };
    if u32_at(d, opt + 16)? != 0 {
        lib.runs_at_load.push("AddressOfEntryPoint".to_string());
    }
    // TLS (A-3): the data alone runs nothing; the callbacks run at load and close. IMAGE_TLS_DIRECTORY holds
    // AddressOfCallBacks, an address (ImageBase added) at +24 (PE32+) or +12 (PE32), of an array that ends with a null.
    let (tls, _) = dir(9)?;
    if tls != 0 {
        let width = if is_64 { 8 } else { 4 };
        let word = |at: usize| if is_64 { u64_at(d, at) } else { u32_at(d, at).map(u64::from) };
        let callbacks = word(lib.offset_of(tls as u64, 3 * width + width)? + 3 * width as usize)?;
        if callbacks != 0 {
            let first = word(lib.offset_of(callbacks.checked_sub(base)?, width)?)?;
            if first != 0 {
                lib.runs_at_load.push("TLS callbacks (IMAGE_TLS_DIRECTORY.AddressOfCallBacks)".to_string());
            }
        }
    }
    let off = |lib: &Library, rva: u32, len: u64| lib.offset_of(rva as u64, len);
    // Exports.
    let (export_rva, export_size) = dir(0)?;
    if export_rva != 0 && export_size != 0 {
        let e = off(&lib, export_rva, 40)?;
        let functions = u32_at(d, e + 28)?;
        let name_count = u32_at(d, e + 24)? as usize;
        let names = u32_at(d, e + 32)?;
        let ordinals = u32_at(d, e + 36)?;
        for i in 0..name_count {
            let name_rva = u32_at(d, off(&lib, names, 4 * (i as u64 + 1))? + i * 4)?;
            let name = cstr(d, off(&lib, name_rva, 1)?, 4096)?;
            let ordinal = u16_at(d, off(&lib, ordinals, 2 * (i as u64 + 1))? + i * 2)? as usize;
            let rva = u32_at(d, off(&lib, functions, 4 * (ordinal as u64 + 1))? + ordinal * 4)?;
            lib.exports.insert(name, rva as u64);
        }
    }
    // Imports (directory 1) and delay imports (13): each library's name and its functions by name or ordinal.
    for (index, entry_size, name_field, thunk_field) in [(1usize, 20usize, 12usize, 0usize), (13, 32, 4, 16)] {
        let (rva, _) = dir(index)?;
        if rva == 0 {
            continue;
        }
        for n in 0..4096 {
            let at = off(&lib, rva + (n * entry_size) as u32, entry_size as u64)?;
            if d.get(at..at + entry_size)?.iter().all(|&b| b == 0) {
                break;
            }
            let library = cstr(d, off(&lib, u32_at(d, at + name_field)?, 1)?, 512)?;
            let mut thunks = u32_at(d, at + thunk_field)?;
            if thunks == 0 && index == 1 {
                thunks = u32_at(d, at + 16)?;
            }
            lib.needed.push(library.clone());
            let width = if is_64 { 8 } else { 4 };
            for k in 0..65536u32 {
                let t_at = off(&lib, thunks + k * width, width as u64)?;
                let value = if is_64 { u64_at(d, t_at)? } else { u32_at(d, t_at)? as u64 };
                if value == 0 {
                    break;
                }
                let by_ordinal = if is_64 { value >> 63 != 0 } else { value >> 31 != 0 };
                let symbol = if by_ordinal {
                    format!("#{}", value & 0xffff)
                } else {
                    cstr(d, off(&lib, (value & 0x7fff_ffff) as u32 + 2, 1)?, 4096)?
                };
                lib.imports.push(Import { library: library.clone(), symbol });
            }
        }
    }
    Some(lib)
}

fn read_elf(d: &[u8]) -> Option<Library<'_>> {
    if d.get(4)? != &2 || d.get(5)? != &1 {
        return None; // only 64-bit little-endian
    }
    let kind = u16_at(d, 16)?;
    let machine = match u16_at(d, 18)? {
        62 => Machine::X64,
        183 => Machine::Arm64,
        m => Machine::Other(m as u32),
    };
    let ph_at = u64_at(d, 32)? as usize;
    let sh_at = u64_at(d, 40)? as usize;
    let ph_size = u16_at(d, 54)? as usize;
    let ph_count = u16_at(d, 56)? as usize;
    let sh_size = u16_at(d, 58)? as usize;
    let sh_count = u16_at(d, 60)? as usize;
    let mut maps = Vec::new();
    let mut dynamic = None;
    for i in 0..ph_count {
        let h = ph_at.checked_add(i.checked_mul(ph_size)?)?;
        let kind = u32_at(d, h)?;
        let offset = u64_at(d, h + 8)?;
        let vaddr = u64_at(d, h + 16)?;
        let filesz = u64_at(d, h + 32)?;
        if kind == 1 {
            maps.push((vaddr, filesz, offset));
        } else if kind == 2 {
            dynamic = Some((offset as usize, filesz as usize));
        }
    }
    let mut lib = Library {
        format: Format::Elf,
        machine,
        shared: kind == 3,
        exports: HashMap::new(),
        imports: Vec::new(),
        needed: Vec::new(),
        runs_at_load: Vec::new(),
        runs_at_close: Vec::new(),
        data: d,
        maps,
        fixups: HashMap::new(),
        chained: None,
        base: 0,
        is_64: true,
    };
    let Some((dyn_at, dyn_size)) = dynamic else { return Some(lib) };
    let mut tags: HashMap<u64, u64> = HashMap::new();
    let mut needed_offsets = Vec::new();
    for i in 0..dyn_size / 16 {
        let tag = u64_at(d, dyn_at + i * 16)?;
        let value = u64_at(d, dyn_at + i * 16 + 8)?;
        if tag == 0 {
            break;
        }
        if tag == 1 {
            needed_offsets.push(value);
        } else {
            tags.insert(tag, value);
        }
    }
    if tags.contains_key(&12) {
        lib.runs_at_load.push("DT_INIT".to_string());
    }
    for (array, size, name) in [(25u64, 27u64, "DT_INIT_ARRAY"), (32, 33, "DT_PREINIT_ARRAY")] {
        if tags.contains_key(&array) && tags.get(&size).copied().unwrap_or(1) != 0 {
            lib.runs_at_load.push(name.to_string());
        }
    }
    if tags.contains_key(&13) {
        lib.runs_at_close.push("DT_FINI".to_string());
    }
    if tags.contains_key(&26) && tags.get(&28).copied().unwrap_or(1) != 0 {
        lib.runs_at_close.push("DT_FINI_ARRAY".to_string());
    }
    let strtab = lib.offset_of(*tags.get(&5)?, 1)?;
    let strsz = *tags.get(&10).unwrap_or(&0) as usize;
    let string = |o: u64| -> Option<String> {
        let o = usize::try_from(o).ok()?;
        if strsz != 0 && o >= strsz {
            return None;
        }
        cstr(d, strtab.checked_add(o)?, 4096)
    };
    for o in needed_offsets {
        lib.needed.push(string(o)?);
    }
    // The dynamic symbols: the count from the section headers (.dynsym), else from DT_HASH.
    let symtab = tags.get(&6).copied();
    let mut count = 0usize;
    for i in 0..sh_count {
        let h = sh_at.checked_add(i.checked_mul(sh_size)?)?;
        if u32_at(d, h + 4)? == 11 {
            let size = u64_at(d, h + 32)? as usize;
            let entsize = (u64_at(d, h + 56)? as usize).max(1);
            count = size / entsize;
        }
    }
    if count == 0 {
        if let Some(&hash) = tags.get(&4) {
            count = u32_at(d, lib.offset_of(hash, 8)? + 4)? as usize;
        }
    }
    let mut symbols: Vec<(String, u64, bool)> = Vec::new();
    if let Some(symtab) = symtab {
        let base = lib.offset_of(symtab, 24)?;
        for i in 0..count {
            let s = base.checked_add(i * 24)?;
            let name = string(u32_at(d, s)? as u64)?;
            let info = *d.get(s + 4)?;
            let other = *d.get(s + 5)?;
            let shndx = u16_at(d, s + 6)?;
            let value = u64_at(d, s + 8)?;
            let bind = info >> 4;
            symbols.push((name.clone(), value, shndx != 0));
            if name.is_empty() || !(bind == 1 || bind == 2) {
                continue;
            }
            if shndx == 0 {
                lib.imports.push(Import { library: String::new(), symbol: name });
            } else if other & 3 == 0 || other & 3 == 3 {
                lib.exports.insert(name, value);
            }
        }
    }
    // Relocations that write pointers: RELA (DT_RELA/DT_RELASZ and the PLT's DT_JMPREL/DT_PLTRELSZ) and RELR.
    let relative = if machine == Machine::Arm64 { 1027 } else { 8 };
    let absolute = if machine == Machine::Arm64 { [257u32, 1025] } else { [1u32, 6] };
    for (at_tag, size_tag) in [(7u64, 8u64), (23, 2)] {
        let (Some(&at), Some(&size)) = (tags.get(&at_tag), tags.get(&size_tag)) else { continue };
        let Some(start) = lib.offset_of(at, size) else { continue };
        for i in 0..size as usize / 24 {
            let r = start + i * 24;
            let offset = u64_at(d, r)?;
            let info = u64_at(d, r + 8)?;
            let addend = u64_at(d, r + 16)?;
            let (kind, sym) = ((info & 0xffff_ffff) as u32, (info >> 32) as usize);
            if kind == relative {
                lib.fixups.insert(offset, addend);
            } else if absolute.contains(&kind) {
                if let Some((_, value, true)) = symbols.get(sym) {
                    lib.fixups.insert(offset, value.wrapping_add(addend));
                }
            }
        }
    }
    if let (Some(&at), Some(&size)) = (tags.get(&36), tags.get(&35)) {
        if let Some(start) = lib.offset_of(at, size) {
            let mut next = 0u64;
            for i in 0..size as usize / 8 {
                let entry = u64_at(d, start + i * 8)?;
                if entry & 1 == 0 {
                    // An address: the word there is relocated by the base (0 here), so its value stays.
                    if let Some(v) = lib.offset_of(entry, 8).and_then(|o| u64_at(d, o)) {
                        lib.fixups.insert(entry, v);
                    }
                    next = entry + 8;
                } else {
                    let mut bits = entry >> 1;
                    let mut where_ = next;
                    while bits != 0 {
                        if bits & 1 != 0 {
                            if let Some(v) = lib.offset_of(where_, 8).and_then(|o| u64_at(d, o)) {
                                lib.fixups.insert(where_, v);
                            }
                        }
                        bits >>= 1;
                        where_ += 8;
                    }
                    next += 63 * 8;
                }
            }
        }
    }
    Some(lib)
}

fn read_macho(d: &[u8]) -> Option<Library<'_>> {
    let cpu = u32_at(d, 4)?;
    let machine = match cpu {
        0x0100_0007 => Machine::X64,
        0x0100_000c => Machine::Arm64,
        m => Machine::Other(m),
    };
    let filetype = u32_at(d, 12)?;
    let ncmds = u32_at(d, 16)? as usize;
    let mut lib = Library {
        format: Format::MachO,
        machine,
        shared: filetype == 6,
        exports: HashMap::new(),
        imports: Vec::new(),
        needed: Vec::new(),
        runs_at_load: Vec::new(),
        runs_at_close: Vec::new(),
        data: d,
        maps: Vec::new(),
        fixups: HashMap::new(),
        chained: None,
        base: 0,
        is_64: true,
    };
    let mut at = 32usize;
    let mut symtab = None;
    let mut chained_at = None;
    for _ in 0..ncmds {
        let cmd = u32_at(d, at)?;
        let size = u32_at(d, at + 4)? as usize;
        if size < 8 {
            return None;
        }
        match cmd {
            0x19 => {
                let segname = d.get(at + 8..at + 24)?;
                let name = String::from_utf8_lossy(segname).trim_end_matches('\0').to_string();
                let vmaddr = u64_at(d, at + 24)?;
                let fileoff = u64_at(d, at + 40)?;
                let filesize = u64_at(d, at + 48)?;
                if name == "__TEXT" {
                    lib.base = vmaddr;
                }
                lib.maps.push((vmaddr, filesize, fileoff));
                // The sections are inside the command (A-2): more than its size holds is not a readable library.
                let nsects = u32_at(d, at + 64)? as usize;
                if nsects.checked_mul(80)?.checked_add(72)? > size {
                    return None;
                }
                for s in 0..nsects {
                    let h = at + 72 + s * 80;
                    let size = u64_at(d, h + 40)?;
                    let kind = u32_at(d, h + 64)? & 0xff;
                    let list = match kind {
                        0x9 | 0x16 => &mut lib.runs_at_load,
                        0xa => &mut lib.runs_at_close,
                        _ => continue,
                    };
                    if size != 0 {
                        let sect = String::from_utf8_lossy(d.get(h..h + 16)?).trim_end_matches('\0').to_string();
                        list.push(format!("{name},{sect}"));
                    }
                }
            }
            0x1a => lib.runs_at_load.push("LC_ROUTINES_64".to_string()),
            0x2 => symtab = Some((u32_at(d, at + 8)? as usize, u32_at(d, at + 12)? as usize, u32_at(d, at + 16)? as usize)),
            0xc | 0x8000_0018 | 0x8000_001f | 0x20 => {
                let name_off = u32_at(d, at + 8)? as usize;
                lib.needed.push(cstr(d, at + name_off, 1024)?);
            }
            0x8000_0034 => chained_at = Some(u32_at(d, at + 8)? as usize),
            _ => {}
        }
        at = at.checked_add(size)?;
    }
    // Addresses are read relative to the base, like the other formats.
    for m in &mut lib.maps {
        m.0 = m.0.wrapping_sub(lib.base);
    }
    if let Some(off) = chained_at {
        // dyld_chained_fixups_header: starts_offset at +4; dyld_chained_starts_in_image: seg_count, then offsets to
        // each dyld_chained_starts_in_segment, whose pointer_format is at +6. One format is used by all segments of
        // an arm64 or x86-64 library.
        let starts = off + u32_at(d, off + 4)? as usize;
        let segs = u32_at(d, starts)? as usize;
        for s in 0..segs {
            let seg_off = u32_at(d, starts + 4 + s * 4)? as usize;
            if seg_off != 0 {
                lib.chained = Some(u16_at(d, starts + seg_off + 6)?);
                break;
            }
        }
    }
    if let Some((symoff, nsyms, stroff)) = symtab {
        for i in 0..nsyms {
            let s = symoff.checked_add(i * 16)?;
            let strx = u32_at(d, s)? as usize;
            let n_type = *d.get(s + 4)?;
            let n_desc = u16_at(d, s + 6)?;
            let value = u64_at(d, s + 8)?;
            if n_type & 0xe0 != 0 || n_type & 0x01 == 0 {
                continue; // debugging symbols and locals
            }
            let name = cstr(d, stroff.checked_add(strx)?, 4096)?;
            let name = name.strip_prefix('_').map(str::to_string).unwrap_or(name);
            match n_type & 0x0e {
                0x0e => {
                    lib.exports.insert(name, value.wrapping_sub(lib.base));
                }
                0x00 => {
                    let ordinal = (n_desc >> 8) as usize;
                    let library = match ordinal {
                        1..=0xfd => lib.needed.get(ordinal - 1).cloned().unwrap_or_default(),
                        _ => String::new(),
                    };
                    lib.imports.push(Import { library, symbol: name });
                }
                _ => {}
            }
        }
    }
    Some(lib)
}

/// `luma_mod` as the loader reads it (luma.h, 64-bit layout): LumaModInfo and each LumaImport.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ModInfo {
    pub size: u32,
    pub api: u32,
    pub imports: Vec<ImportRow>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImportRow {
    pub kind: u32,
    pub flags: u32,
    pub name: String,
    pub signature: Option<String>,
    pub hook_kind: u32,
}

pub const MOD_INFO_SIZE: u32 = 40;
const IMPORT_SIZE: u64 = 56;
pub const MAX_IMPORTS: u32 = 65_536;

/// Reads `luma_mod`; Err is a phrase saying what in it cannot be read.
pub fn mod_info(lib: &Library, at: u64) -> Result<ModInfo, &'static str> {
    let size = lib.u32_at_address(at).ok_or("Lib.InfoUnreadable")?;
    let api = lib.u32_at_address(at + 4).ok_or("Lib.InfoUnreadable")?;
    if size < MOD_INFO_SIZE {
        return Err("Lib.InfoTooSmall");
    }
    let count = lib.u32_at_address(at + 16).ok_or("Lib.InfoUnreadable")?;
    if count > MAX_IMPORTS {
        return Err("Lib.TooManyImports");
    }
    let mut imports = Vec::new();
    if count > 0 {
        let list = lib.pointer_at(at + 8).ok_or("Lib.ImportsUnreadable")?;
        for i in 0..count as u64 {
            let row = list + i * IMPORT_SIZE;
            let kind = lib.u32_at_address(row).ok_or("Lib.ImportsUnreadable")?;
            let flags = lib.u32_at_address(row + 4).ok_or("Lib.ImportsUnreadable")?;
            let name_at = lib.pointer_at(row + 8).ok_or("Lib.ImportsUnreadable")?;
            let name = lib.string_at(name_at, 1024).ok_or("Lib.ImportsUnreadable")?;
            // A null signature is a word of zero that no fixup touches.
            let signature = match lib.pointer_at(row + 16) {
                Some(0) | None => None,
                Some(p) => Some(lib.string_at(p, 1024).ok_or("Lib.ImportsUnreadable")?),
            };
            let hook_kind = lib.u32_at_address(row + 40).ok_or("Lib.ImportsUnreadable")?;
            imports.push(ImportRow { kind, flags, name, signature, hook_kind });
        }
    }
    Ok(ModInfo { size, api, imports })
}
