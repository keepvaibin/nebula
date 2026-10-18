#ifndef GDBASE_H
#define GDBASE_H

#include "revolution/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SS_MASK(mask) ((u32)((0xfe << 24) | (mask)))

typedef struct _GDLObj {
    u8* start;
    u32 length;
    u8* ptr;
    u8* top;
} GDLObj;

typedef void (*GDOverflowCallback)(void);

extern GDLObj* __GDCurrentDL;

void GDInitGDLObj(GDLObj*, void*, u32);

void GDPadCurr32();

void GDOverflowed();

static inline void GDOverflowCheck(u32 size) {
    if (__GDCurrentDL->ptr + size > __GDCurrentDL->top) {
        GDOverflowed();
    }
}

static inline void __GDWrite(u8 data) {
    *__GDCurrentDL->ptr++ = data;
}

#ifdef TARGET_PC
// `size` bytes at the display list's write position, which moves past them: one load and store of the position per
// command or value (docs/NATIVE_PERFORMANCE.md), big-endian stores into it.
static inline u8* __GDReserve(u32 size) {
    GDLObj* const dl = __GDCurrentDL;
    u8* const p = dl->ptr;
    dl->ptr = p + size;
    return p;
}

static inline void __GDStore16(u8* p, u16 v) {
    v = __builtin_bswap16(v);
    __builtin_memcpy(p, &v, sizeof(v));
}

static inline void __GDStore32(u8* p, u32 v) {
    v = __builtin_bswap32(v);
    __builtin_memcpy(p, &v, sizeof(v));
}

static inline void GDWrite_u8(u8 data) {
    GDOverflowCheck(1);
    *__GDReserve(1) = data;
}

static inline void GDWrite_u16(u16 data) {
    GDOverflowCheck(2);
    __GDStore16(__GDReserve(2), data);
}

static inline void GDWrite_u32(u32 data) {
    GDOverflowCheck(4);
    __GDStore32(__GDReserve(4), data);
}
#else
static inline void GDWrite_u8(u8 data) {
    GDOverflowCheck(1);
    __GDWrite(data);
}

static inline void GDWrite_u16(u16 data) {
    GDOverflowCheck(2);
    __GDWrite((u8)(data >> 8));
    __GDWrite((u8)(data & 0xff));
}

static inline void GDWrite_u32(u32 data) {
    GDOverflowCheck(4);
    __GDWrite((u8)((data >> 24) & 0xff));
    __GDWrite((u8)((data >> 16) & 0xff));
    __GDWrite((u8)((data >> 8) & 0xff));
    __GDWrite((u8)((data >> 0) & 0xff));
}
#endif

static inline void GDWrite_f32(f32 data) {
    typedef union {
        f32 f;
        u32 u;
    } _FloatInt;

    _FloatInt fid;

    fid.f = data;

    GDWrite_u32(fid.u);
}

#ifdef TARGET_PC
static inline void GDWriteBPCmd(u32 regval) {
    GDOverflowCheck(5);
    u8* const p = __GDReserve(5);
    p[0] = 0x61;
    __GDStore32(p + 1, regval);
}
#else
static inline void GDWriteBPCmd(u32 regval) {
    GDWrite_u8(0x61);
    GDWrite_u32(regval);
}
#endif

static inline void GDTexCoord2f32(f32 x, f32 y) {
    GDWrite_f32(x);
    GDWrite_f32(y);
}

#ifdef TARGET_PC
static inline void GDWriteCPCmd(u8 addr, u32 val) {
    GDOverflowCheck(6);
    u8* const p = __GDReserve(6);
    p[0] = 8;
    p[1] = addr;
    __GDStore32(p + 2, val);
}

static inline void GDWriteXFCmd(u16 addr, u32 val) {
    GDOverflowCheck(9);
    u8* const p = __GDReserve(9);
    p[0] = 0x10;
    __GDStore16(p + 1, 0);
    __GDStore16(p + 3, addr);
    __GDStore32(p + 5, val);
}

static inline void GDWriteXFCmdHdr(u16 addr, u8 len) {
    GDOverflowCheck(5);
    u8* const p = __GDReserve(5);
    p[0] = 0x10;
    __GDStore16(p + 1, (u16)((len)-1));
    __GDStore16(p + 3, addr);
}
#else
static inline void GDWriteCPCmd(u8 addr, u32 val) {
    GDWrite_u8(8);
    GDWrite_u8(addr);
    GDWrite_u32(val);
}

static inline void GDWriteXFCmd(u16 addr, u32 val) {
    GDWrite_u8(0x10);
    GDWrite_u16(0);
    GDWrite_u16(addr);
    GDWrite_u32(val);
}

static inline void GDWriteXFCmdHdr(u16 addr, u8 len) {
    GDWrite_u8(0x10);
    GDWrite_u16((u16)((len)-1));
    GDWrite_u16(addr);
}
#endif

void GDOverflowed(void);

static inline void GDPosition3f32(f32 x, f32 y, f32 z) {
    GDWrite_f32(x);
    GDWrite_f32(y);
    GDWrite_f32(z);
}

static inline void GDSetCurrent(GDLObj* dl) {
    __GDCurrentDL = dl;
}

static inline u32 GDGetCurrOffset(void) {
    return (u32)(__GDCurrentDL->ptr - __GDCurrentDL->start);
}

static inline void GDSetCurrOffset(u32 offset) {
    __GDCurrentDL->ptr = __GDCurrentDL->start + offset;
}

static inline void* GDGetCurrPointer(void) {
    return (void*)__GDCurrentDL->ptr;
}

static inline u32 GDGetGDLObjOffset(const GDLObj* dl) {
    return (u32)(dl->ptr - dl->start);
}

inline void GDColor4u8(u8 r, u8 g, u8 b, u8 a) {
    GDWrite_u8(r);
    GDWrite_u8(g);
    GDWrite_u8(b);
    GDWrite_u8(a);
}

void GDFlushCurrToMem();

#ifdef __cplusplus
}
#endif

#endif  // GDBASE_H
