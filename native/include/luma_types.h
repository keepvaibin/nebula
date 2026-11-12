#pragma once
// The type rows (contract types, plan 1.2): every record, field, base and global of the game that a mod may name. Written
// by tools/lumacore/gen_types.py; the numbers are the compiler's own (sizeof, alignof, offsetof, -fno-access-control),
// so they are this platform's. Rows sit in linker sections beside the function rows and are read only when a mod binds:
// no code runs for them at start.
// plan 1.3 (contract objects): a polymorphic record's row holds its type_info, the RTTI data the module with rows is
// built with (typeid of a type is a constant address). Nothing here calls into the C++ runtime, which the module has
// none of on Windows: the kernel compares addresses.
#include <luma_hook.h>
#include <typeinfo>

namespace luma {

    struct TypeField {
        const char* name;   // "mVelocity"; an anonymous member's field "outer.inner"
        const char* type;   // instrument.py's type text ("Vec3f"; a bit-field "u32 : 4"): Z-4 compares it whole
        unsigned offset;    // bytes from the record's start (a bit-field: the byte its bits start in)
        unsigned count;     // array elements, all extents multiplied; 1 for no array
        unsigned short bit; // a bit-field: its first bit within that byte's word, else 0
        unsigned short bits;// a bit-field's width, else 0
    };
    struct TypeBase {
        const char* name;   // the base's qualified name, a row of its own
        unsigned offset;    // where it starts in the record (libclang's, checked by the type test)
        unsigned isVirtual;
    };
    struct TypeRow {
        const char* name;   // qualified, "MarioModule::Data"; a specialization as its type is spelled, "Vec3<f32>"
        unsigned size;
        unsigned align;
        const TypeField* fields;
        unsigned fieldCount;
        const TypeBase* bases;
        unsigned baseCount;
        const void* typeInfo;  // &typeid(the record) when it is polymorphic, else null (contract objects)
        // Contract classes (overrides by slot): the record's table of virtual functions at its start, as the compiler's
        // own layout dump says (gen_types.py, -fdump-vtable-layouts): the entries before the address an object holds
        // (Microsoft's: 1, the RTTI locator; Itanium's: 2, offset to top and the type_info) and all of its entries. 0 and
        // 0: not known (no layout in the dump, a record only a .cpp defines, a virtual base), and no class takes it as
        // its base.
        unsigned vtablePrefix;
        unsigned vtableCount;
    };
    struct AliasRow {
        const char* name;   // a typedef or another spelling of a record ("TVec3f", "JGeometry::TVec3<float>")
        const char* record; // the record's row name ("JGeometry::TVec3<f32>")
    };
    struct GlobalRow {
        const char* name;   // "MR::sDefaultGravity"
        const char* type;   // as the source writes it
        void* address;
        unsigned readOnly;  // const: bound as `const void*`
    };
    // Contract objects: a virtual function a record row declares (not a destructor). `function` is its virtual call:
    // invoke takes args[0] as the object seen as `record` and runs what the object's own class runs. An override has
    // the same name and params (its result may be covariant): overrider finds the one the own class runs.
    // A member function pointer's bytes as this compiler holds one (its ABI's layout: one to three words).
    struct MemberBytes {
        unsigned char bytes[24];
        unsigned size;
    };
    // 64 bytes aligned to 32: a linker's pad between the files' rows is whole rows (null, skipped).
    struct alignas(32) MethodRow {
        const Function* function;  // name and signature as the function rows have them (identity()); hooked null
        const char* record;        // the declaring record's row name
        const char* name;          // its own name: "control", "operator==", "operator bool"
        const char* params;        // "(s32, const Vec3f &) const": the parameters and qualifiers, no result
        // Contract classes, overrides by slot: set for a record the factory makes (NameObj at its start), else null.
        // members: &record::name (its slot, read by the kernel per ABI) and &entry, the code a mod class's table takes in
        // that slot (it hands the call to slotCall). original: its invoke calls what the object's class ran in the slot
        // before the mod's class took it (the base's own), through slotOriginal.
        void (*members)(MemberBytes* pVirtual, MemberBytes* pEntry);
        const Function* original;
    };
    static_assert(sizeof(MethodRow) == 64, "a method row is one 64-byte step");

    // Overrides by slot (LumaClasses.cpp). The entry's call: the mod's hook runs with it, and its proceed runs
    // `pOriginal` (the slot's code before). args and result as an invoke takes them.
    void slotCall(const Function* pOriginal, void** args, void* result);
    template <class R> inline R slotRun(const Function* pOriginal, void** args) {
        Result<R> result;
        slotCall(pOriginal, args, &result);
        return result.take();
    }
    // The code the slot `pOriginal` stands for held in the table of the object's class before the mod's class took it.
    const void* slotOriginal(const void* pObject, const Function* pOriginal);
    // A member function pointer of type P to `code`: a function that takes the object as its `this` with no adjustment,
    // not called through a table (Microsoft's: the address, one word for a class with no base; Itanium's: the address and
    // an adjustment of 0, which on x86-64 and arm64 both say a function that is not virtual).
    template <class P> inline P memberAt(const void* code) {
        static_assert(sizeof(P) == sizeof(void*) || sizeof(P) == 2 * sizeof(void*), "a member pointer of one or two words");
        unsigned char bytes[sizeof(P)] = {};
        __builtin_memcpy(bytes, &code, sizeof(code));
        P p;
        __builtin_memcpy(&p, bytes, sizeof(P));
        return p;
    }
    // A name the generator left out (luma_types.json's left_out), so the kernel refuses it with the validator's words.
    enum : unsigned { kLeftType = 0, kLeftField = 1, kLeftGlobal = 2, kLeftTemplate = 3, kLeftGroups = 4 };
    struct LeftRow {
        const char* name;   // as left_out names it: "Type", "Type.field" (the record's row name), "MR::sX", "TBox"
        unsigned group;     // kLeftType (a record, alias or enum), kLeftField, kLeftGlobal, kLeftTemplate
        const char* why;    // the reason's id, LumaLink's row ("LeftOut.KernelOwn")
        const char* what;   // the names the reason holds ("A::x, B::x", a virtual base), or null
    };
}

#if defined(_WIN32)
#define LUMA_TYPE_SECTION __attribute__((section("lumaty$m"), used))
#define LUMA_ALIAS_SECTION __attribute__((section("lumaal$m"), used))
#define LUMA_GLOBAL_SECTION __attribute__((section("lumagl$m"), used))
#define LUMA_LEFT_SECTION __attribute__((section("lumalo$m"), used))
#define LUMA_METHOD_SECTION __attribute__((section("lumamt$m"), used))
#elif defined(__APPLE__)
#define LUMA_TYPE_SECTION __attribute__((section("__DATA,lumaty"), used))
#define LUMA_ALIAS_SECTION __attribute__((section("__DATA,lumaal"), used))
#define LUMA_GLOBAL_SECTION __attribute__((section("__DATA,lumagl"), used))
#define LUMA_LEFT_SECTION __attribute__((section("__DATA,lumalo"), used))
#define LUMA_METHOD_SECTION __attribute__((section("__DATA,lumamt"), used))
#else
#define LUMA_TYPE_SECTION __attribute__((section("lumaty"), used, retain))
#define LUMA_ALIAS_SECTION __attribute__((section("lumaal"), used, retain))
#define LUMA_GLOBAL_SECTION __attribute__((section("lumagl"), used, retain))
#define LUMA_LEFT_SECTION __attribute__((section("lumalo"), used, retain))
// Contract objects: the method rows (MethodRow), each with its virtual call.
#define LUMA_METHOD_SECTION __attribute__((section("lumamt"), used, retain))
#endif
