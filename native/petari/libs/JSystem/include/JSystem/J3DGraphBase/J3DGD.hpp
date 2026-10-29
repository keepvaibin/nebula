#pragma once

#include <revolution/gd.h>
#include <revolution/gx.h>
#include <revolution/gx/GXVert.h>
#ifdef TARGET_PC
// One reservation per value or command (GDBase.h's __GDReserve).
inline void J3DGDWrite_u8(u8 data) {
    *__GDReserve(1) = data;
}

static inline void J3DGDWrite_u16(u16 data) {
    __GDStore16(__GDReserve(2), data);
}

static inline void J3DGDWrite_u32(u32 data) {
    __GDStore32(__GDReserve(4), data);
}
#else
inline void J3DGDWrite_u8(u8 data) {
    __GDWrite(data);
}

static inline void J3DGDWrite_u16(u16 data) {
    __GDWrite((u8)((data >> 8)));
    __GDWrite((u8)((data >> 0) & 0xFF));
}

static inline void J3DGDWrite_u32(u32 data) {
    __GDWrite((u8)((data >> 24) & 0xFF));
    __GDWrite((u8)((data >> 16) & 0xFF));
    __GDWrite((u8)((data >> 8) & 0xFF));
    __GDWrite((u8)((data >> 0) & 0xFF));
}
#endif

inline void J3DGDWrite_f32(f32 data) {
    union {
        f32 f;
        u32 u;
    } fid;
    fid.f = data;
    J3DGDWrite_u32(fid.u);
}

#ifdef TARGET_PC
static inline void J3DGDWriteBPCmd(u32 regval) {
    u8* const p = __GDReserve(5);
    p[0] = 0x61;
    __GDStore32(p + 1, regval);
}

inline void J3DGDWriteXFCmd(u16 addr, u32 val) {
    u8* const p = __GDReserve(9);
    p[0] = 0x10;
    __GDStore16(p + 1, 0);
    __GDStore16(p + 3, addr);
    __GDStore32(p + 5, val);
}

static inline void J3DGDWriteXFCmdHdr(u16 addr, u8 len) {
    u8* const p = __GDReserve(5);
    p[0] = 0x10;
    __GDStore16(p + 1, (u16)(len - 1));
    __GDStore16(p + 3, addr);
}
#else
static inline void J3DGDWriteBPCmd(u32 regval) {
    J3DGDWrite_u8(0x61);
    J3DGDWrite_u32(regval);
}

inline void J3DGDWriteXFCmd(u16 addr, u32 val) {
    J3DGDWrite_u8(0x10);
    J3DGDWrite_u16(0);
    J3DGDWrite_u16(addr);
    J3DGDWrite_u32(val);
}

static inline void J3DGDWriteXFCmdHdr(u16 addr, u8 len) {
    J3DGDWrite_u8(0x10);
    J3DGDWrite_u16(len - 1);
    J3DGDWrite_u16(addr);
}
#endif

inline void J3DGXCmd1f32ptr(f32* data) {
    GXCmd1u32(*(u32*)data);
}

inline void J3DGXCmd1f32(f32 data) {
    union {
        f32 f;
        u32 u;
    } fid;
    fid.f = data;
    GXCmd1u32(fid.u);
}

#ifdef TARGET_PC
inline void J3DGDWriteCPCmd(u8 reg, u32 value) {
    u8* const p = __GDReserve(6);
    p[0] = 8;
    p[1] = reg;
    __GDStore32(p + 2, value);
}
#else
inline void J3DGDWriteCPCmd(u8 reg, u32 value) {
    J3DGDWrite_u8(8);
    J3DGDWrite_u8(reg);
    J3DGDWrite_u32(value);
}
#endif









