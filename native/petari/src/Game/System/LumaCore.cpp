// The game's classes by id (LumaCore.hpp).
#include "Game/System/LumaCore.hpp"
#include "Game/NameObj/NameObj.hpp"
#include <cstring>

namespace lumacore {
    extern const ClassRow kClassRows[];
    extern const u16 kClassRowCount;
}  // namespace lumacore

namespace {
    // Bit 15 of NameObj::mLumaClass: the id is the object's own class, not a base's.
    const u16 cExact = 0x8000;

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
}  // namespace

namespace lumacore {
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

    u16 classOf(const NameObj* pObj) {
        if (pObj == nullptr) {
            return 0;
        }
        const u16 id = pObj->mLumaClass;
        if ((id & cExact) != 0) {
            return static_cast< u16 >(id & ~cExact);
        }
        const void* const vtable = vtableOf(pObj);
        if (vtable == nullptr) {
            return id;  // no object (memory of zeros): an empty slot's null must not match it, nor anything be written
        }
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

    void released(const void* pStart, const void* pEnd) {
        (void)pStart;
        (void)pEnd;
    }
}  // namespace lumacore
