// Mods' own classes (LumaClasses.hpp; contract classes, plan 1.4).
#include "Game/System/LumaClasses.hpp"

#include "Game/NameObj/NameObj.hpp"
#include "Game/NameObj/NameObjFactory.hpp"
#include "Game/System/LumaHook.hpp"
#include "Game/System/LumaLoader.hpp"
#include "Game/System/LumaTypes.hpp"

#include <nebula_format.h>
#include <nebula_module.h>
#include <stdlib.h>

namespace luma {
    namespace classes {
        namespace {
            constexpr u32 kMaxClasses = 1024;  // in a session; a row is never reused, so a number names one class
            constexpr u32 kNameBytes = 64;

            struct ClassRow;

            // One override: the row the base runs, and the mod's wrap. Kept for the session (a call on another thread may
            // still read it after the mod's hooks are gone; the rows are at most kMaxClasses).
            struct Override {
                u32 row;
                const void* baseTypeInfo;
                const Function* function;
                LumaHook hook;
                void* user;
            };

            struct ClassRow {
                bool live;
                const LumaMod* mod;
                char name[kNameBytes];
                char replaces[kNameBytes];  // "" when none
                char makerName[kNameBytes];
                const TypeRow* base;
                const void* baseTypeInfo;
                CreatorFuncPtr maker;
                u32 dataSize;
                u32 dataAlign;
                void (*made)(void* object, void* data, void* user);
                void* user;
                Override* overrides;
                u32 overrideCount;
                bool saidOther;  // the maker made another class: said once
            };

            ClassRow* sRows[kMaxClasses];
            u32 sRowCount = 0;
            u32 sLiveCount = 0;

            // The objects of the live classes, by the whole object's address: open addressing, linear probing, removal by
            // backward shift. Under sLock, as are the rows' `live` and the makes in progress.
            struct Instance {
                const void* object;  // null: empty
                void* data;
                u32 row;
            };
            Instance* sSlots = nullptr;
            u32 sSlotCount = 0;  // a power of two, or 0
            u32 sInstanceCount = 0;
            u32 sLock = 0;
            u32 sMaking = 0;  // makes between the maker and `made` (modGone waits for none)

            // The kernel's own hooks (the factory, NameObj's destructor): an owner no mod is.
            const char sKernelOwner = 0;
            bool sHooked = false;
            // The factory asked by the kernel itself: the game's own answer, never a mod class's.
            LUMA_THREAD bool tResolving = false;

            void lock() {
                while (__atomic_exchange_n(&sLock, 1u, __ATOMIC_ACQUIRE) != 0u) {
#if defined(__x86_64__) || defined(__i386__)
                    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
                    __asm__ volatile("yield");
#endif
                }
            }

            void unlock() { __atomic_store_n(&sLock, 0u, __ATOMIC_RELEASE); }

            u32 textLength(const char* p) {
                u32 n = 0;
                while (p[n] != 0) {
                    n++;
                }
                return n;
            }

            bool same(const char* pA, const char* pB) {
                while (*pA != 0 && *pA == *pB) {
                    pA++;
                    pB++;
                }
                return *pA == *pB;
            }

            void copyName(char* pTo, const char* pFrom) {
                u32 i = 0;
                for (; pFrom != nullptr && pFrom[i] != 0 && i + 1 < kNameBytes; i++) {
                    pTo[i] = pFrom[i];
                }
                pTo[i] = 0;
            }

            const LumaCore* core() { return luma::core(); }

            __attribute__((format(printf, 2, 3))) void say(const LumaMod* pMod, const char* pFormat, ...) {
                char text[512];
                __builtin_va_list args;
                __builtin_va_start(args, pFormat);
                nebula_vsnprintf(text, sizeof(text), pFormat, args);
                __builtin_va_end(args);
                core()->log(pMod, text);
            }

            u32 slotOf(const void* pObject) {
                const u64 key = reinterpret_cast<__UINTPTR_TYPE__>(pObject);
                // Objects are at least 8 bytes apart: the low bits say nothing.
                return static_cast<u32>(((key >> 3) * 0x9E3779B97F4A7C15ull) >> 32) & (sSlotCount - 1);
            }

            // Under sLock. The object's slot, or -1.
            s32 findLocked(const void* pObject) {
                if (sSlotCount == 0) {
                    return -1;
                }
                for (u32 at = slotOf(pObject);; at = (at + 1) & (sSlotCount - 1)) {
                    if (sSlots[at].object == pObject) {
                        return static_cast<s32>(at);
                    }
                    if (sSlots[at].object == nullptr) {
                        return -1;
                    }
                }
            }

            // Under sLock. False: no memory for a larger table (nothing changed).
            bool growLocked() {
                const u32 count = sSlotCount == 0 ? 64 : sSlotCount * 2;
                Instance* const pNew = static_cast<Instance*>(::calloc(count, sizeof(Instance)));
                if (pNew == nullptr) {
                    return false;
                }
                Instance* const pOld = sSlots;
                const u32 oldCount = sSlotCount;
                sSlots = pNew;
                sSlotCount = count;
                for (u32 i = 0; i < oldCount; i++) {
                    if (pOld[i].object != nullptr) {
                        u32 at = slotOf(pOld[i].object);
                        while (sSlots[at].object != nullptr) {
                            at = (at + 1) & (sSlotCount - 1);
                        }
                        sSlots[at] = pOld[i];
                    }
                }
                ::free(pOld);
                return true;
            }

            // Under sLock. An object made again at an address an object of a class had (its destructor not seen) takes
            // the place. False: no memory.
            bool insertLocked(const void* pObject, void* pData, u32 row, Instance* pReplaced) {
                pReplaced->object = nullptr;
                const s32 found = findLocked(pObject);
                if (found >= 0) {
                    *pReplaced = sSlots[found];
                    sSlots[found].data = pData;
                    sSlots[found].row = row;
                    return true;
                }
                if ((sInstanceCount + 1) * 2 > sSlotCount && !growLocked()) {
                    return false;
                }
                u32 at = slotOf(pObject);
                while (sSlots[at].object != nullptr) {
                    at = (at + 1) & (sSlotCount - 1);
                }
                sSlots[at] = {pObject, pData, row};
                sInstanceCount++;
                return true;
            }

            // Under sLock: empties a slot, moving back the ones after it that probed past it.
            void removeAtLocked(u32 hole) {
                u32 at = hole;
                for (;;) {
                    at = (at + 1) & (sSlotCount - 1);
                    if (sSlots[at].object == nullptr) {
                        break;
                    }
                    const u32 home = slotOf(sSlots[at].object);
                    // The entry may move into the hole when its home is not between the hole and it (cyclically).
                    const bool stays = hole <= at ? (hole < home && home <= at) : (hole < home || home <= at);
                    if (!stays) {
                        sSlots[hole] = sSlots[at];
                        hole = at;
                    }
                }
                sSlots[hole] = {nullptr, nullptr, 0};
                sInstanceCount--;
            }

            // A live row by the name the factory is asked for: its own name, or the name it replaces. -1: none.
            s32 rowForPlacement(const char* pName) {
                if (pName == nullptr) {
                    return -1;
                }
                for (u32 r = 0; r < sRowCount; r++) {
                    const ClassRow* const pRow = sRows[r];
                    if (pRow->live && (same(pRow->name, pName) || (pRow->replaces[0] != 0 && same(pRow->replaces, pName)))) {
                        return static_cast<s32>(r);
                    }
                }
                return -1;
            }

            CreatorFuncPtr gameCreator(const char* pName) {
                tResolving = true;
                const CreatorFuncPtr pCreator = NameObjFactory::getCreator(pName);
                tResolving = false;
                return pCreator;
            }

            // The creator the factory gives for a mod class's name: the base's maker makes it (the name the maker is the
            // factory's row for is the one its constructor is given), then its data and `made`.
            NameObj* make(const char* pName) {
                lock();
                const s32 r = rowForPlacement(pName);
                if (r < 0) {
                    unlock();
                    // The class went between the factory's answer and this call: the game's own object, if it has one.
                    const CreatorFuncPtr pGame = gameCreator(pName);
                    nebula_log("LumaCore: %s was a mod's class when the stage asked for it and is not now; %s\n", pName,
                               pGame != nullptr ? "the game's object is made" : "nothing is made");
                    return pGame != nullptr ? pGame(pName) : nullptr;
                }
                ClassRow* const pRow = sRows[r];
                sMaking++;
                unlock();
                NameObj* const pObject = pRow->maker(pRow->makerName);
                const void* pComplete = nullptr;
                const void* const pTypeInfo = pObject != nullptr ? ownTypeInfo(pObject, &pComplete) : nullptr;
                if (pObject != nullptr && pTypeInfo != pRow->baseTypeInfo) {
                    if (!pRow->saidOther) {
                        pRow->saidOther = true;
                        say(pRow->mod, "class %s: the factory's %s made a %s, not a %s; its objects are the game's", pRow->name, pRow->makerName,
                            pTypeInfo != nullptr ? typeInfoName(pTypeInfo) : "object the kernel cannot read", pRow->base->name);
                    }
                } else if (pObject != nullptr) {
                    void* pData = nullptr;
                    if (pRow->dataSize != 0) {
                        pData = core()->alloc(pRow->mod, pRow->dataSize, pRow->dataAlign);
                        if (pData == nullptr) {
                            say(pRow->mod, "class %s: no memory for an object's %u bytes of data; this one is a plain %s", pRow->name,
                                pRow->dataSize, pRow->base->name);
                        } else {
                            __builtin_memset(pData, 0, pRow->dataSize);
                        }
                    }
                    if (pRow->dataSize == 0 || pData != nullptr) {
                        Instance replaced;
                        lock();
                        const bool kept = insertLocked(pComplete, pData, static_cast<u32>(r), &replaced);
                        unlock();
                        if (!kept) {
                            say(pRow->mod, "class %s: no memory to keep one more object; this one is a plain %s", pRow->name, pRow->base->name);
                            core()->free(pRow->mod, pData);
                        } else {
                            if (replaced.object != nullptr && replaced.data != nullptr) {
                                core()->free(sRows[replaced.row]->mod, replaced.data);
                            }
                            if (pRow->made != nullptr) {
                                pRow->made(const_cast<void*>(pComplete), pData, pRow->user);
                            }
                        }
                    }
                }
                lock();
                sMaking--;
                unlock();
                return pObject;
            }

            // After NameObjFactory::getCreator: a live class's name (or the name it replaces) gets the kernel's maker.
            s32 factoryHook(Call* pCall, void*) {
                if (tResolving || sLiveCount == 0) {
                    return 0;
                }
                const char* const pName = *static_cast<const char* const*>(pCall->args[1]);
                lock();
                const bool ours = rowForPlacement(pName) >= 0;
                unlock();
                if (ours) {
                    *static_cast<CreatorFuncPtr*>(pCall->result) = &make;
                }
                return 0;
            }

            // Before NameObj::~NameObj: a class's object is gone. The base's NameObj is at its start (define checks), so
            // `this` there is the whole object's address.
            s32 destroyedHook(Call* pCall, void*) {
                if (sInstanceCount == 0) {
                    return 0;
                }
                Instance gone = {nullptr, nullptr, 0};
                lock();
                const s32 at = findLocked(pCall->args[0]);
                if (at >= 0) {
                    gone = sSlots[at];
                    removeAtLocked(static_cast<u32>(at));
                }
                unlock();
                if (gone.data != nullptr) {
                    core()->free(sRows[gone.row]->mod, gone.data);
                }
                return 0;
            }

            // The wrap on a row the base runs: the mod's override for its own objects, the rest of the chain for others.
            int32_t overrideHook(LumaCall* pCall, void* pUser) {
                const Override* const pOverride = static_cast<const Override*>(pUser);
                const void* const pSelf = pCall->args[0];
                const void* pComplete = nullptr;
                bool own = false;
                // Most calls of a shared row are other classes': one vtable read sends them on with no lock.
                if (pSelf != nullptr && ownTypeInfo(pSelf, &pComplete) == pOverride->baseTypeInfo) {
                    lock();
                    const s32 at = findLocked(pComplete);
                    own = at >= 0 && sSlots[at].row == pOverride->row;
                    unlock();
                }
                if (own) {
                    pOverride->hook(pCall, pOverride->user);
                } else {
                    core()->proceed(pCall);
                }
                return 0;
            }

            bool installKernelHooks() {
                if (sHooked) {
                    return true;
                }
                const Function* const pFactory = findFunction("NameObjFactory::getCreator", "CreatorFuncPtr(const char *)", nullptr);
                const Function* const pDestroyed = findFunction("NameObj::~NameObj", "()", nullptr);
                if (pFactory == nullptr || pDestroyed == nullptr) {
                    nebula_log("LumaCore: %s has no row; no mod can define a class\n", pFactory == nullptr ? "NameObjFactory::getCreator" : "NameObj::~NameObj");
                    return false;
                }
                // The factory's answer is the kernel's before any mod's after sees it (afters run the highest order first).
                if (addHook(pFactory, Hook_After, factoryHook, nullptr, &sKernelOwner, 0x7FFFFFFF) != HookResult_Added) {
                    nebula_log("LumaCore: the factory's hook was refused; no mod can define a class\n");
                    return false;
                }
                if (addHook(pDestroyed, Hook_Before, destroyedHook, nullptr, &sKernelOwner, 0x7FFFFFFF) != HookResult_Added) {
                    removeHook(pFactory, Hook_After, factoryHook, nullptr, &sKernelOwner);
                    nebula_log("LumaCore: NameObj's destructor's hook was refused; no mod can define a class\n");
                    return false;
                }
                sHooked = true;
                return true;
            }

            bool nameTaken(const char* pName) {
                if (findType(pName) != nullptr || gameCreator(pName) != nullptr) {
                    return true;
                }
                lock();
                const bool taken = rowForPlacement(pName) >= 0;
                unlock();
                return taken;
            }

            void unhookOverrides(const LumaMod* pMod, const Override* pOverrides, u32 count) {
                for (u32 i = 0; i < count; i++) {
                    core()->unhook(pMod, reinterpret_cast<const LumaFunction*>(pOverrides[i].function), LUMA_WRAP, overrideHook,
                                   const_cast<Override*>(&pOverrides[i]));
                }
            }
        }  // namespace

        s32 define(const LumaMod* pMod, const LumaClassDef* pDef) {
            if (!onGameThread()) {
                return -3;
            }
            if (pMod == nullptr || pDef == nullptr || pDef->size < sizeof(LumaClassDef)) {
                say(pMod, "define_class: %s", pDef == nullptr ? "no definition" : "the definition is smaller than luma.h's LumaClassDef");
                return -1;
            }
            const char* const pName = pDef->name != nullptr ? pDef->name : "";
            const u32 nameLength = textLength(pName);
            if (nameLength == 0 || nameLength >= kNameBytes || pDef->base == nullptr ||
                (pDef->replaces != nullptr && (pDef->replaces[0] == 0 || textLength(pDef->replaces) >= kNameBytes)) ||
                (pDef->maker != nullptr && textLength(pDef->maker) >= kNameBytes) || (pDef->override_count != 0 && pDef->overrides == nullptr)) {
                say(pMod, "define_class %s: a name (the class's, replaces or maker) is empty or over 63 bytes, the base is not named, or "
                          "overrides is null with a count", nameLength < kNameBytes ? pName : "(over 63 bytes)");
                return -1;
            }
            const u32 align = pDef->data_align == 0 ? 16u : pDef->data_align;
            if ((align & (align - 1)) != 0 || align > 4096) {
                say(pMod, "define_class %s: data_align %u is not a power of two up to 4096", pName, pDef->data_align);
                return -1;
            }
            const TypeRow* const pBase = findType(pDef->base);
            const TypeRow* const pNameObj = findType("NameObj");
            u32 nameObjAt = 1;
            if (pBase == nullptr || pBase->typeInfo == nullptr || !baseOffset(pBase, pNameObj, &nameObjAt) || nameObjAt != 0) {
                say(pMod, "define_class %s: base %s is %s", pName, pDef->base,
                    pBase == nullptr             ? "no class the game has a row for"
                    : pBase->typeInfo == nullptr ? "not polymorphic"
                                                 : "not an object the factory makes (NameObj is not at its start)");
                return -1;
            }
            if (sRowCount == kMaxClasses) {
                say(pMod, "define_class %s: the session's %u classes are all defined", pName, kMaxClasses);
                return -1;
            }
            if (nameTaken(pName)) {
                say(pMod, "define_class %s: the name is a game class's, a factory row's or another class's", pName);
                return -2;
            }
            if (pDef->replaces != nullptr) {
                lock();
                const bool replaced = rowForPlacement(pDef->replaces) >= 0;
                unlock();
                if (replaced) {
                    say(pMod, "define_class %s: %s is replaced by another class already", pName, pDef->replaces);
                    return -2;
                }
                if (gameCreator(pDef->replaces) == nullptr) {
                    say(pMod, "define_class %s: replaces %s, which no stage can place (the factory makes nothing by that name)", pName, pDef->replaces);
                    return -5;
                }
            }
            const char* const pMakerName = pDef->maker != nullptr ? pDef->maker : pDef->base;
            const CreatorFuncPtr pMaker = gameCreator(pMakerName);
            if (pMaker == nullptr) {
                say(pMod, "define_class %s: the factory has no row %s to make its base", pName, pMakerName);
                return -5;
            }
            if (!installKernelHooks()) {
                say(pMod, "define_class %s: the factory cannot be hooked (said above)", pName);
                return -1;
            }

            ClassRow* const pRow = static_cast<ClassRow*>(::calloc(1, sizeof(ClassRow)));
            Override* const pOverrides =
                pDef->override_count != 0 ? static_cast<Override*>(::calloc(pDef->override_count, sizeof(Override))) : nullptr;
            if (pRow == nullptr || (pDef->override_count != 0 && pOverrides == nullptr)) {
                ::free(pRow);
                ::free(pOverrides);
                say(pMod, "define_class %s: no memory for the class", pName);
                return -1;
            }
            const u32 row = sRowCount;
            for (u32 i = 0; i < pDef->override_count; i++) {
                const LumaOverride& rWant = pDef->overrides[i];
                const MethodRow* const pVirtual = findMethodRow(rWant.name, rWant.signature);
                bool ambiguous = false;
                const MethodRow* const pRuns = pVirtual != nullptr ? overriderOfClass(pBase, pVirtual, &ambiguous) : nullptr;
                const Function* const pFunction =
                    pRuns != nullptr ? findFunction(pRuns->function->name, pRuns->function->signature, nullptr) : nullptr;
                s32 hooked = -1;
                if (pFunction != nullptr && rWant.hook != nullptr) {
                    pOverrides[i] = {row, pBase->typeInfo, pFunction, rWant.hook, rWant.user};
                    hooked = core()->hook(pMod, reinterpret_cast<const LumaFunction*>(pFunction), LUMA_WRAP, overrideHook, &pOverrides[i]);
                }
                if (hooked != 0) {
                    const char* const pWhy = rWant.hook == nullptr ? "has no hook"
                                             : pVirtual == nullptr ? "names no virtual function the game declares"
                                             : ambiguous           ? "is the base's twice (two subobjects)"
                                             : pRuns == nullptr    ? "is not a virtual of the base"
                                             : pFunction == nullptr ? "is run by a function with no row (inline or implicit)"
                                                                    : "was refused as a hook";
                    say(pMod, "define_class %s: override %s %s %s%s%s", pName, rWant.name != nullptr ? rWant.name : "(no name)",
                        rWant.signature != nullptr ? rWant.signature : "(no signature)", pWhy, pRuns != nullptr && pFunction == nullptr ? ": " : "",
                        pRuns != nullptr && pFunction == nullptr ? pRuns->function->name : "");
                    unhookOverrides(pMod, pOverrides, i);
                    ::free(pRow);
                    ::free(pOverrides);
                    return -4;
                }
            }
            pRow->live = true;
            pRow->mod = pMod;
            copyName(pRow->name, pName);
            copyName(pRow->replaces, pDef->replaces);
            copyName(pRow->makerName, pMakerName);
            pRow->base = pBase;
            pRow->baseTypeInfo = pBase->typeInfo;
            pRow->maker = pMaker;
            pRow->dataSize = pDef->data_size;
            pRow->dataAlign = align;
            pRow->made = pDef->made;
            pRow->user = pDef->user;
            pRow->overrides = pOverrides;
            pRow->overrideCount = pDef->override_count;
            lock();
            sRows[row] = pRow;
            sRowCount = row + 1;
            sLiveCount++;
            unlock();
            say(pMod, "class %s: from %s (made by the factory's %s%s%s), %u bytes of its own, %u overrides", pName, pBase->name, pMakerName,
                pDef->replaces != nullptr ? "; in place of " : "", pDef->replaces != nullptr ? pDef->replaces : "", pDef->data_size,
                pDef->override_count);
            return static_cast<s32>(classCount() + row);
        }

        void* data(const void* pObject, s32 number) {
            const s32 row = number - static_cast<s32>(classCount());
            if (pObject == nullptr || row < 0 || sInstanceCount == 0) {
                return nullptr;
            }
            const void* pComplete = nullptr;
            if (ownTypeInfo(pObject, &pComplete) == nullptr) {
                return nullptr;
            }
            lock();
            const s32 at = findLocked(pComplete);
            void* const pData = at >= 0 && sSlots[at].row == static_cast<u32>(row) ? sSlots[at].data : nullptr;
            unlock();
            return pData;
        }

        s32 classOfObject(const void* pObject) {
            if (pObject == nullptr || sInstanceCount == 0) {
                return -1;
            }
            const void* pComplete = nullptr;
            if (ownTypeInfo(pObject, &pComplete) == nullptr) {
                return -1;
            }
            lock();
            const s32 at = findLocked(pComplete);
            const s32 number = at >= 0 ? static_cast<s32>(classCount() + sSlots[at].row) : -1;
            unlock();
            return number;
        }

        const void* asModClass(const void* pObject, s32 number) {
            const void* pComplete = nullptr;
            if (classOfObject(pObject) != number || number < 0 || ownTypeInfo(pObject, &pComplete) == nullptr) {
                return nullptr;
            }
            return pComplete;
        }

        s32 numberOf(const char* pName) {
            if (pName == nullptr) {
                return -1;
            }
            lock();
            s32 number = -1;
            for (u32 r = 0; r < sRowCount; r++) {
                if (sRows[r]->live && same(sRows[r]->name, pName)) {
                    number = static_cast<s32>(classCount() + r);
                    break;
                }
            }
            unlock();
            return number;
        }

        bool isModNumber(s32 number) { return number >= static_cast<s32>(classCount()); }

        void modGone(const LumaMod* pMod) {
            if (sRowCount == 0) {
                return;
            }
            // Its classes end first, so no new make starts for them; then a make already past the factory finishes
            // (it calls the mod's `made`, whose library closes only after the grace).
            u32 ended = 0;
            lock();
            for (u32 r = 0; r < sRowCount; r++) {
                if (sRows[r]->live && sRows[r]->mod == pMod) {
                    sRows[r]->live = false;
                    sLiveCount--;
                    ended++;
                }
            }
            unlock();
            if (ended == 0) {
                return;
            }
            for (;;) {
                lock();
                const u32 making = sMaking;
                unlock();
                if (making == 0) {
                    break;
                }
#if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
                __asm__ volatile("yield");
#endif
            }
            // Their objects are the base's again; their data is the mod's memory, freed with it after the grace.
            u32 objects = 0;
            lock();
            for (u32 at = 0; at < sSlotCount;) {
                if (sSlots[at].object != nullptr && sRows[sSlots[at].row]->mod == pMod) {
                    removeAtLocked(at);
                    objects++;
                    continue;  // the slot now holds what moved back into it
                }
                at++;
            }
            unlock();
            say(pMod, "its %u classes ended; their %u objects are the game's own now", ended, objects);
        }

        void sceneGone() {
            if (sInstanceCount == 0) {
                return;
            }
            // The scene's objects went with its memory (their destructors may not have run): every object is forgotten.
            lock();
            Instance* const pOld = sSlots;
            const u32 count = sSlotCount;
            sSlots = nullptr;
            sSlotCount = 0;
            sInstanceCount = 0;
            unlock();
            for (u32 i = 0; i < count; i++) {
                if (pOld[i].object != nullptr && pOld[i].data != nullptr) {
                    core()->free(sRows[pOld[i].row]->mod, pOld[i].data);
                }
            }
            ::free(pOld);
        }
    }  // namespace classes
}  // namespace luma
