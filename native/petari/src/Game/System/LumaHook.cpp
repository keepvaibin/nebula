#include "Game/System/LumaHook.hpp"

#include <nebula_module.h>

namespace luma {
    LUMA_THREAD const Function* tBody = nullptr;

#if defined(__APPLE__)
    extern const Function sRowsStart __asm("section$start$__DATA$lumafn");
    extern const Function sRowsStop __asm("section$end$__DATA$lumafn");
    extern unsigned char sFlagsStart __asm("section$start$__DATA$lumaflg");
    extern unsigned char sFlagsStop __asm("section$end$__DATA$lumaflg");
#elif !defined(_WIN32)
    extern "C" const Function __start_lumafn[];
    extern "C" const Function __stop_lumafn[];
    extern "C" unsigned char __start_lumaflg[];
    extern "C" unsigned char __stop_lumaflg[];
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
        // A function's hooks, by order then sequence.
        struct Chain {
            Hook* hooks;
            u32 count;
            u32 capacity;
        };

        const Function** sSorted = nullptr;
        u32 sCount = 0;
        Chain* sChains = nullptr;  // one per flag
        u32 sSequence = 0;
        bool sPrepared = false;

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
                if (pRow->signature == nullptr || pRow->hooked < flagsBegin() || pRow->hooked >= flagsEnd()) {
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
            sChains = new Chain[flags + 1]();
        }

        Chain& chainOf(const Function* pFunction) { return sChains[pFunction->hooked - flagsBegin()]; }

        bool wraps(HookKind kind) { return kind == Hook_Wrap || kind == Hook_Replace; }
    }

    u32 functionCount() {
        prepare();
        return sCount;
    }

    const Function* functionAt(u32 index) {
        prepare();
        return index < sCount ? sSorted[index] : nullptr;
    }

    const Function* findFunction(const char* pName, const char* pSignature, FindResult* pResult) {
        prepare();
        FindResult result = Find_NoName;
        const Function* pFound = nullptr;
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

    const void* replacedBy(const Function* pFunction) {
        prepare();
        const Chain& rChain = chainOf(pFunction);
        for (u32 i = 0; i < rChain.count; i++) {
            if (rChain.hooks[i].kind == Hook_Replace) {
                return rChain.hooks[i].owner;
            }
        }
        return nullptr;
    }

    HookResult addHook(const Function* pFunction, HookKind kind, HookCallback pCallback, void* pUser, const void* pOwner, s32 order) {
        prepare();
        if (pFunction == nullptr || pCallback == nullptr || kind >= Hook_KindCount || (pFunction->invoke == nullptr && wraps(kind))) {
            return HookResult_Refused;
        }
        if (kind == Hook_Replace) {
            const void* pHolder = replacedBy(pFunction);
            if (pHolder != nullptr && pHolder != pOwner) {
                return HookResult_Replaced;
            }
        }
        Chain& rChain = chainOf(pFunction);
        if (rChain.count == rChain.capacity) {
            const u32 capacity = rChain.capacity == 0 ? 2 : rChain.capacity * 2;
            Hook* pHooks = new Hook[capacity];
            for (u32 i = 0; i < rChain.count; i++) {
                pHooks[i] = rChain.hooks[i];
            }
            delete[] rChain.hooks;
            rChain.hooks = pHooks;
            rChain.capacity = capacity;
        }
        u32 at = rChain.count;
        while (at > 0 && rChain.hooks[at - 1].order > order) {
            rChain.hooks[at] = rChain.hooks[at - 1];
            at--;
        }
        rChain.hooks[at] = {pCallback, pUser, pOwner, order, sSequence++, kind};
        rChain.count++;
        *pFunction->hooked = 1;
        return HookResult_Added;
    }

    void removeHooks(const void* pOwner) {
        prepare();
        for (u32 f = 0; f < sCount; f++) {
            Chain& rChain = chainOf(sSorted[f]);
            u32 kept = 0;
            for (u32 i = 0; i < rChain.count; i++) {
                if (rChain.hooks[i].owner != pOwner) {
                    rChain.hooks[kept++] = rChain.hooks[i];
                }
            }
            rChain.count = kept;
            if (kept == 0) {
                *sSorted[f]->hooked = 0;
            }
        }
    }

    void proceed(Call* pCall) {
        const Chain& rChain = chainOf(pCall->function);
        for (u32 i = pCall->next; i < rChain.count; i++) {
            if (wraps(rChain.hooks[i].kind)) {
                pCall->next = i + 1;
                rChain.hooks[i].callback(pCall, rChain.hooks[i].user);
                pCall->next = i;
                return;
            }
        }
        tBody = pCall->function;
        pCall->function->invoke(pCall->args, pCall->result);
    }

    void run(const Function* pFunction, void** pArgs, void* pResult) {
        const Chain& rChain = chainOf(pFunction);
        Call call = {pFunction, pArgs, pResult, 0};
        bool skip = false;
        for (u32 i = 0; i < rChain.count && !skip; i++) {
            if (rChain.hooks[i].kind == Hook_Before) {
                skip = rChain.hooks[i].callback(&call, rChain.hooks[i].user) != 0;
            }
        }
        if (!skip) {
            proceed(&call);
        }
        for (u32 i = rChain.count; i-- > 0;) {
            if (rChain.hooks[i].kind == Hook_After) {
                rChain.hooks[i].callback(&call, rChain.hooks[i].user);
            }
        }
    }

    void tell(const Function* pFunction, void** pArgs) {
        const Chain& rChain = chainOf(pFunction);
        Call call = {pFunction, pArgs, nullptr, 0};
        for (u32 i = 0; i < rChain.count; i++) {
            rChain.hooks[i].callback(&call, rChain.hooks[i].user);
        }
    }
}
