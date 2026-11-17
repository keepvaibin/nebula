#include "Game/System/LumaEvents.hpp"

#include "Game/System/NebulaMods.hpp"
#include <luma.h>
#include <luma_events.h>
#include <luma_service_provider.h>
#include <luma_services.h>
#include <nebula_mod_api.h>
#include <stddef.h>

// The events service (docs/modding/reference/services/events.md). A game event some mod watches is kept, as it was
// raised, in each watching mod's list, and the mod's node posted; at the next frame's start the kernel gives the node
// back and every kept event of the mod's kinds goes to its fn in order. Nothing of this runs for an event no mod
// watches: NebulaMods::fire tests one flag.
//
// Threads. watch, deliver and the mod's begin, gone and end run on the game thread; raise runs on whichever thread
// raises the event (the game raises them on the game thread; any other is allowed). The lists are under one spin
// lock, held only to copy a record in or to swap a list out; nothing calls a mod with it held.

static_assert(static_cast< u32 >(LUMA_EVENTS_EVENT_STAGE_FRAME) == NEBULA_MOD_EVENT_STAGE_FRAME, "the kinds are the game's events");
static_assert(static_cast< u32 >(LUMA_EVENTS_EVENT_PLAYER_ACTION) == NEBULA_MOD_EVENT_PLAYER_ACTION, "the kinds are the game's events");
static_assert(NEBULA_MOD_EVENT_COUNT == 29, "LUMA_EVENTS_ALL names every event but FRAME");
static_assert(LUMA_EVENTS_ALL == (((1u << NEBULA_MOD_EVENT_COUNT) - 1u) & ~1u), "LUMA_EVENTS_ALL names every event but FRAME");
static_assert(sizeof(((LumaEventsGame*)nullptr)->name) == LUMA_EVENTS_NAME_MOST, "the name's room is LUMA_EVENTS_NAME_MOST");

namespace luma {
    namespace events {
        namespace {
            const u32 kMostWatches = LUMA_MAX_MODS * 2u;  // as the registry's records: mods in their grace keep theirs
            const u32 kFirstRoom = 64u;                   // records a list holds before it first grows

            // One event as raised.
            struct Record {
                u32 kind;
                s32 value;
                u32 flags;
                u32 nameSize;
                u64 actor;
                u32 serial;  // sSerial when it was raised
                char name[LUMA_EVENTS_NAME_MOST];
            };

            // A list of records, in the mod's memory (the kernel's alloc counts it on the Mods page).
            struct List {
                Record* records;
                u32 count;
                u32 room;
            };

            // One mod's watch: its node first, so the kernel's node is the watch.
            struct Watch {
                LumaEventNode node;
                const LumaMod* mod;  // null: a free slot
                u32 kinds;           // 0: not watching
                LumaEventFn fn;
                void* user;
                List kept;       // raised since the last delivery (sLock)
                List giving;     // swapped out of `kept` by deliver (game thread)
                bool lost;       // an event could not be kept since the last delivery (sLock)
            };

            const LumaServiceKernel* sKernel = nullptr;
            Watch sWatches[kMostWatches];
            u32 sWatchHigh = 0;  // slots in use are below it
            u32 sLock = 0;       // atomic: the lists and `lost`
            u32 sSerial = 0;     // atomic: scenes gone since the start

            void lock() {
                while (__atomic_exchange_n(&sLock, 1u, __ATOMIC_ACQUIRE) != 0u) {
                }
            }
            void unlock() { __atomic_store_n(&sLock, 0u, __ATOMIC_RELEASE); }

            // Game thread: NebulaMods' flag of each event, from the watches (an event no mod watches costs one load).
            void kindsChanged() {
                u32 any = 0;
                for (u32 i = 0; i < sWatchHigh; i++) {
                    if (sWatches[i].mod != nullptr) {
                        any |= sWatches[i].kinds;
                    }
                }
                for (u32 event = 1; event < NEBULA_MOD_EVENT_COUNT; event++) {
                    NebulaMods::setWatched(event, (any >> event & 1u) != 0);
                }
            }

            Watch* findWatch(const LumaMod* pMod) {
                for (u32 i = 0; i < sWatchHigh; i++) {
                    if (sWatches[i].mod == pMod) {
                        return &sWatches[i];
                    }
                }
                return nullptr;
            }

            void freeList(const LumaMod* pMod, List* pList) {
                if (pList->records != nullptr) {
                    sKernel->free(pMod, pList->records);
                }
                pList->records = nullptr;
                pList->count = 0;
                pList->room = 0;
            }

            // sLock held: room for one more record in the list. False: no memory.
            bool roomFor(const LumaMod* pMod, List* pList) {
                if (pList->count < pList->room) {
                    return true;
                }
                const u32 room = pList->room == 0 ? kFirstRoom : pList->room * 2u;
                if (room <= pList->room) {
                    return false;
                }
                Record* const pNew = static_cast< Record* >(sKernel->alloc(pMod, static_cast< u64 >(room) * sizeof(Record), alignof(Record)));
                if (pNew == nullptr) {
                    return false;
                }
                for (u32 i = 0; i < pList->count; i++) {
                    pNew[i] = pList->records[i];
                }
                if (pList->records != nullptr) {
                    sKernel->free(pMod, pList->records);
                }
                pList->records = pNew;
                pList->room = room;
                return true;
            }

            // Game thread, at the frame's start: the mod's kept events, in order, to its fn.
            void deliver(LumaEventNode* pNode) {
                Watch* const pWatch = reinterpret_cast< Watch* >(pNode);
                const LumaMod* const pMod = pWatch->mod;
                if (pMod == nullptr || pWatch->fn == nullptr) {
                    return;
                }
                lock();
                const List taken = pWatch->kept;
                pWatch->kept = pWatch->giving;  // emptied below after the last delivery: its room is kept
                pWatch->kept.count = 0;
                bool lost = pWatch->lost;
                pWatch->lost = false;
                unlock();
                pWatch->giving = taken;
                const u32 serial = __atomic_load_n(&sSerial, __ATOMIC_RELAXED);
                for (u32 i = 0; i < taken.count; i++) {
                    // The fn may stop watching or change its kinds from inside a call: read both each time.
                    if (pWatch->mod != pMod || pWatch->fn == nullptr) {
                        break;
                    }
                    const Record& rRecord = taken.records[i];
                    if ((pWatch->kinds >> rRecord.kind & 1u) == 0) {
                        continue;
                    }
                    LumaEventsGame game;
                    game.size = sizeof(game);
                    game.value = rRecord.value;
                    game.flags = rRecord.flags;
                    game.name_size = rRecord.nameSize;
                    game.actor = rRecord.serial == serial ? rRecord.actor : 0;
                    __builtin_memcpy(game.name, rRecord.name, sizeof(game.name));
                    LumaEvent event;
                    __builtin_memset(&event, 0, sizeof(event));
                    event.size = sizeof(event);
                    event.kind = rRecord.kind;
                    event.status = lost ? LUMA_E_MEMORY : LUMA_OK;
                    event.flags = rRecord.flags;
                    event.data = &game;
                    event.data_size = sizeof(game);
                    event.value = static_cast< u64 >(static_cast< s64 >(rRecord.value));
                    lost = false;
                    if (sKernel->call(pMod, LUMA_EVENTS_NAME, pWatch->fn, pWatch->user, &event) != LUMA_OK) {
                        break;  // the mod is gone
                    }
                }
                if (pWatch->mod == pMod) {
                    pWatch->giving.count = 0;
                }
            }

            void drop(LumaEventNode* pNode) { (void)pNode; }  // the node is the watch's

            s32 watch(const LumaMod* pMod, u32 kinds, LumaEventFn fn, void* pUser) {
                const char* const cWhere = "events.watch";
                if (sKernel == nullptr || pMod == nullptr || sKernel->mod_id(pMod) == nullptr) {
                    if (sKernel != nullptr) {
                        sKernel->unknown_mod(cWhere);
                    }
                    return LUMA_E_ARGUMENT;
                }
                if (sKernel->mod_live(pMod) == 0) {
                    return LUMA_E_CANCELLED;
                }
                if (sKernel->game_thread() == 0) {
                    sKernel->report(pMod, cWhere, 100u, nullptr, 0u);  // Service.Thread
                    return LUMA_E_THREAD;
                }
                if ((kinds & ~static_cast< u32 >(LUMA_EVENTS_ALL)) != 0) {
                    const char* args[1] = {"kinds"};
                    sKernel->report(pMod, cWhere, 101u, args, 1u);  // Service.Argument
                    return LUMA_E_ARGUMENT;
                }
                const bool on = kinds != 0 && fn != nullptr;
                Watch* pWatch = findWatch(pMod);
                if (pWatch == nullptr && !on) {
                    return LUMA_OK;
                }
                if (pWatch == nullptr) {
                    // A free slot whose node is idle (a gone mod's node may still be the kernel's until its end).
                    for (u32 i = 0; i < kMostWatches && pWatch == nullptr; i++) {
                        if (sWatches[i].mod == nullptr && sWatches[i].node.state == 0u) {
                            pWatch = &sWatches[i];
                        }
                    }
                    if (pWatch == nullptr) {
                        return LUMA_E_MEMORY;  // more watches than twice the most mods: cannot happen (one a mod)
                    }
                    __builtin_memset(pWatch, 0, sizeof(*pWatch));
                    pWatch->node.mod = pMod;
                    pWatch->node.deliver = deliver;
                    pWatch->node.drop = drop;
                    pWatch->mod = pMod;
                    const u32 at = static_cast< u32 >(pWatch - sWatches);
                    if (at >= sWatchHigh) {
                        sWatchHigh = at + 1u;
                    }
                }
                lock();
                pWatch->kinds = on ? kinds : 0;
                pWatch->fn = on ? fn : nullptr;
                pWatch->user = on ? pUser : nullptr;
                if (!on) {
                    pWatch->kept.count = 0;  // those not yet given are dropped
                    pWatch->lost = false;
                }
                unlock();
                kindsChanged();
                return LUMA_OK;
            }

            const LumaEventsService cTable = {
                {sizeof(LumaEventsService), LUMA_EVENTS_MAJOR, LUMA_EVENTS_MINOR},
                watch,
            };

            void modGone(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                Watch* const pWatch = findWatch(pMod);
                if (pWatch == nullptr) {
                    return;
                }
                lock();
                pWatch->kinds = 0;
                pWatch->fn = nullptr;
                pWatch->user = nullptr;
                pWatch->kept.count = 0;
                unlock();
                kindsChanged();
            }

            void modEnd(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                Watch* const pWatch = findWatch(pMod);
                if (pWatch == nullptr) {
                    return;
                }
                // The registry dropped the mod's queued node before its end; no thread raises for it (kinds 0 since
                // its gone, and raise skips a watch of no kinds under the lock).
                lock();
                List kept = pWatch->kept;
                pWatch->kept = List();
                unlock();
                freeList(pMod, &kept);
                freeList(pMod, &pWatch->giving);
                pWatch->mod = nullptr;
                while (sWatchHigh > 0 && sWatches[sWatchHigh - 1].mod == nullptr) {
                    sWatchHigh--;
                }
            }

            void registered(void* pUser, const LumaServiceKernel* pKernel) {
                (void)pUser;
                sKernel = pKernel;
            }

            const LumaServiceProvider cProvider = {
                sizeof(LumaServiceProvider),
                LUMA_EVENTS_NAME,
                &cTable.base,
                nullptr,
                nullptr,
                0,
                nullptr,
                nullptr,
                modGone,
                modEnd,
                registered,
            };
        }  // namespace

        void raise(u32 event, s32 value, void* pActor, const char* pName) {
            if (sKernel == nullptr || event >= NEBULA_MOD_EVENT_COUNT) {
                return;
            }
            const u32 flags = sKernel->game_thread() != 0 ? 0u : static_cast< u32 >(LUMA_EVENTS_OTHER_THREAD);
            const u32 serial = __atomic_load_n(&sSerial, __ATOMIC_RELAXED);
            u32 nameSize = 0;
            if (pName != nullptr) {
                while (pName[nameSize] != 0) {
                    nameSize++;
                }
            }
            const u32 kept = nameSize < LUMA_EVENTS_NAME_MOST ? nameSize : LUMA_EVENTS_NAME_MOST - 1u;
            lock();
            const u32 high = sWatchHigh;
            for (u32 i = 0; i < high; i++) {
                Watch& rWatch = sWatches[i];
                if (rWatch.mod == nullptr || (rWatch.kinds >> event & 1u) == 0) {
                    continue;
                }
                if (!roomFor(rWatch.mod, &rWatch.kept)) {
                    rWatch.lost = true;
                    continue;
                }
                Record& rRecord = rWatch.kept.records[rWatch.kept.count++];
                rRecord.kind = event;
                rRecord.value = value;
                rRecord.flags = flags;
                rRecord.nameSize = nameSize;
                rRecord.actor = static_cast< u64 >(reinterpret_cast< uintptr_t >(pActor));
                rRecord.serial = serial;
                if (kept != 0) {
                    __builtin_memcpy(rRecord.name, pName, kept);
                }
                rRecord.name[kept] = 0;
                (void)sKernel->post(&rWatch.node);  // queued once until delivered
            }
            unlock();
        }

        void sceneGone() { __atomic_fetch_add(&sSerial, 1u, __ATOMIC_RELAXED); }

        const LumaServiceProvider* provider() { return &cProvider; }
    }  // namespace events
}  // namespace luma
