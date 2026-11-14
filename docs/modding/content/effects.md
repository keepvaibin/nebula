# Content: particle effects

## More rules

### Effects

Sections 1 to 6: what the game does with a cooked effect, and the calls. Section 7: the source form and the cook.

### 1. What a mod's effect is on screen

- A mod's effect is cooked into the game's own particle data (the merged effect file and its name rows, as the
  game's: particles::merge, overlay.rs), and drawn by the game's particle code: no second particle system.
- Its name is `<id>:<name>` (luma-format mod paths). A mod that changes a game particle in place uses `replace`
 : the cooked particle keeps the game particle's number and name, so the game's own tables start it. `like`
  only starts a new particle from a game particle's records.
- Bound by a call, never by a table the modder must also edit. The game's own actors start effects through their
  `Effect` tables (AutoEffectList); a mod's code starts its effects with section 3's calls, which register the effect
  on the actor itself (EffectKeeper::registerEffect, as ModForm does today) the first time it is started. A mod
  that replaces a game particle changes what the game's table starts; nothing to add.

### 2. The failures made loud

Each is a mod-log entry (source CONTENT, Error, the row named) and, while the harness runs, a fact; none is silent:

| What | When found | Row |
|---|---|---|
| A literal name is in neither the mod's effects nor the game's names | the validator, before any run | the validator's references check |
| A built name (not a literal) is in no store | at the call | `Content.MissingEffect` (references 4: the placeholder effect starts in its place) |
| The joint is not on the actor | at the call | `Content.MissingJoint` (the effect starts at the actor's base) |
| The effect draws nothing: no particle alive (EmitterCore::getParticleNumber 0 on each emitter) for the 60 frames (a tunable) after the emitter's own first frame (DYNAMICS `first_frame`), while the actor is drawn | at the end of that window | `Content.EffectEmpty` "{mod}: the effect "{found}" started but shows nothing (no particle was made)" (new, 14 languages) |
| A role's texture is not in the effect file | at the cook (section 7) | `Cook.Effect.NoTexture` |

`Content.EffectEmpty` is checked once per effect name a session (the first start), so a check costs a counter per
emitter for its window and nothing after. An effect stopped before its window ends is not checked. It
catches the aura's kind of fault from the other side: whatever made the particle invisible in the data, the player
and the modder are told which effect.

### 3. The calls: the service `content.fx` 1.0

How the calls reach a mod. A catalogued service (services-registry 2, 9), not a LumaCore member:
`content.fx` 1.0, no permission, provided by the module, bound by an import row of kind 5, in the
self-description; LumaKit's `C.fx` is LumaScript over it.

```c
/* luma_content_fx.h, in the registry's one form (services-registry 9). LumaActor as models 7 (luma_content.h). */
/* In luma_content.h: an offset in game units and a turn in degrees (X, Y, Z, composed as the game composes a
   joint's), in the joint's space. */
typedef struct LumaPlace {
    uint32_t size;
    float offset[3];
    float turn[3];
} LumaPlace;
typedef struct LumaContentFxService {
    LumaServiceTable base;
    /* The effect `name` (a LumaEffectName: lifted and checked by the validator) on `actor`: at the joint `joint`
       (null: the actor's base), offset and turned by `place` (null: none), followed each frame (`follow` 1) or left
       where it started (0). LUMA_OK with *out, a handle owned by the mod; the placeholder's handle on a missing
       built name (section 2). */
    int32_t (*start)(const LumaMod* mod, LumaActor actor, LumaEffectName name, const char* joint,
                     const LumaPlace* place, int32_t follow, LumaHandle* out);
    /* The same on a model the mod attached (models 7), at its joint or socket `socket`. An ended model
       handle is LUMA_E_HANDLE; an unknown socket Content.MissingJoint, the effect at the model's root. */
    int32_t (*start_on_model)(const LumaMod* mod, LumaHandle model, LumaEffectName name, const char* socket,
                              const LumaPlace* place, int32_t follow, LumaHandle* out);
    /* Stops making particles; those alive fade as the effect says (`now` 0), or all go at once (`now` 1). */
    int32_t (*stop)(const LumaMod* mod, LumaHandle effect, int32_t now);
    /* Scale and colour on top of the effect's own (1, 1, 1, 1 none), as the game's setEffectBaseScale / Color. */
    int32_t (*tint)(const LumaMod* mod, LumaHandle effect, float scale, uint32_t rgba);
} LumaContentFxService;
```

- Game thread (services-registry 4); off it, `LUMA_E_THREAD`, reported.
- A handle is the mod's. It ends at `stop`, at the actor's end (its effects stop with it, as the game's do), at the
  end of the model it was started on, at a scene change, and at the mod's end (mod_gone stops all). A call on an
  ended handle is `LUMA_E_HANDLE`, never a crash.
- `name` may be a game effect name too (`"Glow"`): started as the game starts it, by the game's name rule (section 7a).
- One effect name started twice on one actor is two handles (two emitters); the game's own one-per-name rule applies
  to its table's effects only.
- Before registering, the run time looks up every particle the name resolves to; one missing is MissingEffect for the
  whole effect (the placeholder), never an emitter with no resource (see 7a on the game's own space list).

### 4. Draw groups, order, light

- The group is the game's `DrawOrder` (AutoEffectInfo.cpp sDrawOrderDataTable; MultiEmitter::setDrawOrder sets each
  emitter's group id). An actor's effect takes one of the six 3D groups, by the game's names:

  | Name | Group | What it is |
  |---|---|---|
  | `3D` | 0 | the default: drawn in the scene, paused with it |
  | `PAUSE_IGNORE` | 1 | drawn in the scene, keeps stepping while the game is paused |
  | `INDIRECT` | 2 | drawn in the indirect (screen-warp) pass |
  | `AFTER_INDIRECT` | 3 | drawn after the indirect pass (over warped water, heat haze) |
  | `BLOOM_EFFECT` | 4 | drawn into the bloom source |
  | `AFTER_IMAGE_EFFECT` | 5 | drawn into the after-image pass |

  `2D`, `2D_PAUSE_IGNORE` (6, 7: the screen pass, ParticleDrawExecutor::draw2D) and `FOR_2D_MODEL` (8: with the
  screen's 3D models) are screen groups for layouts (PaneEffectKeeper), not for an actor's effect, so the cook
  refuses them with `Cook.Effect.Choice` listing the six until a screen effect call exists. (Group 1 and 7 step
  through a pause: ParticleEmitterHolder::update and requestMovementOnAllEmitters.)
- The group is the source's (`draw`), written to `<id>:effects` and set by the run time at registration
  with `setDrawOrder`, as setupMultiEmitter does for a table's effect; not at the call.
- Effects pause and slow with the scene (services-time: their emitters step with the world's steps), but for
  `PAUSE_IGNORE`, which steps through a pause as the game's do.
- RT: a mod's effect is an emitter of light as the game's are: nothing for the mod to do.

### 5. No cost with no mod

No mod effect started: no registration, no check counters, nothing per frame.

### 6. Tests

| Test | Must show |
|---|---|
| 3501 | `<id>:spark` started on Mario's `HandR`: emitter alive, particles above 0 within 10 frames, following the hand |
| 3502 | a built unknown name: `Content.MissingEffect` once, the placeholder effect alive at the actor |
| 3503 | an unknown joint: `Content.MissingJoint`, the effect at the base |
| 3504 | an effect whose emitter makes no particle (rate 0): `Content.EffectEmpty` once, at its first frame + 60 |
| 3504b | an emitter with `first_frame` 90 and a working rate: no `Content.EffectEmpty` |
| 3505 | the Gold Flower's aura in roles: particles alive, the sprite GoldSpark (fact `cook.effect`, a picture of it) |
| 3506 | the actor ends: its mod effects stop, their handles answer LUMA_E_HANDLE after |
| 3507 | no mod: no effect state, no counters |
| 3508 | a mod `replace` of a game particle the game's table starts (a Kuribo's): the replaced look, no mod call |
| 3509 | an effect of three emitters in group `AFTER_INDIRECT`: three emitters, each with group id 3 |
| 3510 | `start_on_model` at an attached model's socket `tip`; the model detached: the effect's handle answers LUMA_E_HANDLE |

Facts the tests read, for the harness table, written only while the harness runs:
- `fx.start.<handle>` {"mod", "name", "particles": [numbers], "group", "joint", "follow"} at each start, and
  `fx.end.<handle>` {"why": "stop"|"actor"|"model"|"scene"|"mod"} when it ends (3501, 3506, 3509, 3510);
- `fx.alive.<handle>` {"frame", "particles", "at": [x, y, z], "joint_at": [x, y, z]} once a frame while a test asks
  with the step `{"facts": "fx.alive"}` (3501: particles above 0 and `at` on `joint_at`);
- the check of section 2 as its mod-log entry (`modlog` fact, row `Content.EffectEmpty`, 3504, 3504b);
- `cook.effect` (section 7); no `fx.*` fact with no mod (3507).

### 6a. Rules the source form keeps

- `like` starts a particle from a game particle: it copies its records, and the result is a new particle numbered
  after the game's. `"replaces": "<game particle name>"` replaces a game particle in place: the cooked particle takes
  that particle's number and its name row stays, so the game's tables (AutoEffectList) start it with nothing to add.
  Two mods replacing one particle follow the overlay order, as two mods' files at one path do: the higher wins, and
  the lower is named in the overlap fact.
- An emitter may start late on purpose (DYNAMICS `first_frame`, 0x68). EffectEmpty's 60 frames count from
  `first_frame`, not from the start call, and an effect stopped before its window ends is not checked.
- The records point into the texture list at four places, the four roles: SHAPE `texture_index` (0x50, `sprite`),
  CHILD `texture_index` (0x45, `child`), EXTEX `second_texture` (0x1A, `second`) and EXTEX `indirect_texture`
  (0x19, `indirect`).
- A particle's name is part of the particle, so the cook writes the name rows; a mod has no names file.
- The validator checks a literal effect name against the mod's effects file and the game's names before any run;
  MissingEffect at the call is left for names built while running.

### 7. Source form and cook

The file. One per mod: `effects.json` at the mod's root (luma-format: a mod's content file). There is no merge
folder and no names file.

```json
{ "textures": { "GoldSpark": "textures/gold_spark.png" },
  "effects": {
    "aura": { "draw": "3D",
              "particles": [ { "like": "Glow",
                               "textures": { "sprite": "GoldSpark", "second": "mr_noise00_i" },
                               "emitter": { "rate": 4 },
                               "shape": { "primary": "ffd040ff" } } ] },
    "spark": { "particles": [ { "texture_frames": ["Spark0", "Spark1", "Spark2"], "emitter": { "life": 30 } } ] } },
  "replace": {
    "Glow": { "textures": { "sprite": "GoldSpark" } } } }
```

- `effects`: each key is a name `[a-z][a-z0-9_]*`, started as `<id>:<name>` (LumaEffectName).
  - `particles` are its emitters, in order; at least one, at most 16.
  - `draw` is the draw group (section 4): `"3D"` (default), `"3D_indirect"`, `"2D"`, by the names of the game's
    groups.
  - The cook names the particles itself: `<id>:<name>`, and `<id>:<name>#<k>` for the k-th emitter after the first. It
    writes their ParticleNames rows and the effect's emitter list, in the form the game's effect start reads. J
    confirms that form (an AutoEffectList row's names, joined as the game joins them).
- `replace`: each key is a game particle's name, and its value is a particle written over that particle's own records.
  Unknown names are refused.
- A particle:
  - `like` (a game particle to start from, else the plain glowing dot of today);
  - `emitter`, `shape`, `extra`, `child`, `second_texture`, `fields`, `keys`;
  - `primary_colors`, `environment_colors`;
  - `textures`, `texture_frames`.
  - Every value of every record has a name: the DYNAMICS, SHAPE, EXTRA, CHILD, EXTEX and FIELD lists of
    particle_json.rs. Phase 10's reference table is generated from those lists, not written by hand.
  - `null` for `extra`, `child` or `second_texture` takes the record away.
  - A value given replaces that value only. Derived values are derived: rates, tables, counts.
- Roles, never indices:
  - `"textures": {"sprite", "child", "second", "indirect"}` names the texture each role uses.
  - `"texture_frames": [names]` is a sprite animation. Its names join the list, and `sprite` defaults to the first.
  - The cook builds the resource's list (without duplicates) and writes the four indices and the frame table.
  - A role over `like` replaces only that role.
  - `shape.texture_index`, `child.texture_index`, `second_texture.second_texture` and
    `second_texture.indirect_texture` are refused, naming the role to write: `Cook.Effect.RawIndex` {particle, field,
    role}.
  - A list for `textures` (the positional form) is refused, naming the four roles: `Cook.Effect.Positional`.
  - `second` or `indirect` without `second_texture.second_on` or `indirect_on` is a Note, `Cook.Effect.RoleOff`
    {role}: the texture is named but the record does not draw it.
- Textures: the file's own (`"textures"`: name to a PNG or JPEG in the mod), or the game's particle textures by name.
  Cooked as today: RGBA8 with levels, clamped edges, the record named for texture packs.
- `lumalink extract` writes any of the game's particles in this form: roles, its name, every value. A mod can start
  from a copy. LumaLink's report prints one line per role a `like` changed, e.g. `aura: sprite GoldSpark (was
  mr_noise00_i)`.

The cook. It runs in nebula-import (the host's start-check step 4) and in `lumalink check`/`pack`. LumaLink has
no game, so a game name stands for itself there, as particle_json::check does today.

- New particles are added after the game's and other mods' particles (`add_particles`). Their name rows are written
  by the cook. Replaced particles keep their number.
- The emitter list per effect, `<id>:<name>` to its particle names and draw group, is written to the store as
  `<id>:effects` for luma_effect_start.

Refusals. L804 with a `why` phrase in 14 languages. Each names the file, the effect and the
emitter number, and stops the whole file: no half-merged particle set.

- `Cook.Effect.UnknownPart` {particle, key, parts}
- `Cook.Effect.UnknownValue` {record, key, names}
- `Cook.Effect.Range` {value, min, max}
- `Cook.Effect.Choice` {value, list}
- `Cook.Effect.Color` {value}: not `rrggbbaa`
- `Cook.Effect.Keys` {track}: frames must rise, at least one
- `Cook.Effect.NoTexture` {name}: neither the file's nor the game's
- `Cook.Effect.LikeUnknown` {name}
- `Cook.Effect.ReplaceUnknown` {name}
- `Cook.Effect.RawIndex`
- `Cook.Effect.Positional`
- `Cook.Effect.Frames` {count}: more than 255
- `Cook.Effect.NameBad` {name}
- `Cook.Effect.Empty` {effect}: no particles
- `Cook.Effect.TooMany` {effect}: over 16
- `Cook.Effect.Count`: past u16, the particle numbers
- The image's own (from gltf::decode_image: kind, size, read)

`Cook.Effect.RoleOff` is a Note.

Facts the tests read. For each effect, a mod-log COOK fact `cook.effect` {file, effect, particles (their
numbers), textures added, replaced (game names)}. 3505 reads the aura's sprite role by name. 3507 reads that there is
none.

Tests (in nebula-import).
- One per phrase.
- Each role over `like` changing that role only.
- The aura source, against the game's Glow, gives the same records as today's hand form except for its sprite.
- `replaces` keeps the number.
- Two emitters named `#1`.
- extract then cook gives the same bytes for every game particle (all of them, one test).

The placeholder (`data/placeholder/effect.json`) is one
effect `placeholder`, one plain particle.

- A table row naming a particle no one has. After the overlay merge of
  `ParticleData/Effect.arc/AutoEffectList.bcsv` (every mod's patch in list order), the cook reads each row a mod's
  patch added or changed, splits its EffectName by 7a, and refuses
  - a list word that is a particle in neither the game's names nor the merged ones, and
  - a numbered name with no `<name>00` in either (allocateEmitter would make no emitter at all), with
    `Cook.Effect.TableUnknown` {row, word}, naming the mod and its patch file (the row's mod is the highest in the
    overlay order that wrote the cell). Game rows no mod touched are not read. Words joined by the game's own rows
    stay the game's.

### 7b. How the service plays an effect

- not the actor's EffectKeeper. The keeper's size is fixed when the actor is made
  (EffectKeeper's constructor: the table's effects plus the class's extra count), so a mod's effect cannot be
  registered on any actor. content.fx owns it: the game's MultiEmitter with an AutoEffectInfo of its own, as a table's
  effect has (flags AFFECT_TRANS/ROTATE/SCALE at its making, FOLLOW_* too while `follow`; offset; draw order), so the
  game's MultiEmitterCallBack follows it. What the keeper does for the actor's own effects the service does for these:
  at the actor's death `playCalcAndDeleteForeverEmitter` (EffectKeeper::clear), on the clip
  `stopEmitterOnClipped`/`playEmitterOffClipped`. Both are seen at the frame's start (GameSystem::update, after the
  services' events), so they act one frame after the game's own.
- the place. A joint's matrix, else the actor's base matrix (with the actor's scale, as the
  table's), else its position and rotation. A turn composes `source x turn` each frame while it follows (once when
  not); the offset stays in the joint's space (the turn's inverse applied first).
- values. `follow` and `now` other than 0 or 1: LUMA_E_ARGUMENT, reported. `tint`'s scale not
  above 0 or not finite: LUMA_E_ARGUMENT, reported. A dead actor, or one whose class is not LiveActor's: argument
  "actor". Open handles: 256 a mod (`LUMA_CONTENT_FX_MOST`), 4096 for all mods together.
- the placeholder is `nebula:placeholder/effect` (references 4). When the store lacks it too,
  nothing shows; the handle works (stop, tint) and fx.start says `"placeholder": true`.
- Content.EffectEmpty is code 640 (references 6's effects block), row in 14 languages.
- fx.alive without a step. Written each frame for a handle's first 120 frames while the harness
  runs (no step to add to the harness); fx.start gains `"placeholder"`. fx.end's `why` is `stop`, `actor`, `scene` or
  `mod` (`model` with models 7).
- the group, read from the cook's `Luma/<id>/effects.txt` (below, test 3509).
- start_on_model: on a model handle from content.model (below, test 3510).


### 7c. The effects list

- Path: `Luma/<id>/effects.txt` in the mod's store (the overlay the cook writes for the mod; a plain file, no
  archive), looked up with the store's file lookup. It is absent when the mod has no `effects.json`.
- Form: UTF-8, LF line ends, no header, no blank line. One line per effect of the mod's effects.json, sorted by name
  in byte order: `<name>\t<group>\t<emitters>\n`. `<name>` is the effect's own name (`[a-z][a-z0-9_]*`, no `<id>:`),
  `<group>` 0 to 5 as section 4's table, and `<emitters>` 1 to 16. The cook refuses everything else before it writes,
  so the reader may treat any other line as a damaged store: say Content.EffectEmpty's sibling for a bad file once per
  mod, naming the line, and draw group 0.

- `typedef LumaHandle LumaContentFxEffect;` in luma_content_fx.h; start's out is `LumaContentFxEffect* out`,
  stop's and tint's parameter `LumaContentFxEffect effect`. start_on_model's model stays `LumaHandle` until models 7
  names content.model's kind; then it takes that kind (a 1.0 change before any release).
- `follow` and `now` are `bool` (stdbool). The service's 0-or-1 checks are gone (a C++ bool is 0 or 1).
- every member's comment names every status it returns: LUMA_OK, LUMA_E_ARGUMENT, LUMA_E_HANDLE, LUMA_E_LIMIT,
  LUMA_E_THREAD, LUMA_E_CANCELLED as each can.
- The group comes from `Luma/<id>/effects.txt` as written above; a line outside the form: one
  Content.EffectEmpty-sibling note per mod naming the line, group 0.

### 7d. The effects list reader

- A mod effect `<id>:<name>` takes its group from `Luma/<id>/effects.txt`, read once a session per mod; no list: group
  0.
- start_on_model: the model handle from content.model; its joints are the sockets; the effect ends "model" when the
  model's handle ends. fx.start facts carry `model` (0: on an actor). Test 3510.

### 7e. Section 7 as the cook does it

Where section 7 above says otherwise, this holds:
- Place. `layers/<layer>/assets/effects.json` (the one form has no content at the mod's root). luma-format 5: a
  layer's `assets/<mod path>` is the store's `Luma/<id>/<mod path>`.
- Layers. A mod's files are one: a later layer's texture, effect or replacement of a name is used whole in place of
  the earlier's. A texture's file is a mod path (no `..`, `\`, `:`), read from `Luma/<id>/<path>` (the last layer's).
- Replace starts from the game's own particle (not an earlier mod's version), so the later mod's is used whole; `like`
  inside a replacement starts it from another game particle. `like` and `replace` reach the game's particles only.
  Note `Cook.Effect.Overlap` {name, other, winner} names the earlier mod.
- Texture list. It starts as the start particle's list (so an unchanged particle is the same bytes), and a role naming
  a texture not in it adds it. The sprite role may not be null; child, second and indirect may (taken away). RoleOff
  (a Note) also covers `child` with no child record.
- Effects list. `Luma/<id>/effects.txt` as 7c writes it; absent when the mod has no effect of its own (only
  replacements).
- Phrases. Every refusal is L804, `why` a `Cook.Effect.<Kind>` phrase and `where` the value's JSON pointer in the file
  (it names the effect and the emitter: `/effects/aura/particles/1/emitter/rate`). Added to the list above: Kind
  {where, expected: phrase Is.Number / Is.Whole / Is.Bool / Is.Text / Is.List / Is.Object}, Length, Missing,
  TextureName, FilePath, FileMissing, Overlap (Note), OldForm (a `.jpc.json` merge), TableNoFirst. A bad JSON file is
  `Cook.Model.Json` {line, column}. 33 rows, 14 languages.
- A table row naming an unknown particle, with the phrase split by 7a's rule: `TableNoFirst` {where, word} (numbered, no `<word>00`) and
  `TableUnknown` {where, word} (a list word); `where` is the cell's pointer in the patch. The patch is left out before
  the patches apply, after every mod's effects are cooked (a mod's table row may start its own effect).
- Facts. `cook.effect` in the facts file: `other` = `<id>:<name>` (or the replaced game particle), then
  `particles=<n,n>`, `group`, `sprite`, `textures`, `replaced` (0/1). 3505 reads `sprite=`.
- LumaLink. check and pack cook every layer's effects.json (with `--game`, against its particles; without, any game
  name passes) and report the findings on the last layer's file. `extract` of ParticleData/Effect.arc writes the
  particles as an effects.json: each under `replace` by name, every value, textures by role.
- Tests (nebula-import and LumaLink): one per phrase; roles over like; frames; written-and-replaced gives
  the same particle; extract then cook; numbering and names; later layer wins; a plan through write_overlay (list,
  particles, names, facts, refused mod isolated, overlap); the table row check through write_overlay. The extract-then-cook test runs
  on fixture particles: all of the game's 1:1 needs the game's store.
