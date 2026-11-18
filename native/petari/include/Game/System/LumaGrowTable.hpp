#pragma once

// A table of entries that grows as far as memory lets it and never moves an entry: the kernel's tables of classes,
// aliases, textures, mods' records and clocks (N-3008). Entries live in chunks of 2^kChunkBits, found through a directory
// of chunk pointers; a full directory is replaced by one twice its size, and the old one stays allocated, since a reader
// on another thread may still hold it (the directories kept add up to less than the last one).
//
// One writer at a time (the caller's lock or thread): reserve(i) makes entry i, zeroed, for every i up to the next
// unreserved one. Readers on any thread: at(i) for an i the reader learned after reserve(i) returned (from a count the
// writer stored with release and the reader loaded with acquire, or under the writer's lock). Entries are plain data:
// calloc'd, never constructed or destroyed.

#include <stdint.h>
#include <stdlib.h>

namespace luma {
    template <class T, uint32_t kChunkBits = 6u> class GrowTable {
    public:
        static constexpr uint32_t kChunk = 1u << kChunkBits;

        GrowTable() = default;
        GrowTable(const GrowTable&) = delete;
        GrowTable& operator=(const GrowTable&) = delete;

        // Entry i, reserved before (see above).
        T& at(uint32_t i) const {
            T* const* const pDir = __atomic_load_n(&mDir, __ATOMIC_ACQUIRE);
            T* const pChunk = __atomic_load_n(&pDir[i >> kChunkBits], __ATOMIC_ACQUIRE);
            return pChunk[i & (kChunk - 1u)];
        }

        // Entry i, zeroed when new; i is at most the number reserved so far. Null: no memory (nothing changed).
        T* reserve(uint32_t i) {
            if (i < mReserved) {
                return &at(i);
            }
            if (i != mReserved || mReserved == UINT32_MAX) {
                return nullptr;
            }
            const uint32_t c = i >> kChunkBits;
            if ((i & (kChunk - 1u)) == 0u) {
                if (c == mDirRoom) {
                    const uint32_t room = mDirRoom == 0u ? 4u : mDirRoom * 2u;
                    if (room <= mDirRoom) {
                        return nullptr;
                    }
                    T** const pNew = static_cast<T**>(::calloc(room, sizeof(T*)));
                    if (pNew == nullptr) {
                        return nullptr;
                    }
                    for (uint32_t k = 0; k < mDirRoom; k++) {
                        pNew[k] = mDir[k];
                    }
                    T* const pChunk = static_cast<T*>(::calloc(kChunk, sizeof(T)));
                    if (pChunk == nullptr) {
                        ::free(pNew);
                        return nullptr;
                    }
                    pNew[c] = pChunk;
                    // The old directory stays: a reader may hold it. Every entry it reaches is the same in the new one.
                    __atomic_store_n(&mDir, pNew, __ATOMIC_RELEASE);
                    mDirRoom = room;
                } else {
                    T* const pChunk = static_cast<T*>(::calloc(kChunk, sizeof(T)));
                    if (pChunk == nullptr) {
                        return nullptr;
                    }
                    __atomic_store_n(&mDir[c], pChunk, __ATOMIC_RELEASE);
                }
            }
            mReserved = i + 1u;
            return &mDir[c][i & (kChunk - 1u)];
        }

        // Entries up to n reserved, each new one zeroed. False: no memory (those reserved so far stay).
        bool reserveTo(uint32_t n) {
            while (mReserved < n) {
                if (reserve(mReserved) == nullptr) {
                    return false;
                }
            }
            return true;
        }

        // Entries reserved (the writer's view).
        uint32_t reserved() const { return mReserved; }

    private:
        T** mDir = nullptr;
        uint32_t mDirRoom = 0;
        uint32_t mReserved = 0;
    };
}  // namespace luma
