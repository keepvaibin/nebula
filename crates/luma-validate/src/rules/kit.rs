//! L700-L702: data for library mods such as LumaKit (`kit/<module>/`, luma-format 3 and 4; plan 5.3).

use super::{p, t, Ctx};
use std::collections::BTreeSet;

pub(crate) fn check(ctx: &mut Ctx) {
    let declared: Vec<String> = ctx
        .manifest
        .as_ref()
        .and_then(|r| r.get("kit"))
        .map(|k| k.members().iter().map(|m| m.key.clone()).collect())
        .unwrap_or_default();
    // A key is `<library id>.<module>` (luma-format 4: "lumakit.player.form"); its data is in `kit/<module>/`
    // (luma-format 3; the library asks mods.kit_read for `module`). The library is a mod this one needs, or itself.
    let own_id = ctx.manifest.as_ref().and_then(|r| r.get("id")).and_then(|v| v.as_str()).map(str::to_string);
    let libraries: Vec<String> = ctx.facts.needs.iter().map(|(id, _)| id.clone()).chain(own_id).collect();
    let folder_of = |key: &str| -> String {
        libraries
            .iter()
            .filter_map(|id| key.strip_prefix(id.as_str()).and_then(|rest| rest.strip_prefix('.')))
            .filter(|rest| !rest.is_empty())
            .min_by_key(|rest| rest.len())
            .unwrap_or(key)
            .to_string()
    };
    let declared_folders: Vec<String> = declared.iter().map(|k| folder_of(k)).collect();
    let mut folders = BTreeSet::new();
    for item in &ctx.m.items {
        if let Some(rest) = item.name.strip_prefix("kit/") {
            let module = rest.split('/').next().unwrap_or("");
            if !module.is_empty() {
                folders.insert(module.to_string());
            }
        }
    }
    for module in &folders {
        if !declared_folders.contains(module) {
            ctx.add(700, &format!("kit/{module}/"), None, None, vec![("why", p("Kit.FolderNotDeclared")), ("module", t(module.clone()))]);
        }
    }
    for module in &declared {
        if !folders.contains(&folder_of(module)) {
            ctx.add(700, "luma.json", None, None, vec![("why", p("Kit.DeclaredNoFolder")), ("module", t(module.clone()))]);
        }
        // A module's schema comes from the installed module that provides it (plan 5.3); none is given to the
        // validator yet (LumaKit's modules are phase 5).
        ctx.not_checked(701, "Missing.KitSchemas");
        ctx.not_checked(702, "Missing.KitSchemas");
    }
}
