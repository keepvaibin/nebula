#include "Game/Render/RenderTexture.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/NebulaArena.hpp"
#include <nebula_cooked_texture.h>
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    void release(const void* pImage);

    // The images whose bytes an arena holds, in its memory or in a file view mapped for it: their ids go with the arena.
    class TextureList : public ArenaOwned {
    public:
        explicit TextureList(NebulaArena* pArena) : mArena(pArena), mImages(nullptr), mCount(0), mRoom(0), mNext(sFirst) {
            sFirst = this;
        }

        ~TextureList() override {
            for (u32 i = 0; i < mCount; i++) {
                release(mImages[i]);
            }
            delete[] mImages;
            TextureList** ppAt = &sFirst;
            while (*ppAt != this) {
                ppAt = &(*ppAt)->mNext;
            }
            *ppAt = mNext;
        }

        void add(const void* pImage) {
            if (mCount == mRoom) {
                const u32 room = mRoom != 0 ? mRoom * 2 : 64;
                const void** pImages = new (mArena, 0) const void*[room];
                for (u32 i = 0; i < mCount; i++) {
                    pImages[i] = mImages[i];
                }
                delete[] mImages;
                mImages = pImages;
                mRoom = room;
            }
            mImages[mCount++] = pImage;
        }

        // The list of the arena that holds pImage's bytes; null for bytes no arena holds (statics, shared views).
        static TextureList* of(const void* pImage) {
            NebulaArena* pArena = NebulaArena::findFromRoot(const_cast< void* >(pImage));
            if (pArena == nullptr) {
                pArena = GameAssets::viewArena(pImage);
            }
            if (pArena == nullptr) {
                return nullptr;
            }
            for (TextureList* pList = sFirst; pList != nullptr; pList = pList->mNext) {
                if (pList->mArena == pArena) {
                    return pList;
                }
            }
            return new (pArena, 0) TextureList(pArena);
        }

    private:
        NebulaArena* mArena;
        const void** mImages;
        u32 mCount;
        u32 mRoom;
        TextureList* mNext;
        static TextureList* sFirst;
    };

    TextureList* TextureList::sFirst = nullptr;

    // Open addressing by the image's address; an empty slot ends a search, a forgotten one is a tombstone.
    struct Entry {
        const void* mImage;
        u32 mId;
        bool mStale;
        bool mTarget;
        u32 mSize;
    };
    Entry* sTable;
    u32 sRoom;
    u32 sUsed;
    const void* const cGone = reinterpret_cast< const void* >(static_cast< uintptr_t >(1));

    u32 hashOf(const void* p) {
        const u64 v = static_cast< u64 >(reinterpret_cast< uintptr_t >(p)) >> 4;
        return static_cast< u32 >(v ^ (v >> 21)) * 2654435761u;
    }

    void grow() {
        const u32 room = sRoom != 0 ? sRoom * 2 : 8192;
        const NebulaHostServices* pHost = nebula_host();
        Entry* pNew = static_cast< Entry* >(pHost->heap_alloc(pHost->user, room * sizeof(Entry)));
        memset(pNew, 0, room * sizeof(Entry));
        u32 used = 0;
        for (u32 i = 0; i < sRoom; i++) {
            if (sTable[i].mImage != nullptr && sTable[i].mImage != cGone) {
                u32 at = hashOf(sTable[i].mImage) & (room - 1);
                while (pNew[at].mImage != nullptr) {
                    at = (at + 1) & (room - 1);
                }
                pNew[at] = sTable[i];
                used++;
            }
        }
        if (sTable != nullptr) {
            pHost->heap_free(pHost->user, sTable);
        }
        sTable = pNew;
        sRoom = room;
        sUsed = used;
    }

    Entry* find(const void* pImage, bool add) {
        if (sUsed * 2 >= sRoom) {
            grow();
        }
        u32 at = hashOf(pImage) & (sRoom - 1);
        Entry* pFree = nullptr;
        while (sTable[at].mImage != nullptr) {
            if (sTable[at].mImage == pImage) {
                return &sTable[at];
            }
            if (sTable[at].mImage == cGone && pFree == nullptr) {
                pFree = &sTable[at];
            }
            at = (at + 1) & (sRoom - 1);
        }
        if (!add) {
            return nullptr;
        }
        if (pFree == nullptr) {
            pFree = &sTable[at];
            sUsed++;
        }
        pFree->mImage = pImage;
        pFree->mId = 0;
        pFree->mStale = false;
        pFree->mTarget = false;
        pFree->mSize = 0;
        // A list is made in its arena, not in the table, so pFree stays where it is.
        if (TextureList* pList = TextureList::of(pImage)) {
            pList->add(pImage);
        }
        return pFree;
    }

    void release(const void* pImage) {
        Entry* pEntry = find(pImage, false);
        if (pEntry == nullptr) {
            return;
        }
        if (pEntry->mId != 0) {
            const NebulaHostServices* pHost = nebula_host();
            pHost->texture_free(pHost->user, pEntry->mId);
        }
        pEntry->mImage = cGone;
    }

    u32 registerRecord(const NebulaTexture* pRecord) {
        NebulaTextureImage image;
        image.bytes = pRecord;
        image.size = pRecord->size;
        image.width = pRecord->width;
        image.height = pRecord->height;
        image.format = NEBULA_TEXTURE_IMAGE;
        image.levels = pRecord->levels;
        const NebulaHostServices* pHost = nebula_host();
        return pHost->texture_register(pHost->user, &image);
    }
}  // namespace

namespace RenderTexture {
    u32 idOf(const void* pImage) {
        if (pImage == nullptr) {
            return 0;
        }
        Entry* pEntry = find(pImage, true);
        if (pEntry->mId != 0 && !pEntry->mStale) {
            return pEntry->mId;
        }
        const NebulaHostServices* pHost = nebula_host();
        const NebulaTexture* pRecord = nebula_texture_record(pImage);
        if (pRecord != nullptr) {
            if (pEntry->mId != 0) {
                pHost->texture_update(pHost->user, pEntry->mId, pRecord, pRecord->size);
            } else {
                pEntry->mId = registerRecord(pRecord);
                pEntry->mSize = pRecord->size;
            }
        } else if (pEntry->mId != 0 && pEntry->mSize != 0 && !pEntry->mTarget) {
            // A linear image (linear(): raw pixels, no record) written again since its upload (rewritten): its pixels
            // as they are now, at the size it was registered with. A mod's texture from pixels and texture_update, and
            // the draw service's missing-texture checker, are written after linear() registers them.
            pHost->texture_update(pHost->user, pEntry->mId, pImage, pEntry->mSize);
        } else if (pEntry->mId == 0) {
            pEntry->mId = pHost->texture_target(pHost->user);
            pEntry->mTarget = true;
        }
        pEntry->mStale = false;
        return pEntry->mId;
    }

    u32 linearSize(u8 format, u32 width, u32 height, u32 levels) {
        return nebula_texture_level_bytes(width, height, levels != 0 ? levels : 1, nebula_texture_linear_layout(format));
    }

    u32 linear(const void* pImage, u8 format, u32 width, u32 height, u32 levels) {
        if (pImage == nullptr || width == 0 || height == 0) {
            return 0;
        }
        Entry* pEntry = find(pImage, true);
        NebulaTextureImage image;
        image.bytes = pImage;
        image.size = linearSize(format, width, height, levels);
        image.width = width;
        image.height = height;
        image.format = format;
        image.levels = levels;
        const NebulaHostServices* pHost = nebula_host();
        if (pEntry->mId != 0 && pEntry->mSize == image.size) {
            pHost->texture_update(pHost->user, pEntry->mId, pImage, image.size);
        } else {
            if (pEntry->mId != 0) {
                pHost->texture_free(pHost->user, pEntry->mId);
            }
            pEntry->mId = pHost->texture_register(pHost->user, &image);
            pEntry->mSize = image.size;
        }
        pEntry->mStale = false;
        return pEntry->mId;
    }

    void rewritten(const void* pAddress, u32 size) {
        const uintptr_t start = reinterpret_cast< uintptr_t >(pAddress);
        for (u32 i = 0; i < sRoom; i++) {
            const uintptr_t at = reinterpret_cast< uintptr_t >(sTable[i].mImage);
            if (sTable[i].mImage != nullptr && sTable[i].mImage != cGone && !sTable[i].mTarget && at + sTable[i].mSize > start &&
                at < start + size) {
                sTable[i].mStale = true;
            }
        }
    }

    void forget(uintptr_t start, uintptr_t end) {
        const NebulaHostServices* pHost = nebula_host();
        for (u32 i = 0; i < sRoom; i++) {
            const uintptr_t at = reinterpret_cast< uintptr_t >(sTable[i].mImage);
            if (sTable[i].mImage != nullptr && sTable[i].mImage != cGone && at >= start && at < end) {
                pHost->texture_free(pHost->user, sTable[i].mId);
                sTable[i].mImage = cGone;
            }
        }
    }

#if defined(NEBULA_PLAIN_MODULE)
    namespace {
        // The range holding `at`, as an index into pRanges, or count: pByStart lists the ranges by start.
        u32 rangeOf(uintptr_t at, const Range* pRanges, const u32* pByStart, u32 count) {
            u32 low = 0;
            u32 high = count;
            while (low < high) {
                const u32 middle = low + (high - low) / 2;
                if (pRanges[pByStart[middle]].mStart <= at) {
                    low = middle + 1;
                } else {
                    high = middle;
                }
            }
            return low != 0 && at < pRanges[pByStart[low - 1]].mEnd ? pByStart[low - 1] : count;
        }

        void siftByStart(u32* pIndex, u32 root, u32 end, const Range* pRanges) {
            for (u32 child = 2 * root + 1; child < end; child = 2 * root + 1) {
                if (child + 1 < end && pRanges[pIndex[child]].mStart < pRanges[pIndex[child + 1]].mStart) {
                    child++;
                }
                if (!(pRanges[pIndex[root]].mStart < pRanges[pIndex[child]].mStart)) {
                    return;
                }
                const u32 held = pIndex[root];
                pIndex[root] = pIndex[child];
                pIndex[child] = held;
                root = child;
            }
        }
    }  // namespace

    void forget(const Range* pRanges, u32 count) {
        const NebulaHostServices* pHost = nebula_host();
        // pByStart[count], pFirst[count + 1] (where each range's images begin in pOrder), pOrder[sRoom].
        u32* const pBuffer = count > 1 && sRoom != 0 ? static_cast< u32* >(pHost->heap_alloc(pHost->user, (2 * count + 1 + sRoom) * sizeof(u32)))
                                                     : nullptr;
        if (pBuffer == nullptr) {
            for (u32 k = 0; k < count; k++) {
                forget(pRanges[k].mStart, pRanges[k].mEnd);
            }
            return;
        }
        u32* const pByStart = pBuffer;
        u32* const pFirst = pBuffer + count;
        u32* const pOrder = pFirst + count + 1;

        for (u32 k = 0; k < count; k++) {
            pByStart[k] = k;
            pFirst[k] = 0;
        }
        pFirst[count] = 0;
        for (u32 i = count / 2; i-- > 0;) {
            siftByStart(pByStart, i, count, pRanges);
        }
        for (u32 end = count; end-- > 1;) {
            const u32 held = pByStart[0];
            pByStart[0] = pByStart[end];
            pByStart[end] = held;
            siftByStart(pByStart, 0, end, pRanges);
        }

        // Count each range's images, then place them by range in table order: forget(range 0), forget(range 1), ...
        const auto live = [](const Entry& rEntry) {
            return rEntry.mImage != nullptr && rEntry.mImage != cGone;
        };
        for (u32 i = 0; i < sRoom; i++) {
            if (live(sTable[i])) {
                const u32 k = rangeOf(reinterpret_cast< uintptr_t >(sTable[i].mImage), pRanges, pByStart, count);
                if (k != count) {
                    pFirst[k + 1]++;
                }
            }
        }
        for (u32 k = 0; k < count; k++) {
            pFirst[k + 1] += pFirst[k];
        }
        const u32 total = pFirst[count];
        for (u32 i = 0; i < sRoom; i++) {
            if (live(sTable[i])) {
                const u32 k = rangeOf(reinterpret_cast< uintptr_t >(sTable[i].mImage), pRanges, pByStart, count);
                if (k != count) {
                    pOrder[pFirst[k]++] = i;
                }
            }
        }
        for (u32 n = 0; n < total; n++) {
            Entry& rEntry = sTable[pOrder[n]];
            pHost->texture_free(pHost->user, rEntry.mId);
            rEntry.mImage = cGone;
        }
        pHost->heap_free(pHost->user, pBuffer);
    }
#endif
}  // namespace RenderTexture
