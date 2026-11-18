#include "Game/System/LumaWeak.hpp"

#include "Game/System/LumaGrowTable.hpp"
#include "Game/System/LumaLoader.hpp"
#include <nebula_format.h>
#include <stdlib.h>

// N-4003. A handle is (generation << 32) | slot. A slot holds one object while it lives; when the object goes the slot
// is emptied, so every handle to it reads null from then; given out again, its generation moves on, so they stay null.
// Slots never move (GrowTable); the hash (object -> slot, linear probing) is the lock's, grows as objects are referred
// to and shrinks as they go. Every access is under one lock, from any thread: the game frees and destroys objects on
// several. Nothing here allocates through NebulaArena, so nothing under the lock comes back here.

namespace luma {
    namespace weak {
        namespace {
            struct Slot {
                const void* object;  // its NameObj; null: empty
                uint32_t generation;
                uint32_t nextFree;
            };

            GrowTable< Slot, 8 > sSlots;  // slot 0 is never given: handle 0 is none
            uint32_t sUsed = 0;           // slots made so far (0 counted once any is)
            uint32_t sLive = 0;           // slots holding an object; read with no lock to skip the work when 0
            uint32_t sFree = 0;           // an emptied slot to give out again, 0 for none
            uint32_t* sHash = nullptr;    // object -> slot; 0 empty
            uint32_t sHashRoom = 0;       // a power of two, at least twice sLive; 0 when nothing is held
            uint32_t sLock = 0;
            int32_t sNameObj = -2;        // NameObj's class number; -2 until asked

            constexpr uint32_t kSmallestHash = 16u;

            void pause() {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64)
                __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
                __asm__ __volatile__("yield");
#endif
            }

            void lock() {
                uint32_t expected = 0;
                while (!__atomic_compare_exchange_n(&sLock, &expected, 1u, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                    pause();
                    expected = 0;
                }
            }

            void unlock() { __atomic_store_n(&sLock, 0u, __ATOMIC_RELEASE); }

            uint32_t hashOf(const void* p) {
                uint64_t x = static_cast< uint64_t >(reinterpret_cast< uintptr_t >(p));
                x ^= x >> 33;
                x *= 0xff51afd7ed558ccdull;
                x ^= x >> 33;
                return static_cast< uint32_t >(x);
            }

            // The cell holding `o`'s slot, or the empty cell where it would go; sHash is not null.
            uint32_t* cell(const void* o) {
                const uint32_t mask = sHashRoom - 1u;
                uint32_t i = hashOf(o) & mask;
                while (sHash[i] != 0 && sSlots.at(sHash[i]).object != o) {
                    i = (i + 1u) & mask;
                }
                return &sHash[i];
            }

            // The hash with `room` cells (a power of two above sLive * 2), every held slot in it. False: no memory, the
            // old one stays.
            bool rehash(uint32_t room) {
                uint32_t* const pNew = static_cast< uint32_t* >(::calloc(room, sizeof(uint32_t)));
                if (pNew == nullptr) {
                    return false;
                }
                uint32_t* const pOld = sHash;
                sHash = pNew;
                sHashRoom = room;
                for (uint32_t s = 1; s < sUsed; s++) {
                    if (sSlots.at(s).object != nullptr) {
                        *cell(sSlots.at(s).object) = s;
                    }
                }
                ::free(pOld);
                return true;
            }

            // Takes cell `c` out, moving back the cells after it that probed past it.
            void unlink(uint32_t* c) {
                const uint32_t mask = sHashRoom - 1u;
                uint32_t i = static_cast< uint32_t >(c - sHash);
                for (uint32_t j = (i + 1u) & mask; sHash[j] != 0; j = (j + 1u) & mask) {
                    const uint32_t k = hashOf(sSlots.at(sHash[j]).object) & mask;
                    // j's home k is cyclically outside (i, j]: it may move to i
                    if ((i <= j) ? (k <= i || k > j) : (k <= i && k > j)) {
                        sHash[i] = sHash[j];
                        i = j;
                    }
                }
                sHash[i] = 0;
            }

            void empty(uint32_t* c) {
                const uint32_t s = *c;
                unlink(c);
                Slot& rSlot = sSlots.at(s);
                rSlot.object = nullptr;  // its handles read null; make() moves the generation on when it gives the slot again
                rSlot.nextFree = sFree;
                sFree = s;
                __atomic_store_n(&sLive, sLive - 1u, __ATOMIC_RELAXED);
            }

            // After objects went: the hash freed when nothing is held, halved while it holds less than an eighth.
            void shrink() {
                if (sLive == 0) {
                    ::free(sHash);
                    sHash = nullptr;
                    sHashRoom = 0;
                    return;
                }
                uint32_t room = sHashRoom;
                while (room > kSmallestHash && sLive * 8u <= room) {
                    room /= 2u;
                }
                if (room != sHashRoom) {
                    (void)rehash(room);  // no memory: the larger one stays, as correct
                }
            }

            void forget(const void* pLow, const void* pHigh) {
                if (__atomic_load_n(&sLive, __ATOMIC_RELAXED) == 0) {
                    return;
                }
                lock();
                if (sLive != 0) {
                    if (static_cast< const char* >(pHigh) == static_cast< const char* >(pLow) + 1) {
                        uint32_t* const c = cell(pLow);
                        if (*c != 0) {
                            empty(c);
                            shrink();
                        }
                    } else {
                        bool any = false;
                        for (uint32_t s = 1; s < sUsed && sLive != 0; s++) {
                            const char* const o = static_cast< const char* >(sSlots.at(s).object);
                            if (o != nullptr && o >= pLow && o < pHigh) {
                                empty(cell(o));
                                any = true;
                            }
                        }
                        if (any) {
                            shrink();
                        }
                    }
                }
                unlock();
            }

            void sayNoMemory(const LumaMod* pMod) {
                char text[160];
                nebula_snprintf(text, sizeof(text), "LumaCore: no memory for one more weak reference (%u held)", sLive);
                luma::core()->log(pMod, text);
            }
        }  // namespace

        uint64_t make(const LumaMod* pMod, const void* pObject) {
            if (pObject == nullptr) {
                return 0;
            }
            int32_t nameObj = __atomic_load_n(&sNameObj, __ATOMIC_RELAXED);
            if (nameObj == -2) {
                nameObj = luma::core()->class_of("NameObj");
                __atomic_store_n(&sNameObj, nameObj, __ATOMIC_RELAXED);
            }
            const void* const o = nameObj >= 0 ? luma::core()->as_class(pMod, pObject, nameObj) : nullptr;
            if (o == nullptr) {
                return 0;
            }
            lock();
            if (sHash != nullptr) {
                const uint32_t s = *cell(o);
                if (s != 0) {
                    const uint64_t handle = (static_cast< uint64_t >(sSlots.at(s).generation) << 32) | s;
                    unlock();
                    return handle;
                }
            }
            if ((sLive + 1u) * 2u > sHashRoom && (sHashRoom > 0x80000000u || !rehash(sHashRoom == 0 ? kSmallestHash : sHashRoom * 2u))) {
                unlock();
                sayNoMemory(pMod);
                return 0;
            }
            uint32_t s = sFree;
            if (s != 0) {
                sFree = sSlots.at(s).nextFree;
            } else {
                s = sUsed == 0 ? 1u : sUsed;
                if (s == UINT32_MAX || !sSlots.reserveTo(s + 1u)) {
                    if (sLive == 0) {
                        shrink();
                    }
                    unlock();
                    sayNoMemory(pMod);
                    return 0;
                }
                sUsed = s + 1u;
            }
            Slot& rSlot = sSlots.at(s);
            rSlot.object = o;
            if (++rSlot.generation == 0) {
                rSlot.generation = 1;
            }
            *cell(o) = s;
            __atomic_store_n(&sLive, sLive + 1u, __ATOMIC_RELAXED);
            const uint64_t handle = (static_cast< uint64_t >(rSlot.generation) << 32) | s;
            unlock();
            return handle;
        }

        const void* get(uint64_t handle) {
            const uint32_t s = static_cast< uint32_t >(handle);
            if (s == 0) {
                return nullptr;
            }
            lock();
            const void* const o = s < sUsed && sSlots.at(s).generation == static_cast< uint32_t >(handle >> 32) ? sSlots.at(s).object : nullptr;
            unlock();
            return o;
        }

        void gone(const void* pObject) { forget(pObject, static_cast< const char* >(pObject) + 1); }

        void freed(const void* pData) {
            if (pData != nullptr) {
                forget(pData, static_cast< const char* >(pData) + 1);
            }
        }

        void released(const void* pStart, const void* pEnd) {
            if (pStart < pEnd) {
                forget(pStart, pEnd);
            }
        }
    }  // namespace weak
}  // namespace luma
