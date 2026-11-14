# Types across the boundary

## More rules

### 1. What is read, and how

- One reader: libclang, loaded as instrument.py loads it (1.1's rows). It reads every translation unit of the game
  module with that unit's own compile command from the build's compile_commands.json, so the target, the defines and
  the platform's headers are the build's. No regular expressions over source text. gen_classes.py and gen_fields.py are
  retired in 1.10.
- The game's declarations only: a declaration whose file is under `native/petari/`, `native/math/` or `native/include/`
  (what smg_game.dll is built from, less `third_party/` and the system's headers). The same type seen by many units is one
  row, keyed by its USR. Two units that disagree on a type's layout fail the generator, naming both.
- Two passes, no extra parse of the game's units. What headers declare comes from one unit that includes every
  game header, with a game unit's flags. What a .cpp defines at namespace scope (nerve classes, file-level structs,
  globals) comes from instrument.py's parse of that unit, which the build already runs for the rows; it writes
  OUTPUT.types beside OUTPUT.rows, main-file definitions only. An internal-linkage global (`static`, an unnamed
  namespace) has no name a mod can bind and is listed as left out.
- Made at build time, for each platform's build, before the game module compiles. The outputs are build products,
  never committed. Rule 4: a type reaches mods because the generator read it.

### 2. What is recorded

For each record (class, struct, union) that has a definition:
- `name`: the qualified name as the source writes it (`MarioModule::Data`). An anonymous record nested in a named one is
  named by its field (`Owner.mField`). A wholly anonymous one at namespace scope has no row and is listed as left out.
- `kind`, `size`, `align`, `polymorphic` (it has a vtable), `abstract`.
- `bases`: name, offset, virtual or not, in declaration order.
- `fields`: every non-static data member, in declaration order, each with:
  - `name`;
  - `type`: the type text with the source's own aliases kept (`TVec3f`, `f32`, `JGeometry::TVec3<f32>`), canonical
    spelling only where no alias was written;
  - `offset` in bytes;
  - `count`: the product of the array extents (1 for no array), with `extents` when there is more than one;
  - `bits`: width and bit offset for a bit-field, else null;
  - `access`.
  Fields of an anonymous struct or union member appear in the owner as `outer.inner`, as LUMACORE_FIELD does today.
- `file` and `line` of the definition.

A specialization (explicit or partial, `template <> struct Vec3IntArg<int>`) is named by its type's spelling,
and so is any record nested in one (`Vec3IntArg<int>::Type`).

A template: each of its instances that the game uses has a row named by its written spelling
(`JGeometry::TVec3<f32>`): an instance used as a field's type, a base, or a global's type. The template itself has no
size, so it has no row.

For each enum: `name`, `underlying` type, `scoped`, and its `values` as name and value.

For each alias (typedef and `using`) the game declares: `name` and `of`.

For each global: every variable at namespace scope and every static data member that the module defines (not only
declares): `name`, `type`, `file`, `line`, `const`. A `constexpr` or `const` global whose value is known at compile time
also carries `value`. Writing to it changes nothing in the game, so a mod reads it from the self-description and binds
nothing.

Left out, each with its reason (self-description `left_out`): a record with no definition (forward declared only),
a template with no instance, a dependent type, a local class, a field of an anonymous type at namespace scope.

### 3. What the module holds

- Generated C++, compiled into smg_game.dll with `-fno-access-control` as lumacore_fields.cpp is today. Each number is
  the compiler's own `sizeof`, `alignof`, `offsetof` (and a bit-field's position from libclang, checked by the generated
  test below). So the module's table is correct for its own ABI on every platform by construction.
- Where the rows are compiled: header types in one generated unit, which includes every game header. A type a .cpp
  defines gets its row from instrument.py, written into that unit's OUTPUT, because only that unit can name it
  (offsetof in its own unit). Those rows go in a linker section beside the function rows (`lumaty`, as `lumafn`
  is for functions), gathered by the linker with no code run at start.
- Rows sorted by name, for a binary search at bind (sorted once at the first bind, as the function rows are). One table for records (size, align, first field, field count, first
  base, base count), one for fields (name, type text, offset, count, bits), one for globals (name, type text, address).
  Text is in one pool.
- The module writes the table out on request (`--luma-describe <file>`, self-description). The build compares that
  with the generator's JSON from libclang. Any difference, in any number or name, fails the build and names the row.
- Cost with no mod: the tables are read-only data, touched only when a mod binds. Startup and the frame do no
  work for them. Measured as module size, and as 0 at run (rule: no cost when no mod).

### 4. What a mod binds (luma.h)

- `LUMA_IMPORT_FIELD` (kind 1): any record's field, `"Type.field"` or `"Type.outer.inner"`. The signature is its type
  text. Z-4: a different type text refuses the mod, as today (L505 `Name.FieldSig`). A field in a base is found
  through the record's bases, nearest first, and its offset includes the base's.
- `LUMA_IMPORT_SIZE` (kind 2): any record's size.
- `LUMA_IMPORT_CLASS` (kind 3): stays the NameObj class number, for `is_a`. Any polymorphic record's class number is
  1.3's.
- New, `LUMA_IMPORT_GLOBAL` (kind 7): `name` the global's qualified name, `signature` its type text, slot
  `void` (its address). A different type is L505 `Name.GlobalSig`; none is `Name.Global` (two rows asked of G, in 14
  languages). A `const` global is bound read-only: luma.h's macro makes the slot `const void`.
- Enum values and `constexpr` values are not bound. LumaScript and C mods take them from the self-description at
  build time (`luma.h` users from a generated header LumaLink writes per platform, `luma_game_<platform>.h`). They are
  facts of the source, the same in every module of one release.

- Generator, built: every record the module's old field table had is in the new one with the same offsets and sizes
  (a script compares the two at the same commit before the old one goes in 1.10).
- The build's comparison of `--luma-describe` against libclang's JSON passes on Windows, Android and Linux builds.
  A planted difference fails it, named.
- Conformance mods (C, built with build_mod.sh):
  - a field of a non-NameObj struct (a `MarioModule` data struct, a `JGeometry::TVec3<f32>` member), read and written
    in a run with the value seen through a function the game calls;
  - a field reached through a base two levels up;
  - a bit-field read and set;
  - an array field's element `count - 1`;
  - a global read (`MR::` or a manager's static) and written, seen by the game;
  - Z-4: a field with the wrong type text gives L505 Name.FieldSig, and a global with the wrong type gives
    Name.GlobalSig, each said once;
  - the size of a struct that is no NameObj.
- Coverage: the self-description's `left_out` for types is listed with counts by reason in the run's report. G
  checks a sample of 20 left-out rows against the source.

### Generator built as a prototype (not in a tree)

- 2,644 records (2,446 declared, plus 198 template instances the game uses as a field's, base's or global's type),
  13,999 fields, 421 enums, 200 aliases, 249 globals. 130 globals get a row; 119 are declared and defined by no
  object.
- Left out, with reasons: 61 globals of internal linkage, 5 records with no writable name, 1 thread-local global
  (`luma::tBody`: one per thread, no one address; a new rule for section 2).
- The generated table (luma_types_table.cpp) compiles with the game's flags, -fno-access-control and `#pragma clang
  diagnostic ignored "-Winvalid-offsetof"`, in 1.0 s. offsetof on a class that is not standard layout is clang's to
  define; it does for every class without a virtual base, and fields of a class with one have no row. Sections total
  889 KB before the linker merges equal strings.
- Proposed for section 6: `native/include`'s kernel headers (luma.h, luma_hook.h, luma_host.h, luma_types.h) are the
  kernel's interface, not the game. Their types and globals (`luma::Function`, `luma::tBody`) are left out with "the
  kernel's own". The game's files are then native/petari and native/math, plus native/include's game headers
  (nebula_game_api.h and the rest).
- Still to write after review: instrument.py's `.types` for what a .cpp defines; the kernel's kind 7 and the field
  lookup through bases over the new rows; `--luma-describe` and the build's comparison; the type test (base offsets
  and bit-fields checked against the compiler at run).
- They compile with the game's flags. luma.h's side (l12_lumah.py): kind 7 `LUMA_GLOBAL`, kind 1's words for any
  record, `LUMA_BITS_GET`/`LUMA_BITS_SET`. These compile in C11 and C++20 with -Wall -Wextra -Werror, and a run over a
  5/7/20-bit struct reads and writes every value of the middle field without touching the others.

---

- TY-1 taken: `reachable`, one answer for "Type.field".
  - The generator writes `reachable` for every record. It lists every field name a mod may write for the record,
    own and inherited, each with its type text, its offset from the start of the record, and the record that
    declares it. Lookup is C++'s. A name the record declares hides every base's. Otherwise each direct non-virtual
    base is searched the same way, and the name must be found in exactly one base subobject. If it is found in two
    (two bases at any depth, or one base reached twice without `virtual`), it is ambiguous and goes to
    `left_out` as "ambiguous: A::x, B::x". If it is found through a virtual base, it goes to `left_out` as "through
    the virtual base V": its offset is not fixed.
  - Size: the self-description holds the inherited rows only, as (record, name, declaring record, offset). Own fields
    are already in the record. The module's table keeps own fields and non-virtual bases with their offsets, and the
    kernel walks them with the same rule.
  - The build compares, for every record and every name in `reachable` and `left_out`, the kernel's answer (through
    `--luma-describe`) with the generator's. A difference fails the build. The validator reads `reachable` and does
    no walk of its own.
- TY-2 taken: one name per template instance, every other spelling an alias.
  - The row's name is the qualified template name with each argument printed from its canonical type. Builtin
    arithmetic types are written as the game's fixed-width names: f32, f64, s8, u8, s16, u16, s32, u32, s64, u64
    (`long` and `unsigned long` by their width on the platform), with bool and char kept. So it is
    `JGeometry::TVec3<f32>` on every platform. Class and enum arguments use their own row names; pointer, reference
    and const are printed as the type text is.
  - Every typedef or alias of the instance in the game's files is an alias row (`of`), and so is every other
    spelling written in a field's or function's type (`JGeometry::TVec3<float>`). Kind 1 and kind 2 resolve an alias
    to its record, so `TVec3f.x`, `JGeometry::TVec3<float>.x` and `JGeometry::TVec3<f32>.x` are one field.
- TY-3 taken: the exact type text is instrument.py's `type_text`. It is clang_getTypeSpelling with
  "(anonymous namespace)::" removed, the text 1.1's function rows use, from one function in tools/lumacore that both
  generators import. A bit-field is that text, " : " and the width (`u32 : 3`). An array field is its element's
  text, with `count` and `extents` as separate facts. A type the function refuses (unnamed, lambda, dependent) leaves
  the field out as "a type with no name a mod can write".
- TY-4 taken. LUMA_BITS_GET and LUMA_BITS_SET read and write the bytes `position / 8` to
  `(position + width - 1) / 8` one by one, never a wider access. SET is for the game thread. The test puts a
  bit-field in the last byte of an object placed so that it ends at the end of a page whose next page has no access.
- TY-5 taken. native/petari, native/math, native/include's game headers. luma.h, luma_hook.h, luma_host.h,
  luma_harness.h and luma_types.h are left out as "the kernel's own". The `left_out` reasons are a fixed list of
  phrases, and the run report counts by them:
  - "the kernel's own";
  - "internal linkage: no name a mod can bind";
  - "thread-local: one per thread, no one address";
  - "no name a mod can write";
  - "no size (incomplete or dependent)";
  - "ambiguous: {names}";
  - "through the virtual base {name}";
  - "a type with no name a mod can write";
  - "a template not used with these arguments".
- TY-6 taken. Each platform's build writes its own describe output. One merge step (tools/lumacore, mine) writes
  the self-description with one value per fact, plus a per-platform value only where one differs. A LumaScript module
  built for several platforms takes such a value as a build error naming the constant and its values.
- TY-7: yours, as written. The record list for kind 2 and `reachable` for kind 1 are in the self-description
  that 1.2 builds. Kind 7 against `globals`.
- TY-8 taken. Section 5 gains:
  - the validator gives the same L505 as the kernel for each Z-4 conformance mod, on the real self-description;
  - a derived class's field hides its base's field of the same name;
  - an ambiguous name and a name through a virtual base are refused by both and named in `left_out`.
- The per-unit pass (instrument.py), found while answering: a record or global a .cpp makes through a macro
  (NEW_NERVE's nerve classes and their `sInstance`) is spelled in a header but expanded in the .cpp. The pass keeps
  by the expansion location: 60 units give 129 records and 137 globals (it gave 0 by the spelling location). Its
  rows name the .cpp, not the macro's header.

- TY-9: a type text names every record, enum and alias by its row's name.
  - Texts are clang_getFullyQualifiedName's (instrument.py type_text), the same for fields, globals, aliases' `of`,
    enums' `underlying` and the function rows' signatures. Named parts inside an array's element and a function type's
    result and parameters (which clang leaves as written) are put in their qualified form too. Typedef names stay
    typedef names.
  - A typedef from outside the game's trees at file scope (<stdint.h>, <vcruntime.h>: uint32_t, size_t, uintptr_t)
    that any text names is an alias row: `record` null, `file` null, `of` the platform's type.
  - Whatever a field, global or the signature of a header's function or method names is described, reached through
    pointers, references and arrays:
    - a template instance or a record nested in one is a row;
    - a typedef or using of an instance (`NebulaList<AudMe>::Link`) is an alias row, with its spelling alias;
    - an enum of an instance is an enum row. One with no size is forced by a sizeof when its name is public all the
      way; otherwise, and if still no size, it is left out by its full name, "no size (incomplete or dependent)". A
      record declared and defined nowhere (an opaque handle) is left out the same way. What only .cpp files name is
      left out "a template instance only a .cpp names".
  - The build checks it: every type text's outer name (past pointers, references, arrays, cv, keywords and a
    bit-field's width; function types and pointers to members are the checker's to split) is a row, a built-in or left
    out. Otherwise the generator stops, listing every such text.
- TY-10: two facts per record, each checked by the compiler.
  - `trivial_copy`: trivially copyable, so a bit copy is the C++ copy.
  - `trivial_destroy`: trivially destructible, so dropping one frees nothing.
  - The generator works them out from libclang: special members of its own (not defaulted, not deleted), the vtable,
    virtual bases, and every base and field. The module's rows (the table, each unit's rows, an in-class function for
    a record named privately) carry `static_assert(__is_trivially_copyable(T) == claim &&
    __is_trivially_destructible(T) == claim)`, so a wrong claim stops the build, naming the record.
  - On l12: 5989 records, 852 trivially copyable, 4401 trivially destructible. The decompiled source writes its own
    copy constructors for plain data (TVec3, SoundID), which is why the two differ.
- The checker takes a record by value only when both hold.

- TY-10b: a third fact per record, `no_copy`: no copy or move constructor and no copy or move assignment can be called
  (each deleted, declared or implicitly).
  - Why: clang's `__is_trivially_copyable` holds for a record whose every copy and move is deleted (clang does not
    apply CWG1734). The game has 113 such records on l12, among them NebulaListLink<T>, NebulaList<T>, Ui::Layout,
    GameTexture and ModelData. `trivial_copy` stays the compiler's trait exactly, and a reader that takes a record by
    value needs `trivial_copy && trivial_destroy && !no_copy`.
  - The generator works it out from the declared special members (a user-declared copy or move deletes the implicit
    copy) and from every base and field (an assignment also needs no reference and no const field). Special members
    clang declared implicitly, which libclang lists once a unit used them, are taken as clang's answer.
  - Checked by the compiler: the table (built without access control) asserts both ways with `__is_constructible` and
    `__is_assignable` from `const T &` and `T &&`. A unit's rows and an in-class claim, whose traits see public
    members only, assert that a no_copy record has none.
- TY-9b: the build's check reads every name a text holds outside template arguments, not only the outermost: a
  function pointer's result and parameters, a member pointer's class. An instance is one name with its arguments, and
  with a member after it (`A<x>::B<y>::C`). The generator describes what such places name, as it does for fields.
- The fixture is in the tree: tools/lumacore/types_fixture_test.py, with 62 driver checks against the compiler, the
  comparison as built and planted, the three TY-10 facts on ten records, and the name reader.
- A named member of an anonymous type keeps its fields (+29).

### The old table against the rows (before 1.10 retires lumacore_fields.cpp)

  - 4197 bind by one kind-1 name (a row's field or reachable name, through bases and anonymous members);
  - 425 are a field of a named type's field (`LiveActor.mFlag.mIsDead`, mFlag being a LiveActorFlag) and take two
    imports, `LiveActor.mFlag` and `LiveActorFlag.mIsDead`, with the offsets added;
  - none is missing.
- The LumaScript checker reaches the same fields as a view's fields, with no import per step.
- On l12 every no_copy record also has a non-trivial destructor, so the rule refuses none there that was not refused
  already.
