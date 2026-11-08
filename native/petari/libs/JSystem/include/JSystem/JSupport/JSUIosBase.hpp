#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

enum JSUStreamSeekFrom { SEEK_FROM_START, SEEK_FROM_POSITION, SEEK_FROM_END };

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

    bool mState;
};
