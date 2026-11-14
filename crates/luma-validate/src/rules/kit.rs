//! L700-L702: data for library mods such as LumaKit (`kit/<module>/`, luma-format 3 and 4), checked against the schema
//! the library gives for that module's data (crate::schema).
//!
//! - A library gives its schemas at `code/<id>.kit/<module>/<version>.json`; they go into its report
//!   (`Touches::kit_schemas`), from which the checks of the mods that use it read them.
//! - A mod's `kit` names `<library id>.<module>` and the data version; its data is `kit/<module>/<name>.json`, the
//!   files the library's code reads (services-mods 10: `kit_names` and `kit_read`).

use super::{p, t, Ctx};
use crate::json::Pos;
use crate::schema::Schema;
use crate::zip::EntryKind;
use std::collections::BTreeSet;

/// The most of a schema file read (it is kept in the library's report).
const MAX_SCHEMA: u64 = 1 << 20;
/// The most of a data file read: what the `mods` service reads of one (LUMA_MODS_KIT_FILE_MOST).
const MAX_DATA: u64 = 4 << 20;
const SEP: char = '\u{1f}';

/// A name the `mods` service lists and reads (`kit_names`, `kit_read`): 1 to 64 of a-z, 0-9, `_`, `-` and `.`, not
/// starting with a dot, no `..`.
fn kit_name(name: &str) -> bool {
    (1..=64).contains(&name.len())
        && name.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || matches!(b, b'_' | b'-' | b'.'))
        && !name.starts_with('.')
        && !name.contains("..")
}

/// A data version as `kit` and a schema's file name write it: a whole number from 1 to 1000000, no leading zero.
fn version_of(text: &str) -> Option<i64> {
    let n: i64 = text.parse().ok().filter(|_| !text.starts_with('0') && text.bytes().all(|b| b.is_ascii_digit()))?;
    (1..=1_000_000).contains(&n).then_some(n)
}

pub(crate) fn check(ctx: &mut Ctx) {
    let own_id = ctx.manifest.as_ref().and_then(|r| r.get("id")).and_then(|v| v.as_str()).map(str::to_string);
    if let Some(id) = &own_id {
        given_schemas(ctx, id);
    }
    let declared: Vec<(String, Option<i64>, Pos)> = ctx
        .manifest
        .as_ref()
        .and_then(|r| r.get("kit"))
        .map(|k| k.members().iter().map(|m| (m.key.clone(), m.value.as_i64(), m.key_pos)).collect())
        .unwrap_or_default();
    // A key is `<library id>.<module>` (luma-format 4: "lumakit.player.form"); its data is in `kit/<module>/`
    // (luma-format 3; the library asks the `mods` service for `module`). The library is a mod this one needs, or itself.
    let libraries: Vec<String> = ctx.facts.needs.iter().map(|(id, _)| id.clone()).chain(own_id.clone()).collect();
    let library_of = |key: &str| -> Option<(String, String)> {
        libraries
            .iter()
            .filter_map(|id| key.strip_prefix(id.as_str()).and_then(|rest| rest.strip_prefix('.')).filter(|rest| !rest.is_empty()).map(|rest| (id.clone(), rest.to_string())))
            .min_by_key(|(_, rest)| rest.len())
    };
    let mut folders = BTreeSet::new();
    for item in &ctx.m.items {
        if let Some(rest) = item.name.strip_prefix("kit/") {
            let module = rest.split('/').next().unwrap_or("");
            if !module.is_empty() {
                folders.insert(module.to_string());
            }
        }
    }
    let declared_folders: Vec<String> = declared.iter().map(|(k, _, _)| library_of(k).map_or_else(|| k.clone(), |(_, f)| f)).collect();
    for module in &folders {
        if !declared_folders.contains(module) {
            ctx.add(700, &format!("kit/{module}/"), None, None, vec![("why", p("Kit.FolderNotDeclared")), ("module", t(module.clone()))]);
        }
    }
    // Files under kit/<module>/ that the library cannot list or read: only `<name>.json` directly in a folder whose
    // name the service takes (both kit names).
    for item in ctx.m.items.clone() {
        let Some(rest) = item.name.strip_prefix("kit/") else { continue };
        if item.kind != EntryKind::File {
            continue;
        }
        let read = rest.split_once('/').is_some_and(|(module, file)| kit_name(module) && file.strip_suffix(".json").is_some_and(kit_name));
        if !read {
            ctx.add(700, &item.name, None, None, vec![("why", p("Kit.NotData")), ("entry", t(item.name.clone()))]);
        }
    }
    for (key, version, at) in declared {
        let pointer = format!("/kit/{}", super::seg(&key));
        let Some((library, folder)) = library_of(&key) else {
            ctx.add(700, "luma.json", Some(at), Some(&pointer), vec![("why", p("Kit.NotNeeded")), ("module", t(key.clone()))]);
            continue;
        };
        if !folders.contains(&folder) {
            ctx.add(700, "luma.json", None, None, vec![("why", p("Kit.DeclaredNoFolder")), ("module", t(key.clone()))]);
        }
        // A version that is no whole number is L204's.
        let Some(version) = version else { continue };
        // The library's schemas: this mod's own, or those of the installed library's report. A library that is not
        // installed is L800's (a need missing), and then the mod does not run.
        let given: Vec<String> = if own_id.as_deref() == Some(library.as_str()) {
            ctx.facts.touches.kit_schemas.clone()
        } else {
            match ctx.input.installed.iter().find(|o| o.id == library) {
                Some(o) => o.touches.kit_schemas.clone(),
                None => continue,
            }
        };
        let mut versions: Vec<i64> = Vec::new();
        let mut schema = None;
        for row in &given {
            let mut fields = row.splitn(3, SEP);
            let (Some(k), Some(v), Some(text)) = (fields.next(), fields.next(), fields.next()) else { continue };
            let Some(v) = version_of(v) else { continue };
            if k != key {
                continue;
            }
            // A schema kept in a report was read when the library was checked; one that no longer reads is not given.
            let Some(read) = crate::json::parse(text.as_bytes()).ok().and_then(|value| Schema::read(&value).ok()) else { continue };
            versions.push(v);
            if v == version {
                schema = Some(read);
            }
        }
        versions.sort_unstable();
        versions.dedup();
        let Some(schema) = schema else {
            let expected = if versions.is_empty() { p("Kit.NoModuleData") } else { t(versions.iter().map(i64::to_string).collect::<Vec<_>>().join(", ")) };
            // The library named, so the words say what to update.
            ctx.add(
                702,
                "luma.json",
                Some(at),
                Some(&pointer),
                vec![("module", t(key.clone())), ("found", t(version.to_string())), ("expected", expected), ("library", t(library.clone()))],
            );
            continue;
        };
        // Each data file against the schema of the version named.
        let prefix = format!("kit/{folder}/");
        let files: Vec<(usize, String)> = ctx
            .m
            .items
            .iter()
            .enumerate()
            .filter(|(_, i)| i.kind == EntryKind::File)
            .filter(|(_, i)| i.name.strip_prefix(&prefix).and_then(|f| f.strip_suffix(".json")).is_some_and(kit_name))
            .map(|(n, i)| (n, i.name.clone()))
            .collect();
        for (index, name) in files {
            let one = Pos { line: 1, column: 1 };
            let bytes = match ctx.m.read(index, MAX_DATA) {
                Ok(b) => b,
                Err(crate::source::ReadError::OverLimit) => {
                    let size = ctx.m.items[index].size;
                    let args = vec![("module", t(key.clone())), ("text", p("Kit.Most")), ("key", t("/")), ("what", p("Kit.What.Bytes")), ("found", t(size.to_string())), ("max", t(MAX_DATA.to_string()))];
                    ctx.add(701, &name, Some(one), None, args);
                    continue;
                }
                Err(_) => continue, // L106 already
            };
            let value = match crate::json::parse(&bytes) {
                Ok(v) => v,
                Err(e) => {
                    ctx.add(701, &name, Some(e.pos), None, vec![("module", t(key.clone())), ("text", p("Kit.NotJson")), ("found", p(e.what)), ("detail", t(e.detail))]);
                    continue;
                }
            };
            for problem in schema.check(&value) {
                let mut args = vec![("module", t(key.clone())), ("text", p(problem.phrase))];
                args.extend(problem.args);
                ctx.add(701, &name, Some(problem.pos), Some(&problem.pointer), args);
            }
        }
    }
}

/// The library's own schemas, `code/<id>.kit/<module>/<version>.json`: each read and kept in its report, or L700 naming
/// why it is not one the validator reads.
fn given_schemas(ctx: &mut Ctx, id: &str) {
    let prefix = format!("code/{id}.kit/");
    let entries: Vec<(usize, String, String)> =
        ctx.m.items.iter().enumerate().filter(|(_, i)| i.kind == EntryKind::File).filter_map(|(n, i)| i.name.strip_prefix(&prefix).map(|rest| (n, i.name.clone(), rest.to_string()))).collect();
    for (index, name, rest) in entries {
        let place = rest.split_once('/').and_then(|(module, file)| Some((module, version_of(file.strip_suffix(".json")?)?))).filter(|(module, _)| kit_name(module));
        let Some((module, version)) = place else {
            ctx.add(700, &name, None, None, vec![("why", p("Kit.SchemaPlace")), ("entry", t(name.clone()))]);
            continue;
        };
        let bytes = match ctx.m.read(index, MAX_SCHEMA) {
            Ok(b) => b,
            Err(crate::source::ReadError::OverLimit) => {
                ctx.add(700, &name, None, None, vec![("why", p("Kit.SchemaUnread")), ("entry", t(name.clone())), ("found", p("Json.TooLarge")), ("detail", t(""))]);
                continue;
            }
            Err(_) => continue, // L106 already
        };
        let value = match crate::json::parse(&bytes) {
            Ok(v) => v,
            Err(e) => {
                ctx.add(700, &name, Some(e.pos), None, vec![("why", p("Kit.SchemaUnread")), ("entry", t(name.clone())), ("found", p(e.what)), ("detail", t(e.detail))]);
                continue;
            }
        };
        match Schema::read(&value) {
            Ok(_) => {
                let text = String::from_utf8_lossy(&bytes).into_owned();
                ctx.facts.touches.kit_schemas.push(format!("{id}.{module}{SEP}{version}{SEP}{text}"));
            }
            Err(unread) => {
                let pointer = if unread.pointer.is_empty() { "/".to_string() } else { unread.pointer.clone() };
                ctx.add(700, &name, None, None, vec![("why", p("Kit.SchemaKeyword")), ("entry", t(name.clone())), ("key", t(pointer)), ("found", t(unread.what))]);
            }
        }
    }
}
