#pragma once

#include <revolution.h>

enum JSUStreamSeekFrom { SEEK_FROM_START, SEEK_FROM_POSITION, SEEK_FROM_END };

#ifdef TARGET_PC
// Natively a scalar a stream reads or writes through its own type (read(&u16Value, 2), writeU32) travels big-endian,
// as on the console: the streams carry save data and other console-format files. These are the element types swapped;
// anything else (bytes, structs, void*) passes through unchanged.
template < typename T >
struct JSUBigEndianScalar {};
#define JSU_BIG_ENDIAN_SCALAR(T)                                                                                                                     \
    template <>                                                                                                                                      \
    struct JSUBigEndianScalar< T > {                                                                                                                 \
        typedef s32 Result;                                                                                                                          \
    };
JSU_BIG_ENDIAN_SCALAR(u16)
JSU_BIG_ENDIAN_SCALAR(s16)
JSU_BIG_ENDIAN_SCALAR(u32)
JSU_BIG_ENDIAN_SCALAR(s32)
JSU_BIG_ENDIAN_SCALAR(u64)
JSU_BIG_ENDIAN_SCALAR(s64)
JSU_BIG_ENDIAN_SCALAR(f32)
JSU_BIG_ENDIAN_SCALAR(f64)
#undef JSU_BIG_ENDIAN_SCALAR
#endif

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
