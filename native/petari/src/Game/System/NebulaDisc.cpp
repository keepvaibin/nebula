#include "Game/System/NebulaDisc.hpp"

#include <revolution/os.h>

#include <nebula_game_api.h>

extern "C" {
const NebulaHostServices* nebula_host(void);
void nebula_raise_interrupt(__OSInterrupt interrupt);
__attribute__((noreturn)) void nebula_fatal(const char* format, ...);
}

namespace {
    // Reads in flight: the host completes a slot from its own thread; the disc interrupt finishes it on the game's.
    struct Slot {
        NebulaDisc::Request* mRequest;  // nullptr when free (game thread only)
        bool mCanceled;
        volatile s32 mDone;
        s32 mResult;
    };

    const u32 cSlots = 64;
    Slot sSlots[cSlots];
    OSThreadQueue sCancelQueue;

    void finish(Slot* pSlot) {
        NebulaDisc::Request* pRequest = pSlot->mRequest;
        const bool canceled = pSlot->mCanceled;
        const s32 result = canceled ? NebulaDisc::cResultCanceled : pSlot->mResult < 0 ? NebulaDisc::cResultFailed : pSlot->mResult;
        pSlot->mRequest = nullptr;
        pSlot->mDone = 0;
        pRequest->mResult = result;
        pRequest->mBusy = false;

        if (pRequest->mCallback != nullptr) {
            pRequest->mCallback(result, pRequest);
        }

        if (canceled) {
            OSWakeupThread(&sCancelQueue);
        }
    }

    void onDiscInterrupt(__OSInterrupt, OSContext*) {
        for (u32 i = 0; i < cSlots; i++) {
            if (sSlots[i].mRequest != nullptr && __atomic_load_n(&sSlots[i].mDone, __ATOMIC_ACQUIRE) != 0) {
                finish(&sSlots[i]);
            }
        }
    }

    Slot* slotOf(const NebulaDisc::Request* pRequest) {
        for (u32 i = 0; i < cSlots; i++) {
            if (sSlots[i].mRequest == pRequest) {
                return &sSlots[i];
            }
        }

        return nullptr;
    }

    bool toFile(const NebulaDiscEntry& rEntry, NebulaDisc::File* pFile) {
        if (rEntry.folder != 0) {
            return false;
        }

        pFile->mEntry = rEntry.index;
        pFile->mLength = rEntry.length;
        return true;
    }

    void wakeReader(s32, NebulaDisc::Request* pRequest) {
        OSWakeupThread(static_cast< OSThreadQueue* >(pRequest->mUser));
    }
}  // namespace

extern "C" NEBULA_GAME_API void nebula_game_disc_complete(uint32_t slot, int32_t result) {
    if (slot < cSlots) {
        sSlots[slot].mResult = result;
        __atomic_store_n(&sSlots[slot].mDone, 1, __ATOMIC_RELEASE);
        nebula_raise_interrupt(__OS_INTERRUPT_PI_DI);
    }
}

namespace NebulaDisc {
    void init() {
        static bool sStarted;
        if (sStarted) {
            return;
        }
        sStarted = true;
        OSInitThreadQueue(&sCancelQueue);
        __OSSetInterruptHandler(__OS_INTERRUPT_PI_DI, onDiscInterrupt);
        __OSUnmaskInterrupts(OS_INTERRUPTMASK_PI_DI);
    }

    s32 find(const char* pPath) {
        const NebulaHostServices* const host = nebula_host();
        NebulaDiscEntry entry;
        return host->disc_find(host->user, pPath, &entry) != 0 ? entry.index : -1;
    }

    bool open(const char* pPath, File* pFile) {
        const NebulaHostServices* const host = nebula_host();
        NebulaDiscEntry entry;
        return host->disc_find(host->user, pPath, &entry) != 0 && toFile(entry, pFile);
    }

    bool open(s32 entry, File* pFile) {
        const NebulaHostServices* const host = nebula_host();
        NebulaDiscEntry found;
        return host->disc_entry(host->user, entry, &found) != 0 && toFile(found, pFile);
    }

    bool folderEntry(const char* pPath, u32 index, FolderEntry* pEntry) {
        const NebulaHostServices* const host = nebula_host();
        NebulaDiscEntry folder;
        NebulaDiscEntry child;
        if (host->disc_find(host->user, pPath, &folder) == 0 || folder.folder == 0 ||
            host->disc_child(host->user, folder.index, index, &child) == 0) {
            return false;
        }

        pEntry->mEntry = child.index;
        pEntry->mIsFolder = child.folder != 0;
        pEntry->mName = child.name;
        return true;
    }

    bool readAsync(const File* pFile, Request* pRequest, void* pDst, s32 size, s32 offset, Callback callback) {
        // The console stopped the game on a read outside the file (DVDReadAsync's checks).
        if (offset < 0 || static_cast< u32 >(offset) > pFile->mLength || size < 0 ||
            static_cast< u64 >(offset) + static_cast< u64 >(size) >= static_cast< u64 >(pFile->mLength) + 32) {
            nebula_fatal("disc read outside file %d (length %u): %d bytes at %d", pFile->mEntry, pFile->mLength, size, offset);
        }

        const BOOL enabled = OSDisableInterrupts();
        u32 slot = 0;
        while (slot < cSlots && sSlots[slot].mRequest != nullptr) {
            slot++;
        }

        if (slot == cSlots) {
            nebula_fatal("more than %u disc reads in flight", cSlots);
        }

        sSlots[slot].mRequest = pRequest;
        sSlots[slot].mCanceled = false;
        pRequest->mCallback = callback;
        pRequest->mBusy = true;
        OSRestoreInterrupts(enabled);

        const NebulaHostServices* const host = nebula_host();
        host->disc_read(host->user, pFile->mEntry, static_cast< u32 >(offset), static_cast< u32 >(size), pDst, slot);
        return true;
    }

    s32 read(const File* pFile, void* pDst, s32 size, s32 offset) {
        OSThreadQueue queue;
        OSInitThreadQueue(&queue);
        Request request;
        request.mUser = &queue;

        const BOOL enabled = OSDisableInterrupts();
        readAsync(pFile, &request, pDst, size, offset, wakeReader);
        while (request.mBusy) {
            OSSleepThread(&queue);
        }
        OSRestoreInterrupts(enabled);
        return request.mResult;
    }

    void cancel(Request* pRequest) {
        const BOOL enabled = OSDisableInterrupts();
        Slot* pSlot = slotOf(pRequest);
        if (pSlot != nullptr) {
            pSlot->mCanceled = true;
            while (slotOf(pRequest) != nullptr) {
                OSSleepThread(&sCancelQueue);
            }
        }
        OSRestoreInterrupts(enabled);
    }

    bool isBusy() {
        for (u32 i = 0; i < cSlots; i++) {
            if (sSlots[i].mRequest != nullptr) {
                return true;
            }
        }

        return false;
    }
}  // namespace NebulaDisc
