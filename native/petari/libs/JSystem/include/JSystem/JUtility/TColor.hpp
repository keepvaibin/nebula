#pragma once

#include "Inline.hpp"
#include <revolution/gx.h>

namespace JUtility {
    struct TColor : public GXColor {
    public:
        TColor(u8 r, u8 g, u8 b, u8 a);
        TColor() {
            set(0xffffffff);
        }

        TColor(u32 u32Color) {
            set(u32Color);
        }

        TColor(GXColor color) {
            set(color);
        }

        TColor& operator=(const TColor& rColor);

        void set(u8 cR, u8 cG, u8 cB, u8 cA) {
            r = cR;
            g = cG;
            b = cB;
            a = cA;
        }

#ifdef TARGET_PC
        // The u32 form is the console's: 0xRRGGBBAA.
        void set(u32 u32Color) {
            r = u8(u32Color >> 24);
            g = u8(u32Color >> 16);
            b = u8(u32Color >> 8);
            a = u8(u32Color);
        }
#else
        void set(u32 u32Color) {
            *reinterpret_cast< u32* >(&r) = u32Color;
        }
#endif

        operator u32() const {
            return toUInt32();
        }

#ifdef TARGET_PC
        u32 toUInt32() const {
            return (u32)r << 24 | (u32)g << 16 | (u32)b << 8 | a;
        }
#else
        u32 toUInt32() const {
            return *reinterpret_cast< const u32* >(&r);
        }
#endif

        void set(GXColor gxColor) {
            GXColor* temp = this;
            *temp = gxColor;
        }
    } ATTRIBUTE_ALIGN(4);
};  // namespace JUtility

inline JUtility::TColor::TColor(u8 r, u8 g, u8 b, u8 a) {
    this->r = r;
    this->g = g;
    this->b = b;
    this->a = a;
}
