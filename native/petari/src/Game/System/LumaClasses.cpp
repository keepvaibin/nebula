// Mods' own classes (LumaClasses.hpp; contract classes, plan 1.4).
#include "Game/System/LumaClasses.hpp"

#include "Game/NameObj/NameObj.hpp"
#include "Game/NameObj/NameObjFactory.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/SingletonHolder.hpp"
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

            // One override: a slot of the class's table (from its address point) and the mod's hook. Kept for the session
            // (a call on another thread may still read it after the mod went; the rows are at most kMaxClasses).
            struct Override {
                const MethodRow* method;    // the virtual the mod named
                const Function* original;   // method->original: the call's function, and the entry's key
                u32 slot;
                const void* entry;          // the build's code for the slot (it calls slotCall)
                const void* code;           // the base's code in the slot: set when the table is copied
                LumaHook hook;
                void* user;
            };

            // A class's table, ahead of the copy: the kernel's mark and the row, so an entry finds its class from the
            // object's table with no lookup. kPrefix: the entries before the address point, per ABI (gen_types.py reads
            // the build's layout dump; a base whose prefix differs takes no override).
#if defined(_WIN32)
            constexpr u32 kPrefix = 1;  // Microsoft's: the RTTI locator
#else
            constexpr u32 kPrefix = 2;  // Itanium's: offset to top, the type_info
#endif
            constexpr uintptr_t kTableMark = 0x4C756D6154626C31ull;  // "LumaTbl1"
            struct TableHead {
                uintptr_t mark;
                ClassRow* row;
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
                void (*gone)(void* object, void* data, void* user);  // null: none (or a definition from before it)
                void* user;
                Override* overrides;
                u32 overrideCount;
                bool saidOther;  // the maker made another class: said once
                // With overrides: the base's table at its address point, as the first object made had it, and the
                // class's copy (never freed: an object or a call may hold it). Null until the first object.
                void* const* baseTable;
                void** table;
                u32 tableCount;  // the base's: all entries, the prefix's too
                bool saidTable;  // a table problem: said once
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

            // Names as the game's factory compares them (NameObjFactory: hashNameCase, isEqualStringCase): ASCII case
            // folded, so a stage row's "kuribo" finds the class that replaces Kuribo, and two classes may not differ by
            // case alone.
            bool same(const char* pA, const char* pB) { return ::nebula_stricmp(pA, pB) == 0; }

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

            // The mod's `gone` for one object of row pRow's class, before the kernel frees its data (contract classes,
            // "gone"): once for every object the class kept, whose `made` ran just after. `pObject` is the object's
            // address while its NameObj part is still whole (its own parts' destructors have run); null when the scene's
            // memory went with it.
            void callGone(const ClassRow* pRow, const void* pObject, void* pData) {
                if (pRow->gone != nullptr) {
                    pRow->gone(const_cast<void*>(pObject), pData, pRow->user);
                }
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

            // A scene is running: its objects (the planet map's creators among them) can be asked for.
            bool inScene() {
                const GameSystem* const pSystem = SingletonHolder< GameSystem >::get();
                return pSystem != nullptr && pSystem->mSceneController != nullptr && pSystem->mSceneController->isExistSceneObjHolder();
            }

            CreatorFuncPtr gameCreator(const char* pName) {
                if (!inScene()) {
                    // A mod's start comes before any scene: the factory's planet rows are a scene's table (getCreator would
                    // ask a scene that is not there), so only its own table answers.
                    const NameObjFactory::Name2CreateFunc* const pRow = NameObjFactory::getName2CreateFunc(pName);
                    return pRow != nullptr ? pRow->mCreateFunc : nullptr;
                }
                tResolving = true;
                const CreatorFuncPtr pCreator = NameObjFactory::getCreator(pName);
                tResolving = false;
                return pCreator;
            }

            // The creator the factory gives for class row r's name: the base's maker makes it (the name the maker is the
            // factory's row for is the one its constructor is given), then its data and `made`. The game calls a creator
            // with the placement's own name (often not the class name the factory was asked for), so each row has its
            // own creator (kMakers) and the row is never found from the name.
            bool takeTable(ClassRow* pRow, void* pObject);

            NameObj* make(u32 r, const char* pName) {
                lock();
                ClassRow* const pRow = r < sRowCount ? sRows[r] : nullptr;
                if (pRow == nullptr || !pRow->live) {
                    unlock();
                    // The class went between the factory's answer and this call: the object it replaced, if any.
                    const CreatorFuncPtr pGame = pRow != nullptr && pRow->replaces[0] != 0 ? gameCreator(pRow->replaces) : nullptr;
                    nebula_log("LumaCore: %s (%s) was a mod's class when the stage asked for it and is not now; %s\n",
                               pRow != nullptr ? pRow->name : "?", pName != nullptr ? pName : "",
                               pGame != nullptr ? "the game's object is made" : "nothing is made");
                    return pGame != nullptr ? pGame(pName) : nullptr;
                }
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
                            if (replaced.object != nullptr) {
                                // The object at this address went without its destructor seen: its class's gone, then
                                // its data.
                                callGone(sRows[replaced.row], nullptr, replaced.data);
                                if (replaced.data != nullptr) {
                                    core()->free(sRows[replaced.row]->mod, replaced.data);
                                }
                            }
                            if (pRow->overrideCount != 0 && !takeTable(pRow, const_cast<void*>(pComplete))) {
                                // No table of its own (said): the object is not the class's; it stays a plain base.
                                lock();
                                const s32 at = findLocked(pComplete);
                                if (at >= 0 && sSlots[at].row == static_cast<u32>(r)) {
                                    removeAtLocked(static_cast<u32>(at));
                                }
                                unlock();
                                core()->free(pRow->mod, pData);
                            } else if (pRow->made != nullptr) {
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

            template <u32 R> NameObj* makeRow(const char* pName) { return make(R, pName); }
            template <class T, T... Rows> struct Makers {
                static constexpr CreatorFuncPtr kAll[sizeof...(Rows)] = {&makeRow<Rows>...};
            };
            // Row r's creator, for every row a session can have.
            constexpr const CreatorFuncPtr* kMakers = __make_integer_seq<Makers, u32, kMaxClasses>::kAll;

            // After NameObjFactory::getCreator: a live class's name (or the name it replaces) gets the kernel's maker.
            s32 factoryHook(Call* pCall, void*) {
                if (tResolving || sLiveCount == 0) {
                    return 0;
                }
                const char* const pName = *static_cast<const char* const*>(pCall->args[1]);
                lock();
                const s32 r = rowForPlacement(pName);
                unlock();
                if (r >= 0) {
                    *static_cast<CreatorFuncPtr*>(pCall->result) = kMakers[r];
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
                if (gone.object != nullptr) {
                    callGone(sRows[gone.row], gone.object, gone.data);
                    if (gone.data != nullptr) {
                        core()->free(sRows[gone.row]->mod, gone.data);
                    }
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

            uintptr_t word(const MemberBytes& rBytes, u32 at) {
                uintptr_t value = 0;
                if ((at + 1) * sizeof(uintptr_t) <= rBytes.size) {
                    __builtin_memcpy(&value, rBytes.bytes + at * sizeof(uintptr_t), sizeof(value));
                }
                return value;
            }

            // The slot (from the address point) a member pointer to a virtual calls through, per ABI, or -1 when it is
            // not one of the table at the object's start (an adjustment of `this`) or its code is not the compiler's
            // own form. Microsoft's x64: the pointer is a thunk, "mov rax, [rcx]" then the slot's load and a jump
            // (clang's: mov rax, [rax + d]; jmp rax; or jmp [rax + d]), the other words (adjustments) 0. Itanium's
            // x86-64: the pointer is 1 + the slot's offset, the adjustment 0. Itanium's arm64: the pointer is the offset,
            // the adjustment 1 (2 x 0 + virtual).
            s32 slotOfVirtual(const MemberBytes& rMember) {
                for (u32 i = 1; i * sizeof(uintptr_t) < rMember.size; i++) {
#if defined(__aarch64__) && !defined(_WIN32)
                    if (word(rMember, i) != (i == 1 ? 1u : 0u)) {
#else
                    if (word(rMember, i) != 0) {
#endif
                        return -1;
                    }
                }
                uintptr_t offset = 0;
#if defined(_WIN32) && defined(__x86_64__)
                const unsigned char* p = reinterpret_cast<const unsigned char*>(word(rMember, 0));
                if (p == nullptr) {
                    return -1;
                }
                if (p[0] == 0xE9) {  // a linker's jump to the thunk
                    s32 rel;
                    __builtin_memcpy(&rel, p + 1, sizeof(rel));
                    p += 5 + rel;
                }
                if (!(p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x01)) {
                    return -1;
                }
                p += 3;
                s32 displacement = 0;
                bool jumped = false;
                if (p[0] == 0x48 && p[1] == 0x8B && (p[2] == 0x00 || p[2] == 0x40 || p[2] == 0x80)) {
                    if (p[2] == 0x40) {
                        displacement = static_cast<signed char>(p[3]);
                        p += 4;
                    } else if (p[2] == 0x80) {
                        __builtin_memcpy(&displacement, p + 3, sizeof(displacement));
                        p += 7;
                    } else {
                        p += 3;
                    }
                    jumped = (p[0] == 0x48 && p[1] == 0xFF && p[2] == 0xE0) || (p[0] == 0xFF && p[1] == 0xE0);
                } else if (p[0] == 0xFF && (p[1] == 0x20 || p[1] == 0x60 || p[1] == 0xA0)) {
                    if (p[1] == 0x60) {
                        displacement = static_cast<signed char>(p[2]);
                    } else if (p[1] == 0xA0) {
                        __builtin_memcpy(&displacement, p + 2, sizeof(displacement));
                    }
                    jumped = true;
                }
                if (!jumped || displacement < 0) {
                    return -1;
                }
                offset = static_cast<uintptr_t>(displacement);
#elif defined(__x86_64__)
                const uintptr_t pointer = word(rMember, 0);
                if ((pointer & 1u) == 0) {
                    return -1;
                }
                offset = pointer - 1;
#elif defined(__aarch64__)
                offset = word(rMember, 0);
#else
                return -1;
#endif
                if (offset % sizeof(void*) != 0 || offset / sizeof(void*) > 0xFFFF) {
                    return -1;
                }
                return static_cast<s32>(offset / sizeof(void*));
            }

            // The code of a member pointer to a function that is not virtual and takes `this` unadjusted (the entry's).
            const void* codeOfMember(const MemberBytes& rMember) {
                for (u32 i = 1; i * sizeof(uintptr_t) < rMember.size; i++) {
                    if (word(rMember, i) != 0) {
                        return nullptr;
                    }
                }
#if defined(__x86_64__) && !defined(_WIN32)
                if ((word(rMember, 0) & 1u) != 0) {
                    return nullptr;  // Itanium x86-64: odd says virtual
                }
#endif
                return reinterpret_cast<const void*>(word(rMember, 0));
            }

            // The class's table from an object's: its head when the table is one of the kernel's copies, else null.
            ClassRow* rowOfTable(const void* pObject) {
                void* const* const pTable = *static_cast<void* const* const*>(pObject);
                const TableHead* const pHead = reinterpret_cast<const TableHead*>(pTable - kPrefix) - 1;
                return pHead->mark == kTableMark ? pHead->row : nullptr;
            }

            // An object's class row from the kernel's own records (the object's table may be the base's again).
            ClassRow* rowOfObject(const void* pObject) {
                const void* pComplete = nullptr;
                if (ownTypeInfo(pObject, &pComplete) == nullptr) {
                    return nullptr;
                }
                lock();
                const s32 at = findLocked(pComplete);
                ClassRow* const pRow = at >= 0 ? sRows[sSlots[at].row] : nullptr;
                unlock();
                return pRow;
            }

            const Override* overrideOf(const ClassRow* pRow, const Function* pOriginal) {
                for (u32 i = 0; pRow != nullptr && i < pRow->overrideCount; i++) {
                    if (pRow->overrides[i].original == pOriginal) {
                        return &pRow->overrides[i];
                    }
                }
                return nullptr;
            }

            // The object gets the class's table: the first object's table (the base's) is copied whole with the head
            // before it, and each overridden slot takes its entry. Later objects must have the same base table. False:
            // said (once) and the object stays a plain base.
            bool takeTable(ClassRow* pRow, void* pObject) {
                void* const* const pBase = *static_cast<void* const* const*>(pObject);
                lock();
                void* const* const pKnown = pRow->baseTable;
                void** const pTable = pRow->table;
                unlock();
                if (pKnown == nullptr) {
                    const u32 count = pRow->tableCount;
                    void** const pCopy = static_cast<void**>(::calloc(count + 2, sizeof(void*)));  // the head is two words
                    if (pCopy == nullptr) {
                        if (!pRow->saidTable) {
                            pRow->saidTable = true;
                            say(pRow->mod, "class %s: no memory for its table; its objects are plain %s", pRow->name, pRow->base->name);
                        }
                        return false;
                    }
                    TableHead* const pHead = reinterpret_cast<TableHead*>(pCopy);
                    pHead->mark = kTableMark;
                    pHead->row = pRow;
                    void** const pPoint = pCopy + 2 + kPrefix;
                    for (u32 i = 0; i < count; i++) {
                        pCopy[2 + i] = pBase[static_cast<s32>(i) - static_cast<s32>(kPrefix)];
                    }
                    for (u32 i = 0; i < pRow->overrideCount; i++) {
                        Override& rOverride = pRow->overrides[i];
                        rOverride.code = pPoint[rOverride.slot];
                        pPoint[rOverride.slot] = const_cast<void*>(rOverride.entry);
                    }
                    lock();
                    if (pRow->baseTable == nullptr) {
                        pRow->baseTable = pBase;
                        pRow->table = pPoint;
                        unlock();
                        *static_cast<void***>(pObject) = pPoint;
                        return true;
                    }
                    unlock();
                    ::free(pCopy);  // another thread's first object took it first
                    return takeTable(pRow, pObject);
                }
                if (pBase != pKnown) {
                    if (!pRow->saidTable) {
                        pRow->saidTable = true;
                        say(pRow->mod, "class %s: the factory's %s made an object with another table than the first one's; it is a plain %s",
                            pRow->name, pRow->makerName, pRow->base->name);
                    }
                    return false;
                }
                *static_cast<void***>(pObject) = pTable;
                return true;
            }
        }  // namespace

        s32 define(const LumaMod* pMod, const LumaClassDef* pDef) {
            if (!onGameThread()) {
                return -3;
            }
            // A definition from before `gone` (contract classes, "gone") ends at override_count: taken, with no gone.
            constexpr u32 cBeforeGone = __builtin_offsetof(LumaClassDef, gone);
            if (pMod == nullptr || pDef == nullptr || pDef->size < cBeforeGone) {
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
                say(pMod, "define_class %s: the factory has no row %s to make its base%s", pName, pMakerName,
                    inScene() ? "" : " (before a scene only its own table is known; a stage's planet names are not)");
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
            // Overrides by slot: the base's table must be one the build laid out with this ABI's prefix.
            if (pDef->override_count != 0 && (pBase->vtablePrefix != kPrefix || pBase->vtableCount <= kPrefix)) {
                say(pMod, "define_class %s: the build gives no layout of %s's table of virtual functions, so no override can take a "
                          "slot of it", pName, pBase->name);
                ::free(pRow);
                ::free(pOverrides);
                return -4;
            }
            for (u32 i = 0; i < pDef->override_count; i++) {
                const LumaOverride& rWant = pDef->overrides[i];
                const MethodRow* const pVirtual = findMethodRow(rWant.name, rWant.signature);
                const TypeRow* const pDeclaring = pVirtual != nullptr ? findType(pVirtual->record) : nullptr;
                bool ambiguous = false;
                const MethodRow* const pRuns = pVirtual != nullptr ? overriderOfClass(pBase, pVirtual, &ambiguous) : nullptr;
                u32 declaredAt = 1;
                const bool atStart = pDeclaring != nullptr && (pDeclaring == pBase || (baseOffset(pBase, pDeclaring, &declaredAt) && declaredAt == 0));
                MemberBytes member = {};
                MemberBytes entry = {};
                if (pVirtual != nullptr && pVirtual->members != nullptr) {
                    pVirtual->members(&member, &entry);
                }
                const s32 slot = pVirtual != nullptr && pVirtual->members != nullptr ? slotOfVirtual(member) : -1;
                const void* const pEntry = pVirtual != nullptr && pVirtual->members != nullptr ? codeOfMember(entry) : nullptr;
                unsigned kind = kFixed;
                if (pVirtual != nullptr && pVirtual->original != nullptr) {
                    pVirtual->original->invoke(kAskKind, &kind);
                }
                bool twice = false;
                for (u32 j = 0; j < i; j++) {
                    twice = twice || (slot >= 0 && pOverrides[j].slot == static_cast<u32>(slot));
                }
                const char* const pWhy =
                    rWant.hook == nullptr                                  ? "has no hook"
                    : pVirtual == nullptr                                  ? "names no virtual function the game declares"
                    : ambiguous                                            ? "is the base's twice (two subobjects)"
                    : pRuns == nullptr                                     ? "is not a virtual of the base"
                    : pVirtual->members == nullptr                         ? "is declared by a class the factory does not make, so it has no slot entry"
                    : !atStart                                             ? "is a virtual of a base that is not at the start of the base (not taken yet)"
                    : slot < 0 || pEntry == nullptr                        ? "has a slot this build cannot read"
                    : static_cast<u32>(slot) + kPrefix >= pBase->vtableCount ? "has a slot past the end of the base's table"
                    : kind == kFixed                                       ? "returns a class with a destructor or no default constructor, which an override cannot leave unbuilt"
                    : twice                                                ? "is overridden twice"
                                                                           : nullptr;
                if (pWhy != nullptr) {
                    say(pMod, "define_class %s: override %s %s %s", pName, rWant.name != nullptr ? rWant.name : "(no name)",
                        rWant.signature != nullptr ? rWant.signature : "(no signature)", pWhy);
                    ::free(pRow);
                    ::free(pOverrides);
                    return -4;
                }
                pOverrides[i] = {pVirtual, pVirtual->original, static_cast<u32>(slot), pEntry, nullptr, rWant.hook, rWant.user};
            }
            const u32 row = sRowCount;
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
            pRow->gone = pDef->size >= cBeforeGone + sizeof(pDef->gone) ? pDef->gone : nullptr;
            pRow->user = pDef->user;
            pRow->overrides = pOverrides;
            pRow->overrideCount = pDef->override_count;
            pRow->tableCount = pBase->vtableCount;
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

        void slotCall(const Function* pOriginal, void** args, void* result) {  // luma::slotCall
            // Only the kernel's tables hold an entry, so the object's table is one: its head says the class.
            ClassRow* const pRow = rowOfTable(args[0]);
            const Override* const pOverride = overrideOf(pRow, pOriginal);
            if (pOverride == nullptr) {
                NEBULA_PANIC("LumaCore: the entry of %s ran on an object whose table has no override of it", pOriginal->name);
            }
            Call call = {pOriginal, args, result, 0, 0, nullptr};
            // A class that ended (its mod gone) runs the base's code; a live class's override runs and proceeds itself, as a
            // wrap does (LumaHook proceed).
            if (!pRow->live) {
                proceed(&call);
            } else {
                (void)pOverride->hook(reinterpret_cast<LumaCall*>(&call), pOverride->user);
            }
        }

        const void* slotOriginal(const void* pObject, const Function* pOriginal) {  // luma::slotOriginal
            // The object's table is the class's copy, or the base's again (its mod went while the call was in it): the
            // kernel's records say the class then.
            ClassRow* pRow = rowOfTable(pObject);
            if (pRow == nullptr) {
                pRow = rowOfObject(pObject);
            }
            const Override* const pOverride = overrideOf(pRow, pOriginal);
            if (pOverride == nullptr || pOverride->code == nullptr) {
                NEBULA_PANIC("LumaCore: %s proceeded on an object that is no class overriding it", pOriginal->name);
            }
            return pOverride->code;
        }

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
                    const ClassRow* const pRow = sRows[sSlots[at].row];
                    void** const pObject = static_cast<void**>(const_cast<void*>(sSlots[at].object));
                    if (pRow->table != nullptr && *pObject == static_cast<void*>(pRow->table)) {
                        *pObject = const_cast<void*>(static_cast<const void*>(pRow->baseTable));
                    }
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
                if (pOld[i].object != nullptr) {
                    callGone(sRows[pOld[i].row], nullptr, pOld[i].data);
                    if (pOld[i].data != nullptr) {
                        core()->free(sRows[pOld[i].row]->mod, pOld[i].data);
                    }
                }
            }
            ::free(pOld);
        }
    }  // namespace classes

    void slotCall(const Function* pOriginal, void** args, void* result) { classes::slotCall(pOriginal, args, result); }
    const void* slotOriginal(const void* pObject, const Function* pOriginal) { return classes::slotOriginal(pObject, pOriginal); }
}  // namespace luma
