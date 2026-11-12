#pragma once
// Numbers in the game's code by name (contract numbers, plan 1.5; luma.h LumaCore.number and set_number). The build
// names them: tools/lumacore/instrument.py reads each number written in a row's code from one record per unit and
// lists it in the "lumanum" section (luma_hook.h luma::Number) as the row's name, signature and place, or a file's
// number constant by its name. The source is never changed, and the module with no rows has none of this (1.7).

#include <luma.h>
#include <nebula_types.h>

namespace luma {
    namespace numbers {
        // LumaCore.number: the number's address and kind, or null (none of that name, or several: said). Any thread.
        void* find(const LumaMod* pMod, const char* pName, const char* pSignature, u32 place, u32* pKind);
        // LumaCore.set_number: 0, or luma.h's -1 to -3, said in the log. On the game thread.
        s32 set(const LumaMod* pMod, const char* pName, const char* pSignature, u32 place, f64 value);
        // The numbers the build named (every unit's), counted once.
        u32 count();
        // The mod is going (unloadMod, a refused start): each number it set is again what the mods still here set
        // last, or the game's own. Game thread.
        void modGone(const LumaMod* pMod);
    }
}
