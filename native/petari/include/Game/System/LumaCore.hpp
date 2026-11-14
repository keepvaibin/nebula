#pragma once

// LumaCore: what scripts and plugins reach the game through (docs/modding).
// Values (NebulaValue), handles with their class checked, the toolkit table (every MR:: utility by name), the class
// tree and the fields of the game's classes. The generated parts are in src/Game/System/generated (tools/lumacore).

#include <nebula_mod_api.h>
#include <nebula_geometry.hpp>
#include <nebula_types.h>
#include "Game/System/generated/lumacore_class_ids.hpp"

class NameObj;
class HitSensor;

namespace lumacore {
    // The game's classes by id: 0 is "unknown" (a NameObj made outside the factory whose class no placed one shared).
    struct ClassRow {
        const char* name;
        u16 base;      // the parent's id; 0 for NameObj
        u32 size;
        void* (*cast)(NameObj* pObj);  // the object as this class (where its fields start)
    };
    // kClassRows (index = id; generated/lumacore_classes.cpp) and kClassId<T> (generated/lumacore_class_ids.hpp) for
    // every class deriving from NameObj.

    u16 classCount();
    const ClassRow& classRow(u16 id);
    // `id` is `ancestor` or derives from it.
    bool isA(u16 id, u16 ancestor);
    // By name ("SpinDriver"): the id, or -1. A binary search over a name order built at the first call.
    s32 findClass(const char* pName);
    // The class of a living object: its factory's id, or the id another object with its vtable had.
    u16 classOf(const NameObj* pObj);
    // The factory sets it as it creates an object; the vtable is remembered for objects made without it.
    void setClass(NameObj* pObj, u16 id);
    // A common base's constructor (LiveActor, NPCActor...): the object is at least this; a later one refines it.
    void setBaseClass(NameObj* pObj, u16 id);
    // The class of the object a value names: 0 for anything but a living object.
    u16 classOfValue(const NebulaValue& rValue);

    // Handles: what a script holds. The scene's generation goes up when a scene ends; an older handle is none.
    enum HandleType : u16 {
        HandleType_Object = 1,  // a NameObj (actor, area, layout...): the class is the object's
        HandleType_Sensor,
        HandleType_Triangle,
        HandleType_CollisionParts,
        HandleType_Joint,
        HandleType_PlacementRow,
        HandleType_Matrix,      // 12 floats: the game's (a joint's matrix) or a script's own
        HandleType_TalkCtrl,
        HandleType_CameraInfo,
        HandleType_Other,       // one of the game's other types: its tag (kOpaqueNames) in the upper 16 bits
    };
    u32 generation();
    void nextGeneration();
    NebulaValue handle(NameObj* pObj);
    NebulaValue handle(HitSensor* pSensor);
    NebulaValue handleOf(void* p, HandleType type);

    // The object a value names when it is (or derives from) T; null otherwise (logged once per call site).
    NameObj* object(const NebulaValue& rValue, u16 classId);
    void* typed(const NebulaValue& rValue, HandleType type);
    // Another type's handle, its tag checked (a model's handle is never taken as an emitter's).
    void* opaque(const NebulaValue& rValue, u16 tag);
    NebulaValue opaqueHandle(const void* p, u16 tag);

    template < typename T >
    T* as(const NebulaValue& rValue) {
        return static_cast< T* >(object(rValue, kClassId< T >));
    }

    // Inputs and outputs of the generated thunks.
    extern const NebulaValue kNone;
    s64 asInt(const NebulaValue& rValue);
    f64 asFloat(const NebulaValue& rValue);
    bool asBool(const NebulaValue& rValue);
    Vec3f asVec3(const NebulaValue& rValue);
    const char* asText(const NebulaValue& rValue);
    // UTF-16 text for the calls that take it: valid until the next call on this thread.
    const char16_t* asText16(const NebulaValue& rValue);
    Vec2f asVec2(const NebulaValue& rValue);
    template < typename T >
    T asVec4(const NebulaValue& rValue) {
        T q;
        const bool given = rValue.kind == NEBULA_KIND_VEC4 || rValue.kind == NEBULA_KIND_VEC3;
        q.x = given ? rValue.u.v[0] : 0.0f;
        q.y = given ? rValue.u.v[1] : 0.0f;
        q.z = given ? rValue.u.v[2] : 0.0f;
        q.w = rValue.kind == NEBULA_KIND_VEC4 ? rValue.u.v[3] : 1.0f;
        return q;
    }
    // A matrix value (12 floats, 3 rows of 4) into the game's type; the identity when none is given.
    void copyMatrix(void* pDst, const NebulaValue& rValue);
    template < typename T >
    T asMatrix(const NebulaValue& rValue) {
        static_assert(sizeof(T) == sizeof(f32) * 12, "a 3x4 matrix");
        T m;
        copyMatrix(&m, rValue);
        return m;
    }

    NebulaValue fromBool(bool value);
    NebulaValue fromInt(s64 value);
    NebulaValue fromFloat(f64 value);
    NebulaValue fromVec3(const Vec3f& rValue);
    NebulaValue fromText(const char* pText);
    NebulaValue fromVec2(const Vec2f& rValue);
    template < typename T >
    NebulaValue fromVec4(const T& rValue) {
        NebulaValue v = kNone;
        v.kind = NEBULA_KIND_VEC4;
        v.u.v[0] = rValue.x;
        v.u.v[1] = rValue.y;
        v.u.v[2] = rValue.z;
        v.u.v[3] = rValue.w;
        return v;
    }
    // A matrix the script owns (a copy, from a ring a variable copies out of), and a view of one of the game's (the
    // joint's own matrix: it changes as the game does).
    NebulaValue fromMatrix(const void* p12Floats);
    NebulaValue matrixView(const void* p12Floats);
    // A matrix from fromMatrix's ring (a variable copies it out before the ring comes round).
    bool isOwnMatrix(const NebulaValue& rValue);

    // A call refused (a missing handle, a wrong class, too few arguments): logged once per function and reason.
    void refuse(u32 function, const char* pWhy);
    // Whether a refusal happened since the last call (the API's toolkit_call answers -1 then).
    bool takeRefusal();

    // The toolkit's table: the generated rows. Parameter and result kinds as letters: a none, b bool, c int, d float,
    // e vec3, f matrix, g text, h handle, i vec4, j vec2; upper case is an out-parameter (comes back after the result).
    typedef void (*Thunk)(const NebulaValue* pArgs, u32 count, NebulaValue* pOut);
    struct ToolkitRow {
        const char* name;
        const char* params;
        const char* result;
        Thunk thunk;
    };
    u32 toolkitCount();
    const ToolkitRow* toolkitRow(u32 index);
    // By name: the row's index, or -1. An overloaded name takes its parameter letters after a ':' ("MR::isBindedGround:h").
    s32 findToolkit(const char* pName);

    // Fields: a class's field by name, as a typed slot. Offsets come from offsetof in the generated file, so they are
    // the platform's own (the layouts differ between ABIs).
    enum FieldType : u16 {
        FieldType_Bool = 1,
        FieldType_S8,
        FieldType_U8,
        FieldType_S16,
        FieldType_U16,
        FieldType_S32,
        FieldType_U32,
        FieldType_F32,
        FieldType_F64,
        FieldType_Vec3,    // three f32 (Vec3f)
        FieldType_Object,  // a pointer to a NameObj (read-only)
        FieldType_Sensor,  // a pointer to a HitSensor (read-only)
        FieldType_Text,    // const char* (read-only)
    };
    struct FieldRow {
        u16 classId;
        u16 type;      // FieldType
        u32 offset;
        u32 count;     // array length (1 for one)
        u32 stride;    // bytes between elements
        u32 flags;     // FieldFlag_*
        const char* name;
    };
    enum FieldFlag { FieldFlag_ReadOnly = 1 };
    const FieldRow* findField(u16 classId, const char* pName);
    // The field table by index (the API's field numbers): null past the last.
    const FieldRow* fieldRow(u32 index);
    s32 fieldIndex(const FieldRow* pField);
    bool readField(const NebulaValue& rObject, const FieldRow& rField, u32 index, NebulaValue* pOut);
    bool writeField(const NebulaValue& rObject, const FieldRow& rField, u32 index, const NebulaValue& rValue);
};  // namespace lumacore
