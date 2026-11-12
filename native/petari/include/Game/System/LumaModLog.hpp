#pragma once

// The on-screen mod log's store (contract mod-log): every entry said about a mod this session, kept once with a count
// of its repeats, its words rendered once in the played language and in English. Read by the notice
// (NebulaModLogNotice) and the LOG page. No mod: never allocated.

#include <luma_modlog.h>
#include <nebula_types.h>

namespace luma {
    namespace modlog {
        const u32 cPerMod = 256;      // a mod's entries kept (as the kernel's record keeps)
        const u32 cSession = 1024;    // all mods' together
        const u32 cFirstKept = 16;    // a mod's first ones, kept past cSession

        // One kept entry. Its fields never change once it is published, except `count` (read it with countOf).
        struct Entry {
            u32 source;
            u32 level;
            u32 code;
            u32 line;
            u32 column;
            u32 count;               // repeats included; atomic
            u32 reload;              // the mod's reload number when it was added (plan 1.11); 0 before any
            u32 shown;               // 1: shown as a notice (an Error, or a Note of a row in cShownNotes); set at make
            const char* mod;
            const char* file;        // "" when none
            const char16_t* text;    // the played language, for the screen
            const char* textUtf8;    // the same, for the facts
            const char* english;     // for the game's log and the facts
            const char* codeName;    // the code as the page names it: "L203", "Hook.NoResult", "S0901"; "" for none
            const char* row;         // the NebulaText row its words came from ("Consent.OffDeclined"); "" for none
            Entry* next;             // in added order; atomic
            Entry* nextOfMod;        // the same mod's next entry, for the repeat lookup; under the store's lock
            u32 factCount;           // the highest count a fact was written at (one fact a count); atomic
        };

        // A mod the log has entries of, in the order its first entry came.
        struct Mod {
            const char* id;
            u32 kept;
            u32 past;    // entries not kept (cPerMod or cSession reached); atomic
            u32 reload;
            Mod* next;   // atomic
            Entry* entries;    // its entries in added order (Entry::nextOfMod); under the store's lock
            Entry* lastEntry;  // under the store's lock
            u32 factPast;      // the highest `past` a fact was written at; atomic
        };

        // From the kernel and the game's content (CODE, CONTENT, GUARD): the entry with `text` and `english` null, and
        // `pRowId` the NebulaText row its words are; the row's {names} are filled from the entry's names and values,
        // {mod} from its mod. A null row or one NebulaText lacks: the code and every value, so nothing is lost. The
        // host's entries (luma_modlog_add) carry their own words. Returns the entry kept for this key (the new one, or the
        // one it repeats), null when the limits kept none: the caller may hold it to count repeats with `counted`.
        const Entry* add(const LumaModLogEntry& rEntry, const char* pRowId);
        // A repeat its caller counts itself (the kernel's record, contract 2): the entry's count becomes `count` when that
        // is more. One atomic: no lock, no lookup, nothing copied; any thread. Null does nothing.
        void counted(const Entry* pEntry, u32 count);
        // luma_modlog_session.
        void session(u32 modsOn, LumaModLogFact fact);
        // A mod taken out and loaded again (1.11): its later entries carry the new number; the earlier stay.
        void reloaded(const char* pMod);

        // Whether a scene makes the notice: mods are on this session, or something was said anyway.
        bool wanted();
        bool allocated();
        // Grows by one with every entry kept, counted or noted; 0 while nothing is kept.
        u32 revision();
        // The first kept entry and the first mod (in order); null when none. Follow `next` (with nextOf).
        const Entry* first();
        const Mod* firstMod();
        const Entry* nextOf(const Entry* pEntry);
        const Mod* nextOf(const Mod* pMod);
        u32 countOf(const Entry* pEntry);
        u32 pastOf(const Mod* pMod);
        u32 kept();
        u32 errors();

        // The notice's cursor: the next shown entry (Entry::shown) no notice has taken yet (in added order), taken. Null
        // when none.
        const Entry* takeNotice();
        // Shown entries no notice has taken yet.
        u32 noticesWaiting();
        // Once a session, for the first notice made: how many Errors were kept before it (they are taken as one summary
        // line, not one by one). 0 after the first call.
        u32 takeSummary();

        // A harness fact, when this is a harness run (the writer luma_modlog_session gave); nothing otherwise.
        void fact(const char* pKey, const char* pJson);
        bool facts();
        // `pText` as a JSON string with its quotes, into pDst (cut at a character when it would not fit).
        void jsonString(char* pDst, u32 size, const char* pText);
        // The module's row `pRow` in the played language as UTF-8 into pDst (`size` bytes, cut at a character), {mod} and
        // {count} filled; the bytes written, 0 when the row is unknown (luma_modlog_words).
        u32 words(const char* pRow, const char* pMod, u32 count, char* pDst, u32 size);
        // Any row with {mod} and the entry's {names} filled (luma_modlog_row_words); 0 when the row is unknown.
        u32 rowWords(const char* pRow, const LumaModLogEntry& rValues, char* pDst, u32 size);
        // UTF-16 to UTF-8 within `size`.
        void toUtf8(char* pDst, u32 size, const char16_t* pText);
    }  // namespace modlog
}  // namespace luma
