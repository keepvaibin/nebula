#pragma once

#include <JSystem/JSupport/JSUIosBase.hpp>

// A save's scalars: host order (version 3). A version 2 file, in the console's byte order (a Wii save, an older
// install's), is read through gSaveConsoleOrder once and converted (SaveDataHandler).
namespace MR {
    inline u16 loadSaveU16(const void* pSrc) {
        u16 value;
        __builtin_memcpy(&value, pSrc, sizeof(value));
        return gSaveConsoleOrder ? __builtin_bswap16(value) : value;
    }

    inline u32 loadSaveU32(const void* pSrc) {
        u32 value;
        __builtin_memcpy(&value, pSrc, sizeof(value));
        return gSaveConsoleOrder ? __builtin_bswap32(value) : value;
    }

    inline u64 loadSaveU64(const void* pSrc) {
        u64 value;
        __builtin_memcpy(&value, pSrc, sizeof(value));
        return gSaveConsoleOrder ? __builtin_bswap64(value) : value;
    }

    inline void storeSaveU16(void* pDst, u16 value) {
        __builtin_memcpy(pDst, &value, sizeof(value));
    }

    inline void storeSaveU32(void* pDst, u32 value) {
        __builtin_memcpy(pDst, &value, sizeof(value));
    }

    inline void storeSaveU64(void* pDst, u64 value) {
        __builtin_memcpy(pDst, &value, sizeof(value));
    }
};  // namespace MR
