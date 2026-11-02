#pragma once

#include <NebulaGpu.h>
#include <revolution/mtx.h>
#include <revolution/os.h>

enum JSUStreamSeekFrom { SEEK_FROM_START, SEEK_FROM_POSITION, SEEK_FROM_END };

// While a save in the console's byte order is converted (SaveDataHandler): the streams and MR::loadSave* read it so.
// Everything else is host order.
inline bool gSaveConsoleOrder = false;

template < typename T >
inline T jsuSwapScalar(T value) {
    if constexpr (sizeof(T) == 2) {
        u16 v;
        __builtin_memcpy(&v, &value, 2);
        v = __builtin_bswap16(v);
        __builtin_memcpy(&value, &v, 2);
    } else if constexpr (sizeof(T) == 4) {
        u32 v;
        __builtin_memcpy(&v, &value, 4);
        v = __builtin_bswap32(v);
        __builtin_memcpy(&value, &v, 4);
    } else {
        u64 v;
        __builtin_memcpy(&v, &value, 8);
        v = __builtin_bswap64(v);
        __builtin_memcpy(&value, &v, 8);
    }
    return value;
}

// A stream's scalars, element by element.
template < typename T >
struct JSUScalar {};
#define JSU_SCALAR(T)                                                                                                                     \
    template <>                                                                                                                                      \
    struct JSUScalar< T > {                                                                                                                 \
        typedef s32 Result;                                                                                                                          \
    };
JSU_SCALAR(u16)
JSU_SCALAR(s16)
JSU_SCALAR(u32)
JSU_SCALAR(s32)
JSU_SCALAR(u64)
JSU_SCALAR(s64)
JSU_SCALAR(f32)
JSU_SCALAR(f64)
#undef JSU_SCALAR

class JSUIosBase {
public:
    enum EIoState { IO_OK = 0b00000000, IO_ERROR = 0b00000001, IO_MEMORY_ERROR = 0b00000010 };

    JSUIosBase() {
        mState = IO_OK;
    }

    virtual ~JSUIosBase() {
    }

    bool isGood() const {
        return mState == IO_OK;
    }

    void setState(EIoState state) {
        mState |= state;
    }

    void clearState(EIoState state) {
        mState &= state;
    }

    u8 getState(EIoState state) {
        return mState & state;
    }

    /* 0x4 */ bool mState;
};
