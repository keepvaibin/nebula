---
title: References to game objects
shortTitle: References to game objects
intro: How a mod keeps references to game objects safely.
contentType: reference
---

## About references to game objects

A mod names models, effects, sounds and other content by name. This page describes how those references are checked and resolved, the placeholders used when a name is missing, and the mod log codes from 620 to 699.

## What a reference is, and the one rule

A reference is a name in a mod that must find something: in the game's files, in the mod's own files, in another
mod it `needs`, or in something code makes at start (a derived class).

The rule. A reference that does not resolve never stops the game: the mod stays on, the thing it named is replaced by
a placeholder of its kind where one exists, and the mod log has one Error entry naming the mod, the file, the
line (or JSON pointer), the kind and the name, and what was expected. Texts have no placeholder
. A file of the mod the cook cannot make (a model whose image is missing or unreadable, a picture that does
not read) is not a placeholder's: LumaLink's check and its pack refuse the mod (L804, the cook's reason); at a start
the cook leaves the file out and the mod log has the same L804 Error on the mod (source COOK), the mod's other files
apply, and a name of the left-out file then answers as a missing one (a model: the placeholder model, 621).

## The kinds, and who resolves each

| Kind | Where it is written | Resolves against | Checked by |
|---|---|---|---|
| texture of a model | a glTF's images (embedded or a URI inside the mod) | the mod's files | validator, L600-L619 block |
| texture of a material, program of a material | a material source, a shader name | the mod's files, the game's programs, the mod's shaders | validator, L620-L649 |
| texture of a particle; an effect's particles | the effects source | the mod's files, the game's particle textures by name | validator, L650-L669 |
| joint | an effect or socket bound to a joint; a character bound by joint names | the model it is bound to (the game's or the mod's) | validator, L600-L619 and L650-L669 |
| animation | an actor's animation by name | the model's archive | validator, L670-L679 |
| sound | a sound by name | the game's sound table, the mod's sounds | validator, L680-L689 |
| text | a message id (3.8; a placement's message, a layout's text) | the game's message tables in each language, the mod's added lines | validator, L690-L699 |
| object class | a placement row's class name | the game's factory (self-description) and classes mods make | validator (an `after_start` row for a class no game list names), then the game after the mods' start |
| file | a whole file or patch target (luma.json 5) | the game's store | validator, L302 (exists) |
| a name code passes at run time | `assets`, `draw`, `audio`, LumaKit calls taking a name | as its kind | the call itself, and the validator for names the compiler lifts |

This contract asks one thing of every such rule: its finding carries `kind`, `found` (the name) and `expected` (where
it was looked for) as parameters, so the game's placeholder step can read from the kept report which names
to replace, without a second check.

## When the game resolves

- At import and at each start: the validator's start check runs every reference rule with the store, the
  self-description and the installed mods. The self-description is the `luma` folder beside the game's program (an
  installed game's `versions/<v>/luma`), read as LumaLink's `--names` reads it: the function rows, `luma_types.json`,
  `luma_services.json` and the placement facts `smg_game.luma-placement.json` (LumaValidateInput `names`, `types`,
  `services`, `placement`), so the game and LumaLink judge a mod alike. Without the folder (a development build, a
  device with none) those rules go unchecked; one the validator refuses is said on stderr and the mod is checked as
  without it. Its findings become mod-log entries (source VALIDATOR, at the title's
  summary). A start's check reads the kept report for the rules that read the whole `.luma`; reference rules that read
  the store or other mods run again at every start, so a game update or another mod's removal that breaks a reference
  is found then.
- After start: object classes. A row of a stage's placement table (`jmp/Placement/<layer>/` or
  `jmp/MapParts/<layer>/`, not the zone list `StageObjInfo`; a patch's `add`, `rows`, `set` and `select`, or a whole
  table in `files/`) names its object's class. A class that neither the game's factory list (the placement facts'
  `factory`) nor its planet table names, and that is not made by its shape (`ShapeModelNo` other than
  -1), may be one a mod's code makes. The validator keeps it in the mod's report as an `after_start` row (class,
  file, and the line of `name` in a patch or the row's number from 1 in a whole table) and counts it as not checked
  (L403, `Missing.AfterStart`; `Missing.Classes` when the factory list was not given, and then every placement but
  a planet's waits). After the last mod's start, before any scene, the host gives every mod's rows to the game
  module's `luma_refs_check_after_start` (luma_refs.h), once; the game asks its factory (with every mod's classes)
  for each. The planet table is a scene's, so planets are the validator's to know: it reads them from the store's
  table as the mod's own files and patches leave it (a whole table stands for the game's; a patch's rows and values
  add their `PlanetName`), whether or not the factory list is given. A class no mod made is `Content.MissingClass` in the mod log,
  and each placement of it makes the placeholder object.
- Nothing is resolved lazily for content: by the title every content reference of every mod on is either resolved or
  has its placeholder decided. A stage loaded later reads the decision, it does not look again.

## Placeholders

The placeholder model is the object archive `NebulaPlaceholder` (the game's `ObjectData/NebulaPlaceholder.arc` as
the store has it), cooked from `data/placeholder/model.gltf` with 3.2's model path; the placeholder object loads it by
that name.

| Kind | Placeholder | Made by |
|---|---|---|
| model (new: an object the game does not have) | `nebula:placeholder/model`: a 100-unit cube, a magenta and black checker on each face with a white "?" | the game, when content.model is given a name no store has; a mod file the cook cannot read is refused (L804), not replaced |
| model (replacing one of the game's) | the game's own model | the cooker keeps the game's file and leaves the mod's out (the mod's other files still apply) |
| texture | `nebula:placeholder/texture`: 64 x 64, magenta and black 8-pixel checker, opaque | the draw service, for a texture name it does not find; a glTF naming a missing image is refused by the cook (`Cook.Material.MissingTexture`, L804) |
| material program | the default lit program with the material's base color; never the missing pass | the game, when the program is absent at load (3.13 refuses the start if the compiler itself is absent) |
| particle effect | `nebula:placeholder/effect`: a slow ring of 8 white and magenta sparks, 50 units across, at the binding point | the game, when an effect name does not resolve |
| particle texture | the texture placeholder | the cooker |
| joint | the model's root joint | the game, at binding |
| animation | the model's bind pose (a mod's new actor) or the game's own animation of that name (a replacement) | the game, at the animation's start |
| sound | `nebula:placeholder/sound`: a 120 ms soft two-note chime (`NebulaData/PlaceholderSound.nsfx`, cooked from `data/placeholder/sound.wav`), at the call's place and bus | the audio service, when `play`, `play_at` or `music` is given a name no file has; as music it plays once. A name of the game's own sound table (`game_play`) not there is answered LUMA_E_NOT_FOUND |
| text | none: a missing message id is not replaced | |
| object class | the placeholder object with `nebula:placeholder/model` | the game, after the mods' start |

- The placeholder assets are Nebula's own data: sources under `data/placeholder/` in Nebula, cooked into the
  store with the rest of Nebula's data by the importer. Their absence is a start-up error, so a
  placeholder can never itself be missing.
- A placeholder is drawn, sounded or shown exactly as its kind is; it costs only what the thing it stands for would.
- Each placeholder stands for one reference, and its mod-log entry names that reference. The harness reads
  `placeholder {"mod", "kind", "found", "file", "line"}` facts when one is first used.

## The call

content.model, content.fx, the draw service's textures and the audio service's sounds (`play`, `play_at`, `music`),
given a name they do not find:
- returns the placeholder's handle and `LUMA_OK` (the caller's program goes on, the placeholder shows), and
- reports through `report` the content code of its kind, `file` and `line` the caller's (LumaScript passes
  them; a C mod passes `__FILE__`, `__LINE__`), once per (mod, file, line, code) as `report` already counts repeats.

## Mod log codes 620 to 699

| Code | Row | English | Args |
|---|---|---|---|
| 620 | `Content.MissingClass` | `{mod}: {file} places "{found}", which no mod made; a placeholder stands there.` | file, found |
| 621 | `Content.MissingModel` | `{mod}: there is no model "{found}"; a placeholder is drawn instead.` | found |
| 622 | `Content.MissingTexture` | `{mod}: there is no texture "{found}"; a checker is drawn instead.` | found |
| 623 | `Content.MissingEffect` | `{mod}: there is no effect "{found}"; placeholder sparks are shown instead.` | found |
| 624 | `Content.MissingSound` | `{mod}: there is no sound "{found}"; a chime plays instead.` | found |
| 625 | `Content.MissingText` | `{mod}: there is no text "{found}".` | found |
| 626 | `Content.MissingJoint` | `{mod}: {model} has no joint "{found}"; its root is used.` | model, found |
| 627 | `Content.MissingAnimation` | `{mod}: {model} has no animation "{found}".` | model, found |
| 628 | `Content.MissingProgram` | `{mod}: the material "{found}" has no program for {renderer}; it is drawn plainly.` | found, renderer |
| 640-669 | effects | content/effects.md | |
| 670 | `Content.EventPastEnd` | `{mod}: the event "{found}" of {clip} is at frame {frame}, after its last frame {last}, so it never plays.` | found, clip, frame, last |
| 671 | `Content.MissingParam` | `{mod}: the material "{material}" of {model} has no shader parameter "{found}".` | model, found, material |
| 672 | `Content.MissingFont` | `{mod}: the font {found} is not there or cannot be read; the message font is used instead.` | found |
| 673 | `Content.MissingMovie` | `{mod}: there is no movie "{found}"; nothing is played.` | found |
| 674 | `Movie.NoScene` | `{mod} called {where} while no stage with a movie player was running; it did nothing.` | where |
| 675 | `Movie.NotMovie` | `{mod}: {where} was given "{found}", which is not a cooked movie; nothing is played.` | where, found |
| 676-699 | free for 3.2 to 3.13 | | |

## No mod, no cost

No mod: no reference is read, no placeholder loaded (they are in the store, read only when used),
`luma_refs_check_after_start` is not called.

## Further reading

* [LumaScript reference](index.md)
