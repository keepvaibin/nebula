#pragma once
// The type rows (contract types, plan 1.2): every record, field, base and global of the game that a mod may name. Written
// by tools/lumacore/gen_types.py; the numbers are the compiler's own (sizeof, alignof, offsetof, -fno-access-control),
// so they are this platform's. Rows sit in linker sections beside the function rows and are read only when a mod binds:
// no code runs for them at start.
// plan 1.3 (contract objects): a polymorphic record's row holds its type_info, the RTTI data the module with rows is
// built with (typeid of a type is a constant address). Nothing here calls into the C++ runtime, which the module has
// none of on Windows: the kernel compares addresses.
#include <typeinfo>

namespace luma {
    struct Function;  // luma_hook.h

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
    struct MethodRow {
        const Function* function;  // name and signature as the function rows have them (identity()); hooked null
        const char* record;        // the declaring record's row name
        const char* name;          // its own name: "control", "operator==", "operator bool"
        const char* params;        // "(s32, const Vec3f &) const": the parameters and qualifiers, no result
    };
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
