#pragma once
// The kernel's lookups over the type and global rows (luma_types.h; contract types, plan 1.2).
#include "luma_types.h"
#include <nebula_game_api.h>
#include <nebula_types.h>

namespace luma {
    // Sorts the rows once. The loader calls it on the game thread before a mod's code runs; afterwards every lookup
    // only reads, from any thread.
    void prepareTypes();

    struct FieldBind {
        bool found;     // the type declares the field, or exactly one base subobject has it with no virtual base on the way
        bool sameType;  // the mod's signature is the field's type text (Z-4)
        s32 value;      // its offset in bytes from the type's start; a bit-field's position in bits
        // TY2-1: why a name some base has is refused (left_out's reason): kNone when found or when no record has it.
        enum : u8 { kNone = 0, kAmbiguous = 1, kThroughVirtualBase = 2 } why;
        // kAmbiguous: "A::x, B::x" (at most 8, sorted); kThroughVirtualBase: "V". Cut to fit, always ends in a 0.
        char whyNames[256];
    };
    // A record by its qualified name ("MarioModule::Data", "Vec3<f32>"); null when the game has none.
    const TypeRow* findType(const char* pName);
    // "Type.field" (kind 1).
    FieldBind findField(const char* pName, const char* pSignature);
    // The same, with the type's name and the field's apart (LumaCore's field()).
    FieldBind findField(const char* pType, const char* pField, const char* pSignature);
    // A global by its qualified name (kind 7); null when the module defines none.
    const GlobalRow* findGlobal(const char* pName);
    // Why the generator left out the name a kind 1, 2 or 7 import gives (contract types TY-7, as the validator answers):
    // kind 1 a field of a record ("<row>.<field>") or the record's own reason, kind 2 the type's or its template's,
    // kind 7 the global's. Null: no reason (no such name).
    const LeftRow* leftOut(u32 kind, const char* pName);
    // The rows as JSON, with the kernel's answer for every name a record's bases give it (nebula_game_luma_describe).
    void describeTypes(NebulaDescribeWrite write, void* user);
}
