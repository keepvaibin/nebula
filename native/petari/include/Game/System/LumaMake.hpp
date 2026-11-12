#pragma once
// Objects a mod makes by name (plan 1.3 "make"; luma.h LumaCore.make, contract classes "make"): made as a stage places
// them, by the game's own factory, while the scene makes its objects.

#include <luma.h>
#include <nebula_types.h>

struct TableData;

namespace luma {
    namespace make {
        // LumaCore.make: the count made, or luma.h's -1 to -4, said in the log. On the game thread.
        s32 make(const LumaMod* pMod, const char* pName, u32 count, const LumaPlace* pPlace, u32 flags, void** ppObjects);
        // A placement table `make` wrote (its objects stand in the main stage): StageDataHolder::findPlacedStageDataHolder.
        bool isMadeTable(const TableData* pData);
    }
}
