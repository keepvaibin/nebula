#pragma once

#include <NebulaGpu.h>

namespace JUtility {
    struct TColor : public NebulaColor8 {
    public:
        TColor(u8 r, u8 g, u8 b, u8 a);
        TColor() {
            set(0xffffffff);
        }

        TColor(u32 u32Color) {
            set(u32Color);
        }

        TColor(NebulaColor8 color) {
            set(color);
        }

        TColor& operator=(const TColor& rColor) {
            static_cast< NebulaColor8& >(*this) = rColor;
            return *this;
        }

        void set(u8 cR, u8 cG, u8 cB, u8 cA) {
            r = cR;
            g = cG;
            b = cB;
            a = cA;
        }

        // The u32 form is 0xRRGGBBAA.
        void set(u32 u32Color) {
            r = u8(u32Color >> 24);
            g = u8(u32Color >> 16);
            b = u8(u32Color >> 8);
            a = u8(u32Color);
        }

        operator u32() const {
            return toUInt32();
        }

        u32 toUInt32() const {
            return (u32)r << 24 | (u32)g << 16 | (u32)b << 8 | a;
        }

        void set(NebulaColor8 color) {
            NebulaColor8* temp = this;
            *temp = color;
        }
    } ATTRIBUTE_ALIGN(4);
};  // namespace JUtility

inline JUtility::TColor::TColor(u8 r, u8 g, u8 b, u8 a) {
    this->r = r;
    this->g = g;
    this->b = b;
    this->a = a;
}
