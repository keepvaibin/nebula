//! A mod's effects (effects contract 7): `layers/<layer>/assets/effects.json`, which the host hands the cook as the
//! change `+Luma/<id>/effects.json`, one a layer in layer order (luma-format 5: a layer's assets are the store's
//! `Luma/<id>/<mod path>`). The cook adds their particles to the game's particle file and names, and writes the mod's
//! list of effects, `Luma/<id>/effects.txt`, from which the run time takes each one's draw group.
//!
//! ```json
//! { "textures": { "GoldSpark": "textures/gold_spark.png" },
//!   "effects": { "aura": { "draw": "3D", "particles": [ { "like": "Glow", "textures": { "sprite": "GoldSpark" } } ] } },
//!   "replace": { "Glow": { "textures": { "sprite": "GoldSpark" } } } }
//! ```
//!
//! - Effect `<name>` is started as `<id>:<name>`. Its particles are `<id>:<name>00`, `01`, ..., the game's numbered
//!   names (MultiEmitter::allocateEmitter), numbered after the game's and earlier mods' particles, with their names.
//! - `replace` writes over a game particle, which keeps its number and name. It starts from the game's own particle
//!   (as `like` does), so of two mods replacing one particle the later one's is used whole, and the earlier is named.
//! - A mod's layers are one file: a later layer's texture, effect or replacement of a name is used in place of the
//!   earlier's.
//! - A texture's file is a mod path, read from the store's `Luma/<id>/<path>` (the last layer's file at that path).
//! - Particles are the game's records by name (particle_json.rs); every refusal is L804 with a `Cook.Effect.<Kind>`
//!   phrase and the value's JSON pointer, and stops the mod's effects whole: no half-added particle set.

use super::particle_json::{self as pj, at_key, Game, Roles, Textures};
use super::particles::{ParticleIr, Resource, Texture};
use super::refuse::{refuse, EffectFacts};
use anyhow::Result;
use serde_json::{json, Map, Value};
use std::collections::BTreeMap;

/// The game's particle file and its names, where each mod's particles are added.
pub const PARTICLES: &str = "ParticleData/Effect.arc/Particles.jpc";
pub const PARTICLE_NAMES: &str = "ParticleData/Effect.arc/ParticleNames.bcsv";

/// The six draw groups an actor's effect takes (effects contract 4), by the game's names; the number is the place.
pub const GROUPS: [&str; 6] = ["3D", "PAUSE_IGNORE", "INDIRECT", "AFTER_INDIRECT", "BLOOM_EFFECT", "AFTER_IMAGE_EFFECT"];
/// Emitters an effect may have (MultiEmitter's list holds 32).
pub const MOST_EMITTERS: usize = 16;
pub const MOST_NAME: usize = 64;

/// The mod a store path is the effects source of: `Luma/<id>/effects.json`.
pub fn source_of(path: &str) -> Option<&str> {
    let mut parts = path.trim_start_matches('/').split('/');
    let (luma, id, file) = (parts.next()?, parts.next()?, parts.next()?);
    (parts.next().is_none() && luma.eq_ignore_ascii_case("luma") && !id.is_empty() && file.eq_ignore_ascii_case("effects.json")).then_some(id)
}

/// Where a mod's asset is in the store.
pub fn asset_path(id: &str, path: &str) -> String {
    format!("Luma/{id}/{path}")
}

/// The mod's list of effects (J's run time reads it: `<name>\t<group>\t<emitters>\n`, sorted by name).
pub fn list_path(id: &str) -> String {
    asset_path(id, "effects.txt")
}

/// `[a-z][a-z0-9_]*`, at most 64 long, not ending in two digits (that would make the game read it as one particle's
/// exact name, effects contract 7a).
fn effect_name_ok(name: &str) -> bool {
    let b = name.as_bytes();
    !b.is_empty()
        && b.len() <= MOST_NAME
        && b[0].is_ascii_lowercase()
        && b.iter().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || *c == b'_')
        && !ends_in_two_digits(name)
}

fn ends_in_two_digits(name: &str) -> bool {
    let b = name.as_bytes();
    b.len() >= 2 && b[b.len() - 1].is_ascii_digit() && b[b.len() - 2].is_ascii_digit()
}

/// A texture's name, as texture packs see it: letters, digits, `_`, `-` and `.`.
fn texture_name_ok(name: &str) -> bool {
    !name.is_empty() && name.len() <= MOST_NAME && name.bytes().all(|c| c.is_ascii_alphanumeric() || b"_-.".contains(&c))
}

/// A mod path inside the layer's assets: no root, no `..`, no `\` or `:`.
fn path_ok(path: &str) -> bool {
    !path.is_empty() && !path.contains('\\') && !path.contains(':') && path.split('/').all(|p| !p.is_empty() && p != "." && p != "..")
}

pub struct Effect {
    pub name: String,
    pub group: u8,
    pub particles: Vec<Resource>,
}

pub struct Replaced {
    pub name: String,
    pub number: u32,
    pub particle: Resource,
}

/// A mod's effects, cooked: every resource's texture list indexes `textures`.
pub struct Cooked {
    pub textures: Vec<Texture>,
    /// Sorted by name.
    pub effects: Vec<Effect>,
    pub replaced: Vec<Replaced>,
}

fn json_of(bytes: &[u8]) -> Result<Value> {
    serde_json::from_slice(bytes).map_err(|e| refuse("Cook.Model.Json", &[("line", e.line().to_string()), ("column", e.column().to_string())]))
}

/// A mod's effects files (one a layer, in order) cooked over `game`. `image` reads a mod path's file, `None` when the
/// mod has none.
pub fn cook(files: &[Vec<u8>], image: &mut dyn FnMut(&str) -> Option<Vec<u8>>, game: &Game) -> Result<Cooked> {
    // Its caller takes what the cook notes (cook_plan, a mod's check).
    let _reading = super::refuse::reading();
    let mut parts: [BTreeMap<String, Value>; 3] = Default::default();
    const PARTS: [&str; 3] = ["textures", "effects", "replace"];
    for bytes in files {
        let root = json_of(bytes)?;
        let root = pj::object(&root, "/")?;
        pj::only(root, "/", &PARTS)?;
        for (k, part) in PARTS.iter().enumerate() {
            if let Some(v) = root.get(*part) {
                for (name, v) in pj::object(v, &at_key("/", part))? {
                    parts[k].insert(name.clone(), v.clone());
                }
            }
        }
    }
    let [texture_files, effect_values, replace_values] = parts;
    let mut textures = Textures::new(game);
    for (name, file) in &texture_files {
        let at = at_key("/textures", name);
        if !texture_name_ok(name) {
            return Err(refuse("Cook.Effect.TextureName", &[("where", at), ("name", name.clone())]));
        }
        let path = pj::text(file, &at)?;
        if !path_ok(path) {
            return Err(refuse("Cook.Effect.FilePath", &[("where", at), ("uri", path.to_string())]));
        }
        let bytes = image(path).ok_or_else(|| refuse("Cook.Effect.FileMissing", &[("where", at.clone()), ("uri", path.to_string())]))?;
        let (width, height, rgba) = super::gltf::decode_image(&bytes, path)?;
        textures.add(name, pj::texture_from_rgba(name, width, height, &rgba)?);
    }
    let mut effects = Vec::with_capacity(effect_values.len());
    for (name, v) in &effect_values {
        let at = at_key("/effects", name);
        if !effect_name_ok(name) {
            return Err(refuse("Cook.Effect.NameBad", &[("where", at), ("name", name.clone())]));
        }
        let e = pj::object(v, &at)?;
        pj::only(e, &at, &["draw", "particles"])?;
        let group = match e.get("draw") {
            None => 0,
            Some(d) => {
                let here = at_key(&at, "draw");
                let t = pj::text(d, &here)?;
                GROUPS.iter().position(|g| *g == t).ok_or_else(|| {
                    refuse("Cook.Effect.Choice", &[("where", here.clone()), ("value", t.to_string()), ("expected", GROUPS.join(", "))])
                })? as u8
            }
        };
        let here = at_key(&at, "particles");
        let list = pj::list(e.get("particles").ok_or_else(|| pj::missing(&at, "particles"))?, &here)?;
        if list.is_empty() {
            return Err(refuse("Cook.Effect.Empty", &[("where", here)]));
        }
        if list.len() > MOST_EMITTERS {
            return Err(refuse("Cook.Effect.TooMany", &[("where", here), ("count", list.len().to_string()), ("max", MOST_EMITTERS.to_string())]));
        }
        let particles = list.iter().enumerate().map(|(k, p)| pj::particle(p, &at_key(&here, &k.to_string()), None, game, &mut textures)).collect::<Result<_>>()?;
        effects.push(Effect { name: name.clone(), group, particles });
    }
    let mut replaced = Vec::with_capacity(replace_values.len());
    for (name, v) in &replace_values {
        let at = at_key("/replace", name);
        let start = game.particle(name).ok_or_else(|| refuse("Cook.Effect.ReplaceUnknown", &[("where", at.clone()), ("name", name.clone())]))?;
        let number = start.res.user_index;
        let mut particle = pj::particle(v, &at, Some(start), game, &mut textures)?;
        particle.user_index = number;
        replaced.push(Replaced { name: name.clone(), number, particle });
    }
    Ok(Cooked { textures: textures.out, effects, replaced })
}

/// The mod's list of effects: `<name>\t<group>\t<emitters>\n` a line, sorted by name, LF.
pub fn list_text(cooked: &Cooked) -> Vec<u8> {
    cooked.effects.iter().map(|e| format!("{}\t{}\t{}\n", e.name, e.group, e.particles.len())).collect::<String>().into_bytes()
}

/// The particle file and names with mod `id`'s effects in, and what each effect became.
pub struct Applied {
    pub particles: Vec<u8>,
    pub names: Vec<u8>,
    pub facts: Vec<EffectFacts>,
}

/// Mod `id`'s cooked effects over the particle file and names so far (the game's, with earlier mods' particles).
pub fn apply(id: &str, cooked: &Cooked, particles: &[u8], names: &[u8]) -> Result<Applied> {
    let current = super::particles::read_nptc(particles)?;
    let first = super::table::row_count(names)?;
    let total: usize = cooked.effects.iter().map(|e| e.particles.len()).sum();
    let most = usize::from(u16::MAX) + 1;
    if first + total > most {
        return Err(refuse("Cook.Effect.Count", &[("count", (first + total).to_string()), ("max", most.to_string())]));
    }
    let first = first as u32;
    let own = ParticleIr { resources: Vec::new(), textures: cooked.textures.clone() };
    let sprite = |res: &Resource| Roles::of(&own, res).sprite.unwrap_or_default();
    let mut extra = ParticleIr { resources: Vec::new(), textures: cooked.textures.clone() };
    let mut rows = Vec::with_capacity(total);
    let mut facts = Vec::with_capacity(cooked.effects.len() + cooked.replaced.len());
    for e in &cooked.effects {
        let mut numbers = Vec::with_capacity(e.particles.len());
        for (k, p) in e.particles.iter().enumerate() {
            let n = extra.resources.len() as u32;
            let mut p = p.clone();
            p.user_index = n;
            extra.resources.push(p);
            rows.push(json!({ "name": format!("{id}:{}{k:02}", e.name), "id": first + n }));
            numbers.push(first + n);
        }
        let name = format!("{id}:{}", e.name);
        facts.push(EffectFacts { name, particles: numbers, group: e.group, sprite: sprite(&e.particles[0]), textures: cooked.textures.len(), replaced: false });
    }
    let mut merged = super::particles::merge(&current, &extra, first)?;
    let offset = current.textures.len() as u32;
    for r in &cooked.replaced {
        let slot = merged.resources.iter_mut().find(|x| x.user_index == r.number).ok_or_else(|| {
            refuse("Cook.Effect.ReplaceUnknown", &[("where", at_key("/replace", &r.name)), ("name", r.name.clone())])
        })?;
        let mut p = r.particle.clone();
        for t in &mut p.texture_indices {
            *t += offset;
        }
        *slot = p;
        facts.push(EffectFacts { name: r.name.clone(), particles: vec![r.number], group: 0, sprite: sprite(&r.particle), textures: cooked.textures.len(), replaced: true });
    }
    let mut table = super::table::TableIr::from_ntbl(names)?;
    table.add_rows(&rows)?;
    Ok(Applied { particles: super::particles::write(&merged), names: table.to_ntbl(), facts })
}

/// The game's table of the effects actors start by name (effects contract 7, G-E6).
pub const EFFECT_TABLE: &str = "ParticleData/Effect.arc/AutoEffectList.bcsv";

/// G-E6: an EffectName a patch of the effect table writes that would start no emitter, read by 7a's rule. A name with
/// no space that does not end in two digits is numbered: it needs `<name>00`. Any other name is a list of exact
/// particle names. `known` says whether a particle of that name is in the game's names or a mod's. The first such
/// name: the JSON pointer of its cell in the patch, the word, and whether it was numbered.
pub fn table_unknown(patch: &Value, known: &dyn Fn(&str) -> bool) -> Option<(String, String, bool)> {
    let mut cells: Vec<(String, &Value)> = Vec::new();
    for (part, values) in [("set", Some("values")), ("select", Some("values")), ("add", None), ("rows", None)] {
        for (k, entry) in patch.get(part).and_then(Value::as_array).into_iter().flatten().enumerate() {
            let row = match values {
                Some(key) => entry.get(key),
                None => Some(entry),
            };
            if let Some(name) = row.and_then(|r| r.get("EffectName")) {
                let at = format!("/{part}/{k}{}/EffectName", values.map_or(String::new(), |key| format!("/{key}")));
                cells.push((at, name));
            }
        }
    }
    for (at, name) in cells {
        let Some(name) = name.as_str() else { continue };
        let numbered = !name.contains(' ') && !ends_in_two_digits(name);
        if numbered {
            if !name.is_empty() && !known(&format!("{name}00")) {
                return Some((at, name.to_string(), true));
            }
        } else if let Some(word) = name.split(' ').filter(|w| !w.is_empty()).find(|w| !known(w)) {
            return Some((at, word.to_string(), false));
        }
    }
    None
}

/// Any of the game's particles as a mod's effects.json writes them (`lumalink extract`): each under `replace` by its
/// name, so that the file cooked unchanged replaces each with itself.
pub fn extract(ir: &ParticleIr, names: &std::collections::HashMap<String, u32>, wanted: &dyn Fn(&str) -> bool) -> Value {
    let mut by_number: Vec<(&String, &u32)> = names.iter().filter(|(name, _)| wanted(name)).collect();
    by_number.sort_by_key(|(name, n)| (**n, (*name).clone()));
    let mut replace = Map::new();
    for (name, n) in by_number {
        if let Some(res) = ir.resources.iter().find(|r| r.user_index == *n) {
            replace.insert(name.clone(), pj::write(ir, res));
        }
    }
    json!({ "replace": replace })
}

#[cfg(test)]
pub(crate) mod tests {
    use super::super::particle_json::tests::tex;
    use super::super::particles::read_nptc;
    use super::super::refuse::said;
    use super::*;
    use std::collections::HashMap;

    /// A 2x2 PNG.
    pub(crate) fn png() -> Vec<u8> {
        let mut out = Vec::new();
        {
            let mut encoder = png::Encoder::new(&mut out, 2, 2);
            encoder.set_color(png::ColorType::Rgba);
            encoder.set_depth(png::BitDepth::Eight);
            encoder.write_header().unwrap().write_image_data(&[200; 16]).unwrap();
        }
        out
    }

    /// The game: "Glow" (0) drawing "Ring", "Spark" (1) drawing "Dot".
    pub(crate) fn game() -> (ParticleIr, HashMap<String, u32>) {
        let empty = ParticleIr::default();
        let none = HashMap::new();
        let maker = Game { particles: &empty, names: &none, any_name: false };
        let mut textures = Textures::new(&maker);
        textures.add("Ring", tex("Ring"));
        textures.add("Dot", tex("Dot"));
        let mut glow = pj::particle(&json!({ "textures": { "sprite": "Ring" }, "emitter": { "rate": 4 } }), "/g", None, &maker, &mut textures).unwrap();
        let mut spark = pj::particle(&json!({ "textures": { "sprite": "Dot" }, "emitter": { "life": 9 } }), "/s", None, &maker, &mut textures).unwrap();
        glow.user_index = 0;
        spark.user_index = 1;
        (ParticleIr { resources: vec![glow, spark], textures: textures.out }, HashMap::from([("Glow".into(), 0), ("Spark".into(), 1)]))
    }

    pub(crate) fn names_table(names: &[(&str, u32)]) -> Vec<u8> {
        let rows: Vec<Value> = names.iter().map(|(name, id)| json!({ "name": name, "id": id })).collect();
        let text = json!({ "columns": { "name": "string", "id": "int" }, "rows": rows }).to_string();
        super::super::table::TableIr::from_json(text.as_bytes()).unwrap().to_ntbl()
    }

    fn cooked(file: Value, game: &Game) -> Result<Cooked> {
        let picture = png();
        cook(&[file.to_string().into_bytes()], &mut |path: &str| (path == "textures/gold.png").then(|| picture.clone()), game)
    }

    fn rate(res: &Resource) -> f32 {
        f32::from_le_bytes(res.dynamics[0x44..0x48].try_into().unwrap())
    }

    #[test]
    fn effects_become_numbered_particles_after_the_games() {
        let (ir, names) = game();
        let g = Game { particles: &ir, names: &names, any_name: false };
        let file = json!({
            "textures": { "Gold": "textures/gold.png" },
            "effects": {
                "spark": { "particles": [{ "like": "Spark" }] },
                "aura": { "draw": "BLOOM_EFFECT", "particles": [
                    { "like": "Glow", "textures": { "sprite": "Gold" } },
                    { "textures": { "sprite": "Dot" }, "emitter": { "rate": 2 } }] } } });
        let c = cooked(file, &g).unwrap();
        assert_eq!(c.effects.iter().map(|e| (e.name.as_str(), e.group, e.particles.len())).collect::<Vec<_>>(), [("aura", 4, 2), ("spark", 0, 1)]);
        assert_eq!(list_text(&c), b"aura\t4\t2\nspark\t0\t1\n");
        // The aura's first is the game's Glow but for its sprite.
        assert_eq!(rate(&c.effects[0].particles[0]), 4.0);
        let own = ParticleIr { resources: vec![], textures: c.textures.clone() };
        assert_eq!(Roles::of(&own, &c.effects[0].particles[0]).sprite.as_deref(), Some("Gold"));

        let jpc = super::super::particles::write(&ir);
        let applied = apply("gold.mario", &c, &jpc, &names_table(&[("Glow", 0), ("Spark", 1)])).unwrap();
        let after = read_nptc(&applied.particles).unwrap();
        assert_eq!(after.resources.iter().map(|r| r.user_index).collect::<Vec<_>>(), [0, 1, 2, 3, 4]);
        let index = pj::names_index(&applied.names).unwrap();
        assert_eq!((index["gold.mario:aura00"], index["gold.mario:aura01"], index["gold.mario:spark00"], index["Glow"]), (2, 3, 4, 0));
        assert_eq!(rate(&after.resources[3]), 2.0);
        assert_eq!(Roles::of(&after, &after.resources[2]).sprite.as_deref(), Some("Gold"));
        assert_eq!(Roles::of(&after, &after.resources[3]).sprite.as_deref(), Some("Dot"));
        assert_eq!(applied.facts[0], EffectFacts { name: "gold.mario:aura".into(), particles: vec![2, 3], group: 4, sprite: "Gold".into(), textures: 3, replaced: false });
    }

    #[test]
    fn a_replaced_particle_keeps_its_number_and_a_later_layer_wins() {
        let (ir, names) = game();
        let g = Game { particles: &ir, names: &names, any_name: false };
        let first = json!({ "textures": { "Gold": "textures/gold.png" }, "replace": { "Glow": { "emitter": { "rate": 9 } } },
                            "effects": { "aura": { "particles": [{ "like": "Glow" }] } } });
        let second = json!({ "replace": { "Glow": { "textures": { "sprite": "Gold" } } } });
        let picture = png();
        let c = cook(&[first.to_string().into_bytes(), second.to_string().into_bytes()], &mut |_| Some(picture.clone()), &g).unwrap();
        assert_eq!(c.replaced.len(), 1);
        assert_eq!((c.replaced[0].name.as_str(), c.replaced[0].number), ("Glow", 0));
        // The later layer's Glow is used whole: the game's rate, the Gold sprite.
        assert_eq!(rate(&c.replaced[0].particle), 4.0);
        let applied = apply("gold.mario", &c, &super::super::particles::write(&ir), &names_table(&[("Glow", 0), ("Spark", 1)])).unwrap();
        let after = read_nptc(&applied.particles).unwrap();
        assert_eq!(after.resources.len(), 3);
        let glow = after.resources.iter().find(|r| r.user_index == 0).unwrap();
        assert_eq!(Roles::of(&after, glow).sprite.as_deref(), Some("Gold"));
        assert_eq!(pj::names_index(&applied.names).unwrap()["Glow"], 0);
        assert!(applied.facts.iter().any(|f| f.replaced && f.name == "Glow" && f.particles == [0]));
    }

    #[test]
    fn the_games_particles_extracted_and_cooked_are_the_same() {
        let (ir, names) = game();
        let g = Game { particles: &ir, names: &names, any_name: false };
        let file = extract(&ir, &names, &|_| true);
        let c = cook(&[serde_json::to_vec_pretty(&file).unwrap()], &mut |_| None, &g).unwrap();
        let applied = apply("copy", &c, &super::super::particles::write(&ir), &names_table(&[("Glow", 0), ("Spark", 1)])).unwrap();
        let after = read_nptc(&applied.particles).unwrap();
        for (a, b) in ir.resources.iter().zip(&after.resources) {
            assert_eq!((a.user_index, &a.dynamics, &a.shape.record), (b.user_index, &b.dynamics, &b.shape.record));
            assert_eq!(Roles::of(&ir, a), Roles::of(&after, b));
        }
    }

    #[test]
    fn a_file_says_what_it_cannot_be() {
        let (ir, names) = game();
        let g = Game { particles: &ir, names: &names, any_name: false };
        let refused = |file: Value| said(&cooked(file, &g).err().unwrap());
        let one = || json!([{ "like": "Glow" }]);
        let own = |pairs: &[(&'static str, &str)]| pairs.iter().map(|(n, v)| (*n, v.to_string())).collect::<Vec<_>>();
        assert_eq!(said(&cook(&[b"{".to_vec()], &mut |_| None, &g).err().unwrap()).0, "Cook.Model.Json");
        assert_eq!(refused(json!({ "effect": {} })), ("Cook.Effect.UnknownPart", own(&[("where", "/"), ("key", "effect"), ("expected", "textures, effects, replace")])));
        assert_eq!(refused(json!({ "effects": { "aura01": { "particles": one() } } })), ("Cook.Effect.NameBad", own(&[("where", "/effects/aura01"), ("name", "aura01")])));
        assert_eq!(refused(json!({ "effects": { "Aura": { "particles": one() } } })).0, "Cook.Effect.NameBad");
        assert_eq!(refused(json!({ "effects": { "aura": { "particles": [] } } })), ("Cook.Effect.Empty", own(&[("where", "/effects/aura/particles")])));
        let many: Vec<Value> = (0..17).map(|_| json!({})).collect();
        assert_eq!(refused(json!({ "effects": { "aura": { "particles": many } } })), ("Cook.Effect.TooMany", own(&[("where", "/effects/aura/particles"), ("count", "17"), ("max", "16")])));
        assert_eq!(refused(json!({ "effects": { "aura": { "draw": "2D", "particles": one() } } })).0, "Cook.Effect.Choice");
        assert_eq!(refused(json!({ "effects": { "aura": {} } })), ("Cook.Effect.Missing", own(&[("where", "/effects/aura"), ("key", "particles")])));
        assert_eq!(refused(json!({ "replace": { "Nope": {} } })), ("Cook.Effect.ReplaceUnknown", own(&[("where", "/replace/Nope"), ("name", "Nope")])));
        assert_eq!(refused(json!({ "textures": { "Gold": "../gold.png" } })), ("Cook.Effect.FilePath", own(&[("where", "/textures/Gold"), ("uri", "../gold.png")])));
        assert_eq!(refused(json!({ "textures": { "Gold": "gone.png" } })), ("Cook.Effect.FileMissing", own(&[("where", "/textures/Gold"), ("uri", "gone.png")])));
        assert_eq!(refused(json!({ "textures": { "Gold Leaf": "textures/gold.png" } })).0, "Cook.Effect.TextureName");
        assert_eq!(source_of("/Luma/gold.mario/effects.json"), Some("gold.mario"));
        assert_eq!(source_of("Luma/gold.mario/fx/effects.json"), None);
    }

    #[test]
    fn checking_with_no_game_takes_any_game_name() {
        let ir = ParticleIr::default();
        let names = HashMap::new();
        let g = Game { particles: &ir, names: &names, any_name: true };
        let c = cooked(json!({ "effects": { "aura": { "particles": [{ "like": "Anything", "textures": { "sprite": "SomeGameTexture" } }] } },
                                 "replace": { "Glow": { "emitter": { "rate": 1 } } } }), &g).unwrap();
        assert_eq!((c.effects.len(), c.replaced.len()), (1, 1));
    }

    #[test]
    fn a_table_row_naming_no_particle_is_found() {
        let known = |name: &str| ["Glow00", "Glow01", "Spark", "gold.mario:aura00"].contains(&name);
        let none = |patch: Value| table_unknown(&patch, &known);
        assert_eq!(none(json!({ "add": [{ "UniqueName": "A", "EffectName": "Glow" }, { "EffectName": "gold.mario:aura" }] })), None);
        assert_eq!(none(json!({ "set": [{ "where": { "UniqueName": "A" }, "values": { "EffectName": "Spark Glow01" } }] })), None);
        assert_eq!(none(json!({ "set": [{ "where": {}, "values": { "Joint": "x" } }] })), None);
        assert_eq!(
            none(json!({ "add": [{ "EffectName": "Glow" }, { "EffectName": "Nope" }] })),
            Some(("/add/1/EffectName".to_string(), "Nope".to_string(), true))
        );
        assert_eq!(
            none(json!({ "select": [{ "match": {}, "values": { "EffectName": "Spark Nope01" } }] })),
            Some(("/select/0/values/EffectName".to_string(), "Nope01".to_string(), false))
        );
        // Two digits at the end: one particle's exact name.
        assert_eq!(none(json!({ "rows": [{ "EffectName": "Glow02" }] })), Some(("/rows/0/EffectName".to_string(), "Glow02".to_string(), false)));
    }
}
