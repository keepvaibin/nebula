#pragma once

// The game's classes by id (tools/lumacore/gen_lumacore.py): the factory and every constructor name an object's class,
// so a living object's class is one load. LumaCore's kernel names classes by their types (luma_types.h); these ids
// serve the content services (an effect's or a model's host by class).

#include <nebula_geometry.hpp>
#include <nebula_types.h>
#include "Game/System/generated/lumacore_class_ids.hpp"

class NameObj;

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
    // The class of a living object: its factory's id, or the id another object with its vtable had.
    u16 classOf(const NameObj* pObj);
    // The factory sets it as it creates an object; the vtable is remembered for objects made without it.
    void setClass(NameObj* pObj, u16 id);

    // An arena's memory, [pStart, pEnd), is about to go with every object in it, whose destructors mostly do not run:
    // told at the start of each NebulaArena::destroy, a parent before its children (loader contract 7). It does
    // nothing itself; a mod that keeps objects hooks it (before) and forgets those in the range.
    void released(const void* pStart, const void* pEnd);
}  // namespace lumacore
