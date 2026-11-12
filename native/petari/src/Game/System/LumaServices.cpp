#include "Game/System/LumaServices.hpp"

#include "Game/System/LumaHook.hpp"
#include "Game/System/LumaContentFx.hpp"
#include "Game/System/LumaContentModel.hpp"
#include "Game/System/LumaAnim.hpp"
#include "Game/System/LumaUi.hpp"
#include "Game/System/LumaDrawLight.hpp"
#include "Game/System/LumaDraw.hpp"
#include "Game/System/LumaMovie.hpp"
#include "Game/System/LumaDrawInst.hpp"
#include "Game/System/LumaLoader.hpp"
#include "Game/System/LumaSave.hpp"
#include "Game/System/LumaTime.hpp"
#include <luma.h>
#include <luma_service_provider.h>
#include <luma_services.h>
#include <nebula_format.h>
#include <nebula_game_api.h>
#include <nebula_module.h>
#include <stddef.h>
#include <stdlib.h>

// The service registry, its catalog and the services' event queue (services-registry contract 2, 6, 7, 8, 9, 11).
//
// Threads. Registration happens on the game thread before the first mod begins, and is read-only after. A mod's record
// is written on the game thread (begin, gone, end); other threads read it only to post an event or ask a fact, which a
// provider does only between the mod's begin and its gone (luma_service_provider.h). Each mod's queue is a lock-free
// stack: posts push from any thread, the frame's start takes the whole stack with one exchange.

namespace luma {
    namespace services {
        namespace {
            // The catalog: every service the game defines (services-registry 9). A row is added with the code of its
            // service when that service's contract is signed; none is yet.
            const CatalogRow cCatalog[] = {
                {"time", 1, 0, nullptr, "services-time"},
                {"system", 1, 0, nullptr, "services-system"},
                {"system.clipboard", 1, 0, "system.clipboard", "services-system"},
                {"system.links", 1, 0, "system.links", "services-system"},
                {"mods", 1, 0, nullptr, "services-mods"},
                {"mods.ids", 1, 0, nullptr, "services-mods"},
                {"mods.install", 1, 0, nullptr, "services-mods"},
                {"content.fx", 1, 0, nullptr, "effects"},
                {"content.model", 1, 1, nullptr, "models"},
                {"ui", 1, 2, nullptr, "ui"},
                {"draw.light", 1, 0, nullptr, "draw"},
                {"draw", 1, 0, nullptr, "draw"},
                {"movie", 1, 0, nullptr, "movies"},
                {"draw.inst", 1, 0, nullptr, "draw"},
                {"anim", 1, 1, nullptr, "animation"},
                {"save", 1, 0, nullptr, "services-save"},
                {"save.global", 1, 0, nullptr, "services-save"},
                {"save.game", 1, 0, "save.game", "services-save"},
                {"fs", 1, 0, nullptr, "services-fs"},
                {"fs.exchange", 1, 0, "fs.exchange", "services-fs"},
                {"net.local", 1, 4, "net.local", "services-net"},
                {"net.internet", 1, 4, "net.internet", "services-net"},
                {"assets", 1, 0, nullptr, "services-assets"},
                {"input", 1, 0, nullptr, "services-input"},
                {"audio", 1, 0, nullptr, "services-audio"},
                {"audio.microphone", 1, 0, "audio.microphone", "services-audio"},
                {"capture.camera", 1, 0, "capture.camera", "services-audio"},
                {nullptr, 0, 0, nullptr, nullptr},
            };

            // Registered services: the catalog's, the module's and the host's, and a host extension's "test." ones.
            const u32 kMostServices = 64;
            struct Entry {
                const LumaServiceProvider* provider;
                const char* permission;  // the catalog's, or a test service's own; null: none
                u16 major;
                u16 minor;
            };
            Entry sEntries[kMostServices];
            u32 sEntryCount = 0;  // written on the game thread before sSealed, with release; read with acquire
            bool sSealed = false; // a mod began: no more registrations (game thread)
            bool sOpened = false; // luma_services_open ran: the module's own are registered (game thread)

            // A provider as far as v1 defines it: everything up to mod_end.
            const u32 kProviderLeast = static_cast<u32>(offsetof(LumaServiceProvider, mod_end) + sizeof(void (*)(void*, const LumaMod*)));

            // One mod, from its begin to its end. Slots are reused only after an end; a mod taken out waits for its
            // grace in its slot, so there are twice as many slots as mods may be on.
            const u32 kMostRecords = LUMA_MAX_MODS * 2u;
            // A mod's id and its 0: luma-format 2 allows 3 to 64 characters.
            const u32 kIdBytes = 64u + 1u;
            struct Record {
                const LumaMod* mod;  // null: a free slot. Stored with release on the game thread; loaded with acquire
                char id[kIdBytes];
                s32 order;
                char** granted;
                u32 grantedCount;
                u32 gone;              // 1 after its gone; atomic
                LumaEventNode* head;   // posted and not taken, newest first; atomic
                LumaEventNode* first;  // taken and not yet delivered, oldest first (game thread)
                LumaEventNode* last;
                u32 carried;           // nodes in first..last (game thread)
            };
            Record sRecords[kMostRecords];
            u32 sRecordHigh = 0;  // slots below it may be in use; atomic

            // The live records by list order, top first (game thread). Gone ones leave it.
            Record* sByOrder[kMostRecords];
            // frameStart's copy of sByOrder (game thread; a frame's start never runs inside another). Not on the
            // stack: 4 KiB there needs __chkstk, which the module (no C runtime) does not have.
            Record* sDeliverOrder[kMostRecords];
            u32 sByOrderCount = 0;

            u32 sPosted = 0;   // 1 when a node was posted since the frame's start took the stacks, or nodes are carried; atomic
            u32 sCarried = 0;  // nodes taken but kept for a later frame, all mods (game thread)
            void (*sTimePost)() = nullptr;     // time's due timers, before the events; null while no mod has a timer
            void (*sFrameHandler)() = nullptr;  // time's frame handlers, after them; null while no mod has one
            void (*sSaveWork)() = nullptr;      // save.global's writes, a FORGET's save; null while there is none
            void (*sSaveWake)() = nullptr;      // wakeSaveHook's, taken at the frame's start; atomic

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

            bool startsWith(const char* pText, const char* pStart) {
                if (pText == nullptr) {
                    return false;
                }
                for (; *pStart != 0; pStart++, pText++) {
                    if (*pText != *pStart) {
                        return false;
                    }
                }
                return true;
            }

            // Parts of [a-z][a-z0-9]* joined by '.', at most 31 bytes (services-registry 2).
            bool validName(const char* pName) {
                u32 length = 0;
                bool partStart = true;
                for (const char* p = pName; *p != 0; p++, length++) {
                    const char c = *p;
                    if (partStart) {
                        if (c < 'a' || c > 'z') {
                            return false;
                        }
                        partStart = false;
                    } else if (c == '.') {
                        partStart = true;
                    } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
                        return false;
                    }
                }
                return length != 0 && length <= 31 && !partStart;
            }

            // A service's block of report codes (services-registry 11), by the name's first part. False: none.
            struct RowBlock {
                const char* service;
                u16 first;
                u16 last;
            };
            const RowBlock cRowBlocks[] = {
                {"time", 120, 139},    {"system", 140, 159},  {"mods", 160, 179},  {"save", 180, 199},
                {"fs", 200, 249},      {"net", 250, 299},     {"input", 300, 349}, {"audio", 350, 399},
                {"capture", 400, 419}, {"assets", 420, 469},  {"draw", 470, 569},  {"ui", 570, 619},
            };
            bool rowBlock(const char* pName, u32* pFirst, u32* pLast) {
                for (u32 i = 0; i < sizeof(cRowBlocks) / sizeof(cRowBlocks[0]); i++) {
                    const char* pService = cRowBlocks[i].service;
                    const char* p = pName;
                    while (*pService != 0 && *p == *pService) {
                        pService++;
                        p++;
                    }
                    if (*pService == 0 && (*p == 0 || *p == '.')) {
                        *pFirst = cRowBlocks[i].first;
                        *pLast = cRowBlocks[i].last;
                        return true;
                    }
                }
                return false;
            }

            // A copy from the module's malloc; the game stops when there is no memory (as the loader does).
            char* keepText(const char* pText) {
                const char* const pFrom = pText != nullptr ? pText : "";
                size_t length = 0;
                while (pFrom[length] != 0) {
                    length++;
                }
                char* const pKept = static_cast<char*>(::malloc(length + 1));
                if (pKept == nullptr) {
                    NEBULA_PANIC("LumaCore: no memory to keep a mod's permissions");
                }
                for (size_t i = 0; i <= length; i++) {
                    pKept[i] = pFrom[i];
                }
                return pKept;
            }

            const CatalogRow* catalogRow(const char* pName, u16 major) {
                for (const CatalogRow* pRow = cCatalog; pRow->name != nullptr; pRow++) {
                    if (pRow->major == major && sameText(pRow->name, pName)) {
                        return pRow;
                    }
                }
                return nullptr;
            }

            Record* recordOf(const LumaMod* pMod) {
                if (pMod == nullptr) {
                    return nullptr;
                }
                const u32 high = __atomic_load_n(&sRecordHigh, __ATOMIC_ACQUIRE);
                for (u32 i = 0; i < high; i++) {
                    if (__atomic_load_n(&sRecords[i].mod, __ATOMIC_ACQUIRE) == pMod) {
                        return &sRecords[i];
                    }
                }
                return nullptr;
            }

            bool isGone(const Record* pRecord) { return __atomic_load_n(&pRecord->gone, __ATOMIC_ACQUIRE) != 0; }

            // The registry's own rows (services-registry 11).
            struct OwnRow {
                u32 code;
                const char* id;
                const char* names[4];
            };
            const OwnRow cOwnRows[] = {
                {100, "Service.Thread", {"where", nullptr, nullptr, nullptr}},
                {101, "Service.Argument", {"where", "argument", nullptr, nullptr}},
                {102, "Service.Handle", {"where", nullptr, nullptr, nullptr}},
                {103, "Service.StructSize", {"where", "struct", "found", "expected"}},
                {104, "Service.Limit", {"where", "count", nullptr, nullptr}},
            };

            bool powerOfTen(u64 n) {
                while (n >= 10u && n % 10u == 0) {
                    n /= 10u;
                }
                return n == 1u;
            }

            // A caller's bug said once a place and counted (said again at 10, 100, 1000...), so a bad call every frame
            // does not fill the log (services-registry 4.1). Any thread: slots are claimed by compare-and-swap on the
            // place's key and never freed.
            struct Said {
                u64 key;  // 0: free
                u64 count;
            };
            const u32 kMostSaid = 64u;
            Said sSaid[kMostSaid];
            u64 sSaidPast = 0;

            u64 saidKey(const char* pWhere, const char* pWhat) {
                u64 hash = 14695981039346656037ull;
                const char* const ppParts[2] = {pWhere, pWhat};
                for (const char* pPart : ppParts) {
                    for (const char* p = pPart; p != nullptr && *p != 0; p++) {
                        hash = (hash ^ static_cast<unsigned char>(*p)) * 1099511628211ull;
                    }
                    hash = hash * 1099511628211ull;  // a 0 byte between the parts
                }
                return hash | 1u;
            }

            void sayCounted(const char* pWhere, const char* pWhat) {
                const char* const pPlace = pWhere != nullptr ? pWhere : "a service";
                const u64 key = saidKey(pPlace, pWhat);
                for (u32 i = 0; i < kMostSaid; i++) {
                    u64 held = __atomic_load_n(&sSaid[i].key, __ATOMIC_ACQUIRE);
                    if (held == 0) {
                        u64 expected = 0;
                        held = __atomic_compare_exchange_n(&sSaid[i].key, &expected, key, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? key : expected;
                    }
                    if (held == key) {
                        const u64 count = __atomic_add_fetch(&sSaid[i].count, 1u, __ATOMIC_RELAXED);
                        if (powerOfTen(count)) {
                            nebula_log("LumaCore: %s: %s (%llu times)\n", pPlace, pWhat, static_cast<unsigned long long>(count));
                        }
                        return;
                    }
                }
                const u64 past = __atomic_add_fetch(&sSaidPast, 1u, __ATOMIC_RELAXED);
                if (powerOfTen(past)) {
                    nebula_log("LumaCore: %s: %s (and %llu more lines of %u kinds kept, counted together)\n", pPlace, pWhat,
                               static_cast<unsigned long long>(past), kMostSaid);
                }
            }

            // The kernel's half for providers (luma_service_provider.h).
            void kernelUnknownMod(const char* pWhere) {
                sayCounted(pWhere, "called with no mod, or one the game does not know (a bug in the caller); LUMA_E_ARGUMENT");
            }

            const char* kernelModId(const LumaMod* pMod) {
                const Record* const pRecord = recordOf(pMod);
                return pRecord != nullptr ? pRecord->id : nullptr;
            }

            s32 kernelModOrder(const LumaMod* pMod) {
                const Record* const pRecord = recordOf(pMod);
                return pRecord != nullptr ? pRecord->order : -1;
            }

            s32 kernelGranted(const LumaMod* pMod, const char* pPermission) {
                const Record* const pRecord = recordOf(pMod);
                if (pRecord == nullptr || pPermission == nullptr) {
                    return 0;
                }
                for (u32 i = 0; i < pRecord->grantedCount; i++) {
                    if (sameText(pRecord->granted[i], pPermission)) {
                        return 1;
                    }
                }
                return 0;
            }

            s32 kernelModLive(const LumaMod* pMod) {
                const Record* const pRecord = recordOf(pMod);
                return pRecord != nullptr && !isGone(pRecord) ? 1 : 0;
            }

            s32 kernelGameThread() { return onGameThread() ? 1 : 0; }

            void kernelReport(const LumaMod* pMod, const char* pWhere, u32 code, const char* const* ppArgs, u32 argCount) {
                if (pMod == nullptr) {
                    sayCounted(pWhere, "a report without its mod (a provider bug); not kept");
                    return;
                }
                // Every service row names {where} first (services-registry 11): the kernel puts the place there, so no
                // provider can leave it out and shift the rest. LumaCore.report keeps it under (file, line, code): the
                // place is "<service>.<function>", line 0; it keeps at most 8 arguments.
                const char* const pPlace = pWhere != nullptr ? pWhere : "";
                const char* ppAll[8] = {pPlace};
                u32 count = 1;
                for (u32 i = 0; ppArgs != nullptr && i < argCount && count < 8u; i++) {
                    ppAll[count++] = ppArgs[i];
                }
                core()->report(pMod, pPlace, 0, code, ppAll, count);
            }

            int32_t kernelPost(LumaEventNode* pNode) {
                if (pNode == nullptr || pNode->mod == nullptr || pNode->deliver == nullptr) {
                    sayCounted("LumaServiceKernel.post", pNode == nullptr ? "a post with no node (a provider bug)"
                                                         : (pNode->mod == nullptr ? "a post with no mod (a provider bug)"
                                                                                  : "a post with no deliver (a provider bug)"));
                    return LUMA_E_ARGUMENT;
                }
                Record* const pRecord = recordOf(pNode->mod);
                if (pRecord == nullptr || isGone(pRecord)) {
                    return LUMA_E_CANCELLED;
                }
                // Queued already: its deliver will see what arrived meanwhile.
                u32 idle = 0;
                if (!__atomic_compare_exchange_n(&pNode->state, &idle, 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                    return LUMA_OK;
                }
                LumaEventNode* pHead = __atomic_load_n(&pRecord->head, __ATOMIC_RELAXED);
                do {
                    pNode->next = pHead;
                } while (!__atomic_compare_exchange_n(&pRecord->head, &pHead, pNode, true, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
                // After the push: a frame's start that cleared the flag before this store sees the flag set again.
                __atomic_store_n(&sPosted, 1u, __ATOMIC_RELEASE);
                return LUMA_OK;
            }

            int32_t kernelCall(const LumaMod* pMod, const char* pService, LumaEventFn fn, void* pUser, const LumaEvent* pEvent) {
                if (!onGameThread()) {
                    sayCounted(pService, "called a mod's event function off the game thread (a provider bug); not called");
                    return LUMA_E_THREAD;
                }
                if (fn == nullptr || pEvent == nullptr) {
                    sayCounted(pService, fn == nullptr ? "called a mod's event function with no function; not called"
                                                       : "called a mod's event function with no event (a provider bug); not called");
                    return LUMA_E_ARGUMENT;
                }
                const Record* const pRecord = recordOf(pMod);
                if (pRecord == nullptr || isGone(pRecord)) {
                    return LUMA_E_CANCELLED;
                }
                // The guard (plan 1.6) wraps this call when it exists: a fault in fn is the mod's, caught there and named
                // "<service> event <kind>" in the mod log (services-registry 6.1, J S-7).
                fn(pUser, pEvent);
                return LUMA_OK;
            }

            void* kernelAlloc(const LumaMod* pMod, uint64_t size, uint64_t align) { return core()->alloc(pMod, size, align); }

            void kernelFree(const LumaMod* pMod, void* p) { core()->free(pMod, p); }

            const LumaServiceKernel sKernel = {
                sizeof(LumaServiceKernel), kernelModId, kernelModOrder, kernelGranted, kernelModLive, kernelGameThread,
                kernelReport,              kernelPost,  kernelCall,     kernelAlloc,   kernelFree,       kernelUnknownMod,
            };

            // Every node of the mod's stack and carried list goes to its drop: the mod is gone (game thread).
            void dropAll(Record* pRecord) {
                LumaEventNode* pCarried = pRecord->first;
                sCarried -= pRecord->carried;
                pRecord->first = nullptr;
                pRecord->last = nullptr;
                pRecord->carried = 0;
                LumaEventNode* pStack = __atomic_exchange_n(&pRecord->head, static_cast<LumaEventNode*>(nullptr), __ATOMIC_ACQUIRE);
                // Carried first, then the stack in post order: the order they would have been delivered in.
                LumaEventNode* pOrdered = nullptr;
                while (pStack != nullptr) {
                    LumaEventNode* const pNext = pStack->next;
                    pStack->next = pOrdered;
                    pOrdered = pStack;
                    pStack = pNext;
                }
                for (int list = 0; list < 2; list++) {
                    LumaEventNode* pNode = list == 0 ? pCarried : pOrdered;
                    while (pNode != nullptr) {
                        LumaEventNode* const pNext = pNode->next;
                        pNode->next = nullptr;
                        __atomic_store_n(&pNode->state, 0u, __ATOMIC_RELEASE);
                        if (pNode->drop != nullptr) {
                            pNode->drop(pNode);
                        }
                        pNode = pNext;
                    }
                }
            }

            // One mod's events this frame: what was posted is taken (post order) behind what was carried, then up to
            // LUMA_EVENTS_PER_FRAME nodes are delivered (a deliver may make several calls, as its service's contract
            // states); the rest stay carried, in order (game thread).
            void deliverMod(Record* pRecord) {
                LumaEventNode* pStack = __atomic_exchange_n(&pRecord->head, static_cast<LumaEventNode*>(nullptr), __ATOMIC_ACQUIRE);
                LumaEventNode* pOrdered = nullptr;
                LumaEventNode* pOrderedLast = pStack;  // the newest becomes the last
                u32 taken = 0;
                while (pStack != nullptr) {
                    LumaEventNode* const pNext = pStack->next;
                    pStack->next = pOrdered;
                    pOrdered = pStack;
                    pStack = pNext;
                    taken++;
                }
                if (pOrdered != nullptr) {
                    if (pRecord->last != nullptr) {
                        pRecord->last->next = pOrdered;
                    } else {
                        pRecord->first = pOrdered;
                    }
                    pRecord->last = pOrderedLast;
                    pRecord->carried += taken;
                    sCarried += taken;
                }
                for (u32 given = 0; given < LUMA_EVENTS_PER_FRAME && pRecord->first != nullptr; given++) {
                    // Taken out while one of its events ran (the guard): its gone dropped the rest.
                    if (isGone(pRecord)) {
                        return;
                    }
                    LumaEventNode* const pNode = pRecord->first;
                    pRecord->first = pNode->next;
                    if (pRecord->first == nullptr) {
                        pRecord->last = nullptr;
                    }
                    pRecord->carried--;
                    sCarried--;
                    pNode->next = nullptr;
                    // Idle before deliver: a post from here on (another thread's, or deliver's own) queues it again.
                    __atomic_store_n(&pNode->state, 0u, __ATOMIC_RELEASE);
                    pNode->deliver(pNode);
                }
            }

            void removeFromOrder(const Record* pRecord) {
                for (u32 i = 0; i < sByOrderCount; i++) {
                    if (sByOrder[i] == pRecord) {
                        for (u32 j = i + 1; j < sByOrderCount; j++) {
                            sByOrder[j - 1] = sByOrder[j];
                        }
                        sByOrderCount--;
                        return;
                    }
                }
            }

            void freeGranted(Record* pRecord) {
                for (u32 i = 0; i < pRecord->grantedCount; i++) {
                    ::free(pRecord->granted[i]);
                }
                ::free(pRecord->granted);
                pRecord->granted = nullptr;
                pRecord->grantedCount = 0;
            }

            // Every catalogued service has a table on every platform (services-registry 1, item 3): checked once, when
            // the first mod begins, after the module's and the host's registrations.
            void checkCatalogComplete() {
                for (const CatalogRow* pRow = cCatalog; pRow->name != nullptr; pRow++) {
                    bool found = false;
                    for (u32 i = 0; i < sEntryCount && !found; i++) {
                        found = sEntries[i].major == pRow->major && sameText(sEntries[i].provider->name, pRow->name);
                    }
                    if (!found) {
                        NEBULA_PANIC("LumaCore: the service %s %u.%u has no provider in this build (%s)", pRow->name, pRow->major, pRow->minor,
                                     pRow->contract);
                    }
                }
            }

            s32 provide(const LumaServiceProvider* pProvider) {
                if (!onGameThread()) {
                    NEBULA_PANIC("LumaCore: a service was provided off the game thread");
                }
                // luma_services_open first: the module's own services are registered before the host's, so mod_gone
                // reaches them last (section 8's reverse order).
                if (!sOpened) {
                    NEBULA_PANIC("LumaCore: a service was provided before luma_services_open");
                }
                const char* const pName = pProvider != nullptr && pProvider->name != nullptr ? pProvider->name : "(no name)";
                if (pProvider == nullptr || pProvider->size < kProviderLeast || pProvider->name == nullptr || pProvider->table == nullptr) {
                    NEBULA_PANIC("LumaCore: the provider of service %s is incomplete", pName);
                }
                if (sSealed) {
                    NEBULA_PANIC("LumaCore: the service %s was provided after the first mod loaded", pName);
                }
                if (!validName(pName)) {
                    NEBULA_PANIC("LumaCore: \"%s\" is not a service name", pName);
                }
                const LumaServiceTable* const pTable = pProvider->table;
                if (pTable->size < sizeof(LumaServiceTable)) {
                    NEBULA_PANIC("LumaCore: the table of service %s is %u bytes", pName, pTable->size);
                }
                const bool test = startsWith(pName, "test.");
                const CatalogRow* const pRow = test ? nullptr : catalogRow(pName, pTable->major);
                if (!test && pRow == nullptr) {
                    NEBULA_PANIC("LumaCore: the service %s %u.%u is not in the services catalog", pName, pTable->major, pTable->minor);
                }
                if (pRow != nullptr && pRow->minor != pTable->minor) {
                    NEBULA_PANIC("LumaCore: the service %s has a %u.%u table; the catalog says %u.%u", pName, pTable->major, pTable->minor, pRow->major,
                                 pRow->minor);
                }
                if (test && pProvider->permission != nullptr && !startsWith(pProvider->permission, "test.")) {
                    NEBULA_PANIC("LumaCore: the test service %s asks for the permission %s, which is not a test one", pName, pProvider->permission);
                }
                for (u32 i = 0; i < sEntryCount; i++) {
                    if (sEntries[i].major == pTable->major && sameText(sEntries[i].provider->name, pName)) {
                        NEBULA_PANIC("LumaCore: the service %s %u was provided twice", pName, pTable->major);
                    }
                }
                if (pProvider->row_count != 0 && pProvider->rows == nullptr) {
                    NEBULA_PANIC("LumaCore: the service %s gives %u rows and no table of them", pName, pProvider->row_count);
                }
                if (test && pProvider->row_count != 0) {
                    NEBULA_PANIC("LumaCore: the test service %s gives report rows; test services report through 100 to 104 only", pName);
                }
                u32 blockFirst = 0;
                u32 blockLast = 0;
                if (pProvider->row_count != 0 && !rowBlock(pName, &blockFirst, &blockLast)) {
                    NEBULA_PANIC("LumaCore: the service %s gives report rows and has no code block (services-registry 11)", pName);
                }
                for (u32 r = 0; r < pProvider->row_count; r++) {
                    const LumaServiceRow& rRow = pProvider->rows[r];
                    if (rRow.code < blockFirst || rRow.code > blockLast || rRow.id == nullptr) {
                        NEBULA_PANIC("LumaCore: the service %s's report row %u has code %u; its block is %u to %u", pName, r, rRow.code,
                                     blockFirst, blockLast);
                    }
                    const char* const* ppNames = nullptr;
                    char id[64];
                    if (rowOf(rRow.code, id, sizeof(id), &ppNames)) {
                        NEBULA_PANIC("LumaCore: the service %s's report code %u is %s's already", pName, rRow.code, id);
                    }
                    for (u32 s = 0; s < r; s++) {
                        if (pProvider->rows[s].code == rRow.code) {
                            NEBULA_PANIC("LumaCore: the service %s gives report code %u twice", pName, rRow.code);
                        }
                    }
                }
                if (sEntryCount == kMostServices) {
                    NEBULA_PANIC("LumaCore: more than %u services", kMostServices);
                }
                Entry& rEntry = sEntries[sEntryCount];
                rEntry.provider = pProvider;
                rEntry.permission = pRow != nullptr ? pRow->permission : pProvider->permission;
                rEntry.major = pTable->major;
                rEntry.minor = pTable->minor;
                __atomic_store_n(&sEntryCount, sEntryCount + 1, __ATOMIC_RELEASE);
                // `registered` is past the least size: read only when the provider's size covers it.
                if (pProvider->size >= offsetof(LumaServiceProvider, registered) + sizeof(pProvider->registered) &&
                    pProvider->registered != nullptr) {
                    pProvider->registered(pProvider->user, &sKernel);
                }
                return 0;
            }
        }

        const CatalogRow* catalogAt(u32 index) {
            const u32 count = static_cast<u32>(sizeof(cCatalog) / sizeof(cCatalog[0])) - 1u;
            return index < count ? &cCatalog[index] : nullptr;
        }

        void provideOwn() {
            // The catalog's own check (services-registry 9): names by section 2's grammar, one row per name and major, a
            // permission only the service's own name. The resolution rule against the headers' member names is the
            // crates' mirror test (it reads the headers). A bad catalog is a build fault: the game stops naming it.
            for (const CatalogRow* pRow = cCatalog; pRow->name != nullptr; pRow++) {
                if (!validName(pRow->name)) {
                    NEBULA_PANIC("LumaCore: the catalog's service \"%s\" is not a service name", pRow->name);
                }
                if (pRow->permission != nullptr && !sameText(pRow->permission, pRow->name)) {
                    NEBULA_PANIC("LumaCore: the catalog's service %s needs the permission %s; a service's permission is its own name",
                                 pRow->name, pRow->permission);
                }
                for (const CatalogRow* pOther = cCatalog; pOther != pRow; pOther++) {
                    if (pOther->major == pRow->major && sameText(pOther->name, pRow->name)) {
                        NEBULA_PANIC("LumaCore: the catalog lists the service %s %u twice", pRow->name, pRow->major);
                    }
                }
            }
            // The module's own services, each when its contract is signed: time (services-time), content.fx (effects),
            // content.model (models).
            (void)provide(luma::time::provider());
            (void)provide(luma::content::fx::provider());
            (void)provide(luma::content::model::provider());
            (void)provide(luma::ui::provider());
            (void)provide(luma::draw::light::provider());
            (void)provide(luma::draw::provider());
            (void)provide(luma::movie::provider());
            (void)provide(luma::draw::inst::provider());
            (void)provide(luma::anim::provider());
            (void)provide(luma::save::provider());
            (void)provide(luma::save::globalProvider());
            (void)provide(luma::save::gameProvider());
        }

        const void* find(const char* pName, u32 major, u32 minor, const char** ppPermission) {
            if (ppPermission != nullptr) {
                *ppPermission = nullptr;
            }
            if (pName == nullptr) {
                return nullptr;
            }
            const u32 count = __atomic_load_n(&sEntryCount, __ATOMIC_ACQUIRE);
            for (u32 i = 0; i < count; i++) {
                const Entry& rEntry = sEntries[i];
                if (rEntry.major == major && rEntry.minor >= minor && sameText(rEntry.provider->name, pName)) {
                    if (ppPermission != nullptr) {
                        *ppPermission = rEntry.permission;
                    }
                    return rEntry.provider->table;
                }
            }
            return nullptr;
        }

        void modBegin(const LumaMod* pMod, const char* pId, s32 order, const char* const* ppGranted, u32 grantedCount) {
            if (!onGameThread()) {
                NEBULA_PANIC("LumaCore: a mod's services began off the game thread");
            }
            if (pMod == nullptr) {
                NEBULA_PANIC("LumaCore: a mod's services began with no mod");
            }
            if (!sSealed) {
                sSealed = true;
                checkCatalogComplete();
            }
            if (recordOf(pMod) != nullptr) {
                NEBULA_PANIC("LumaCore: %s's services began twice", pId != nullptr ? pId : "a mod");
            }
            u32 slot = 0;
            const u32 high = __atomic_load_n(&sRecordHigh, __ATOMIC_RELAXED);
            while (slot < high && sRecords[slot].mod != nullptr) {
                slot++;
            }
            if (slot == kMostRecords) {
                NEBULA_PANIC("LumaCore: %s's services cannot begin: all %u slots hold mods that are on or waiting for their end",
                             pId != nullptr ? pId : "a mod", kMostRecords);
            }
            Record& rRecord = sRecords[slot];
            nebula_snprintf(rRecord.id, sizeof(rRecord.id), "%s", pId != nullptr ? pId : "");
            rRecord.order = order;
            rRecord.granted = nullptr;
            rRecord.grantedCount = 0;
            if (ppGranted != nullptr && grantedCount != 0) {
                rRecord.granted = static_cast<char**>(::malloc(grantedCount * sizeof(char*)));
                if (rRecord.granted == nullptr) {
                    NEBULA_PANIC("LumaCore: no memory to keep %s's permissions", rRecord.id);
                }
                for (u32 i = 0; i < grantedCount; i++) {
                    rRecord.granted[i] = keepText(ppGranted[i]);
                }
                rRecord.grantedCount = grantedCount;
            }
            __atomic_store_n(&rRecord.gone, 0u, __ATOMIC_RELAXED);
            __atomic_store_n(&rRecord.head, static_cast<LumaEventNode*>(nullptr), __ATOMIC_RELAXED);
            rRecord.first = nullptr;
            rRecord.last = nullptr;
            rRecord.carried = 0;
            // Published last: another thread that finds the mod sees its record whole.
            __atomic_store_n(&rRecord.mod, pMod, __ATOMIC_RELEASE);
            if (slot == high) {
                __atomic_store_n(&sRecordHigh, high + 1, __ATOMIC_RELEASE);
            }
            // In list order; a mod of equal order goes after those already there.
            u32 at = sByOrderCount;
            while (at > 0 && sByOrder[at - 1]->order > order) {
                sByOrder[at] = sByOrder[at - 1];
                at--;
            }
            sByOrder[at] = &rRecord;
            sByOrderCount++;
            for (u32 i = 0; i < sEntryCount; i++) {
                const LumaServiceProvider* const pProvider = sEntries[i].provider;
                if (pProvider->mod_begin != nullptr) {
                    pProvider->mod_begin(pProvider->user, pMod);
                }
            }
        }

        void modGone(const LumaMod* pMod) {
            if (!onGameThread()) {
                NEBULA_PANIC("LumaCore: a mod's services were ended off the game thread");
            }
            Record* const pRecord = recordOf(pMod);
            if (pRecord == nullptr || isGone(pRecord)) {
                return;  // refused before its services began, or gone already
            }
            __atomic_store_n(&pRecord->gone, 1u, __ATOMIC_RELEASE);
            removeFromOrder(pRecord);
            // The last registered first, so a service built on another is told before the one it uses.
            for (u32 i = sEntryCount; i-- > 0;) {
                const LumaServiceProvider* const pProvider = sEntries[i].provider;
                if (pProvider->mod_gone != nullptr) {
                    pProvider->mod_gone(pProvider->user, pMod);
                }
            }
            dropAll(pRecord);
        }

        void modEnd(const LumaMod* pMod) {
            if (!onGameThread()) {
                NEBULA_PANIC("LumaCore: a mod's services were finished off the game thread");
            }
            Record* const pRecord = recordOf(pMod);
            if (pRecord == nullptr) {
                return;  // refused before its services began
            }
            if (!isGone(pRecord)) {
                modGone(pMod);
            }
            // A post that raced the providers' mod_gone.
            dropAll(pRecord);
            for (u32 i = sEntryCount; i-- > 0;) {
                const LumaServiceProvider* const pProvider = sEntries[i].provider;
                if (pProvider->mod_end != nullptr) {
                    pProvider->mod_end(pProvider->user, pMod);
                }
            }
            // Nothing can be posted after the providers' mod_gone and the drop above: a node here is a provider that broke
            // that promise, and the slot's next begin would lose it.
            if (__atomic_load_n(&pRecord->head, __ATOMIC_ACQUIRE) != nullptr) {
                NEBULA_PANIC("LumaCore: a service posted an event for the mod %s after its services were told it is gone", pRecord->id);
            }
            // The address first (release), so no lookup finds the record; then what it holds is freed (section 8).
            __atomic_store_n(&pRecord->mod, static_cast<const LumaMod*>(nullptr), __ATOMIC_RELEASE);
            freeGranted(pRecord);
            pRecord->id[0] = 0;
            pRecord->order = -1;
        }

        bool rowOf(u32 code, char* pId, u32 size, const char* const** pppNames) {
            *pppNames = nullptr;
            for (u32 i = 0; i < sizeof(cOwnRows) / sizeof(cOwnRows[0]); i++) {
                if (cOwnRows[i].code == code) {
                    nebula_snprintf(pId, size, "%s", cOwnRows[i].id);
                    *pppNames = cOwnRows[i].names;
                    return true;
                }
            }
            const u32 count = __atomic_load_n(&sEntryCount, __ATOMIC_ACQUIRE);
            for (u32 i = 0; i < count; i++) {
                const LumaServiceProvider* const pProvider = sEntries[i].provider;
                for (u32 r = 0; r < pProvider->row_count; r++) {
                    if (pProvider->rows[r].code == code) {
                        nebula_snprintf(pId, size, "%s", pProvider->rows[r].id);
                        *pppNames = pProvider->rows[r].names;
                        return true;
                    }
                }
            }
            return false;
        }

        void frameStart() {
            // One relaxed load a frame while nobody woke it (a FORGET is the host's, from any thread).
            if (__atomic_load_n(&sSaveWake, __ATOMIC_RELAXED) != nullptr) {
                sSaveWork = __atomic_exchange_n(&sSaveWake, nullptr, __ATOMIC_ACQUIRE);
            }
            if (sSaveWork != nullptr) {
                sSaveWork();
            }
            if (sTimePost != nullptr) {
                sTimePost();
            }
            // sPosted alone: it stays 1 while nodes are carried to a later frame (set again below).
            if (__atomic_load_n(&sPosted, __ATOMIC_ACQUIRE) != 0) {
                (void)__atomic_exchange_n(&sPosted, 0u, __ATOMIC_ACQ_REL);
                // A copy of the order: a mod taken out while its events run (the guard) leaves the list meanwhile.
                const u32 count = sByOrderCount;
                for (u32 i = 0; i < count; i++) {
                    sDeliverOrder[i] = sByOrder[i];
                }
                for (u32 i = 0; i < count; i++) {
                    if (!isGone(sDeliverOrder[i])) {
                        deliverMod(sDeliverOrder[i]);
                    }
                }
                if (sCarried != 0) {
                    __atomic_store_n(&sPosted, 1u, __ATOMIC_RELEASE);
                }
            }
            if (sFrameHandler != nullptr) {
                sFrameHandler();
            }
        }

        void setTimeHooks(void (*pPost)(), void (*pAfter)()) {
            if (!onGameThread()) {
                NEBULA_PANIC("LumaCore: the time hooks were set off the game thread");
            }
            sTimePost = pPost;
            sFrameHandler = pAfter;
        }

        void setSaveHook(void (*pWork)()) {
            if (!onGameThread()) {
                NEBULA_PANIC("LumaCore: the save hook was set off the game thread");
            }
            sSaveWork = pWork;
        }

        void wakeSaveHook(void (*pWork)()) {
            __atomic_store_n(&sSaveWake, pWork, __ATOMIC_RELEASE);
        }

        const LumaServiceKernel* kernel() { return &sKernel; }

        const LumaServiceKernel* open() {
            if (!onGameThread()) {
                NEBULA_PANIC("LumaCore: the services were opened off the game thread");
            }
            if (!sOpened) {
                sOpened = true;
                provideOwn();
            }
            return &sKernel;
        }
    }
}

extern "C" {
NEBULA_GAME_API int32_t luma_provide_service(const LumaServiceProvider* provider) {
    return luma::services::provide(provider);
}

NEBULA_GAME_API const LumaServiceKernel* luma_services_open(void) {
    return luma::services::open();
}
}
