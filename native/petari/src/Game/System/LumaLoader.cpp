#include "Game/System/LumaLoader.hpp"

#include "Game/NameObj/NameObj.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/LumaHook.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/LumaServices.hpp"
#include "Game/System/LumaTypes.hpp"
#include "Game/System/LumaClasses.hpp"
#include "Game/System/LumaMake.hpp"
#include "Game/System/LumaNumbers.hpp"
#include "Game/System/NebulaText.hpp"
#include <luma_host.h>
#include <luma_modlog.h>
#include <nebula_format.h>
#include <nebula_game_api.h>
#include <nebula_module.h>
#include <stddef.h>
#include <stdlib.h>

namespace luma {
    // `count` zeroed items of `size` bytes from the module's malloc (it has no calloc); null when there is no memory or
    // the size does not fit.
    static void* zeroed(size_t count, size_t size) {
        if (size != 0 && count > static_cast<size_t>(-1) / size) {
            return nullptr;
        }
        const size_t bytes = count * size;
        unsigned char* const p = static_cast<unsigned char*>(::malloc(bytes != 0 ? bytes : 1));
        if (p != nullptr) {
            for (size_t i = 0; i < bytes; i++) {
                p[i] = 0;
            }
        }
        return p;
    }

    // One block a mod took with core->alloc, just before the bytes it gives. `free` reads it only after finding the
    // pointer in the mod's own set (D2-3); the check word is a second check.
    struct Block {
        void* start;  // what malloc gave
        u64 size;     // what the mod asked for
        const LumaMod* owner;
        uintptr_t check;  // the block's address mixed with kBlockCheck; 0 once freed
    };
    const uintptr_t kBlockCheck = static_cast<uintptr_t>(0x4C554D41424C4B21ull);

    // One thing a mod's code reported (loader contract 6, D3-1): its place and code, its args the first time, how often.
    const u32 kReportArgs = 8;
    const u32 kReportsKept = 256;
    struct Report {
        char* file;  // copied
        u32 line;
        u32 code;
        char* args[kReportArgs];  // copied; past kReportArgs, `cut`
        u32 argCount;
        bool cut;
        u64 count;
        const luma::modlog::Entry* entry;  // its entry in the mod log (contract mod-log 2), counted with it; null: not kept
    };

    // The addresses alloc gave one mod: open addressing, kept at most half full. 0 is an empty slot, 1 a taken-out one.
    struct BlockSet {
        uintptr_t* slots;
        u32 capacity;  // a power of two, or 0
        u32 used;      // live and taken-out slots

        static u32 hash(uintptr_t p, u32 capacity) {
            return static_cast<u32>(((p >> 4) * static_cast<uintptr_t>(0x9E3779B97F4A7C15ull)) >> 32) & (capacity - 1);
        }

        bool add(uintptr_t p) {
            if ((used + 1) * 2 > capacity) {
                const u32 grown = capacity == 0 ? 64u : capacity * 2u;
                uintptr_t* const pSlots = static_cast<uintptr_t*>(zeroed(grown, sizeof(uintptr_t)));
                if (pSlots == nullptr) {
                    return false;
                }
                u32 live = 0;
                for (u32 i = 0; i < capacity; i++) {
                    if (slots[i] > 1) {
                        u32 at = hash(slots[i], grown);
                        while (pSlots[at] != 0) {
                            at = (at + 1) & (grown - 1);
                        }
                        pSlots[at] = slots[i];
                        live++;
                    }
                }
                ::free(slots);
                slots = pSlots;
                capacity = grown;
                used = live;
            }
            u32 at = hash(p, capacity);
            while (slots[at] > 1) {
                at = (at + 1) & (capacity - 1);
            }
            if (slots[at] == 0) {
                used++;
            }
            slots[at] = p;
            return true;
        }

        // Taken out when there; false when this set never held it.
        bool remove(uintptr_t p) {
            if (capacity == 0) {
                return false;
            }
            for (u32 at = hash(p, capacity), n = 0; n < capacity && slots[at] != 0; at = (at + 1) & (capacity - 1), n++) {
                if (slots[at] == p) {
                    slots[at] = 1;
                    return true;
                }
            }
            return false;
        }
    };
}

struct LumaMod {
    const LumaModInfo* info;
    char name[64];   // the mod's id
    char file[128];  // its library's file name
    s32 order;
    // Its memory (core->alloc), counted for the Mods screen and freed after its grace. `lock` guards the set.
    luma::BlockSet blocks;
    u64 bytes;
    u32 blockCount;
    u32 lock;
    // Its library, closed by the kernel (LR-5).
    void* library;
    LumaCloseLibrary close;
    bool begun;      // its begin ran (so finish runs; T-2: a mod refused before begin runs no terminator)
    bool published;  // a hook of it was in a chain at some time (so its end waits for the grace)
    u32 grace;       // the grace mark of its end
    LumaMod* nextGone;
    // What its code reported (D3-1), for the Mods page and the mod log; under `lock`. Past kReportsKept, counted.
    luma::Report* reports;
    u32 reportCount;
    u64 reportsPast;
    // The permissions the player granted it (D3-2), copied.
    char** granted;
    u32 grantedCount;
    // The mods it imported functions from (kind 6, D3-3), each once, in bind order: told `dependent_gone` when it goes.
    LumaMod** uses;
    u32 useCount;
    // How it ended (reload.md 3, the takeout.<id> fact) and where its code is now (keep and kept, reload.md 4).
    bool takenOut;      // taken out while the game runs (luma_take_out_request)
    bool startRefused;  // its start answered non-zero
    bool stopped;       // its stop ran
    bool inStop;        // inside its stop: keep answers
    bool inStart;       // inside its start: kept answers
    u32 startedAt;      // its place in the start order, from 1 (a mod starts after every mod it uses)
};

namespace luma {
    static_assert(sizeof(LumaFunction) == sizeof(Function) && sizeof(LumaCall) == sizeof(Call), "luma.h and luma_hook.h name the same things");
    static_assert(offsetof(LumaCall, ran) == offsetof(Call, ran) && offsetof(LumaCall, chain) == offsetof(Call, chain),
                  "luma.h and LumaHook.hpp lay a call out alike");
    static_assert(LUMA_BEFORE == Hook_Before && LUMA_AFTER == Hook_After && LUMA_WRAP == Hook_Wrap && LUMA_REPLACE == Hook_Replace,
                  "luma.h and LumaHook.hpp number the hook kinds alike");

    namespace {
        const u32 cMaxMods = LUMA_MAX_MODS;
        // luma.h's LumaModInfo before begin and finish: a mod built so far still binds; it has neither.
        const u32 cInfoLeast = static_cast<u32>(offsetof(LumaModInfo, begin));
        LumaMod* sMods[cMaxMods];
        u32 sModCount = 0;
        // Mods taken out (or refused after their code ran) whose grace has not ended.
        LumaMod* sGone = nullptr;
        u32 sStartedCount = 0;

        // Take-out requests (reload.md 2): any thread queues a mod's id under sTakeLock; the game thread takes them all
        // at a frame's start. No request, no cost: sTakeCount is 0 and the frame's check is one load.
        const u32 cTakeQueue = 16;
        const u32 cIdBytes = 64;  // LumaMod::name's size: an id is at most 63 bytes
        char sTakeQueue[cTakeQueue][cIdBytes];
        u32 sTakeCount = 0;
        u32 sTakeLock = 0;
        // takeOutRequested's own lists (game thread only), here rather than on its stack: the module links no
        // __chkstk, so no frame may reach a page (LUMA_MAX_MODS pointers are 2 KiB).
        char sTakeIds[cTakeQueue][cIdBytes];
        LumaMod* sTakeOut[cMaxMods];
        // The host, told each mod's id as it is taken out (luma_set_taken_out); null: nobody.
        LumaTakenOut sTakenOut = nullptr;

        // What a mod's stop kept for its next load this session (keep and kept, reload.md 4): one entry a mod id, the
        // bytes copied. An entry goes when its bytes are read (after the start that read them) or kept as nothing.
        struct Kept {
            char id[cIdBytes];  // empty: a free entry
            unsigned char* pBytes;
            u32 size;
            bool read;  // given by kept during the start under way
        };
        Kept sKept[cMaxMods];
        const u32 cKeepMost = 1u << 20;

        const LumaFunction* asPublic(const Function* pFunction) { return reinterpret_cast<const LumaFunction*>(pFunction); }
        const Function* asRow(const LumaFunction* pFunction) { return reinterpret_cast<const Function*>(pFunction); }

        void copyText(char* pDst, u32 size, const char* pSrc) {
            u32 i = 0;
            for (; i + 1 < size && pSrc[i] != 0; i++) {
                pDst[i] = pSrc[i];
            }
            // Never half a UTF-8 character at the end.
            if (pSrc[i] != 0) {
                while (i > 0 && (static_cast<unsigned char>(pSrc[i]) & 0xC0u) == 0x80u) {
                    i--;
                }
            }
            if (size != 0) {
                pDst[i] = 0;
            }
        }

        // A refusal as the validator says it (loader contract 3): the code, the place, then the rule's parameters.
        struct Refuse {
            LumaRefusal* refusal;
            const char* id;    // as the host gave them, whole
            const char* file;

            void start(u16 code) {
                if (refusal == nullptr) {
                    return;
                }
                refusal->code = code;
                refusal->count = 0;
                put("mod", 'T', id);
                put("file", 'T', file);
            }

            // `kind` 'T' a literal, 'P' a phrase id. The value is copied whole and kept for the session (D2-2): the
            // mod's library, where a name it asked for lives, is closed before the Mods page shows it.
            void put(const char* pName, char kind, const char* pValue) {
                if (refusal == nullptr) {
                    return;
                }
                if (refusal->count == LUMA_REFUSAL_MOST) {
                    NEBULA_PANIC("LumaCore: a refusal with more than %d parameters", LUMA_REFUSAL_MOST);
                }
                const char* const pText = pValue != nullptr ? pValue : "";
                size_t length = 0;
                while (pText[length] != 0) {
                    length++;
                }
                char* const pKept = static_cast<char*>(::malloc(length + 3));
                if (pKept == nullptr) {
                    NEBULA_PANIC("LumaCore: no memory to keep a refusal's words");
                }
                pKept[0] = kind;
                pKept[1] = ':';
                for (size_t i = 0; i <= length; i++) {
                    pKept[2 + i] = pText[i];
                }
                refusal->values[refusal->count] = pKept;
                refusal->names[refusal->count++] = pName;
            }

            void number(const char* pName, s64 value) {
                char text[24];
                nebula_snprintf(text, sizeof(text), "%lld", static_cast<long long>(value));
                put(pName, 'T', text);
            }
        };

        void apiLog(const LumaMod* pMod, const char* pText) { nebula_log("[mod %s] %s\n", pMod != nullptr ? pMod->name : "?", pText); }

        const LumaFunction* apiFunction(const char* pName, const char* pSignature) {
            return pName != nullptr ? asPublic(findFunction(pName, pSignature, nullptr)) : nullptr;
        }

        const LumaFunction* apiFunctionAt(u32 index) { return asPublic(functionAt(index)); }

        s32 apiHook(const LumaMod* pConstMod, const LumaFunction* pFunction, u32 kind, LumaHook pHook, void* pUser) {
            LumaMod* const pMod = const_cast<LumaMod*>(pConstMod);
            if (pMod == nullptr) {
                return HookResult_Refused;
            }
            const HookResult result =
                addHook(asRow(pFunction), static_cast<HookKind>(kind), reinterpret_cast<HookCallback>(pHook), pUser, pMod, pMod->order);
            if (result == HookResult_Added) {
                pMod->published = true;
            }
            return result;
        }

        void apiProceed(LumaCall* pCall) { proceed(reinterpret_cast<Call*>(pCall)); }

        // A class's number (objects 10, O-3): its record's index among the record rows sorted by name; -1 when none.
        // A mod's class (classes contract) by its name when no record has it.
        s32 apiClassOf(const char* pClass) {
            const s32 number = classNumber(findType(pClass));
            return number >= 0 ? number : luma::classes::numberOf(pClass);
        }

        // Any record the game defines (contract types), by its name or an alias's.
        s32 apiSizeOf(const char* pType) {
            const TypeRow* const pRow = findType(pType);
            return pRow != nullptr ? static_cast<s32>(pRow->size) : -1;
        }

        // As kind 1 binds it: through the record's bases, a bit-field's position in bits; with a type, only that type.
        s32 apiField(const char* pType, const char* pField, const char* pTypeText) {
            const FieldBind bind = findField(pType, pField, pTypeText);
            return bind.found && (pTypeText == nullptr || bind.sameType) ? bind.value : -1;
        }

        // as_class's answer without a report (it has no mod): 1 when the object's own class is that class or derives
        // from it by one path.
        s32 apiIsA(const void* pObject, s32 number) {
            if (luma::classes::isModNumber(number)) {
                return luma::classes::asModClass(pObject, number) != nullptr ? 1 : 0;
            }
            bool ambiguous = false;
            return asClass(pObject, classAt(number), &ambiguous) != nullptr ? 1 : 0;
        }

        // The host's finder (luma_set_service_finder): the harness's extension services, which need no permission.
        LumaServiceFinder sServiceFinder = nullptr;

        // A service and the permission it needs (null: none), or null: the registry's first (services-registry 12e),
        // then the host's finder.
        const void* findService(const char* pName, u32 major, u32 minor, const char** ppPermission) {
            *ppPermission = nullptr;
            if (const void* const pTable = luma::services::find(pName, major, minor, ppPermission)) {
                return pTable;
            }
            return pName != nullptr && sServiceFinder != nullptr ? sServiceFinder(pName, major, minor, ppPermission) : nullptr;
        }

        const void* apiService(const LumaMod* pConstMod, const char* pName, u32 major, u32 minor);

        s32 apiGameThread() { return onGameThread() ? 1 : 0; }

        s32 apiUnhook(const LumaMod* pMod, const LumaFunction* pFunction, u32 kind, LumaHook pHook, void* pUser) {
            if (pMod == nullptr || pFunction == nullptr || kind >= Hook_KindCount) {
                return -1;
            }
            return removeHook(asRow(pFunction), static_cast<HookKind>(kind), reinterpret_cast<HookCallback>(pHook), pUser, pMod);
        }

        void lockMod(LumaMod* pMod) {
            while (__atomic_exchange_n(&pMod->lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
#if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
                __asm__ volatile("yield");
#endif
            }
        }

        void unlockMod(LumaMod* pMod) { __atomic_store_n(&pMod->lock, 0u, __ATOMIC_RELEASE); }

        // The take-out queue's lock (any thread; held for a few copies only).
        void lockTakes() {
            while (__atomic_exchange_n(&sTakeLock, 1u, __ATOMIC_ACQUIRE) != 0u) {
#if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
                __asm__ volatile("yield");
#endif
            }
        }

        void unlockTakes() { __atomic_store_n(&sTakeLock, 0u, __ATOMIC_RELEASE); }

        bool sameText(const char* pA, const char* pB) {
            if (pA == nullptr || pB == nullptr) {
                return pA == pB;
            }
            while (*pA != 0 && *pA == *pB) {
                pA++;
                pB++;
            }
            return *pA == *pB;
        }

        char* keepText(const char* pText) {
            const char* const pFrom = pText != nullptr ? pText : "";
            size_t length = 0;
            while (pFrom[length] != 0) {
                length++;
            }
            char* const pKept = static_cast<char*>(::malloc(length + 1));
            if (pKept == nullptr) {
                NEBULA_PANIC("LumaCore: no memory to keep a mod's report");
            }
            for (size_t i = 0; i <= length; i++) {
                pKept[i] = pFrom[i];
            }
            return pKept;
        }

        void dropReport(Report* pReport) {
            ::free(pReport->file);
            for (u32 i = 0; i < pReport->argCount; i++) {
                ::free(pReport->args[i]);
            }
        }

        // The kernel's and luma.h's own codes (D3-1): the row and the names of its args in order. {mod} the kernel fills.
        struct OwnRow {
            u32 code;
            const char* id;
            const char* names[4];
        };
        const OwnRow cOwnRows[] = {
            {LUMA_REPORT_OUT_OF_MEMORY, "Hook.OutOfMemory", {"size", nullptr}},
            {2, "Hook.NoResult", {"found", nullptr}},
            {3, "Hook.SkipRefused", {"found", nullptr}},
            {4, "Hook.ServiceNotGranted", {"service", "permission"}},
            {5, "Object.NoRow", {"found", nullptr}},
            {6, "Object.NotReadable", {"where", nullptr}},
            {7, "Object.Ambiguous", {"where", "found"}},
        };
        const u32 kCodeNoResult = 2;
        const u32 kCodeSkipRefused = 3;
        const u32 kCodeNotGranted = 4;
        const u32 kCodeObjectNoRow = 5;
        const u32 kCodeObjectNotReadable = 6;
        const u32 kCodeObjectAmbiguous = 7;

        // The row a code says, into pId, and the names of its args (null: positional); false when the code is no row
        // (a bug in the reporting runtime). 100 to 899 are the services' (services-registry 12d).
        bool rowOf(u32 code, char* pId, u32 size, const char* const** pppNames) {
            *pppNames = nullptr;
            for (u32 i = 0; i < sizeof(cOwnRows) / sizeof(cOwnRows[0]); i++) {
                if (cOwnRows[i].code == code) {
                    *pppNames = cOwnRows[i].names;
                    copyText(pId, size, cOwnRows[i].id);
                    return true;
                }
            }
            if (code >= 100u && code <= 899u) {
                return luma::services::rowOf(code, pId, size, pppNames);
            }
            if (code >= 900u && code <= 9999u) {
                nebula_snprintf(pId, size, "Script.S%04u", code);
                return true;
            }
            return false;
        }

        // UTF-8 of one UTF-16 unit or pair, appended within `size`.
        u32 putUnit(char* pDst, u32 at, u32 size, u32 c) {
            char bytes[4];
            u32 n = 0;
            if (c < 0x80u) {
                bytes[n++] = static_cast<char>(c);
            } else if (c < 0x800u) {
                bytes[n++] = static_cast<char>(0xC0u | (c >> 6));
                bytes[n++] = static_cast<char>(0x80u | (c & 0x3Fu));
            } else if (c < 0x10000u) {
                bytes[n++] = static_cast<char>(0xE0u | (c >> 12));
                bytes[n++] = static_cast<char>(0x80u | ((c >> 6) & 0x3Fu));
                bytes[n++] = static_cast<char>(0x80u | (c & 0x3Fu));
            } else {
                bytes[n++] = static_cast<char>(0xF0u | (c >> 18));
                bytes[n++] = static_cast<char>(0x80u | ((c >> 12) & 0x3Fu));
                bytes[n++] = static_cast<char>(0x80u | ((c >> 6) & 0x3Fu));
                bytes[n++] = static_cast<char>(0x80u | (c & 0x3Fu));
            }
            if (at + n >= size) {
                return at;
            }
            for (u32 i = 0; i < n; i++) {
                pDst[at++] = bytes[i];
            }
            pDst[at] = 0;
            return at;
        }

        u32 putText(char* pDst, u32 at, u32 size, const char* pText) {
            for (; pText != nullptr && *pText != 0 && at + 1 < size; pText++) {
                pDst[at++] = *pText;
            }
            pDst[at] = 0;
            return at;
        }

        // The report's English words for the game's log: the row's {names} filled, a missing one left as {name}; with no
        // such row (its table not built yet, or a bad code), the row's id or the code and every arg, so nothing is lost.
        void englishOf(char* pDst, u32 size, const LumaMod* pMod, const Report& rReport) {
            pDst[0] = 0;
            char id[32];
            const char* const* pNames = nullptr;
            const bool known = rowOf(rReport.code, id, sizeof(id), &pNames);
            const char16_t* const pTemplate = known ? NebulaText::english(id) : nullptr;
            u32 at = 0;
            if (pTemplate == nullptr) {
                if (known) {
                    at = putText(pDst, at, size, id);
                } else {
                    char code[48];
                    nebula_snprintf(code, sizeof(code), "report code %u is no row (a bug in the mod's runtime)", rReport.code);
                    at = putText(pDst, at, size, code);
                }
                for (u32 i = 0; i < rReport.argCount; i++) {
                    at = putText(pDst, at, size, i == 0 ? ": " : ", ");
                    at = putText(pDst, at, size, rReport.args[i]);
                }
                if (rReport.cut) {
                    at = putText(pDst, at, size, ", ...");
                }
                return;
            }
            for (const char16_t* pAt = pTemplate; *pAt != 0;) {
                if (*pAt == u'{') {
                    char name[24];
                    u32 n = 0;
                    const char16_t* pEnd = pAt + 1;
                    while (*pEnd != 0 && *pEnd != u'}' && n + 1 < sizeof(name) && *pEnd < 0x80) {
                        name[n++] = static_cast<char>(*pEnd++);
                    }
                    name[n] = 0;
                    if (*pEnd == u'}') {
                        const char* pValue = nullptr;
                        if (sameText(name, "mod")) {
                            pValue = pMod->name;
                        } else {
                            for (u32 i = 0; i < rReport.argCount && pValue == nullptr; i++) {
                                char positional[12];
                                nebula_snprintf(positional, sizeof(positional), "%u", i);
                                const char* const pName = pNames != nullptr ? (i < 4 ? pNames[i] : nullptr) : positional;
                                if (pName != nullptr && sameText(name, pName)) {
                                    pValue = rReport.args[i];
                                }
                            }
                        }
                        if (pValue != nullptr) {
                            at = putText(pDst, at, size, pValue);
                            pAt = pEnd + 1;
                            continue;
                        }
                    }
                }
                u32 c = *pAt++;
                if (c >= 0xD800u && c < 0xDC00u && *pAt >= 0xDC00u && *pAt < 0xE000u) {
                    c = 0x10000u + ((c - 0xD800u) << 10) + (static_cast<u32>(*pAt++) - 0xDC00u);
                }
                at = putUnit(pDst, at, size, c);
            }
            if (rReport.cut) {
                putText(pDst, at, size, " (...)");
            }
        }

        void logReport(const LumaMod* pMod, const Report& rReport, u64 seen) {
            char text[768];
            englishOf(text, sizeof(text), pMod, rReport);
            char times[40] = "";
            if (seen > 1) {
                nebula_snprintf(times, sizeof(times), " (seen %llu times)", static_cast<unsigned long long>(seen));
            }
            if (rReport.line != 0) {
                nebula_log("[mod %s] %s:%u: %s%s\n", pMod->name, rReport.file, rReport.line, text, times);
            } else {
                nebula_log("[mod %s] %s%s\n", pMod->name, text, times);
            }
        }

        // The report as an entry of the mod log (contract mod-log 2, the kernel's step 1): source CODE, an error, its
        // row's words; the log keeps its own limits. Returns the entry kept for this key, or null. Any thread.
        const luma::modlog::Entry* toModLog(const LumaMod* pMod, const char* pFile, u32 line, u32 code, const char* const* ppArgs,
                                            u32 argCount) {
            static const char* const cPositional[LUMA_MODLOG_ARGS] = {"0", "1", "2", "3", "4", "5", "6", "7"};
            char id[32];
            const char* const* pNames = nullptr;
            const bool known = rowOf(code, id, sizeof(id), &pNames);
            LumaModLogEntry entry = {};
            entry.size = sizeof(entry);
            entry.source = LUMA_MODLOG_CODE;
            entry.level = LUMA_MODLOG_ERROR;
            entry.code = code;
            entry.mod = pMod->name;
            entry.file = pFile;
            entry.line = line;
            entry.count = argCount < LUMA_MODLOG_ARGS ? argCount : LUMA_MODLOG_ARGS;
            for (u32 i = 0; i < entry.count; i++) {
                entry.names[i] = pNames != nullptr ? (i < 4 ? pNames[i] : nullptr) : cPositional[i];
                entry.values[i] = ppArgs != nullptr && ppArgs[i] != nullptr ? ppArgs[i] : "";
            }
            return luma::modlog::add(entry, known ? id : nullptr);
        }

        // The arguments' part of a report's key (mod-log 2): the same mistake again is a repeat; another mistake at the
        // same place ("wrong flags" after "wrong path", both from assets.find) is a report of its own.
        bool sameArgs(const Report& rReport, const char* const* ppArgs, u32 argCount) {
            if ((argCount < kReportArgs ? argCount : kReportArgs) != rReport.argCount || (argCount > kReportArgs) != rReport.cut) {
                return false;
            }
            for (u32 i = 0; i < rReport.argCount; i++) {
                const char* const pGiven = ppArgs != nullptr && ppArgs[i] != nullptr ? ppArgs[i] : "";
                if (!sameText(rReport.args[i] != nullptr ? rReport.args[i] : "", pGiven)) {
                    return false;
                }
            }
            return true;
        }

        bool powerOfTen(u64 n) {
            while (n >= 10u && n % 10u == 0) {
                n /= 10u;
            }
            return n == 1u;
        }

        // One report of a mod (D3-1): a new (file, line, code) is kept and said; a repeat is counted and said again at
        // 10, 100, 1000... Past kReportsKept kept ones, counted as one more, said the same way.
        void record(LumaMod* pMod, const char* pFile, u32 line, u32 code, const char* const* ppArgs, u32 argCount) {
            const char* const pPlace = pFile != nullptr ? pFile : "";
            for (int pass = 0; pass < 2; pass++) {
                lockMod(pMod);
                for (u32 i = 0; i < pMod->reportCount; i++) {
                    Report& rReport = pMod->reports[i];
                    if (rReport.code == code && rReport.line == line && sameText(rReport.file, pPlace) &&
                        sameArgs(rReport, ppArgs, argCount)) {
                        const u64 seen = ++rReport.count;
                        const luma::modlog::Entry* const pEntry = rReport.entry;
                        unlockMod(pMod);
                        luma::modlog::counted(pEntry, seen < 0xFFFFFFFFull ? static_cast<u32>(seen) : 0xFFFFFFFFu);
                        if (powerOfTen(seen)) {
                            logReport(pMod, rReport, seen);
                        }
                        return;
                    }
                }
                if (pMod->reportCount == kReportsKept) {
                    const u64 past = ++pMod->reportsPast;
                    unlockMod(pMod);
                    (void)toModLog(pMod, pPlace, line, code, ppArgs, argCount);  // the log counts what it keeps itself
                    if (powerOfTen(past)) {
                        nebula_log("[mod %s] %llu reports more than the Mods page keeps (%u); counted, not kept\n", pMod->name,
                                   static_cast<unsigned long long>(past), kReportsKept);
                    }
                    return;
                }
                unlockMod(pMod);
                if (pass == 1) {
                    break;
                }
                // Copied outside the lock; another thread may have kept the same one meanwhile (the second pass).
                Report made = {};
                made.file = keepText(pPlace);
                made.line = line;
                made.code = code;
                made.argCount = argCount < kReportArgs ? argCount : kReportArgs;
                made.cut = argCount > kReportArgs;
                for (u32 i = 0; i < made.argCount; i++) {
                    made.args[i] = keepText(ppArgs != nullptr ? ppArgs[i] : nullptr);
                }
                made.count = 1;
                made.entry = toModLog(pMod, pPlace, line, code, ppArgs, argCount);
                lockMod(pMod);
                bool kept = false;
                for (u32 i = 0; i < pMod->reportCount && !kept; i++) {
                    Report& rReport = pMod->reports[i];
                    if (rReport.code == code && rReport.line == line && sameText(rReport.file, pPlace) &&
                        sameArgs(rReport, ppArgs, argCount)) {
                        rReport.count++;
                        luma::modlog::counted(rReport.entry, rReport.count < 0xFFFFFFFFull ? static_cast<u32>(rReport.count) : 0xFFFFFFFFu);
                        kept = true;
                    }
                }
                if (!kept && pMod->reportCount < kReportsKept) {
                    if (pMod->reports == nullptr) {
                        pMod->reports = static_cast<Report*>(zeroed(kReportsKept, sizeof(Report)));
                        if (pMod->reports == nullptr) {
                            unlockMod(pMod);
                            NEBULA_PANIC("LumaCore: no memory for %s's reports", pMod->name);
                        }
                    }
                    pMod->reports[pMod->reportCount++] = made;
                    unlockMod(pMod);
                    logReport(pMod, made, 1);
                    return;
                }
                unlockMod(pMod);
                dropReport(&made);
                if (!kept) {
                    continue;  // the list filled meanwhile: counted past it
                }
                return;
            }
            // The list filled between the passes.
            lockMod(pMod);
            const u64 past = ++pMod->reportsPast;
            unlockMod(pMod);
            if (powerOfTen(past)) {
                nebula_log("[mod %s] %llu reports more than the Mods page keeps (%u); counted, not kept\n", pMod->name,
                           static_cast<unsigned long long>(past), kReportsKept);
            }
        }

        void apiReport(const LumaMod* pConstMod, const char* pFile, u32 line, u32 code, const char* const* ppArgs, u32 argCount) {
            LumaMod* const pMod = const_cast<LumaMod*>(pConstMod);
            if (pMod == nullptr) {
                nebula_log("LumaCore: report(%u) without its mod; it cannot be kept\n", code);
                return;
            }
            record(pMod, pFile, line, code, ppArgs, argCount);
        }

        // Objects (contract objects 10). What a call was given, read once: 0, or own_class's -1 (null), -2 (its class
        // has no row: reported with the type_info's name) or -3 (not a game object the kernel can read: reported with
        // the call's name). A report's place is the call's name, so each is kept once a mod and repeats are counted.
        s32 readObject(const LumaMod* pMod, const void* pObject, const char* pWhere, OwnClass* pOwn) {
            *pOwn = ownClass(pObject);
            if (pObject == nullptr) {
                return -1;
            }
            if (pOwn->typeInfo == nullptr) {
                const char* const args[1] = {pWhere};
                apiReport(pMod, pWhere, 0, kCodeObjectNotReadable, args, 1);
                return -3;
            }
            if (pOwn->type == nullptr) {
                const char* const args[1] = {typeInfoName(pOwn->typeInfo)};
                apiReport(pMod, pWhere, 0, kCodeObjectNoRow, args, 1);
                return -2;
            }
            return 0;
        }

        void reportAmbiguous(const LumaMod* pMod, const char* pWhere, const TypeRow* pClass) {
            const char* const args[2] = {pWhere, pClass->name};
            apiReport(pMod, pWhere, 0, kCodeObjectAmbiguous, args, 2);
        }

        s32 apiOwnClass(const LumaMod* pMod, const void* pObject) {
            OwnClass own;
            const s32 state = readObject(pMod, pObject, "own_class", &own);
            if (state != 0) {
                return state;
            }
            // One of a mod's classes (classes contract): that class is the most derived.
            const s32 modClass = luma::classes::classOfObject(pObject);
            return modClass >= 0 ? modClass : classNumber(own.type);
        }

        const void* apiAsClass(const LumaMod* pMod, const void* pObject, s32 number) {
            if (luma::classes::isModNumber(number)) {
                return luma::classes::asModClass(pObject, number);
            }
            const TypeRow* const pTarget = classAt(number);
            OwnClass own;
            if (pTarget == nullptr || readObject(pMod, pObject, "as_class", &own) != 0) {
                return nullptr;
            }
            bool ambiguous = false;
            const void* const pSubobject = asClass(own, pTarget, &ambiguous);
            if (ambiguous) {
                reportAmbiguous(pMod, "as_class", pTarget);
            }
            return pSubobject;
        }

        // The virtual call (invoke takes args[0] as the object seen as the declaring class; `hooked` null: it is not
        // one function, and hook refuses it).
        const LumaFunction* apiMethod(const char* pName, const char* pSignature) { return asPublic(findMethod(pName, pSignature)); }

        // The function row of the declaration the object's own class runs: the row of the overrider's own name and
        // signature, null when it has none (inline in a header, implicit).
        const LumaFunction* apiOverrider(const LumaMod* pMod, const void* pObject, const char* pName, const char* pSignature) {
            const MethodRow* const pVirtual = findMethodRow(pName, pSignature);
            OwnClass own;
            if (pVirtual == nullptr || readObject(pMod, pObject, "overrider", &own) != 0) {
                return nullptr;
            }
            bool ambiguous = false;
            const MethodRow* const pRow = overrider(own, pObject, pVirtual, &ambiguous);
            if (ambiguous) {
                reportAmbiguous(pMod, "overrider", findType(pVirtual->record));
            }
            return pRow != nullptr ? asPublic(findFunction(pRow->function->name, pRow->function->signature, nullptr)) : nullptr;
        }

        // method() with the object adjusted to the declaring class: 0 called; -1 no such virtual; -2 not of that class
        // (no row, or ambiguous: reported); -3 not readable or null (reported).
        s32 apiCallMethod(const LumaMod* pMod, const void* pObject, const char* pName, const char* pSignature, void** ppArgs,
                          void* pResult) {
            const MethodRow* const pVirtual = findMethodRow(pName, pSignature);
            // Null args: only for a virtual with no parameters ("()" or "() const"); args[0] is then the kernel's own.
            void* pOwnArgs[1] = {nullptr};
            const bool noParams = pVirtual != nullptr && pVirtual->params[0] == '(' && pVirtual->params[1] == ')';
            if (pVirtual == nullptr || (ppArgs == nullptr && !noParams)) {
                return -1;
            }
            if (ppArgs == nullptr) {
                ppArgs = pOwnArgs;
            }
            OwnClass own;
            const s32 state = readObject(pMod, pObject, "call_method", &own);
            if (state == -1) {
                const char* const args[1] = {"call_method"};
                apiReport(pMod, "call_method", 0, kCodeObjectNotReadable, args, 1);
                return -3;
            }
            if (state != 0) {
                return state;
            }
            const TypeRow* const pDeclaring = findType(pVirtual->record);
            bool ambiguous = false;
            const void* const pSubobject = asClass(own, pDeclaring, &ambiguous);
            if (ambiguous) {
                reportAmbiguous(pMod, "call_method", pDeclaring);
            }
            if (pSubobject == nullptr) {
                return -2;
            }
            ppArgs[0] = const_cast<void*>(pSubobject);
            pVirtual->function->invoke(ppArgs, pResult);
            return 0;
        }

        bool grants(const LumaMod* pMod, const char* pPermission) {
            for (u32 i = 0; i < pMod->grantedCount; i++) {
                if (sameText(pMod->granted[i], pPermission)) {
                    return true;
                }
            }
            return false;
        }

        // luma.h's types: uint64_t is unsigned long on LP64 Android and Linux, which u64 (unsigned long long) is not.
        void* apiAlloc(const LumaMod* pConstMod, uint64_t size, uint64_t align) {
            LumaMod* const pMod = const_cast<LumaMod*>(pConstMod);
            if (pMod == nullptr) {
                return nullptr;
            }
            // Arguments no mod means to give: said, never silent (LR-5). Alignments up to 65536 (C++'s alignas, D2-7).
            if (size == 0 || align == 0 || (align & (align - 1)) != 0 || align > 65536u || size > (static_cast<u64>(1) << 40)) {
                nebula_log("[mod %s] alloc(%llu, %llu): the size must be 1 to 2^40 and the alignment a power of two up to 65536; "
                           "it gave nothing\n",
                           pMod->name, static_cast<unsigned long long>(size), static_cast<unsigned long long>(align));
                return nullptr;
            }
            const u64 total = sizeof(Block) + size + align;
            void* const pStart = ::malloc(static_cast<size_t>(total));
            if (pStart == nullptr) {
                return nullptr;
            }
            // The bytes start at the first `align` boundary with room for the block before them.
            const uintptr_t at = (reinterpret_cast<uintptr_t>(pStart) + sizeof(Block) + align - 1) & ~static_cast<uintptr_t>(align - 1);
            Block* const pBlock = reinterpret_cast<Block*>(at) - 1;
            pBlock->start = pStart;
            pBlock->size = size;
            pBlock->owner = pMod;
            pBlock->check = reinterpret_cast<uintptr_t>(pBlock) ^ kBlockCheck;
            lockMod(pMod);
            const bool kept = pMod->blocks.add(at);
            if (kept) {
                pMod->bytes += size;
                pMod->blockCount++;
            }
            unlockMod(pMod);
            if (!kept) {
                ::free(pStart);
                return nullptr;
            }
            return reinterpret_cast<void*>(at);
        }

        void apiFree(const LumaMod* pConstMod, void* p) {
            LumaMod* const pMod = const_cast<LumaMod*>(pConstMod);
            if (pMod == nullptr || p == nullptr) {
                return;
            }
            // Nothing is read through `p` until the mod's own set has it (D2-3).
            lockMod(pMod);
            const bool held = pMod->blocks.remove(reinterpret_cast<uintptr_t>(p));
            Block* const pBlock = static_cast<Block*>(p) - 1;
            if (held && (pBlock->check != (reinterpret_cast<uintptr_t>(pBlock) ^ kBlockCheck) || pBlock->owner != pMod)) {
                unlockMod(pMod);
                NEBULA_PANIC("LumaCore: %s's block at %p was written over in front of its bytes", pMod->name, p);
            }
            if (held) {
                pMod->bytes -= pBlock->size;
                pMod->blockCount--;
                pBlock->check = 0;
            }
            unlockMod(pMod);
            if (!held) {
                nebula_log("[mod %s] free(%p): not a block alloc gave this mod (another mod's, freed already, or never "
                           "from alloc); nothing was freed\n",
                           pMod->name, p);
                return;
            }
            ::free(pBlock->start);
        }

        // What freeAll found and freed.
        struct Held {
            u32 blocks;
            u64 bytes;
        };

        // Everything a mod still holds, after its grace, counted (reload.md 3): every block is checked and counted
        // before any is freed, and the count must be the mod's own; a broken set would free the wrong memory.
        Held freeAll(LumaMod* pMod) {
            BlockSet& rSet = pMod->blocks;
            Held held = {0, 0};
            for (u32 i = 0; i < rSet.capacity; i++) {
                if (rSet.slots[i] > 1) {
                    const Block* const pBlock = reinterpret_cast<const Block*>(rSet.slots[i]) - 1;
                    if (pBlock->check != (reinterpret_cast<uintptr_t>(pBlock) ^ kBlockCheck) || pBlock->owner != pMod) {
                        NEBULA_PANIC("LumaCore: %s's block at %p was written over in front of its bytes", pMod->name,
                                     reinterpret_cast<void*>(rSet.slots[i]));
                    }
                    held.blocks++;
                    held.bytes += pBlock->size;
                }
            }
            if (held.blocks != pMod->blockCount || held.bytes != pMod->bytes) {
                NEBULA_PANIC("LumaCore: %s's memory count is wrong: %u blocks and %llu bytes kept, %u and %llu found", pMod->name,
                             pMod->blockCount, static_cast<unsigned long long>(pMod->bytes), held.blocks,
                             static_cast<unsigned long long>(held.bytes));
            }
            for (u32 i = 0; i < rSet.capacity; i++) {
                if (rSet.slots[i] > 1) {
                    Block* const pBlock = reinterpret_cast<Block*>(rSet.slots[i]) - 1;
                    pBlock->check = 0;
                    ::free(pBlock->start);
                }
            }
            ::free(rSet.slots);
            rSet = BlockSet{};
            pMod->bytes = 0;
            pMod->blockCount = 0;
            return held;
        }

        // The kept entry for a mod id, or null.
        Kept* keptOf(const char* pId) {
            for (u32 i = 0; i < cMaxMods; i++) {
                if (sKept[i].id[0] != 0 && sameText(sKept[i].id, pId)) {
                    return &sKept[i];
                }
            }
            return nullptr;
        }

        void dropKept(Kept* pEntry) {
            ::free(pEntry->pBytes);
            pEntry->pBytes = nullptr;
            pEntry->size = 0;
            pEntry->read = false;
            pEntry->id[0] = 0;
        }

        // In stop only (reload.md 4): a copy of the bytes for the mod's next load this session; the last call wins.
        int32_t apiKeep(const LumaMod* pConstMod, const void* pBytes, uint32_t size) {
            if (!onGameThread()) {
                return -3;
            }
            if (pConstMod == nullptr || !pConstMod->inStop || size > cKeepMost || (pBytes == nullptr && size != 0)) {
                return -1;
            }
            Kept* pEntry = keptOf(pConstMod->name);
            if (pEntry == nullptr) {
                for (u32 i = 0; i < cMaxMods && pEntry == nullptr; i++) {
                    if (sKept[i].id[0] == 0) {
                        pEntry = &sKept[i];
                    }
                }
                if (pEntry == nullptr) {
                    // Every entry is a mod's that kept bytes and was not loaded again; said, never silent.
                    nebula_log("[mod %s] keep: the game already holds kept bytes for %u mods; nothing was kept\n", pConstMod->name, cMaxMods);
                    return -1;
                }
            }
            unsigned char* pCopy = nullptr;
            if (size != 0) {
                pCopy = static_cast<unsigned char*>(::malloc(size));
                if (pCopy == nullptr) {
                    nebula_log("[mod %s] keep: no memory for %u bytes; nothing was kept\n", pConstMod->name, size);
                    return -1;
                }
                const unsigned char* const pFrom = static_cast<const unsigned char*>(pBytes);
                for (u32 i = 0; i < size; i++) {
                    pCopy[i] = pFrom[i];
                }
            }
            dropKept(pEntry);
            if (size == 0) {
                return 0;  // kept as nothing: the next start's kept gives null
            }
            copyText(pEntry->id, sizeof(pEntry->id), pConstMod->name);
            pEntry->pBytes = pCopy;
            pEntry->size = size;
            return 0;
        }

        // In start only: what the mod's previous load kept, valid until start returns.
        const void* apiKept(const LumaMod* pConstMod, uint32_t* pSize) {
            if (pSize != nullptr) {
                *pSize = 0;
            }
            if (pConstMod == nullptr || !pConstMod->inStart || !onGameThread()) {
                return nullptr;
            }
            Kept* const pEntry = keptOf(pConstMod->name);
            if (pEntry == nullptr) {
                return nullptr;
            }
            pEntry->read = true;
            if (pSize != nullptr) {
                *pSize = pEntry->size;
            }
            return pEntry->pBytes;
        }

        const void* apiService(const LumaMod* pConstMod, const char* pName, u32 major, u32 minor) {
            const char* pPermission = nullptr;
            const void* const pTable = findService(pName, major, minor, &pPermission);
            if (pTable == nullptr || pPermission == nullptr) {
                return pTable;
            }
            LumaMod* const pMod = const_cast<LumaMod*>(pConstMod);
            if (pMod == nullptr) {
                nebula_log("LumaCore: service(%s) without its mod: refused, as the permission %s cannot be checked\n", pName, pPermission);
                return nullptr;
            }
            if (!grants(pMod, pPermission)) {
                const char* const args[2] = {pName, pPermission};
                record(pMod, pName, 0, kCodeNotGranted, args, 2);
                return nullptr;
            }
            return pTable;
        }

        // A hook's result that the kernel had to mend, said through report (D3-1): once per (function, mod), then counted.
        void mended(const Function* pFunction, const void* pOwner, u32 code) {
            if (pOwner == nullptr) {
                nebula_log("LumaCore: a hook with no mod on %s %s (code %u)\n", pFunction->name, pFunction->signature, code);
                return;
            }
            char found[256];
            nebula_snprintf(found, sizeof(found), "%s %s", pFunction->name, pFunction->signature);
            const char* const args[1] = {found};
            record(const_cast<LumaMod*>(static_cast<const LumaMod*>(pOwner)), found, 0, code, args, 1);
        }

        void skipRefused(const Function* pFunction, const void* pOwner) { mended(pFunction, pOwner, kCodeSkipRefused); }

        void noResult(const Function* pFunction, const void* pOwner) { mended(pFunction, pOwner, kCodeNoResult); }

        // Classes (classes contract, plan 1.4).
        s32 apiDefineClass(const LumaMod* pMod, const LumaClassDef* pDef) { return luma::classes::define(pMod, pDef); }

        void* apiClassData(const LumaMod*, const void* pObject, s32 number) { return luma::classes::data(pObject, number); }

        // Numbers (numbers contract, plan 1.5).
        void* apiNumber(const LumaMod* pMod, const char* pName, const char* pSignature, u32 place, u32* pKind) {
            return luma::numbers::find(pMod, pName, pSignature, place, pKind);
        }

        s32 apiSetNumber(const LumaMod* pMod, const char* pName, const char* pSignature, u32 place, f64 value) {
            return luma::numbers::set(pMod, pName, pSignature, place, value);
        }

        s32 apiTable(const LumaMod* pMod, const char* pName, LumaTable* pOut) {
            return luma::numbers::table(pMod, pName, pOut);
        }

        s32 apiSetTable(const LumaMod* pMod, const char* pName, u32 offset, const void* pBytes, u32 size) {
            return luma::numbers::setTable(pMod, pName, offset, pBytes, size);
        }

        // Objects made by name (classes contract, "make").
        s32 apiMake(const LumaMod* pMod, const char* pName, u32 count, const LumaMakePlace* pPlace, u32 flags, void** ppObjects) {
            return luma::make::make(pMod, pName, count, pPlace, flags, ppObjects);
        }

        const LumaCore sCore = {
            sizeof(LumaCore), LUMA_API, apiLog, apiFunction, apiFunctionAt, apiHook, apiProceed, apiField, apiSizeOf, apiClassOf, apiIsA, apiService,
            apiGameThread, apiUnhook, apiAlloc, apiFree, apiReport, apiOwnClass, apiAsClass, apiMethod, apiOverrider, apiCallMethod,
            apiKeep, apiKept, apiDefineClass, apiClassData, apiNumber, apiSetNumber, apiTable, apiSetTable, apiMake,
        };

        // One import: its function (functions and hooks) or number (fields, sizes, classes). False: the game has none,
        // or (a field) not of the type the import names.
        bool resolve(const LumaImport& rImport, const Function** ppFunction, s32* pNumber) {
            *ppFunction = nullptr;
            *pNumber = -1;
            switch (rImport.kind) {
            case LUMA_IMPORT_FUNCTION:
            case LUMA_IMPORT_HOOK:
                *ppFunction = findFunction(rImport.name, rImport.signature, nullptr);
                return *ppFunction != nullptr;
            case LUMA_IMPORT_METHOD:
                *ppFunction = findMethod(rImport.name, rImport.signature);
                return *ppFunction != nullptr;
            case LUMA_IMPORT_FIELD: {
                const FieldBind bind = findField(rImport.name, rImport.signature);
                if (!bind.found || (rImport.signature != nullptr && !bind.sameType)) {
                    return false;
                }
                *pNumber = bind.value;
                return true;
            }
            case LUMA_IMPORT_SIZE:
                *pNumber = apiSizeOf(rImport.name);
                return *pNumber >= 0;
            case LUMA_IMPORT_SERVICE:
            case LUMA_IMPORT_GLOBAL:
                return false;  // their own passes
            default:
                *pNumber = apiClassOf(rImport.name);
                return *pNumber >= 0;
            }
        }

        // The validator's phrase for a name the game lacks (L505's `kind`).
        const char* missingKind(const LumaImport& rImport) {
            switch (rImport.kind) {
            case LUMA_IMPORT_FUNCTION:
            case LUMA_IMPORT_HOOK:
                return rImport.signature != nullptr ? "Name.FunctionSig" : "Name.Function";
            case LUMA_IMPORT_FIELD:
                return rImport.signature != nullptr ? "Name.FieldSig" : "Name.Field";
            case LUMA_IMPORT_SERVICE:
                return "Name.Service";
            case LUMA_IMPORT_MOD_FUNCTION:
                return "Name.ModFunction";
            case LUMA_IMPORT_GLOBAL:
                return rImport.signature != nullptr ? "Name.GlobalSig" : "Name.Global";
            case LUMA_IMPORT_SIZE:
                return "Name.Type";
            case LUMA_IMPORT_METHOD:
                return "Name.Method";
            default:
                return "Name.Class";
            }
        }

        // L505's `has` (validator revision 5): the signatures of `count` rows from `first` (by signature in byte order,
        // as their table keeps them), each quoted, joined by ", ". The caller frees it.
        char* signatureList(const Function* (*pAt)(u32), u32 first, u32 count) {
            size_t length = 1;
            for (u32 i = 0; i < count; i++) {
                for (const char* p = pAt(first + i)->signature; *p != 0; p++) {
                    length++;
                }
                length += 4;  // two quotation marks, ", "
            }
            char* const pText = static_cast<char*>(::malloc(length));
            if (pText == nullptr) {
                NEBULA_PANIC("LumaCore: no memory to list a name's signatures");
            }
            size_t at = 0;
            const char* pLast = nullptr;
            for (u32 i = 0; i < count; i++) {
                const char* const pSignature = pAt(first + i)->signature;
                if (pLast != nullptr && sameText(pLast, pSignature)) {
                    continue;
                }
                if (pLast != nullptr) {
                    pText[at++] = ',';
                    pText[at++] = ' ';
                }
                pText[at++] = '"';
                for (const char* p = pSignature; *p != 0; p++) {
                    pText[at++] = *p;
                }
                pText[at++] = '"';
                pLast = pSignature;
            }
            pText[at] = 0;
            return pText;
        }

        // The reason a name is unbound, after kind, found and sig (contract types TY-7, TY2-1): its id, and the names
        // it holds when it holds any.
        void putWhy(Refuse& rRefuse, const char* pWhy, const char* pWhat) {
            rRefuse.put("why", 'P', pWhy);
            if (pWhat != nullptr) {
                rRefuse.put("what", 'T', pWhat);
            }
        }

        bool covers(const LumaModInfo* pInfo, size_t end) { return pInfo->size >= end; }

        // pB is the first `length` bytes of pA, whole.
        bool sameUpTo(const char* pA, u32 length, const char* pB) {
            for (u32 i = 0; i < length; i++) {
                if (pA[i] != pB[i] || pB[i] == 0) {
                    return false;
                }
            }
            return pB[length] == 0;
        }

        // A loaded mod by id; null when none is loaded.
        LumaMod* loadedMod(const char* pId, u32 length) {
            for (u32 i = 0; i < sModCount; i++) {
                if (sameUpTo(pId, length, sMods[i]->name)) {
                    return sMods[i];
                }
            }
            return nullptr;
        }

        // A kind 6 import's mod and export (D3-3): "<mod id>/<export name>". The export by name; *pSameSig whether its
        // signature is the import's. Null: no such mod loaded, or it exports no such name.
        const LumaExport* modExport(const LumaImport& rImport, LumaMod** ppOwner, bool* pSameSig) {
            *ppOwner = nullptr;
            *pSameSig = false;
            u32 slash = 0;
            while (rImport.name[slash] != 0 && rImport.name[slash] != '/') {
                slash++;
            }
            if (rImport.name[slash] != '/' || slash == 0) {
                return nullptr;
            }
            LumaMod* const pOwner = loadedMod(rImport.name, slash);
            if (pOwner == nullptr) {
                return nullptr;
            }
            const LumaModInfo* const pInfo = pOwner->info;
            if (!covers(pInfo, offsetof(LumaModInfo, export_count) + sizeof(pInfo->export_count)) || pInfo->exports == nullptr) {
                return nullptr;
            }
            const char* const pName = rImport.name + slash + 1;
            for (u32 i = 0; i < pInfo->export_count; i++) {
                const LumaExport& rExport = pInfo->exports[i];
                if (rExport.name != nullptr && sameText(pName, rExport.name)) {
                    *ppOwner = pOwner;
                    *pSameSig = rExport.signature != nullptr && rImport.signature != nullptr && sameText(rImport.signature, rExport.signature);
                    return &rExport;
                }
            }
            return nullptr;
        }

        // The mods `pMod` imported from are told it is going (D3-3, R-3): after its stop, before its grace, the last
        // bound first, on the game thread.
        void tellDependentGone(LumaMod* pMod) {
            for (u32 i = pMod->useCount; i > 0; i--) {
                LumaMod* const pOwner = pMod->uses[i - 1];
                const LumaModInfo* const pInfo = pOwner->info;
                if (covers(pInfo, offsetof(LumaModInfo, dependent_gone) + sizeof(pInfo->dependent_gone)) && pInfo->dependent_gone != nullptr) {
                    pInfo->dependent_gone(pMod);
                }
            }
        }

        // A service import's version (services-registry 2, validator revision 5): "<major>.<minor>", each a decimal
        // number from 0 to 65535 with no leading zero, nothing else; false when it is not that.
        bool versionOf(const char* pText, u32* pMajor, u32* pMinor) {
            u32 parts[2] = {0, 0};
            u32 digits[2] = {0, 0};
            u32 part = 0;
            for (const char* p = pText; p != nullptr && *p != 0; p++) {
                if (*p >= '0' && *p <= '9') {
                    if (digits[part] == 1 && parts[part] == 0) {
                        return false;  // a leading zero
                    }
                    parts[part] = parts[part] * 10u + static_cast<u32>(*p - '0');
                    digits[part]++;
                    if (parts[part] > 65535u) {
                        return false;
                    }
                } else if (*p == '.' && part == 0 && digits[0] != 0) {
                    part = 1;
                } else {
                    return false;
                }
            }
            *pMajor = parts[0];
            *pMinor = parts[1];
            return part == 1 && digits[1] != 0;
        }

        // A service name (services-registry 2): parts [a-z][a-z0-9]* joined by '.', at most 31 bytes.
        bool serviceNameOk(const char* pName) {
            u32 length = 0;
            bool partStart = true;
            for (const char* p = pName; *p != 0; p++, length++) {
                const char c = *p;
                if (length == 31) {
                    return false;
                }
                if (c == '.') {
                    if (partStart) {
                        return false;  // an empty part
                    }
                    partStart = true;
                } else if (c >= 'a' && c <= 'z') {
                    partStart = false;
                } else if (c < '0' || c > '9' || partStart) {
                    return false;  // not a letter or digit, or a part that does not start with a letter
                }
            }
            return length != 0 && !partStart;
        }

        // After the grace: the mod's destructors, its memory, its library. A mod taken out or ended after a refused
        // start says what it still held, and the harness gets takeout.<id> once its library is closed (reload.md 3).
        void finishMod(LumaMod* pMod) {
            const LumaModInfo* const pInfo = pMod->info;
            const bool finished = pMod->begun && covers(pInfo, offsetof(LumaModInfo, finish) + sizeof(pInfo->finish)) && pInfo->finish != nullptr;
            if (finished) {
                pInfo->finish();
            }
            luma::services::modEnd(pMod);
            const Held held = freeAll(pMod);
            if (pMod->library != nullptr && pMod->close != nullptr) {
                pMod->close(pMod->library);
            }
            if (pMod->takenOut || pMod->startRefused) {
                if (held.blocks != 0) {
                    nebula_log("[mod %s] held %u blocks (%llu bytes) when it ended; the game freed them\n", pMod->name, held.blocks,
                               static_cast<unsigned long long>(held.bytes));
                }
                char key[cIdBytes + 16];
                nebula_snprintf(key, sizeof(key), "takeout.%s", pMod->name);
                char value[128];
                nebula_snprintf(value, sizeof(value), "{\"blocks\": %u, \"bytes\": %llu, \"stopped\": %s, \"finished\": %s}", held.blocks,
                                static_cast<unsigned long long>(held.bytes), pMod->stopped ? "true" : "false", finished ? "true" : "false");
                luma::modlog::fact(key, value);
            }
            for (u32 i = 0; i < pMod->reportCount; i++) {
                dropReport(&pMod->reports[i]);
            }
            ::free(pMod->reports);
            for (u32 i = 0; i < pMod->grantedCount; i++) {
                ::free(pMod->granted[i]);
            }
            ::free(pMod->granted);
            ::free(pMod->uses);
            delete pMod;
        }

        // A mod whose code may still run on another thread waits for the grace; one whose code cannot ends now.
        void endMod(LumaMod* pMod) {
            if (!pMod->published) {
                finishMod(pMod);
                return;
            }
            pMod->grace = graceMark();
            pMod->nextGone = sGone;
            sGone = pMod;
        }

        bool wraps(u32 kind) { return kind == LUMA_WRAP || kind == LUMA_REPLACE; }
    }

    const LumaCore* core() { return &sCore; }

    void frame() {
        reclaim(sGone != nullptr);
        for (LumaMod** ppAt = &sGone; *ppAt != nullptr;) {
            LumaMod* const pMod = *ppAt;
            if (graceOver(pMod->grace)) {
                *ppAt = pMod->nextGone;
                finishMod(pMod);
            } else {
                ppAt = &pMod->nextGone;
            }
        }
    }

    bool loadMod(const LumaModInfo* pInfo, const char* pId, const char* pFile, s32 order, void* pLibrary, LumaCloseLibrary close,
                 const char* const* pGranted, u32 grantedCount, LumaRefusal* pRefusal) {
        if (pRefusal != nullptr) {
            pRefusal->code = 0;
            pRefusal->count = 0;
        }
        if (!onGameThread()) {
            NEBULA_PANIC("a LumaCore mod was loaded off the game thread");
        }
        // The type rows are sorted here, before any mod's code runs, so later lookups from any thread only read.
        prepareTypes();
        gSkipRefused = skipRefused;
        gNoResult = noResult;
        LumaMod* const pMod = new LumaMod();
        copyText(pMod->name, sizeof(pMod->name), pId != nullptr ? pId : "");
        copyText(pMod->file, sizeof(pMod->file), pFile != nullptr ? pFile : "");
        pMod->info = pInfo;
        pMod->order = order;
        pMod->library = pLibrary;
        pMod->close = close;
        if (pGranted != nullptr && grantedCount != 0) {
            pMod->granted = static_cast<char**>(zeroed(grantedCount, sizeof(char*)));
            if (pMod->granted == nullptr) {
                NEBULA_PANIC("LumaCore: no memory to keep %s's permissions", pMod->name);
            }
            for (u32 i = 0; i < grantedCount; i++) {
                pMod->granted[i] = keepText(pGranted[i]);
            }
            pMod->grantedCount = grantedCount;
        }
        Refuse refuse = {pRefusal, pId, pFile};

        // Each refusal is the first one found, with the validator's code and parameters (loader contract 2, 3).
        bool refused = true;
        if (pInfo == nullptr) {
            refuse.start(512);
            refuse.put("why", 'P', "Lib.InfoUnreadable");
        } else if (pInfo->api == 0) {
            refuse.start(512);
            refuse.put("why", 'P', "Lib.NotLumaH");
        } else if (pInfo->api > LUMA_API) {
            refuse.start(508);
            refuse.number("found", pInfo->api);
            refuse.number("expected", LUMA_API);
        } else if (pInfo->size < cInfoLeast) {
            refuse.start(512);
            refuse.put("why", 'P', "Lib.InfoTooSmall");
        } else if (pInfo->import_count != 0 && pInfo->imports == nullptr) {
            refuse.start(512);
            refuse.put("why", 'P', "Lib.ImportsUnreadable");
        } else if (sModCount == cMaxMods) {
            refuse.start(500);
            refuse.put("why", 'P', "Lib.TooManyMods");
            refuse.number("count", LUMA_MAX_MODS);
        } else {
            refused = false;
        }

        // Pass 1, every name; pass 2, every hook as `hook` would take it. Nothing is bound yet (LR-2).
        for (u32 i = 0; !refused && i < pInfo->import_count; i++) {
            const LumaImport& rImport = pInfo->imports[i];
            if (rImport.name == nullptr || rImport.name[0] == 0 || rImport.kind > LUMA_IMPORT_METHOD ||
                (rImport.kind == LUMA_IMPORT_HOOK && (rImport.hook == nullptr || rImport.hook_kind >= Hook_KindCount))) {
                refuse.start(512);
                refuse.put("why", 'P', "Lib.ImportRowBad");
                refuse.number("row", i);
                refused = true;
                break;
            }
            // A service (D3-2): absent or older is L505 (Name.Service); not granted is L506.
            if (rImport.kind == LUMA_IMPORT_SERVICE) {
                u32 major = 0;
                u32 minor = 0;
                // Registry 2's grammar, whatever the flags: as the validator says it (L505, `sig` as found).
                if (!serviceNameOk(rImport.name) || !versionOf(rImport.signature, &major, &minor)) {
                    refuse.start(505);
                    refuse.put("kind", 'P', "Name.Service");
                    refuse.put("found", 'T', rImport.name);
                    refuse.put("sig", 'T', rImport.signature != nullptr ? rImport.signature : "");
                    refused = true;
                    break;
                }
                const char* pPermission = nullptr;
                const void* const pTable = findService(rImport.name, major, minor, &pPermission);
                if ((rImport.flags & LUMA_OPTIONAL) != 0) {
                    continue;
                }
                if (pTable == nullptr) {
                    refuse.start(505);
                    refuse.put("kind", 'P', "Name.Service");
                    refuse.put("found", 'T', rImport.name);
                    refuse.put("sig", 'T', rImport.signature);
                    refused = true;
                    break;
                }
                if (pPermission != nullptr && !grants(pMod, pPermission)) {
                    refuse.start(506);
                    refuse.put("service", 'T', rImport.name);
                    refuse.put("permission", 'T', pPermission);
                    refused = true;
                    break;
                }
                continue;
            }
            // Another mod's function (D3-3): that mod loaded (its needs come first), the export by name, then by
            // signature; L505 Name.ModFunction or Name.FunctionSig.
            if (rImport.kind == LUMA_IMPORT_MOD_FUNCTION) {
                LumaMod* pOwner = nullptr;
                bool sameSig = false;
                const LumaExport* const pExport = modExport(rImport, &pOwner, &sameSig);
                if ((pExport != nullptr && sameSig) || (rImport.flags & LUMA_OPTIONAL) != 0) {
                    continue;
                }
                refuse.start(505);
                refuse.put("kind", 'P', pExport == nullptr ? "Name.ModFunction" : "Name.FunctionSig");
                refuse.put("found", 'T', rImport.name);
                refuse.put("sig", 'T', rImport.signature != nullptr ? rImport.signature : "");
                refused = true;
                break;
            }
            // A global (types 1.2): by name, then its type text. L505 as the validator gives it: one the generator
            // left out is Name.GlobalUnbound with its reason; else Name.Global, or Name.GlobalSig with a signature.
            if (rImport.kind == LUMA_IMPORT_GLOBAL) {
                const GlobalRow* const pGlobal = findGlobal(rImport.name);
                const bool same = pGlobal != nullptr && (rImport.signature == nullptr || sameText(rImport.signature, pGlobal->type));
                if (same || (rImport.flags & LUMA_OPTIONAL) != 0) {
                    continue;
                }
                const LeftRow* const pLeft = pGlobal == nullptr ? leftOut(rImport.kind, rImport.name) : nullptr;
                refuse.start(505);
                refuse.put("kind", 'P', pLeft != nullptr ? "Name.GlobalUnbound" : missingKind(rImport));
                refuse.put("found", 'T', rImport.name);
                refuse.put("sig", 'T', rImport.signature != nullptr ? rImport.signature : "");
                if (pLeft != nullptr) {
                    putWhy(refuse, pLeft->why, pLeft->what);
                }
                refused = true;
                break;
            }
            const Function* pFunction = nullptr;
            s32 number = -1;
            if (!resolve(rImport, &pFunction, &number)) {
                if ((rImport.flags & LUMA_OPTIONAL) != 0) {
                    continue;
                }
                refuse.start(505);
                if (rImport.kind == LUMA_IMPORT_FIELD) {
                    // One of another type is Name.FieldSig (Z-4). A name some base has twice, or through a virtual
                    // base, is Name.FieldUnbound with the kernel's own reason and names (TY2-1; the build's comparison
                    // proves them the generator's); any other name the generator left out, with its reason. Else
                    // Name.Field, or Name.FieldSig with a signature.
                    const FieldBind bind = findField(rImport.name, rImport.signature);
                    const LeftRow* const pLeft = !bind.found && bind.why == FieldBind::kNone ? leftOut(rImport.kind, rImport.name) : nullptr;
                    refuse.put("kind", 'P', bind.found ? "Name.FieldSig"
                                            : bind.why != FieldBind::kNone || pLeft != nullptr ? "Name.FieldUnbound"
                                                                                               : missingKind(rImport));
                    refuse.put("found", 'T', rImport.name);
                    refuse.put("sig", 'T', rImport.signature != nullptr ? rImport.signature : "");
                    if (bind.why != FieldBind::kNone) {
                        putWhy(refuse, bind.why == FieldBind::kAmbiguous ? "LeftOut.Ambiguous" : "LeftOut.VirtualBase", bind.whyNames);
                    } else if (pLeft != nullptr) {
                        putWhy(refuse, pLeft->why, pLeft->what);
                    }
                } else if (rImport.kind == LUMA_IMPORT_SIZE) {
                    // A record by its name or an alias's: Name.Type, or Name.TypeUnbound with the generator's reason.
                    const LeftRow* const pLeft = leftOut(rImport.kind, rImport.name);
                    refuse.put("kind", 'P', pLeft != nullptr ? "Name.TypeUnbound" : "Name.Type");
                    refuse.put("found", 'T', rImport.name);
                    refuse.put("sig", 'T', rImport.signature);
                    if (pLeft != nullptr) {
                        putWhy(refuse, pLeft->why, pLeft->what);
                    }
                } else if (rImport.kind == LUMA_IMPORT_FUNCTION || rImport.kind == LUMA_IMPORT_HOOK || rImport.kind == LUMA_IMPORT_METHOD) {
                    // The name is the game's under other signatures (or several, none asked): Name.FunctionSigHas,
                    // Name.FunctionMany or Name.MethodHas with `has` listing them (validator revision 5). Else
                    // Name.Function, Name.FunctionSig or Name.Method.
                    const bool method = rImport.kind == LUMA_IMPORT_METHOD;
                    u32 first = 0;
                    const u32 count = method ? methodsNamed(rImport.name, &first) : functionsNamed(rImport.name, &first);
                    refuse.put("kind", 'P', count == 0                     ? missingKind(rImport)
                                            : method                       ? "Name.MethodHas"
                                            : rImport.signature != nullptr ? "Name.FunctionSigHas"
                                                                           : "Name.FunctionMany");
                    refuse.put("found", 'T', rImport.name);
                    refuse.put("sig", 'T', rImport.signature);
                    if (count != 0) {
                        char* const pHas = signatureList(method ? methodAt : functionAt, first, count);
                        refuse.put("has", 'T', pHas);
                        ::free(pHas);
                    }
                } else {
                    refuse.put("kind", 'P', missingKind(rImport));
                    refuse.put("found", 'T', rImport.name);
                    refuse.put("sig", 'T', rImport.signature);
                }
                refused = true;
                break;
            }
            if (rImport.kind == LUMA_IMPORT_FUNCTION && isTell(pFunction)) {
                // Contract varargs 3: a call goes through the row's invoke, which a constructor, a destructor or a
                // function with `...` has none of.
                refuse.start(512);
                refuse.put("why", 'P', "Lib.CallTell");
                refuse.put("found", 'T', rImport.name);
                refused = true;
                break;
            }
            if (rImport.kind == LUMA_IMPORT_HOOK && isUnhookable(pFunction)) {
                refuse.start(512);
                refuse.put("why", 'P', "Hook.Constexpr");
                refuse.put("found", 'T', rImport.name);
                refused = true;
                break;
            }
            if (rImport.kind == LUMA_IMPORT_HOOK && rImport.hook_kind != LUMA_BEFORE && isVariadic(pFunction)) {
                refuse.start(512);
                refuse.put("why", 'P', "Hook.Variadic");
                refuse.put("found", 'T', rImport.name);
                refused = true;
                break;
            }
            if (rImport.kind != LUMA_IMPORT_HOOK || !wraps(rImport.hook_kind)) {
                continue;
            }
            if (isTell(pFunction)) {
                refuse.start(512);
                refuse.put("why", 'P', "Lib.HookOnConstructor");
                refuse.put("found", 'T', rImport.name);
                refused = true;
            } else if (resultKind(pFunction) == kFixed) {
                refuse.start(512);
                refuse.put("why", 'P', "Hook.NoDefaultResult");
                refuse.put("found", 'T', rImport.name);
                refused = true;
            } else if (rImport.hook_kind == LUMA_REPLACE) {
                const void* const pHolder = replacedBy(pFunction);
                if (pHolder != nullptr) {
                    refuse.start(805);
                    refuse.put("found", 'T', rImport.name);
                    refuse.put("sig", 'T', pFunction->signature);
                    refuse.put("other", 'T', static_cast<const LumaMod*>(pHolder)->name);
                    refused = true;
                }
            }
        }
        if (refused) {
            // None of its code ran: the library closes now.
            finishMod(pMod);
            return false;
        }

        // Pass 3: the slots, then its begin and start; its rows' hooks are published only after start agrees.
        for (u32 i = 0; i < pInfo->import_count; i++) {
            const LumaImport& rImport = pInfo->imports[i];
            if (rImport.kind == LUMA_IMPORT_HOOK || rImport.slot == nullptr) {
                continue;
            }
            if (rImport.kind == LUMA_IMPORT_SERVICE) {
                // Pass 1 checked it; an optional one the player may not use, or the game lacks, stays null.
                u32 major = 0;
                u32 minor = 0;
                const char* pPermission = nullptr;
                const void* pTable = versionOf(rImport.signature, &major, &minor) ? findService(rImport.name, major, minor, &pPermission) : nullptr;
                if (pTable != nullptr && pPermission != nullptr && !grants(pMod, pPermission)) {
                    pTable = nullptr;
                }
                *static_cast<const void**>(rImport.slot) = pTable;
                continue;
            }
            if (rImport.kind == LUMA_IMPORT_MOD_FUNCTION) {
                // Pass 1 checked it; an optional one the needed mod lacks stays null. Its mod is kept, once, to be told.
                LumaMod* pOwner = nullptr;
                bool sameSig = false;
                const LumaExport* const pExport = modExport(rImport, &pOwner, &sameSig);
                const bool bound = pExport != nullptr && sameSig;
                *static_cast<const void**>(rImport.slot) = bound ? pExport->address : nullptr;
                if (bound) {
                    bool known = false;
                    for (u32 k = 0; k < pMod->useCount && !known; k++) {
                        known = pMod->uses[k] == pOwner;
                    }
                    if (!known) {
                        if (pMod->uses == nullptr) {
                            pMod->uses = static_cast<LumaMod**>(zeroed(pInfo->import_count, sizeof(LumaMod*)));
                            if (pMod->uses == nullptr) {
                                NEBULA_PANIC("LumaCore: no memory to keep which mods %s uses", pMod->name);
                            }
                        }
                        pMod->uses[pMod->useCount++] = pOwner;
                    }
                }
                continue;
            }
            if (rImport.kind == LUMA_IMPORT_GLOBAL) {
                // Pass 1 checked it; an optional one the game lacks, or of another type, stays null.
                const GlobalRow* const pGlobal = findGlobal(rImport.name);
                const bool same = pGlobal != nullptr && (rImport.signature == nullptr || sameText(rImport.signature, pGlobal->type));
                *static_cast<void**>(rImport.slot) = same ? pGlobal->address : nullptr;
                continue;
            }
            const Function* pFunction = nullptr;
            s32 number = -1;
            const bool found = resolve(rImport, &pFunction, &number);
            if (rImport.kind == LUMA_IMPORT_FUNCTION || rImport.kind == LUMA_IMPORT_METHOD) {
                *static_cast<const LumaFunction**>(rImport.slot) = asPublic(pFunction);
            } else {
                *static_cast<s32*>(rImport.slot) = found ? number : -1;
            }
        }
        luma::services::modBegin(pMod, pMod->name, order, pMod->granted, pMod->grantedCount);
        if (covers(pInfo, offsetof(LumaModInfo, begin) + sizeof(pInfo->begin)) && pInfo->begin != nullptr) {
            pMod->begun = true;
            pInfo->begin(&sCore, pMod);
        }
        if (pInfo->start != nullptr) {
            pMod->inStart = true;
            const int32_t answer = pInfo->start(&sCore, pMod);
            pMod->inStart = false;
            // Kept bytes it read are gone now (reload.md 4).
            if (Kept* const pKept = keptOf(pMod->name)) {
                if (pKept->read) {
                    dropKept(pKept);
                }
            }
            if (answer != 0) {
                pMod->startRefused = true;
                refuse.start(512);
                refuse.put("why", 'P', "Lib.StartRefused");
                refuse.number("found", answer);
                // Hooks its start added itself go; the mods it imported from are told; it ends after the grace when any
                // was published.
                removeHooks(pMod);
                luma::classes::modGone(pMod);
                luma::numbers::modGone(pMod);
                tellDependentGone(pMod);
                luma::services::modGone(pMod);
                endMod(pMod);
                return false;
            }
        }
        for (u32 i = 0; i < pInfo->import_count; i++) {
            const LumaImport& rImport = pInfo->imports[i];
            if (rImport.kind != LUMA_IMPORT_HOOK) {
                continue;
            }
            const Function* pFunction = nullptr;
            s32 number = -1;
            if (!resolve(rImport, &pFunction, &number)) {
                continue;  // optional (pass 1)
            }
            const HookResult result = addHook(pFunction, static_cast<HookKind>(rImport.hook_kind), reinterpret_cast<HookCallback>(rImport.hook),
                                              rImport.user, pMod, order);
            if (result != HookResult_Added) {
                // Pass 2 took every condition `hook` checks, on this thread, with no other mod between.
                NEBULA_PANIC("LumaCore: %s's hook on %s was refused after it was checked (%d)", pMod->name, rImport.name, static_cast<int>(result));
            }
            pMod->published = true;
        }
        pMod->startedAt = ++sStartedCount;
        sMods[sModCount++] = pMod;
        return true;
    }

    void unloadMod(const LumaModInfo* pInfo) {
        for (u32 i = 0; i < sModCount; i++) {
            if (sMods[i]->info != pInfo) {
                continue;
            }
            LumaMod* const pMod = sMods[i];
            // A mod another loaded mod imports from goes after it (D3-3: the host stops the dependents first); taken
            // out before, the other would call into a closed library.
            for (u32 j = 0; j < sModCount; j++) {
                for (u32 k = 0; k < sMods[j]->useCount; k++) {
                    if (sMods[j]->uses[k] == pMod) {
                        NEBULA_PANIC("LumaCore: %s was taken out while %s, which uses it, is on", pMod->name, sMods[j]->name);
                    }
                }
            }
            sMods[i] = sMods[--sModCount];
            // Unbind, its stop, the mods it imported from told, then after the grace its destructors, memory and
            // library (LR-5, D3-3).
            removeHooks(pMod);
            luma::classes::modGone(pMod);
            luma::numbers::modGone(pMod);
            if (pInfo->stop != nullptr) {
                pMod->inStop = true;
                pInfo->stop();
                pMod->inStop = false;
                pMod->stopped = true;
            }
            tellDependentGone(pMod);
            luma::services::modGone(pMod);
            endMod(pMod);
            frame();
            return;
        }
    }

    bool requestTakeOut(const char* pId) {
        u32 length = 0;
        while (pId != nullptr && length < cIdBytes && pId[length] != 0) {
            length++;
        }
        if (length == 0 || length >= cIdBytes) {
            nebula_log("LumaCore: a take-out request for %s, which is no mod's id; nothing was queued\n", pId == nullptr ? "no mod" : "an id over 63 bytes");
            return false;
        }
        lockTakes();
        const u32 count = __atomic_load_n(&sTakeCount, __ATOMIC_RELAXED);
        for (u32 i = 0; i < count; i++) {
            if (sameText(sTakeQueue[i], pId)) {
                unlockTakes();
                return true;  // one request
            }
        }
        if (count == cTakeQueue) {
            unlockTakes();
            nebula_log("[mod %s] take-out not queued: %u requests already wait for the next frame\n", pId, cTakeQueue);
            return false;
        }
        copyText(sTakeQueue[count], cIdBytes, pId);
        __atomic_store_n(&sTakeCount, count + 1u, __ATOMIC_RELEASE);
        unlockTakes();
        return true;
    }

    void takeOutRequested() {
        if (__atomic_load_n(&sTakeCount, __ATOMIC_ACQUIRE) == 0u) {
            return;
        }
        if (!onGameThread()) {
            NEBULA_PANIC("LumaCore: take-out requests were taken off the game thread");
        }
        char (&ids)[cTakeQueue][cIdBytes] = sTakeIds;
        lockTakes();
        const u32 count = __atomic_load_n(&sTakeCount, __ATOMIC_RELAXED);
        for (u32 i = 0; i < count; i++) {
            copyText(ids[i], cIdBytes, sTakeQueue[i]);
        }
        __atomic_store_n(&sTakeCount, 0u, __ATOMIC_RELEASE);
        unlockTakes();

        for (u32 r = 0; r < count; r++) {
            LumaMod* pMod = nullptr;
            for (u32 i = 0; i < sModCount && pMod == nullptr; i++) {
                if (sameText(sMods[i]->name, ids[r])) {
                    pMod = sMods[i];
                }
            }
            if (pMod == nullptr) {
                nebula_log("[mod %s] take-out asked while it is not running (refused, never started, or taken out already); nothing "
                           "was done\n",
                           ids[r]);
                continue;
            }
            // It and every mod that uses it, through `uses`, transitively (reload.md 2).
            LumaMod** const out = sTakeOut;
            u32 outCount = 0;
            out[outCount++] = pMod;
            for (bool grew = true; grew;) {
                grew = false;
                for (u32 j = 0; j < sModCount; j++) {
                    LumaMod* const pOther = sMods[j];
                    bool listed = false;
                    for (u32 k = 0; k < outCount && !listed; k++) {
                        listed = out[k] == pOther;
                    }
                    if (listed) {
                        continue;
                    }
                    bool uses = false;
                    for (u32 u = 0; u < pOther->useCount && !uses; u++) {
                        for (u32 k = 0; k < outCount && !uses; k++) {
                            uses = pOther->uses[u] == out[k];
                        }
                    }
                    if (uses) {
                        out[outCount++] = pOther;
                        grew = true;
                    }
                }
            }
            // The last started first: a mod starts after every mod it uses, so each goes before what it uses.
            for (u32 i = 1; i < outCount; i++) {
                LumaMod* const pAt = out[i];
                u32 j = i;
                for (; j > 0 && out[j - 1]->startedAt < pAt->startedAt; j--) {
                    out[j] = out[j - 1];
                }
                out[j] = pAt;
            }
            for (u32 i = 0; i < outCount; i++) {
                // unloadMod may end it at once (no hook of it was ever published): nothing of it is read after.
                char name[cIdBytes];
                copyText(name, sizeof(name), out[i]->name);
                const LumaModInfo* const pInfo = out[i]->info;
                out[i]->takenOut = true;
                nebula_log("[mod %s] taken out while the game runs%s\n", name, out[i] == pMod ? "" : " (it uses a mod taken out)");
                unloadMod(pInfo);
                if (sTakenOut != nullptr) {
                    sTakenOut(name);
                }
            }
        }
    }
}

extern "C" {
NEBULA_GAME_API int32_t luma_load_mod(const LumaModInfo* info, const char* mod_id, const char* file, int32_t order, void* library,
                                      LumaCloseLibrary close, const char* const* granted, uint32_t granted_count, LumaRefusal* refusal) {
    return luma::loadMod(info, mod_id, file, order, library, close, granted, granted_count, refusal) ? 1 : 0;
}

NEBULA_GAME_API void luma_unload_mod(const LumaModInfo* info) {
    luma::unloadMod(info);
}

NEBULA_GAME_API const LumaCore* luma_core(void) {
    return luma::core();
}

NEBULA_GAME_API void luma_set_service_finder(LumaServiceFinder finder) {
    luma::sServiceFinder = finder;
}

NEBULA_GAME_API int32_t luma_take_out_request(const char* mod_id) {
    return luma::requestTakeOut(mod_id) ? 1 : 0;
}

NEBULA_GAME_API void luma_set_taken_out(LumaTakenOut taken_out) {
    luma::sTakenOut = taken_out;
}
}
