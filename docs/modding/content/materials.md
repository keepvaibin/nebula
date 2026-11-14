# Content: materials

## 3.4 Hand-written material shaders

1. Source. `layers/<l>/assets/<name>.hlsl` with `<name>.shader.json`: stage `material`, params [{name, type
   float|float4, default}], textures [{slot 0-7, name}], reads [normal, world_position, view, time]. The HLSL defines
   `void surface(in SurfaceIn i, inout Surface s)`. Surface = {base, normal, roughness, metal, emission, alpha}, the
   record program_pbr.hlsli lights. SurfaceIn = {uv0, uv1, color, normal, world_position, view, time}, where time is
   view_scale.y.
2. A material picks it in glTF: materials[i].extras.nebula.shader = "<name>" (the assets path without its ending).
   Per-material values come from extras.nebula.params {name: value}, else the shader.json default. Textures come from
   extras.nebula.textures {name: glTF texture index}; a missing one is refused by name.
3. Cook. The generator writes the lit form with the mod's surface() where 3.3's stages make the base colour, through
   the programs path; lumalink check compiles it (L804 with the file and line). One float4 place per param, in params
   order, skipping places the material's own colours use. Places are the material's rows: 0-2 the stage registers
   (-4..4), 3-6 the constants (0..1), in 255ths. More params than fit are refused by name. Beside the model the cook
   writes `<model>.params`: an NTBL with columns material (text), param (text) and register (int 0-6).
4. Run time. content.model 1.1 `set_param(mod, model, material, param, rgba)` writes the place on the attached model's
   own copy of the material (Model::ownMaterial), held to the place's range. It lasts until it is set again or a clip
   that moves that colour plays. A material or param the .params lacks, or a model that shows nothing:
   LUMA_E_NOT_FOUND and Content.MissingParam (code 671). An ended handle: LUMA_E_HANDLE. Null arguments or a NaN:
   LUMA_E_ARGUMENT. Fact content.model.param.<h> {material, param, register, rgba}. Capability
   content.model.set_param.

### 3.4 layer 1: the cook

Code: crates/nebula-import/src/cook/surface_shader.rs (reading, checks, places, the check's compile), gltf.rs (the
material), programs.rs (surface_globals, surface_head_lines, surface_tail_lines), model_ir.rs SurfaceIr.

- Where the files are. The glTF's folder and up to the folder holding `luma.json`; the first layer in name order whose
  `assets/<name>.shader.json` exists, with `assets/<name>.hlsl` beside it. `<name>` is parts of letters, digits, `_`
  and `-` joined by `/`. With a shader, `extras.nebula.textures` is the object of item 2 (by the shader's names);
  without one it stays 3.2's array of extra layers.
- What the shader sees. Parameters are globals of their own names (`static float speed; `, `static float4 tint; `) set
  from their places before `surface` runs. A texture `paint` is two functions: `float4 paint(float2 uv)` and `float4
  paint_level(float2 uv, float lod)`. SurfaceIn always has uv0, uv1 (the mesh's TEXCOORD_0 and _1, 0 where the mesh
  has none) and color (COLOR_0). It has normal, world_position, view and time only when `reads` lists them, so reading
  an undeclared one is a compile error at its line. normal and view are in eye space, unit length (view points toward
  the eye); world_position is in game units; time is view_scale.y.
- What Surface starts as. base and alpha: the glTF base colour (factor x COLOR_0 with vertex colours, else the
  factor). normal: the interpolated normal, turned by the glTF normal map if the material has one. roughness and
  metal: the factors times the metal-roughness map. emission: the emissive factor, display-encoded. The glTF base
  colour map and emissive map are not sampled for a shader material; the shader samples what it declares. Colours are
  display-encoded (sRGB) 0 to 1, as the game's colours are.
- The order in the lit form. surface() runs first. Its normal goes into enh_begin, so every light, highlight and
  reflection uses it. Then the light set is summed (pbr_channel_lines), then base and alpha are lit by pbr_finish with
  the shader's metal and roughness and the material's occlusion map. Then the emission is added, then the material's
  alpha test and fog. A replacing pass (shadow, mask) keeps the material's stages.
- Places. In params order, each takes the next place the stages leave unread (a glTF material's stages read constant
  colour 0 with vertex colours, constant colour 1 with a glow). Registers 1 to 3 hold -4 to 4, constant colours 0 to 3
  hold 0 to 1, stored in 255ths. A float writes x and leaves y, z, w at 0. `<model>.params` (NTBL: material string,
  param string, register int) is written in the model's archive beside it, one row for each (material, param).
- Refused, L804 (14 languages), each naming the material or the file:
  - `Cook.Shader.Name`, `.Missing`: the shader's name or files.
  - `.Json` with the key: the JSON is outside the interface.
  - `.Twice`: a name given twice.
  - `.TooLarge`: over 64 KiB.
  - `.Text`: not UTF-8.
  - `.Forbidden` with the line and word: `register`, `cbuffer`, `tbuffer`, `ConstantBuffer`, any Texture, RWTexture,
    sampler or Buffer type, `groupshared`, `globallycoherent`, `#include`, `#pragma`, `#line`, `[[`.
  - `.NoSurface`: no surface().
  - `.ParamType`, `.UnknownParam`, `.NoTexture`, `.UnknownTexture`: the material and the shader disagree.
  - `.TooManyParams`: more parameters than free places.
  - `.ParamRange`: a value outside its place's range.
  - `.Unlit`: the material is KHR_materials_unlit.
- The check compiles (LumaLink sets `surface_shader::CHECK_COMPILER`). It compiles each shader material's plain form
  with dxc: NEBULA_DXC, else `dxc/dxc.exe` beside the given folders (LumaLink's own, the game's). The shader's code
  sits under `#line 1 "assets/<name>.hlsl"`, so dxc's first error in it is `Cook.Shader.Compile {file, line, detail}`.
  An error in the generated part is `.CompileProgram`. A compiler that does not start is `.Compiler`. With no compiler
  found, a Note `.NotCompiled` names each shader. The game's own cook does not compile here; it builds the programs
  itself (mod_shader_compile).
- Proven by compile.
  - cargo nebula-import + luma-validate: 316 pass.
  - surface_shader tests (9): the refusals by name and line; the cube's places, registers and .params; the forms (the
    call before enh_begin, SurfaceIn holds only what it reads, a replacing pass without it).
  - With dxc 1.8 (NEBULA_DXC): the stripes cube's plain form compiles; `sped` for `speed` is refused at line 3;
    `i.normal` without `reads` at line 5.
  - Vulkan and Metal output and the ray-hit form are layers 2 and 3 below; the stripes picture is test 3302 (run on
    Direct3D 12).
  - No check bounds a shader's loops: the cook and the check accept a loop that never ends.

### 3.4 layer 2: every renderer

Code: surface_shader.rs (check_compile, find_glslang, vulkan_text, first_error), vk/shader_compile_vk.cpp
`compile_mod_stage`, vk/mod_shader_compile_vk.cpp, mtl/mod_shader_compile_mtl.cpp, platform/apple/metal.cmake.

- No compiled program is stored in a .luma. A .luma carries the cooked HLSL text (the cooker's v/p<hash>.hlsl and
  flags.tsv). The player's machine builds what it lacks when the mod is converted (mod_shader_build.cpp), with the
  compiler of the renderer it runs: Direct3D 12 dxc beside the game, Vulkan glslang's HLSL front end linked in, Metal
  the same front end's SPIR-V turned to MSL. One .luma serves every renderer. The program-interface hash
  (mod_shaders.h) cannot make a stored binary stale.
- Metal (MSL text, through the vendored SPIRV-Cross). compile_mod_shader_stage on Metal calls `vk::compile_mod_stage`
  (includes inlined, the defines, `program_ps`/`program_vs` as main, the draw bindings, the bound-table form when
  asked: the code Vulkan's compile used, moved so both share it), then `mtl::spirv_to_msl` with the options of the
  game's own programs (programs_msl.cpp: ios, bound_tables). The MSL text is the stored program; pipelines_mtl.mm
  `function_of` compiles it (`main0`) when a draw first needs it. The pixel programs are built in both forms
  (`mod_shader_has_bound_form` true). `mod_shader_compiler_revision` = front end x 256 + kMslRevision.
  galaxy_render_mtl compiles render/vk/shader_compile_vk.cpp and links nebula_shader_compiler (glslang). Before this,
  Metal refused every mod program.
- The check (LumaLink) compiles for every renderer. Direct3D 12: dxc as in layer 1. Vulkan/Metal: glslang, the front
  end the game links. `CHECK_GLSLANG` (None: found by `find_glslang`; Some(None): none), `NEBULA_GLSLANG`, else
  `glslang/glslang.exe` in the folder that holds the check's `dxc` folder. The text gets the game's preamble
  (NEBULA_VULKAN, row-major matrices, `program_ps` as main), in both table forms (NEBULA_BOUND_TABLES,
  NEBULA_TABLE_SLOTS 16), with the game's settings: `-D -V --target-env vulkan1.1 --hlsl-offsets --hlsl-dx-position-w
  -fhlsl_functionality1 --auto-map-locations --spirv-val`. Metal adds no check of its own: its program is
  SPIRV-Cross's translation of the SPIR-V the Vulkan check proves, and Apple's compiler runs only on Apple systems.
- Refusals name the renderer (`{target}`: `Direct3D 12`, `Vulkan/Metal`). `Cook.Shader.Compile` {target, file, line,
  detail}, `.CompileProgram` {target, material, file, detail}, and the note `.NotCompiled` {target, material, file,
  name (dxc, glslang), key (NEBULA_DXC, NEBULA_GLSLANG)} once for each missing compiler. The names are luma-validate's
  own (text.rs ARG_NAMES). glslang's `ERROR: <file>:<line>: <words>` is read as dxc's is. The 14 languages are
  rewritten for these three rows.
- Why both compilers are needed: an HLSL 2021 template (`template<typename T> T twice(T x)`) compiles with dxc and is
  refused by glslang (`'template' : Reserved word`). Layer 1's check passed such a mod, and every Vulkan and Metal
  player failed to build it.
- Metal refuses them).
- Proven:
  - cargo test nebula-import + luma-validate: all pass (nebula-import lib 219/0).
  - The ignored tests with dxc 1.9.2609 (NEBULA_DXC) and glslang 16.6 (NEBULA_GLSLANG):
    a_shader_compiles_or_names_its_line, and a_shader_compiles_for_vulkan_and_metal_or_names_its_line. The stripes
    shader compiles in both table forms; `sped` is refused at line 3; the template is refused for Vulkan/Metal at line
    1, also with no dxc.
  - a_check_without_a_compiler_says_the_shader_was_not_compiled: one note for each renderer.
  - clang -fsyntax-only of shader_compile_vk.cpp, vk/ and mtl/mod_shader_compile_*.cpp and mod_shader_build.cpp, with
    a stand-in TargetConditionals.h for the Metal one.
  - Not run: a Mac build. The Metal path is checked only as C++ here, and no game was run.

### 3.4 layer 3: the ray-hit form

Code: surface_shader.rs (`ray`, the check), programs.rs `ray_hit_form`, hit_record.rs `apply_ray_rows`, model_write.rs
(the record), gltf.rs `ray_hit_table`, model_ir.rs RayRows.

- Rows: what a ray reads of a shader material. By default its hit record (nebula_model.h NebulaModelHit) is what
  the cook reads of the glTF factors the shader starts from (hit_record::pbr_reading), as for any glTF material: class
  by the factors (blend with transmission glass, blend translucent, metallic 0.5 or more metal, an emissive that carries
  the look emissive, roughness 0.4 or less gloss, else dielectric), base, roughness, metal, emission, through. A
  shader.json `ray` object replaces any of them: `class` (dielectric, gloss, metal, glass, cutout, translucent,
  emissive, painted; not ramp or water, which need a ramp map or the pack's water row), `base` and `emission` (three
  numbers, display-encoded; base 0 to 1, emission 0 or more), `roughness`, `metal`, `through` (0 to 1). Applied before
  the record is finished, so a pack's rt_materials.tsv row still decides after. Anything else is `Cook.Shader.Json`
  with the key (`/ray/class`, `/ray/base/2`).
- Outputs: the ray-hit form. For each shader material the cook writes `<model>.rayhit` beside `<model>.params`
  (NTBL: material string, text string): an HLSL text that runs the same `surface()` at a ray's hit, inside
  `namespace RT_MOD_SURFACE_NS` (`rt_mod` unless the includer #defines another), so the tracer can include every
  material's form in one program. The includer gives, before it:
  - `struct RtModHit { float2 uv; float4 color; float3 normal; float3 world_position; float3 view; float time; float lod; }`;
  - `struct RtModSurface { float3 base; float alpha; float3 normal; float roughness; float metal; float3 emission; }`;
  - `float4 rt_mod_place(uint place)`: the material's place on the hit's item (0 to 2 registers -4 to 4, 3 to 6
    constant colours 0 to 1), as content.model set_param leaves it;
  - `float4 rt_mod_map(uint slot, float2 uv, float lod)`: the material's map `slot` (the slot SurfaceIr gives each
    texture).
  It calls `RT_MOD_SURFACE_NS::surface_hit(h, o)`: `o` comes in as the record says (base x the vertex colour where
  flagged, not x a map; display-encoded) and leaves as the shader made it, clamped as the raster clamps it.
- What differs from the raster form. SurfaceIn.uv1 is uv0 (the tracer's vertices carry one coordinate); normal and
  view are world space (eye space in the raster: a shader may rely only on their relation, as `dot(n, view)`);
  a texture function samples at the ray cone's level; `ddx`, `ddy`, `fwidth` and their coarse/fine forms are 0;
  `discard` and `clip` do not end the call but leave alpha 0 (the tracer reads no surface there). Class and through
  stay the record's. A parameter or texture may not be named `h` or start with `rt_`, `Rt` or `RT_`.
- The check compiles the ray-hit form too (cs_6_0, over a stand-in of the tracer's three names,
  surface_shader::RAY_HIT_STAND_IN): an error in the shader's code is `Cook.Shader.Compile` at its line, as for the
  plain form.
- Proven: nebula-import lib 219/0 (5 ignored) on Nebula-LFC34 (tests a_shaders_ray_rows_are_checked_and_replace_its_factors,
  a_shader_material_has_a_ray_hit_form_beside_its_model); with dxc 1.9 (NEBULA_DXC, a_shader_compiles_or_names_its_line):
  the stripes shader's plain and ray-hit forms compile, `sped` is refused at line 3 and an undeclared read at line 5,
  and a pixel's `discard`, `fwidth` and `ddx` compile in the ray-hit form.
- The tracer's side (FA/FB, run time): read `<model>.rayhit` with the model, build the forms into the hit-read
  program (one namespace each), give the three names from the item's material places and maps, and call the form in
  rt_hit_read for an item whose material has one. Until then the rows above are what a ray sees.

## Ray values

Code: surface_shader.rs `ray_rows`, `material_ray`; model_ir.rs RayRows (`over`, `vertex_albedo`); hit_record.rs
`finish_with`, `ray_numbers`, `ray_glow`; model_write.rs (the record); gltf.rs `ray_hit_table`.

- Where: a shader.json `ray` (any material with that shader) and a glTF material's `extras.nebula.ray` (that material
  alone; any glTF material, with or without a shader). The material's own values stand over its shader's, key by key.
  A shader.json `ray` takes the factors of 3.4 layer 3 and the ten keys below; a material's takes the ten alone (its
  factors are glTF's own).
- Keys, all optional; absent, the record is the cook's:

  | key | values | record |
  |---|---|---|
  | `ior` | 1.0 to 3.0 | ior |
  | `thickness` | 0 or more, world units (100 a metre) | thickness |
  | `body` | 0 to 1 | body |
  | `shine` | 0 to 16 | shine |
  | `tint_keep` | 0 to 1 | tint_keep |
  | `traced` | `seen`, `hidden` | traced hidden |
  | `sun_through` | 0 to 1 | through (the sun's share past its far side; not `through`, the transmission) |
  | `metal_kind` | gold, silver, copper, iron, chrome, brass, platinum, `art` | a metal's colour as the cook's metal table gives it (`art`: its colour as it is); a metal class only |
  | `glow` | `{color: [r, g, b] 0 to 1 or "own", candela: 0 or more}` or `{luminance: cd/m2}` | glow (`own`: the material's colour) |
  | `vertex_colour` | `auto`, `albedo`, `light` | see below |

- `vertex_colour`: `auto` (the cook's rule) and `light` read the mesh's colour as painted light, as the game's models
  hold it: de-lit by the gain stream, a share of the painted reflection, the painted-shadow flag (bit 6) where the
  material is unlit. `albedo` reads it as the surface's own colour, as glTF's COLOR_0 means it: environment share 0,
  no gain stream, no bit 6, no paint light; bit 3 stays (the colour multiplies the base).
- Applied after the cook's reading and before a pack's rt_materials.tsv row, which still decides after.
- Refusals: a key outside the list or its range, or a `ray` that is not an object. A shader's: `Cook.Shader.Json`
  {file, key: `/ray/<key>`}. A material's: `Cook.Material.Ray` {material, key:
  `/materials/<n>/extras/nebula/ray/<key>`}. More than 255 materials with a shader in one model (the tracer numbers
  `.rayhit` rows in a byte): `Cook.Shader.Materials` {count, max}. A material name met twice keeps its first row.
- `traced` and `sun_through` are cooked into the reading the record is written from, as the game's own rt_materials
  rows are; the record's writer leaves the through byte 0 and bit 7 to the tracer's writer, which takes them from
  that reading.
- Proven (cargo test nebula-import): a_materials_ray_values_are_checked_and_set_its_hit_record (no `ray`, `{}`,
  `auto` and `light` cook the same bytes; the numbers land in the record; `albedo` clears the share, bit 6 and the
  gain; each refusal's pointer), a_materials_ray_is_over_its_shaders,
  a_shaders_ray_placeholders_are_checked_and_set_its_record, more_than_255_shader_materials_are_refused.
