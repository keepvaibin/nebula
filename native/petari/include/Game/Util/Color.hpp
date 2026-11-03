#pragma once

#include <NebulaGpu.h>

class Color8 {
public:
    Color8() {
        set(255, 255, 255, 255);
    }

    Color8(NebulaColor8 color) {
        set(color);
    }

    Color8(u8 r, u8 g, u8 b, u8 a) {
        set(r, g, b, a);
    }

    // 0xRRGGBBAA, on any machine.
    Color8(u32 color) {
        set(static_cast< u8 >(color >> 24), static_cast< u8 >(color >> 16), static_cast< u8 >(color >> 8),
            static_cast< u8 >(color));
    }

    inline operator NebulaColor8() const {
        return mRGBA;
    }

    inline operator u32() const {
        return static_cast< u32 >(r) << 24 | static_cast< u32 >(g) << 16 | static_cast< u32 >(b) << 8 | a;
    }

    inline const Color8& operator=(const NebulaColor8& rOther) {
        mRGBA = rOther;
        return *this;
    }

    inline const Color8& operator=(const Color8& rOther) {
        r = rOther.r;
        g = rOther.g;
        b = rOther.b;
        a = rOther.a;
        return *this;
    }

    void set(u8 _r, u8 _g, u8 _b, u8 _a) {
        r = _r;
        g = _g;
        b = _b;
        a = _a;
    }

    void set(NebulaColor8 color) {
        NebulaColor8* const pColor = &mRGBA;
        *pColor = color;
    }

    union {
        struct {
            u8 r;
            u8 g;
            u8 b;
            u8 a;
        };
        NebulaColor8 mRGBA;
    };
};

class Color10 {
public:
    Color10() : r(0x3FF), g(0x3FF), b(0x3FF), a(0x3FF) {
    }

    operator NebulaColor16() const {
        return mColor;
    }

    union {
        struct {
            u16 r;
            u16 g;
            u16 b;
            u16 a;
        };
        NebulaColor16 mColor;
    };
};
