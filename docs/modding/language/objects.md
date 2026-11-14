# Making and owning game objects

## More rules

### 0. Ground truth, proven before this draft

- smg_game.dll imports nothing (llvm-readobj --coff-imports on l12: no DLL). The module has no C runtime
  (native/math/src/nebula_runtime_win.c). On Windows a `typeid(expr)` or `dynamic_cast` compiles to calls into
  vcruntime (__RTtypeid, __RTDynamicCast, __RTCastToVoid), so neither may appear in the module.
- The game builds with -fno-rtti (native/CMakeLists.txt:197) and no game source uses typeid or dynamic_cast.
- RTTI *data* alone (`&typeid(T)` of a type, and the vtables' RTTI slot) needs from outside the unit only the
  type_info vtables: `??_7type_info@@6B@` on x86_64-pc-windows-msvc. On aarch64-linux-android, x86_64-linux-gnu and
  arm64-apple-macos it needs `__cxxabiv1::__class_type_info` and `__vmi_class_type_info` vtables (scratch rtti.cpp), and
  `__si_class_type_info` for a class with one base (scratch si.cpp). Each compiled and listed with llvm-nm.
- On MSVC x64 with RTTI, vftable[-1] is the complete object locator (COL): `offset` gives the complete object, and
  `pTypeDescriptor` (an RVA; the image base is the COL's address less its `pSelf`) is `&typeid(own class)`. Proven on
  Windows (scratch col.cpp): from a primary and from a secondary base pointer of D : C : A, B, and of C, the own
  class and the complete object are right in all four cases.
- On l12: 4688 polymorphic record rows (82 roots, 11 with a second polymorphic base at a non-zero offset, no virtual
  base anywhere); 570 records left out (523 with no name a mod can write).

### 1. What a mod gets (LumaCore, luma.h)

- `class_of(name)`: unchanged. A record row's number (its index in the types file's record order), or -1.
- `int32_t own_class(const void* object)`: the number of the object's own class (the most derived one). `object` is
  the address of a game object of a polymorphic class, or of any polymorphic subobject of one (a pointer to a
  secondary base works). -1: object is null. -2: the own class has no row (a class with no name a mod can write);
  the kernel's report says which, once.
- `const void* as_class(const void* object, int32_t class_number)`: the address of the object's `class_number`
  subobject when its own class is that class or derives from it, else null. Bases are walked through the types
  rows, from the complete object. A class reached twice is null, with a report (C++'s ambiguous base).
- `is_a(object, class_number)`: stays, as `as_class(object, class_number) != null`. The old answer, which covered
  NameObj only, retires (section 5).
- `const LumaFunction* method(const char* name, const char* signature)`: the virtual call of a virtual function
  that a record row declares, defined or not ("LiveActor::control", "void()"; "LiveActor::getBaseMtx",
  "MtxPtr() const"). Its `invoke(args, result)` takes args[0] as the object seen as that class (`as_class` gives
  it), and the compiler's own dispatch runs what the object's own class runs. That function's row runs its hooks.
  A virtual call is not itself hooked (`hooked` null). Hooking every override is K.vhook (DECISIONS 3), later.
- `const LumaFunction* overrider(const void* object, const char* name, const char* signature)`: the function row
  the object's own class runs for that virtual. Null when that function has no row (inline in a header, implicit),
  or when object or name is not known.
- Import kind 8, `LUMA_IMPORT_METHOD`: `name` and `signature` as `method`; slot `const LumaFunction`. Bound
  at load, whole or not at all, like kind 0.
- Name and signature follow C-1's identity() rule. A virtual function's name is the class that
  declares it plus its own name.

### 2. How the module knows the own class

- RTTI data, never RTTI code. The module with rows is compiled with -frtti. The module without rows (1.7) keeps
  -fno-rtti, so a player with no mods pays nothing; until 1.7 there is one module. No typeid expression on an
  object and no dynamic_cast, anywhere.
- The type_info vtables, data that nothing calls:
  - MSVC ABI: the module links no system library (-nostdlib, /NODEFAULTLIB: native/CMakeLists.txt:212), so
    `??_7type_info@@6B@` is defined in nebula_runtime_win.c;
  - Itanium: the module links the C++ library on Apple, Linux and Android (native/CMakeLists.txt:218-230), and that
    library defines the three `__cxxabiv1` vtables. The build's link is the proof, on each platform.
- The own class is read from the vtable, in two ABI readers in the kernel, with nothing else ABI-specific:
  - MSVC x64: vptr[-1] is the COL; the complete object is object - COL.offset; the type is
    image base + COL.pTypeDescriptor, where image base = COL - COL.pSelf.
  - Itanium (Android, Linux, macOS, iOS): vptr[-2] is the offset to top; vptr[-1] is the type_info.
- The rows: each polymorphic record row carries `&typeid(T)`. For a header's records it is in
  luma_types_table.cpp; for a .cpp's records it is in the unit's LumaUnitRows. At the first ask the kernel sorts the
  addresses and binary-searches them. A type_info is one object in the module: COMDAT on COFF, weak and merged on
  ELF and Mach-O. Addresses are only compared within the module.
- Uniqueness test: the build checks that no two rows share a type_info address.

### 3. Virtual calls

- For each virtual function a record row declares, the generator writes a thunk with LumaFunction.invoke's form:
  `static_cast<T*>(args[0])->f(luma::arg<P>(args[1])...)`. The call is unqualified, so the compiler dispatches.
  - A header's declarations go in luma_types_table.cpp, and a .cpp's in its unit rows. Both are compiled with
    -fno-access-control, so private and protected virtuals are callable too.
  - Its name and signature come from identity(). The thunks are one table, sorted by name then signature.
- overrider: the kernel takes the own class D, then searches D and its bases in the order the types rows give
  them, depth first. The first record that declares a function overriding `name` (directly or through the chain the
  generator records) gives the answer; its row, if it has one, is returned.
- What the generator records: per record, its virtual declarations, each with what it overrides directly. This
  is clang_getOverriddenCursors in the header pass, plus C-1's facts for the .cpp's records.

- Fixture: single and multiple inheritance; a secondary base pointer; a class in an unnamed namespace
  (-2); a template instance; a private virtual; a pure virtual with no body.
  - The real generator writes the rows. A driver built with the C++ runtime compares own_class, as_class, method and
    overrider against the compiler's typeid and dynamic_cast on every case. The driver may use the C++ runtime; the
    module never does.
  - It runs on Windows now. Android and Linux run it in their builds, and Apple when a machine is there.
- Game, joint run:
  - a mod reads own_class of the player's actor through a LiveActor pointer, expecting MarioActor's number;
  - as_class to NameObj gives the same address;
  - method("LiveActor::getBaseMtx", "MtxPtr() const") equals what MarioActor's own row returns;
  - overrider names MarioActor's row;
  - an object whose class has no row gives -2 with its report.
  The exact functions are checked against the types and functions files before the tests are written.
- Build checks: the type_info uniqueness above, the thunk count against the declarations read, and the
  module's imports still empty on Windows.

### 5. Retired by this (in 1.10)

- NameObj::mLumaClass, and LumaCore.cpp's setClass/classOf, which learn vtables from the factory: the own class
  needs no game field and no constructor.
- lumacore::kClassRows: the old class table. Class numbers are the types file's record order.

### 7. Found while building the prototype (uncommitted)

- Fixture as in section 4, own_class and as_class only (method and overrider come next): 78 checks, 0 bad, against
  typeid and dynamic_cast. Two ABIs:
  - Windows: the l12 game unit's flags with -frtti.
  - Itanium: the Android x86_64 module's flags. The driver is linked static and run on a Linux
    kernel: `wsl.exe -d docker-desktop -e`, which runs a static bionic binary. types_fixture_test.py gained
    `--run-with` and `--path-map` for this, and `--libclang` (the NDK has none beside its clang).
- The Android run found a 1.2 fault, now fixed in gen_types.py: bionic's `uintptr_t` is `__uintptr_t`, so TY-9
  stopped the generator. A foreign alias's own text is now named in turn until nothing is new (Darwin's size_t,
  `__darwin_size_t`, is the same case).
- -frtti makes MSVC-ABI clang emit the vftable of a base class the game never makes alone. Three virtuals had a
  declaration and no body, and the module stopped linking:
  - `AreaForm::init` and `isInVolume`: now pure. Every form defines both, and no AreaForm of its own is made.
  - `SimpleBreakableObjBase::control`: the declaration is removed. It inherits MapObjActor's, and only
    SimpleBreakableObj, which has one, is made.
  These change the functions file: the rows' abstract and pure flags, and one row fewer. It is the game as it is: a
  function with no body cannot be called.
- On Itanium a class's type_info is emitted with its key function. A polymorphic row whose key function is defined
  nowhere would leave `&typeid` in the table undefined. The Android module build answers whether any exist (pending).
- The kernel now stops (NEBULA_PANIC) when a base names no row, in as_class and in the field lookup too. luma_types.h
  says a base is a row of its own. Before, the field lookup skipped such a base and answered "no field".
- A class with no row gives own_class -2, and as_class null even when it derives from a class with a row. The kernel
  walks rows, and a class with no row has none to walk. Section 1 says the same; it is repeated here because the
  fixture case (a class local to the driver, deriving FoD) is what proves it.

### 7a. Virtual calls, prototype

- The generator (gen_types.py `virtual()`) writes, for every virtual function a record row declares (not a destructor,
  not deleted), a thunk with LumaFunction.invoke's form and a `luma::Function` row {name, signature, invoke, hooked =
  null} in a new section, lumamt (luma_types.h LUMA_METHOD_SECTION). It covers the header pass (the table), a unit's
  records (LumaUnitRows members) and a record named through a scope that is not public (the record's own static
  members).
  - Name and signature are instrument.py's identity(); an instance's method is the record's row name plus
    identity_signature(), as C-1 names an instance's override.
  - The thunk: `static_cast<[const] T*>(args[0])->f(luma::arg<P>(args[i])...)`, unqualified, into Result<R>::put,
    answering kAskKind as a function row does. A private virtual is callable (the table has no access check; a unit's
    rows are friends).
  - luma_types.json: each record's "virtuals": name, signature, pure, row; one with a name or type no mod can write is
    there with "row": false (no thunk).
- The kernel sorts the method rows by name and then signature (Index<Function>; one name, several signatures are
  overloads). Two rows with one name and signature stop the game. `findMethod(name, signature)`.
- "polymorphic" was set by a virtual *method* only: a record whose only virtual is its destructor or a conversion had
  no type_info row, so own_class could not name it. Now any virtual sets it. The table asserts the claim with
  `__is_polymorphic` for every record, so a wrong flag fails the build.
- Fixture, both ABIs: 86 checks, 0 bad. New cases: FoA::fa, FoB::fb through a secondary base, FoA::add with a `const
  s32 &` parameter (dispatching to FoC's), FoD::secret (private), kAskKind gives kPlain, hooked null, no row for
  another signature, an unknown name or a destructor; FoV (polymorphic by its destructor alone) has its type_info.
- Android: LumaLoader.cpp's apiAlloc took u64 where luma.h says uint64_t. They are different types on LP64 Android and
  Linux, and the module did not compile there. Fixed in the same branch (a 1.1 fault).
- Cost, Windows l14 module before thunks: 26,673,664 bytes against l12's 25,345,536 (+1.33 MB, RTTI data). Only the
  module with rows; 1.7's module without rows keeps -fno-rtti.
- Still to do in 1.3: overrider; the LumaCore calls and kind 8; describe and compare_types counting the method rows
  against the JSON's (section 4's thunk count); the game tests.
- O-3, sharpened: the types file lists records in the generator's walk order, and the kernel sorts its rows by name
  (byte order, strcmp). "Index in the types file" would need a reader to know the walk. Proposal: a class number is
  the record's index among the record rows sorted by name in byte order, the kernel's own sTypes order. The validator
  and LumaLink get it by sorting the JSON's record names the same way (records only: not aliases or enums). It is
  stable for one build and binds by name at load (kind 3), as before.
