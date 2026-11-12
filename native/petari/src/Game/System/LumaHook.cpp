#include "Game/System/LumaHook.hpp"

#include <nebula_module.h>
#include <stdlib.h>

namespace luma {
    LUMA_THREAD const Function* tBody = nullptr;

#if defined(__APPLE__)
    extern const Function sRowsStart __asm("section$start$__DATA$lumafn");
    extern const Function sRowsStop __asm("section$end$__DATA$lumafn");
    extern unsigned char sFlagsStart __asm("section$start$__DATA$lumaflg");
    extern unsigned char sFlagsStop __asm("section$end$__DATA$lumaflg");
#elif !defined(_WIN32)
    // Weak: the linker defines them only when the section exists, and the module without rows (plan 1.7) has none;
    // both are then null, so there are no rows.
    extern "C" __attribute__((weak, visibility("hidden"))) const Function __start_lumafn[];
    extern "C" __attribute__((weak, visibility("hidden"))) const Function __stop_lumafn[];
    extern "C" __attribute__((weak, visibility("hidden"))) unsigned char __start_lumaflg[];
    extern "C" __attribute__((weak, visibility("hidden"))) unsigned char __stop_lumaflg[];
#endif

    namespace {
        // The rows and flags the linker gathered from every function.
#if defined(_WIN32)
        __attribute__((section("lumafn$a"), used)) const Function sRowsBefore = {};
        __attribute__((section("lumafn$z"), used)) const Function sRowsAfter = {};
        __attribute__((section("lumaflg$a"), used)) unsigned char sFlagsBefore = 0;
        __attribute__((section("lumaflg$z"), used)) unsigned char sFlagsAfter = 0;
        const Function* rowsBegin() { return &sRowsBefore + 1; }
        const Function* rowsEnd() { return &sRowsAfter; }
        unsigned char* flagsBegin() { return &sFlagsBefore + 1; }
        unsigned char* flagsEnd() { return &sFlagsAfter; }
#elif defined(__APPLE__)
        const Function* rowsBegin() { return &sRowsStart; }
        const Function* rowsEnd() { return &sRowsStop; }
        unsigned char* flagsBegin() { return &sFlagsStart; }
        unsigned char* flagsEnd() { return &sFlagsStop; }
#else
        const Function* rowsBegin() { return __start_lumafn; }
        const Function* rowsEnd() { return __stop_lumafn; }
        unsigned char* flagsBegin() { return __start_lumaflg; }
        unsigned char* flagsEnd() { return __stop_lumaflg; }
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
        Chain** sChains = nullptr;  // one per flag; null: no hooks
        Retired* sRetired = nullptr;
        u32 sRetiredCount = 0;
        // Calls on threads other than the game thread, counted as SRCU counts them: a call adds 1 to
        // sReaders[epoch & 1] and then reads the function's chain; the game thread flips sEpoch.
        u32 sEpoch = 0;
        u32 sReaders[2] = {0, 0};
        u32 sSequence = 0;
        bool sPrepared = false;
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

        void prepare() {
            if (sPrepared) {
                return;
            }
            sPrepared = true;
            const u32 rows = static_cast<u32>(rowsEnd() - rowsBegin());
            const u32 flags = static_cast<u32>(flagsEnd() - flagsBegin());
            sSorted = new const Function*[rows + 1];
            for (const Function* pRow = rowsBegin(); pRow != rowsEnd(); pRow++) {
                // A linker may pad between the files' rows.
                if (pRow->name == nullptr) {
                    continue;
                }
                // A row with no flag is a call row (contract varargs 4: constexpr): found and called, never hooked.
                if (pRow->signature == nullptr ||
                    (pRow->hooked != nullptr && (pRow->hooked < flagsBegin() || pRow->hooked >= flagsEnd()))) {
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
        }

        Chain** slotOf(const Function* pFunction) { return &sChains[pFunction->hooked - flagsBegin()]; }

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

    void proceed(Call* pCall) {
        const Chain* const pChain = static_cast<const Chain*>(pCall->chain);
        for (u32 i = pCall->next; pChain != nullptr && i < pChain->count; i++) {
            if (wraps(pChain->hooks[i].kind)) {
                pCall->next = i + 1;
                // Wraps are entered outermost first, so the last one recorded is the deepest entered (D2-1).
                pCall->ran = (pCall->ran & 1u) | ((i + 1u) << 1);
                pChain->hooks[i].callback(pCall, pChain->hooks[i].user);
                pCall->next = i;
                return;
            }
        }
        pCall->ran |= 1u;
        tBody = pCall->function;
        pCall->function->invoke(pCall->args, pCall->result);
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
                skip = pChain->hooks[i].callback(&call, pChain->hooks[i].user) != 0;
                // A fixed result is never left unbuilt: the function runs (loader contract 4).
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
        // filled by the function itself (LR-1). A plain result already holds its default.
        if ((call.ran & 1u) == 0 && resultKind(pFunction) == kReference && *static_cast<void**>(pResult) == nullptr) {
            // The skipping before's mod, else the deepest wrap entered: every wrap outside it proceeded (D2-1).
            const void* pOwner = pSkipper;
            if (pOwner == nullptr && (call.ran >> 1) != 0) {
                pOwner = pChain->hooks[(call.ran >> 1) - 1].owner;
            }
            if (gNoResult != nullptr) {
                gNoResult(pFunction, pOwner);
            }
            call.ran |= 1u;
            tBody = pFunction;
            pFunction->invoke(pArgs, pResult);
        }
        for (u32 i = count; i-- > 0;) {
            if (pChain->hooks[i].kind == Hook_After) {
                pChain->hooks[i].callback(&call, pChain->hooks[i].user);
            }
        }
    }

    void tell(const Function* pFunction, void** pArgs) {
        const Hold hold(pFunction);
        const Chain* const pChain = hold.chain;
        Call call = {pFunction, pArgs, nullptr, 0, 0, pChain};
        for (u32 i = 0; pChain != nullptr && i < pChain->count; i++) {
            pChain->hooks[i].callback(&call, pChain->hooks[i].user);
        }
    }
}
