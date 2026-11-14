#pragma once
// Objects a mod makes by name (luma.h LumaCore.make; docs/modding/language/classes.md "make"): made as a stage places
// them, by the game's own factory, while the scene makes its objects.

#include <luma.h>
#include <nebula_types.h>

struct TableData;

namespace luma {
    namespace make {
        // LumaCore.make: the count made, or luma.h's -1 to -4, said in the log. On the game thread.
        s32 make(const LumaMod* pMod, const char* pName, u32 count, const LumaMakePlace* pPlace, u32 flags, void** ppObjects);
        // A placement table `make` wrote (its objects stand in the main stage): StageDataHolder::findPlacedStageDataHolder.
        bool isMadeTable(const TableData* pData);
        // LumaCore.placement_of: 0, or luma.h's -1, -3 or -4. Game thread.
        s32 placementOf(const LumaMod* pMod, const void* pObject, LumaPlacement* pOut);
        // LumaCore.placed_at: the object, or null. Game thread.
        void* placedAt(const LumaMod* pMod, const LumaPlacement* pPlace);
    }
}
