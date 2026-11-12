//! L500-L512: a mod's code (luma-format 6; F-1).

use super::{p, t, Ctx};
use crate::binary::{self, Format, Machine};
use crate::zip::EntryKind;
use crate::names::{service_name_ok, service_version};
use super::patches::SEP;
use std::collections::BTreeMap;

pub const MAX_LIBRARY: u64 = 1 << 30;
const IMPORT_KINDS: u32 = 9; // LUMA_IMPORT_FUNCTION..LUMA_IMPORT_HOOK, LUMA_IMPORT_SERVICE, LUMA_IMPORT_MOD_FUNCTION, LUMA_IMPORT_GLOBAL, LUMA_IMPORT_METHOD
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

/// The entry library's path in a mod for a platform.
pub(crate) fn entry_path(plat: &str) -> Option<String> {
    platform(plat).map(|(entry, _, _)| format!("code/{plat}/{entry}"))
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
    // L218: a permission asked for that nothing of the mod needs (services-registry 5.2, a Note): `native` with no extra
    // library; any other when no entry library imports its service (a permissioned service's name is its permission's).
    // Said only when every entry library's imports were read (one that was not is an error already).
    let native_used = libraries.values().any(|l| !l.is_empty());
    let all_read = ctx.facts.entries_read == listed.len();
    for (permission, at, pointer) in ctx.facts.permission_at.clone() {
        let used = if permission == "native" {
            native_used
        } else if all_read {
            ctx.facts.services_imported.contains(&permission)
        } else {
            true
        };
        if !used {
            ctx.add(218, "luma.json", Some(at), Some(&pointer), vec![("permission", t(permission))]);
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
    // Code the system's loader would run before LumaCore's `begin` gives the core (loader contract D2-6): a C++
    // constructor there would find no core, and nothing would say why.
    for what in &lib.runs_at_load {
        ctx.add(509, &name, None, None, vec![("why", p("Lib.RunsAtLoad")), ("found", t(what.clone()))]);
    }
    // And code it would run when it closes the library, after the mod's memory is freed (F's audit of R3-A, A-1).
    for what in &lib.runs_at_close {
        ctx.add(509, &name, None, None, vec![("why", p("Lib.RunsAtClose")), ("found", t(what.clone()))]);
    }
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
    ctx.facts.entries_read += 1;
    let (mut any_service, mut services_unchecked, mut mod_functions) = (false, false, false);
    for (i, row) in info.imports.iter().enumerate() {
        if row.kind >= IMPORT_KINDS || (row.kind == 4 && row.hook_kind >= HOOK_KINDS) || row.name.is_empty() {
            ctx.add(512, &name, None, None, vec![("why", p("Lib.ImportRowBad")), ("row", t(i.to_string()))]);
            continue;
        }
        match row.kind {
            // A service (loader D3-2, services-registry 2): `signature` is "major.minor", bound against the game's
            // catalog (the self-description's `services`, registry 9) when it is given; not checked without it.
            5 => {
                any_service = true;
                let optional = row.flags & LUMA_OPTIONAL != 0;
                let sig = row.signature.clone().unwrap_or_default();
                // Registry 2: a name outside its grammar or a version text other than "<major>.<minor>" is L505 with
                // `sig` as found, whatever the flags (the loader reads the same grammar).
                let Some((major, minor)) = row.signature.as_deref().and_then(service_version).filter(|_| service_name_ok(&row.name)) else {
                    ctx.add(505, &name, None, None, vec![("kind", p("Name.Service")), ("found", t(row.name.clone())), ("sig", t(sig))]);
                    continue;
                };
                ctx.facts.services_imported.insert(row.name.clone());
                if !optional {
                    let required = format!("{plat}{SEP}{}{SEP}{major}.{minor}", row.name);
                    if !ctx.facts.touches.services.contains(&required) {
                        ctx.facts.touches.services.push(required);
                    }
                }
                match names.and_then(|n| n.services.as_ref()) {
                    None => services_unchecked = true,
                    Some(catalog) => match catalog.find(&row.name, major, minor) {
                        // An optional import of a service the game lacks is `none` (registry 2).
                        None if optional => {}
                        None => ctx.add(505, &name, None, None, vec![("kind", p("Name.Service")), ("found", t(row.name.clone())), ("sig", t(sig))]),
                        // Optional or not: without the permission declared it is never granted.
                        Some(service) => {
                            if let Some(permission) = service.permission.clone() {
                                if !ctx.facts.permissions.contains(&permission) {
                                    ctx.add(506, &name, None, None, vec![("service", t(row.name.clone())), ("permission", t(permission))]);
                                }
                            }
                        }
                    },
                }
                continue;
            }
            // Another mod's function (D3-3, lumakit R-1): "<mod id>/<export name>", that mod among `needs` (the kernel
            // binds only from a needed mod that is loaded: L505 with the same phrase). Its export list is the needed
            // mod's library's: said below.
            6 => {
                let needed = row.name.split_once('/').filter(|(m, e)| !m.is_empty() && !e.is_empty()).map(|(m, _)| m.to_string());
                match needed {
                    None => ctx.add(512, &name, None, None, vec![("why", p("Lib.ImportRowBad")), ("row", t(i.to_string()))]),
                    Some(m) if row.flags & LUMA_OPTIONAL == 0 && !ctx.facts.needs.iter().any(|(id, _)| *id == m) => {
                        ctx.add(505, &name, None, None, vec![("kind", p("Name.ModFunction")), ("found", t(row.name.clone())), ("sig", t(row.signature.clone().unwrap_or_default()))]);
                    }
                    Some(_) => mod_functions = true,
                }
                continue;
            }
            _ => {}
        }
        let Some(n) = names else {
            ctx.not_checked(505, "Missing.SelfDescription");
            continue;
        };
        if row.kind == 4 && row.hook_kind == 3 && ctx.input.platforms.contains(&plat) {
            let sig = row.signature.clone().unwrap_or_default();
            ctx.facts.touches.functions.push(format!("{}{}{sig}", row.name, super::patches::SEP));
        }
        let optional = row.flags & LUMA_OPTIONAL != 0;
        let (kind, ok) = match row.kind {
            0 | 4 => {
                let rows = n.functions.get(&row.name);
                let matched = rows.and_then(|list| match &row.signature {
                    Some(sig) => list.iter().find(|(s, _)| s == sig),
                    None if list.len() == 1 => list.first(),
                    None => None,
                });
                // A constexpr function's row is called, never hooked (contract varargs 4); a variadic function's
                // takes before hooks only (varargs 3: its va_list sibling takes every kind).
                if let (4, Some((sig, _))) = (row.kind, matched) {
                    if n.call_only.contains(&(row.name.clone(), sig.clone())) {
                        ctx.add(512, &name, None, None, vec![("why", p("Hook.Constexpr")), ("found", t(row.name.clone()))]);
                        continue;
                    }
                    if row.hook_kind >= 1 && crate::names::signature_variadic(sig) {
                        ctx.add(512, &name, None, None, vec![("why", p("Hook.Variadic")), ("found", t(row.name.clone()))]);
                        continue;
                    }
                }
                // A constructor or destructor (`tell`) takes before and after only.
                if row.kind == 4 && row.hook_kind >= 2 && matched.is_some_and(|(_, tell)| *tell) {
                    ctx.add(512, &name, None, None, vec![("why", p("Lib.HookOnConstructor")), ("found", t(row.name.clone()))]);
                    continue;
                }
                // The name is the game's but not the signature asked (or none was asked and it has several): L505
                // lists the game's signatures for the name, so the fix is read off the message.
                if let (None, Some(list), false) = (matched, rows.filter(|l| !l.is_empty()), optional) {
                    let kind = if row.signature.is_some() { "Name.FunctionSigHas" } else { "Name.FunctionMany" };
                    let has = signatures(list.iter().map(|(s, _)| s.as_str()));
                    ctx.add(505, &name, None, None, vec![("kind", p(kind)), ("found", t(row.name.clone())), ("sig", t(row.signature.clone().unwrap_or_default())), ("has", t(has))]);
                    continue;
                }
                (if row.signature.is_some() { "Name.FunctionSig" } else { "Name.Function" }, matched.is_some())
            }
            // A record's size (kind 2), its field (kind 1), a global (kind 7): against the types file, as the kernel
            // binds them (LumaTypes.cpp). A name the generator left out is refused with its reason (TY2-1).
            1 | 2 | 7 => {
                let Some(types) = &n.types else {
                    ctx.not_checked(505, "Missing.Types");
                    continue;
                };
                match typed(types, row.kind, &row.name, row.signature.as_deref()) {
                    Typed::Bound => continue,
                    Typed::Missing(kind) => (kind, false),
                    Typed::LeftOut(kind, reason) => {
                        if !optional {
                            let mut args = vec![("kind", p(kind)), ("found", t(row.name.clone())), ("sig", t(row.signature.clone().unwrap_or_default()))];
                            args.extend(left_out_reason(reason));
                            ctx.add(505, &name, None, None, args);
                        }
                        continue;
                    }
                }
            }
            // A class's number (kind 3, objects 10 O-3): any record, by its name or an alias's, as the kernel's
            // class_of finds it.
            3 => match &n.types {
                Some(types) => ("Name.Class", types.records.contains_key(&row.name) || types.aliases.contains_key(&row.name)),
                None => {
                    ctx.not_checked(505, "Missing.Types");
                    continue;
                }
            },
            // A virtual call (kind 8, objects 10): a virtual a record declares, by its name and signature, that has a
            // row ("row": true). Bound whole or not at all, as kind 0.
            _ => match n.types.as_ref().map(|t| t.methods.as_ref()) {
                Some(Some(methods)) => {
                    let found = row.signature.as_ref().and_then(|sig| methods.get(&(row.name.clone(), sig.clone())));
                    // Not callable as asked, but the name has virtuals with a row: L505 lists their signatures.
                    if found != Some(&true) && !optional {
                        let callable: Vec<&str> = methods.iter().filter(|((n, _), with_row)| **with_row && *n == row.name).map(|((_, s), _)| s.as_str()).collect();
                        if !callable.is_empty() {
                            let has = signatures(callable.into_iter());
                            ctx.add(505, &name, None, None, vec![("kind", p("Name.MethodHas")), ("found", t(row.name.clone())), ("sig", t(row.signature.clone().unwrap_or_default())), ("has", t(has))]);
                            continue;
                        }
                    }
                    ("Name.Method", found == Some(&true))
                }
                Some(None) => {
                    ctx.not_checked(505, "Missing.Virtuals");
                    continue;
                }
                None => {
                    ctx.not_checked(505, "Missing.Types");
                    continue;
                }
            },
        };
        if !ok && !optional {
            ctx.add(505, &name, None, None, vec![("kind", p(kind)), ("found", t(row.name.clone())), ("sig", t(row.signature.clone().unwrap_or_default()))]);
        }
    }
    // A library may also ask `core->service(mod, ...)` by name at run time, which no row shows: the kernel refuses one
    // not granted (Hook.ServiceNotGranted), and L218 stays a Note for it.
    if any_service && services_unchecked {
        ctx.not_checked(505, "Missing.Services");
        ctx.not_checked(506, "Missing.Services");
    }
    if mod_functions {
        ctx.not_checked(505, "Missing.ModExports");
    }
}

/// The game's signatures for a name, as L505's `has` gives them: each quoted, in byte order, joined by ", ".
fn signatures<'a>(list: impl Iterator<Item = &'a str>) -> String {
    let mut list: Vec<&str> = list.collect();
    list.sort_unstable();
    list.dedup();
    list.iter().map(|s| format!("\"{s}\"")).collect::<Vec<_>>().join(", ")
}

/// What the types file says of a kind 1, 2 or 7 import.
#[derive(Debug, PartialEq)]
enum Typed<'a> {
    Bound,
    /// No such name, or (with a signature) not of that type: the L505 kind phrase.
    Missing(&'static str),
    /// A name the generator left out: the kind phrase and its reason as written.
    LeftOut(&'static str, &'a str),
}

/// Kind 1 "Type.field" (the record by its name or another spelling, then its own and reachable fields), kind 2 a
/// record, kind 7 a global; a signature is the type text whole (Z-4: a bit-field's reads "u32 : 3", so one named with
/// its plain type is refused). As LumaTypes.cpp's findType, findField and findGlobal answer.
fn typed<'a>(types: &'a crate::names::Types, kind: u32, name: &str, signature: Option<&str>) -> Typed<'a> {
    use crate::names::LeftOutKind;
    let same = |ty: &str| signature.map_or(true, |s| s == ty);
    let with_sig = |plain, sig| if signature.is_some() { sig } else { plain };
    // A type that is no record: why, if the generator left it out as one of these or as a template (by its name
    // before the arguments, `TVec3<s8>` as `TVec3`).
    let type_left_out = |ty: &str| {
        [LeftOutKind::Record, LeftOutKind::Alias, LeftOutKind::Enum]
            .iter()
            .find_map(|k| types.left_out(*k, ty))
            .or_else(|| types.left_out(LeftOutKind::Template, ty.split_once('<').map_or(ty, |(t, _)| t)))
    };
    match kind {
        1 => {
            let Some((record, field)) = crate::names::split_field(name) else {
                return Typed::Missing(with_sig("Name.Field", "Name.FieldSig"));
            };
            match types.record(record) {
                Some((row, r)) => match r.fields.get(field) {
                    Some(ty) if same(ty) => Typed::Bound,
                    Some(_) => Typed::Missing("Name.FieldSig"),
                    None => match types.left_out(LeftOutKind::Field, &format!("{row}.{field}")) {
                        Some(reason) => Typed::LeftOut("Name.FieldUnbound", reason),
                        None => Typed::Missing(with_sig("Name.Field", "Name.FieldSig")),
                    },
                },
                None => match type_left_out(record) {
                    Some(reason) => Typed::LeftOut("Name.FieldUnbound", reason),
                    None => Typed::Missing(with_sig("Name.Field", "Name.FieldSig")),
                },
            }
        }
        2 => match types.record(name) {
            Some(_) => Typed::Bound,
            None => match type_left_out(name) {
                Some(reason) => Typed::LeftOut("Name.TypeUnbound", reason),
                None => Typed::Missing("Name.Type"),
            },
        },
        _ => match (types.globals.get(name), types.left_out(LeftOutKind::Global, name)) {
            (Some(ty), _) if same(ty) => Typed::Bound,
            (None, Some(reason)) => Typed::LeftOut("Name.GlobalUnbound", reason),
            _ => Typed::Missing(with_sig("Name.Global", "Name.GlobalSig")),
        },
    }
}

/// A left_out reason (the generator's fixed phrases, tools/lumacore/gen_types.py R_*) as the finding's `why` phrase,
/// and `what` for the names in it. A phrase this list does not know is shown as the generator wrote it, never dropped
/// (a test reads every reason of a real types file through this and fails on one).
fn left_out_reason(reason: &str) -> Vec<(&'static str, crate::Arg)> {
    let fixed = match reason {
        "the kernel's own" => Some("LeftOut.KernelOwn"),
        "internal linkage: no name a mod can bind" => Some("LeftOut.InternalLinkage"),
        "thread-local: one per thread, no one address" => Some("LeftOut.ThreadLocal"),
        "no name a mod can write" => Some("LeftOut.NoName"),
        "no size (incomplete or dependent)" => Some("LeftOut.NoSize"),
        "a type with no name a mod can write" => Some("LeftOut.TypeNoName"),
        "a template not used with these arguments" => Some("LeftOut.TemplateUnused"),
        "a bit-field wider than 32 bits" => Some("LeftOut.WideBitField"),
        "declared, defined nowhere in the game" => Some("LeftOut.NotDefined"),
        "a static member of a class template" => Some("LeftOut.TemplateMember"),
        "a template instance only a .cpp names" => Some("LeftOut.InstanceInCpp"),
        _ => None,
    };
    if let Some(id) = fixed {
        return vec![("why", p(id))];
    }
    if let Some(names) = reason.strip_prefix("ambiguous: ") {
        return vec![("why", p("LeftOut.Ambiguous")), ("what", t(names))];
    }
    if let Some(base) = reason.strip_prefix("through the virtual base ") {
        return vec![("why", p("LeftOut.VirtualBase")), ("what", t(base))];
    }
    // A file-local typedef two .cpp files give to different types (A-TY7-1): "<record> (<file>), ...".
    if let Some(types) = reason.strip_prefix("a name .cpp files give to different types: ") {
        return vec![("why", p("LeftOut.AliasTwice")), ("what", t(types))];
    }
    vec![("why", t(reason))]
}

#[cfg(test)]
mod types_tests {
    use super::{left_out_reason, typed, Typed};
    use crate::names::{LeftOutKind, Types, TypesError};
    use crate::Arg;

    /// Rows in the form the generator writes (tools/lumacore/gen_types.py), one of each case the readers take.
    const SAMPLE: &str = r#"{
      "types": [
        {"kind": "struct", "name": "TVec3<f32>", "size": 12, "align": 4, "polymorphic": false, "abstract": false, "trivial_copy": true, "trivial_destroy": true, "no_copy": false, "bases": [],
         "fields": [{"name": "x", "type": "f32", "offset": 0, "count": 1, "extents": null, "access": "public", "bits": null}], "reachable": []},
        {"kind": "class", "name": "NameObj", "size": 12, "align": 4, "polymorphic": true, "abstract": false, "trivial_copy": false, "trivial_destroy": false, "no_copy": false, "bases": [],
         "fields": [{"name": "mName", "type": "const char *", "offset": 4, "count": 1, "extents": null, "access": "public", "bits": null}], "reachable": []},
        {"kind": "class", "name": "LiveActor", "size": 140, "align": 4, "polymorphic": true, "abstract": false, "trivial_copy": false, "trivial_destroy": false, "no_copy": false,
         "bases": [{"name": "NameObj", "offset": 0, "virtual": false}],
         "fields": [{"name": "mFlags", "type": "u32 : 3", "offset": 12, "count": 1, "extents": null, "access": "protected",
                     "bits": {"width": 3, "offset": 0}},
                    {"name": "mGrid", "type": "f32", "offset": 16, "count": 12, "extents": [3, 4], "access": "public", "bits": null},
                    {"name": "mRow", "type": "f32", "offset": 64, "count": 4, "extents": null, "access": "private", "bits": null}],
         "reachable": [{"name": "mName", "from": "NameObj", "offset": 4, "type": "const char *"}]},
        {"kind": "union", "name": "Both", "size": 4, "align": 4, "polymorphic": false, "abstract": false, "trivial_copy": true, "trivial_destroy": true, "no_copy": true, "bases": [], "fields": [], "reachable": []},
        {"kind": "alias", "name": "TVec3f", "of": "TVec3<f32>", "record": "TVec3<f32>"},
        {"kind": "spelling", "name": "JGeometry::TVec3<float>", "of": "TVec3<f32>", "record": "TVec3<f32>"},
        {"kind": "alias", "name": "s8", "of": "signed char", "record": null},
        {"kind": "alias", "name": "Both", "of": "Both", "record": "Both"},
        {"kind": "enum", "name": "Mode", "underlying": "u32", "scoped": true, "values": [{"name": "Off", "value": 0}, {"name": "On", "value": 4294967295}]},
        {"kind": "enum", "name": null, "underlying": "int", "scoped": false, "values": [{"name": "LOW", "value": -2147483648}]}
      ],
      "globals": [
        {"name": "MR::sCount", "type": "s32", "const": false, "defined": true, "value": null},
        {"name": "MR::sDeclared", "type": "s32", "const": false, "defined": false, "value": null},
        {"name": "MR::cLimit", "type": "const f32", "const": true, "defined": false, "value": -0.5}
      ],
      "left_out": [
        {"kind": "record", "name": "LumaMod", "file": "native/include/luma.h", "line": 1, "reason": "the kernel's own"},
        {"kind": "alias", "name": "LumaHook", "file": "native/include/luma_hook.h", "line": 1, "reason": "the kernel's own"},
        {"kind": "template", "name": "TBox", "file": "a.hpp", "line": 2, "reason": "a template not used with these arguments"},
        {"kind": "record", "name": "Forward", "file": "a.hpp", "line": 3, "reason": "no size (incomplete or dependent)"},
        {"kind": "field", "name": "LiveActor.mPos", "file": "a.hpp", "line": 4, "reason": "ambiguous: A::mPos, B::mPos"},
        {"kind": "field", "name": "LiveActor.mBase", "file": "a.hpp", "line": 5, "reason": "through the virtual base V"},
        {"kind": "field", "name": "LiveActor.mWide", "file": "a.hpp", "line": 6, "reason": "a bit-field wider than 32 bits"},
        {"kind": "field", "name": "LiveActor.mWide", "file": "a.hpp", "line": 6, "reason": "a second reason, not kept"},
        {"kind": "global", "name": "MR::sDeclared", "file": "a.hpp", "line": 7, "reason": "declared, defined nowhere in the game"},
        {"kind": "global", "name": "sLocal", "file": "a.cpp", "line": 8, "reason": "internal linkage: no name a mod can bind"},
        {"kind": "enum", "name": "", "file": "a.hpp", "line": 9, "reason": "no name a mod can write"}
      ]
    }"#;

    fn sample() -> Types {
        Types::from_json(SAMPLE.as_bytes()).unwrap()
    }

    #[test]
    fn a_types_file_reads_as_draft_2_gives_it() {
        let types = sample();
        assert_eq!(types.records.len(), 4);
        assert_eq!(types.record("TVec3f").map(|(n, r)| (n, r.size)), Some(("TVec3<f32>", 12)));
        assert_eq!(types.record("JGeometry::TVec3<float>").map(|(n, _)| n), Some("TVec3<f32>"));
        // An alias of no record is no record; one spelled as its record is the record.
        assert!(types.record("s8").is_none());
        assert_eq!(types.aliases.get("Both"), None);
        // Reachable and own fields together; a declared global with no definition is not bound.
        assert_eq!(types.records["LiveActor"].fields.len(), 4);
        assert_eq!(types.globals.keys().collect::<Vec<_>>(), vec!["MR::sCount"]);
        assert_eq!(types.left_out(LeftOutKind::Field, "LiveActor.mWide"), Some("a bit-field wider than 32 bits"));
        assert_eq!(types.left_out(LeftOutKind::Enum, ""), Some("no name a mod can write"));
        // The first reason of a name is kept, a second other one recorded (A-TY7-3).
        assert_eq!(types.left_out_again, vec![(LeftOutKind::Field, "LiveActor.mWide".to_string(), "a second reason, not kept".to_string())]);
    }

    #[test]
    fn a_types_file_gives_the_facts_the_compiler_reads() {
        use crate::names::{Access, Base, Bits, Enum, Global, Reached, RecordKind};
        let types = sample();
        let actor = &types.records["LiveActor"];
        assert_eq!((actor.kind, actor.align, actor.polymorphic, actor.is_abstract), (RecordKind::Class, 4, true, false));
        assert_eq!((actor.trivial_copy, actor.trivial_destroy), (false, false));
        assert!(types.records["TVec3<f32>"].trivial_copy && types.records["TVec3<f32>"].trivial_destroy);
        // TY-10b: the third fact, read as the file gives it.
        assert!(types.records["Both"].no_copy && !types.records["TVec3<f32>"].no_copy && !actor.no_copy);
        assert_eq!(actor.bases, vec![Base { name: "NameObj".into(), offset: 0, is_virtual: false }]);
        assert_eq!(actor.reached, vec![Reached { name: "mName".into(), from: "NameObj".into(), offset: 4, ty: "const char *".into() }]);
        let own: Vec<_> = actor.own.iter().map(|f| (f.name.as_str(), f.ty.as_str(), f.offset, f.extents.clone(), f.bits, f.access)).collect();
        assert_eq!(
            own,
            vec![
                ("mFlags", "u32 : 3", 12, vec![], Some(Bits { width: 3, offset: 0 }), Access::Protected),
                ("mGrid", "f32", 16, vec![3, 4], None, Access::Public),
                // A one-extent array: the file gives only the count.
                ("mRow", "f32", 64, vec![4], None, Access::Private),
            ]
        );
        assert_eq!(types.records["Both"].kind, RecordKind::Union);
        assert_eq!(types.enums["Mode"], Enum { underlying: "u32".into(), scoped: true, values: vec![("Off".into(), 0), ("On".into(), 4294967295)] });
        assert_eq!(types.anonymous_enums, vec![Enum { underlying: "int".into(), scoped: false, values: vec![("LOW".into(), -2147483648)] }]);
        // Every alias and spelling row, a record's or not.
        assert_eq!(types.typedefs.get("s8").map(String::as_str), Some("signed char"));
        assert_eq!(types.typedefs.get("TVec3f").map(String::as_str), Some("TVec3<f32>"));
        assert_eq!(types.typedefs.len(), 4);
        // Every global, defined or not; a constant's value as the file writes it.
        assert_eq!(types.global_facts["MR::cLimit"], Global { ty: "const f32".into(), konst: true, defined: false, value: Some("-0.5".into()) });
        assert_eq!(types.global_facts.len(), 3);
        assert!(!types.globals.contains_key("MR::cLimit"));
    }

    #[test]
    fn a_types_file_not_in_the_form_is_refused_naming_the_row() {
        let refused = |json: &str| Types::from_json(json.as_bytes()).unwrap_err().to_string();
        assert!(refused("{").starts_with("line 1 column ") && refused("{").contains(": not JSON ("), "{}", refused("{"));
        assert_eq!(refused(r#"{"globals": [], "left_out": []}"#), "types: missing, or not a list");
        // A record row with every key draft 2 gives, and the fields given.
        let record = |name: &str, kind: &str, fields: &str| {
            format!(r#"{{"kind": "{kind}", "name": "{name}", "size": 1, "align": 1, "polymorphic": false, "abstract": false, "trivial_copy": true, "trivial_destroy": true, "no_copy": false, "bases": [], "fields": [{fields}], "reachable": []}}"#)
        };
        let file = |types: &str, globals: &str| format!(r#"{{"types": [{types}], "globals": [{globals}], "left_out": []}}"#);
        assert_eq!(refused(&file(&record("A", "struct", "").replace("\"size\": 1", "\"size\": -1"), "")), "types[0].size: missing, or not a whole number from 0");
        assert_eq!(refused(&file(&record("A", "struct", "").replace("\"align\": 1, ", ""), "")), "types[0].align: missing, or not a whole number from 0");
        for (key, value) in [("trivial_copy", "true"), ("trivial_destroy", "true"), ("no_copy", "false")] {
            let without = record("A", "struct", "").replace(&format!("\"{key}\": {value}, "), "");
            assert_eq!(refused(&file(&without, "")), format!("types[0].{key}: missing, or not true or false"));
        }
        assert_eq!(
            refused(&file(&record("A", "struct", "").replace("\"bases\": []", r#""bases": [{"name": "B", "offset": 0}]"#), "")),
            "types[0].bases[0].virtual: missing, or not true or false"
        );
        let two = format!("{}, {}", record("A", "struct", ""), record("A", "class", ""));
        assert_eq!(refused(&file(&two, "")), "types[1]: a second record named \"A\"");
        let field = |rest: &str| file(&record("A", "struct", &format!(r#"{{"name": "f", "type": "f32", "offset": 0, {rest}}}"#)), "");
        assert_eq!(
            refused(&field(r#""count": 12, "extents": [3, 5], "access": "public", "bits": null"#)),
            "types[0].fields[0].extents: [3, 5] is not two or more extents whose product is the count 12"
        );
        assert_eq!(
            refused(&field(r#""count": 4, "extents": [4], "access": "public", "bits": null"#)),
            "types[0].fields[0].extents: [4] is not two or more extents whose product is the count 4"
        );
        assert_eq!(
            refused(&field(r#""count": 1, "extents": null, "access": "public", "bits": {"width": 0, "offset": 0}"#)),
            "types[0].fields[0].bits: width 0 at bit 0 is no bit-field"
        );
        assert_eq!(refused(&field(r#""count": 1, "extents": null, "access": "friend", "bits": null"#)), "types[0].fields[0].access: \"friend\" is no access");
        assert_eq!(refused(&field(r#""count": 1, "extents": null, "access": "public""#)), "types[0].fields[0].bits: missing, or not null or an object");
        let reach = record("A", "struct", "").replace("\"reachable\": []", r#""reachable": [{"name": "m", "offset": 0, "type": "f32"}]"#);
        assert_eq!(refused(&file(&reach, "")), "types[0].reachable[0].from: missing, or not text");
        let (a, b) = (record("A", "struct", ""), record("B", "struct", ""));
        let alias = format!(r#"{a}, {b}, {{"kind": "alias", "name": "C", "of": "A", "record": "A"}}, {{"kind": "alias", "name": "C", "of": "B", "record": "B"}}"#);
        assert_eq!(refused(&file(&alias, "")), "types[3]: \"C\" is both \"A\" and \"B\"");
        let alias = format!(r#"{a}, {b}, {{"kind": "alias", "name": "C", "of": "X", "record": "A"}}, {{"kind": "alias", "name": "C", "of": "X", "record": "B"}}"#);
        assert_eq!(refused(&file(&alias, "")), "types[3]: \"C\" names both \"A\" and \"B\"");
        assert_eq!(refused(&file(r#"{"kind": "alias", "name": "s8", "of": "signed char"}"#, "")), "types[0].record: missing, or not text or null");
        assert_eq!(refused(&file(r#"{"kind": "nerve", "name": "A"}"#, "")), "types[0].kind: \"nerve\" is no kind of draft 2");
        let e = |values: &str| format!(r#"{{"kind": "enum", "name": "E", "underlying": "int", "scoped": false, "values": [{values}]}}"#);
        assert_eq!(refused(&file(&e(r#"{"name": "A", "value": 1.5}"#), "")), "types[0].values[0].value: missing, or not a whole number");
        assert_eq!(refused(&file(&format!("{}, {}", e(""), e("")), "")), "types[1]: a second enum named \"E\"");
        assert_eq!(refused(&file("", r#"{"name": "g", "type": "s32", "const": false, "value": null}"#)), "globals[0].defined: missing, or not true or false");
        assert_eq!(
            refused(&file("", r#"{"name": "g", "type": "s32", "const": false, "defined": true, "value": "1"}"#)),
            "globals[0].value: missing, or not null or a number"
        );
        assert_eq!(refused(&file("", r#"{"name": "g", "type": "s32", "defined": true, "value": null}"#)), "globals[0].const: missing, or not true or false");
        let twice = r#"{"name": "g", "type": "s32", "const": false, "defined": false, "value": null}, {"name": "g", "type": "s32", "const": false, "defined": true, "value": null}"#;
        assert_eq!(refused(&file("", twice)), "globals[1]: a second global named \"g\"");
        assert!(matches!(Types::from_json(b"[]"), Err(TypesError::Row { .. })));
    }

    #[test]
    fn kinds_1_2_and_7_bind_as_the_kernel_binds() {
        let types = sample();
        let typed = |kind, name, sig| typed(&types, kind, name, sig);
        // Kind 2: a record by any spelling; a type left out by its reason, a template by its name before `<`.
        assert_eq!(typed(2, "TVec3f", None), Typed::Bound);
        assert_eq!(typed(2, "JGeometry::TVec3<float>", None), Typed::Bound);
        assert_eq!(typed(2, "Mode", None), Typed::Missing("Name.Type"));
        assert_eq!(typed(2, "LumaMod", None), Typed::LeftOut("Name.TypeUnbound", "the kernel's own"));
        assert_eq!(typed(2, "LumaHook", None), Typed::LeftOut("Name.TypeUnbound", "the kernel's own"));
        assert_eq!(typed(2, "TBox<s8>", None), Typed::LeftOut("Name.TypeUnbound", "a template not used with these arguments"));
        assert_eq!(typed(2, "Forward", None), Typed::LeftOut("Name.TypeUnbound", "no size (incomplete or dependent)"));
        // Kind 1: own and reachable fields, through an alias; a bit-field's type is its whole text.
        assert_eq!(typed(1, "TVec3f.x", Some("f32")), Typed::Bound);
        assert_eq!(typed(1, "LiveActor.mName", None), Typed::Bound);
        assert_eq!(typed(1, "LiveActor.mFlags", Some("u32 : 3")), Typed::Bound);
        assert_eq!(typed(1, "LiveActor.mFlags", Some("u32")), Typed::Missing("Name.FieldSig"));
        assert_eq!(typed(1, "LiveActor.mPos", None), Typed::LeftOut("Name.FieldUnbound", "ambiguous: A::mPos, B::mPos"));
        assert_eq!(typed(1, "LiveActor.mWide", None), Typed::LeftOut("Name.FieldUnbound", "a bit-field wider than 32 bits"));
        assert_eq!(typed(1, "LumaMod.version", None), Typed::LeftOut("Name.FieldUnbound", "the kernel's own"));
        assert_eq!(typed(1, "LiveActor.mNone", None), Typed::Missing("Name.Field"));
        assert_eq!(typed(1, "LiveActor.mNone", Some("u32")), Typed::Missing("Name.FieldSig"));
        assert_eq!(typed(1, "LiveActor", None), Typed::Missing("Name.Field"));
        assert_eq!(typed(1, "TVec3<f32.x>", None), Typed::Missing("Name.Field"));
        // Kind 7: defined globals only, the type whole.
        assert_eq!(typed(7, "MR::sCount", Some("s32")), Typed::Bound);
        assert_eq!(typed(7, "MR::sCount", Some("u32")), Typed::Missing("Name.GlobalSig"));
        assert_eq!(typed(7, "MR::sDeclared", None), Typed::LeftOut("Name.GlobalUnbound", "declared, defined nowhere in the game"));
        assert_eq!(typed(7, "sLocal", None), Typed::LeftOut("Name.GlobalUnbound", "internal linkage: no name a mod can bind"));
        assert_eq!(typed(7, "MR::sNone", None), Typed::Missing("Name.Global"));
    }

    fn as_given(args: &[(&'static str, Arg)]) -> Vec<(&'static str, String)> {
        args.iter()
            .map(|(n, a)| (*n, match a {
                Arg::Text(t) => format!("T:{t}"),
                Arg::Phrase(p) => format!("P:{p}"),
            }))
            .collect()
    }

    /// Every reason the generator writes (gen_types.py's R_*) has its row, and L505 says it whole in all 14 languages.
    const REASONS: [&str; 14] = [
        "the kernel's own",
        "internal linkage: no name a mod can bind",
        "thread-local: one per thread, no one address",
        "no name a mod can write",
        "no size (incomplete or dependent)",
        "a type with no name a mod can write",
        "a template not used with these arguments",
        "a bit-field wider than 32 bits",
        "declared, defined nowhere in the game",
        "a static member of a class template",
        "a template instance only a .cpp names",
        "ambiguous: A::mPos, B::mPos",
        "through the virtual base V",
        "a name .cpp files give to different types: TailRoot (a.cpp), TailNode (b.cpp)",
    ];

    fn said(kind: &'static str, reason: &str, language: &str) -> String {
        let mut args = vec![("kind", Arg::Phrase(kind)), ("found", Arg::Text("LiveActor.mPos".into())), ("sig", Arg::Text(String::new()))];
        args.extend(left_out_reason(reason));
        let given = as_given(&args);
        let given: Vec<(&str, &str)> = given.iter().map(|(n, v)| (*n, v.as_str())).collect();
        crate::message_of(505, &[&[("mod", "m"), ("file", "code/windows-x64/mod.dll")][..], &given].concat(), language)
    }

    #[test]
    fn every_left_out_reason_is_said_in_every_language() {
        for reason in REASONS {
            let args = left_out_reason(reason);
            assert!(matches!(args[0], ("why", Arg::Phrase(_))), "{reason}: no row");
            for kind in ["Name.TypeUnbound", "Name.FieldUnbound", "Name.GlobalUnbound"] {
                for language in crate::LANGUAGES {
                    let text = said(kind, reason, language);
                    assert!(!text.contains('{') && !text.contains('}'), "{reason} {kind} {language}: {text}");
                }
            }
        }
        let en = said("Name.FieldUnbound", "ambiguous: A::mPos, B::mPos", "UsEnglish");
        assert!(en.contains("\"LiveActor.mPos\"") && en.contains("(A::mPos, B::mPos each have it)"), "{en}");
        // A phrase no row knows is shown as written, never dropped.
        assert_eq!(as_given(&left_out_reason("a new reason")), vec![("why", "T:a new reason".to_string())]);
    }

    /// The generator's own reason list (`LUMA_GEN_TYPES=<tools/lumacore/gen_types.py> cargo test -- --ignored`):
    /// every `R_* = "..."` phrase has a row, a prefix (ending in a space) with names after it as `what`. A reason
    /// added to the generator without a row here fails this test before any types file carries it.
    #[test]
    #[ignore]
    fn every_reason_the_generator_has_has_a_row() {
        let path = std::env::var("LUMA_GEN_TYPES").expect("LUMA_GEN_TYPES names tools/lumacore/gen_types.py");
        let source = std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("{path}: {e}"));
        let mut reasons = Vec::new();
        for line in source.lines() {
            let Some((name, value)) = line.split_once('=') else { continue };
            let name = name.trim();
            if !name.starts_with("R_") || !name.bytes().all(|c| c.is_ascii_uppercase() || c == b'_') {
                continue;
            }
            let value = value.trim();
            let text = value.strip_prefix('"').and_then(|v| v.strip_suffix('"')).unwrap_or_else(|| panic!("{path}: {name} is not one plain string: {value}"));
            assert!(!text.contains('\\') && !text.contains('"'), "{path}: {name}: {text}");
            reasons.push((name.to_string(), text.to_string()));
        }
        assert!(reasons.len() >= 14, "{path}: only {} R_ constants read: {reasons:?}", reasons.len());
        for (name, text) in &reasons {
            let (given, names) = if text.ends_with(' ') { (format!("{text}A::x (a.cpp), B::x (b.cpp)"), true) } else { (text.clone(), false) };
            let args = as_given(&left_out_reason(&given));
            assert!(args[0].0 == "why" && args[0].1.starts_with("P:LeftOut."), "{name} = {text:?} has no row: {args:?}");
            assert_eq!(args.len() == 2 && args[1] == ("what", "T:A::x (a.cpp), B::x (b.cpp)".to_string()), names, "{name}: {args:?}");
        }
        eprintln!("{path}: {} reasons, each with a row", reasons.len());
    }

    /// The real types file (`LUMA_TYPES=<path> cargo test -- --ignored`): it reads, and every reason in it has a row.
    #[test]
    #[ignore]
    fn the_real_types_file_reads_and_every_reason_has_a_row() {
        let path = std::env::var("LUMA_TYPES").expect("LUMA_TYPES names a luma_types.json");
        let types = Types::from_json(&std::fs::read(&path).unwrap()).unwrap_or_else(|e| panic!("{path}: {e}"));
        // Each record's own and reachable fields bind as kind 1 by the record's name.
        for (name, record) in &types.records {
            for (field, ty) in &record.fields {
                let full = format!("{name}.{field}");
                if crate::names::split_field(&full) == Some((name.as_str(), field.as_str())) {
                    assert_eq!(typed(&types, 1, &full, Some(ty)), Typed::Bound, "{full}");
                }
            }
            assert_eq!(typed(&types, 2, name, None), Typed::Bound, "{name}");
        }
        for (name, ty) in &types.globals {
            assert_eq!(typed(&types, 7, name, Some(ty)), Typed::Bound, "{name}");
        }
        eprintln!("{path}: {} records, {} aliases, {} globals, {} left out", types.records.len(), types.aliases.len(), types.globals.len(), types.left_out.len());
        // A name left out twice with two reasons keeps the first, as the kernel does (A-TY7-3); each is shown.
        for (kind, name, reason) in &types.left_out_again {
            eprintln!("left out again, not kept: {kind:?} {name}: {reason} (kept: {})", types.left_out(*kind, name).unwrap_or_default());
        }
        let mut unknown: Vec<&str> = types.left_out.values().map(String::as_str).filter(|r| !matches!(left_out_reason(r)[0], ("why", Arg::Phrase(_)))).collect();
        unknown.sort();
        unknown.dedup();
        assert!(unknown.is_empty(), "reasons with no row: {unknown:?}");
    }
}
