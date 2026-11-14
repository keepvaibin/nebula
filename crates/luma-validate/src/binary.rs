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
    /// Mach-O: the system it is built for, as LC_BUILD_VERSION's platform numbers it (1 macOS, 2 iOS, 7 the iOS
    /// simulator...; LC_VERSION_MIN_MACOSX gives 1, LC_VERSION_MIN_IPHONEOS 2, LC_VERSION_MIN_TVOS 3,
    /// LC_VERSION_MIN_WATCHOS 4); None when it says none.
    pub apple_platform: Option<u32>,
    /// Mach-O: LC_ENCRYPTION_INFO or LC_ENCRYPTION_INFO_64 with a non-zero cryptid (the store's encryption).
    pub encrypted: bool,
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

// Every offset, size and count read from the file goes through these: a sum or product past usize, or a range past
// the file's end, is None, which the reader gives as the library not reading (its format's phrase, L502).

/// `base + off`.
fn ad(base: usize, off: usize) -> Option<usize> {
    base.checked_add(off)
}
/// The `i`th record of `step` bytes from `base`.
fn nth(base: usize, i: usize, step: usize) -> Option<usize> {
    base.checked_add(i.checked_mul(step)?)
}
/// A value of the file as a place or a count in memory.
fn us(v: u64) -> Option<usize> {
    usize::try_from(v).ok()
}
/// `len` bytes at `at + off`.
fn bytes_at(d: &[u8], at: usize, off: usize, len: usize) -> Option<&[u8]> {
    let start = ad(at, off)?;
    d.get(start..ad(start, len)?)
}
fn u8_at(d: &[u8], at: usize, off: usize) -> Option<u8> {
    d.get(ad(at, off)?).copied()
}
fn u16_at(d: &[u8], at: usize, off: usize) -> Option<u16> {
    Some(u16::from_le_bytes(bytes_at(d, at, off, 2)?.try_into().ok()?))
}
fn u32_at(d: &[u8], at: usize, off: usize) -> Option<u32> {
    Some(u32::from_le_bytes(bytes_at(d, at, off, 4)?.try_into().ok()?))
}
fn u64_at(d: &[u8], at: usize, off: usize) -> Option<u64> {
    Some(u64::from_le_bytes(bytes_at(d, at, off, 8)?.try_into().ok()?))
}
/// A 32-bit field as a place or count.
fn u32z(d: &[u8], at: usize, off: usize) -> Option<usize> {
    us(u64::from(u32_at(d, at, off)?))
}
/// A 16-bit field as a place or count.
fn u16z(d: &[u8], at: usize, off: usize) -> Option<usize> {
    us(u64::from(u16_at(d, at, off)?))
}
/// A 64-bit field as a place or count.
fn u64z(d: &[u8], at: usize, off: usize) -> Option<usize> {
    us(u64_at(d, at, off)?)
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
            .and_then(|&(start, _, off)| us(off.checked_add(address - start)?))
    }
    pub fn u32_at_address(&self, address: u64) -> Option<u32> {
        u32_at(self.data, self.offset_of(address, 4)?, 0)
    }
    /// A pointer stored at `address`, as the loader leaves it, as an address of this library (base subtracted).
    pub fn pointer_at(&self, address: u64) -> Option<u64> {
        if !self.is_64 {
            return None;
        }
        if let Some(&v) = self.fixups.get(&address) {
            return Some(v);
        }
        let raw = u64_at(self.data, self.offset_of(address, 8)?, 0)?;
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
    let pe = u32z(d, 0x3c, 0)?;
    if bytes_at(d, pe, 0, 4)? != b"PE\0\0" {
        return None;
    }
    let coff = ad(pe, 4)?;
    let machine = match u16_at(d, coff, 0)? {
        0x8664 => Machine::X64,
        0xaa64 => Machine::Arm64,
        m => Machine::Other(m as u32),
    };
    let sections = u16z(d, coff, 2)?;
    let opt_size = u16z(d, coff, 16)?;
    let characteristics = u16_at(d, coff, 18)?;
    let opt = ad(coff, 20)?;
    let is_64 = u16_at(d, opt, 0)? == 0x20b;
    let (base, dirs) = if is_64 { (u64_at(d, opt, 24)?, ad(opt, 112)?) } else { (u64::from(u32_at(d, opt, 28)?), ad(opt, 96)?) };
    let dir_count = u32z(d, opt, if is_64 { 108 } else { 92 })?;
    let dir = |i: usize| -> Option<(u32, u32)> {
        if i >= dir_count {
            return Some((0, 0));
        }
        let at = nth(dirs, i, 8)?;
        Some((u32_at(d, at, 0)?, u32_at(d, at, 4)?))
    };
    let table = ad(opt, opt_size)?;
    let mut maps = Vec::new();
    for s in 0..sections {
        let h = nth(table, s, 40)?;
        let vsize = u64::from(u32_at(d, h, 8)?);
        let va = u64::from(u32_at(d, h, 12)?);
        let raw_size = u64::from(u32_at(d, h, 16)?);
        let raw_at = u64::from(u32_at(d, h, 20)?);
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
        apple_platform: None,
        encrypted: false,
        data: d,
        maps,
        fixups: HashMap::new(),
        chained: None,
        base,
        is_64,
    };
    if u32_at(d, opt, 16)? != 0 {
        lib.runs_at_load.push("AddressOfEntryPoint".to_string());
    }
    // TLS (A-3): the data alone runs nothing; the callbacks run at load and close. IMAGE_TLS_DIRECTORY holds
    // AddressOfCallBacks, an address (ImageBase added) at +24 (PE32+) or +12 (PE32), of an array that ends with a null.
    let (tls, _) = dir(9)?;
    if tls != 0 {
        let width: usize = if is_64 { 8 } else { 4 };
        let word = |at: usize, off: usize| if is_64 { u64_at(d, at, off) } else { u32_at(d, at, off).map(u64::from) };
        let callbacks = word(lib.offset_of(u64::from(tls), 4 * width as u64)?, 3 * width)?;
        if callbacks != 0 {
            let first = word(lib.offset_of(callbacks.checked_sub(base)?, width as u64)?, 0)?;
            if first != 0 {
                lib.runs_at_load.push("TLS callbacks (IMAGE_TLS_DIRECTORY.AddressOfCallBacks)".to_string());
            }
        }
    }
    let off = |lib: &Library, rva: u64, len: u64| lib.offset_of(rva, len);
    // Exports.
    let (export_rva, export_size) = dir(0)?;
    if export_rva != 0 && export_size != 0 {
        let e = off(&lib, u64::from(export_rva), 40)?;
        let functions = u64::from(u32_at(d, e, 28)?);
        let name_count = u32z(d, e, 24)?;
        let names = u64::from(u32_at(d, e, 32)?);
        let ordinals = u64::from(u32_at(d, e, 36)?);
        for i in 0..name_count {
            let i64_ = i as u64;
            let name_rva = u32_at(d, off(&lib, names.checked_add(i64_.checked_mul(4)?)?, 4)?, 0)?;
            let name = cstr(d, off(&lib, u64::from(name_rva), 1)?, 4096)?;
            let ordinal = u64::from(u16_at(d, off(&lib, ordinals.checked_add(i64_.checked_mul(2)?)?, 2)?, 0)?);
            let rva = u32_at(d, off(&lib, functions.checked_add(ordinal.checked_mul(4)?)?, 4)?, 0)?;
            lib.exports.insert(name, u64::from(rva));
        }
    }
    // Imports (directory 1) and delay imports (13): each library's name and its functions by name or ordinal.
    for (index, entry_size, name_field, thunk_field) in [(1usize, 20u64, 12usize, 0usize), (13, 32, 4, 16)] {
        let (rva, _) = dir(index)?;
        if rva == 0 {
            continue;
        }
        for n in 0..4096u64 {
            let at = off(&lib, u64::from(rva).checked_add(n * entry_size)?, entry_size)?;
            if bytes_at(d, at, 0, entry_size as usize)?.iter().all(|&b| b == 0) {
                break;
            }
            let library = cstr(d, off(&lib, u64::from(u32_at(d, at, name_field)?), 1)?, 512)?;
            let mut thunks = u32_at(d, at, thunk_field)?;
            if thunks == 0 && index == 1 {
                thunks = u32_at(d, at, 16)?;
            }
            lib.needed.push(library.clone());
            let width: u64 = if is_64 { 8 } else { 4 };
            for k in 0..65536u64 {
                let t_at = off(&lib, u64::from(thunks).checked_add(k * width)?, width)?;
                let value = if is_64 { u64_at(d, t_at, 0)? } else { u64::from(u32_at(d, t_at, 0)?) };
                if value == 0 {
                    break;
                }
                let by_ordinal = if is_64 { value >> 63 != 0 } else { value >> 31 != 0 };
                let symbol = if by_ordinal {
                    format!("#{}", value & 0xffff)
                } else {
                    cstr(d, off(&lib, (value & 0x7fff_ffff).checked_add(2)?, 1)?, 4096)?
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
    let kind = u16_at(d, 16, 0)?;
    let machine = match u16_at(d, 18, 0)? {
        62 => Machine::X64,
        183 => Machine::Arm64,
        m => Machine::Other(m as u32),
    };
    let ph_at = u64z(d, 32, 0)?;
    let sh_at = u64z(d, 40, 0)?;
    let ph_size = u16z(d, 54, 0)?;
    let ph_count = u16z(d, 56, 0)?;
    let sh_size = u16z(d, 58, 0)?;
    let sh_count = u16z(d, 60, 0)?;
    let mut maps = Vec::new();
    let mut dynamic = None;
    for i in 0..ph_count {
        let h = nth(ph_at, i, ph_size)?;
        let kind = u32_at(d, h, 0)?;
        let offset = u64_at(d, h, 8)?;
        let vaddr = u64_at(d, h, 16)?;
        let filesz = u64_at(d, h, 32)?;
        if kind == 1 {
            maps.push((vaddr, filesz, offset));
        } else if kind == 2 {
            dynamic = Some((us(offset)?, us(filesz)?));
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
        apple_platform: None,
        encrypted: false,
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
        let at = nth(dyn_at, i, 16)?;
        let tag = u64_at(d, at, 0)?;
        let value = u64_at(d, at, 8)?;
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
    let strsz = us(*tags.get(&10).unwrap_or(&0))?;
    let string = |o: u64| -> Option<String> {
        let o = us(o)?;
        if strsz != 0 && o >= strsz {
            return None;
        }
        cstr(d, ad(strtab, o)?, 4096)
    };
    for o in needed_offsets {
        lib.needed.push(string(o)?);
    }
    // The dynamic symbols: the count from the section headers (.dynsym), else from DT_HASH.
    let symtab = tags.get(&6).copied();
    let mut count = 0usize;
    for i in 0..sh_count {
        let h = nth(sh_at, i, sh_size)?;
        if u32_at(d, h, 4)? == 11 {
            let size = u64z(d, h, 32)?;
            let entsize = u64z(d, h, 56)?.max(1);
            count = size / entsize;
        }
    }
    if count == 0 {
        if let Some(&hash) = tags.get(&4) {
            count = u32z(d, lib.offset_of(hash, 8)?, 4)?;
        }
    }
    let mut symbols: Vec<(String, u64, bool)> = Vec::new();
    if let Some(symtab) = symtab {
        let base = lib.offset_of(symtab, 24)?;
        for i in 0..count {
            let s = nth(base, i, 24)?;
            let name = string(u64::from(u32_at(d, s, 0)?))?;
            let info = u8_at(d, s, 4)?;
            let other = u8_at(d, s, 5)?;
            let shndx = u16_at(d, s, 6)?;
            let value = u64_at(d, s, 8)?;
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
        for i in 0..us(size)? / 24 {
            let r = nth(start, i, 24)?;
            let offset = u64_at(d, r, 0)?;
            let info = u64_at(d, r, 8)?;
            let addend = u64_at(d, r, 16)?;
            let (kind, sym) = ((info & 0xffff_ffff) as u32, us(info >> 32)?);
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
            for i in 0..us(size)? / 8 {
                let entry = u64_at(d, nth(start, i, 8)?, 0)?;
                if entry & 1 == 0 {
                    // An address: the word there is relocated by the base (0 here), so its value stays.
                    if let Some(v) = lib.offset_of(entry, 8).and_then(|o| u64_at(d, o, 0)) {
                        lib.fixups.insert(entry, v);
                    }
                    next = entry.checked_add(8)?;
                } else {
                    let mut bits = entry >> 1;
                    let mut where_ = next;
                    while bits != 0 {
                        if bits & 1 != 0 {
                            if let Some(v) = lib.offset_of(where_, 8).and_then(|o| u64_at(d, o, 0)) {
                                lib.fixups.insert(where_, v);
                            }
                        }
                        bits >>= 1;
                        where_ = where_.checked_add(8)?;
                    }
                    next = next.checked_add(63 * 8)?;
                }
            }
        }
    }
    Some(lib)
}

/// The Mach-O load commands that load a library: LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB,
/// LC_LAZY_LOAD_DYLIB and LC_LOAD_UPWARD_DYLIB.
const MACHO_LOADS: [u32; 5] = [0xc, 0x8000_0018, 0x8000_001f, 0x20, 0x8000_0023];

#[derive(Clone, Copy, PartialEq, Eq)]
enum BindTable {
    Bind,
    /// Weak definitions coalesced by name across every image: no library ordinal; a symbol flagged as this library's
    /// own strong definition binds nothing.
    Weak,
    /// Entries end with BIND_OPCODE_DONE each.
    Lazy,
}

/// The symbols an LC_DYLD_INFO bind table binds, each with the library ordinal it is bound from (the bind opcodes of
/// mach-o/loader.h). None: an opcode or a number that does not read.
fn bind_symbols(table: &[u8], kind: BindTable) -> Option<Vec<(i64, String)>> {
    fn uleb(table: &[u8], i: &mut usize) -> Option<u64> {
        let mut value = 0u64;
        let mut shift = 0u32;
        loop {
            let byte = *table.get(*i)?;
            *i = ad(*i, 1)?;
            if shift >= 64 {
                return None;
            }
            value |= u64::from(byte & 0x7f) << shift;
            if byte & 0x80 == 0 {
                return Some(value);
            }
            shift += 7;
        }
    }
    let mut out = Vec::new();
    let mut i = 0usize;
    let mut ordinal = 0i64;
    let mut symbol = String::new();
    let mut own = false;
    while i < table.len() {
        let byte = table[i];
        i = ad(i, 1)?;
        let imm = byte & 0x0f;
        let mut bind = false;
        match byte & 0xf0 {
            0x00 if kind == BindTable::Lazy => {}
            0x00 => break,
            0x10 => ordinal = i64::from(imm),
            0x20 => ordinal = i64::try_from(uleb(table, &mut i)?).ok()?,
            // BIND_OPCODE_SET_DYLIB_SPECIAL_IMM: 0 itself, else the immediate sign-extended (-1 the main program, -2
            // a flat lookup, -3 a weak lookup).
            0x30 => ordinal = if imm == 0 { 0 } else { i64::from(imm) - 0x10 },
            0x40 => {
                own = imm & 0x8 != 0; // BIND_SYMBOL_FLAGS_NON_WEAK_DEFINITION
                let rest = table.get(i..)?;
                let end = rest.iter().position(|&b| b == 0)?;
                symbol = String::from_utf8(rest[..end].to_vec()).ok()?;
                i = ad(i, ad(end, 1)?)?;
            }
            0x50 => {}
            0x60 | 0x70 | 0x80 => {
                uleb(table, &mut i)?; // an addend (SLEB, read as its bytes), a segment offset, an address step
            }
            0x90 | 0xb0 => bind = true,
            0xa0 => {
                uleb(table, &mut i)?;
                bind = true;
            }
            0xc0 => {
                uleb(table, &mut i)?;
                uleb(table, &mut i)?;
                bind = true;
            }
            // BIND_OPCODE_THREADED: SET_BIND_ORDINAL_TABLE_SIZE_ULEB (0), APPLY (1).
            0xd0 => match imm {
                0 => {
                    uleb(table, &mut i)?;
                }
                1 => {}
                _ => return None,
            },
            _ => return None,
        }
        if bind && !symbol.is_empty() && !(kind == BindTable::Weak && own) {
            out.push((if kind == BindTable::Weak { -3 } else { ordinal }, symbol.clone()));
        }
    }
    Some(out)
}

/// A symbol a Mach-O binds, from the library its ordinal names (1 the first library loaded); 0 is the library itself
/// and binds nothing from outside; a special ordinal, or one past the libraries loaded (as the symbol table's are
/// read), names no library.
fn macho_import(lib: &mut Library, ordinal: i64, symbol: String) -> Option<()> {
    let library = match ordinal {
        0 => return Some(()),
        o if o < 0 => String::new(),
        o => lib.needed.get(us(o.unsigned_abs())?.checked_sub(1)?).cloned().unwrap_or_default(),
    };
    let symbol = symbol.strip_prefix('_').map(str::to_string).unwrap_or(symbol);
    let import = Import { library, symbol };
    if !lib.imports.contains(&import) {
        lib.imports.push(import);
    }
    Some(())
}

fn read_macho(d: &[u8]) -> Option<Library<'_>> {
    let cpu = u32_at(d, 4, 0)?;
    let machine = match cpu {
        0x0100_0007 => Machine::X64,
        0x0100_000c => Machine::Arm64,
        m => Machine::Other(m),
    };
    let filetype = u32_at(d, 12, 0)?;
    let ncmds = u32z(d, 16, 0)?;
    let mut lib = Library {
        format: Format::MachO,
        machine,
        shared: filetype == 6,
        exports: HashMap::new(),
        imports: Vec::new(),
        needed: Vec::new(),
        runs_at_load: Vec::new(),
        runs_at_close: Vec::new(),
        apple_platform: None,
        encrypted: false,
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
    let mut bind_tables = Vec::new();
    for _ in 0..ncmds {
        let cmd = u32_at(d, at, 0)?;
        let size = u32z(d, at, 4)?;
        if size < 8 {
            return None;
        }
        // A command reaching past the file is not a readable library.
        bytes_at(d, at, 0, size)?;
        match cmd {
            0x19 => {
                let segname = bytes_at(d, at, 8, 16)?;
                let name = String::from_utf8_lossy(segname).trim_end_matches('\0').to_string();
                let vmaddr = u64_at(d, at, 24)?;
                let fileoff = u64_at(d, at, 40)?;
                let filesize = u64_at(d, at, 48)?;
                if name == "__TEXT" {
                    lib.base = vmaddr;
                }
                lib.maps.push((vmaddr, filesize, fileoff));
                // The sections are inside the command (A-2): more than its size holds is not a readable library.
                let nsects = u32z(d, at, 64)?;
                if nsects.checked_mul(80)?.checked_add(72)? > size {
                    return None;
                }
                for s in 0..nsects {
                    let h = nth(ad(at, 72)?, s, 80)?;
                    let size = u64_at(d, h, 40)?;
                    let kind = u32_at(d, h, 64)? & 0xff;
                    let list = match kind {
                        0x9 | 0x16 => &mut lib.runs_at_load,
                        0xa => &mut lib.runs_at_close,
                        _ => continue,
                    };
                    if size != 0 {
                        let sect = String::from_utf8_lossy(bytes_at(d, h, 0, 16)?).trim_end_matches('\0').to_string();
                        list.push(format!("{name},{sect}"));
                    }
                }
            }
            0x1a => lib.runs_at_load.push("LC_ROUTINES_64".to_string()),
            0x32 => lib.apple_platform = Some(u32_at(d, at, 8)?),
            0x24 => lib.apple_platform = lib.apple_platform.or(Some(1)),
            0x25 => lib.apple_platform = lib.apple_platform.or(Some(2)),
            0x2f => lib.apple_platform = lib.apple_platform.or(Some(3)),
            0x30 => lib.apple_platform = lib.apple_platform.or(Some(4)),
            0x21 | 0x2c => lib.encrypted |= u32_at(d, at, 16)? != 0,
            0x2 => symtab = Some((u32z(d, at, 8)?, u32z(d, at, 12)?, u32z(d, at, 16)?)),
            c if MACHO_LOADS.contains(&c) => {
                let name_off = u32z(d, at, 8)?;
                lib.needed.push(cstr(d, ad(at, name_off)?, 1024)?);
            }
            // LC_DYLD_INFO and LC_DYLD_INFO_ONLY: the bind, weak bind and lazy bind tables (offset and size at +16,
            // +24, +32), read once every library the ordinals name is known.
            0x22 | 0x8000_0022 => {
                for (field, kind) in [(16usize, BindTable::Bind), (24, BindTable::Weak), (32, BindTable::Lazy)] {
                    let size = u32z(d, at, ad(field, 4)?)?;
                    if size != 0 {
                        bind_tables.push((bytes_at(d, u32z(d, at, field)?, 0, size)?, kind));
                    }
                }
            }
            0x8000_0034 => chained_at = Some(u32z(d, at, 8)?),
            _ => {}
        }
        at = ad(at, size)?;
    }
    // Addresses are read relative to the base, like the other formats.
    for m in &mut lib.maps {
        m.0 = m.0.wrapping_sub(lib.base);
    }
    // What the loader binds from another library: each bind table's symbols, and the chained fixups' imports. The
    // symbol table lists them too when it is whole; these are what dyld reads.
    for (table, kind) in bind_tables {
        for (ordinal, symbol) in bind_symbols(table, kind)? {
            macho_import(&mut lib, ordinal, symbol)?;
        }
    }
    if let Some(off) = chained_at {
        // dyld_chained_fixups_header: starts_offset +4, imports_offset +8, symbols_offset +12, imports_count +16,
        // imports_format +20 (1 DYLD_CHAINED_IMPORT, 2 _ADDEND, 3 _ADDEND64), symbols_format +24 (0: plain names).
        let imports_at = ad(off, u32z(d, off, 8)?)?;
        let names_at = ad(off, u32z(d, off, 12)?)?;
        let count = u32z(d, off, 16)?;
        let format = u32_at(d, off, 20)?;
        if count != 0 && u32_at(d, off, 24)? != 0 {
            return None; // compressed names
        }
        for i in 0..count {
            // The library ordinal (8 or 16 bits; the top three values are the special ordinals -3..-1) and the
            // name's offset.
            let (ordinal, name_off) = match format {
                1 | 2 => {
                    let v = u32_at(d, nth(imports_at, i, if format == 1 { 4 } else { 8 })?, 0)?;
                    let o = v & 0xff;
                    (if o >= 0xfd { i64::from(o) - 0x100 } else { i64::from(o) }, us(u64::from(v >> 9))?)
                }
                3 => {
                    let v = u64_at(d, nth(imports_at, i, 16)?, 0)?;
                    let o = v & 0xffff;
                    (if o >= 0xfffd { o as i64 - 0x1_0000 } else { o as i64 }, us(v >> 32)?)
                }
                _ => return None,
            };
            let symbol = cstr(d, ad(names_at, name_off)?, 4096)?;
            macho_import(&mut lib, ordinal, symbol)?;
        }
        // dyld_chained_starts_in_image: seg_count, then offsets to each dyld_chained_starts_in_segment, whose
        // pointer_format is at +6. One format is used by all segments of an arm64 or x86-64 library.
        let starts = ad(off, u32z(d, off, 4)?)?;
        let segs = u32z(d, starts, 0)?;
        for s in 0..segs {
            let seg_off = u32z(d, nth(ad(starts, 4)?, s, 4)?, 0)?;
            if seg_off != 0 {
                lib.chained = Some(u16_at(d, ad(starts, seg_off)?, 6)?);
                break;
            }
        }
    }
    if let Some((symoff, nsyms, stroff)) = symtab {
        for i in 0..nsyms {
            let s = nth(symoff, i, 16)?;
            let strx = u32z(d, s, 0)?;
            let n_type = u8_at(d, s, 4)?;
            let n_desc = u16_at(d, s, 6)?;
            let value = u64_at(d, s, 8)?;
            if n_type & 0xe0 != 0 || n_type & 0x01 == 0 {
                continue; // debugging symbols and locals
            }
            let name = cstr(d, ad(stroff, strx)?, 4096)?;
            let name = name.strip_prefix('_').map(str::to_string).unwrap_or(name);
            match n_type & 0x0e {
                0x0e => {
                    lib.exports.insert(name, value.wrapping_sub(lib.base));
                }
                0x00 => {
                    let ordinal = usize::from(n_desc >> 8);
                    let library = match ordinal {
                        1..=0xfd => lib.needed.get(ordinal - 1).cloned().unwrap_or_default(),
                        _ => String::new(),
                    };
                    let import = Import { library, symbol: name };
                    if !lib.imports.contains(&import) {
                        lib.imports.push(import);
                    }
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

/// Reads `luma_mod`; Err is a phrase saying what in it cannot be read. An address past the 64-bit range is a field that
/// cannot be read.
pub fn mod_info(lib: &Library, at: u64) -> Result<ModInfo, &'static str> {
    let field = |base: u64, off: u64, why: &'static str| base.checked_add(off).ok_or(why);
    let info = "Lib.InfoUnreadable";
    let size = lib.u32_at_address(at).ok_or(info)?;
    let api = lib.u32_at_address(field(at, 4, info)?).ok_or(info)?;
    if size < MOD_INFO_SIZE {
        return Err("Lib.InfoTooSmall");
    }
    let count = lib.u32_at_address(field(at, 16, info)?).ok_or(info)?;
    if count > MAX_IMPORTS {
        return Err("Lib.TooManyImports");
    }
    let mut imports = Vec::new();
    if count > 0 {
        let bad = "Lib.ImportsUnreadable";
        let list = lib.pointer_at(field(at, 8, info)?).ok_or(bad)?;
        for i in 0..u64::from(count) {
            let row = field(list, i * IMPORT_SIZE, bad)?;
            let kind = lib.u32_at_address(row).ok_or(bad)?;
            let flags = lib.u32_at_address(field(row, 4, bad)?).ok_or(bad)?;
            let name_at = lib.pointer_at(field(row, 8, bad)?).ok_or(bad)?;
            let name = lib.string_at(name_at, 1024).ok_or(bad)?;
            // A null signature is a word of zero that no fixup touches.
            let signature = match lib.pointer_at(field(row, 16, bad)?) {
                Some(0) | None => None,
                Some(p) => Some(lib.string_at(p, 1024).ok_or(bad)?),
            };
            let hook_kind = lib.u32_at_address(field(row, 40, bad)?).ok_or(bad)?;
            imports.push(ImportRow { kind, flags, name, signature, hook_kind });
        }
    }
    Ok(ModInfo { size, api, imports })
}
