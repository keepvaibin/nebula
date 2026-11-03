#pragma once

#include <NebulaGpuTypes.h>

namespace nw4r {
    namespace ut {
        struct Color : public NebulaColor8 {
        public:
            static const int ALPHA_MAX = 255;

            static const u32 WHITE = 0xFFFFFFFF;

            Color() {
                *this = 0xFFFFFFFF;
            }

            Color(u32 color) {
                *this = color;
            }

            Color(const NebulaColor8& color) {
                *this = color;
            }

            // The u32 form is 0xRRGGBBAA: layout files, colour arguments and the game's literals all use it.
            Color& operator=(u32 color) {
                r = u8(color >> 24);
                g = u8(color >> 16);
                b = u8(color >> 8);
                a = u8(color);
                return *this;
            }

            Color& operator=(const NebulaColor8& color) {
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
        } ATTRIBUTE_ALIGN(4);
    };  // namespace ut
};  // namespace nw4r
