#include "Game/System/LumaHook.hpp"

#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSequenceDirector.hpp"
#include "Game/System/GameSequenceProgress.hpp"
#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/Player/MarioHolder.hpp"
#include "Game/System/LumaGuard.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/SingletonHolder.hpp"

#include <nebula_module.h>
#include <stdlib.h>

namespace luma {
    LUMA_THREAD const Function* tBody = nullptr;

#if defined(__APPLE__)
    extern const Function sRowsStart __asm__("section$start$__DATA$lumafn");
    extern const Function sRowsStop __asm__("section$end$__DATA$lumafn");
    extern Flag sFlagsStart __asm__("section$start$__DATA$lumaflg");
    extern Flag sFlagsStop __asm__("section$end$__DATA$lumaflg");
    extern const NeedsRow sNeedsStart __asm__("section$start$__DATA$lumand");
    extern const NeedsRow sNeedsStop __asm__("section$end$__DATA$lumand");
#elif !defined(_WIN32)
    // Weak: the linker defines them only when the section exists, and the module without rows has none;
    // both are then null, so there are no rows.
    extern "C" __attribute__((weak, visibility("hidden"))) const Function __start_lumafn[];
    extern "C" __attribute__((weak, visibility("hidden"))) const Function __stop_lumafn[];
    extern "C" __attribute__((weak, visibility("hidden"))) Flag __start_lumaflg[];
    extern "C" __attribute__((weak, visibility("hidden"))) Flag __stop_lumaflg[];
    extern "C" __attribute__((weak, visibility("hidden"))) const NeedsRow __start_lumand[];
    extern "C" __attribute__((weak, visibility("hidden"))) const NeedsRow __stop_lumand[];
#endif

    namespace {
        // The rows and flags the linker gathered from every function.
#if defined(_WIN32)
        __attribute__((section("lumafn$a"), used)) const Function sRowsBefore = {};
        __attribute__((section("lumafn$z"), used)) const Function sRowsAfter = {};
        __attribute__((section("lumaflg$a"), used)) Flag sFlagsBefore = {0, 0, 0};
        __attribute__((section("lumaflg$z"), used)) Flag sFlagsAfter = {0, 0, 0};
        __attribute__((section("lumand$a"), used)) const NeedsRow sNeedsBefore = {};
        __attribute__((section("lumand$z"), used)) const NeedsRow sNeedsAfter = {};
        const Function* rowsBegin() { return &sRowsBefore + 1; }
        const Function* rowsEnd() { return &sRowsAfter; }
        Flag* flagsBegin() { return &sFlagsBefore + 1; }
        Flag* flagsEnd() { return &sFlagsAfter; }
        const NeedsRow* needsBegin() { return &sNeedsBefore + 1; }
        const NeedsRow* needsEnd() { return &sNeedsAfter; }
#elif defined(__APPLE__)
        const Function* rowsBegin() { return &sRowsStart; }
        const Function* rowsEnd() { return &sRowsStop; }
        Flag* flagsBegin() { return &sFlagsStart; }
        Flag* flagsEnd() { return &sFlagsStop; }
        const NeedsRow* needsBegin() { return &sNeedsStart; }
        const NeedsRow* needsEnd() { return &sNeedsStop; }
#else
        const Function* rowsBegin() { return __start_lumafn; }
        const Function* rowsEnd() { return __stop_lumafn; }
        Flag* flagsBegin() { return __start_lumaflg; }
        Flag* flagsEnd() { return __stop_lumaflg; }
        const NeedsRow* needsBegin() { return __start_lumand; }
        const NeedsRow* needsEnd() { return __stop_lumand; }
#endif

        struct Hook {
            HookCallback callback;
            void* user;
            const void* owner;
            s32 order;
            u32 sequence;
            HookKind kind;
        };
        // A function's hooks, by order then sequence. Never changed once published: a change builds a new one.
        struct Chain {
            u32 count;
            u32 gameUsers;  // the game thread's calls inside it now (only that thread touches it)
            Hook hooks[1];  // `count` of them
        };
        // A replaced chain, kept until no call can be inside it: freed once the game thread has left it and two flips
        // of sEpoch, each made when the other threads' counter it leaves had drained, came after it was replaced.
        struct Retired {
            Chain* chain;
            Retired* next;
            u32 epoch;  // sEpoch when it was replaced
        };

        const Function** sSorted = nullptr;
        u32 sCount = 0;
        // The sets of needs the rows have (NeedsRow), each once; a flag's `needs` is its set's place + 1.
        struct NeedsSet {
            const unsigned char* ids;
            const char* names;
        };
        NeedsSet* sNeedsSets = nullptr;
        u32 sNeedsSetCount = 0;
        Chain** sChains = nullptr;  // one per flag; null: no hooks
        Retired* sRetired = nullptr;
        u32 sRetiredCount = 0;
        // Calls on threads other than the game thread, counted as SRCU counts them: a call adds 1 to
        // sReaders[epoch & 1] and then reads the function's chain; the game thread flips sEpoch.
        u32 sEpoch = 0;
        u32 sReaders[2] = {0, 0};
        u32 sSequence = 0;
        // 0: not sorted, 1: one thread sorting, 2: sorted (atomic). The rows are read only once it is 2, from any thread.
        u32 sPrepared = 0;
        LUMA_THREAD bool tGameThread = false;

        s32 compare(const char* pA, const char* pB) {
            while (*pA != 0 && *pA == *pB) {
                pA++;
                pB++;
            }
            return static_cast<s32>(static_cast<unsigned char>(*pA)) - static_cast<s32>(static_cast<unsigned char>(*pB));
        }

        bool before(const Function* pA, const Function* pB) {
            const s32 name = compare(pA->name, pB->name);
            return name != 0 ? name < 0 : compare(pA->signature, pB->signature) < 0;
        }

        void sift(const Function** pHeap, u32 root, u32 count) {
            for (;;) {
                u32 child = root * 2 + 1;
                if (child >= count) {
                    return;
                }
                if (child + 1 < count && before(pHeap[child], pHeap[child + 1])) {
                    child++;
                }
                if (!before(pHeap[root], pHeap[child])) {
                    return;
                }
                const Function* pHeld = pHeap[root];
                pHeap[root] = pHeap[child];
                pHeap[child] = pHeld;
                root = child;
            }
        }

        void sortRows();
        void giveNeeds();
        Flag* flagOf(const Function* pFunction);

        // The first caller sorts; a caller on another thread meanwhile waits for it, so no thread reads half a table.
        void prepare() {
            if (__atomic_load_n(&sPrepared, __ATOMIC_ACQUIRE) == 2u) {
                return;
            }
            u32 expected = 0;
            if (__atomic_compare_exchange_n(&sPrepared, &expected, 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                sortRows();
                __atomic_store_n(&sPrepared, 2u, __ATOMIC_RELEASE);
                return;
            }
            while (__atomic_load_n(&sPrepared, __ATOMIC_ACQUIRE) != 2u) {
            }
        }

        void sortRows() {
            const u32 rows = static_cast<u32>(rowsEnd() - rowsBegin());
            const u32 flags = static_cast<u32>(flagsEnd() - flagsBegin());  // Flag records, pads between files included
            sSorted = new const Function*[rows + 1];
            for (const Function* pRow = rowsBegin(); pRow != rowsEnd(); pRow++) {
                // A linker may pad between the files' rows.
                if (pRow->name == nullptr) {
                    continue;
                }
                // A row with no flag is a call row (varargs.md section 4: constexpr): found and called, never hooked.
                if (pRow->signature == nullptr ||
                    (pRow->hooked != nullptr && !flagOf(pRow))) {
                    nebula_log("[luma] the function rows are not as the build wrote them; no function can be hooked\n");
                    sCount = 0;
                    return;
                }
                sSorted[sCount++] = pRow;
            }
            for (u32 i = sCount / 2; i-- > 0;) {
                sift(sSorted, i, sCount);
            }
            for (u32 end = sCount; end-- > 1;) {
                const Function* pHeld = sSorted[0];
                sSorted[0] = sSorted[end];
                sSorted[end] = pHeld;
                sift(sSorted, 0, end);
            }
            sChains = new Chain*[flags + 1]();
            giveNeeds();
        }

        // The flag record a row's `hooked` points into; null when it points at no whole record of the section.
        Flag* flagOf(const Function* pFunction) {
            unsigned char* const pAt = pFunction->hooked;
            unsigned char* const pBegin = reinterpret_cast<unsigned char*>(flagsBegin());
            if (pAt < pBegin || pAt >= reinterpret_cast<unsigned char*>(flagsEnd()) ||
                static_cast<u64>(pAt - pBegin) % sizeof(Flag) != 0) {
                return nullptr;
            }
            return reinterpret_cast<Flag*>(pAt);
        }

        Chain** slotOf(const Function* pFunction) { return &sChains[reinterpret_cast<Flag*>(pFunction->hooked) - flagsBegin()]; }

        const Function* sortedRow(const char* pName, const char* pSignature) {
            u32 low = 0;
            u32 high = sCount;
            while (low < high) {
                const u32 middle = (low + high) / 2;
                const s32 name = compare(sSorted[middle]->name, pName);
                const s32 order = name != 0 ? name : compare(sSorted[middle]->signature, pSignature);
                if (order < 0) {
                    low = middle + 1;
                } else {
                    high = middle;
                }
            }
            return low < sCount && compare(sSorted[low]->name, pName) == 0 && compare(sSorted[low]->signature, pSignature) == 0
                       ? sSorted[low]
                       : nullptr;
        }

        bool sameIds(const unsigned char* pA, const unsigned char* pB) {
            while (*pA != kNeedsEnd && *pA == *pB) {
                pA++;
                pB++;
            }
            return *pA == *pB;
        }

        // Each row the needs section names gets its set in its flag (M2; luma_hook.h present). A row the section
        // names that is not a row of the module is said once: the build wrote both, so it is a fault of the build.
        void giveNeeds() {
            const u32 rows = static_cast<u32>(needsEnd() - needsBegin());
            if (rows == 0) {
                return;
            }
            sNeedsSets = new NeedsSet[rows];
            u32 missing = 0;
            for (const NeedsRow* pRow = needsBegin(); pRow != needsEnd(); pRow++) {
                if (pRow->name == nullptr || pRow->ids == nullptr || pRow->ids[0] == kNeedsEnd) {
                    continue;  // a linker's pad
                }
                const Function* const pFunction = sortedRow(pRow->name, pRow->signature);
                Flag* const pFlag = pFunction != nullptr && pFunction->hooked != nullptr ? flagOf(pFunction) : nullptr;
                if (pFlag == nullptr) {
                    missing++;
                    continue;
                }
                u32 set = 0;
                while (set < sNeedsSetCount && !sameIds(sNeedsSets[set].ids, pRow->ids)) {
                    set++;
                }
                if (set == sNeedsSetCount) {
                    if (sNeedsSetCount == 0xFFFFu) {
                        missing++;
                        continue;
                    }
                    sNeedsSets[sNeedsSetCount++] = {pRow->ids, pRow->names};
                }
                pFlag->needs = static_cast<unsigned short>(set + 1);
            }
            if (missing != 0) {
                nebula_log("[luma] %u needs rows name no function row of the module; those functions are called with no "
                           "check\n",
                           missing);
            }
        }

        // Whether the scene's holder of scene objects is there (MR::getSceneObjHolder reads it with no test). Before
        // the game system is made (a mod's start) there is none.
        bool sceneHolderThere() {
            const GameSystem* const pSystem = SingletonHolder< GameSystem >::get();
            const GameSystemSceneController* const pController = pSystem != nullptr ? pSystem->mSceneController : nullptr;
            return pController != nullptr && pController->isExistSceneObjHolder();
        }

        // Whether the player's actor is in the scene's MarioHolder (luma_hook.h kNeedMarioActor): the holder is made
        // with the scene's objects, the actor set into it later in the making.
        bool marioActorThere() {
            return sceneHolderThere() && MR::isExistSceneObj(SceneObj_MarioHolder) &&
                   MR::getSceneObj< MarioHolder >(SceneObj_MarioHolder)->mActor != nullptr;
        }

        // Whether the game system's object a need names (luma_hook.h, kNeedUserFile and kNeedCometScheduler) is there:
        // made by GameSequenceDirector::initAfterResourceLoaded, once the system archive is loaded. The last one each
        // makes, so an object half made (a hook inside its making) is not there.
        bool systemThere(u32 id) {
            const GameSystem* const pSystem = SingletonHolder< GameSystem >::get();
            const GameSequenceDirector* const pDirector = pSystem != nullptr ? pSystem->mSequenceDirector : nullptr;
            if (pDirector == nullptr) {
                return false;
            }
            if (id == kNeedUserFile) {
                const SaveDataHandleSequence* const pSave = pDirector->mSaveDataHandleSequence;
                return pSave != nullptr && pSave->mCurrentUserFile != nullptr && pSave->mSaveIcon != nullptr;
            }
            if (id == kNeedCometScheduler) {
                const GameSequenceProgress* const pProgress = pDirector->mGameSequenceProgress;
                return pProgress != nullptr && pProgress->mGalaxyCometScheduler != nullptr;
            }
            return false;
        }

        const char* nameAt(const char* pNames, u32 index) {
            for (; index > 0; index--) {
                while (*pNames != 0) {
                    pNames++;
                }
                pNames++;
            }
            return pNames;
        }

        // The chain a call runs: read once, when it starts.
        const Chain* chainOf(const Function* pFunction) { return __atomic_load_n(slotOf(pFunction), __ATOMIC_ACQUIRE); }

        bool wraps(HookKind kind) { return kind == Hook_Wrap || kind == Hook_Replace; }

        Chain* newChain(u32 count) {
            void* const p = ::malloc(sizeof(Chain) + (count != 0 ? count - 1 : 0) * sizeof(Hook));
            if (p == nullptr) {
                NEBULA_PANIC("out of memory for a function's hooks");
            }
            Chain* const pChain = static_cast<Chain*>(p);
            pChain->count = count;
            pChain->gameUsers = 0;
            return pChain;
        }

        // Publishes a function's new chain (null when it has no hooks) and keeps the old one until no call can be
        // inside it. The flag is set before a chain exists and cleared after it is gone, so a call that sees the flag
        // and then no chain runs the function alone.
        void publish(const Function* pFunction, Chain* pChain) {
            Chain** const ppSlot = slotOf(pFunction);
            Chain* const pOld = *ppSlot;
            if (pChain != nullptr) {
                __atomic_store_n(pFunction->hooked, static_cast<unsigned char>(1), __ATOMIC_RELEASE);
            }
            // Ordered with every other thread's count and read (Hold): a call counted after the next flip reads this.
            __atomic_store_n(ppSlot, pChain, __ATOMIC_SEQ_CST);
            if (pChain == nullptr) {
                __atomic_store_n(pFunction->hooked, static_cast<unsigned char>(0), __ATOMIC_RELEASE);
            }
            if (pOld != nullptr) {
                Retired* const pRetired = new Retired;
                pRetired->chain = pOld;
                pRetired->next = sRetired;
                pRetired->epoch = __atomic_load_n(&sEpoch, __ATOMIC_RELAXED);
                sRetired = pRetired;
                sRetiredCount++;
            }
        }

        // The chain one call runs, held until the call ends. On the game thread the chain counts it (a plain add: no
        // other thread frees chains); elsewhere sReaders does, counted before the chain is read.
        struct Hold {
            Chain* chain;
            s32 counter;  // -1: the game thread

            explicit Hold(const Function* pFunction) {
                if (tGameThread) {
                    counter = -1;
                    chain = *slotOf(pFunction);
                    if (chain != nullptr) {
                        chain->gameUsers++;
                    }
                } else {
                    counter = static_cast<s32>(__atomic_load_n(&sEpoch, __ATOMIC_SEQ_CST) & 1u);
                    __atomic_fetch_add(&sReaders[counter], 1u, __ATOMIC_SEQ_CST);
                    chain = __atomic_load_n(slotOf(pFunction), __ATOMIC_SEQ_CST);
                }
            }

            ~Hold() {
                if (counter < 0) {
                    if (chain != nullptr) {
                        chain->gameUsers--;
                    }
                } else {
                    __atomic_fetch_sub(&sReaders[counter], 1u, __ATOMIC_RELEASE);
                }
            }

            Hold(const Hold&) = delete;
            Hold& operator=(const Hold&) = delete;
        };

        // The chain without the hooks `keep` refuses (null when none is left). `keep` is asked once per hook, in order:
        // removeHook's takes only the first match, so asking twice would keep what the first pass took out.
        template <class Keep> Chain* without(const Chain* pChain, Keep keep, u32* pRemoved) {
            Chain* const pNew = newChain(pChain->count);
            u32 at = 0;
            for (u32 i = 0; i < pChain->count; i++) {
                if (keep(pChain->hooks[i])) {
                    pNew->hooks[at++] = pChain->hooks[i];
                }
            }
            *pRemoved = pChain->count - at;
            if (at == 0) {
                ::free(pNew);
                return nullptr;
            }
            pNew->count = at;
            return pNew;
        }
    }

    // The address a function's invoke takes as "what kind is your result?" (luma_hook.h).
    void* kAskKind[1] = {nullptr};

    void (*gSkipRefused)(const Function* pFunction, const void* pOwner) = nullptr;
    void (*gNoResult)(const Function* pFunction, const void* pOwner) = nullptr;

    void (*gNotNow)(const Function* pFunction, const char* pNeed) = nullptr;

    namespace {
        // A refused call's reference result (luma_hook.h emptyObject): zero-initialized, so no runtime init.
        alignas(64) thread_local unsigned char tEmpty[kEmptyBytes];
    }  // namespace

    void* emptyObject() {
        __builtin_memset(tEmpty, 0, sizeof(tEmpty));
        return tEmpty;
    }

    bool present(const Function* pFunction, unsigned short needs) {
        // Only a mod's call is checked: the host and the harness call rows too (GameSystem::update each frame), and
        // they call each one where the game does.
        if (needs == 0 || needs > sNeedsSetCount || guard::current() == nullptr) {
            return true;
        }
        const NeedsSet& rSet = sNeedsSets[needs - 1];
        const bool holder = sceneHolderThere();  // every scene object is the holder's: none is there without it
        for (u32 i = 0; rSet.ids[i] != kNeedsEnd; i++) {
            const u32 id = rSet.ids[i];
            const bool there = id == kNeedUserFile || id == kNeedCometScheduler ? systemThere(id)
                               : id == kNeedMarioActor                         ? marioActorThere()
                                                                               : holder && (id == kNeedScene || MR::isExistSceneObj(static_cast<int>(id)));
            if (!there) {
                if (gNotNow != nullptr) {
                    gNotNow(pFunction, nameAt(rSet.names, i));
                }
                return false;
            }
        }
        return true;
    }

    void markGameThread() { tGameThread = true; }

    bool onGameThread() { return tGameThread; }

    u32 functionCount() {
        prepare();
        return sCount;
    }

    const Function* functionAt(u32 index) {
        prepare();
        return index < sCount ? sSorted[index] : nullptr;
    }

    u32 functionsNamed(const char* pName, u32* pFirst) {
        prepare();
        u32 low = 0;
        u32 high = sCount;
        while (low < high) {
            const u32 middle = (low + high) / 2;
            if (compare(sSorted[middle]->name, pName) < 0) {
                low = middle + 1;
            } else {
                high = middle;
            }
        }
        u32 end = low;
        while (end < sCount && compare(sSorted[end]->name, pName) == 0) {
            end++;
        }
        *pFirst = low;
        return end - low;
    }

    const Function* findFunction(const char* pName, const char* pSignature, FindResult* pResult) {
        FindResult result = Find_NoName;
        const Function* pFound = nullptr;
        u32 low = 0;
        const u32 count = functionsNamed(pName, &low);
        const u32 end = low + count;
        if (end > low) {
            if (pSignature == nullptr) {
                result = end - low == 1 ? Find_Found : Find_SeveralSignatures;
                pFound = end - low == 1 ? sSorted[low] : nullptr;
            } else {
                result = Find_OtherSignature;
                for (u32 i = low; i < end; i++) {
                    if (compare(sSorted[i]->signature, pSignature) == 0) {
                        result = Find_Found;
                        pFound = sSorted[i];
                        break;
                    }
                }
            }
        }
        if (pResult != nullptr) {
            *pResult = result;
        }
        return pFound;
    }

    unsigned resultKind(const Function* pFunction) {
        if (isTell(pFunction)) {
            return kPlain;  // a constructor or destructor: told, never skipped
        }
        unsigned kind = kPlain;
        pFunction->invoke(kAskKind, &kind);
        return kind;
    }

    const void* replacedBy(const Function* pFunction) {
        prepare();
        const Chain* const pChain = pFunction->hooked != nullptr ? chainOf(pFunction) : nullptr;
        for (u32 i = 0; pChain != nullptr && i < pChain->count; i++) {
            if (pChain->hooks[i].kind == Hook_Replace) {
                return pChain->hooks[i].owner;
            }
        }
        return nullptr;
    }

    HookResult addHook(const Function* pFunction, HookKind kind, HookCallback pCallback, void* pUser, const void* pOwner, s32 order) {
        prepare();
        if (!tGameThread) {
            return HookResult_NotGameThread;
        }
        // A virtual call (objects 10: LumaCore.method, import kind 8) has no flag: it is not one function. Its rows'
        // own functions are hooked instead.
        if (pFunction == nullptr || pFunction->hooked == nullptr || pCallback == nullptr || kind >= Hook_KindCount ||
            (wraps(kind) && (isTell(pFunction) || resultKind(pFunction) == kFixed)) ||
            (kind != Hook_Before && isVariadic(pFunction))) {
            return HookResult_Refused;
        }
        if (kind == Hook_Replace) {
            const void* pHolder = replacedBy(pFunction);
            if (pHolder != nullptr && pHolder != pOwner) {
                return HookResult_Replaced;
            }
        }
        const Chain* const pOld = chainOf(pFunction);
        const u32 count = pOld != nullptr ? pOld->count : 0;
        Chain* const pNew = newChain(count + 1);
        // After every hook of a lower or the same order: a mod's hooks keep the order of its rows.
        u32 at = 0;
        while (at < count && pOld->hooks[at].order <= order) {
            pNew->hooks[at] = pOld->hooks[at];
            at++;
        }
        pNew->hooks[at] = {pCallback, pUser, pOwner, order, sSequence++, kind};
        for (u32 i = at; i < count; i++) {
            pNew->hooks[i + 1] = pOld->hooks[i];
        }
        publish(pFunction, pNew);
        return HookResult_Added;
    }

    s32 removeHook(const Function* pFunction, HookKind kind, HookCallback pCallback, void* pUser, const void* pOwner) {
        prepare();
        if (!tGameThread) {
            return HookResult_NotGameThread;
        }
        const Chain* const pChain = pFunction != nullptr && pFunction->hooked != nullptr ? chainOf(pFunction) : nullptr;
        if (pChain == nullptr) {
            return -1;
        }
        // One hook: the first that matches.
        bool taken = false;
        u32 removed = 0;
        Chain* const pNew = without(
            pChain,
            [&](const Hook& rHook) {
                if (!taken && rHook.owner == pOwner && rHook.kind == kind && rHook.callback == pCallback && rHook.user == pUser) {
                    taken = true;
                    return false;
                }
                return true;
            },
            &removed);
        if (removed == 0) {
            ::free(pNew);
            return -1;
        }
        publish(pFunction, pNew);
        return 0;
    }

    void removeHooks(const void* pOwner) {
        prepare();
        for (u32 f = 0; f < sCount; f++) {
            const Chain* const pChain = sSorted[f]->hooked != nullptr ? chainOf(sSorted[f]) : nullptr;
            if (pChain == nullptr) {
                continue;
            }
            u32 removed = 0;
            Chain* const pNew = without(pChain, [&](const Hook& rHook) { return rHook.owner != pOwner; }, &removed);
            if (removed == 0) {
                ::free(pNew);
                continue;
            }
            publish(sSorted[f], pNew);
        }
    }

    void reclaim(bool waiting) {
        if ((sRetired == nullptr && !waiting) || !tGameThread) {
            return;
        }
        // Up to two flips, each when the counter the calls leave for it has drained: a call counted there may hold
        // a chain replaced before the flip; one counted after it reads the chain published before it.
        for (u32 flip = 0; flip < 2; flip++) {
            const u32 epoch = __atomic_load_n(&sEpoch, __ATOMIC_RELAXED);
            if (__atomic_load_n(&sReaders[(epoch + 1u) & 1u], __ATOMIC_SEQ_CST) != 0) {
                break;
            }
            __atomic_store_n(&sEpoch, epoch + 1u, __ATOMIC_SEQ_CST);
        }
        const u32 epoch = __atomic_load_n(&sEpoch, __ATOMIC_RELAXED);
        for (Retired** ppAt = &sRetired; *ppAt != nullptr;) {
            Retired* const pRetired = *ppAt;
            if (epoch - pRetired->epoch >= 2u && pRetired->chain->gameUsers == 0) {
                *ppAt = pRetired->next;
                ::free(pRetired->chain);
                delete pRetired;
                sRetiredCount--;
            } else {
                ppAt = &pRetired->next;
            }
        }
    }

    u32 retiredCount() { return sRetiredCount; }

    u32 graceMark() { return __atomic_load_n(&sEpoch, __ATOMIC_RELAXED); }

    bool graceOver(u32 mark) {
        if (__atomic_load_n(&sEpoch, __ATOMIC_RELAXED) - mark < 2u) {
            return false;
        }
        // A list replaced at or before the mark and not freed: the game thread is still inside it.
        for (const Retired* pRetired = sRetired; pRetired != nullptr; pRetired = pRetired->next) {
            if (static_cast<s32>(pRetired->epoch - mark) <= 0) {
                return false;
            }
        }
        return true;
    }

    const char cKernelOwner = 0;

    namespace {
        // A hook's callback, through the guard when a mod owns it. False: the guard caught a fault in it, or
        // its mod is off after one; *pAnswer is then 0 (a before does not skip).
        bool callHook(const Hook& rHook, Call* pCall, u32 boundary, s32* pAnswer) {
            if (rHook.owner == &cKernelOwner) {
                *pAnswer = rHook.callback(pCall, rHook.user);
                return true;
            }
            u64 answer = 0;
            const bool ran = guard::run(const_cast<LumaMod*>(static_cast<const LumaMod*>(rHook.owner)), boundary, pCall->function->name,
                                        reinterpret_cast<const void*>(rHook.callback), reinterpret_cast<uintptr_t>(pCall),
                                        reinterpret_cast<uintptr_t>(rHook.user), 0, 0, &answer);
            *pAnswer = static_cast<s32>(static_cast<u32>(answer));
            return ran;
        }

        // Runs the function's own code: tBody tells its entry check to run the body instead of the hooks. Its entry
        // clears tBody only while its flag is set, and the arguments are built after tBody is set (a by-value
        // parameter's constructor may reach another hooked function, whose own entry clears tBody). So tBody is put
        // back as it was once the call returns: a flag cleared meanwhile (the last hook taken out from a before) leaves
        // no stale tBody to skip the hooks of the function's next call, and a call whose arguments are still being
        // built keeps its own.
        void enterBody(const Function* pFunction, void** pArgs, void* pResult) {
            const Function* const pPending = tBody;
            tBody = pFunction;
            pFunction->invoke(pArgs, pResult);
            tBody = pPending;
        }
    }

    void proceed(Call* pCall) {
        const Chain* const pChain = static_cast<const Chain*>(pCall->chain);
        for (u32 i = pCall->next; pChain != nullptr && i < pChain->count; i++) {
            if (wraps(pChain->hooks[i].kind)) {
                pCall->next = i + 1;
                // Wraps are entered outermost first, so the last one recorded is the deepest entered.
                pCall->ran = (pCall->ran & 1u) | ((i + 1u) << 1);
                s32 answer = 0;
                if (!callHook(pChain->hooks[i], pCall, LUMA_GUARD_WRAP, &answer) && (pCall->ran & 1u) == 0 &&
                    (pCall->ran >> 1) == i + 1u) {
                    // Its mod's code failed (or is off) before it proceeded and before any wrap inside it was entered:
                    // the call goes on as if it had proceeded, to the next wrap or the function's own code.
                    pCall->next = i + 1;
                    proceed(pCall);
                }
                pCall->next = i;
                return;
            }
        }
        pCall->ran |= 1u;
        enterBody(pCall->function, pCall->args, pCall->result);
    }

    void run(const Function* pFunction, void** pArgs, void* pResult) {
        const Hold hold(pFunction);
        const Chain* const pChain = hold.chain;
        Call call = {pFunction, pArgs, pResult, 0, 0, pChain};
        const u32 count = pChain != nullptr ? pChain->count : 0;
        bool skip = false;
        const void* pSkipper = nullptr;
        for (u32 i = 0; i < count && !skip; i++) {
            if (pChain->hooks[i].kind == Hook_Before) {
                s32 answer = 0;
                callHook(pChain->hooks[i], &call, LUMA_GUARD_BEFORE, &answer);
                skip = answer != 0;
                // A fixed result is never left unbuilt: the function runs (docs/modding/reference/lumascript/compiled-form.md).
                if (skip && resultKind(pFunction) == kFixed) {
                    if (gSkipRefused != nullptr) {
                        gSkipRefused(pFunction, pChain->hooks[i].owner);
                    }
                    skip = false;
                }
                if (skip) {
                    pSkipper = pChain->hooks[i].owner;
                }
            }
        }
        if (!skip) {
            proceed(&call);
        }
        // The function's own code did not run (a skip, or a wrap that did not proceed): a reference nothing wrote is
        // filled by the function itself. A plain result already holds its default.
        if ((call.ran & 1u) == 0 && resultKind(pFunction) == kReference && *static_cast<void**>(pResult) == nullptr) {
            // The skipping before's mod, else the deepest wrap entered: every wrap outside it proceeded.
            const void* pOwner = pSkipper;
            if (pOwner == nullptr && (call.ran >> 1) != 0) {
                pOwner = pChain->hooks[(call.ran >> 1) - 1].owner;
            }
            if (gNoResult != nullptr) {
                gNoResult(pFunction, pOwner);
            }
            call.ran |= 1u;
            enterBody(pFunction, pArgs, pResult);
        }
        for (u32 i = count; i-- > 0;) {
            if (pChain->hooks[i].kind == Hook_After) {
                s32 answer = 0;
                callHook(pChain->hooks[i], &call, LUMA_GUARD_AFTER, &answer);
            }
        }
    }

    void tell(const Function* pFunction, void** pArgs) {
        const Hold hold(pFunction);
        const Chain* const pChain = hold.chain;
        Call call = {pFunction, pArgs, nullptr, 0, 0, pChain};
        for (u32 i = 0; pChain != nullptr && i < pChain->count; i++) {
            s32 answer = 0;
            callHook(pChain->hooks[i], &call, LUMA_GUARD_TELL, &answer);
        }
    }
}
