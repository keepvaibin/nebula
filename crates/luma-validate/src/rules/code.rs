//! L500-L512: a mod's code (luma-format 6; F-1).

use super::{p, t, Ctx};
use crate::binary::{self, Format, Machine};
use crate::zip::EntryKind;
use std::collections::BTreeMap;

pub const MAX_LIBRARY: u64 = 1 << 30;
const IMPORT_KINDS: u32 = 8; // LUMA_IMPORT_FUNCTION..LUMA_IMPORT_HOOK, LUMA_IMPORT_SERVICE, LUMA_IMPORT_MOD_FUNCTION, LUMA_IMPORT_GLOBAL
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
    let (mut services, mut mod_functions) = (false, false);
    for (i, row) in info.imports.iter().enumerate() {
        if row.kind >= IMPORT_KINDS || (row.kind == 4 && row.hook_kind >= HOOK_KINDS) || row.name.is_empty() {
            ctx.add(512, &name, None, None, vec![("why", p("Lib.ImportRowBad")), ("row", t(i.to_string()))]);
            continue;
        }
        match row.kind {
            // A service (loader D3-2): `signature` is "major.minor". Which services there are, their versions and
            // permissions are the host's (its service finder), not the self-description's: said below.
            5 => {
                if row.signature.as_deref().is_some_and(is_service_version) {
                    services = true;
                } else {
                    ctx.add(512, &name, None, None, vec![("why", p("Lib.ImportRowBad")), ("row", t(i.to_string()))]);
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
                // A constructor or destructor (`tell`) takes before and after only.
                if row.kind == 4 && row.hook_kind >= 2 && matched.is_some_and(|(_, tell)| *tell) {
                    ctx.add(512, &name, None, None, vec![("why", p("Lib.HookOnConstructor")), ("found", t(row.name.clone()))]);
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
            // A NameObj class's number (kind 3, for is_a; types 4 and plan 1.3).
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
    if services {
        ctx.not_checked(505, "Missing.Services");
        ctx.not_checked(506, "Missing.Services");
    } else {
        // A library may still ask `core->service(mod, ...)` by name at run time, which no row shows; the kernel refuses
        // one not granted (Hook.ServiceNotGranted).
        ctx.not_checked(506, "Missing.ServiceImports");
    }
    if mod_functions {
        ctx.not_checked(505, "Missing.ModExports");
    }
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
    vec![("why", t(reason))]
}

/// A service import's version, "major.minor": two whole numbers (loader D3-2).
fn is_service_version(s: &str) -> bool {
    s.split_once('.').is_some_and(|(a, b)| [a, b].iter().all(|x| !x.is_empty() && x.len() <= 5 && x.bytes().all(|c| c.is_ascii_digit())))
}

#[cfg(test)]
mod types_tests {
    use super::{left_out_reason, typed, Typed};
    use crate::names::{LeftOutKind, Types, TypesError};
    use crate::Arg;

    /// Rows in the form the generator writes (tools/lumacore/gen_types.py), one of each case the readers take.
    const SAMPLE: &str = r#"{
      "types": [
        {"kind": "struct", "name": "TVec3<f32>", "size": 12, "fields": [{"name": "x", "type": "f32"}], "reachable": []},
        {"kind": "class", "name": "NameObj", "size": 12, "fields": [{"name": "mName", "type": "const char *"}], "reachable": []},
        {"kind": "class", "name": "LiveActor", "size": 140, "fields": [{"name": "mFlags", "type": "u32 : 3"}],
         "reachable": [{"name": "mName", "type": "const char *"}]},
        {"kind": "union", "name": "Both", "size": 4, "fields": [], "reachable": []},
        {"kind": "alias", "name": "TVec3f", "of": "TVec3<f32>", "record": "TVec3<f32>"},
        {"kind": "spelling", "name": "JGeometry::TVec3<float>", "of": "TVec3<f32>", "record": "TVec3<f32>"},
        {"kind": "alias", "name": "s8", "of": "signed char"},
        {"kind": "alias", "name": "Both", "of": "Both", "record": "Both"},
        {"kind": "enum", "name": "Mode", "underlying": "u32"}
      ],
      "globals": [
        {"name": "MR::sCount", "type": "s32", "defined": true},
        {"name": "MR::sDeclared", "type": "s32", "defined": false}
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
        assert_eq!(types.records["LiveActor"].fields.len(), 2);
        assert_eq!(types.globals.keys().collect::<Vec<_>>(), vec!["MR::sCount"]);
        assert_eq!(types.left_out(LeftOutKind::Field, "LiveActor.mWide"), Some("a bit-field wider than 32 bits"));
        assert_eq!(types.left_out(LeftOutKind::Enum, ""), Some("no name a mod can write"));
    }

    #[test]
    fn a_types_file_not_in_the_form_is_refused_naming_the_row() {
        let refused = |json: &str| Types::from_json(json.as_bytes()).unwrap_err().to_string();
        assert!(refused("{").starts_with("line 1 column ") && refused("{").contains(": not JSON ("), "{}", refused("{"));
        assert_eq!(refused(r#"{"globals": [], "left_out": []}"#), "types: missing, or not a list");
        assert_eq!(
            refused(r#"{"types": [{"kind": "struct", "name": "A", "size": -1, "fields": [], "reachable": []}], "globals": [], "left_out": []}"#),
            "types[0].size: missing, or not a size"
        );
        let two = r#"{"types": [{"kind": "struct", "name": "A", "size": 1, "fields": [], "reachable": []},
                                {"kind": "class", "name": "A", "size": 1, "fields": [], "reachable": []}], "globals": [], "left_out": []}"#;
        assert_eq!(refused(two), "types[1]: a second record named \"A\"");
        let alias = r#"{"types": [{"kind": "struct", "name": "A", "size": 1, "fields": [], "reachable": []},
                                  {"kind": "struct", "name": "B", "size": 1, "fields": [], "reachable": []},
                                  {"kind": "alias", "name": "C", "record": "A"}, {"kind": "alias", "name": "C", "record": "B"}],
                        "globals": [], "left_out": []}"#;
        assert_eq!(refused(alias), "types[3]: \"C\" names both \"A\" and \"B\"");
        assert_eq!(
            refused(r#"{"types": [{"kind": "nerve", "name": "A"}], "globals": [], "left_out": []}"#),
            "types[0].kind: \"nerve\" is no kind of draft 2"
        );
        assert_eq!(
            refused(r#"{"types": [], "globals": [{"name": "g", "type": "s32"}], "left_out": []}"#),
            "globals[0].defined: missing, or not true or false"
        );
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
    const REASONS: [&str; 12] = [
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
        "ambiguous: A::mPos, B::mPos",
        "through the virtual base V",
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
        let mut unknown: Vec<&str> = types.left_out.values().map(String::as_str).filter(|r| !matches!(left_out_reason(r)[0], ("why", Arg::Phrase(_)))).collect();
        unknown.sort();
        unknown.dedup();
        assert!(unknown.is_empty(), "reasons with no row: {unknown:?}");
    }
}
