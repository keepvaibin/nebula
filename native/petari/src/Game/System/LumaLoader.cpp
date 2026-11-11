#include "Game/System/LumaLoader.hpp"

#include "Game/NameObj/NameObj.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/LumaHook.hpp"
#include <luma_host.h>
#include <nebula_format.h>
#include <nebula_game_api.h>
#include <nebula_module.h>
#include <stddef.h>
#include <stdlib.h>

namespace luma {
    // One block a mod took with core->alloc, just before the bytes it gives. `free` reads it only after finding the
    // pointer in the mod's own set (D2-3); the check word is a second check.
    struct Block {
        void* start;  // what malloc gave
        u64 size;     // what the mod asked for
        const LumaMod* owner;
        uintptr_t check;  // the block's address mixed with kBlockCheck; 0 once freed
    };
    const uintptr_t kBlockCheck = static_cast<uintptr_t>(0x4C554D41424C4B21ull);

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
                uintptr_t* const pSlots = static_cast<uintptr_t*>(::calloc(grown, sizeof(uintptr_t)));
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
    bool begun;      // its begin ran (so finish runs)
    bool published;  // a hook of it was in a chain at some time (so its end waits for the grace)
    u32 grace;       // the grace mark of its end
    LumaMod* nextGone;
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

        // "Class.field" split at its first dot.
        bool splitField(const char* pName, char* pClass, u32 classSize, const char** ppField) {
            u32 i = 0;
            for (; pName[i] != 0 && pName[i] != '.'; i++) {
                if (i + 1 >= classSize) {
                    return false;
                }
                pClass[i] = pName[i];
            }
            pClass[i] = 0;
            if (pName[i] != '.' || i == 0 || pName[i + 1] == 0) {
                return false;
            }
            *ppField = pName + i + 1;
            return true;
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

        s32 apiClassOf(const char* pClass) { return pClass != nullptr ? lumacore::findClass(pClass) : -1; }

        s32 apiSizeOf(const char* pClass) {
            const s32 id = apiClassOf(pClass);
            return id < 0 ? -1 : static_cast<s32>(lumacore::classRow(static_cast<u16>(id)).size);
        }

        s32 apiField(const char* pClass, const char* pField, const char* /*pType*/) {
            const s32 id = apiClassOf(pClass);
            if (id < 0 || pField == nullptr) {
                return -1;
            }
            // A field of a base is found on the base it belongs to.
            for (u16 at = static_cast<u16>(id);; at = lumacore::classRow(at).base) {
                const lumacore::FieldRow* pRow = lumacore::findField(at, pField);
                if (pRow != nullptr) {
                    return static_cast<s32>(pRow->offset);
                }
                if (at == 0 || lumacore::classRow(at).base == at) {
                    return -1;
                }
            }
        }

        s32 apiIsA(const void* pObject, s32 classNumber) {
            if (pObject == nullptr || classNumber < 0) {
                return 0;
            }
            return lumacore::isA(lumacore::classOf(static_cast<const NameObj*>(pObject)), static_cast<u16>(classNumber)) ? 1 : 0;
        }

        // Services come from the host (luma_set_service_finder), which asks its own services and its extensions.
        LumaServiceFinder sServiceFinder = nullptr;

        const void* apiService(const char* pName, u32 major, u32 minor) {
            return pName != nullptr && sServiceFinder != nullptr ? sServiceFinder(pName, major, minor) : nullptr;
        }

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

        void* apiAlloc(const LumaMod* pConstMod, u64 size, u64 align) {
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

        // Everything a mod still holds, after its grace.
        void freeAll(LumaMod* pMod) {
            BlockSet& rSet = pMod->blocks;
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
        }

        // A hook's result that the kernel had to mend, said once per (what, function, mod): the first 256 triples;
        // later ones each time, so none is lost.
        struct Said {
            const Function* function;
            const void* owner;
            u32 what;
        };
        Said sSaid[256];
        u32 sSaidCount = 0;

        bool sayOnce(const Function* pFunction, const void* pOwner, u32 what) {
            for (u32 i = 0; i < sSaidCount; i++) {
                if (sSaid[i].function == pFunction && sSaid[i].owner == pOwner && sSaid[i].what == what) {
                    return false;
                }
            }
            if (sSaidCount < 256u) {
                sSaid[sSaidCount++] = {pFunction, pOwner, what};
            }
            return true;
        }

        const char* modName(const void* pOwner) { return pOwner != nullptr ? static_cast<const LumaMod*>(pOwner)->name : "?"; }

        void skipRefused(const Function* pFunction, const void* pOwner) {
            if (sayOnce(pFunction, pOwner, 0)) {
                nebula_log("[mod %s] a before hook skipped %s %s, whose result is built by the function itself; the function ran\n",
                           modName(pOwner), pFunction->name, pFunction->signature);
            }
        }

        void noResult(const Function* pFunction, const void* pOwner) {
            if (sayOnce(pFunction, pOwner, 1)) {
                nebula_log("[mod %s] a hook on %s %s gave no result, so the function ran\n", modName(pOwner), pFunction->name,
                           pFunction->signature);
            }
        }

        const LumaCore sCore = {
            sizeof(LumaCore), LUMA_API, apiLog, apiFunction, apiFunctionAt, apiHook, apiProceed, apiField, apiSizeOf, apiClassOf, apiIsA, apiService,
            apiGameThread, apiUnhook, apiAlloc, apiFree,
        };

        // One import: its function (functions and hooks) or number (fields, sizes, classes). False: the game has none.
        bool resolve(const LumaImport& rImport, const Function** ppFunction, s32* pNumber) {
            *ppFunction = nullptr;
            *pNumber = -1;
            switch (rImport.kind) {
            case LUMA_IMPORT_FUNCTION:
            case LUMA_IMPORT_HOOK:
                *ppFunction = findFunction(rImport.name, rImport.signature, nullptr);
                return *ppFunction != nullptr;
            case LUMA_IMPORT_FIELD: {
                char className[96];
                const char* pField = nullptr;
                if (!splitField(rImport.name, className, sizeof(className), &pField)) {
                    return false;
                }
                *pNumber = apiField(className, pField, rImport.signature);
                return *pNumber >= 0;
            }
            case LUMA_IMPORT_SIZE:
                *pNumber = apiSizeOf(rImport.name);
                return *pNumber >= 0;
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
            default:
                return "Name.Class";
            }
        }

        bool covers(const LumaModInfo* pInfo, size_t end) { return pInfo->size >= end; }

        // After the grace: the mod's destructors, its memory, its library.
        void finishMod(LumaMod* pMod) {
            const LumaModInfo* const pInfo = pMod->info;
            if (pMod->begun && covers(pInfo, offsetof(LumaModInfo, finish) + sizeof(pInfo->finish)) && pInfo->finish != nullptr) {
                pInfo->finish();
            }
            freeAll(pMod);
            if (pMod->library != nullptr && pMod->close != nullptr) {
                pMod->close(pMod->library);
            }
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
                 LumaRefusal* pRefusal) {
        if (pRefusal != nullptr) {
            pRefusal->code = 0;
            pRefusal->count = 0;
        }
        if (!onGameThread()) {
            NEBULA_PANIC("a LumaCore mod was loaded off the game thread");
        }
        gSkipRefused = skipRefused;
        gNoResult = noResult;
        LumaMod* const pMod = new LumaMod();
        copyText(pMod->name, sizeof(pMod->name), pId != nullptr ? pId : "");
        copyText(pMod->file, sizeof(pMod->file), pFile != nullptr ? pFile : "");
        pMod->info = pInfo;
        pMod->order = order;
        pMod->library = pLibrary;
        pMod->close = close;
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
            if (rImport.name == nullptr || rImport.kind > LUMA_IMPORT_HOOK ||
                (rImport.kind == LUMA_IMPORT_HOOK && (rImport.hook == nullptr || rImport.hook_kind >= Hook_KindCount))) {
                refuse.start(512);
                refuse.put("why", 'P', "Lib.ImportRowBad");
                refuse.number("row", i);
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
                refuse.put("kind", 'P', missingKind(rImport));
                refuse.put("found", 'T', rImport.name);
                refuse.put("sig", 'T', rImport.signature);
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
            const Function* pFunction = nullptr;
            s32 number = -1;
            const bool found = resolve(rImport, &pFunction, &number);
            if (rImport.kind == LUMA_IMPORT_FUNCTION) {
                *static_cast<const LumaFunction**>(rImport.slot) = asPublic(pFunction);
            } else {
                *static_cast<s32*>(rImport.slot) = found ? number : -1;
            }
        }
        if (covers(pInfo, offsetof(LumaModInfo, begin) + sizeof(pInfo->begin)) && pInfo->begin != nullptr) {
            pMod->begun = true;
            pInfo->begin(&sCore, pMod);
        }
        if (pInfo->start != nullptr) {
            const int32_t answer = pInfo->start(&sCore, pMod);
            if (answer != 0) {
                refuse.start(512);
                refuse.put("why", 'P', "Lib.StartRefused");
                refuse.number("found", answer);
                // Hooks its start added itself go; it ends after the grace when any was published.
                removeHooks(pMod);
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
        sMods[sModCount++] = pMod;
        return true;
    }

    void unloadMod(const LumaModInfo* pInfo) {
        for (u32 i = 0; i < sModCount; i++) {
            if (sMods[i]->info != pInfo) {
                continue;
            }
            LumaMod* const pMod = sMods[i];
            sMods[i] = sMods[--sModCount];
            // Unbind, its stop, then after the grace its destructors, memory and library (LR-5).
            removeHooks(pMod);
            if (pInfo->stop != nullptr) {
                pInfo->stop();
            }
            endMod(pMod);
            frame();
            return;
        }
    }
}

extern "C" {
NEBULA_GAME_API int32_t luma_load_mod(const LumaModInfo* info, const char* mod_id, const char* file, int32_t order, void* library,
                                      LumaCloseLibrary close, LumaRefusal* refusal) {
    return luma::loadMod(info, mod_id, file, order, library, close, refusal) ? 1 : 0;
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
}
