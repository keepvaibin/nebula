#pragma once
// Numbers and tables in the game's code by name (docs/modding/language/numbers.md; luma.h LumaCore.number, set_number,
// table and set_table). The build names them: tools/lumacore/instrument.py reads each number written in a row's code
// from one record per unit, makes each table of the game's data writable in its copy, and lists both in the "lumanum"
// section (luma_hook.h luma::Numbers): a number by the row's name, signature and place, or a file's number constant by
// its name; a table by its name. The source is never changed, and the module with no rows has none of this (1.7).

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
        // LumaCore.table: 0 and *pOut, or -1 (no such table, no memory: said). Any thread.
        s32 table(const LumaMod* pMod, const char* pName, LumaTable* pOut);
        // LumaCore.set_table: 0, or luma.h's -1 to -3, said in the log. On the game thread.
        s32 setTable(const LumaMod* pMod, const char* pName, u32 offset, const void* pBytes, u32 size);
        // The tables the build named, counted once.
        u32 tableCount();
        // The mod is going (unloadMod, a refused start): each number it set is again what the mods still here set
        // last, or the game's own; each table it wrote is the game's again with the other mods' writes in their order.
        // Game thread.
        void modGone(const LumaMod* pMod);
    }
}
