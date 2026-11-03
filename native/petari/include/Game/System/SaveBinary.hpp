#pragma once

#include "Game/System/OldSave.hpp"
#include <nebula_types.h>

// A save's scalars: host order (version 3); an old one's come through saveScalar (OldSave.hpp).
namespace MR {
    inline u16 loadSaveU16(const void* pSrc) {
        u16 value;
        __builtin_memcpy(&value, pSrc, sizeof(value));
        return saveScalar(value);
    }

    inline u32 loadSaveU32(const void* pSrc) {
        u32 value;
        __builtin_memcpy(&value, pSrc, sizeof(value));
        return saveScalar(value);
    }

    inline u64 loadSaveU64(const void* pSrc) {
        u64 value;
        __builtin_memcpy(&value, pSrc, sizeof(value));
        return saveScalar(value);
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
