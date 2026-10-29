/* The GX command stream natively (docs/NATIVE_PORT.md, "Memory and the GPU's addresses"): what the console's
   write-gather pipe did. GX_WRITE_*, the GX vertex functions and GXWGFifo stores append big-endian bytes at the
   current CPU FIFO's write position: the main command ring (native/sdk/gx/gx_fifo.c), whose spans the renderer reads in
   place, or a display list being recorded into game memory.

   Space is reserved once per command, not per value (docs/NATIVE_PERFORMANCE.md): the writer's position lives in
   memory that the byte stores may alias, so each separate reservation reloads and stores it; one reservation followed
   by stores through a local pointer does not. */
#ifndef NEBULA_GX_FIFO_H
#define NEBULA_GX_FIFO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NebulaGXWriter {
    uint8_t* ptr;
    uint8_t* end;
} NebulaGXWriter;

extern NebulaGXWriter __NebulaGXWriter;

/* Fewer than `size` bytes are left: the main ring moves to fresh space (handing the renderer what is written), or a
   display list is marked overflowed and the rest diverted. Afterwards at least `size` bytes are free. */
void __NebulaGXWriterFull(unsigned size);

/* `size` bytes at the write position, which moves past them. */
static inline uint8_t* __NebulaGXReserve(unsigned size) {
    if (__builtin_expect((uintptr_t)(__NebulaGXWriter.end - __NebulaGXWriter.ptr) < size, 0)) {
        __NebulaGXWriterFull(size);
    }
    uint8_t* const p = __NebulaGXWriter.ptr;
    __NebulaGXWriter.ptr = p + size;
    return p;
}

/* Big-endian stores into reserved space. */
static inline void __NebulaGXStore16(uint8_t* p, uint16_t v) {
    v = __builtin_bswap16(v);
    __builtin_memcpy(p, &v, sizeof(v));
}

static inline void __NebulaGXStore32(uint8_t* p, uint32_t v) {
    v = __builtin_bswap32(v);
    __builtin_memcpy(p, &v, sizeof(v));
}

static inline void __NebulaGXStoreF32(uint8_t* p, float v) {
    uint32_t bits;
    __builtin_memcpy(&bits, &v, sizeof(bits));
    __NebulaGXStore32(p, bits);
}

static inline void __NebulaGXPut8(uint8_t v) {
    *__NebulaGXReserve(1) = v;
}

static inline void __NebulaGXPut16(uint16_t v) {
    __NebulaGXStore16(__NebulaGXReserve(2), v);
}

static inline void __NebulaGXPut32(uint32_t v) {
    __NebulaGXStore32(__NebulaGXReserve(4), v);
}

static inline void __NebulaGXPutF32(float v) {
    __NebulaGXStoreF32(__NebulaGXReserve(4), v);
}

/* BP register load (GX_WRITE_RA_REG): 0x61, the register word. */
static inline void __NebulaGXCmdBP(uint32_t reg) {
    uint8_t* const p = __NebulaGXReserve(5);
    p[0] = 0x61;
    __NebulaGXStore32(p + 1, reg);
}

/* CP register load (GX_WRITE_CP_STRM_REG): 0x08, the register address, the value. */
static inline void __NebulaGXCmdCP(uint8_t addr, uint32_t value) {
    uint8_t* const p = __NebulaGXReserve(6);
    p[0] = 0x08;
    p[1] = addr;
    __NebulaGXStore32(p + 2, value);
}

/* XF register load of one word (GX_WRITE_XF_REG): 0x10, the load header, the value. */
static inline void __NebulaGXCmdXF(uint32_t header, uint32_t value) {
    uint8_t* const p = __NebulaGXReserve(9);
    p[0] = 0x10;
    __NebulaGXStore32(p + 1, header);
    __NebulaGXStore32(p + 5, value);
}

/* XF load of `count` words at `addr` (0x10, then count - 1 and addr as 16-bit fields), returning where the words go. */
static inline uint8_t* __NebulaGXCmdXFWords(uint16_t addr, unsigned count) {
    uint8_t* const p = __NebulaGXReserve(5 + 4 * count);
    p[0] = 0x10;
    __NebulaGXStore16(p + 1, (uint16_t)(count - 1));
    __NebulaGXStore16(p + 3, addr);
    return p + 5;
}

/* Nonzero while host markers go into the stream: the renderer wants them (frame interpolation, motion vectors, ray
   tracing; asked at each scene's begin) and the writer is in the main ring, not a display list. */
extern uint8_t __NebulaGXMarkers;

/* A host marker (galaxy/gx/fifo_parser.h op::kHostMarker, galaxy/motion_signals.h): the game object whose GX commands
   follow, or a scene signal. 12 bytes: 0xF0, the kind, then aux, a and b big-endian. Never inside a GXBegin. */
static inline void __NebulaGXHostMarker(uint8_t kind, uint16_t aux, uint32_t a, uint32_t b) {
    if (__NebulaGXMarkers == 0) {
        return;
    }
    uint8_t* const p = __NebulaGXReserve(12);
    p[0] = 0xF0;
    p[1] = kind;
    __NebulaGXStore16(p + 2, aux);
    __NebulaGXStore32(p + 4, a);
    __NebulaGXStore32(p + 8, b);
}

/* Nonzero while the writer is in the main ring (not a display list). */
extern uint8_t __NebulaGXLinked;

/* The game's bloom through the renderer (NEBULA_OPTION_BLOOM; galaxy/motion_signals.h kMarkerBloom, its phases and
   values): a host marker whatever the draw identities, in the main ring only. */
enum {
    NEBULA_GX_MARKER_BLOOM = 15,
    NEBULA_GX_BLOOM_KEEP = 1,
    NEBULA_GX_BLOOM_NORMAL = 2,
    NEBULA_GX_BLOOM_SIMPLE = 3,
    NEBULA_GX_BLOOM_RESTORE = 4,
};

static inline void __NebulaGXBloomMarker(uint16_t phase, uint32_t a, uint32_t b) {
    if (__NebulaGXLinked == 0) {
        return;
    }
    uint8_t* const p = __NebulaGXReserve(12);
    p[0] = 0xF0;
    p[1] = NEBULA_GX_MARKER_BLOOM;
    __NebulaGXStore16(p + 2, phase);
    __NebulaGXStore32(p + 4, a);
    __NebulaGXStore32(p + 8, b);
}

/* The next draw is an EFB clear quad (MainLoopFramework::clearEfb, MR::clearZBuffer): one quad filling its viewport
   in a constant colour, writing its Z texture's depth (`z`, 24 bits) with the compare always. The renderer clears that
   depth and draws the colour without the Z texture where the draw is the one it expects; else the quad draws as
   written. With its colour (__NebulaGXClearQuadColourMarker: the TEV's only stage outputs C0 and A0, `rgba`) the
   renderer clears the colour too. In the main ring only. */
enum { NEBULA_GX_MARKER_CLEAR_QUAD = 16 };

static inline void __NebulaGXClearQuadMarkerWith(uint32_t z, uint16_t has_colour, uint32_t rgba) {
    if (__NebulaGXLinked == 0) {
        return;
    }
    uint8_t* const p = __NebulaGXReserve(12);
    p[0] = 0xF0;
    p[1] = NEBULA_GX_MARKER_CLEAR_QUAD;
    __NebulaGXStore16(p + 2, has_colour);
    __NebulaGXStore32(p + 4, z);
    __NebulaGXStore32(p + 8, rgba);
}

static inline void __NebulaGXClearQuadMarker(uint32_t z) {
    __NebulaGXClearQuadMarkerWith(z, 0, 0);
}

static inline void __NebulaGXClearQuadColourMarker(uint32_t z, uint32_t rgba) {
    __NebulaGXClearQuadMarkerWith(z, 1, rgba);
}

/* Indexed load (0x20/0x28/0x30/0x38): the opcode, the index, the address and size word. */
static inline void __NebulaGXCmdIndexed(uint8_t opcode, uint16_t index, uint16_t addr) {
    uint8_t* const p = __NebulaGXReserve(5);
    p[0] = opcode;
    __NebulaGXStore16(p + 1, index);
    __NebulaGXStore16(p + 3, addr);
}

/* `rows` x `cols` floats of a matrix with `stride` floats per row, into reserved space. */
static inline void __NebulaGXStoreMatrix(uint8_t* p, const float* m, int rows, int cols, int stride) {
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            __NebulaGXStoreF32(p, m[r * stride + c]);
            p += 4;
        }
    }
}

#ifdef __cplusplus
}

/* GXWGFifo.<type> = value, as game code writes the pipe directly: each member's assignment appends the value. */
struct NebulaWGPipe {
    struct U8 {
        void operator=(uint8_t v) const { __NebulaGXPut8(v); }
    } u8;
    struct S8 {
        void operator=(int8_t v) const { __NebulaGXPut8((uint8_t)v); }
    } s8;
    struct U16 {
        void operator=(uint16_t v) const { __NebulaGXPut16(v); }
    } u16;
    struct S16 {
        void operator=(int16_t v) const { __NebulaGXPut16((uint16_t)v); }
    } s16;
    struct U32 {
        void operator=(uint32_t v) const { __NebulaGXPut32(v); }
    } u32;
    struct S32 {
        void operator=(int32_t v) const { __NebulaGXPut32((uint32_t)v); }
    } s32;
    struct F32 {
        void operator=(float v) const { __NebulaGXPutF32(v); }
    } f32;
};
#endif

#endif
