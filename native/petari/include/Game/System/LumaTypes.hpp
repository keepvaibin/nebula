#pragma once
// The kernel's lookups over the type and global rows (luma_types.h; docs/modding/language/types.md).
#include "luma_types.h"
#include <luma_hook.h>
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
    // Why the generator left out the name a kind 1, 2 or 7 import gives (types.md, as the validator answers):
    // kind 1 a field of a record ("<row>.<field>") or the record's own reason, kind 2 the type's or its template's,
    // kind 7 the global's. Null: no reason (no such name).
    const LeftRow* leftOut(u32 kind, const char* pName);
    // The rows as JSON, with the kernel's answer for every name a record's bases give it (nebula_game_luma_describe).
    void describeTypes(NebulaDescribeWrite write, void* user);

    // Objects (docs/modding/language/objects.md): an object's own class, read from its vtable's RTTI data. pObject is a
    // game object of a polymorphic class or any polymorphic subobject of one.
    struct OwnClass {
        const TypeRow* type;   // the own class's row; null when it has none (`typeInfo` still says which class it is)
        const void* complete;  // the whole object: pObject less its subobject's offset
        const void* typeInfo;  // the own class's type_info (MSVC: its type descriptor)
    };
    OwnClass ownClass(const void* pObject);
    // A class's number (objects.md section 10): its record's index among the record rows sorted by name in byte
    // order, the order these lookups keep. -1 for null. LumaCore's class_of, own_class, as_class and kind 3 give it.
    s32 classNumber(const TypeRow* pType);
    // The record of a class number; null when there is none.
    const TypeRow* classAt(s32 number);
    // The object's pTarget subobject when its own class is pTarget or derives from it; null otherwise, and null with
    // *pAmbiguous set when pTarget is reached by two paths (C++'s ambiguous base).
    const void* asClass(const void* pObject, const TypeRow* pTarget, bool* pAmbiguous);
    // The same from an own class already read (one read of the vtable for a call that also reports what it found).
    const void* asClass(const OwnClass& rOwn, const TypeRow* pTarget, bool* pAmbiguous);
    // The name a type_info carries, as the ABI writes it (MSVC decorated ".?AVLiveActor@@", Itanium mangled
    // "9LiveActor"): for a report naming a class with no row. "" for null.
    const char* typeInfoName(const void* pTypeInfo);
    // The virtual call of a virtual function a record row declares ("LiveActor::control", "void()"): its invoke takes
    // args[0] as the object seen as the declaring class and runs what the object's own class runs. Null: no such row.
    const Function* findMethod(const char* pName, const char* pSignature);
    // Its method row (the declaring record, own name, params); null as findMethod, or for a null name or signature.
    const MethodRow* findMethodRow(const char* pName, const char* pSignature);
    // The virtual calls of a name (findMethod's): how many; *pFirst is the first one's index (methodAt), the others
    // follow it by signature in byte order.
    u32 methodsNamed(const char* pName, u32* pFirst);
    const Function* methodAt(u32 index);
    // The method row of the declaration the object's own class runs for that virtual ("FoA::fa", "s32()" on a FoD
    // gives FoC's fa): for the declaring class's subobject pObject points at, the most derived record on that
    // subobject's path that declares one of that name and parameters (objects.md: D : B1, B2 from A gives
    // B1's or B2's by the A pObject is). Null: no row for the virtual, the own class has none, or pObject is no
    // subobject of the declaring class and it has more than one, with *pAmbiguous set (as asClass).
    const MethodRow* overrider(const void* pObject, const char* pName, const char* pSignature, bool* pAmbiguous);
    // The same from an own class already read and the virtual's row (findMethodRow); null for a null row.
    const MethodRow* overrider(const OwnClass& rOwn, const void* pObject, const MethodRow* pTarget, bool* pAmbiguous);

    // Classes (docs/modding/language/classes.md). The record rows' count: a game class's number is below it, a mod
    // class's at or above it.
    u32 classCount();
    // The type_info of the object's own class and the whole object, read from the vtable as ownClass reads them, with
    // no row looked up (a mod class's override asks on every call); null when the object cannot be read.
    const void* ownTypeInfo(const void* pObject, const void** ppComplete);
    // The method row a whole object of class pClass runs for the virtual pTarget (overrider's answer for an object
    // pointer at the whole object); null when pClass is not pTarget's record or derives from it, or ambiguous.
    const MethodRow* overriderOfClass(const TypeRow* pClass, const MethodRow* pTarget, bool* pAmbiguous);
    // Where the one pBase subobject is in a whole pType object. False: pBase is not pType or a base of it, or is one
    // twice.
    bool baseOffset(const TypeRow* pType, const TypeRow* pBase, u32* pOffset);
}
