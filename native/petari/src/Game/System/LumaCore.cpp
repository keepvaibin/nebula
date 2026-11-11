// LumaCore's shared parts (LumaCore.hpp): classes, handles, values, the toolkit table, fields.
#include "Game/System/LumaCore.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstring>
#include <nebula_module.h>

namespace lumacore {
    extern const ClassRow kClassRows[];
    extern const u16 kClassRowCount;
    extern const ToolkitRow kToolkitRows[];
    extern const u32 kToolkitRowCount;
    extern const FieldRow kFieldRows[];
    extern const u32 kFieldRowCount;
    // Per class: its first field row and how many (fields sorted by class, then name).
    extern const u32 kClassFields[][2];
}  // namespace lumacore

namespace {
    // Bit 15 of NameObj::mLumaClass: the id is the object's own class, not a base's.
    const u16 cExact = 0x8000;

    u32 sGeneration = 1;

    // The vtables the factory saw, by its pointer: the class of an object made elsewhere with the same one.
    const u32 cVtableSlots = 4096;  // a power of two, past the game's class count
    struct VtableSlot {
        const void* mVtable;
        u16 mClass;
    };
    VtableSlot sVtables[cVtableSlots];

    u32 slotOf(const void* pVtable) {
        u64 h = reinterpret_cast< uintptr_t >(pVtable);
        h ^= h >> 33;
        h *= 0xFF51AFD7ED558CCDull;
        h ^= h >> 33;
        return static_cast< u32 >(h) & (cVtableSlots - 1);
    }

    const void* vtableOf(const NameObj* pObj) {
        const void* v;
        std::memcpy(&v, static_cast< const void* >(pObj), sizeof(v));
        return v;
    }

    // Byte order of names, for the sorted tables (strcmp's order; the game module has no strcmp).
    s32 compare(const char* a, const char* b) {
        while (*a != '\0' && *a == *b) {
            a++;
            b++;
        }
        return static_cast< s32 >(static_cast< u8 >(*a)) - static_cast< s32 >(static_cast< u8 >(*b));
    }

    // Set by refuse(), cleared by takeRefusal().
    bool sRefusedNow = false;

    // Class ids in name order: built at the first findClass (the class table is in id order, parents first).
    const u32 cClassSlots = 4096;
    u16 sClassByName[cClassSlots];
    u32 sClassByNameCount = 0;

    // Each refusal reason once per toolkit row.
    const u32 cRefusedWords = 4096;
    u32 sRefused[cRefusedWords];

    char16_t sText16[512];

    // Matrices scripts make: a ring (a variable keeps its own copy, LumaScript keep()).
    const u32 cMatrixSlots = 1024;
    f32 sMatrices[cMatrixSlots][3][4];
    u32 sMatrixNext = 0;
}  // namespace

namespace lumacore {
    extern const char* const kOpaqueNames[];
    extern const u32 kOpaqueCount;
    const NebulaValue kNone = {};

    u16 classCount() {
        return kClassRowCount;
    }

    const ClassRow& classRow(u16 id) {
        return kClassRows[id < kClassRowCount ? id : 0];
    }

    bool isA(u16 id, u16 ancestor) {
        // The tree is shallow (under 12 levels); ids are parents-first, so a parent's id is below its child's.
        for (u32 guard = 0; guard < 64; guard++) {
            if (id == ancestor) {
                return true;
            }
            if (id == 0 || id < ancestor) {
                return false;
            }
            id = kClassRows[id].base;
        }
        return false;
    }

    void setClass(NameObj* pObj, u16 id) {
        // 0: a type with no row (a template's instance): the id its bases' constructors set stays.
        if (pObj == nullptr || id == 0 || id >= kClassRowCount) {
            return;
        }
        pObj->mLumaClass = static_cast< u16 >(id | cExact);
        const void* const vtable = vtableOf(pObj);
        for (u32 i = slotOf(vtable), n = 0; n < cVtableSlots; i = (i + 1) & (cVtableSlots - 1), n++) {
            if (sVtables[i].mVtable == vtable) {
                return;
            }
            if (sVtables[i].mVtable == nullptr) {
                sVtables[i].mVtable = vtable;
                sVtables[i].mClass = id;
                return;
            }
        }
    }

    void setBaseClass(NameObj* pObj, u16 id) {
        // A base's constructor: a deeper base's (which runs later) or the factory's id replaces it.
        pObj->mLumaClass = id;
    }

    u16 classOf(const NameObj* pObj) {
        if (pObj == nullptr) {
            return 0;
        }
        const u16 id = pObj->mLumaClass;
        if ((id & cExact) != 0) {
            return static_cast< u16 >(id & ~cExact);
        }
        const void* const vtable = vtableOf(pObj);
        for (u32 i = slotOf(vtable), n = 0; n < cVtableSlots; i = (i + 1) & (cVtableSlots - 1), n++) {
            if (sVtables[i].mVtable == vtable) {
                const_cast< NameObj* >(pObj)->mLumaClass = static_cast< u16 >(sVtables[i].mClass | cExact);
                return sVtables[i].mClass;
            }
            if (sVtables[i].mVtable == nullptr) {
                break;
            }
        }
        return id;
    }

    u16 classOfValue(const NebulaValue& rValue) {
        if (rValue.kind != NEBULA_KIND_HANDLE || (rValue.type & 0xFFFF) != HandleType_Object ||
            rValue.u.h.generation != sGeneration || rValue.u.h.p == nullptr) {
            return 0;
        }
        return classOf(static_cast< const NameObj* >(rValue.u.h.p));
    }

    void released(const void* pStart, const void* pEnd) {
        (void)pStart;
        (void)pEnd;
    }

    u32 generation() {
        return sGeneration;
    }

    void nextGeneration() {
        sGeneration++;
    }

    NebulaValue handleOf(void* p, HandleType type) {
        NebulaValue v;
        std::memset(&v, 0, sizeof(v));
        if (p == nullptr) {
            return v;
        }
        v.kind = NEBULA_KIND_HANDLE;
        v.type = type;
        v.u.h.p = p;
        v.u.h.generation = sGeneration;
        return v;
    }

    NebulaValue handle(NameObj* pObj) {
        NebulaValue v = handleOf(pObj, HandleType_Object);
        if (pObj != nullptr) {
            v.type |= static_cast< u32 >(classOf(pObj)) << 16;
        }
        return v;
    }

    NebulaValue handle(HitSensor* pSensor) {
        return handleOf(pSensor, HandleType_Sensor);
    }

    void* typed(const NebulaValue& rValue, HandleType type) {
        if (rValue.kind != NEBULA_KIND_HANDLE || (rValue.type & 0xFFFF) != type || rValue.u.h.generation != sGeneration) {
            return nullptr;
        }
        return rValue.u.h.p;
    }

    void* opaque(const NebulaValue& rValue, u16 tag) {
        if (rValue.kind != NEBULA_KIND_HANDLE || (rValue.type & 0xFFFF) != HandleType_Other ||
            (rValue.type >> 16) != tag || rValue.u.h.generation != sGeneration) {
            return nullptr;
        }
        return rValue.u.h.p;
    }

    NebulaValue opaqueHandle(const void* p, u16 tag) {
        NebulaValue v = handleOf(const_cast< void* >(p), HandleType_Other);
        if (p != nullptr) {
            v.type |= static_cast< u32 >(tag) << 16;
        }
        return v;
    }

    NebulaValue matrixView(const void* p12Floats) {
        return handleOf(const_cast< void* >(p12Floats), HandleType_Matrix);
    }

    NebulaValue fromMatrix(const void* p12Floats) {
        f32 (*const slot)[4] = sMatrices[sMatrixNext];
        sMatrixNext = (sMatrixNext + 1) % cMatrixSlots;
        std::memcpy(slot, p12Floats, sizeof(f32) * 12);
        NebulaValue v = handleOf(slot, HandleType_Matrix);
        v.u.h.generation = 0;  // the script's own: no scene ends it
        return v;
    }

    bool isOwnMatrix(const NebulaValue& rValue) {
        const u8* const p = static_cast< const u8* >(rValue.u.h.p);
        const u8* const first = reinterpret_cast< const u8* >(sMatrices);
        return rValue.kind == NEBULA_KIND_HANDLE && (rValue.type & 0xFFFF) == HandleType_Matrix && p >= first &&
               p < first + sizeof(sMatrices);
    }

    void copyMatrix(void* pDst, const NebulaValue& rValue) {
        static const f32 cIdentity[12] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        const void* src = cIdentity;
        if (rValue.kind == NEBULA_KIND_HANDLE && (rValue.type & 0xFFFF) == HandleType_Matrix && rValue.u.h.p != nullptr &&
            (rValue.u.h.generation == 0 || rValue.u.h.generation == sGeneration)) {
            src = rValue.u.h.p;
        }
        std::memcpy(pDst, src, sizeof(f32) * 12);
    }

    Vec2f asVec2(const NebulaValue& rValue) {
        if (rValue.kind == NEBULA_KIND_VEC2 || rValue.kind == NEBULA_KIND_VEC3 || rValue.kind == NEBULA_KIND_VEC4) {
            return Vec2f(rValue.u.v[0], rValue.u.v[1]);
        }
        return Vec2f(0.0f, 0.0f);
    }

    NebulaValue fromVec2(const Vec2f& rValue) {
        NebulaValue v = kNone;
        v.kind = NEBULA_KIND_VEC2;
        v.u.v[0] = rValue.x;
        v.u.v[1] = rValue.y;
        return v;
    }

    NameObj* object(const NebulaValue& rValue, u16 classId) {
        NameObj* const obj = static_cast< NameObj* >(typed(rValue, HandleType_Object));
        if (obj == nullptr || !isA(classOf(obj), classId)) {
            return nullptr;
        }
        return obj;
    }

    s64 asInt(const NebulaValue& rValue) {
        switch (rValue.kind) {
        case NEBULA_KIND_BOOL:
        case NEBULA_KIND_INT:
            return rValue.u.i;
        case NEBULA_KIND_FLOAT: {
            const f64 f = rValue.u.f;
            if (!(f == f)) {
                return 0;
            }
            return f >= 9.2233720368547748e18 ? 0x7FFFFFFFFFFFFFFFll
                   : f <= -9.2233720368547758e18 ? static_cast< s64 >(0x8000000000000000ull)
                                                 : static_cast< s64 >(f);
        }
        default:
            return 0;
        }
    }

    f64 asFloat(const NebulaValue& rValue) {
        switch (rValue.kind) {
        case NEBULA_KIND_BOOL:
        case NEBULA_KIND_INT:
            return static_cast< f64 >(rValue.u.i);
        case NEBULA_KIND_FLOAT:
            return rValue.u.f;
        default:
            return 0.0;
        }
    }

    bool asBool(const NebulaValue& rValue) {
        switch (rValue.kind) {
        case NEBULA_KIND_BOOL:
        case NEBULA_KIND_INT:
            return rValue.u.i != 0;
        case NEBULA_KIND_FLOAT:
            return rValue.u.f != 0.0;
        default:
            return false;
        }
    }

    Vec3f asVec3(const NebulaValue& rValue) {
        if (rValue.kind == NEBULA_KIND_VEC3 || rValue.kind == NEBULA_KIND_VEC4) {
            return Vec3f(rValue.u.v[0], rValue.u.v[1], rValue.u.v[2]);
        }
        return Vec3f(0.0f, 0.0f, 0.0f);
    }

    const char* asText(const NebulaValue& rValue) {
        return rValue.kind == NEBULA_KIND_TEXT && rValue.u.s != nullptr ? rValue.u.s : "";
    }

    const char16_t* asText16(const NebulaValue& rValue) {
        // UTF-8 to UTF-16; a broken sequence becomes U+FFFD.
        const u8* s = reinterpret_cast< const u8* >(asText(rValue));
        u32 out = 0;
        const u32 room = sizeof(sText16) / sizeof(sText16[0]) - 1;
        while (*s != 0 && out < room) {
            u32 c = *s++;
            u32 extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            if (c >= 0x80 && c < 0xC0) {
                c = 0xFFFD;
                extra = 0;
            } else if (extra != 0) {
                c &= 0x3Fu >> extra;
            }
            for (u32 k = 0; k < extra; k++) {
                if ((*s & 0xC0) != 0x80) {
                    c = 0xFFFD;
                    break;
                }
                c = (c << 6) | (*s++ & 0x3F);
            }
            if (c >= 0x10000 && c <= 0x10FFFF) {
                if (out + 2 > room) {
                    break;
                }
                c -= 0x10000;
                sText16[out++] = static_cast< char16_t >(0xD800 + (c >> 10));
                sText16[out++] = static_cast< char16_t >(0xDC00 + (c & 0x3FF));
            } else {
                sText16[out++] = static_cast< char16_t >(c > 0x10FFFF ? 0xFFFD : c);
            }
        }
        sText16[out] = 0;
        return sText16;
    }

    NebulaValue fromBool(bool value) {
        NebulaValue v;
        std::memset(&v, 0, sizeof(v));
        v.kind = NEBULA_KIND_BOOL;
        v.u.i = value ? 1 : 0;
        return v;
    }

    NebulaValue fromInt(s64 value) {
        NebulaValue v;
        std::memset(&v, 0, sizeof(v));
        v.kind = NEBULA_KIND_INT;
        v.u.i = value;
        return v;
    }

    NebulaValue fromFloat(f64 value) {
        NebulaValue v;
        std::memset(&v, 0, sizeof(v));
        v.kind = NEBULA_KIND_FLOAT;
        v.u.f = value;
        return v;
    }

    NebulaValue fromVec3(const Vec3f& rValue) {
        NebulaValue v;
        std::memset(&v, 0, sizeof(v));
        v.kind = NEBULA_KIND_VEC3;
        v.u.v[0] = rValue.x;
        v.u.v[1] = rValue.y;
        v.u.v[2] = rValue.z;
        return v;
    }

    NebulaValue fromText(const char* pText) {
        NebulaValue v;
        std::memset(&v, 0, sizeof(v));
        if (pText != nullptr) {
            v.kind = NEBULA_KIND_TEXT;
            v.u.s = pText;
        }
        return v;
    }

    bool takeRefusal() {
        const bool was = sRefusedNow;
        sRefusedNow = false;
        return was;
    }

    s32 findClass(const char* pName) {
        if (pName == nullptr) {
            return -1;
        }
        if (sClassByNameCount == 0) {
            // Insertion sort once: about a thousand rows, at the first lookup a mod makes.
            const u32 count = kClassRowCount < cClassSlots ? kClassRowCount : cClassSlots;
            for (u32 i = 0; i < count; i++) {
                u32 j = i;
                while (j > 0 && compare(kClassRows[sClassByName[j - 1]].name, kClassRows[i].name) > 0) {
                    sClassByName[j] = sClassByName[j - 1];
                    j--;
                }
                sClassByName[j] = static_cast< u16 >(i);
            }
            sClassByNameCount = count;
        }
        u32 lo = 0;
        u32 hi = sClassByNameCount;
        while (lo < hi) {
            const u32 mid = lo + (hi - lo) / 2;
            const s32 c = compare(kClassRows[sClassByName[mid]].name, pName);
            if (c == 0) {
                return sClassByName[mid];
            }
            if (c < 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return -1;
    }

    const FieldRow* fieldRow(u32 index) {
        return index < kFieldRowCount ? &kFieldRows[index] : nullptr;
    }

    s32 fieldIndex(const FieldRow* pField) {
        return pField == nullptr ? -1 : static_cast< s32 >(pField - kFieldRows);
    }

    void refuse(u32 function, const char* pWhy) {
        sRefusedNow = true;
        if (function < cRefusedWords * 32 && (sRefused[function >> 5] & (1u << (function & 31))) == 0) {
            sRefused[function >> 5] |= 1u << (function & 31);
            nebula_log("mods: %s refused: %s\n", function < kToolkitRowCount ? kToolkitRows[function].name : "?", pWhy);
        }
    }

    u32 toolkitCount() {
        return kToolkitRowCount;
    }

    const ToolkitRow* toolkitRow(u32 index) {
        return index < kToolkitRowCount ? &kToolkitRows[index] : nullptr;
    }

    s32 findToolkit(const char* pName) {
        // Rows are sorted by name (the generator sorts them).
        u32 lo = 0;
        u32 hi = kToolkitRowCount;
        while (lo < hi) {
            const u32 mid = lo + (hi - lo) / 2;
            const s32 c = compare(kToolkitRows[mid].name, pName);
            if (c == 0) {
                return static_cast< s32 >(mid);
            }
            if (c < 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return -1;
    }

    const FieldRow* findField(u16 classId, const char* pName) {
        // The class's own fields, then each base's.
        for (u32 guard = 0; guard < 64 && classId < kClassRowCount; guard++) {
            u32 lo = kClassFields[classId][0];
            u32 hi = lo + kClassFields[classId][1];
            while (lo < hi) {
                const u32 mid = lo + (hi - lo) / 2;
                const s32 c = compare(kFieldRows[mid].name, pName);
                if (c == 0) {
                    return &kFieldRows[mid];
                }
                if (c < 0) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            if (classId == 0) {
                break;
            }
            classId = kClassRows[classId].base;
        }
        return nullptr;
    }

    // The field's bytes in the object: through the class's own pointer (a class whose NameObj is not its first base
    // starts elsewhere).
    static u8* fieldAddress(const NebulaValue& rObject, const FieldRow& rField, u32 index) {
        NameObj* const obj = object(rObject, rField.classId);
        if (obj == nullptr || index >= rField.count) {
            return nullptr;
        }
        u8* const base = static_cast< u8* >(kClassRows[rField.classId].cast(obj));
        return base + rField.offset + index * rField.stride;
    }

    bool readField(const NebulaValue& rObject, const FieldRow& rField, u32 index, NebulaValue* pOut) {
        const u8* const at = fieldAddress(rObject, rField, index);
        if (at == nullptr) {
            return false;
        }
        switch (rField.type) {
        case FieldType_Bool:
            *pOut = fromBool(*reinterpret_cast< const bool* >(at));
            return true;
        case FieldType_S8:
            *pOut = fromInt(*reinterpret_cast< const s8* >(at));
            return true;
        case FieldType_U8:
            *pOut = fromInt(*at);
            return true;
        case FieldType_S16:
            *pOut = fromInt(*reinterpret_cast< const s16* >(at));
            return true;
        case FieldType_U16:
            *pOut = fromInt(*reinterpret_cast< const u16* >(at));
            return true;
        case FieldType_S32:
            *pOut = fromInt(*reinterpret_cast< const s32* >(at));
            return true;
        case FieldType_U32:
            *pOut = fromInt(*reinterpret_cast< const u32* >(at));
            return true;
        case FieldType_F32:
            *pOut = fromFloat(*reinterpret_cast< const f32* >(at));
            return true;
        case FieldType_F64:
            *pOut = fromFloat(*reinterpret_cast< const f64* >(at));
            return true;
        case FieldType_Vec3: {
            const f32* const f = reinterpret_cast< const f32* >(at);
            *pOut = fromVec3(Vec3f(f[0], f[1], f[2]));
            return true;
        }
        case FieldType_Object: {
            NameObj* const target = *reinterpret_cast< NameObj* const* >(at);
            *pOut = handle(target);
            return true;
        }
        case FieldType_Sensor: {
            HitSensor* const target = *reinterpret_cast< HitSensor* const* >(at);
            *pOut = handle(target);
            return true;
        }
        case FieldType_Text: {
            const char* const text = *reinterpret_cast< const char* const* >(at);
            *pOut = fromText(text);
            return true;
        }
        default:
            return false;
        }
    }

    bool writeField(const NebulaValue& rObject, const FieldRow& rField, u32 index, const NebulaValue& rValue) {
        if ((rField.flags & FieldFlag_ReadOnly) != 0) {
            return false;
        }
        u8* const at = fieldAddress(rObject, rField, index);
        if (at == nullptr) {
            return false;
        }
        switch (rField.type) {
        case FieldType_Bool:
            *reinterpret_cast< bool* >(at) = asBool(rValue);
            return true;
        case FieldType_S8:
            *reinterpret_cast< s8* >(at) = static_cast< s8 >(asInt(rValue));
            return true;
        case FieldType_U8:
            *at = static_cast< u8 >(asInt(rValue));
            return true;
        case FieldType_S16:
            *reinterpret_cast< s16* >(at) = static_cast< s16 >(asInt(rValue));
            return true;
        case FieldType_U16:
            *reinterpret_cast< u16* >(at) = static_cast< u16 >(asInt(rValue));
            return true;
        case FieldType_S32:
            *reinterpret_cast< s32* >(at) = static_cast< s32 >(asInt(rValue));
            return true;
        case FieldType_U32:
            *reinterpret_cast< u32* >(at) = static_cast< u32 >(asInt(rValue));
            return true;
        case FieldType_F32: {
            const f64 f = asFloat(rValue);
            if (!(f == f)) {
                return false;  // NaN never reaches the game's numbers
            }
            *reinterpret_cast< f32* >(at) = static_cast< f32 >(f);
            return true;
        }
        case FieldType_F64: {
            const f64 f = asFloat(rValue);
            if (!(f == f)) {
                return false;
            }
            *reinterpret_cast< f64* >(at) = f;
            return true;
        }
        case FieldType_Vec3: {
            if (rValue.kind != NEBULA_KIND_VEC3) {
                return false;
            }
            const f32* const v = rValue.u.v;
            if (!(v[0] == v[0]) || !(v[1] == v[1]) || !(v[2] == v[2])) {
                return false;
            }
            std::memcpy(at, v, sizeof(f32) * 3);
            return true;
        }
        default:
            return false;  // handles and text are read-only
        }
    }
}  // namespace lumacore
