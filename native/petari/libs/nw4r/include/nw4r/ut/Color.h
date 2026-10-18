#pragma once

#include <revolution/gx/GXStruct.h>

namespace nw4r {
    namespace ut {
        struct Color : public GXColor {
        public:
            static const int ALPHA_MAX = 255;

            static const u32 WHITE = 0xFFFFFFFF;

            Color() {
                *this = 0xFFFFFFFF;
            }

            Color(u32 color) {
                *this = color;
            }

            Color(const GXColor& color) {
                *this = color;
            }

#ifdef TARGET_PC
            // The u32 form is the console's: 0xRRGGBBAA, the bytes r, g, b, a read big-endian (layout files, GX
            // colour arguments and the game's literals all use it).
            Color& operator=(u32 color) {
                r = u8(color >> 24);
                g = u8(color >> 16);
                b = u8(color >> 8);
                a = u8(color);
                return *this;
            }

            Color& operator=(const GXColor& color) {
                r = color.r;
                g = color.g;
                b = color.b;
                a = color.a;
                return *this;
            }

            ~Color() {
            }

            operator u32() const {
                return (u32(r) << 24) | (u32(g) << 16) | (u32(b) << 8) | a;
            }
#else
            Color& operator=(u32 color) {
                ToU32ref() = color;
                return *this;
            }

            Color& operator=(const GXColor& color) {
                return operator=(*reinterpret_cast< const u32* >(&color));
            }

            ~Color() {
            }

            operator u32() const {
                return ToU32ref();
            }

            u32& ToU32ref() {
                return *reinterpret_cast< u32* >(this);
            }
            const u32& ToU32ref() const {
                return *reinterpret_cast< const u32* >(this);
            }
#endif
        } ATTRIBUTE_ALIGN(4);
    };  // namespace ut
};  // namespace nw4r
