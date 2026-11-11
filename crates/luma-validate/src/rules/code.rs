//! L500-L512: a mod's code (luma-format 6; F-1).

use super::{p, t, Ctx};
use crate::binary::{self, Format, Machine};
use crate::zip::EntryKind;
use std::collections::BTreeMap;

pub const MAX_LIBRARY: u64 = 1 << 30;
const IMPORT_KINDS: u32 = 5; // LUMA_IMPORT_FUNCTION..LUMA_IMPORT_HOOK
const HOOK_KINDS: u32 = 4; // LUMA_BEFORE..LUMA_REPLACE
const LUMA_OPTIONAL: u32 = 1;

/// The entry library's file name and the format and machine each platform loads.
fn platform(name: &str) -> Option<(&'static str, Format, Machine)> {
    Some(match name {
        "windows-x64" => ("mod.dll", Format::Pe, Machine::X64),
        "windows-arm64" => ("mod.dll", Format::Pe, Machine::Arm64),
        "linux-x64" => ("mod.so", Format::Elf, Machine::X64),
        "linux-arm64" => ("mod.so", Format::Elf, Machine::Arm64),
        "android-arm64" => ("mod.so", Format::Elf, Machine::Arm64),
        "android-x64" => ("mod.so", Format::Elf, Machine::X64),
        "macos-arm64" => ("mod.dylib", Format::MachO, Machine::Arm64),
        "macos-x64" => ("mod.dylib", Format::MachO, Machine::X64),
        "ios-arm64" => ("mod.dylib", Format::MachO, Machine::Arm64),
        _ => return None,
    })
}

/// A system library's name as an argument; an ELF import names none (the loader finds it in any library loaded).
fn library_arg(library: &str) -> crate::Arg {
    if library.is_empty() { p("Lib.AnyLibrary") } else { t(library) }
}

fn kind_phrase(format: Format, machine: Machine) -> String {
    let f = match format {
        Format::Pe => "PE",
        Format::Elf => "ELF",
        Format::MachO => "Mach-O",
    };
    let m = match machine {
        Machine::X64 => "x86-64".to_string(),
        Machine::Arm64 => "arm64".to_string(),
        Machine::Other(n) => format!("machine {n:#x}"),
    };
    format!("{f} {m}")
}

pub(crate) fn check(ctx: &mut Ctx) {
    let has_folder = ctx.m.items.iter().any(|i| i.name.starts_with("code/") || i.name == "code");
    let has_key = ctx.facts.has_code_key;
    if has_folder && !has_key {
        ctx.add(500, "luma.json", None, None, vec![("why", p("Code.FolderWithoutKey"))]);
    } else if has_key && !has_folder {
        ctx.add(500, "luma.json", None, None, vec![("why", p("Code.KeyWithoutFolder"))]);
    }
    let listed = ctx.facts.code_platforms.clone();
    let root = ctx.manifest.clone();
    let libraries: BTreeMap<String, Vec<String>> = root
        .as_ref()
        .and_then(|r| r.get("code"))
        .and_then(|c| c.get("libraries"))
        .map(|l| l.members().iter().map(|m| (m.key.clone(), m.value.items().iter().filter_map(|x| x.as_str().map(str::to_string)).collect())).collect())
        .unwrap_or_default();

    // The folders under code/: each a known platform, listed; nothing else under code/.
    let mut found: BTreeMap<String, Vec<String>> = BTreeMap::new();
    for item in ctx.m.items.clone() {
        let Some(rest) = item.name.strip_prefix("code/") else { continue };
        let mut parts = rest.splitn(2, '/');
        let folder = parts.next().unwrap_or("").to_string();
        let file = parts.next();
        if platform(&folder).is_none() {
            ctx.add(501, &item.name, None, None, vec![("found", t(folder.clone())), ("why", p("Code.UnknownPlatform"))]);
            continue;
        }
        match file {
            Some(f) if item.kind == EntryKind::File => {
                if f.contains('/') {
                    ctx.add(509, &item.name, None, None, vec![("why", p("Code.Nested"))]);
                } else {
                    found.entry(folder).or_default().push(f.to_string());
                }
            }
            None if item.kind == EntryKind::File => ctx.add(501, &item.name, None, None, vec![("found", t(folder)), ("why", p("Code.NotAFolder"))]),
            _ => {
                found.entry(folder).or_default();
            }
        }
    }
    let mut seen_folders: Vec<String> = found.keys().cloned().collect();
    seen_folders.dedup();
    for folder in &seen_folders {
        if !listed.contains(folder) {
            ctx.add(501, &format!("code/{folder}/"), None, None, vec![("found", t(folder.clone())), ("why", p("Code.NotListed"))]);
        }
    }
    let permissions = ctx.facts.permissions.clone();
    for plat in &listed {
        let Some((entry_name, format, machine)) = platform(plat) else { continue };
        let files = found.get(plat).cloned().unwrap_or_default();
        if !files.iter().any(|f| f == entry_name) {
            ctx.add(501, "luma.json", None, None, vec![("found", t(plat.clone())), ("why", p("Code.NoEntryLibrary")), ("expected", t(format!("code/{plat}/{entry_name}")))]);
        }
        let extras = libraries.get(plat).cloned().unwrap_or_default();
        for f in &files {
            if f != entry_name && !extras.contains(f) {
                ctx.add(509, &format!("code/{plat}/{f}"), None, None, vec![("why", p("Code.NotNamed"))]);
            }
        }
        for extra in &extras {
            if !files.contains(extra) {
                ctx.add(501, "luma.json", None, None, vec![("found", t(plat.clone())), ("why", p("Code.ExtraMissing")), ("expected", t(format!("code/{plat}/{extra}")))]);
            }
        }
        if !extras.is_empty() && !permissions.contains("native") {
            ctx.add(509, "luma.json", None, None, vec![("why", p("Code.ExtraWithoutNative")), ("found", t(extras.join(", ")))]);
        }
        if plat == "ios-arm64" && !extras.is_empty() {
            // Which extra libraries iOS signing allows is plan 8.3's, not written yet.
            ctx.not_checked(511, "Missing.SigningRules");
        }
        for f in files.iter().filter(|f| *f == entry_name || extras.contains(f)) {
            library(ctx, plat, f, f == entry_name, format, machine, &extras);
        }
    }
    // L507: the platforms checked for, where the mod's code does not run (the mod is off there).
    if has_key {
        for plat in ctx.input.platforms {
            if !listed.iter().any(|l| l == plat) {
                ctx.add(507, "luma.json", None, None, vec![("platform", t(*plat))]);
            }
        }
    }
}

fn library(ctx: &mut Ctx, plat: &str, file: &str, is_entry: bool, format: Format, machine: Machine, extras: &[String]) {
    let name = format!("code/{plat}/{file}");
    let Some(index) = ctx.m.index_of(&name) else { return };
    let data = match ctx.m.kept.get(&name) {
        Some(d) => d.clone(),
        None => match ctx.m.keep(index, MAX_LIBRARY) {
            Ok(d) => d.to_vec(),
            Err(_) => return, // L105 or L106 already
        },
    };
    let expected = kind_phrase(format, machine);
    let lib = match binary::read(&data) {
        Ok(lib) => lib,
        Err(why) => {
            ctx.add(502, &name, None, None, vec![("expected", t(expected)), ("found", p(why))]);
            return;
        }
    };
    if lib.format != format || lib.machine != machine || !lib.shared {
        let found = if lib.shared { t(kind_phrase(lib.format, lib.machine)) } else { p("Lib.NotShared") };
        ctx.add(502, &name, None, None, vec![("expected", t(expected)), ("found", found), ("kind", t(kind_phrase(lib.format, lib.machine)))]);
        return;
    }
    let is_apple = format == Format::MachO;
    if !is_entry {
        // L510: every system import of an extra library, listed by library.
        let mut by_library: BTreeMap<String, Vec<String>> = BTreeMap::new();
        for i in &lib.imports {
            by_library.entry(i.library.clone()).or_default().push(i.symbol.clone());
        }
        for needed in &lib.needed {
            by_library.entry(needed.clone()).or_default();
        }
        for (library, symbols) in by_library {
            let found = if symbols.is_empty() { p("Lib.NoNames") } else { t(symbols.join(", ")) };
            ctx.add(510, &name, None, None, vec![("found", found), ("library", library_arg(&library))]);
        }
        return;
    }
    // The entry library: luma_mod exported, nothing imported from the system (L503, L504).
    let Some(&info_at) = lib.exports.get("luma_mod") else {
        ctx.add(503, &name, None, None, vec![]);
        return;
    };
    let from_extra = |library: &str| extras.iter().any(|e| e.eq_ignore_ascii_case(library) || library.ends_with(&format!("/{e}")));
    if is_apple {
        // What the linker forces into a -nostdlib dylib is the kernel loader contract's list (1.1), from evidence.
        if lib.imports.iter().any(|i| !from_extra(&i.library)) || lib.needed.iter().any(|n| !from_extra(n)) {
            ctx.not_checked(504, "Missing.LoaderContract");
        }
    } else {
        let mut reported = std::collections::BTreeSet::new();
        for i in &lib.imports {
            if from_extra(&i.library) {
                continue;
            }
            // An ELF symbol names no library: allowed only when an extra library is loaded to give it, which the
            // validator cannot see from here; with no extra library it comes from the system.
            if i.library.is_empty() && !extras.is_empty() {
                continue;
            }
            if reported.insert((i.library.clone(), i.symbol.clone())) {
                ctx.add(504, &name, None, None, vec![("found", t(format!("\"{}\"", i.symbol))), ("library", library_arg(&i.library))]);
            }
        }
        for n in &lib.needed {
            if !from_extra(n) && !lib.imports.iter().any(|i| &i.library == n) {
                ctx.add(504, &name, None, None, vec![("found", p("Lib.WholeLibrary")), ("library", library_arg(n))]);
            }
        }
    }
    let info = match binary::mod_info(&lib, info_at) {
        Ok(info) => info,
        Err(why) => {
            ctx.add(512, &name, None, None, vec![("why", p(why))]);
            return;
        }
    };
    let names = ctx.game().and_then(|g| g.names);
    if let Some(n) = names {
        if info.api > n.api {
            ctx.add(508, &name, None, None, vec![("found", t(info.api.to_string())), ("expected", t(n.api.to_string()))]);
        }
    } else {
        ctx.not_checked(508, "Missing.SelfDescription");
    }
    for (i, row) in info.imports.iter().enumerate() {
        if row.kind >= IMPORT_KINDS || (row.kind == 4 && row.hook_kind >= HOOK_KINDS) || row.name.is_empty() {
            ctx.add(512, &name, None, None, vec![("why", p("Lib.ImportRowBad")), ("row", t(i.to_string()))]);
            continue;
        }
        let Some(n) = names else {
            ctx.not_checked(505, "Missing.SelfDescription");
            continue;
        };
        let optional = row.flags & LUMA_OPTIONAL != 0;
        let (kind, ok) = match row.kind {
            0 | 4 => {
                let rows = n.functions.get(&row.name);
                let matched = rows.and_then(|list| match &row.signature {
                    Some(sig) => list.iter().find(|(s, _)| s == sig),
                    None if list.len() == 1 => list.first(),
                    None => None,
                });
                // A constructor or destructor (`tell`) takes before and after only.
                if row.kind == 4 && row.hook_kind >= 2 && matched.is_some_and(|(_, tell)| *tell) {
                    ctx.add(512, &name, None, None, vec![("why", p("Lib.HookOnConstructor")), ("found", t(row.name.clone()))]);
                    continue;
                }
                (if row.signature.is_some() { "Name.FunctionSig" } else { "Name.Function" }, matched.is_some())
            }
            1 => match &n.fields {
                Some(fields) => (
                    if row.signature.is_some() { "Name.FieldSig" } else { "Name.Field" },
                    fields.get(&row.name).is_some_and(|ty| row.signature.as_ref().map_or(true, |s| s == ty)),
                ),
                None => {
                    ctx.not_checked(505, "Missing.Fields");
                    continue;
                }
            },
            _ => match &n.classes {
                Some(classes) => ("Name.Class", classes.contains(&row.name)),
                None => {
                    ctx.not_checked(505, "Missing.Classes");
                    continue;
                }
            },
        };
        if !ok && !optional {
            ctx.add(505, &name, None, None, vec![("kind", p(kind)), ("found", t(row.name.clone())), ("sig", t(row.signature.clone().unwrap_or_default()))]);
        }
    }
    // Services are asked for by name at run time (`core->service`), not listed in the import rows, so which ones a
    // library uses cannot be read from it yet: L506 waits for a service import kind in luma.h (asked of F).
    ctx.not_checked(506, "Missing.ServiceImports");
}
