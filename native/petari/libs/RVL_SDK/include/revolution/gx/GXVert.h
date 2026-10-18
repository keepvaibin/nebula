#ifndef GXVERT_H
#define GXVERT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "revolution/base/PPCWGPipe.h"

#ifdef __MWERKS__
volatile PPCWGPipe GXWGFifo : 0xCC008000;
#else
#if defined(TARGET_PC) && defined(__cplusplus)
// Game code that stores to the pipe directly (GXWGFifo.f32 = x) appends to the command stream.
#include <nebula_gx_fifo.h>
static const NebulaWGPipe GXWGFifo = {};
#elif defined(TARGET_PC)
extern volatile PPCWGPipe GXWGFifo;
#else
volatile PPCWGPipe GXWGFifo;
#endif
#endif

#define __GXCDEF(prfx, n, t) __GXCDEF##n(prfx##n##t, t, t)
#define __GXCDEFX(func, n, t) __GXCDEF##n(func, t, t)

#ifdef TARGET_PC
#include <nebula_gx_fifo.h>
// Each call's values appended big-endian to the command stream, as the write-gather pipe took them, in one
// reservation (nebula_gx_fifo.h).
static inline void __NebulaGXStore_u8(u8* p, u8 v) {
    *p = v;
}
static inline void __NebulaGXStore_s8(u8* p, s8 v) {
    *p = (u8)v;
}
static inline void __NebulaGXStore_u16(u8* p, u16 v) {
    __NebulaGXStore16(p, v);
}
static inline void __NebulaGXStore_s16(u8* p, s16 v) {
    __NebulaGXStore16(p, (u16)v);
}
static inline void __NebulaGXStore_u32(u8* p, u32 v) {
    __NebulaGXStore32(p, v);
}
static inline void __NebulaGXStore_f32(u8* p, f32 v) {
    __NebulaGXStoreF32(p, v);
}

#define __GXCDEF1(func, ts, td)                                                                                                                      \
    static inline void func(const ts x) {                                                                                                            \
        u8* const p = __NebulaGXReserve(sizeof(td));                                                                                                 \
        __NebulaGXStore_##td(p, (td)x);                                                                                                              \
    }

#define __GXCDEF2(func, ts, td)                                                                                                                      \
    static inline void func(const ts x, const ts y) {                                                                                                \
        u8* const p = __NebulaGXReserve(2 * sizeof(td));                                                                                             \
        __NebulaGXStore_##td(p, (td)x);                                                                                                              \
        __NebulaGXStore_##td(p + sizeof(td), (td)y);                                                                                                 \
    }

#define __GXCDEF3(func, ts, td)                                                                                                                      \
    static inline void func(const ts x, const ts y, const ts z) {                                                                                    \
        u8* const p = __NebulaGXReserve(3 * sizeof(td));                                                                                             \
        __NebulaGXStore_##td(p, (td)x);                                                                                                              \
        __NebulaGXStore_##td(p + sizeof(td), (td)y);                                                                                                 \
        __NebulaGXStore_##td(p + 2 * sizeof(td), (td)z);                                                                                             \
    }

#define __GXCDEF4(func, ts, td)                                                                                                                      \
    static inline void func(const ts x, const ts y, const ts z, const ts w) {                                                                        \
        u8* const p = __NebulaGXReserve(4 * sizeof(td));                                                                                             \
        __NebulaGXStore_##td(p, (td)x);                                                                                                              \
        __NebulaGXStore_##td(p + sizeof(td), (td)y);                                                                                                 \
        __NebulaGXStore_##td(p + 2 * sizeof(td), (td)z);                                                                                             \
        __NebulaGXStore_##td(p + 3 * sizeof(td), (td)w);                                                                                             \
    }
#else
#define __NEBULA_GXWG(td, x) GXWGFifo.td = (td)(x)

#define __GXCDEF1(func, ts, td)                                                                                                                      \
    static void func(const ts x) {                                                                                                                   \
        __NEBULA_GXWG(td, x);                                                                                                                        \
        return;                                                                                                                                      \
    }

#define __GXCDEF2(func, ts, td)                                                                                                                      \
    static void func(const ts x, const ts y) {                                                                                                       \
        __NEBULA_GXWG(td, x);                                                                                                                        \
        __NEBULA_GXWG(td, y);                                                                                                                        \
        return;                                                                                                                                      \
    }

#define __GXCDEF3(func, ts, td)                                                                                                                      \
    static void func(const ts x, const ts y, const ts z) {                                                                                           \
        __NEBULA_GXWG(td, x);                                                                                                                        \
        __NEBULA_GXWG(td, y);                                                                                                                        \
        __NEBULA_GXWG(td, z);                                                                                                                        \
        return;                                                                                                                                      \
    }

#define __GXCDEF4(func, ts, td)                                                                                                                      \
    static void func(const ts x, const ts y, const ts z, const ts w) {                                                                               \
        __NEBULA_GXWG(td, x);                                                                                                                        \
        __NEBULA_GXWG(td, y);                                                                                                                        \
        __NEBULA_GXWG(td, z);                                                                                                                        \
        __NEBULA_GXWG(td, w);                                                                                                                        \
        return;                                                                                                                                      \
    }
#endif

__GXCDEF(GXCmd, 1, u8)
__GXCDEF(GXCmd, 1, u16)
__GXCDEF(GXCmd, 1, u32)
__GXCDEF(GXCmd, 1, f32)

__GXCDEF(GXPosition, 3, f32)
__GXCDEF(GXPosition, 3, u8)
__GXCDEF(GXPosition, 3, s8)
__GXCDEF(GXPosition, 3, u16)
__GXCDEF(GXPosition, 3, s16)

__GXCDEF(GXPosition, 2, f32)
__GXCDEF(GXPosition, 2, u8)
__GXCDEF(GXPosition, 2, s8)
__GXCDEF(GXPosition, 2, u16)
__GXCDEF(GXPosition, 2, s16)

__GXCDEF(GXNormal, 3, f32)

__GXCDEF(GXColor, 1, u32)
__GXCDEF(GXColor, 4, u8)

__GXCDEF(GXTexCoord, 2, u8)
__GXCDEF(GXTexCoord, 2, u16)
__GXCDEF(GXTexCoord, 2, s16)
__GXCDEF(GXTexCoord, 2, f32)

__GXCDEFX(GXPosition1x8, 1, u8)
__GXCDEFX(GXNormal1x8, 1, u8)
__GXCDEFX(GXTexCoord1x8, 1, u8)

#ifdef __cplusplus
}
#endif

#endif  // GXVERT_H
