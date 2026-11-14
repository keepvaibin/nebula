# Content: models

## More rules

### Models

Materials and shaders: content/materials.md.

### 1. One format on screen

- A mod's model is cooked into the game's own model block, `nebula_model.h` (NMDL, today version 4), the format the
  importer already writes for the disc's models (crates/nebula-import cook/model_write.rs). The game draws it through
  the code that draws its own (ModelData, Model, Material): no second drawing path, no per-frame cost a game model
  does not have, and every renderer (Direct3D 12, Vulkan, Metal) draws it as it draws the game.
- The cook writes the block into the overlay store at the model's store path (section 2); the game finds it as it
  finds a game model. A source the cook could not turn into a block is L804 naming the file and its reason as a phrase
  `Cook.Model.<Kind>` with arguments, in the player's language (section 9): never a half model.

### 2. Where a model lives

| Source | Stands in for | Store path |
|---|---|---|
| `files/ObjectData/<Arc>.arc/<Name>.glb` (or `.gltf`) | whichever of `<Name>.bdl` and `<Name>.bmd` the game's archive has | the game model's path |
| `files/ObjectData/<Arc>.arc/<Name>.bdl.glb` or `.bmd.glb` | that file | the game model's path |
| `assets/<path>.glb` | nothing: a new model of the mod's | `<id>:<path>` as the store names mod paths |

- A plain `<Name>.glb` in an archive that has neither model is `Cook.Model.NoGameModel` naming the archive's
  models (a new model goes in `assets/`); in one that has both, `Cook.Model.TwoGameModels` asking for `.bdl.glb` or
  `.bmd.glb`. `<Name>.kcl.glb` is collision (`.kcl` and `.pa`), `<Name>.canm.glb` a camera: not models.
- A replacing model keeps the archive's other files (animations, materials for other models, collision): only the
  model entry changes. A mod that replaces a whole archive replaces it as any whole file (luma-format 5).
- Clips. Each glTF animation of a model's file is cooked beside it as the game's clip files: joints
  `<animation name>.bck`, texture matrices `.btk`, texture swaps `.btp`, colours `.bpk`. A clip named as one of the
  game's in that archive replaces it; the game's other clips stay. The game's clips move joints by number, so a
  replacing model's joints keep the game's numbers (section 4).

### 3. Space and units

- `UNITS_PER_METRE` = 100: glTF's metre is 100 game units, for models and collision alike (G-M12; the placeholder
  cube of references 4 is 1 m).
- Axes as glTF: +Y up, the model faces +Z, right-handed, as the game's models are. No axis is turned.
- Winding: glTF triangles wind counter-clockwise and the game's clockwise; the cook turns each triangle, so a modder
  exports as glTF says.
- A model's root is its origin; a replacing model stands where the game model stood (its feet at the origin for a
  character).

### 4. Joints

- Numbering by the game model. A model that replaces a game model is cooked against that model's joints
  (read from the store's block, `NebulaModelJoint.name_at`).
  - The game model has one joint (props, most map parts): the source's root is joint 0 whatever its name.
  - Otherwise every joint keeps the game model's number for the joint of its name (animation tracks, the game's code
    and LumaKit name joints by number or name, and both must still find them). A joint the game model has and the
    source lacks is `Cook.Model.MissingJoint` {joint, model}. Extra joints of the source follow, numbered after the
    game's, parents before children; they move with their parents and play no game animation.
  - Where the cook adds a root (a scene of several roots), it takes the game's joint 0 name.
  - A new model numbers its joints depth-first from the skin's root, parents first.
- Rest pose from the glTF nodes: translation; rotation as X, Y, Z angles composed Z·Y·X, as the game composes a
  joint's rotation; scale. `scale_compensate` 0 (glTF scale is not compensated); `kind` 0.
- Billboard from a node's `"extras": {"nebula": {"billboard": "y"}}` (or `"xy"`), the cook's extras
  form; else 0. Another value is `Cook.Model.Billboard` {node, value}.
- Skins. At most 4 weights a vertex (JOINTS_0/WEIGHTS_0), renormalised.
  - A JOINTS_1/WEIGHTS_1 set with any weight over 0.001 is `Cook.Model.WeightsOver4` {mesh, vertices}.
  - A skinned node whose primitive has no JOINTS_0/WEIGHTS_0 is `Cook.Model.SkinNoWeights` {mesh}.
  - Vertices whose weights are all 0.001 or less go on the skin's root with a Note `Cook.Model.Unweighted` {mesh,
    count}.
  - A second skin is `Cook.Model.TwoSkins` {mesh, mesh}.
  - Vertices on one joint with weight 1 are the block's single-joint draw matrices, the rest envelopes and mixes, as
    the game's. Inverse bind matrices from the skin.
- Bounds. Each joint's `radius`, `min`, `max` from the vertices it moves at rest (the game culls by them).

### 5. Meshes

- Triangles (glTF mode 4); strips (5) and fans (6) are made into triangles in glTF's vertex order; points and lines
  (0 to 3) are `Cook.Model.NotTriangles` {mesh, mode}. Indices 16 or 32 bits as needed.
- Attributes:
  - position (always);
  - normal: from the file, else flat normals as glTF 2.0 says a reader makes, with a Note `Cook.Model.FlatNormals`
    {mesh};
  - tangent: with a normal map, TANGENT from the file (w gives the binormal's side), else computed with
    MikkTSpace in the cook;
  - colours: COLOR_0 and COLOR_1 into the block's MESH_COLOR0 and MESH_COLOR1 (vec3 or vec4; float or normalised
    u8/u16), glTF's linear values written in the game's 8-bit colour space as its own vertex colours are; COLOR_2
    and up are `Cook.Model.ColorSets`;
  - coordinate sets up to `MATERIAL_SLOTS` (the native header's, read by limits.rs); more are `Cook.Model.CoordSets`
    {mesh, n, limit}.
- One mesh per glTF primitive, attached to the joint of its node (static) or the skin's root (skinned).

### 6. Levels of detail and sockets

- Levels of detail as the game names and picks them (LodCtrl: `/ObjectData/<Name>Middle`, `/ObjectData/<Name>Low`,
  picked by the actor's distance).
  - A replacing level is another replacing model: `files/ObjectData/<Name>Low.arc/<Name>Low.glb` (and `Middle`),
    by section 2, with no rule of its own.
  - A new model's levels are `assets/<path>Middle.glb` and `assets/<path>Low.glb`, cooked as blocks at
    `<id>:<path>Middle` and `<id>:<path>Low`. The game does not pick among them: an attached new model always
    draws its base, and no rule refuses an `attach` that names a level file. A level whose base model is missing is
    `Cook.Model.LevelAlone` {file, base}. The endings `Middle` and `Low` are matched with their capitals,
  as LodCtrl writes them (so `yellow.glb` is no level of `yel`).
- Sockets. A glTF node named `socket:<name>` with no mesh always becomes an extra joint named `<name>`
  (with `socket:` taken off), parented to its nearest joint ancestor (the root if none), numbered as an extra joint
  (section 4). Two sockets of one name are `Cook.Model.SocketTwice` {name}; a socket named as a joint is
  `Cook.Model.SocketIsJoint` {name}. Effects (3.5) and attached models (section 7) bind to it by name.

### 7. Run time: the service `content.model` 1.0

How the calls reach a mod. The game's side is a catalogued service (services-registry 2, 9), not a
LumaCore member: `content.model` 1.0, no permission, provided by the module, bound by an import row of kind 5,
listed in the self-description and checked by the validator as any service. LumaKit's `C.model` and `C.skin`
 are LumaScript over it: `LumaKit.model.attach(actor, model, follow)` is `content.model.attach`.

```c
/* luma_content_model.h, in the registry's one form (services-registry 9). LumaActor (luma_content.h, shared with
   content.fx): a game object's address as LumaScript holds one (objects.md); the services check that it is a live
   LiveActor (own_class has LiveActor below it), else LUMA_E_ARGUMENT, reported. */
typedef void* LumaActor;
enum {
    LUMA_MODEL_FOLLOW_ACTOR = 0,     /* the actor's base matrix */
    LUMA_MODEL_FOLLOW_SKELETON = 1,  /* each joint takes the actor's same-named joint's matrix every frame */
    LUMA_MODEL_FOLLOW_JOINT = 2      /* the whole model on one joint or socket of the actor, named by `joint` */
};
typedef struct LumaContentModelService {
    LumaServiceTable base;
    /* `model` (a LumaModelName: lifted and checked by the validator) drawn with `actor`, following as `follow` says
       (`joint` for LUMA_MODEL_FOLLOW_JOINT, else null). LUMA_OK with *out, a handle owned by the mod; a missing
       model draws the placeholder (references 4). */
    int32_t (*attach)(const LumaMod* mod, LumaActor actor, LumaModelName model, int32_t follow, const char* joint,
                      LumaHandle* out);
    /* Stops drawing it; the handle ends. */
    int32_t (*detach)(const LumaMod* mod, LumaHandle model);
    /* Shown (1) or hidden (0); the game's own hide calls on the actor hide it too. */
    int32_t (*show)(const LumaMod* mod, LumaHandle model, int32_t on);
    /* The player's parts (below), `count` of them into `parts` (each `size` set by the mod); *total the number the
       game has. */
    int32_t (*player_parts)(const LumaMod* mod, LumaPlayerPart* parts, uint32_t count, uint32_t* total);
} LumaContentModelService;
typedef struct LumaPlayerPart {
    uint32_t size;
    const char* name;   /* "MarioHandR" */
    const char* joint;  /* the body's joint it is drawn at, "HandR0" ("" for the shadow) */
} LumaPlayerPart;
```

- Game thread (services-registry 4); off it, `LUMA_E_THREAD`, reported.
- Follow. With `LUMA_MODEL_FOLLOW_SKELETON` a joint of the model with no same name on the actor follows its own
  parent. `LUMA_MODEL_FOLLOW_JOINT` on a joint the actor lacks is `Content.MissingJoint` and the model follows the
  actor's base.
- Levels. An attached new model is drawn at its base level at every distance; its `Middle` and `Low` files are cooked
  but not drawn.
- Every way a handle ends. `detach`; the actor's end (no dangling draw); a scene change; the mod's end (take-out,
  loader 1.11: its models detach before its library closes). A call on a handle already ended is `LUMA_E_HANDLE`,
  never a crash.
- The player's parts. Mario's hands and face are models of their own in the game (the parts the game swaps: open,
  fist, pointing; the face's expressions). A mod replaces each like any model (section 2), keeping its joint names;
  `player_parts` lists their names and joints so a mod can draw its own over them with `attach`. From the game's code
  (MarioActorHand.cpp, MarioActorDraw.cpp, MarioActorShadow.cpp): `MarioHandL`, `MarioHandR` (and the forms'
  `IceMarioHandL/R`, `InvincibleMarioHandL/R`), drawn at the body's joints `HandL0` / `HandR0`, which the game hides
  on the body when a hand model shows; `MarioFace`, drawn at `Face0` (hidden on the body; its materials copied onto
  the body's face joint, `copyMaterial`); `MarioShadow`, the simple shadow. Each is
  `ObjectData/<Name>.arc/<Name>.bdl`, replaced by `<Name>.glb` at the same place keeping its joint names.

### 8. No cost with no mod

No mod model: nothing of section 7 exists; the cook writes nothing; the game draws its models as before.

### 9. Cook half

`Cook.Model.LevelAlone` (section 6) among the checks, and the cook fact's `levels` (the level files cooked for a new
model).

### 10. Tests

| Test | Must show |
|---|---|
| 3201 | a game model (Coin) attached to the player's right hand joint follows it |
| 3202 | a missing model: the placeholder drawn, one `Content.MissingModel` |
| 3203 | a joint the actor lacks: `Content.MissingJoint`, the model on the actor's base |
| 3204 | wrong arguments refused and reported |
| 3205 | Luigi attached by `"skeleton"` on the player: each joint on the player's same-named joint every frame |
| 3206 | place, show, detach and reuse of a handle |
| 3207 | the player's parts listed with their joints |
| 3208 | a mod's own model (a 1 m cube) attached at the player's base: not the placeholder, its joint 0 100 units up |
| 3209 | the actor's end ends the handle |
| 3210 | the 65th attach of a mod: LUMA_E_LIMIT |
| 3211 | a scene change ends the handle |

No game run shows a replaced game model's clips, a socket or a level.

Facts the tests read, for the harness table (consent 2.6's way), written only while the harness runs:
- `model.attach.<handle>` {"mod", "model", "follow", "joint", "level"} at each attach, and `model.end.<handle>`
  {"why": "detach"|"actor"|"scene"|"mod"} when it ends (3203, 3204, 3206, 3208);
- `model.joints.<handle>` {"frame", "joints": [{"name", "matrix": [12 numbers]}], "actor": [the same, the actor's
  same-named joints]} once a frame while a test asks for it with the step `{"facts": "model.joints"}` (3203 and 3204
  compare the two lists; F adds the step with the keys);
- `cook.model` (section 9), and no `model.*` fact at all with no mod (3206).

### 7 as built (this wins over section 7's code where they differ)

- Header: native/include/luma_content_model.h is the form. `LumaContentModelFollow` is a typed enum, ACTOR 0, JOINT 1,
  SKELETON 2 (not section 7's order). `attach(mod, actor, model, follow, joint, place, out)` takes a `LumaPlace`
  (null: none; ignored for SKELETON) as content.fx does; `show` takes `bool`; handles are `LumaContentModel`.
  LUMA_CONTENT_MODEL_MOST 64 per mod, then LUMA_E_LIMIT.
- Player parts: the hands are drawn at the body's joints `HandL` / `HandR` (MarioActor.cpp 1471 copies those matrices;
  `HandL0` / `HandR0` are the body's own hand meshes the game hides). Ten parts: the six hands, MarioFace and
  LuigiFace at `Face0`, MarioShadow and LuigiShadow at the base.
- Drawing: the game's PartsModel in the actor's arena, PlayerDecoration lists for the player, MapObjDecoration for the
  rest. Hidden with its actor, hidden or clipped. A dead part is used again for the same actor, model, follow and
  joint.
- Facts (harness only), instead of `model.joints`: `model.attach.<h>` {mod, model, follow, joint, placeholder, drawn,
  joints, mapped, reused}; `model.pose.<h>` for the first 120 frames (tunable pose.1) {frame, at, host_at, gap, worst,
  shown}, where `gap` is joint 0 against what it follows and `worst` is the largest distance between a skeleton's
  joints and the actor's same-named joints; `model.end.<h>` {why}.
- Tests as built: 3201 to 3207 and 3209 to 3211 (a Coin on HandR, a missing model, a missing joint, refusals, Luigi
  by skeleton, place/show/detach/reuse, the player's parts, the actor's end, the limit, a scene change); 3208 a mod's
  own model, 1 m over the player's base. No test shows a socket or a level in a game run.

Read against the cook that exists: Sections 1, 3 and 7 to 8 agreed but for the points below.

- G-M1 (section 2, the name). Today a mod's `X.bdl.glb` becomes `X.bdl`, `X.bmd.glb` becomes `X.bmd`, and a plain
  `X.glb` becomes `X.bmd` (modern_files; its test pins `ObjectData/Test.arc/test.gltf` to `test.bmd`). `X.kcl.glb` is
  collision (`X.kcl` and `X.pa`) and `X.canm.glb` a camera.
  - Proposed: a plain `<Name>.glb` (or `.gltf`) replaces whichever of `<Name>.bdl` and `<Name>.bmd` the game's archive
    has.
  - If the archive has neither, it is L804 `Cook.Model.NoGameModel`, naming the archive's models. `files/` stands in
    for game files; a new model goes in `assets/`.
  - If it has both, it is L804 `Cook.Model.TwoGameModels`, asking for `.bdl.glb` or `.bmd.glb`.
  - The explicit forms and `.kcl.glb`/`.canm.glb` stay as they are.
- G-M2 (section 4, joints by name: new cook work). Today joints are numbered by the skin's list in scene order, with a
  `root` joint added when there are several roots. No game model is read. The cook gains the game model, from the
  store's NMDL (`NebulaModelJoint.name_at`), when the source replaces one.
  - When the game model has one joint (props, most map parts), the source's root is joint 0 whatever its name. A
    modder should not have to rename a node to match a name no tool shows.
  - When the cook adds a root, it takes the game's joint 0 name.
- G-M3 (section 4, skins: today silent, must be said).
  - Today a `JOINTS_1`/`WEIGHTS_1` set is ignored silently. Proposed: refused with `Cook.Model.WeightsOver4` {mesh,
    count of vertices} when any of its weights is over 0.001.
  - Today a skinned node whose primitive lacks `JOINTS_0`/`WEIGHTS_0` is drawn unskinned silently. Proposed:
    `Cook.Model.SkinNoWeights` {mesh}.
  - Today a vertex whose weights are all 0.001 or less goes on joint 0 silently. Proposed: a Note
    `Cook.Model.Unweighted` {mesh, count}, the vertices on the skin's root.
  - Today a second skin fails as "a vertex's joint isn't in the skin". Proposed: `Cook.Model.TwoSkins` {mesh, mesh}.
  - Weights are renormalised, as today.
- G-M4 (section 5). Today every mode but 4 is refused. Strips (5) and fans (6) will be made into triangles in glTF's
  vertex order. Points and lines (0 to 3) stay refused, as `Cook.Model.NotTriangles` {mesh, mode}.
- G-M5 (normals). Today a missing NORMAL becomes (0, 1, 0) silently. Proposed: flat normals, as glTF 2.0 says a reader
  must make, with a Note `Cook.Model.FlatNormals` {mesh}.
- G-M6 (tangents). Today the block's tangent and binormal are the normal's number, a stand-in. With a normal map ,
  TANGENT is read from the file (w gives the binormal's side); without one, it is computed with MikkTSpace, the
  generator glTF names, written natively in the cook.
- G-M7 (colours). Today COLOR_0 is not read (`color: [0; 2]`).
  - Proposed: COLOR_0 and COLOR_1 are read into the block's two colour words. That is two sets, not one: the block has
    MESH_COLOR0 and MESH_COLOR1.
  - Accepted types are vec3 or vec4, float or normalised u8/u16.
  - glTF's linear values are written in the game's 8-bit colour space, as the game's own vertex colours are. COLOR_2
    and up are `Cook.Model.ColorSets`.
- G-M8 (coordinates). The limit is `MATERIAL_SLOTS` from the native header (limits.rs reads it), not a written 8.
  `Cook.Model.CoordSets` {mesh, n, limit}.
- G-M9 (billboard). The cook's extras already live under `extras.nebula` (a material's `extras.nebula.textures`). So:
  a node's `"extras": {"nebula": {"billboard": "y"}}` (or `"xy"`), not a `"nebula:billboard"` key. Another value is
  `Cook.Model.Billboard` {node, value}.
- G-M10 (sockets). Today a meshless node that is not in the skin is not a joint, so a socket would be dropped.
  - Proposed: a `socket:<name>` node always becomes an extra joint, parented to its nearest joint ancestor (the root
    if none), with `socket:` taken off.
  - Two sockets with one name are `Cook.Model.SocketTwice` {name}.
  - A socket's name equal to a joint's is `Cook.Model.SocketIsJoint` {name}.
- G-M11. Today each glTF animation is cooked beside the model as the game's clip files (`<animation name>.bck` for
  joints, `.btk` texture matrices, `.btp` texture swaps, `.bpk` colours; to_files and clip_extension). A clip named as
  one of the game's replaces it; the game's other clips stay. Section 2 or 5 should say so.
- G-M12 (section 3, winding and the unit). glTF triangles wind counter-clockwise and the game's clockwise. The cook
  turns each triangle (measured in gltf.rs on Kuribo, Coin and Mario), so a modder exports as glTF says. "The game's
  unit is a centimetre" is a reading; the rule is `UNITS_PER_METRE` = 100, for models and collision alike.
- G-M13 (section 4, rotation). The block's rotation is the quaternion as X, Y, Z angles composed Z·Y·X (gltf.rs
  `angles`), the order the game composes a joint's rotation in.
- G-M14 (section 6). A replacing low model is another file path, `files/ObjectData/<Name>Low.arc/<Name>Low.glb` by
  G-M1, with no rule of its own. `assets/<path>.lod1.glb` is a level, not a model: the cook does not write it as a
  model of its own.
- G-M15 (L804's text is English only). L804's `{text}` is the cooker's English words (anyhow's message), so a player
  reading Japanese sees English. Proposed, for the model cook:
  - Every refusal above is a phrase `Cook.Model.<Kind>` with its arguments, rendered in the 14 languages.
  - L804 gains a `why` phrase argument (mine).
  - The cooker's other refusals (collision, camera, tables, images) move the same way, in the same revision.

### 9. Cook half

- Where it runs. `nebula-import` cook/gltf.rs, one code for both the host's start-check cook (step 4) and `lumalink
  check`/`pack`. LumaLink builds on the same crate, so a mod is refused on the modder's machine, before it ships, with
  the same L804 and phrase a player would see.
- Reading. glTF 2.0 only: `.glb` (JSON chunk plus one binary chunk) or `.gltf` with its files.
  - Buffers: embedded `data:` base64 or files beside it. A path with `..`, a leading `/` or a `:` is refused
    (`Cook.Model.Leaves` {uri}).
  - Accessors: every component type, normalised integers as glTF reads them, strides, sparse values over a view or
    over zeros.
  - The scene is `scene` or scene 0.
  - Required extensions read: KHR_texture_transform, KHR_animation_pointer and KHR_mesh_quantization. Any other is
    `Cook.Model.Extension` {name, read}.
- Limits. Each is refused naming the limit, never cut:
  - an accessor of at most 2^22 elements;
  - joints and meshes below NEBULA_MODEL_NONE;
  - at most 240 images or coordinate sets a material;
  - images of at most 16384 px a side;
  - coordinate sets up to MATERIAL_SLOTS.
- Checks and their phrases. All are L804 with `why`:
  - from section 4: MissingJoint, WeightsOver4, SkinNoWeights, TwoSkins, Unweighted (a Note);
  - from section 5: NotTriangles, FlatNormals (a Note), ColorSets, CoordSets;
  - from section 6: Billboard, SocketTwice, SocketIsJoint;
  - from G-M1: NoGameModel, TwoGameModels;
  - Leaves, Extension, the limits above, and the reading errors: a cut-off `.glb`, a view past its buffer, an accessor
    past its view, a triangle naming a vertex past the mesh, a node the scene lacks, a skin naming a node outside the
    scene, no triangles at all.
  - Each names the file, and the mesh, node or material by its glTF name, else by its number.
- The block written. `model_write::write` of the ModelIr, NMDL version 4:
  - joints as section 4;
  - one mesh per primitive and one material per mesh (`same_as` the first mesh of its glTF material);
  - single-joint vertices as joint draw matrices and blended ones as envelopes (deduplicated by joints and weight
    bits);
  - positions in a single joint's space when skinned to one joint;
  - bounds per mesh and per joint.
- Materials.
  - base colour factor and texture, then `extras.nebula.textures` multiply layers;
  - `alphaMode` BLEND and MASK with `alphaCutoff`;
  - `doubleSided`;
  - KHR_texture_transform with even scales (unequal is `Cook.Model.UnevenTransform` {material}), animated by
    KHR_animation_pointer.
- Textures. PNG or JPEG, decoded natively. Each is written once per image as RGBA8 with generated levels from 64 px a
  side, and laid out by `fitting_layout` (grey images as L8, grey with alpha as LA8, the rest RGBA8).
- Facts the tests read. For each cooked model, the cook writes a mod-log COOK fact `cook.model` {file, store path,
  replaced (the game model's path or empty), joints, extra joints, meshes, vertices, materials, textures, clips,
  notes}. 3201 and 3202 read it, and 3206 reads that there is none.
- Tests (in nebula-import).
  - One per phrase above, each with a hand-made glTF: the phrase, its arguments, and that no block is written.
  - Strips and fans against the same mesh written as triangles.
  - Flat normals.
  - COLOR_0 against a known colour word.
  - A socket.
  - A clip replacing a game clip by name.

### 2 and 6 as built

- A new model is an object archive of its own. A model outside an archive (a mod's `assets/<path>.glb`, the store's
  `Luma/<id>/<path>.glb` by luma-format 5) is cooked as `Luma/<id>/<path>.arc/<leaf>.bmd` (`.bdl` when named
  `.bdl.glb`). `<leaf>` is the last segment of `<path>`, and the model's clips go inside the archive. This is the
  layout of every game object archive (`ObjectData/Coin.arc/Coin.bdl`), the only form the game's loader opens. Levels:
  `<path>Middle.glb` gives `Luma/<id>/<path>Middle.arc/<leaf>Middle.bmd`, and Low likewise.
- For a name `<id>:<path>`, the game's archive lookup (FileUtil) reads the archive `/Luma/<id>/<path>` and the model
  `<leaf>` inside it, so content.model draws a mod's own model by that name (test 3208).
- LevelAlone is `Cook.Model.LevelAlone {file, model}`, where `model` names the base file that is missing
  (`<path>.glb`). `base` is not a validator argument name. A level is found by the endings `Middle`/`Low` with their
  capitals, and its base by the store's case-blind compare among the mod's own files.
- 3208's model: Nebula `crates/nebula-import/data/metre.gltf` (`make_metre.py`). It is one node `metre` at [0, 1, 0]
  carrying a 1 m cube that hangs to the origin. The test `a_gltf_metre_is_a_hundred_units` shows joint 0 at [0, 100,
  0], the cube's vertices from -100 to 0 in the joint's space, and the joint's bounds from 0 to 100 in the model's
  space. Attached at a base, model.pose's gap of joint 0 is 100.

### Collision

A `<Name>.kcl.glb` is solid ground, not something to see. Each of its materials is one row of the attributes table
(`.pa`); the material's `extras` are the row's columns: `Floor_code`, `Wall_code`, `Sound_code`, `Camera_id`,
`Camera_through`. In an archive folder it cooks beside the model. In a mod's `assets/` it cooks into `<Name>.arc`,
the archive the model of the same name goes to, where an actor of that model reads it.

### Store version

Cooked files carry their format's version. A Nebula with another store version imports the game again and cooks the
mods again.
