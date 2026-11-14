# References to game objects

## More rules

### 1. What a reference is, and the one rule

A reference is a name in a mod that must find something: in the game's files, in the mod's own files, in another
mod it `needs`, or in something code makes at start (a derived class).

The rule. A reference that does not resolve never stops the game: the mod stays on, the thing it named is replaced by
a placeholder of its kind where section 4 has one, and the mod log has one Error entry naming the mod, the file, the
line (or JSON pointer), the kind and the name, and what was expected. Sounds, texts and object classes have no
placeholder (section 4).

### 2. The kinds, and who resolves each

| Kind | Where it is written | Resolves against | Checked by |
|---|---|---|---|
| texture of a model | a glTF's images (embedded or a URI inside the mod) | the mod's files | validator, L600-L619 block (3.2) |
| texture of a material, program of a material | a material source (3.3), a shader name (3.4) | the mod's files, the game's programs, the mod's shaders | validator, L620-L649 |
| texture of a particle; an effect's particles | the effects source (3.5) | the mod's files, the game's particle textures by name | validator, L650-L669 |
| joint | an effect or socket bound to a joint (3.2, 3.5); a character bound by joint names | the model it is bound to (the game's or the mod's) | validator, L600-L619 and L650-L669 |
| animation | an actor's animation by name (3.6) | the model's archive | validator, L670-L679 |
| sound | a sound by name (3.7) | the game's sound table, the mod's sounds | validator, L680-L689 |
| text | a message id (3.8; a placement's message, a layout's text) | the game's message tables in each language, the mod's added lines | validator, L690-L699 |
| object class | a placement row's class name (3.9) | the game's factory (self-description) and classes mods make (1.4) | validator, L900-L929 (stages' half), with section 3's after-start step |
| file | a whole file or patch target (luma-format 5) | the game's store | validator, L302 (exists) |
| a name code passes at run time | `assets`, `draw`, `audio`, LumaKit calls taking a name | as its kind | the call itself (section 5), and the validator for names the compiler lifts (section 5.2) |

This contract asks one thing of every such rule: its finding carries `kind`, `found` (the name) and `expected` (where
it was looked for) as parameters, so the game's placeholder step (section 4) can read from the kept report which names
to replace, without a second check.

### 3. When the game resolves

- At import and at each start: the validator's start check runs every reference rule with the store, the
  self-description and the installed mods. Its findings become mod-log entries (source VALIDATOR, at the title's
  summary). A start's check reads the kept report for the rules that read the whole `.luma`; reference rules that read
  the store or other mods run again at every start, so a game update or another mod's removal that breaks a reference
  is found then.
- Object classes are not checked. A placement row naming a class no mod makes is left out of the stage, with no
  mod-log entry and no placeholder: the validator writes no `after_start` rows, so the host's after-start check (the
  module's `luma_refs_check_after_start`, luma_refs.h) is given none and `Content.MissingClass` (620) is never said.
- Nothing is resolved lazily for content: by the title every content reference of every mod on is either resolved or
  has its placeholder decided. A stage loaded later reads the decision, it does not look again.

### 4. Placeholders

The placeholder model is the object archive `NebulaPlaceholder` (the game's `ObjectData/NebulaPlaceholder.arc` as
the store has it), cooked from `data/placeholder/model.gltf` with 3.2's model path; the placeholder object loads it by
that name.

| Kind | Placeholder | Made by |
|---|---|---|
| model (new: an object the game does not have) | `nebula:placeholder/model`: a 100-unit cube, a magenta and black checker on each face with a white "?" | the game, when content.model is given a name no store has (621); a mod file the cook cannot read is refused (L804), not replaced |
| model (replacing one of the game's) | the game's own model | the cooker keeps the game's file and leaves the mod's out (the mod's other files still apply) |
| texture | `nebula:placeholder/texture`: 64 x 64, magenta and black 8-pixel checker, opaque | the draw service, for a texture name it does not find (622); a glTF naming a missing image is refused by the cook (`Cook.Material.MissingTexture`, L804) |
| material program | the default lit program (3.3) with the material's base colour; never the missing pass | the game, when the program is absent at load (3.13 refuses the start if the compiler itself is absent) |
| particle effect | `nebula:placeholder/effect`: a slow ring of 8 white and magenta sparks, 50 units across, at the binding point | the game, when an effect name does not resolve (3.5) |
| particle texture | the texture placeholder | the cooker |
| joint | the model's root joint | the game, at binding |
| animation | the model's bind pose (a mod's new actor) or the game's own animation of that name (a replacement) | the game, at the animation's start |
| sound | none: the audio service answers a name it does not find with LUMA_E_NOT_FOUND and plays nothing | |
| text | none: a missing message id is not replaced | |
| object class | none: the placement is left out (section 3) | |

- The placeholder assets are Nebula's own data: sources under `data/placeholder/` in Nebula, cooked into the
  store with the rest of Nebula's data by the importer. Their absence is a start-up error (3.13), so a
  placeholder can never itself be missing.
- A placeholder is drawn, sounded or shown exactly as its kind is; it costs only what the thing it stands for would.
- Each placeholder stands for one reference, and its mod-log entry names that reference. The harness reads
  `placeholder {"mod", "kind", "found", "file", "line"}` facts when one is first used.

### 5.1 The call

content.model, content.fx and the draw service's textures, given a name they do not find (the audio service instead
answers LUMA_E_NOT_FOUND):
- returns the placeholder's handle and `LUMA_OK` (the caller's program goes on, the placeholder shows), and
- reports through `report` the content code of its kind (section 6), `file` and `line` the caller's (LumaScript passes
  them; a C mod passes `__FILE__`, `__LINE__`), once per (mod, file, line, code) as `report` already counts repeats.

### 6. Codes (report codes 620-699; rows in 14 languages)

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
| 640-669 | effects | contract effects (3.5) | |
| 670 | `Content.EventPastEnd` | `{mod}: the event "{found}" of {clip} is at frame {frame}, after its last frame {last}; it never comes.` | found, clip, frame, last |
| 671 | `Content.MissingParam` | `{mod}: the material "{material}" of {model} has no shader parameter "{found}".` | model, found, material |
| 672 | `Content.MissingFont` | `{mod}: the font {found} is not there or cannot be read; the message font is used instead.` | found |
| 673 | `Content.MissingMovie` | `{mod}: there is no movie "{found}"; nothing is played.` | found |
| 674 | `Movie.NoScene` | `{mod} called {where} while no stage with a movie player was running; it did nothing.` | where |
| 675 | `Movie.NotMovie` | `{mod}: {where} was given "{found}", which is not a cooked movie; nothing is played.` | where, found |
| 676-699 | free for 3.2 to 3.13 | | |

### 7. No mod, no cost

No mod: no reference is read, no placeholder loaded (they are in the store, read only when used),
`luma_refs_check_after_start` is not called.

### 8. Tests

| Test | Mod | Must show |
|---|---|---|
| 3131 | a new object whose glTF names a missing image | at import and start the validator's L6xx finding; in play the object with the checker; one `placeholder` fact; one mod-log Error, the same text as LumaLink `check` |
| 3132 | a placement of a class no mod makes | `Content.MissingClass` after start; the placeholder cube at the place; the stage plays on |
| 3133 | an effect bound to a joint the model lacks | L65x at start; the effect at the root joint in play |
| 3134 | a script that starts the effect "GoldAuraa" (a typo, literal) | listed at import from the lifted names (5.2), before play; in play the placeholder sparks; one report counted on every call |
| 3135 | a script that starts an effect by a name built at run time that does not exist | no finding at import; in play the placeholder and one `Content.MissingEffect` with the script's file and line |
| 3136 | a message id missing | `[mod:id]` drawn in each of two languages |
| 3137 | the placeholder data removed from the store | the start stops with 3.13's message naming `nebula:placeholder/*` |
| 3138 | no mod | no `placeholder` fact; `luma_refs_check_after_start` not called (no `refs.after_start` fact) |

Pictures: 3131, 3132, 3133 and 3136 on Direct3D 12 and Vulkan, English and one other language.
