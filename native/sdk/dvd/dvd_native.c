/* The disc natively (in place of the SDK's dvd.c and the drive below it). Petari's dvdfs.c (paths, open, directories,
   DVDRead/DVDReadAsync) runs as it is over a host-order copy of the disc's file table. Every read goes to the host
   asynchronously (disc_read), so a thread waiting in DVDRead sleeps while the game's other threads run, as on the
   console; the host's completion (nebula_game_disc_complete, any thread) is delivered as the disc interrupt, whose
   handler finishes the command block and runs its callback on the simulation thread. */
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include <revolution/dvd.h>
#include <revolution/os.h>

#include <nebula_game_api.h>
#include <nebula_endian.h>

#include "nebula_sdk.h"

const NebulaHostServices* nebula_host(void);
void __DVDFSInit(void);

enum {
    kStateEnd = 0,
    kStateBusy = 1,
    kStateFatal = -1,
    kStateCanceled = 10,
    kResultFatal = -1,
    kResultCanceled = -3,
    kSlots = 64,
};

static u32* sFst;
/* Wii discs store positions in 4-byte units already (dvdfs.c shifts by this). */
u32 __DVDLayoutFormat = 0;

const void* __NebulaDVDFST(void) {
    return sFst;
}

/* In-flight reads: the host completes a slot from its own thread; the disc interrupt finishes it. */
typedef struct {
    DVDCommandBlock* block; /* NULL when free (simulation thread only) */
    BOOL canceled;
    DVDCBCallback cancelCallback;
    _Atomic int done;
    s32 result;
} Slot;

static Slot sSlots[kSlots];

NEBULA_GAME_API void nebula_game_disc_complete(uint32_t slot, int32_t result) {
    if (slot < kSlots) {
        sSlots[slot].result = result;
        atomic_store_explicit(&sSlots[slot].done, 1, memory_order_release);
        nebula_raise_interrupt(__OS_INTERRUPT_PI_DI);
    }
}

/* As the console: a canceled read's own callback gets DVD_RESULT_CANCELED, then the canceler's gets 0. */
static void finish(Slot* slot) {
    DVDCommandBlock* block = slot->block;
    const s32 result = slot->result;
    const BOOL canceled = slot->canceled;
    const DVDCBCallback cancelCallback = slot->cancelCallback;
    slot->block = NULL;
    atomic_store(&slot->done, 0);
    s32 report;
    if (canceled) {
        block->state = kStateCanceled;
        report = kResultCanceled;
    } else if (result < 0) {
        block->state = kStateFatal;
        report = kResultFatal;
    } else {
        block->transferredSize = (u32)result;
        block->state = kStateEnd;
        report = result;
    }
    if (block->callback != NULL) {
        block->callback(report, block);
    }
    if (canceled && cancelCallback != NULL) {
        cancelCallback(0, block);
    }
}

/* A free slot for block (interrupts disabled). */
static int claim_slot(DVDCommandBlock* block) {
    for (int i = 0; i < kSlots; i++) {
        if (sSlots[i].block == NULL) {
            sSlots[i].block = block;
            sSlots[i].canceled = FALSE;
            sSlots[i].cancelCallback = NULL;
            return i;
        }
    }
    nebula_fatal("more than %d disc commands in flight", kSlots);
    return -1;
}

/* The disc interrupt: every completed read, in slot order. */
static void disc_interrupt(__OSInterrupt interrupt, OSContext* context) {
    (void)interrupt;
    (void)context;
    for (int i = 0; i < kSlots; i++) {
        if (sSlots[i].block != NULL && atomic_load_explicit(&sSlots[i].done, memory_order_acquire)) {
            finish(&sSlots[i]);
        }
    }
}

void DVDInit(void) {
    static BOOL initialized;
    if (initialized) {
        return;
    }
    initialized = TRUE;

    /* The file table: 12-byte big-endian entries (entry 0's third word is the entry count), then the names. */
    size_t size = 0;
    const uint8_t* fst = nebula_host()->disc_fst(nebula_host()->user, &size);
    if (fst == NULL || size < 12) {
        nebula_fatal("the disc has no file table");
    }
    const u32 entries = nebula_load_be32(fst + 8);
    if ((size_t)entries * 12 > size) {
        nebula_fatal("the disc's file table is truncated");
    }
    sFst = (u32*)malloc(size);
    if (sFst == NULL) {
        nebula_fatal("out of memory for the disc's file table");
    }
    for (u32 i = 0; i < entries * 3; i++) {
        sFst[i] = nebula_load_be32(fst + 4 * i);
    }
    memcpy((u8*)sFst + entries * 12, fst + entries * 12, size - entries * 12);
    __DVDFSInit();

    __OSSetInterruptHandler(__OS_INTERRUPT_PI_DI, disc_interrupt);
    __OSUnmaskInterrupts(OS_INTERRUPTMASK_PI_DI);
}

/* A read of length bytes at disc word offset (4-byte units, as the file table stores positions). */
BOOL DVDReadAbsAsyncPrio(DVDCommandBlock* block, void* addr, s32 length, u32 offset, DVDCBCallback callback, s32 prio) {
    (void)prio;
    const BOOL enabled = OSDisableInterrupts();
    block->command = 1; /* DVD_COMMAND_READ */
    block->state = kStateBusy;
    block->addr = addr;
    block->length = (u32)length;
    block->offset = offset;
    block->currTransferSize = 0;
    block->transferredSize = 0;
    block->callback = callback;
    const int free = claim_slot(block);
    OSRestoreInterrupts(enabled);
    nebula_host()->disc_read(nebula_host()->user, (uint64_t)offset << 2, (uint32_t)length, addr, (uint32_t)free);
    return TRUE;
}

static Slot* slot_of(const DVDCommandBlock* block) {
    for (int i = 0; i < kSlots; i++) {
        if (sSlots[i].block == block) {
            return &sSlots[i];
        }
    }
    return NULL;
}

/* A read cannot be withdrawn from the host; a canceled one completes as canceled. */
BOOL DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback callback) {
    const BOOL enabled = OSDisableInterrupts();
    Slot* slot = slot_of(block);
    if (slot != NULL) {
        slot->canceled = TRUE;
        slot->cancelCallback = callback;
    } else if (block->state != kStateFatal) {
        if (block->state == kStateBusy) {
            block->state = kStateCanceled;
        }
        if (callback != NULL) {
            callback(0, block);
        }
    }
    OSRestoreInterrupts(enabled);
    return TRUE;
}

static OSThreadQueue sCancelQueue;

static void wake_canceler(s32 result, DVDCommandBlock* block) {
    (void)result;
    (void)block;
    OSWakeupThread(&sCancelQueue);
}

s32 DVDCancel(DVDCommandBlock* block) {
    const BOOL enabled = OSDisableInterrupts();
    if (slot_of(block) != NULL) {
        OSInitThreadQueue(&sCancelQueue);
        DVDCancelAsync(block, wake_canceler);
        while (slot_of(block) != NULL) {
            OSSleepThread(&sCancelQueue);
        }
    } else if (block->state == kStateBusy) {
        block->state = kStateCanceled;
    }
    OSRestoreInterrupts(enabled);
    return 0;
}

s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) {
    return ((volatile const DVDCommandBlock*)block)->state;
}

/* Busy while any read is in flight, else ready. */
s32 DVDGetDriveStatus(void) {
    const BOOL enabled = OSDisableInterrupts();
    s32 status = kStateEnd;
    for (int i = 0; i < kSlots; i++) {
        if (sSlots[i].block != NULL) {
            status = kStateBusy;
            break;
        }
    }
    OSRestoreInterrupts(enabled);
    return status;
}

/* The disc is always present: the check succeeds, its callback running from the disc interrupt as on the console. */
BOOL DVDCheckDiskAsync(DVDCommandBlock* block, DVDCBCallback callback) {
    const BOOL enabled = OSDisableInterrupts();
    block->state = kStateBusy;
    block->callback = callback;
    const int slot = claim_slot(block);
    OSRestoreInterrupts(enabled);
    nebula_game_disc_complete((uint32_t)slot, 1);
    return TRUE;
}
