#pragma once

#include <revolution/types.h>

// GameData.bin's numbers are big-endian by the file format (a Wii's and Dolphin's saves load unchanged, and 2.0's
// load in them): the save code reads and writes each field through these, at the file, never in place as a number.
namespace MR {
    inline u16 loadSaveU16(const void* pSrc) {
        const u8* p = static_cast< const u8* >(pSrc);
        return static_cast< u16 >((p[0] << 8) | p[1]);
    }

    inline u32 loadSaveU32(const void* pSrc) {
        const u8* p = static_cast< const u8* >(pSrc);
        return (static_cast< u32 >(p[0]) << 24) | (static_cast< u32 >(p[1]) << 16) | (static_cast< u32 >(p[2]) << 8) | p[3];
    }

    inline u64 loadSaveU64(const void* pSrc) {
        const u8* p = static_cast< const u8* >(pSrc);
        return (static_cast< u64 >(loadSaveU32(p)) << 32) | loadSaveU32(p + 4);
    }

    inline void storeSaveU16(void* pDst, u16 value) {
        u8* p = static_cast< u8* >(pDst);
        p[0] = static_cast< u8 >(value >> 8);
        p[1] = static_cast< u8 >(value);
    }

    inline void storeSaveU32(void* pDst, u32 value) {
        u8* p = static_cast< u8* >(pDst);
        p[0] = static_cast< u8 >(value >> 24);
        p[1] = static_cast< u8 >(value >> 16);
        p[2] = static_cast< u8 >(value >> 8);
        p[3] = static_cast< u8 >(value);
    }

    inline void storeSaveU64(void* pDst, u64 value) {
        u8* p = static_cast< u8* >(pDst);
        storeSaveU32(p, static_cast< u32 >(value >> 32));
        storeSaveU32(p + 4, static_cast< u32 >(value));
    }
};  // namespace MR
