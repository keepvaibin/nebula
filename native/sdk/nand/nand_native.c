/* The console's NAND file system natively (in place of the SDK's NAND library, ISFS and IOS). Paths are the
   console's; the host keeps the files under its NAND root in the Wii's layout and formats, so saves move between
   Nebula and a Wii unchanged. Every operation goes to the host's file worker (fs_request) without blocking the
   simulation thread: a synchronous call sleeps its thread until the completion arrives as the IPC interrupt
   (nebula_game_fs_complete), and an asynchronous one runs its callback from that interrupt, as on the console.
   Safe opens are the host's atomic replace: writes go to a temporary file that the safe close moves over the
   original. */
#include <stdatomic.h>
#include <string.h>

#include <revolution/nand.h>
#include <revolution/os.h>

#include <nebula_game_api.h>

#include "nebula_sdk.h"

const NebulaHostServices* nebula_host(void);

enum { kSlots = 32 };

typedef enum { kAfterNone, kAfterOpen, kAfterLength, kAfterSafeClose } After;

typedef struct {
    BOOL used;
    BOOL sync;
    _Atomic int done;
    s32 result;
    After after;
    NANDFileInfo* info;
    u32* length;
    NANDCallback callback;
    NANDCommandBlock* block;
    NebulaFsRequest request;
    char path[NAND_MAX_PATH];
    char path2[NAND_MAX_PATH];
} Slot;

static Slot sSlots[kSlots];
static OSThreadQueue sSyncQueue;
static BOOL sInitialized;

NEBULA_GAME_API void nebula_game_fs_complete(uint32_t slot, int32_t result) {
    if (slot < kSlots) {
        sSlots[slot].result = result;
        atomic_store_explicit(&sSlots[slot].done, 1, memory_order_release);
        nebula_raise_interrupt(__OS_INTERRUPT_PI_ACR);
    }
}

/* The operation's effect on the caller's records, then its callback (asynchronous calls) or its thread's wake-up. */
static void finish(Slot* slot) {
    const s32 result = slot->result;
    if (result >= 0) {
        switch (slot->after) {
        case kAfterOpen:
            slot->info->fileDescriptor = result;
            slot->info->origFd = result;
            break;
        case kAfterLength:
            *slot->length = (u32)result;
            break;
        default:
            break;
        }
    }
    const s32 report = (slot->after == kAfterOpen || slot->after == kAfterLength) && result > 0 ? NAND_RESULT_OK : result;
    slot->result = report;
    if (slot->sync) {
        atomic_store(&slot->done, 2);
        OSWakeupThread(&sSyncQueue);
        return;
    }
    const NANDCallback callback = slot->callback;
    NANDCommandBlock* block = slot->block;
    slot->used = FALSE;
    atomic_store(&slot->done, 0);
    if (callback != NULL) {
        callback(report, block);
    }
}

static void fs_interrupt(__OSInterrupt interrupt, OSContext* context) {
    (void)interrupt;
    (void)context;
    for (int i = 0; i < kSlots; i++) {
        if (sSlots[i].used && atomic_load_explicit(&sSlots[i].done, memory_order_acquire) == 1) {
            finish(&sSlots[i]);
        }
    }
}

static void init(void) {
    if (!sInitialized) {
        sInitialized = TRUE;
        OSInitThreadQueue(&sSyncQueue);
        __OSSetInterruptHandler(__OS_INTERRUPT_PI_ACR, fs_interrupt);
        __OSUnmaskInterrupts(OS_INTERRUPTMASK_PI_ACR);
    }
}

/* An absolute path: a relative one is under the home directory (nandGenerateAbsPath). */
static void absolute_path(char* out, const char* path) {
    if (path[0] == '/') {
        strncpy(out, path, NAND_MAX_PATH - 1);
    } else {
        strcpy(out, nebula_home_dir());
        strncat(out, "/", NAND_MAX_PATH - 1 - strlen(out));
        strncat(out, path, NAND_MAX_PATH - 1 - strlen(out));
    }
    out[NAND_MAX_PATH - 1] = '\0';
}

static Slot* claim(void) {
    init();
    const BOOL enabled = OSDisableInterrupts();
    for (int i = 0; i < kSlots; i++) {
        if (!sSlots[i].used) {
            Slot* slot = &sSlots[i];
            memset(slot, 0, sizeof(*slot));
            slot->used = TRUE;
            slot->request.fd = -1;
            OSRestoreInterrupts(enabled);
            return slot;
        }
    }
    OSRestoreInterrupts(enabled);
    nebula_fatal("more than %d NAND operations in flight", kSlots);
    return NULL;
}

static void issue(Slot* slot) {
    nebula_host()->fs_request(nebula_host()->user, &slot->request, (uint32_t)(slot - sSlots));
}

static s32 run_sync(Slot* slot) {
    slot->sync = TRUE;
    issue(slot);
    const BOOL enabled = OSDisableInterrupts();
    while (atomic_load(&slot->done) != 2) {
        OSSleepThread(&sSyncQueue);
    }
    const s32 result = slot->result;
    atomic_store(&slot->done, 0);
    slot->used = FALSE;
    OSRestoreInterrupts(enabled);
    return result;
}

static s32 run_async(Slot* slot, NANDCallback callback, NANDCommandBlock* block) {
    slot->callback = callback;
    slot->block = block;
    if (block != NULL) {
        block->callback = (void*)callback;
    }
    issue(slot);
    return NAND_RESULT_OK;
}

static Slot* path_op(u32 op, const char* path) {
    Slot* slot = claim();
    absolute_path(slot->path, path);
    slot->request.op = op;
    slot->request.path = slot->path;
    return slot;
}

static Slot* file_op(u32 op, NANDFileInfo* info) {
    Slot* slot = claim();
    slot->request.op = op;
    slot->request.fd = info->fileDescriptor;
    slot->info = info;
    return slot;
}

static void prepare_info(NANDFileInfo* info, const char* path, u8 access) {
    memset(info, 0, sizeof(*info));
    info->fileDescriptor = -1;
    info->origFd = -1;
    absolute_path(info->origPath, path);
    info->accType = access;
}

static s32 open_common(const char* path, NANDFileInfo* info, u8 access, u32 op, NANDCallback callback,
                       NANDCommandBlock* block, BOOL async) {
    prepare_info(info, path, access);
    Slot* slot = path_op(op, path);
    slot->request.mode = access;
    slot->info = info;
    slot->after = kAfterOpen;
    if (block != NULL) {
        block->fileInfo = info;
    }
    return async ? run_async(slot, callback, block) : run_sync(slot);
}

s32 NANDOpen(const char* path, NANDFileInfo* info, u8 access) {
    return open_common(path, info, access, NEBULA_FS_OPEN, NULL, NULL, FALSE);
}

s32 NANDPrivateOpen(const char* path, NANDFileInfo* info, u8 access) {
    return open_common(path, info, access, NEBULA_FS_OPEN, NULL, NULL, FALSE);
}

s32 NANDOpenAsync(const char* path, NANDFileInfo* info, u8 access, NANDCallback callback, NANDCommandBlock* block) {
    return open_common(path, info, access, NEBULA_FS_OPEN, callback, block, TRUE);
}

s32 NANDPrivateOpenAsync(const char* path, NANDFileInfo* info, const u8 access, NANDCallback callback,
                         NANDCommandBlock* block) {
    return open_common(path, info, access, NEBULA_FS_OPEN, callback, block, TRUE);
}

/* The work buffer the console copied through is not needed. */
s32 NANDPrivateSafeOpenAsync(const char* path, NANDFileInfo* info, const u8 access, void* buf, const u32 length,
                             NANDCallback callback, NANDCommandBlock* block) {
    (void)buf;
    (void)length;
    return open_common(path, info, access, NEBULA_FS_SAFE_OPEN, callback, block, TRUE);
}

s32 NANDClose(NANDFileInfo* info) {
    return run_sync(file_op(NEBULA_FS_CLOSE, info));
}

s32 NANDCloseAsync(NANDFileInfo* info, NANDCallback callback, NANDCommandBlock* block) {
    return run_async(file_op(NEBULA_FS_CLOSE, info), callback, block);
}

s32 NANDSafeCloseAsync(NANDFileInfo* info, NANDCallback callback, NANDCommandBlock* block) {
    return run_async(file_op(NEBULA_FS_SAFE_CLOSE, info), callback, block);
}

static Slot* transfer(u32 op, NANDFileInfo* info, void* buffer, u32 length) {
    Slot* slot = file_op(op, info);
    slot->request.buffer = buffer;
    slot->request.size = length;
    return slot;
}

s32 NANDRead(NANDFileInfo* info, void* buffer, u32 length) {
    return run_sync(transfer(NEBULA_FS_READ, info, buffer, length));
}

s32 NANDReadAsync(NANDFileInfo* info, void* buffer, u32 length, NANDCallback callback, NANDCommandBlock* block) {
    return run_async(transfer(NEBULA_FS_READ, info, buffer, length), callback, block);
}

s32 NANDWrite(NANDFileInfo* info, const void* buffer, u32 length) {
    return run_sync(transfer(NEBULA_FS_WRITE, info, (void*)buffer, length));
}

s32 NANDWriteAsync(NANDFileInfo* info, const void* buffer, u32 length, NANDCallback callback, NANDCommandBlock* block) {
    return run_async(transfer(NEBULA_FS_WRITE, info, (void*)buffer, length), callback, block);
}

static Slot* seek(NANDFileInfo* info, s32 offset, s32 whence) {
    Slot* slot = file_op(NEBULA_FS_SEEK, info);
    slot->request.offset = offset;
    slot->request.whence = whence;
    return slot;
}

s32 NANDSeek(NANDFileInfo* info, s32 offset, s32 whence) {
    return run_sync(seek(info, offset, whence));
}

s32 NANDSeekAsync(NANDFileInfo* info, s32 offset, s32 whence, NANDCallback callback, NANDCommandBlock* block) {
    return run_async(seek(info, offset, whence), callback, block);
}

static Slot* get_length(NANDFileInfo* info, u32* length) {
    Slot* slot = file_op(NEBULA_FS_LENGTH, info);
    slot->length = length;
    slot->after = kAfterLength;
    return slot;
}

s32 NANDGetLength(NANDFileInfo* info, u32* length) {
    return run_sync(get_length(info, length));
}

s32 NANDGetLengthAsync(NANDFileInfo* info, u32* length, NANDCallback callback, NANDCommandBlock* block) {
    return run_async(get_length(info, length), callback, block);
}

static Slot* create(u32 op, const char* path, u8 perm, u8 attr) {
    Slot* slot = path_op(op, path);
    slot->request.mode = perm;
    slot->request.attribute = attr;
    return slot;
}

s32 NANDCreate(const char* path, u8 perm, u8 attr) {
    return run_sync(create(NEBULA_FS_CREATE, path, perm, attr));
}

s32 NANDPrivateCreate(const char* path, u8 perm, u8 attr) {
    return run_sync(create(NEBULA_FS_CREATE, path, perm, attr));
}

s32 NANDPrivateCreateAsync(const char* path, u8 perm, u8 attr, NANDCallback callback, NANDCommandBlock* block) {
    return run_async(create(NEBULA_FS_CREATE, path, perm, attr), callback, block);
}

s32 NANDPrivateCreateDirAsync(const char* path, u8 perm, u8 attr, NANDCallback callback, NANDCommandBlock* block) {
    return run_async(create(NEBULA_FS_CREATE_DIR, path, perm, attr), callback, block);
}

s32 NANDDelete(const char* path) {
    return run_sync(path_op(NEBULA_FS_DELETE, path));
}

s32 NANDPrivateDelete(const char* path) {
    return run_sync(path_op(NEBULA_FS_DELETE, path));
}

s32 NANDPrivateDeleteAsync(const char* path, NANDCallback callback, NANDCommandBlock* block) {
    return run_async(path_op(NEBULA_FS_DELETE, path), callback, block);
}

/* path moves into the directory destDir under its own name. */
s32 NANDMove(const char* path, const char* destDir) {
    Slot* slot = path_op(NEBULA_FS_RENAME, path);
    const char* name = strrchr(slot->path, '/');
    absolute_path(slot->path2, destDir);
    strncat(slot->path2, name != NULL ? name : "/", NAND_MAX_PATH - 1 - strlen(slot->path2));
    slot->request.path2 = slot->path2;
    return run_sync(slot);
}

/* Free space: the host's disk is never the console's 2 GB of NAND. */
s32 NANDCheck(u32 fsBlock, u32 inode, u32* answer) {
    (void)fsBlock;
    (void)inode;
    *answer = 0;
    return NAND_RESULT_OK;
}

s32 NANDGetHomeDir(char path[NAND_MAX_PATH]) {
    strcpy(path, nebula_home_dir());
    return NAND_RESULT_OK;
}

void NANDSetUserData(NANDCommandBlock* block, void* data) {
    block->userData = data;
}

void* NANDGetUserData(const NANDCommandBlock* block) {
    return block->userData;
}

/* The save banner in the console's format: big-endian signature, flags and UTF-16 title and comment (the game's
   strings are host order). The icon speeds the game sets later go through BE<u16> (nand.h). */
static void put_be16(u8* p, u16 v) {
    p[0] = (u8)(v >> 8);
    p[1] = (u8)v;
}

static void put_comment(u8* dst, const u16* text) {
    static const u16 kSpace[] = {' ', 0};
    if (text == NULL || text[0] == 0) {
        text = kSpace;
    }
    for (int i = 0; i < NAND_BANNER_COMMENT_SIZE && text[i] != 0; i++) {
        put_be16(dst + 2 * i, text[i]);
    }
}

void NANDInitBanner(NANDBanner* banner, u32 flag, const u16* title, const u16* comment) {
    u8* b = (u8*)banner;
    memset(banner, 0, sizeof(NANDBanner));
    put_be16(b + 0, (u16)(NAND_BANNER_SIGNATURE >> 16));
    put_be16(b + 2, (u16)NAND_BANNER_SIGNATURE);
    put_be16(b + 4, (u16)(flag >> 16));
    put_be16(b + 6, (u16)flag);
    put_comment(b + 0x20, title);
    put_comment(b + 0x20 + 2 * NAND_BANNER_COMMENT_SIZE, comment);
}
