/* GX's FIFOs natively (in place of the SDK's GXFifo.c).

   The CPU FIFO is where Nebula's FIFO writer appends (nebula_gx_fifo.h). While it is the game's main FIFO (the CPU
   and GP FIFOs are one object, as GXInit sets them), the bytes go to the command ring: host memory mapped twice in a
   row, so a command that runs past the ring's end continues in the mirror and every span is contiguous. At each flush
   (GXFlush, which draw syncs and draw done call) the span written since the last one goes to the host, which parses
   it in place and releases it when done (nebula_game_gx_release): the stream is never copied
   (docs/NATIVE_PERFORMANCE.md). While a display list is being recorded, the CPU FIFO is the game's buffer and the
   bytes go there, as on the console; an overflow is reported as the SDK reports it.

   The rest of the SDK's GX library is compiled as it is; its hardware register accesses land in the register file
   below, and the PE registers the SDK's interrupt handlers read are set when the renderer reports a token or a draw
   done. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* memfd_create */
#endif
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <revolution/gx.h>
#include <revolution/gx/GXFifo.h>
#include <revolution/os.h>

#include <nebula_game_api.h>
#include <nebula_gx_fifo.h>

#include "nebula_sdk.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

const NebulaHostServices* nebula_host(void);

_Static_assert(sizeof(__GXFifoObj) <= sizeof(GXFifoObj) && _Alignof(GXFifoObj) >= _Alignof(__GXFifoObj),
               "GXFifoObj holds GX's FIFO record");

/* The register blocks the SDK addresses (CP, PE, PI, MEM; GXInit.c's pointers): plain memory natively, set up with
   the FIFOs (GXInit's first register access follows __GXFifoInit). */
static u16 sCpRegs[0x80];
static u16 sPeRegs[0x80];
static u32 sPiRegs[0x40];
static u16 sMemRegs[0x80];
extern volatile void* __cpReg;
extern volatile void* __peReg;
extern volatile void* __piReg;
extern volatile void* __memReg;

NebulaGXWriter __NebulaGXWriter;

enum { kRingSize = 16u << 20 };

static uint8_t* sRing;          /* kRingSize bytes, mapped again at sRing + kRingSize */
static uint8_t* sSpanStart;     /* the first byte not yet handed to the host */
static uint64_t sSubmitted;     /* bytes handed to the host, ever */
static _Atomic uint64_t sReleased; /* bytes the host has released, ever */
static atomic_int sWaitingForRoom;

#if defined(_WIN32)
static HANDLE sRoomEvent;
#else
static pthread_mutex_t sRoomLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sRoomCond = PTHREAD_COND_INITIALIZER;
#endif

#if defined(_WIN32)
typedef PVOID(WINAPI* VirtualAlloc2Fn)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);
typedef PVOID(WINAPI* MapViewOfFile3Fn)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*,
                                        ULONG);

static uint8_t* map_ring(void) {
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, kRingSize, NULL);
    if (section == NULL) {
        return NULL;
    }
    uint8_t* ring = NULL;
    HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
    VirtualAlloc2Fn virtualAlloc2 = kernelbase ? (VirtualAlloc2Fn)(void*)GetProcAddress(kernelbase, "VirtualAlloc2") : NULL;
    MapViewOfFile3Fn mapViewOfFile3 = kernelbase ? (MapViewOfFile3Fn)(void*)GetProcAddress(kernelbase, "MapViewOfFile3") : NULL;
    if (virtualAlloc2 != NULL && mapViewOfFile3 != NULL) {
        /* Windows 10 1803 and later: two placeholders, each replaced by a view of the section. */
        uint8_t* p = (uint8_t*)virtualAlloc2(NULL, NULL, 2 * (SIZE_T)kRingSize, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                                              PAGE_NOACCESS, NULL, 0);
        if (p != NULL && VirtualFree(p, kRingSize, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) &&
            mapViewOfFile3(section, NULL, p, 0, kRingSize, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, NULL, 0) == p &&
            mapViewOfFile3(section, NULL, p + kRingSize, 0, kRingSize, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, NULL,
                           0) == p + kRingSize) {
            ring = p;
        }
    }
    for (int attempt = 0; ring == NULL && attempt < 16; attempt++) {
        /* Older Windows: find a free range, release it, and map both views into it before anything else takes it. */
        uint8_t* p = (uint8_t*)VirtualAlloc(NULL, 2 * (SIZE_T)kRingSize, MEM_RESERVE, PAGE_NOACCESS);
        if (p == NULL) {
            break;
        }
        VirtualFree(p, 0, MEM_RELEASE);
        uint8_t* a = (uint8_t*)MapViewOfFileEx(section, FILE_MAP_WRITE, 0, 0, kRingSize, p);
        uint8_t* b = a == p ? (uint8_t*)MapViewOfFileEx(section, FILE_MAP_WRITE, 0, 0, kRingSize, p + kRingSize) : NULL;
        if (a == p && b == p + kRingSize) {
            ring = p;
        } else {
            if (a != NULL) {
                UnmapViewOfFile(a);
            }
            if (b != NULL) {
                UnmapViewOfFile(b);
            }
        }
    }
    CloseHandle(section); /* the views keep it */
    return ring;
}
#else
static uint8_t* map_ring(void) {
    int fd = -1;
#if defined(__linux__)
    fd = memfd_create("nebula-gx-ring", MFD_CLOEXEC);
#else
    char name[64];
    snprintf(name, sizeof(name), "/nebula-gx-ring-%d", (int)getpid());
    fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    shm_unlink(name);
#endif
    if (fd < 0 || ftruncate(fd, kRingSize) != 0) {
        return NULL;
    }
    uint8_t* p = (uint8_t*)mmap(NULL, 2 * (size_t)kRingSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* ring = NULL;
    if (p != MAP_FAILED && mmap(p, kRingSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == p &&
        mmap(p + kRingSize, kRingSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == p + kRingSize) {
        ring = p;
    }
    close(fd);
    return ring;
}
#endif

/* Space the writer may use from `from` without overwriting what the host still holds. */
static size_t ring_room(const uint8_t* from) {
    const uint64_t held = sSubmitted + (uint64_t)(from - sSpanStart) - atomic_load(&sReleased);
    return (size_t)(kRingSize - held);
}

static void wait_for_room(size_t size) {
    const uint64_t started = (uint64_t)nebula_time_base();
    int warned = 0;
    for (;;) {
        atomic_store(&sWaitingForRoom, 1);
        if (ring_room(__NebulaGXWriter.ptr) >= size) {
            break;
        }
#if defined(_WIN32)
        WaitForSingleObject(sRoomEvent, 1000);
#else
        pthread_mutex_lock(&sRoomLock);
        if (ring_room(__NebulaGXWriter.ptr) < size) {
            struct timespec until;
            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_sec += 1;
            pthread_cond_timedwait(&sRoomCond, &sRoomLock, &until);
        }
        pthread_mutex_unlock(&sRoomLock);
#endif
        if (!warned && (uint64_t)nebula_time_base() - started > 5ull * OS_TIMER_CLOCK) {
            nebula_log("Nebula: GX command ring full for 5 s; the renderer has not released its spans\n");
            warned = 1;
        }
    }
    atomic_store(&sWaitingForRoom, 0);
}

NEBULA_GAME_API void nebula_game_gx_release(size_t size) {
    atomic_fetch_add(&sReleased, (uint64_t)size);
    if (atomic_load(&sWaitingForRoom)) {
#if defined(_WIN32)
        SetEvent(sRoomEvent);
#else
        pthread_mutex_lock(&sRoomLock);
        pthread_cond_signal(&sRoomCond);
        pthread_mutex_unlock(&sRoomLock);
#endif
    }
}

static __GXFifoObj sCPUFifo;
static __GXFifoObj sGPFifo;
static GXBool sCPUFifoReady;
static GXBool sGPFifoReady;
static GXBool sLinked; /* the CPU FIFO is the main FIFO: writes go to the ring */

/* Host markers (nebula_gx_fifo.h): the renderer wants them (asked at each scene's begin) and the writer is in the main
   ring. */
uint8_t __NebulaGXMarkers;
uint8_t __NebulaGXLinked;
static u8 sMarkersWanted;

static void update_markers(void) {
    __NebulaGXMarkers = (uint8_t)(sLinked && sMarkersWanted);
    __NebulaGXLinked = (uint8_t)(sLinked != 0);
}
/* Where a display list's writes go once it has overflowed (the SDK then reports the wrap and the list as empty);
   larger than any single reservation. */
static u8 sOverflowSink[4096];

static void point_writer(void) {
    if (sLinked) {
        __NebulaGXWriter.end = __NebulaGXWriter.ptr + ring_room(__NebulaGXWriter.ptr);
    } else if (sCPUFifoReady && !sCPUFifo.wrap) {
        __NebulaGXWriter.ptr = (u8*)sCPUFifo.wrPtr;
        __NebulaGXWriter.end = sCPUFifo.base + sCPUFifo.size;
    } else {
        __NebulaGXWriter.ptr = sOverflowSink;
        __NebulaGXWriter.end = sOverflowSink + sizeof(sOverflowSink);
    }
}

/* A display list's write position back into its FIFO object. */
static void save_display_list(void) {
    if (!sLinked && sCPUFifoReady && !sCPUFifo.wrap) {
        sCPUFifo.wrPtr = __NebulaGXWriter.ptr;
        sCPUFifo.count = (s32)((u8*)sCPUFifo.wrPtr - (u8*)sCPUFifo.rdPtr);
    }
}

/* Hands the span written since the last flush to the host. Only at command boundaries. */
static void submit(void) {
    if (!sLinked) {
        return;
    }
    uint8_t* const end = __NebulaGXWriter.ptr;
    const size_t size = (size_t)(end - sSpanStart);
    if (size != 0) {
        nebula_host()->gx_submit(nebula_host()->user, sSpanStart, size);
        sSubmitted += size;
    }
    /* Back into the first mapping: the same bytes. */
    sSpanStart = end >= sRing + kRingSize ? end - kRingSize : end;
    __NebulaGXWriter.ptr = sSpanStart;
    point_writer();
}

void __NebulaGXWriterFull(unsigned size) {
    if (sLinked) {
        /* Spans end only at flushes, so what the game writes between two flushes must fit the ring by itself. */
        if ((size_t)(__NebulaGXWriter.ptr - sSpanStart) + size > kRingSize) {
            nebula_fatal("the game wrote more than the GX command ring (%u MB) without a flush", kRingSize >> 20);
        }
        wait_for_room((size_t)size);
        point_writer();
        return;
    }
    /* A display list ran out of room: the console's FIFO wrapped; GXEndDisplayList sees the wrap and returns 0. */
    if (sCPUFifoReady && !sCPUFifo.wrap) {
        sCPUFifo.wrPtr = __NebulaGXWriter.ptr;
        sCPUFifo.wrap = GX_TRUE;
    }
    point_writer();
}

/* GXFlush's native half. */
void __NebulaGXFlush(void) {
    submit();
}

/* After GXSetDrawSync's or GXSetDrawDone's flush: where the token or the finish is in the stream. */
void __NebulaGXPeMarker(int finish, u16 token) {
    if (sLinked) {
        nebula_host()->gx_pe_marker(nebula_host()->user, finish, token);
    }
}

void __NebulaGXSceneMarker(u32 marker, const f32 (*view)[4]) {
    if (sLinked) {
        if (marker == NEBULA_SCENE_3D_BEGIN || marker == NEBULA_SCENE_2D_BEGIN) {
            sMarkersWanted = (u8)(nebula_host()->option(nebula_host()->user, NEBULA_OPTION_DRAW_IDENTITIES) != 0);
            update_markers();
        }
        submit();
        nebula_host()->gx_scene_marker(nebula_host()->user, marker, view != NULL ? &view[0][0] : NULL);
    }
}

void __NebulaGXStripeParticle(u32 particle, u32 ageLifetime, u8 strip) {
    if (__NebulaGXMarkers != 0) {
        nebula_host()->gx_stripe_particle(nebula_host()->user, (size_t)(__NebulaGXWriter.ptr - sSpanStart), particle,
                                          ageLifetime, strip);
    }
}

/* The draw-sync token whose callback runs (GXTokenInterruptHandler): its peeks read the EFB as the stream reached it. */
static u16 sPeekToken;

void __NebulaGXPeekToken(u16 token) {
    sPeekToken = token;
}

u32 __NebulaGXPeekEFB(u16 x, u16 y, u32 type) {
    submit();
    return nebula_host()->gx_peek_efb(nebula_host()->user, x, y, type, sPeekToken);
}

void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size) {
    __GXFifoObj* f = (__GXFifoObj*)fifo;
    f->base = (u8*)base;
    f->top = (u8*)base + size - 4;
    f->size = size;
    f->count = 0;
    f->hiWatermark = size - 16 * 1024;
    f->loWatermark = (size / 2) & ~31u;
    f->rdPtr = base;
    f->wrPtr = base;
    f->wrap = GX_FALSE;
}

void GXInitFifoPtrs(GXFifoObj* fifo, void* readPtr, void* writePtr) {
    __GXFifoObj* f = (__GXFifoObj*)fifo;
    f->rdPtr = readPtr;
    f->wrPtr = writePtr;
    f->count = (s32)((u8*)writePtr - (u8*)readPtr);
    if (f->count < 0) {
        f->count += (s32)f->size;
    }
}

void GXInitFifoLimits(GXFifoObj* fifo, u32 hi, u32 lo) {
    __GXFifoObj* f = (__GXFifoObj*)fifo;
    f->hiWatermark = hi;
    f->loWatermark = lo;
}

static void relink(void) {
    const GXBool linked = sCPUFifoReady && sGPFifoReady && sCPUFifo.base == sGPFifo.base;
    if (linked && !sLinked) {
        /* Back to the main FIFO: the ring continues where it left off. */
        __NebulaGXWriter.ptr = sSpanStart;
    }
    sLinked = linked;
    update_markers();
    point_writer();
}

void GXSetCPUFifo(const GXFifoObj* fifo) {
    const BOOL enabled = OSDisableInterrupts();
    if (sLinked) {
        submit(); /* leaving the main FIFO: what it holds goes on first */
    } else {
        save_display_list();
    }
    if (fifo == NULL) {
        sCPUFifoReady = GX_FALSE;
    } else {
        sCPUFifo = *(const __GXFifoObj*)fifo;
        sCPUFifo.wrap = GX_FALSE;
        sCPUFifoReady = GX_TRUE;
    }
    sLinked = GX_FALSE;
    relink();
    OSRestoreInterrupts(enabled);
}

void GXSetGPFifo(const GXFifoObj* fifo) {
    const BOOL enabled = OSDisableInterrupts();
    if (sLinked) {
        submit();
    } else {
        save_display_list();
    }
    if (fifo == NULL) {
        sGPFifoReady = GX_FALSE;
    } else {
        sGPFifo = *(const __GXFifoObj*)fifo;
        sGPFifoReady = GX_TRUE;
    }
    sLinked = GX_FALSE;
    relink();
    OSRestoreInterrupts(enabled);
}

/* The main FIFO's read and write pointers are the ring's (the game only compares them and passes them back as GPU
   breakpoints, which natively never hold); a display list's are its buffer's. */
GXBool GXGetCPUFifo(GXFifoObj* fifo) {
    if (!sCPUFifoReady) {
        return GX_FALSE;
    }
    save_display_list();
    __GXFifoObj* f = (__GXFifoObj*)fifo;
    *f = sCPUFifo;
    if (sLinked) {
        f->rdPtr = sSpanStart;
        f->wrPtr = __NebulaGXWriter.ptr;
        f->count = (s32)(__NebulaGXWriter.ptr - sSpanStart);
    }
    return GX_TRUE;
}

GXBool GXGetGPFifo(GXFifoObj* fifo) {
    if (!sGPFifoReady) {
        return GX_FALSE;
    }
    __GXFifoObj* f = (__GXFifoObj*)fifo;
    *f = sGPFifo;
    if (sLinked) {
        f->rdPtr = sSpanStart;
        f->wrPtr = __NebulaGXWriter.ptr;
        f->count = 0;
    }
    return GX_TRUE;
}

void GXGetFifoPtrs(const GXFifoObj* fifo, void** readPtr, void** writePtr) {
    const __GXFifoObj* f = (const __GXFifoObj*)fifo;
    *readPtr = f->rdPtr;
    *writePtr = f->wrPtr;
}

u32 GXGetFifoCount(const GXFifoObj* fifo) {
    return (u32)((const __GXFifoObj*)fifo)->count;
}

GXBool GXGetFifoWrap(const GXFifoObj* fifo) {
    return ((const __GXFifoObj*)fifo)->wrap;
}

/* The GPU natively is always ready for more: commands go to the renderer's queue. */
void GXGetGPStatus(GXBool* overhi, GXBool* underlow, GXBool* readIdle, GXBool* cmdIdle, GXBool* brkpt) {
    *overhi = GX_FALSE;
    *underlow = GX_TRUE;
    *readIdle = GX_TRUE;
    *cmdIdle = GX_TRUE;
    *brkpt = GX_FALSE;
}

GXBool __GXIsGPFifoReady(void) {
    return sGPFifoReady;
}

void __GXFifoInit(void) {
    __cpReg = sCpRegs;
    __peReg = sPeRegs;
    __piReg = sPiRegs;
    __memReg = sMemRegs;
    if (sRing == NULL) {
        sRing = map_ring();
        if (sRing == NULL) {
            nebula_fatal("could not map the GX command ring");
        }
#if defined(_WIN32)
        sRoomEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
#endif
        sSpanStart = sRing;
        __NebulaGXWriter.ptr = sRing;
    }
    memset(&sCPUFifo, 0, sizeof(sCPUFifo));
    memset(&sGPFifo, 0, sizeof(sGPFifo));
    sCPUFifoReady = GX_FALSE;
    sGPFifoReady = GX_FALSE;
    sLinked = GX_FALSE;
    update_markers();
    point_writer();
}

/* GXAbortFrame: what the GPU has not read yet is dropped, as the console's FIFO reset drops it. */
void __GXCleanGPFifo(void) {
    if (sLinked) {
        __NebulaGXWriter.ptr = sSpanStart;
        point_writer();
    }
}

/* GPU breakpoints stop the GPU at a FIFO address until disabled; the renderer runs submitted work to completion, so
   natively a breakpoint never holds it. The callback is kept for the game's bookkeeping. */
static GXBreakPtCallback sBreakPtCallback;

GXBreakPtCallback GXSetBreakPtCallback(GXBreakPtCallback cb) {
    const GXBreakPtCallback previous = sBreakPtCallback;
    sBreakPtCallback = cb;
    return previous;
}

void GXEnableBreakPt(void* breakPt) {
    (void)breakPt;
}

void GXDisableBreakPt(void) {
}

/* The renderer's PE events (any thread): the token or finish register, then the SDK's PE interrupt. */
NEBULA_GAME_API void nebula_game_gx_event(int finish, uint16_t token) {
    if (finish) {
        nebula_raise_interrupt(__OS_INTERRUPT_PI_PE_FINISH);
    } else {
        __atomic_store_n(&sPeRegs[7], token, __ATOMIC_RELEASE);
        nebula_raise_interrupt(__OS_INTERRUPT_PI_PE_TOKEN);
    }
}
