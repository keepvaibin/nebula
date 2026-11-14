#include "Game/Screen/NebulaIcons.hpp"
#include <nebula_module.h>
#include "Game/Render/RenderTexture.hpp"

#include "Game/Screen/LayoutActor.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemFontHolder.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/System/NebulaArena.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <nebula_cooked_texture.h>
#include <nebula_game_api.h>
#include <Game/Ui/Layout.hpp>
#include <Game/Ui/Material.hpp>
#include <Game/Ui/ResFont.hpp>
#include "Game/System/NebulaClock.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/Util/StringUtil.hpp"

// Nebula's icons are textures of its own, never the game's: the picture font gains sheets after its own (each
// controller family's icons, one to a cell) and a code map before its own that sends each icon's code to the glyph the
// controller shows; the A bubble's two pictures are ours only while it draws. Every picture of ours is a texture record
// (nebula_cooked_texture.h) of RGBA8 rows, as the cooked picture font's sheets are. The game's textures keep their
// names (a texture pack's replacements of them still apply) and ours are the same every run, so a pack names them by
// their texels too. Each of ours drawn is also painted at that size and replaces itself (NebulaHostServices::texture_hd), so it stays sharp at any window size.
namespace {
    // The picture font's code of each icon, in NEBULA_BUTTON_ICONS' order (A, B, C, Z, 1, 2, Plus, Minus, HOME, Control
    // Pad); the code map we put first covers them all.
    const u16 cCodes[NEBULA_BUTTON_ICONS] = {0x30, 0x31, 0x32, 0x49, 0x35, 0x36, 0x48, 0x47, 0x46, 0x43};
    enum { IconPlus = 6, IconMinus = 7, IconHome = 8, IconPad = 9 };
    const u16 cMapFirst = 0x30;
    const u16 cMapLast = 0x49;
    // The A bubble's largest picture at the game's size (48 pixels), and a pack's size of ours.
    const s32 cPictureMax = 48;
    const s32 cHdScale = 8;
    const s32 cHdSheetMax = 1024;

    struct Rgba {
        f32 r, g, b, a;
    };

    Rgba rgba(u32 color) {
        const Rgba c = {(color >> 24 & 0xFF) / 255.0f, (color >> 16 & 0xFF) / 255.0f, (color >> 8 & 0xFF) / 255.0f,
                        (color & 0xFF) / 255.0f};
        return c;
    }

    // A picture being drawn, straight alpha; `mScale` its pixels to one of the game's. `mBandRows` rows from `mBandTop`
    // are drawn, all that `mPixels` holds (none: the whole picture).
    struct Canvas {
        Rgba* mPixels;
        s32 mWidth;
        s32 mHeight;
        f32 mScale;
        s32 mBandTop = 0;
        s32 mBandRows = 0;

        s32 top() const { return mBandRows != 0 ? mBandTop : 0; }
        s32 bottom() const { return mBandRows != 0 ? mBandTop + mBandRows : mHeight; }
        void clear() { memset(mPixels, 0, sizeof(Rgba) * mWidth * (bottom() - top())); }

        // Straight alpha over what is drawn, `cover` of a pixel.
        void blend(s32 x, s32 y, const Rgba& rColor, f32 cover) {
            cover *= rColor.a;
            if (cover <= 0.0f || y < top() || y >= bottom()) {
                return;
            }
            Rgba& rDst = mPixels[(y - top()) * mWidth + x];
            const f32 a = cover + rDst.a * (1.0f - cover);
            const f32 keep = rDst.a * (1.0f - cover);
            rDst.r = (rColor.r * cover + rDst.r * keep) / a;
            rDst.g = (rColor.g * cover + rDst.g * keep) / a;
            rDst.b = (rColor.b * cover + rDst.b * keep) / a;
            rDst.a = a;
        }
    };
    MR::GrowBuffer< Rgba > sCanvasStore;

    // Room for a picture of width x height pixels (a font's cell, an A bubble); the last picture's pixels are gone when a
    // larger one is asked for.
    Rgba* canvasPixels(s32 width, s32 height) {
        return sCanvasStore.room(static_cast< u32 >(width * height));
    }

    // A circle (half width = half height = radius) or a rounded box, centred on (x, y).
    struct Shape {
        f32 mX;
        f32 mY;
        f32 mHalfWidth;
        f32 mHalfHeight;
        f32 mRadius;

        // Negative inside, in pixels.
        f32 distance(f32 x, f32 y) const {
            const f32 qx = __builtin_fabsf(x - mX) - (mHalfWidth - mRadius);
            const f32 qy = __builtin_fabsf(y - mY) - (mHalfHeight - mRadius);
            const f32 ox = qx > 0.0f ? qx : 0.0f;
            const f32 oy = qy > 0.0f ? qy : 0.0f;
            const f32 inside = qx > qy ? qx : qy;
            return __builtin_sqrtf(ox * ox + oy * oy) + (inside < 0.0f ? inside : 0.0f) - mRadius;
        }
    };

    // A plus-shaped pad: two rounded bars `mArm` from the centre (mX, mY) to each tip and `mHalfWidth` half wide.
    struct Cross {
        f32 mX;
        f32 mY;
        f32 mHalfHeight;  // = mArm, for drawShape's shading
        f32 mHalfWidth;
        f32 mRadius;

        f32 distance(f32 x, f32 y) const {
            const Shape across = {mX, mY, mHalfHeight, mHalfWidth, mRadius};
            const Shape upright = {mX, mY, mHalfWidth, mHalfHeight, mRadius};
            const f32 a = across.distance(x, y);
            const f32 b = upright.distance(x, y);
            return a < b ? a : b;
        }
    };

    f32 coverOf(f32 distance) {
        return NebulaUi::clamp(0.5f - distance, 0.0f, 1.0f);
    }

    // `rShape` (a Shape, or a Bean: anything with mY, mHalfHeight and distance) ringed `ring` wide in `rRing`, its face
    // from `rTop` at its top to `rBottom` at its foot.
    template < typename Surface >
    void drawShape(Canvas& rCanvas, const Surface& rShape, const Rgba& rRing, f32 ring, const Rgba& rTop, const Rgba& rBottom) {
        for (s32 y = rCanvas.top(); y < rCanvas.bottom(); y++) {
            const f32 t = NebulaUi::clamp((y + 0.5f - (rShape.mY - rShape.mHalfHeight)) / (rShape.mHalfHeight * 2.0f), 0.0f, 1.0f);
            const Rgba face = {rTop.r + (rBottom.r - rTop.r) * t, rTop.g + (rBottom.g - rTop.g) * t, rTop.b + (rBottom.b - rTop.b) * t,
                               rTop.a + (rBottom.a - rTop.a) * t};
            for (s32 x = 0; x < rCanvas.mWidth; x++) {
                const f32 d = rShape.distance(x + 0.5f, y + 0.5f);
                rCanvas.blend(x, y, rRing, coverOf(d));
                rCanvas.blend(x, y, face, coverOf(d + ring));
            }
        }
    }

    // A filled box (bars of a plus, lines of a menu), as drawShape without a ring.
    void drawBar(Canvas& rCanvas, f32 x, f32 y, f32 halfWidth, f32 halfHeight, const Rgba& rColor) {
        const f32 radius = halfWidth < halfHeight ? halfWidth : halfHeight;
        const Shape bar = {x, y, halfWidth, halfHeight, radius};
        drawShape(rCanvas, bar, rColor, 0.0f, rColor, rColor);
    }

    // The distance from (x, y) to the segment from a to b.
    f32 segmentDistance(f32 x, f32 y, f32 ax, f32 ay, f32 bx, f32 by) {
        const f32 dx = bx - ax;
        const f32 dy = by - ay;
        const f32 t = NebulaUi::clamp(((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
        const f32 ex = x - ax - dx * t;
        const f32 ey = y - ay - dy * t;
        return __builtin_sqrtf(ex * ex + ey * ey);
    }

    // A PlayStation face button's symbol (0 cross, 1 circle, 2 square, 3 triangle) in strokes, centred on (x, y): the
    // cross, circle and square by their box; the triangle raised toward its apex until its centre of mass is within a
    // fifteenth of its height of the middle (its box centred, it reads low: measured 2.5 px low at 4K, its mass 8.6).
    void drawPlayStationSymbol(Canvas& rCanvas, s32 symbol, f32 x, f32 y, f32 diameter, const Rgba& rInk) {
        const f32 stroke = diameter * 0.075f;
        const f32 arm = diameter * 0.165f;
        const Shape square = {x, y, diameter * 0.15f, diameter * 0.15f, diameter * 0.02f};
        const f32 side = diameter * 0.4f;
        const f32 rise = side * 0.866f;
        const f32 apex = y - rise * 0.6f;
        const f32 base = apex + rise;
        for (s32 py = rCanvas.top(); py < rCanvas.bottom(); py++) {
            for (s32 px = 0; px < rCanvas.mWidth; px++) {
                const f32 fx = px + 0.5f;
                const f32 fy = py + 0.5f;
                f32 d = 0.0f;
                switch (symbol) {
                case 0: {
                    const f32 a = segmentDistance(fx, fy, x - arm, y - arm, x + arm, y + arm);
                    const f32 b = segmentDistance(fx, fy, x - arm, y + arm, x + arm, y - arm);
                    d = a < b ? a : b;
                    break;
                }
                case 1:
                    d = __builtin_fabsf(__builtin_sqrtf((fx - x) * (fx - x) + (fy - y) * (fy - y)) - diameter * 0.155f);
                    break;
                case 2:
                    d = __builtin_fabsf(square.distance(fx, fy));
                    break;
                default: {
                    const f32 a = segmentDistance(fx, fy, x, apex, x - side * 0.5f, base);
                    const f32 b = segmentDistance(fx, fy, x - side * 0.5f, base, x + side * 0.5f, base);
                    const f32 c = segmentDistance(fx, fy, x + side * 0.5f, base, x, apex);
                    d = a < b ? (a < c ? a : c) : (b < c ? b : c);
                    break;
                }
                }
                rCanvas.blend(px, py, rInk, coverOf(d - stroke * 0.5f));
            }
        }
    }

    // The buttons' names in letters of Nebula's own, sharp at any size: strokes of one width (cStrokeHalf of a capital
    // each side) with round ends, in a box one capital tall from the capitals' top (0) to their foot (1); a mark above a
    // capital or a cedilla under it outside the box. A segment is (x0, y0, x1, y1); an arc (centre, radius, its start
    // and end directions) turns clockwise on screen, half a turn at most. Codes are Unicode's (the capitals, digits and
    // marks keyboards print), and 1 to 5 the arrows up, down, left and right and Backspace's long arrow. The first twelve
    // are the controllers' (their pictures, and so a texture pack's names for them, stay as they are).
    struct Stroke {
        u8 mArc;
        f32 mA, mB, mC, mD, mE, mF, mG;
    };
    struct Letter {
        u16 mCode;
        f32 mAdvance;  // its ink's width
        s32 mFirst;
        s32 mCount;
    };
    const f32 cStrokeHalf = 0.095f;
    const f32 cLetterGap = 0.14f;
    const Stroke cStrokes[] = {
        {0, 0.42f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.42f, 0.095f, 0.745f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.1933f, 0.66f, 0.6467f, 0.66f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.34f, 0.095f, 0.0f, 0.0f, 0.0f},
        {1, 0.34f, 0.2875f, 0.1925f, 0.0f, -1.0f, 0.0f, 1.0f},
        {0, 0.34f, 0.48f, 0.095f, 0.48f, 0.0f, 0.0f, 0.0f},
        {0, 0.34f, 0.48f, 0.36f, 0.48f, 0.0f, 0.0f, 0.0f},
        {1, 0.36f, 0.6925f, 0.2125f, 0.0f, -1.0f, 0.0f, 1.0f},
        {0, 0.36f, 0.905f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.905f, 0.545f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.36f, 0.095f, 0.0f, 0.0f, 0.0f},
        {1, 0.36f, 0.2975f, 0.2025f, 0.0f, -1.0f, 0.0f, 1.0f},
        {0, 0.36f, 0.5f, 0.095f, 0.5f, 0.0f, 0.0f, 0.0f},
        {0, 0.34f, 0.5f, 0.645f, 0.905f, 0.0f, 0.0f, 0.0f},
        {1, 0.31f, 0.285f, 0.19f, 0.0f, 1.0f, -0.809f, -0.5878f},
        {1, 0.31f, 0.285f, 0.19f, -0.809f, -0.5878f, 0.9511f, -0.309f},
        {1, 0.31f, 0.69f, 0.215f, 0.0f, -1.0f, 0.809f, 0.5878f},
        {1, 0.31f, 0.69f, 0.215f, 0.809f, 0.5878f, -0.9511f, 0.309f},
        {0, 0.095f, 0.095f, 0.625f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.36f, 0.095f, 0.36f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.705f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.705f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.41f, 0.53f, 0.0f, 0.0f, 0.0f},
        {0, 0.725f, 0.095f, 0.41f, 0.53f, 0.0f, 0.0f, 0.0f},
        {0, 0.41f, 0.53f, 0.41f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.625f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.625f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.905f, 0.625f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.345f, 0.095f, 0.345f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.345f, 0.095f, 0.095f, 0.27f, 0.0f, 0.0f, 0.0f},
        {1, 0.335f, 0.31f, 0.215f, -0.9659f, -0.2588f, 0.4226f, -0.9063f},
        {1, 0.335f, 0.31f, 0.215f, 0.4226f, -0.9063f, 0.8192f, 0.5736f},
        {0, 0.5111f, 0.4333f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.905f, 0.55f, 0.905f, 0.0f, 0.0f, 0.0f},
        {1, 0.3f, 0.29f, 0.195f, -0.9397f, -0.342f, 0.8192f, -0.5736f},
        {1, 0.3f, 0.29f, 0.195f, 0.8192f, -0.5736f, 0.0f, 1.0f},
        {1, 0.3f, 0.695f, 0.21f, 0.0f, -1.0f, 0.8192f, 0.5736f},
        {1, 0.3f, 0.695f, 0.21f, 0.8192f, 0.5736f, -0.9397f, 0.342f},
{1, 0.36f, 0.36f, 0.265f, -1.0f, 0.0f, 0.8192f, -0.5736f},
        {1, 0.36f, 0.64f, 0.265f, 0.8192f, 0.5736f, -1.0f, 0.0f},
        {0, 0.095f, 0.36f, 0.095f, 0.64f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.385f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.905f, 0.385f, 0.905f, 0.0f, 0.0f, 0.0f},
        {1, 0.385f, 0.355f, 0.26f, 0.0f, -1.0f, 1.0f, 0.0f},
        {1, 0.385f, 0.645f, 0.26f, 1.0f, 0.0f, 0.0f, 1.0f},
        {0, 0.645f, 0.355f, 0.645f, 0.645f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.525f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.5f, 0.48f, 0.5f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.905f, 0.525f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.505f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.5f, 0.46f, 0.5f, 0.0f, 0.0f, 0.0f},
        {1, 0.38f, 0.38f, 0.285f, -1.0f, 0.0f, 0.866f, -0.5f},
        {1, 0.38f, 0.62f, 0.285f, 1.0f, 0.0f, -1.0f, 0.0f},
        {0, 0.095f, 0.38f, 0.095f, 0.62f, 0.0f, 0.0f, 0.0f},
        {0, 0.665f, 0.62f, 0.665f, 0.53f, 0.0f, 0.0f, 0.0f},
        {0, 0.665f, 0.53f, 0.43f, 0.53f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.645f, 0.095f, 0.645f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.5f, 0.645f, 0.5f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.505f, 0.095f, 0.505f, 0.65f, 0.0f, 0.0f, 0.0f},
        {1, 0.3f, 0.65f, 0.205f, 1.0f, 0.0f, -1.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.625f, 0.095f, 0.095f, 0.6f, 0.0f, 0.0f, 0.0f},
        {0, 0.3f, 0.43f, 0.625f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.905f, 0.095f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.46f, 0.68f, 0.0f, 0.0f, 0.0f},
        {0, 0.46f, 0.68f, 0.825f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.825f, 0.095f, 0.825f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.905f, 0.095f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.665f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.665f, 0.905f, 0.665f, 0.095f, 0.0f, 0.0f, 0.0f},
        {1, 0.4f, 0.4f, 0.305f, -1.0f, 0.0f, 1.0f, 0.0f},
        {1, 0.4f, 0.6f, 0.305f, 1.0f, 0.0f, -1.0f, 0.0f},
        {0, 0.095f, 0.4f, 0.095f, 0.6f, 0.0f, 0.0f, 0.0f},
        {0, 0.705f, 0.4f, 0.705f, 0.6f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.36f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.545f, 0.36f, 0.545f, 0.0f, 0.0f, 0.0f},
        {1, 0.36f, 0.32f, 0.225f, 0.0f, -1.0f, 0.0f, 1.0f},
        {1, 0.4f, 0.4f, 0.305f, -1.0f, 0.0f, 1.0f, 0.0f},
        {1, 0.4f, 0.6f, 0.305f, 1.0f, 0.0f, -1.0f, 0.0f},
        {0, 0.095f, 0.4f, 0.095f, 0.6f, 0.0f, 0.0f, 0.0f},
        {0, 0.705f, 0.4f, 0.705f, 0.6f, 0.0f, 0.0f, 0.0f},
        {0, 0.48f, 0.66f, 0.705f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.63f, 0.0f, 0.0f, 0.0f},
        {0, 0.645f, 0.095f, 0.645f, 0.63f, 0.0f, 0.0f, 0.0f},
        {1, 0.37f, 0.63f, 0.275f, 1.0f, 0.0f, -1.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.4f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.4f, 0.905f, 0.705f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.3f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.3f, 0.905f, 0.52f, 0.32f, 0.0f, 0.0f, 0.0f},
        {0, 0.52f, 0.32f, 0.74f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.74f, 0.905f, 0.945f, 0.095f, 0.0f, 0.0f, 0.0f},
        {1, 0.34f, 0.34f, 0.245f, -1.0f, 0.0f, 1.0f, 0.0f},
        {1, 0.34f, 0.66f, 0.245f, 1.0f, 0.0f, -1.0f, 0.0f},
        {0, 0.095f, 0.34f, 0.095f, 0.66f, 0.0f, 0.0f, 0.0f},
        {0, 0.585f, 0.34f, 0.585f, 0.66f, 0.0f, 0.0f, 0.0f},
        {0, 0.47f, 0.095f, 0.095f, 0.66f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.66f, 0.585f, 0.66f, 0.0f, 0.0f, 0.0f},
        {0, 0.47f, 0.095f, 0.47f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.53f, 0.095f, 0.14f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.14f, 0.095f, 0.12f, 0.45f, 0.0f, 0.0f, 0.0f},
        {0, 0.12f, 0.45f, 0.29f, 0.42f, 0.0f, 0.0f, 0.0f},
        {1, 0.29f, 0.66f, 0.245f, 0.0f, -1.0f, 0.0f, 1.0f},
        {1, 0.29f, 0.66f, 0.245f, 0.0f, 1.0f, -0.866f, 0.5f},
        {1, 0.32f, 0.66f, 0.225f, 1.0f, 0.0f, -1.0f, 0.0f},
        {1, 0.32f, 0.66f, 0.225f, -1.0f, 0.0f, 1.0f, 0.0f},
        {1, 0.6f, 0.66f, 0.505f, -1.0f, 0.0f, -0.3746f, -0.9272f},
        {0, 0.095f, 0.095f, 0.545f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.545f, 0.095f, 0.24f, 0.905f, 0.0f, 0.0f, 0.0f},
        {1, 0.32f, 0.29f, 0.195f, 1.0f, 0.0f, -1.0f, 0.0f},
        {1, 0.32f, 0.29f, 0.195f, -1.0f, 0.0f, 1.0f, 0.0f},
        {1, 0.32f, 0.68f, 0.225f, 1.0f, 0.0f, -1.0f, 0.0f},
        {1, 0.32f, 0.68f, 0.225f, -1.0f, 0.0f, 1.0f, 0.0f},
        {1, 0.32f, 0.34f, 0.225f, 1.0f, 0.0f, -1.0f, 0.0f},
        {1, 0.32f, 0.34f, 0.225f, -1.0f, 0.0f, 1.0f, 0.0f},
        {1, 0.04f, 0.34f, 0.505f, 1.0f, 0.0f, 0.3746f, 0.9272f},
        {0, 0.091f, 0.905f, 0.099f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.121f, 0.865f, 0.129f, 0.865f, 0.0f, 0.0f, 0.0f},
        {0, 0.125f, 0.865f, 0.06f, 1.08f, 0.0f, 0.0f, 0.0f},
        {0, 0.121f, 0.42f, 0.129f, 0.42f, 0.0f, 0.0f, 0.0f},
        {0, 0.121f, 0.865f, 0.129f, 0.865f, 0.0f, 0.0f, 0.0f},
        {0, 0.125f, 0.865f, 0.06f, 1.08f, 0.0f, 0.0f, 0.0f},
        {0, 0.091f, 0.42f, 0.099f, 0.42f, 0.0f, 0.0f, 0.0f},
        {0, 0.091f, 0.905f, 0.099f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.34f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.34f, 0.0f, 0.0f, 0.0f},
        {0, 0.345f, 0.095f, 0.345f, 0.34f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.225f, 0.24f, 0.0f, 0.0f, 0.0f},
        {0, 0.225f, 0.095f, 0.095f, 0.24f, 0.0f, 0.0f, 0.0f},
        {0, 0.425f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.425f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.285f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.905f, 0.285f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.285f, 0.095f, 0.285f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.285f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.905f, 0.285f, 0.905f, 0.0f, 0.0f, 0.0f},
        {1, 0.62f, 0.5f, 0.525f, -0.766f, 0.6428f, -0.766f, -0.6428f},
        {1, -0.28f, 0.5f, 0.525f, 0.766f, -0.6428f, 0.766f, 0.6428f},
        {0, 0.095f, 0.56f, 0.405f, 0.56f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.905f, 0.505f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.42f, 0.505f, 0.42f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.7f, 0.505f, 0.7f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.56f, 0.525f, 0.56f, 0.0f, 0.0f, 0.0f},
        {0, 0.31f, 0.345f, 0.31f, 0.775f, 0.0f, 0.0f, 0.0f},
        {0, 0.28f, 0.28f, 0.28f, 0.72f, 0.0f, 0.0f, 0.0f},
        {0, 0.09f, 0.39f, 0.47f, 0.61f, 0.0f, 0.0f, 0.0f},
        {0, 0.09f, 0.61f, 0.47f, 0.39f, 0.0f, 0.0f, 0.0f},
        {0, 0.465f, 0.3f, 0.095f, 0.56f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.56f, 0.465f, 0.82f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.3f, 0.465f, 0.56f, 0.0f, 0.0f, 0.0f},
        {0, 0.465f, 0.56f, 0.095f, 0.82f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.42f, 0.28f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.28f, 0.095f, 0.465f, 0.42f, 0.0f, 0.0f, 0.0f},
        {1, 0.24f, 0.56f, 0.145f, -1.0f, 0.0f, 1.0f, 0.0f},
        {1, 0.53f, 0.56f, 0.145f, 1.0f, 0.0f, -1.0f, 0.0f},
        {0, 0.3f, 0.095f, 0.22f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.54f, 0.095f, 0.46f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.36f, 0.645f, 0.36f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.64f, 0.645f, 0.64f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.62f, 0.0f, 0.0f, 0.0f},
        {0, 0.091f, 0.905f, 0.099f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.091f, 0.095f, 0.099f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.38f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {1, 0.31f, 0.285f, 0.19f, 0.0f, 1.0f, -0.809f, -0.5878f},
        {1, 0.31f, 0.285f, 0.19f, -0.809f, -0.5878f, 0.9511f, -0.309f},
        {1, 0.31f, 0.69f, 0.215f, 0.0f, -1.0f, 0.809f, 0.5878f},
        {1, 0.31f, 0.69f, 0.215f, 0.809f, 0.5878f, -0.9511f, 0.309f},
        {0, 0.31f, -0.02f, 0.31f, 1.02f, 0.0f, 0.0f, 0.0f},
        {1, 0.5f, 0.5f, 0.405f, 0.866f, 0.5f, -0.866f, -0.5f},
        {1, 0.5f, 0.5f, 0.405f, -0.866f, -0.5f, 0.866f, -0.5f},
        {1, 0.47f, 0.5f, 0.15f, 1.0f, 0.0f, -1.0f, 0.0f},
        {1, 0.47f, 0.5f, 0.15f, -1.0f, 0.0f, 1.0f, 0.0f},
        {0, 0.62f, 0.35f, 0.62f, 0.62f, 0.0f, 0.0f, 0.0f},
        {1, 0.735f, 0.62f, 0.115f, 1.0f, 0.0f, -1.0f, 0.0f},
        {1, 0.25f, 0.25f, 0.155f, -1.0f, 0.0f, 0.9397f, -0.342f},
        {1, 0.25f, 0.25f, 0.155f, 0.0f, 1.0f, -1.0f, 0.0f},
        {1, 0.25f, 0.5f, 0.155f, 0.0f, -1.0f, 0.0f, 1.0f},
        {1, 0.25f, 0.5f, 0.155f, 0.0f, 1.0f, 0.0f, -1.0f},
        {1, 0.25f, 0.75f, 0.155f, 1.0f, 0.0f, 0.0f, 1.0f},
        {1, 0.25f, 0.75f, 0.155f, -0.9397f, 0.342f, 0.9397f, -0.342f},
        {1, 0.25f, 0.75f, 0.155f, 0.9397f, -0.342f, 1.0f, 0.0f},
        {1, 0.2f, 0.25f, 0.105f, 1.0f, 0.0f, -1.0f, 0.0f},
        {1, 0.2f, 0.25f, 0.105f, -1.0f, 0.0f, 1.0f, 0.0f},
        {1, 0.2f, 0.27f, 0.105f, 1.0f, 0.0f, -1.0f, 0.0f},
        {1, 0.2f, 0.27f, 0.105f, -1.0f, 0.0f, 1.0f, 0.0f},
        {0, 0.095f, 0.56f, 0.305f, 0.56f, 0.0f, 0.0f, 0.0f},
        {1, 0.2f, 0.27f, 0.105f, 1.0f, 0.0f, -1.0f, 0.0f},
        {1, 0.2f, 0.27f, 0.105f, -1.0f, 0.0f, 1.0f, 0.0f},
        {0, 0.305f, 0.17f, 0.305f, 0.375f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.56f, 0.305f, 0.56f, 0.0f, 0.0f, 0.0f},
        {1, 0.1843f, 0.1705f, 0.1183f, -0.9659f, -0.2588f, 0.4226f, -0.9063f},
        {1, 0.1843f, 0.1705f, 0.1183f, 0.4226f, -0.9063f, 0.8192f, 0.5736f},
        {0, 0.2811f, 0.2383f, 0.05225f, 0.4978f, 0.0f, 0.0f, 0.0f},
        {0, 0.05225f, 0.4978f, 0.3025f, 0.4978f, 0.0f, 0.0f, 0.0f},
        {0, 0.091f, 0.2f, 0.099f, 0.2f, 0.0f, 0.0f, 0.0f},
        {0, 0.341f, 0.2f, 0.349f, 0.2f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.905f, 0.095f, 0.3f, 0.0f, 0.0f, 0.0f},
        {1, 0.3f, 0.3f, 0.205f, -1.0f, 0.0f, 1.0f, 0.0f},
        {1, 0.3f, 0.3f, 0.205f, 1.0f, 0.0f, 0.0f, 1.0f},
        {0, 0.3f, 0.505f, 0.28f, 0.505f, 0.0f, 0.0f, 0.0f},
        {1, 0.3f, 0.7f, 0.205f, 0.0f, -1.0f, 0.0f, 1.0f},
        {0, 0.3f, 0.905f, 0.2f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.31f, 0.905f, 0.31f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.31f, 0.095f, 0.095f, 0.31f, 0.0f, 0.0f, 0.0f},
        {0, 0.31f, 0.095f, 0.525f, 0.31f, 0.0f, 0.0f, 0.0f},
        {0, 0.31f, 0.095f, 0.31f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.31f, 0.905f, 0.095f, 0.69f, 0.0f, 0.0f, 0.0f},
        {0, 0.31f, 0.905f, 0.525f, 0.69f, 0.0f, 0.0f, 0.0f},
        {0, 0.765f, 0.5f, 0.095f, 0.5f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.5f, 0.31f, 0.285f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.5f, 0.31f, 0.715f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.5f, 0.765f, 0.5f, 0.0f, 0.0f, 0.0f},
        {0, 0.765f, 0.5f, 0.55f, 0.285f, 0.0f, 0.0f, 0.0f},
        {0, 0.765f, 0.5f, 0.55f, 0.715f, 0.0f, 0.0f, 0.0f},
        {0, 1.105f, 0.5f, 0.095f, 0.5f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.5f, 0.31f, 0.285f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.5f, 0.31f, 0.715f, 0.0f, 0.0f, 0.0f},
        {0, 0.42f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.42f, 0.095f, 0.745f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.1933f, 0.66f, 0.6467f, 0.66f, 0.0f, 0.0f, 0.0f},
        {0, 0.246f, -0.18f, 0.254f, -0.18f, 0.0f, 0.0f, 0.0f},
        {0, 0.586f, -0.18f, 0.594f, -0.18f, 0.0f, 0.0f, 0.0f},
        {1, 0.4f, 0.4f, 0.305f, -1.0f, 0.0f, 1.0f, 0.0f},
        {1, 0.4f, 0.6f, 0.305f, 1.0f, 0.0f, -1.0f, 0.0f},
        {0, 0.095f, 0.4f, 0.095f, 0.6f, 0.0f, 0.0f, 0.0f},
        {0, 0.705f, 0.4f, 0.705f, 0.6f, 0.0f, 0.0f, 0.0f},
        {0, 0.226f, -0.18f, 0.234f, -0.18f, 0.0f, 0.0f, 0.0f},
        {0, 0.566f, -0.18f, 0.574f, -0.18f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.63f, 0.0f, 0.0f, 0.0f},
        {0, 0.645f, 0.095f, 0.645f, 0.63f, 0.0f, 0.0f, 0.0f},
        {1, 0.37f, 0.63f, 0.275f, 1.0f, 0.0f, -1.0f, 0.0f},
        {0, 0.196f, -0.18f, 0.204f, -0.18f, 0.0f, 0.0f, 0.0f},
        {0, 0.536f, -0.18f, 0.544f, -0.18f, 0.0f, 0.0f, 0.0f},
        {0, 0.42f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.42f, 0.095f, 0.745f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.1933f, 0.66f, 0.6467f, 0.66f, 0.0f, 0.0f, 0.0f},
        {1, 0.42f, -0.3f, 0.09f, 1.0f, 0.0f, -1.0f, 0.0f},
        {1, 0.42f, -0.3f, 0.09f, -1.0f, 0.0f, 1.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.525f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.5f, 0.48f, 0.5f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.905f, 0.525f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.23f, -0.1f, 0.41f, -0.3f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.525f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.5f, 0.48f, 0.5f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.905f, 0.525f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.39f, -0.1f, 0.21f, -0.3f, 0.0f, 0.0f, 0.0f},
        {0, 0.42f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.42f, 0.095f, 0.745f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.1933f, 0.66f, 0.6467f, 0.66f, 0.0f, 0.0f, 0.0f},
        {0, 0.5f, -0.1f, 0.32f, -0.3f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.175f, -0.1f, -0.005f, -0.3f, 0.0f, 0.0f, 0.0f},
        {1, 0.4f, 0.4f, 0.305f, -1.0f, 0.0f, 1.0f, 0.0f},
        {1, 0.4f, 0.6f, 0.305f, 1.0f, 0.0f, -1.0f, 0.0f},
        {0, 0.095f, 0.4f, 0.095f, 0.6f, 0.0f, 0.0f, 0.0f},
        {0, 0.705f, 0.4f, 0.705f, 0.6f, 0.0f, 0.0f, 0.0f},
        {0, 0.48f, -0.1f, 0.3f, -0.3f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.63f, 0.0f, 0.0f, 0.0f},
        {0, 0.645f, 0.095f, 0.645f, 0.63f, 0.0f, 0.0f, 0.0f},
        {1, 0.37f, 0.63f, 0.275f, 1.0f, 0.0f, -1.0f, 0.0f},
        {0, 0.45f, -0.1f, 0.27f, -0.3f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.905f, 0.095f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.665f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.665f, 0.905f, 0.665f, 0.095f, 0.0f, 0.0f, 0.0f},
        {1, 0.27f, -0.16f, 0.1f, -1.0f, 0.0f, 1.0f, 0.0f},
        {1, 0.47f, -0.16f, 0.1f, 1.0f, 0.0f, -1.0f, 0.0f},
        {1, 0.36f, 0.36f, 0.265f, -1.0f, 0.0f, 0.8192f, -0.5736f},
        {1, 0.36f, 0.64f, 0.265f, 0.8192f, 0.5736f, -1.0f, 0.0f},
        {0, 0.095f, 0.36f, 0.095f, 0.64f, 0.0f, 0.0f, 0.0f},
        {0, 0.36f, 1.0f, 0.42f, 1.13f, 0.0f, 0.0f, 0.0f},
        {0, 0.42f, 1.13f, 0.29f, 1.24f, 0.0f, 0.0f, 0.0f},
        {1, 0.38f, 0.38f, 0.285f, -1.0f, 0.0f, 0.866f, -0.5f},
        {1, 0.38f, 0.62f, 0.285f, 1.0f, 0.0f, -1.0f, 0.0f},
        {0, 0.095f, 0.38f, 0.095f, 0.62f, 0.0f, 0.0f, 0.0f},
        {0, 0.665f, 0.62f, 0.665f, 0.53f, 0.0f, 0.0f, 0.0f},
        {0, 0.665f, 0.53f, 0.43f, 0.53f, 0.0f, 0.0f, 0.0f},
        {1, 0.38f, -0.36f, 0.17f, 0.866f, 0.5f, -0.866f, 0.5f},
        {1, 0.31f, 0.285f, 0.19f, 0.0f, 1.0f, -0.809f, -0.5878f},
        {1, 0.31f, 0.285f, 0.19f, -0.809f, -0.5878f, 0.9511f, -0.309f},
        {1, 0.31f, 0.69f, 0.215f, 0.0f, -1.0f, 0.809f, 0.5878f},
        {1, 0.31f, 0.69f, 0.215f, 0.809f, 0.5878f, -0.9511f, 0.309f},
        {0, 0.31f, 1.0f, 0.37f, 1.13f, 0.0f, 0.0f, 0.0f},
        {0, 0.37f, 1.13f, 0.24f, 1.24f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.095f, 0.095f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.091f, -0.18f, 0.099f, -0.18f, 0.0f, 0.0f, 0.0f},
        {0, 0.095f, 0.905f, 0.5f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.24f, 0.62f, 0.52f, 0.62f, 0.0f, 0.0f, 0.0f},
        {0, 0.5f, 0.095f, 0.5f, 0.905f, 0.0f, 0.0f, 0.0f},
        {0, 0.5f, 0.095f, 0.985f, 0.095f, 0.0f, 0.0f, 0.0f},
        {0, 0.5f, 0.5f, 0.94f, 0.5f, 0.0f, 0.0f, 0.0f},
        {0, 0.5f, 0.905f, 0.985f, 0.905f, 0.0f, 0.0f, 0.0f},
        {1, 0.4f, 0.4f, 0.305f, -1.0f, 0.0f, 1.0f, 0.0f},
        {1, 0.4f, 0.6f, 0.305f, 1.0f, 0.0f, -1.0f, 0.0f},
        {0, 0.095f, 0.4f, 0.095f, 0.6f, 0.0f, 0.0f, 0.0f},
        {0, 0.705f, 0.4f, 0.705f, 0.6f, 0.0f, 0.0f, 0.0f},
        {0, 0.72f, 0.02f, 0.08f, 0.98f, 0.0f, 0.0f, 0.0f},
    };
    const Letter cLetters[] = {
        {'A', 0.84f, 0, 3},   {'B', 0.6675f, 3, 7}, {'L', 0.64f, 10, 2}, {'R', 0.74f, 12, 5},
        {'S', 0.62f, 17, 4},  {'T', 0.72f, 21, 2},  {'X', 0.8f, 23, 2},  {'Y', 0.82f, 25, 3},
        {'Z', 0.72f, 28, 3},  {'1', 0.48f, 31, 2},  {'2', 0.645f, 33, 4}, {'3', 0.605f, 37, 4},
        {'C', 0.72f, 41, 3},
        {'D', 0.74f, 44, 6},
        {'E', 0.62f, 50, 4},
        {'F', 0.6f, 54, 3},
        {'G', 0.76f, 57, 5},
        {'H', 0.74f, 62, 3},
        {'I', 0.19f, 65, 1},
        {'J', 0.6f, 66, 2},
        {'K', 0.72f, 68, 3},
        {'M', 0.92f, 71, 4},
        {'N', 0.76f, 75, 3},
        {'O', 0.8f, 78, 4},
        {'P', 0.68f, 82, 4},
        {'Q', 0.8f, 86, 5},
        {'U', 0.74f, 91, 3},
        {'V', 0.8f, 94, 2},
        {'W', 1.04f, 96, 4},
        {'0', 0.68f, 100, 4},
        {'4', 0.68f, 104, 3},
        {'5', 0.63f, 107, 5},
        {'6', 0.64f, 112, 3},
        {'7', 0.64f, 115, 2},
        {'8', 0.64f, 117, 4},
        {'9', 0.64f, 121, 3},
        {'.', 0.19f, 124, 1},
        {',', 0.22f, 125, 2},
        {';', 0.22f, 127, 3},
        {':', 0.19f, 130, 2},
        {'\'', 0.19f, 132, 1},
        {'"', 0.44f, 133, 2},
        {'`', 0.32f, 135, 1},
        {0xB4, 0.32f, 136, 1},
        {'/', 0.52f, 137, 1},
        {'\\', 0.52f, 138, 1},
        {'[', 0.38f, 139, 3},
        {']', 0.38f, 142, 3},
        {'(', 0.34f, 145, 1},
        {')', 0.34f, 146, 1},
        {'-', 0.5f, 147, 1},
        {'_', 0.6f, 148, 1},
        {'=', 0.6f, 149, 2},
        {'+', 0.62f, 151, 2},
        {'*', 0.56f, 153, 3},
        {'<', 0.56f, 156, 2},
        {'>', 0.56f, 158, 2},
        {'^', 0.56f, 160, 2},
        {'~', 0.78f, 162, 2},
        {'#', 0.74f, 164, 4},
        {'!', 0.19f, 168, 2},
        {0xA1, 0.19f, 170, 2},
        {'$', 0.62f, 172, 5},
        {'@', 1.0f, 177, 6},
        {0xA7, 0.5f, 183, 7},
        {0xB0, 0.4f, 190, 2},
        {0xBA, 0.4f, 192, 3},
        {0xAA, 0.4f, 195, 4},
        {0xB2, 0.42f, 199, 4},
        {0xA8, 0.44f, 203, 2},
        {0xDF, 0.66f, 205, 6},
        {0x01, 0.62f, 211, 3},
        {0x02, 0.62f, 214, 3},
        {0x03, 0.86f, 217, 3},
        {0x04, 0.86f, 220, 3},
        {0x05, 1.2f, 223, 3},
        {0xC4, 0.84f, 226, 5},
        {0xD6, 0.8f, 231, 6},
        {0xDC, 0.74f, 237, 5},
        {0xC5, 0.84f, 242, 5},
        {0xC9, 0.62f, 247, 5},
        {0xC8, 0.62f, 252, 5},
        {0xC0, 0.84f, 257, 4},
        {0xCC, 0.19f, 261, 2},
        {0xD2, 0.8f, 263, 5},
        {0xD9, 0.74f, 268, 4},
        {0xD1, 0.76f, 272, 5},
        {0xC7, 0.72f, 277, 5},
        {0x11E, 0.76f, 282, 6},
        {0x15E, 0.62f, 288, 6},
        {0x130, 0.19f, 294, 2},
        {0xC6, 1.08f, 296, 6},
        {0xD8, 0.8f, 302, 5},
    };

    const Letter* findLetter(u16 code) {
        for (u32 i = 0; i < sizeof(cLetters) / sizeof(cLetters[0]); i++) {
            if (cLetters[i].mCode == code) {
                return &cLetters[i];
            }
        }
        return nullptr;
    }

    // The distance from (x, y), in capitals, to a letter's strokes' middle lines.
    f32 letterDistance(const Letter& rLetter, f32 x, f32 y) {
        f32 best = 1e9f;
        for (s32 i = rLetter.mFirst; i < rLetter.mFirst + rLetter.mCount; i++) {
            const Stroke& s = cStrokes[i];
            f32 d;
            if (s.mArc == 0) {
                d = segmentDistance(x, y, s.mA, s.mB, s.mC, s.mD);
            } else {
                const f32 vx = x - s.mA;
                const f32 vy = y - s.mB;
                if (s.mD * vy - s.mE * vx >= 0.0f && vx * s.mG - vy * s.mF >= 0.0f) {
                    d = __builtin_fabsf(__builtin_sqrtf(vx * vx + vy * vy) - s.mC);
                } else {
                    const f32 ax = vx - s.mC * s.mD;
                    const f32 ay = vy - s.mC * s.mE;
                    const f32 bx = vx - s.mC * s.mF;
                    const f32 by = vy - s.mC * s.mG;
                    const f32 da = __builtin_sqrtf(ax * ax + ay * ay);
                    const f32 db = __builtin_sqrtf(bx * bx + by * by);
                    d = da < db ? da : db;
                }
            }
            best = d < best ? d : best;
        }
        return best;
    }

    // `pText` (letters of cLetters) in `rInk`, its ink centred on (x, y), `capital` pixels tall.
    void drawText(Canvas& rCanvas, const char* pText, f32 x, f32 y, f32 capital, const Rgba& rInk) {
        MR::InlineVector< const Letter*, 8 > letters;
        f32 width = 0.0f;
        for (const char* p = pText; *p != 0; p++) {
            const Letter* const pLetter = findLetter(static_cast< u8 >(*p));
            if (pLetter != nullptr) {
                width += (letters.size() > 0 ? cLetterGap : 0.0f) + pLetter->mAdvance;
                letters.push_back(pLetter);
            }
        }
        f32 left = x - width * capital * 0.5f;
        const f32 top = y - capital * 0.5f;
        for (s32 i = 0; i < letters.size(); i++) {
            const Letter& rLetter = *letters[i];
            // Only the pixels its box (and a stroke's half around it) covers.
            const f32 margin = capital * cStrokeHalf + 1.0f;
            const s32 x0 = static_cast< s32 >(NebulaUi::clamp(left - margin, 0.0f, static_cast< f32 >(rCanvas.mWidth)));
            const s32 x1 = static_cast< s32 >(NebulaUi::clamp(left + rLetter.mAdvance * capital + margin + 1.0f, 0.0f, static_cast< f32 >(rCanvas.mWidth)));
            const s32 y0 = static_cast< s32 >(NebulaUi::clamp(top - margin, static_cast< f32 >(rCanvas.top()), static_cast< f32 >(rCanvas.bottom())));
            const s32 y1 = static_cast< s32 >(NebulaUi::clamp(top + capital + margin + 1.0f, static_cast< f32 >(rCanvas.top()), static_cast< f32 >(rCanvas.bottom())));
            for (s32 py = y0; py < y1; py++) {
                for (s32 px = x0; px < x1; px++) {
                    const f32 d = letterDistance(rLetter, (px + 0.5f - left) / capital, (py + 0.5f - top) / capital) * capital;
                    rCanvas.blend(px, py, rInk, coverOf(d - cStrokeHalf * capital));
                }
            }
            left += (rLetter.mAdvance + cLetterGap) * capital;
        }
    }

    // The colours: Xbox's face buttons in theirs, its other buttons dark; the other styles' white with the window ink.
    const u32 cXboxFaces[4] = {0x5FA83CFF, NEBULA_RGBA(0xD8, 0x44, 0x3A, 0xFF), 0x3C78D8FF, 0xE6B422FF};
    const u32 cRing = 0x262A33FF;
    const u32 cDarkTop = 0x52555EFF;
    const u32 cDarkBottom = 0x2E3036FF;
    const u32 cWhiteTop = 0xFFFFFFFF;
    const u32 cWhiteBottom = NEBULA_RGBA(0xD3, 0xD9, 0xE6, 0xFF);
    const u32 cInk = 0x2B3045FF;
    const u32 cWhite = 0xFFFFFFFF;
    // The GameCube pad's face buttons: A green, B red, X and Y silver grey.
    const u32 cGameCubeA = 0x2EA65CFF;
    const u32 cGameCubeB = NEBULA_RGBA(0xCC, 0x33, 0x33, 0xFF);
    const u32 cGameCubeXY = NEBULA_RGBA(0xC4, 0xC9, 0xD1, 0xFF);
    const u32 cPadRim = 0x6A6E78FF;  // an inner ring on a dark button

    Rgba darkened(const Rgba& rColor) {
        const Rgba c = {rColor.r * 0.9f, rColor.g * 0.9f, rColor.b * 0.9f, rColor.a};
        return c;
    }

    // `rColor` a quarter of the way to white: the top of a lit face.
    Rgba lightened(const Rgba& rColor) {
        const Rgba c = {rColor.r + (1.0f - rColor.r) * 0.25f, rColor.g + (1.0f - rColor.g) * 0.25f, rColor.b + (1.0f - rColor.b) * 0.25f, 1.0f};
        return c;
    }

    // A kidney, the GameCube pad's X and Y: a bar with round ends (`mHalfWidth` x `mHalfHeight` its box, centred on (mX,
    // mY), lying or `mUpright`) with one long side dented by a large circle, on the side `mDent` (+1: below a lying bar,
    // right of an upright one; -1: the other). Negative inside, in pixels; the dent's edge is not an exact distance.
    struct Bean {
        f32 mX;
        f32 mY;
        f32 mHalfWidth;
        f32 mHalfHeight;
        f32 mDent;
        bool mUpright;

        f32 distance(f32 x, f32 y) const {
            const f32 along = mUpright ? y - mY : x - mX;
            const f32 across = mUpright ? x - mX : y - mY;
            const f32 halfLength = mUpright ? mHalfHeight : mHalfWidth;
            const f32 halfAcross = mUpright ? mHalfWidth : mHalfHeight;
            const f32 tip = __builtin_fabsf(along) - (halfLength - halfAcross);
            const f32 beyond = tip > 0.0f ? tip : 0.0f;
            const f32 body = __builtin_sqrtf(beyond * beyond + across * across) - halfAcross;
            // The dent: a circle whose edge reaches 0.7 of the bar's half width into it at the middle.
            const f32 radius = halfLength * 1.2f;
            const f32 centre = mDent * (halfAcross * 0.3f + radius);
            const f32 dx = along;
            const f32 dy = across - centre;
            const f32 cut = radius - __builtin_sqrtf(dx * dx + dy * dy);
            return body > cut ? body : cut;
        }
    };

    // How an icon is drawn: in text (its font cell), or as the A bubble's two frames:
    // the button up (a 39-pixel button at (4, 4) of 48, lit from above, its shadow down and right) and pressed (36 of
    // 40, centred, its shading reversed).
    enum { LookText, LookUp, LookPressed };

    // The icon of `glyph` at `place` in `style`, of `model`'s where that has its own, on the canvas (cleared first); false
    // when the game's own shows (HOME, but for the Steam model's), or the model is another family's.
    bool paint(Canvas& rCanvas, s32 style, s32 glyph, s32 place, s32 look, s32 model = NEBULA_ICON_MODEL_STANDARD) {
        static const char* const cNintendoFaces[4] = {"B", "A", "Y", "X"};
        static const char* const cXboxLetters[4] = {"A", "B", "X", "Y"};
        static const char* const cShoulders[3][2] = {{"LB", "RB"}, {"L", "R"}, {"L1", "R1"}};
        static const char* const cTriggers[3][2] = {{"LT", "RT"}, {"ZL", "ZR"}, {"L2", "R2"}};
        static const char* const cSticks[3][2] = {{"LS", "RS"}, {"L", "R"}, {"L3", "R3"}};
        static const char* const cGrips[3][4] = {{"L4", "R4", "L5", "R5"}, {"GL", "GR", "GL2", "GR2"}, {"LB", "RB", "L5", "R5"}};
        static const char* const cTrackpads[2] = {"LP", "RP"};
        // The GameCube pad's analog L and R are its triggers, its ZL and Z its shoulders.
        static const char* const cGameCubeShoulders[2] = {"ZL", "Z"};
        static const char* const cGameCubeTriggers[2] = {"L", "R"};
        const s32 family = style == NEBULA_ICON_STYLE_XBOX ? 0 : style == NEBULA_ICON_STYLE_NINTENDO ? 1 : 2;
        const bool xbox = family == 0;
        const bool sixaxis = model == NEBULA_ICON_MODEL_SIXAXIS;
        const bool gameCube = model == NEBULA_ICON_MODEL_GAMECUBE;
        const bool steam = model == NEBULA_ICON_MODEL_STEAM;
        const bool edge = model == NEBULA_ICON_MODEL_DUALSENSE_EDGE;
        if ((sixaxis && family != 2) || (edge && family != 2) || (gameCube && family != 1) || (steam && family != 0)) {
            return false;
        }
        const s32 width = rCanvas.mWidth;
        const s32 height = rCanvas.mHeight;
        const f32 side = static_cast< f32 >(width < height ? width : height);
        const f32 diameter = look == LookUp ? side * (39.0f / 48.0f) : look == LookPressed ? side * (36.0f / 40.0f) : side - 2.0f * rCanvas.mScale;
        const f32 cx = look == LookUp ? side * (4.0f / 48.0f) + diameter * 0.5f : width * 0.5f;
        const f32 cy = look == LookUp ? side * (4.0f / 48.0f) + diameter * 0.5f : height * 0.5f;
        const f32 ring = diameter * 0.07f;
        const Rgba ringColor = rgba(cRing);
        const Rgba ink = rgba(xbox ? cWhite : cInk);
        // Pressed, the face is lit from below and a little darker.
        const bool pressed = look == LookPressed;
        const Rgba lit = rgba(xbox ? cDarkTop : cWhiteTop);
        const Rgba shade = rgba(xbox ? cDarkBottom : cWhiteBottom);
        const Rgba faceTop = pressed ? darkened(shade) : lit;
        const Rgba faceBottom = pressed ? darkened(lit) : shade;
        rCanvas.clear();
        if (look == LookUp) {
            // Its shadow, soft-edged (two of the game's pixels), down and right.
            const Shape shadow = {cx + diameter * 0.07f, cy + diameter * 0.07f, diameter * 0.5f, diameter * 0.5f, diameter * 0.5f};
            const Rgba dark = {0.0f, 0.0f, 0.0f, 0.4f};
            const f32 soft = 2.0f * rCanvas.mScale;
            for (s32 y = rCanvas.top(); y < rCanvas.bottom(); y++) {
                for (s32 x = 0; x < width; x++) {
                    rCanvas.blend(x, y, dark, NebulaUi::clamp((rCanvas.mScale - shadow.distance(x + 0.5f, y + 0.5f)) / soft, 0.0f, 1.0f));
                }
            }
        }
        const Shape circle = {cx, cy, diameter * 0.5f, diameter * 0.5f, diameter * 0.5f};
        const Shape pill = {cx, cy, diameter * 0.5f, diameter * 0.32f, diameter * 0.32f};
        const Shape box = {cx, cy, diameter * 0.46f, diameter * 0.4f, diameter * 0.16f};
        switch (glyph) {
        case NEBULA_ICON_FACE: {
            const s32 p = place & 3;
            if (gameCube) {
                // The pad's own shapes and colours by place (Nintendo's positional letters B, A, Y, X): A the large green round
                // button, B the small red one, Y a grey kidney lying and X one upright, each dented toward A.
                const Rgba fill = rgba(p == 1 ? cGameCubeA : p == 0 ? cGameCubeB : cGameCubeXY);
                const Rgba top = lightened(fill);
                const Rgba from = pressed ? darkened(fill) : top;
                const Rgba to = pressed ? darkened(top) : fill;
                const Rgba letter = rgba(p >= 2 ? cInk : cWhite);
                if (p >= 2) {
                    const bool upright = p == 3;
                    const f32 length = diameter * 0.48f;
                    const f32 across = diameter * 0.27f;
                    const Bean kidney = {cx, cy, upright ? across : length, upright ? length : across, upright ? -1.0f : 1.0f, upright};
                    drawShape(rCanvas, kidney, ringColor, ring, from, to);
                    // The letter sits clear of the dent.
                    drawText(rCanvas, cNintendoFaces[p], upright ? cx + diameter * 0.04f : cx, upright ? cy : cy - diameter * 0.04f, diameter * 0.36f, letter);
                } else {
                    const f32 radius = diameter * (p == 1 ? 0.5f : 0.4f);
                    const Shape button = {cx, cy, radius, radius, radius};
                    drawShape(rCanvas, button, ringColor, ring, from, to);
                    drawText(rCanvas, cNintendoFaces[p], cx, cy, radius * 0.92f, letter);
                }
            } else if (xbox) {
                // Y's letter dark on its yellow.
                const Rgba fill = rgba(cXboxFaces[p]);
                const Rgba top = {fill.r + (1.0f - fill.r) * 0.25f, fill.g + (1.0f - fill.g) * 0.25f, fill.b + (1.0f - fill.b) * 0.25f, 1.0f};
                drawShape(rCanvas, circle, ringColor, ring, pressed ? darkened(fill) : top, pressed ? darkened(top) : fill);
                drawText(rCanvas, cXboxLetters[p], cx, cy, diameter * 0.44f, rgba(p == 3 ? 0x3A3A3AFF : cWhite));
            } else {
                drawShape(rCanvas, circle, ringColor, ring, faceTop, faceBottom);
                if (family == 1) {
                    drawText(rCanvas, cNintendoFaces[p], cx, cy, diameter * 0.46f, ink);
                } else {
                    drawPlayStationSymbol(rCanvas, p, cx, cy, diameter, ink);
                }
            }
            return true;
        }
        case NEBULA_ICON_SHOULDER:
            drawShape(rCanvas, pill, ringColor, ring, faceTop, faceBottom);
            drawText(rCanvas, gameCube ? cGameCubeShoulders[place & 1] : cShoulders[family][place & 1], cx, cy, diameter * 0.3f, ink);
            return true;
        case NEBULA_ICON_TRIGGER:
            drawShape(rCanvas, box, ringColor, ring, faceTop, faceBottom);
            drawText(rCanvas, gameCube ? cGameCubeTriggers[place & 1] : cTriggers[family][place & 1], cx, cy, diameter * 0.3f, ink);
            return true;
        case NEBULA_ICON_STICK: {
            // The stick's cap and its rim.
            drawShape(rCanvas, circle, ringColor, ring, faceTop, faceBottom);
            const Shape rim = {cx, cy, diameter * 0.36f, diameter * 0.36f, diameter * 0.36f};
            drawShape(rCanvas, rim, rgba(xbox ? 0x6A6E78FF : 0xB8C0D0FF), ring * 0.7f, faceTop, faceBottom);
            // The GameCube pad's right stick is its C stick.
            drawText(rCanvas, gameCube && (place & 1) != 0 ? "C" : cSticks[family][place & 1], cx, cy, diameter * (family == 1 ? 0.32f : 0.24f), ink);
            return true;
        }
        case NEBULA_ICON_START:
            if (sixaxis || gameCube) {
                drawShape(rCanvas, pill, ringColor, ring, faceTop, faceBottom);
                drawText(rCanvas, "START", cx, cy, diameter * 0.17f, ink);
                return true;
            }
            if (family == 1) {
                // Plus: a round button with a plus sign.
                drawShape(rCanvas, circle, ringColor, ring, faceTop, faceBottom);
                drawBar(rCanvas, cx, cy, diameter * 0.24f, diameter * 0.05f, ink);
                drawBar(rCanvas, cx, cy, diameter * 0.05f, diameter * 0.24f, ink);
                return true;
            }
            drawShape(rCanvas, family == 2 ? pill : circle, ringColor, ring, faceTop, faceBottom);
            // Three lines (Menu, Options).
            for (s32 line = -1; line <= 1; line++) {
                drawBar(rCanvas, cx, cy + line * diameter * 0.15f, diameter * 0.2f, diameter * 0.035f, ink);
            }
            return true;
        case NEBULA_ICON_BACK:
            if (sixaxis) {
                drawShape(rCanvas, pill, ringColor, ring, faceTop, faceBottom);
                drawText(rCanvas, "SELECT", cx, cy, diameter * 0.15f, ink);
                return true;
            }
            if (family == 1) {
                // Minus: a round button with a minus sign.
                drawShape(rCanvas, circle, ringColor, ring, faceTop, faceBottom);
                drawBar(rCanvas, cx, cy, diameter * 0.24f, diameter * 0.05f, ink);
                return true;
            }
            drawShape(rCanvas, family == 2 ? pill : circle, ringColor, ring, faceTop, faceBottom);
            if (xbox) {
                // View: two windows, one over the other.
                const Shape back = {cx - diameter * 0.05f, cy - diameter * 0.05f, diameter * 0.15f, diameter * 0.12f, diameter * 0.03f};
                const Shape front = {cx + diameter * 0.06f, cy + diameter * 0.06f, diameter * 0.15f, diameter * 0.12f, diameter * 0.03f};
                drawShape(rCanvas, back, ink, diameter * 0.05f, faceBottom, faceBottom);
                drawShape(rCanvas, front, ink, diameter * 0.05f, faceTop, faceBottom);
            } else {
                // Share and Create: three short rays.
                for (s32 ray = -1; ray <= 1; ray++) {
                    drawBar(rCanvas, cx + ray * diameter * 0.14f, cy, diameter * 0.035f, diameter * 0.12f, ink);
                }
            }
            return true;
        case NEBULA_ICON_CAPTURE: {
            static const char* const cCaptures[3] = {"SHARE", "CAP", "PAD"};
            if (steam) {
                // Quick access: a round button with three dots.
                drawShape(rCanvas, circle, ringColor, ring, faceTop, faceBottom);
                for (s32 dot = -1; dot <= 1; dot++) {
                    const Shape bead = {cx + dot * diameter * 0.2f, cy, diameter * 0.055f, diameter * 0.055f, diameter * 0.055f};
                    drawShape(rCanvas, bead, ink, 0.0f, ink, ink);
                }
                return true;
            }
            drawShape(rCanvas, pill, ringColor, ring, faceTop, faceBottom);
            drawText(rCanvas, cCaptures[family], cx, cy, diameter * (family == 0 ? 0.17f : 0.26f), ink);
            return true;
        }
        case NEBULA_ICON_CHAT: {
            static const char* const cChats[3] = {"CHAT", "C", "MUTE"};
            drawShape(rCanvas, family == 1 ? circle : pill, ringColor, ring, faceTop, faceBottom);
            drawText(rCanvas, cChats[family], cx, cy, diameter * (family == 1 ? 0.44f : 0.2f), ink);
            return true;
        }
        case NEBULA_ICON_HOME:
            // Only the Steam model draws HOME: its Steam button, a round button named in letters.
            if (!steam) {
                return false;
            }
            drawShape(rCanvas, circle, ringColor, ring, faceTop, faceBottom);
            drawText(rCanvas, "STEAM", cx, cy, diameter * 0.16f, ink);
            return true;
        case NEBULA_ICON_GRIP:
            if (steam || edge) {
                // A back button, tall and narrow: the Steam controllers' L4 R4 L5 R5, the Edge's paddles; the Edge's Fn buttons
                // (4 left, 5 right) lie flat, FN over the side.
                if (edge && place >= 4) {
                    drawShape(rCanvas, box, ringColor, ring, faceTop, faceBottom);
                    drawText(rCanvas, "FN", cx, cy - diameter * 0.1f, diameter * 0.3f, ink);
                    drawText(rCanvas, (place & 1) != 0 ? "R" : "L", cx, cy + diameter * 0.22f, diameter * 0.18f, ink);
                    return true;
                }
                const Shape paddle = {cx, cy, diameter * 0.26f, diameter * 0.5f, diameter * 0.22f};
                drawShape(rCanvas, paddle, ringColor, ring, faceTop, faceBottom);
                if (steam) {
                    drawText(rCanvas, cGrips[0][place & 3], cx, cy, diameter * 0.26f, ink);
                } else {
                    // The Edge's paddle: the side, and two grooves.
                    drawText(rCanvas, (place & 1) != 0 ? "R" : "L", cx, cy - diameter * 0.14f, diameter * 0.3f, ink);
                    drawBar(rCanvas, cx, cy + diameter * 0.14f, diameter * 0.1f, diameter * 0.025f, ink);
                    drawBar(rCanvas, cx, cy + diameter * 0.26f, diameter * 0.1f, diameter * 0.025f, ink);
                }
                return true;
            }
            drawShape(rCanvas, box, ringColor, ring, faceTop, faceBottom);
            drawText(rCanvas, cGrips[family][place & 3], cx, cy, diameter * 0.26f, ink);
            return true;
        case NEBULA_ICON_TRACKPAD:
            if (steam) {
                // A round pad with an inner ring, the side in its middle.
                drawShape(rCanvas, circle, ringColor, ring, faceTop, faceBottom);
                const Shape inner = {cx, cy, diameter * 0.34f, diameter * 0.34f, diameter * 0.34f};
                drawShape(rCanvas, inner, rgba(cPadRim), ring * 0.7f, faceTop, faceBottom);
                drawText(rCanvas, cTrackpads[place & 1], cx, cy, diameter * 0.26f, ink);
                return true;
            }
            drawShape(rCanvas, box, ringColor, ring, faceTop, faceBottom);
            drawText(rCanvas, cTrackpads[place & 1], cx, cy, diameter * 0.3f, ink);
            return true;
        case NEBULA_ICON_DPAD: {
            // The cross, the direction pressed filled with ink from the centre's edge to the tip.
            const f32 arm = diameter * 0.5f;
            const f32 half = diameter * 0.18f;
            const Cross cross = {cx, cy, arm, half, diameter * 0.05f};
            drawShape(rCanvas, cross, ringColor, ring, faceTop, faceBottom);
            const s32 p = place & 3;
            const f32 dx = p == 2 ? -1.0f : p == 3 ? 1.0f : 0.0f;
            const f32 dy = p == 0 ? -1.0f : p == 1 ? 1.0f : 0.0f;
            const f32 inset = ring * 1.5f;
            const f32 length = (arm - half - inset) * 0.5f;
            const f32 middle = half + length;
            const f32 across = half - inset;
            const Shape pressedArm = {cx + dx * middle, cy + dy * middle, dx != 0.0f ? length : across, dx != 0.0f ? across : length,
                                      diameter * 0.03f};
            drawShape(rCanvas, pressedArm, ink, 0.0f, ink, ink);
            return true;
        }
        case NEBULA_ICON_STICK_AXIS: {
            // The stick's cap, the way it is pushed, and which stick.
            static const char* const cArrows[4] = {"\x01", "\x02", "\x03", "\x04"};
            drawShape(rCanvas, circle, ringColor, ring, faceTop, faceBottom);
            drawText(rCanvas, cArrows[place & 3], cx, cy - diameter * 0.1f, diameter * 0.3f, ink);
            drawText(rCanvas, (place & 4) != 0 ? "R" : "L", cx, cy + diameter * 0.24f, diameter * 0.18f, ink);
            return true;
        }
        default:
            return false;
        }
    }

    // The keyboard's icons: a keycap for each key (NEBULA_KEY_*) in the icons' look (the window's ink round it, a face
    // lit from above), its side showing beneath its face as a key's does; its legend in the icons' letters (words on a
    // wide key, two lines where a keyboard prints two, the side of a key that has one of each as a small L or R); a mouse
    // for each mouse button, the button in the menu's blue (its side buttons drawn on theirs alone). The Control Pad:
    // its four keys as a keyboard's arrows sit.
    enum { KeySquare, KeyWide, KeyMouse };
    const s32 cKeyPad = NEBULA_KEYS;  // the Control Pad's
    // A key without its own icon (NEBULA_KEY_OTHER) on a cell of each icon's own, its name (NebulaButtonIcons::other)
    // on it: two such keys at once each show theirs.
    const s32 cKeyNamed = NEBULA_KEYS + 1;
    const s32 cKeyCells = cKeyNamed + NEBULA_BUTTON_ICONS;
    const u32 cKeySideTop = 0xBDC6D6FF;
    const u32 cKeySideBottom = NEBULA_RGBA(0x8F, 0x9A, 0xB0, 0xFF);
    const u32 cKeyEdge = 0xB4BDCDFF;
    const u32 cKeyAccentTop = 0x5D9CF2FF;
    const u32 cKeyAccentBottom = 0x2F6FD0FF;  // the menu's blue (NebulaSettingsMenu cAccent)
    const f32 cSpaceAdvance = 0.32f;

    // A line of a legend: codes of cLetters (' ' a space), its capitals' size against the legend's.
    struct KeyLine {
        MR::InlineVector< u16, 8 > mCodes;
        f32 mScale;
    };
    struct KeyLegend {
        s32 mShape;
        KeyLine mLine[2];
        s32 mLineCount;
        u16 mTag;  // L or R: the key of a pair (Shift, Ctrl, Alt, Windows) on that side
    };

    void setLine(KeyLine& rLine, const char* pText, f32 scale) {
        rLine.mCodes.clear();
        rLine.mScale = scale;
        for (const char* p = pText; *p != 0; p++) {
            rLine.mCodes.push_back(static_cast< u8 >(*p));
        }
    }

    // The named keys' legends, Space to the right Windows key (NEBULA_KEY_SPACE on): codes 1 to 4 are the arrows up,
    // down, left and right, 5 Backspace's long arrow.
    struct NamedKey {
        u8 mShape;
        const char* mTop;
        const char* mBottom;  // a second line, as large
        char mTag;
    };
    const NamedKey cNamedKeys[NEBULA_KEY_LAYOUT_FIRST - NEBULA_KEY_SPACE] = {
        {KeyWide, "SPACE", nullptr, 0}, {KeyWide, "ENTER", nullptr, 0}, {KeyWide, "TAB", nullptr, 0},
        {KeyWide, "\x05", nullptr, 0},  {KeyWide, "ESC", nullptr, 0},   {KeyWide, "SHIFT", nullptr, 0},
        {KeyWide, "SHIFT", nullptr, 'L'}, {KeyWide, "SHIFT", nullptr, 'R'}, {KeyWide, "CTRL", nullptr, 0},
        {KeyWide, "CTRL", nullptr, 'L'}, {KeyWide, "CTRL", nullptr, 'R'}, {KeyWide, "ALT", nullptr, 0},
        {KeyWide, "ALT", nullptr, 'L'}, {KeyWide, "ALT", nullptr, 'R'}, {KeySquare, "\x01", nullptr, 0},
        {KeySquare, "\x02", nullptr, 0}, {KeySquare, "\x03", nullptr, 0}, {KeySquare, "\x04", nullptr, 0},
        {KeyWide, "INS", nullptr, 0},   {KeyWide, "DEL", nullptr, 0},   {KeyWide, "HOME", nullptr, 0},
        {KeyWide, "END", nullptr, 0},   {KeyWide, "PG UP", nullptr, 0}, {KeyWide, "PG DN", nullptr, 0},
        {KeyWide, "CAPS", nullptr, 0},  {KeyWide, "NUM", "LOCK", 0},    {KeyWide, "SCR", "LOCK", 0},
        {KeyWide, "PAUSE", nullptr, 0}, {KeyWide, "PRT SC", nullptr, 0}, {KeyWide, "MENU", nullptr, 0},
        {KeyWide, "WIN", nullptr, 'L'}, {KeyWide, "WIN", nullptr, 'R'},
    };
    // What the layout keys print on a US keyboard (; = , - . / ` [ \ ] ', the ISO key's \), and OEM 8, which a US
    // keyboard lacks, as a UK one prints it.
    const u8 cUsLayout[NEBULA_KEY_LAYOUT_KEYS] = {';', '=', ',', '-', '.', '/', '`', '[', '\\', ']', '\'', '`', '\\'};

    // Key `slot`'s legend; `layout`: what the layout keys print (NebulaButtonIcons::layout; 0 or a character the icons'
    // letters lack: the US legend); `name`: a named cell's key's name.
    void keyLegend(KeyLegend& k, s32 slot, const u16* pLayout, const char* pName) {
        k.mShape = KeySquare;
        k.mLineCount = 1;
        k.mTag = 0;
        k.mLine[0].mCodes.clear();
        k.mLine[0].mScale = 1.0f;
        k.mLine[1].mCodes.clear();
        k.mLine[1].mScale = 0.0f;
        char text[8] = {0};  // one key's letter, digit or F number
        if (slot >= cKeyNamed) {
            const char* pText = pName != nullptr ? pName : "";
            k.mShape = strlen(pText) > 2 ? KeyWide : KeySquare;
            setLine(k.mLine[0], pText, 1.0f);
        } else if (slot < NEBULA_KEY_0) {
            text[0] = static_cast< char >('A' + slot);
            setLine(k.mLine[0], text, 1.0f);
        } else if (slot < NEBULA_KEY_F1) {
            text[0] = static_cast< char >('0' + slot - NEBULA_KEY_0);
            setLine(k.mLine[0], text, 1.0f);
        } else if (slot < NEBULA_KEY_NUM0) {
            nebula_snprintf(text, sizeof(text), "F%d", static_cast< int >(slot - NEBULA_KEY_F1 + 1));
            setLine(k.mLine[0], text, 1.0f);
        } else if (slot <= NEBULA_KEY_NUM_DECIMAL) {
            // The number pad's: NUM over its own legend.
            text[0] = slot < NEBULA_KEY_NUM_ADD ? static_cast< char >('0' + slot - NEBULA_KEY_NUM0) : "+-*/."[slot - NEBULA_KEY_NUM_ADD];
            setLine(k.mLine[0], "NUM", 0.46f);
            setLine(k.mLine[1], text, 1.0f);
            k.mLineCount = 2;
        } else if (slot < NEBULA_KEY_LAYOUT_FIRST) {
            const NamedKey& rNamed = cNamedKeys[slot - NEBULA_KEY_SPACE];
            k.mShape = rNamed.mShape;
            setLine(k.mLine[0], rNamed.mTop, 1.0f);
            if (rNamed.mBottom != nullptr) {
                setLine(k.mLine[1], rNamed.mBottom, 1.0f);
                k.mLineCount = 2;
            }
            k.mTag = static_cast< u8 >(rNamed.mTag);
        } else if (slot < NEBULA_KEY_MOUSE_LEFT) {
            const s32 index = slot - NEBULA_KEY_LAYOUT_FIRST;
            const u16 printed = pLayout != nullptr ? pLayout[index] : 0;
            const u16 code = printed != 0 && findLetter(printed) != nullptr ? printed : cUsLayout[index];
            if (code != 0) {
                k.mLine[0].mCodes.push_back(code);
            }
        } else if (slot < NEBULA_KEY_OTHER) {
            k.mShape = KeyMouse;
            k.mLineCount = 0;
        } else {
            k.mLineCount = 0;  // any other key: a key, unnamed
        }
    }

    // A line's width and its ink's top and foot, in capitals (an accented capital's mark above, a cedilla below).
    void lineExtent(const KeyLine& rLine, f32* pWidth, f32* pTop, f32* pFoot) {
        f32 width = 0.0f;
        f32 top = 0.0f;
        f32 foot = 1.0f;
        for (s32 i = 0; i < rLine.mCodes.size(); i++) {
            const Letter* pLetter = rLine.mCodes[i] == ' ' ? nullptr : findLetter(rLine.mCodes[i]);
            width +=(i > 0 ? cLetterGap : 0.0f) + (pLetter != nullptr ? pLetter->mAdvance : cSpaceAdvance);
            for (s32 s = 0; pLetter != nullptr && s < pLetter->mCount; s++) {
                const Stroke& rStroke = cStrokes[pLetter->mFirst + s];
                f32 low;
                f32 high;
                if (rStroke.mArc == 0) {
                    low = rStroke.mB < rStroke.mD ? rStroke.mB : rStroke.mD;
                    high = rStroke.mB < rStroke.mD ? rStroke.mD : rStroke.mB;
                } else {
                    // Its ends, and its top and foot where it turns through them (clockwise from (mD, mE) to (mF, mG)).
                    const f32 a = rStroke.mB + rStroke.mC * rStroke.mE;
                    const f32 b = rStroke.mB + rStroke.mC * rStroke.mG;
                    low = rStroke.mD <= 0.0f && rStroke.mF >= 0.0f ? rStroke.mB - rStroke.mC : (a < b ? a : b);
                    high = rStroke.mD >= 0.0f && rStroke.mF <= 0.0f ? rStroke.mB + rStroke.mC : (a < b ? b : a);
                }
                top = low - cStrokeHalf < top ? low - cStrokeHalf : top;
                foot = high + cStrokeHalf > foot ? high + cStrokeHalf : foot;
            }
        }
        *pWidth = width;
        *pTop = top;
        *pFoot = foot;
    }

    // A line's letters in `rInk`, its ink's middle at `x`, its capitals' top at `top`, `capital` pixels tall.
    void drawLine(Canvas& rCanvas, const KeyLine& rLine, f32 x, f32 top, f32 capital, const Rgba& rInk) {
        f32 width, inkTop, inkFoot;
        lineExtent(rLine, &width, &inkTop, &inkFoot);
        f32 left = x - width * capital * 0.5f;
        for (s32 i = 0; i < rLine.mCodes.size(); i++) {
            const Letter* pLetter = rLine.mCodes[i] == ' ' ? nullptr : findLetter(rLine.mCodes[i]);
            if (pLetter == nullptr) {
                left += (cSpaceAdvance + cLetterGap) * capital;
                continue;
            }
            const f32 margin = capital * cStrokeHalf + 1.0f;
            const s32 x0 = static_cast< s32 >(NebulaUi::clamp(left - margin, 0.0f, static_cast< f32 >(rCanvas.mWidth)));
            const s32 x1 = static_cast< s32 >(NebulaUi::clamp(left + pLetter->mAdvance * capital + margin + 1.0f, 0.0f, static_cast< f32 >(rCanvas.mWidth)));
            const s32 y0 = static_cast< s32 >(NebulaUi::clamp(top + inkTop * capital - margin, static_cast< f32 >(rCanvas.top()), static_cast< f32 >(rCanvas.bottom())));
            const s32 y1 = static_cast< s32 >(NebulaUi::clamp(top + inkFoot * capital + margin + 1.0f, static_cast< f32 >(rCanvas.top()), static_cast< f32 >(rCanvas.bottom())));
            for (s32 py = y0; py < y1; py++) {
                for (s32 px = x0; px < x1; px++) {
                    const f32 d = letterDistance(*pLetter, (px + 0.5f - left) / capital, (py + 0.5f - top) / capital) * capital;
                    rCanvas.blend(px, py, rInk, coverOf(d - cStrokeHalf * capital));
                }
            }
            left += (pLetter->mAdvance + cLetterGap) * capital;
        }
    }

    // A legend in the face (its middle (x, y), `halfWidth` x `halfHeight`): as large as its lines fit, a single letter
    // no larger than a key's own, a tag to the left of the rest.
    void drawLegend(Canvas& rCanvas, const KeyLegend& rLegend, f32 x, f32 y, f32 halfWidth, f32 halfHeight, const Rgba& rInk) {
        if (rLegend.mLineCount == 0) {
            return;
        }
        f32 widths[2] = {0.0f, 0.0f};
        f32 tops[2] = {0.0f, 0.0f};
        f32 feet[2] = {1.0f, 1.0f};
        f32 height = 0.0f;
        f32 widest = 0.0f;
        const f32 lineGap = 0.3f;
        for (s32 i = 0; i < rLegend.mLineCount; i++) {
            lineExtent(rLegend.mLine[i], &widths[i], &tops[i], &feet[i]);
            const f32 scale = rLegend.mLine[i].mScale;
            height += (feet[i] - tops[i]) * scale + (i > 0 ? lineGap : 0.0f);
            widest = widths[i] * scale > widest ? widths[i] * scale : widest;
        }
        // The tag's room on the left: its letter at half the legend's size.
        KeyLine tag;
        tag.mScale = 0.5f;
        if (rLegend.mTag != 0) {
            tag.mCodes.push_back(rLegend.mTag);
        }
        f32 tagWidth = 0.0f, tagTop = 0.0f, tagFoot = 1.0f;
        if (tag.mCodes.size() != 0) {
            lineExtent(tag, &tagWidth, &tagTop, &tagFoot);
        }
        const f32 room = tag.mCodes.size() != 0 ? (tagWidth * tag.mScale + 0.55f) : 0.0f;
        const f32 single = rLegend.mLineCount == 1 && rLegend.mLine[0].mCodes.size() == 1 ? 0.62f : 0.72f;
        f32 capital = halfHeight * 2.0f * single / height;
        const f32 byWidth = halfWidth * 2.0f * 0.84f / (widest + room);
        capital = byWidth < capital ? byWidth : capital;
        const f32 shift = room * capital * 0.5f;
        f32 top = y - height * capital * 0.5f;
        for (s32 i = 0; i < rLegend.mLineCount; i++) {
            const f32 lineCapital = capital * rLegend.mLine[i].mScale;
            drawLine(rCanvas, rLegend.mLine[i], x + shift, top - tops[i] * lineCapital, lineCapital, rInk);
            top += (feet[i] - tops[i]) * lineCapital + lineGap * capital;
        }
        if (tag.mCodes.size() != 0) {
            const f32 tagCapital = capital * tag.mScale;
            const f32 centre = x + shift - widest * capital * 0.5f - 0.55f * capital * 0.5f - tagWidth * tagCapital * 0.5f;
            drawLine(rCanvas, tag, centre, y - tagCapital * 0.5f, tagCapital, rInk);
        }
    }

    Rgba lerpRgba(const Rgba& rA, const Rgba& rB, f32 t) {
        const Rgba c = {rA.r + (rB.r - rA.r) * t, rA.g + (rB.g - rA.g) * t, rA.b + (rB.b - rA.b) * t, rA.a + (rB.a - rA.a) * t};
        return c;
    }

    // A keycap `halfWidth` x `halfHeight` from its middle (x, y), `unit` the size its ring and edges are drawn for: its
    // body ringed in the window's ink, the side below its face; pressed, the face sits down in it. Returns the face.
    Shape drawKeyCap(Canvas& rCanvas, f32 x, f32 y, f32 halfWidth, f32 halfHeight, f32 unit, bool pressed) {
        const f32 ring = unit * 0.06f;
        const f32 smaller = halfWidth < halfHeight ? halfWidth : halfHeight;
        const Shape body = {x, y, halfWidth, halfHeight, smaller * 0.45f};
        drawShape(rCanvas, body, rgba(cRing), ring, rgba(cKeySideTop), rgba(cKeySideBottom));
        const f32 inset = unit * 0.04f;
        const f32 front = pressed ? inset : unit * 0.11f;  // the side showing beneath the face
        const Shape face = {x, y - (front - inset) * 0.5f, halfWidth - ring - inset, halfHeight - ring - (inset + front) * 0.5f,
                            smaller * 0.36f};
        const Rgba top = rgba(cWhiteTop);
        const Rgba bottom = rgba(cWhiteBottom);
        drawShape(rCanvas, face, rgba(cKeyEdge), unit * 0.014f, pressed ? darkened(bottom) : top, pressed ? darkened(top) : bottom);
        return face;
    }

    // A mouse, `unit` its cell's size, with `button` (NEBULA_KEY_MOUSE_*) in the menu's blue.
    void drawMouse(Canvas& rCanvas, f32 x, f32 y, f32 unit, s32 button, bool pressed) {
        const f32 ring = unit * 0.06f;
        const f32 halfWidth = unit * 0.27f;
        const f32 halfHeight = unit * 0.42f;
        const Rgba ink = rgba(cRing);
        const Rgba faceTop = pressed ? darkened(rgba(cWhiteBottom)) : rgba(cWhiteTop);
        const Rgba faceBottom = pressed ? darkened(rgba(cWhiteTop)) : rgba(cWhiteBottom);
        const Rgba accentTop = rgba(cKeyAccentTop);
        const Rgba accentBottom = rgba(cKeyAccentBottom);
        // Its side buttons (forward over back, standing out from its left side) on their own icons only.
        for (s32 i = 0; i < 2 && (button == NEBULA_KEY_MOUSE_BACK || button == NEBULA_KEY_MOUSE_FORWARD); i++) {
            const bool lit = button == (i == 0 ? NEBULA_KEY_MOUSE_FORWARD : NEBULA_KEY_MOUSE_BACK);
            const Shape side = {x - halfWidth - unit * 0.035f, y + unit * (i == 0 ? -0.04f : 0.16f), unit * 0.095f, unit * 0.075f,
                                unit * 0.06f};
            drawShape(rCanvas, side, ink, ring * 0.6f, lit ? accentTop : faceTop, lit ? accentBottom : faceBottom);
        }
        const Shape body = {x, y, halfWidth, halfHeight, halfWidth};
        drawShape(rCanvas, body, ink, ring, faceTop, faceBottom);
        // Its buttons: the top above `split`, halved; the lit one filled in the blue.
        const f32 split = y - unit * 0.05f;
        const bool left = button == NEBULA_KEY_MOUSE_LEFT;
        const bool right = button == NEBULA_KEY_MOUSE_RIGHT;
        for (s32 py = 0; py < rCanvas.mHeight; py++) {
            const f32 fy = py + 0.5f;
            const f32 t = NebulaUi::clamp((fy - (y - halfHeight)) / (split - (y - halfHeight)), 0.0f, 1.0f);
            const Rgba accent = lerpRgba(accentTop, accentBottom, t);
            for (s32 px = 0; px < rCanvas.mWidth; px++) {
                const f32 fx = px + 0.5f;
                const f32 inside = coverOf(body.distance(fx, fy) + ring);
                if (inside <= 0.0f) {
                    continue;
                }
                if (left || right) {
                    const f32 cover = inside * coverOf(fy - split) * coverOf(left ? fx - x : x - fx);
                    rCanvas.blend(px, py, accent, cover);
                }
                // The lines between the buttons and under them.
                const f32 vertical = fy < split ? __builtin_fabsf(fx - x) : 1e9f;
                const f32 across = __builtin_fabsf(fy - split);
                const f32 line = vertical < across ? vertical : across;
                rCanvas.blend(px, py, ink, inside * coverOf(line - unit * 0.017f));
            }
        }
        // The wheel.
        const bool wheel = button == NEBULA_KEY_MOUSE_MIDDLE;
        const Shape roll = {x, y - unit * 0.22f, unit * 0.068f, unit * 0.11f, unit * 0.068f};
        drawShape(rCanvas, roll, ink, ring * 0.5f, wheel ? accentTop : rgba(NEBULA_RGBA(0x9A, 0xA3, 0xB5, 0xFF)), wheel ? accentBottom : rgba(0x7A8396FF));
    }

    // The Control Pad's four keys (`pPadKeys`: up, down, left, right; NEBULA_KEYS or more: none), small keycaps as a
    // keyboard's arrows sit, each with its legend where it is one letter or picture.
    void drawPadKeys(Canvas& rCanvas, f32 x, f32 y, f32 unit, const u8* pPadKeys, const u16* pLayout) {
        const f32 half = unit * 0.155f;
        const f32 step = half * 2.0f + unit * 0.025f;
        const f32 places[4][2] = {{x, y - step * 0.5f}, {x, y + step * 0.5f}, {x - step, y + step * 0.5f}, {x + step, y + step * 0.5f}};
        for (s32 i = 0; i < 4; i++) {
            const Shape face = drawKeyCap(rCanvas, places[i][0], places[i][1], half, half, unit * 0.5f, false);
            if (pPadKeys[i] >= NEBULA_KEYS) {
                continue;
            }
            KeyLegend legend;
            keyLegend(legend, pPadKeys[i], pLayout, nullptr);
            if (legend.mShape != KeySquare || legend.mLineCount != 1 || legend.mLine[0].mCodes.size() != 1) {
                continue;  // a word does not fit: the key shows alone
            }
            drawLegend(rCanvas, legend, face.mX, face.mY, face.mHalfWidth, face.mHalfHeight, rgba(cInk));
        }
    }

    // Key `slot`'s icon (or the Control Pad's, cKeyPad; a named cell's with its key's `name`) in `look` on the canvas,
    // cleared first; a named cell without a name stays clear.
    void paintKey(Canvas& rCanvas, s32 slot, s32 look, const u16* pLayout, const u8* pPadKeys, const char* pName = nullptr) {
        rCanvas.clear();
        const s32 width = rCanvas.mWidth;
        const s32 height = rCanvas.mHeight;
        const f32 side = static_cast< f32 >(width < height ? width : height);
        // As the other icons: in text the cell less a pixel each side; the A bubble's up (39 of 48 at (4, 4), its shadow
        // down and right) and pressed (36 of 40).
        const f32 unit = look == LookUp ? side * (39.0f / 48.0f) : look == LookPressed ? side * (36.0f / 40.0f) : side - 2.0f * rCanvas.mScale;
        const f32 x = look == LookUp ? side * (4.0f / 48.0f) + unit * 0.5f : width * 0.5f;
        const f32 y = look == LookUp ? side * (4.0f / 48.0f) + unit * 0.5f : height * 0.5f;
        const bool pressed = look == LookPressed;
        if (slot == cKeyPad) {
            drawPadKeys(rCanvas, x, y, unit, pPadKeys, pLayout);
            return;
        }
        if (slot >= cKeyNamed && (pName == nullptr || pName[0] == 0)) {
            return;
        }
        KeyLegend legend;
        keyLegend(legend, slot, pLayout, pName);
        const f32 halfWidth = unit * (legend.mShape == KeyWide ? 0.5f : 0.46f);
        const f32 halfHeight = unit * (legend.mShape == KeyWide ? 0.34f : 0.46f);
        if (look == LookUp) {
            const Shape shadow = legend.mShape == KeyMouse
                                     ? Shape{x + unit * 0.07f, y + unit * 0.07f, unit * 0.27f, unit * 0.42f, unit * 0.27f}
                                     : Shape{x + unit * 0.07f, y + unit * 0.07f, halfWidth, halfHeight, unit * 0.16f};
            const Rgba dark = {0.0f, 0.0f, 0.0f, 0.4f};
            const f32 soft = 2.0f * rCanvas.mScale;
            for (s32 py = 0; py < height; py++) {
                for (s32 px = 0; px < width; px++) {
                    rCanvas.blend(px, py, dark, NebulaUi::clamp((rCanvas.mScale - shadow.distance(px + 0.5f, py + 0.5f)) / soft, 0.0f, 1.0f));
                }
            }
        }
        if (legend.mShape == KeyMouse) {
            drawMouse(rCanvas, x, y, unit, slot, pressed);
            return;
        }
        const Shape face = drawKeyCap(rCanvas, x, y, halfWidth, halfHeight, unit, pressed);
        drawLegend(rCanvas, legend, face.mX, face.mY, face.mHalfWidth, face.mHalfHeight, rgba(cInk));
    }

    // A key whose picture follows the player's layout or bindings (the layout keys, the Control Pad): on sheets of their
    // own after the others, which are the same every run.
    bool keyFollows(s32 slot) {
        return slot >= cKeyPad || (slot >= NEBULA_KEY_LAYOUT_FIRST && slot < NEBULA_KEY_LAYOUT_FIRST + NEBULA_KEY_LAYOUT_KEYS);
    }

    // NEBULA_KEY_OTHER has no cell: an icon shows its own named one.
    bool keyHasCell(s32 slot) {
        return slot != NEBULA_KEY_OTHER;
    }

    // A picture of ours: a texture record (one level of RGBA8 rows) `width` x `height`, its texels cleared.
    void initRecord(u8* pRecord, s32 width, s32 height) {
        nebula_texture_init(pRecord, static_cast< u16 >(width), static_cast< u16 >(height), 1, NEBULA_TEXTURE_RGBA8,
                            0);
        memset(pRecord + NEBULA_TEXTURE_HEADER, 0, static_cast< size_t >(width) * height * 4);
    }

    // A record's texels (RGBA8 rows).
    u8* texelsOf(u8* pRecord) {
        return pRecord + NEBULA_TEXTURE_HEADER;
    }

    // The canvas into a record's texels (`width` wide) at (x, y); a clear texel is all zero (the same bytes wherever
    // nothing is drawn).
    void paintInto(const Canvas& rCanvas, u8* pRecord, s32 width, s32 x, s32 y) {
        u8* pTexels = texelsOf(pRecord);
        for (s32 py = 0; py < rCanvas.mHeight; py++) {
            for (s32 px = 0; px < rCanvas.mWidth; px++) {
                const Rgba& c = rCanvas.mPixels[py * rCanvas.mWidth + px];
                u8* pOut = pTexels + ((y + py) * width + x + px) * 4;
                const u8 a = static_cast< u8 >(NebulaUi::clamp(c.a, 0.0f, 1.0f) * 255.0f + 0.5f);
                pOut[0] = a == 0 ? 0 : static_cast< u8 >(NebulaUi::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
                pOut[1] = a == 0 ? 0 : static_cast< u8 >(NebulaUi::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
                pOut[2] = a == 0 ? 0 : static_cast< u8 >(NebulaUi::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
                pOut[3] = a;
            }
        }
    }

    // A record's texel (x, y) of a picture `width` wide.
    Rgba texelOf(u8* pRecord, s32 width, s32 x, s32 y) {
        const u8* pTexel = texelsOf(pRecord) + (y * width + x) * 4;
        const Rgba c = {pTexel[0] / 255.0f, pTexel[1] / 255.0f, pTexel[2] / 255.0f, pTexel[3] / 255.0f};
        return c;
    }

    // Each family's icons, one to a cell: face buttons by place, shoulders, triggers, sticks, Start, Back, the rarer
    // buttons, then the ones only a model of the family draws (paint leaves those of another family out).
    const s32 cFamilies = 3;  // Xbox, Nintendo, PlayStation: NEBULA_ICON_STYLE_* - 1
    struct AtlasIcon {
        u8 mGlyph;
        u8 mPlace;
        u8 mModel = 0;
    };
    const AtlasIcon cAtlas[] = {
        {NEBULA_ICON_FACE, 0},     // south
        {NEBULA_ICON_FACE, 1},     // east
        {NEBULA_ICON_FACE, 2},     // west
        {NEBULA_ICON_FACE, 3},     // north
        {NEBULA_ICON_SHOULDER, 0}, // left
        {NEBULA_ICON_SHOULDER, 1}, // right
        {NEBULA_ICON_TRIGGER, 0},  // left
        {NEBULA_ICON_TRIGGER, 1},  // right
        {NEBULA_ICON_STICK, 0},    // left
        {NEBULA_ICON_STICK, 1},    // right
        {NEBULA_ICON_START, 0},
        {NEBULA_ICON_BACK, 0},
        {NEBULA_ICON_CAPTURE, 0},
        {NEBULA_ICON_CHAT, 0},
        {NEBULA_ICON_GRIP, 0},     // left
        {NEBULA_ICON_GRIP, 1},     // right
        {NEBULA_ICON_GRIP, 2},     // left 2
        {NEBULA_ICON_GRIP, 3},     // right 2
        {NEBULA_ICON_TRACKPAD, 0}, // left
        {NEBULA_ICON_TRACKPAD, 1}, // right
        {NEBULA_ICON_STICK_AXIS, 0}, // left up
        {NEBULA_ICON_STICK_AXIS, 1}, // left down
        {NEBULA_ICON_STICK_AXIS, 2}, // left left
        {NEBULA_ICON_STICK_AXIS, 3}, // left right
        {NEBULA_ICON_STICK_AXIS, 4}, // right up
        {NEBULA_ICON_STICK_AXIS, 5}, // right down
        {NEBULA_ICON_STICK_AXIS, 6}, // right left
        {NEBULA_ICON_STICK_AXIS, 7}, // right right
        {NEBULA_ICON_DPAD, 0},     // up
        {NEBULA_ICON_DPAD, 1},     // down
        {NEBULA_ICON_DPAD, 2},     // left
        {NEBULA_ICON_DPAD, 3},     // right
        // The ones only a model draws:
        {NEBULA_ICON_START, 0, NEBULA_ICON_MODEL_SIXAXIS},        // start
        {NEBULA_ICON_BACK, 0, NEBULA_ICON_MODEL_SIXAXIS},         // select
        {NEBULA_ICON_SHOULDER, 0, NEBULA_ICON_MODEL_GAMECUBE},    // ZL
        {NEBULA_ICON_SHOULDER, 1, NEBULA_ICON_MODEL_GAMECUBE},    // Z
        {NEBULA_ICON_TRIGGER, 0, NEBULA_ICON_MODEL_GAMECUBE},     // L
        {NEBULA_ICON_TRIGGER, 1, NEBULA_ICON_MODEL_GAMECUBE},     // R
        {NEBULA_ICON_START, 0, NEBULA_ICON_MODEL_GAMECUBE},       // start
        {NEBULA_ICON_STICK, 1, NEBULA_ICON_MODEL_GAMECUBE},       // C stick
        {NEBULA_ICON_FACE, 0, NEBULA_ICON_MODEL_GAMECUBE},        // B
        {NEBULA_ICON_FACE, 1, NEBULA_ICON_MODEL_GAMECUBE},        // A
        {NEBULA_ICON_FACE, 2, NEBULA_ICON_MODEL_GAMECUBE},        // Y
        {NEBULA_ICON_FACE, 3, NEBULA_ICON_MODEL_GAMECUBE},        // X
        {NEBULA_ICON_HOME, 0, NEBULA_ICON_MODEL_STEAM},           // Steam button
        {NEBULA_ICON_CAPTURE, 0, NEBULA_ICON_MODEL_STEAM},        // quick access
        {NEBULA_ICON_GRIP, 0, NEBULA_ICON_MODEL_STEAM},           // L4
        {NEBULA_ICON_GRIP, 1, NEBULA_ICON_MODEL_STEAM},           // R4
        {NEBULA_ICON_GRIP, 2, NEBULA_ICON_MODEL_STEAM},           // L5
        {NEBULA_ICON_GRIP, 3, NEBULA_ICON_MODEL_STEAM},           // R5
        {NEBULA_ICON_TRACKPAD, 0, NEBULA_ICON_MODEL_STEAM},       // left trackpad
        {NEBULA_ICON_TRACKPAD, 1, NEBULA_ICON_MODEL_STEAM},       // right trackpad
        {NEBULA_ICON_GRIP, 0, NEBULA_ICON_MODEL_DUALSENSE_EDGE},  // left paddle
        {NEBULA_ICON_GRIP, 1, NEBULA_ICON_MODEL_DUALSENSE_EDGE},  // right paddle
        {NEBULA_ICON_GRIP, 4, NEBULA_ICON_MODEL_DUALSENSE_EDGE},  // left Fn
        {NEBULA_ICON_GRIP, 5, NEBULA_ICON_MODEL_DUALSENSE_EDGE},  // right Fn
    };
    const s32 cAtlasIcons = sizeof(cAtlas) / sizeof(cAtlas[0]);

    s32 atlasSlot(s32 glyph, s32 place, s32 model) {
        // A model's own, where it has one.
        for (s32 k = 0; model != NEBULA_ICON_MODEL_STANDARD && k < cAtlasIcons; k++) {
            if (cAtlas[k].mModel == model && cAtlas[k].mGlyph == glyph && cAtlas[k].mPlace == place) {
                return k;
            }
        }
        switch (glyph) {
        case NEBULA_ICON_FACE:
            return place & 3;
        case NEBULA_ICON_SHOULDER:
            return 4 + (place & 1);
        case NEBULA_ICON_TRIGGER:
            return 6 + (place & 1);
        case NEBULA_ICON_STICK:
            return 8 + (place & 1);
        case NEBULA_ICON_START:
            return 10;
        case NEBULA_ICON_BACK:
            return 11;
        case NEBULA_ICON_CAPTURE:
            return 12;
        case NEBULA_ICON_CHAT:
            return 13;
        case NEBULA_ICON_GRIP:
            return 14 + (place & 3);
        case NEBULA_ICON_TRACKPAD:
            return 18 + (place & 1);
        case NEBULA_ICON_STICK_AXIS:
            return 20 + (place & 7);
        case NEBULA_ICON_DPAD:
            return 28 + (place & 3);
        default:
            return -1;
        }
    }

    // The picture font as we extend it.
    struct Font {
        Ui::ResFont* mFont;
        u16* mSlot[NEBULA_BUTTON_ICONS];  // each icon code's glyph entry (ResFontBase::GlyphSlot)
        u8* mSheets;                  // the font's sheets (their records as they were), then ours: records mSheetBytes apart
        s32 mSheetBytes;
        s32 mSheetWidth;
        s32 mSheetHeight;
        s32 mCellWidth;   // a cell's pitch less its 1-pixel gap
        s32 mCellHeight;
        s32 mRow;         // cells across a sheet
        s32 mCells;       // cells in a sheet
        s32 mFamilySheets;
        s32 mFirstSheet;  // the first family's first sheet
    s32 mKeyFirstSheet;  // the keyboard's: its fixed keys' sheets, then those of the keys that follow (keyFollows)
        s32 mKeySheets;
        s16 mKeyCell[cKeyCells];  // each key's cell from the keyboard's first sheet
        bool mKeysDrawn;
        bool mKeysFollowing;  // the following keys drawn for mKeyLayout and mKeyPad
        u16 mKeyLayout[NEBULA_KEY_LAYOUT_KEYS];
        u8 mKeyPad[4];
        char mKeyNames[NEBULA_BUTTON_ICONS][NEBULA_KEY_NAME_CHARS];
        s32 mGlyphWidth;  // every icon of ours, as A's cell has it
        s32 mGlyphHeight;
        u16 mOriginal[cMapLast - cMapFirst + 1];  // each code's own glyph
        bool mPainted[cFamilies][cAtlasIcons];
        bool mFamilyDrawn[cFamilies];
    };
    Font sFont;
    enum { FontUnread, FontReady, FontUnusable };
    s32 sFontState = FontUnread;
    NebulaButtonIcons sShown;  // zero: the game's own

    u16 originalGlyph(s32 icon) {
        return sFont.mOriginal[cCodes[icon] - cMapFirst];
    }

    u16 atlasGlyph(s32 family, s32 slot) {
        return static_cast< u16 >((sFont.mFirstSheet + family * sFont.mFamilySheets) * sFont.mCells + slot);
    }

    // A glyph's cell, in its sheet's pixels.
    void cellOf(u16 glyph, u8** ppSheet, s32* pX, s32* pY) {
        const s32 cell = glyph % sFont.mCells;
        *ppSheet = sFont.mSheets + (glyph / sFont.mCells) * sFont.mSheetBytes;
        *pX = (cell % sFont.mRow) * (sFont.mCellWidth + 1) + 1;
        *pY = (cell / sFont.mRow) * (sFont.mCellHeight + 1) + 1;
    }

    // A flag for each sheet of the picture font, set up when our sheets are installed.
    MR::GrowBuffer< bool > sHdWanted;
    MR::GrowBuffer< bool > sHdDone;
    s32 sHdSheetNum = 0;

    // The picture font gains our sheets, glyphs and codes, once it is loaded: larger copies of its tables (its own
    // first), handed to it. A font whose sheets are not RGBA8 records with cells of a usual size (a mod's, or one not
    // cooked) keeps its own icons.
    s32 install() {
        GameSystem* pSystem = SingletonHolder< GameSystem >::get();
        Ui::ResFont* pFont = pSystem != nullptr && pSystem->mFontHolder != nullptr ? pSystem->mFontHolder->mPictureFont : nullptr;
        if (pFont == nullptr || pFont->GetFile() == nullptr) {
            return FontUnread;
        }
        const Ui::FontFile& rFile = *pFont->GetFile();
        const Ui::ResFont::Tables& rOld = pFont->GetTables();
        const NebulaTexture* pSheet = nebula_texture_record(rOld.pSheets);
        bool usable = pSheet != nullptr && pSheet->layout == NEBULA_TEXTURE_RGBA8 && pSheet->levels == 1 &&
                      pSheet->width == rFile.sheetWidth && pSheet->height == rFile.sheetHeight &&
                      rFile.sheetSize >= pSheet->size && rFile.sheetRow > 0 && rFile.sheetLine > 0 && rFile.cellWidth > 0 && rFile.cellHeight > 0 &&
                      static_cast< u32 >(rFile.sheetRow) * rFile.sheetLine <= 0x7FFFu &&
                      (rFile.cellWidth + 1) * rFile.sheetRow <= rFile.sheetWidth &&
                      (rFile.cellHeight + 1) * rFile.sheetLine <= rFile.sheetHeight &&
                      rOld.glyphNum == static_cast< u32 >(rOld.sheetNum) * rFile.sheetRow * rFile.sheetLine;
        for (s32 i = 0; i < NEBULA_BUTTON_ICONS && usable; i++) {
            usable = pFont->HasGlyph(cCodes[i]);
        }
        if (!usable) {
            nebula_log("NebulaIcons: the picture font's sheets are not RGBA8 records of usual cells; its icons stay the game's\n");
            return FontUnusable;
        }
        Font& f = sFont;
        f.mFont = pFont;
        f.mSheetBytes = static_cast< s32 >(rFile.sheetSize);
        f.mSheetWidth = rFile.sheetWidth;
        f.mSheetHeight = rFile.sheetHeight;
        f.mCellWidth = rFile.cellWidth;
        f.mCellHeight = rFile.cellHeight;
        f.mRow = rFile.sheetRow;
        f.mCells = rFile.sheetRow * rFile.sheetLine;
        f.mFamilySheets = (cAtlasIcons + f.mCells - 1) / f.mCells;
        f.mFirstSheet = rOld.sheetNum;
        // The keyboard's cells: the fixed keys' in order, then from a sheet of their own those that follow.
        s32 fixedCells = 0;
        for (s32 slot = 0; slot < cKeyCells; slot++) {
            f.mKeyCell[slot] = 0;
            if (keyHasCell(slot) && !keyFollows(slot)) {
                f.mKeyCell[slot] = static_cast< s16 >(fixedCells++);
            }
        }
        const s32 fixedSheets = (fixedCells + f.mCells - 1) / f.mCells;
        s32 followingCells = 0;
        for (s32 slot = 0; slot < cKeyCells; slot++) {
            if (keyFollows(slot)) {
                f.mKeyCell[slot] = static_cast< s16 >(fixedSheets * f.mCells + followingCells++);
            }
        }
        f.mKeyFirstSheet = f.mFirstSheet + cFamilies * f.mFamilySheets;
        f.mKeySheets = fixedSheets + (followingCells + f.mCells - 1) / f.mCells;
        const s32 sheets = f.mKeyFirstSheet + f.mKeySheets;
        const u32 glyphs = static_cast< u32 >(sheets * f.mCells);
        if (glyphs >= Ui::cNoGlyph) {
            return FontUnusable;
        }
        const Ui::CharWidths widths = pFont->GetCharWidths(cCodes[0]);
        if (widths.glyphWidth == 0) {
            return FontUnusable;
        }
        f.mGlyphWidth = widths.glyphWidth < f.mCellWidth ? widths.glyphWidth : f.mCellWidth;
        f.mGlyphHeight = f.mCellHeight;
        for (u16 code = cMapFirst; code <= cMapLast; code++) {
            f.mOriginal[code - cMapFirst] = pFont->FindGlyphIndex(code);
        }
        // The pages our codes need that the font lacks: the icons' (cMapFirst on) and the keys' (cKeyCodeFirst on).
        const u32 keyLast = NebulaIcons::cKeyCodeFirst + cKeyCells - 1;
        u32 addedPages = 0;
        for (u32 hi = 0; hi < 256; hi++) {
            const bool ours = (hi >= (cMapFirst >> 8) && hi <= (cMapLast >> 8)) || (hi >= (NebulaIcons::cKeyCodeFirst >> 8) && hi <= (keyLast >> 8));
            if (ours && rOld.pPages[hi] == Ui::cNoGlyph) {
                addedPages++;
            }
        }
        // One block that lasts the game (the permanent arena): the sheets, the glyphs, the pages
        // and their blocks.
        const u32 sheetsBytes = static_cast< u32 >(sheets * f.mSheetBytes);
        const u32 glyphBytes = glyphs * static_cast< u32 >(sizeof(Ui::FontGlyph));
        const u32 pageBytes = 256u * sizeof(u16);
        const u32 blockNum = rOld.pageBlockNum + addedPages;
        const u32 bytes = sheetsBytes + glyphBytes + pageBytes + blockNum * pageBytes;
        u8* pBlock = static_cast< u8* >(NebulaArena::alloc(bytes, 16, Arenas::permanent()));
        memcpy(pBlock, rOld.pSheets, static_cast< size_t >(f.mFirstSheet) * f.mSheetBytes);
        memset(pBlock + f.mFirstSheet * f.mSheetBytes, 0, sheetsBytes - f.mFirstSheet * f.mSheetBytes);
        for (s32 sheet = f.mFirstSheet; sheet < sheets; sheet++) {
            initRecord(pBlock + sheet * f.mSheetBytes, f.mSheetWidth, f.mSheetHeight);
        }
        f.mSheets = pBlock;

        Ui::FontGlyph* pGlyphs = reinterpret_cast< Ui::FontGlyph* >(pBlock + sheetsBytes);
        memcpy(pGlyphs, rOld.pGlyphs, rOld.glyphNum * sizeof(Ui::FontGlyph));
        for (u32 index = rOld.glyphNum; index < glyphs; index++) {
            const s32 cell = static_cast< s32 >(index) % f.mCells;
            pGlyphs[index].sheet = static_cast< u16 >(static_cast< s32 >(index) / f.mCells);
            pGlyphs[index].cellX = static_cast< u16 >((cell % f.mRow) * (f.mCellWidth + 1) + 1);
            pGlyphs[index].cellY = static_cast< u16 >((cell / f.mRow) * (f.mCellHeight + 1) + 1);
            pGlyphs[index].widths = widths;
            pGlyphs[index].reserved = 0;
        }
        u16* pPages = reinterpret_cast< u16* >(pBlock + sheetsBytes + glyphBytes);
        u16* pPageBlocks = pPages + 256;
        memcpy(pPages, rOld.pPages, pageBytes);
        memcpy(pPageBlocks, rOld.pPageBlocks, rOld.pageBlockNum * pageBytes);
        u32 nextPage = rOld.pageBlockNum;
        for (u32 hi = 0; hi < 256; hi++) {
            const bool ours = (hi >= (cMapFirst >> 8) && hi <= (cMapLast >> 8)) || (hi >= (NebulaIcons::cKeyCodeFirst >> 8) && hi <= (keyLast >> 8));
            if (ours && pPages[hi] == Ui::cNoGlyph) {
                pPages[hi] = static_cast< u16 >(nextPage);
                for (u32 lo = 0; lo < 256; lo++) {
                    pPageBlocks[nextPage * 256u + lo] = Ui::cNoGlyph;
                }
                nextPage++;
            }
        }
        Ui::ResFont::Tables tables;
        tables.pSheets = pBlock;
        tables.sheetNum = static_cast< u16 >(sheets);
        tables.pGlyphs = pGlyphs;
        tables.glyphNum = glyphs;
        tables.pPages = pPages;
        tables.pPageBlocks = pPageBlocks;
        tables.pageBlockNum = blockNum;
        pFont->SetTables(tables);
        // The keys' codes (the menu's own keys) and the icons' (their own glyphs until a family shows).
        for (s32 slot = 0; slot < cKeyCells; slot++) {
            *pFont->GlyphSlot(static_cast< u16 >(NebulaIcons::cKeyCodeFirst + slot)) =
                static_cast< u16 >(f.mKeyFirstSheet * f.mCells + f.mKeyCell[keyHasCell(slot) ? slot : cKeyNamed]);
        }
        for (s32 i = 0; i < NEBULA_BUTTON_ICONS; i++) {
            f.mSlot[i] = pFont->GlyphSlot(cCodes[i]);
            *f.mSlot[i] = f.mOriginal[cCodes[i] - cMapFirst];
        }
        bool* const pWanted = sHdWanted.room(static_cast< u32 >(sheets));
        bool* const pDone = sHdDone.room(static_cast< u32 >(sheets));
        for (s32 sheet = 0; sheet < sheets; sheet++) {
            pWanted[sheet] = false;
            pDone[sheet] = false;
        }
        sHdSheetNum = sheets;
        return FontReady;
    }

    // A family's icons into its cells, the first time it shows.
    void drawFamily(s32 family) {
        if (sFont.mFamilyDrawn[family]) {
            return;
        }
        Canvas canvas = {canvasPixels(sFont.mGlyphWidth, sFont.mGlyphHeight), sFont.mGlyphWidth, sFont.mGlyphHeight, 1.0f};
        for (s32 k = 0; k < cAtlasIcons; k++) {
            sFont.mPainted[family][k] = paint(canvas, family + 1, cAtlas[k].mGlyph, cAtlas[k].mPlace, LookText, cAtlas[k].mModel);
            if (sFont.mPainted[family][k]) {
                u8* pSheet;
                s32 x, y;
                cellOf(atlasGlyph(family, k), &pSheet, &x, &y);
                paintInto(canvas, pSheet, sFont.mSheetWidth, x, y);
            }
        }
        sFont.mFamilyDrawn[family] = true;
    }

    u16 keyGlyph(s32 slot) {
        return static_cast< u16 >(sFont.mKeyFirstSheet * sFont.mCells + sFont.mKeyCell[slot]);
    }

    // Each of our sheets once it has been drawn from, painted eight times larger, a band of rows per update and with
    // mip levels for a small window, and handed over to stand in for it (NebulaHostServices::texture_hd). A sheet painted
    // again loses its large one until it is drawn from again.
    const s32 cHdBandRows = 32;
    const s32 cHdLevels = 4;
    struct HdPainting {
        u8* mRecord;
        Rgba* mBand;
        s32 mSheet;
        s32 mCell;
        s32 mRow;
    };
    HdPainting sHd = {nullptr, nullptr, -1, 0, 0};
    bool sHdSkippedLogged = false;  // the sheets are too large to repaint at eight times: said once

    void forgetHdSheet(s32 sheet) {
        if (sheet < sHdSheetNum) {
            sHdWanted[sheet] = false;
            if (sHdDone[sheet]) {
                NebulaHost::textureHd(RenderTexture::idOf(sFont.mSheets + sheet * sFont.mSheetBytes), nullptr);
            }
            sHdDone[sheet] = false;
            if (sHd.mSheet == sheet) {
                sHd.mSheet = -1;
            }
        }
    }

    // What the keyboard's cells show now, in `rCanvas`: false when the cell is empty.
    bool paintKeyCell(Canvas& rCanvas, s32 cell);

    // Cell `cell` of `sheet`, as drawFamily and drawKeys draw it: false when it is empty.
    bool paintHdCell(Canvas& rCanvas, s32 sheet, s32 cell) {
        if (sheet >= sFont.mKeyFirstSheet) {
            return paintKeyCell(rCanvas, (sheet - sFont.mKeyFirstSheet) * sFont.mCells + cell);
        }
        const s32 family = (sheet - sFont.mFirstSheet) / sFont.mFamilySheets;
        const s32 k = (sheet - sFont.mFirstSheet - family * sFont.mFamilySheets) * sFont.mCells + cell;
        return k < cAtlasIcons && sFont.mPainted[family][k] &&
               paint(rCanvas, family + 1, cAtlas[k].mGlyph, cAtlas[k].mPlace, LookText, cAtlas[k].mModel);
    }

    // Each level from the one above: four texels' colours by their alpha, as a smaller picture of straight alpha needs.
    void makeHdLevels(u8* pRecord, s32 width, s32 height) {
        u8* pSource = texelsOf(pRecord);
        for (s32 level = 1; level < cHdLevels; level++) {
            const s32 w = width >> level;
            const s32 h = height >> level;
            const s32 sourceWidth = width >> (level - 1);
            u8* pOut = pSource + static_cast< size_t >(sourceWidth) * (height >> (level - 1)) * 4;
            for (s32 y = 0; y < h; y++) {
                for (s32 x = 0; x < w; x++) {
                    u32 sum[3] = {0, 0, 0};
                    u32 alpha = 0;
                    for (s32 i = 0; i < 4; i++) {
                        const u8* pIn = pSource + ((y * 2 + (i >> 1)) * sourceWidth + x * 2 + (i & 1)) * 4;
                        alpha += pIn[3];
                        for (s32 c = 0; c < 3; c++) {
                            sum[c] += pIn[c] * static_cast< u32 >(pIn[3]);
                        }
                    }
                    u8* pTexel = pOut + (y * w + x) * 4;
                    for (s32 c = 0; c < 3; c++) {
                        pTexel[c] = alpha != 0 ? static_cast< u8 >(sum[c] / alpha) : 0;
                    }
                    pTexel[3] = static_cast< u8 >((alpha + 2) / 4);
                }
            }
            pSource = pOut;
        }
    }

    // One band of the sheet being painted large: false when no sheet waits.
    bool paintHdBand() {
        const s32 width = sFont.mSheetWidth * cHdScale;
        const s32 height = sFont.mSheetHeight * cHdScale;
        if (width > cHdSheetMax || height > cHdSheetMax) {
            if (!sHdSkippedLogged) {
                sHdSkippedLogged = true;
                nebula_log("NebulaIcons: the picture font's sheets are %d x %d, too large to repaint at eight times; the icons stay at the game's size\n",
                           static_cast< int >(sFont.mSheetWidth), static_cast< int >(sFont.mSheetHeight));
            }
            return false;
        }
        if (sHd.mSheet < 0) {
            const s32 sheets = sFont.mKeyFirstSheet + sFont.mKeySheets;
            for (s32 sheet = sFont.mFirstSheet; sheet < sheets && sHd.mSheet < 0; sheet++) {
                if (sHdWanted[sheet] && !sHdDone[sheet]) {
                    sHd.mSheet = sheet;
                }
            }
            if (sHd.mSheet < 0) {
                return false;
            }
            if (sHd.mRecord == nullptr) {
                const size_t bytes = NEBULA_TEXTURE_HEADER + nebula_texture_level_bytes(width, height, cHdLevels, NEBULA_TEXTURE_RGBA8);
                sHd.mRecord = static_cast< u8* >(malloc(bytes));
                sHd.mBand = static_cast< Rgba* >(malloc(sizeof(Rgba) * sFont.mGlyphWidth * cHdScale * cHdBandRows));
            }
            nebula_texture_init(sHd.mRecord, static_cast< u16 >(width), static_cast< u16 >(height), cHdLevels, NEBULA_TEXTURE_RGBA8,
                                0);
            memset(sHd.mRecord + NEBULA_TEXTURE_HEADER, 0, nebula_texture_level_bytes(width, height, cHdLevels, NEBULA_TEXTURE_RGBA8));
            sHd.mCell = 0;
            sHd.mRow = 0;
        }
        const s32 cellHeight = sFont.mGlyphHeight * cHdScale;
        Canvas canvas = {sHd.mBand, sFont.mGlyphWidth * cHdScale, cellHeight, static_cast< f32 >(cHdScale), sHd.mRow, cHdBandRows};
        if (cellHeight - sHd.mRow < cHdBandRows) {
            canvas.mBandRows = cellHeight - sHd.mRow;
        }
        if (paintHdCell(canvas, sHd.mSheet, sHd.mCell)) {
            const s32 cellX = (sHd.mCell % sFont.mRow) * (sFont.mCellWidth + 1) + 1;
            const s32 cellY = (sHd.mCell / sFont.mRow) * (sFont.mCellHeight + 1) + 1;
            Canvas band = {sHd.mBand, canvas.mWidth, canvas.mBandRows, canvas.mScale};
            paintInto(band, sHd.mRecord, width, cellX * cHdScale, cellY * cHdScale + sHd.mRow);
            sHd.mRow += cHdBandRows;
        } else {
            sHd.mRow = cellHeight;
        }
        if (sHd.mRow < cellHeight) {
            return true;
        }
        sHd.mRow = 0;
        if (++sHd.mCell < sFont.mCells) {
            return true;
        }
        makeHdLevels(sHd.mRecord, width, height);
        NebulaHost::textureHd(RenderTexture::idOf(sFont.mSheets + sHd.mSheet * sFont.mSheetBytes), sHd.mRecord);
        sHdDone[sHd.mSheet] = true;
        sHd.mSheet = -1;
        return true;
    }

    // The keyboard's cells: its fixed keys the first time they show; the keys that follow the layout, the Control Pad
    // and the named keys whenever these change (a US layout, the arrows and no named key until the host says otherwise).
    void drawKeys(const u16* pLayout, const u8* pPadKeys, const char (*pNames)[NEBULA_KEY_NAME_CHARS]) {
        Canvas canvas = {canvasPixels(sFont.mGlyphWidth, sFont.mGlyphHeight), sFont.mGlyphWidth, sFont.mGlyphHeight, 1.0f};
        if (!sFont.mKeysDrawn) {
            for (s32 slot = 0; slot < cKeyCells; slot++) {
                if (keyFollows(slot) || !keyHasCell(slot)) {
                    continue;
                }
                paintKey(canvas, slot, LookText, nullptr, nullptr);
                u8* pSheet;
                s32 x, y;
                cellOf(keyGlyph(slot), &pSheet, &x, &y);
                paintInto(canvas, pSheet, sFont.mSheetWidth, x, y);
            }
            sFont.mKeysDrawn = true;
        }
        static const u8 cArrows[4] = {NEBULA_KEY_UP, NEBULA_KEY_DOWN, NEBULA_KEY_LEFT, NEBULA_KEY_RIGHT};
        static const u16 cNoLayout[NEBULA_KEY_LAYOUT_KEYS] = {0};
        static const char cNoNames[NEBULA_BUTTON_ICONS][NEBULA_KEY_NAME_CHARS] = {{0}};
        const u16* pShownLayout = pLayout != nullptr ? pLayout : cNoLayout;
        const u8* pShownPad = pPadKeys != nullptr ? pPadKeys : cArrows;
        const char (*pShownNames)[NEBULA_KEY_NAME_CHARS] = pNames != nullptr ? pNames : cNoNames;
        if (sFont.mKeysFollowing && memcmp(sFont.mKeyLayout, pShownLayout, sizeof(sFont.mKeyLayout)) == 0 &&
            memcmp(sFont.mKeyPad, pShownPad, sizeof(sFont.mKeyPad)) == 0 &&
            memcmp(sFont.mKeyNames, pShownNames, sizeof(sFont.mKeyNames)) == 0) {
            return;
        }
        for (s32 slot = 0; slot < cKeyCells; slot++) {
            if (!keyFollows(slot) || !keyHasCell(slot)) {
                continue;
            }
            paintKey(canvas, slot, LookText, pShownLayout, pShownPad, slot >= cKeyNamed ? pShownNames[slot - cKeyNamed] : nullptr);
            u8* pSheet;
            s32 x, y;
            cellOf(keyGlyph(slot), &pSheet, &x, &y);
            paintInto(canvas, pSheet, sFont.mSheetWidth, x, y);
        }
        // The sheets holding keys that follow are painted again.
        for (s32 slot = 0; slot < cKeyCells; slot++) {
            if (keyFollows(slot) && keyHasCell(slot)) {
                forgetHdSheet(sFont.mKeyFirstSheet + sFont.mKeyCell[slot] / sFont.mCells);
            }
        }
        memcpy(sFont.mKeyLayout, pShownLayout, sizeof(sFont.mKeyLayout));
        memcpy(sFont.mKeyPad, pShownPad, sizeof(sFont.mKeyPad));
        memcpy(sFont.mKeyNames, pShownNames, sizeof(sFont.mKeyNames));
        sFont.mKeysFollowing = true;
    }

    bool paintKeyCell(Canvas& rCanvas, s32 cell) {
        for (s32 slot = 0; slot < cKeyCells; slot++) {
            if (keyHasCell(slot) && sFont.mKeyCell[slot] == cell) {
                paintKey(rCanvas, slot, LookText, keyFollows(slot) ? sFont.mKeyLayout : nullptr, keyFollows(slot) ? sFont.mKeyPad : nullptr,
                         slot >= cKeyNamed ? sFont.mKeyNames[slot - cKeyNamed] : nullptr);
                return true;
            }
        }
        return false;
    }

    // The glyph icon `icon` shows on the keyboard: its key's (a key without its own icon: the icon's named cell), the
    // Control Pad's four keys, or its own.
    u16 keyboardGlyph(s32 icon, s32 glyph, s32 place) {
        if (glyph == NEBULA_ICON_KEY && place < NEBULA_KEYS) {
            return keyGlyph(keyHasCell(place) ? place : cKeyNamed + icon);
        }
        if (glyph == NEBULA_ICON_DPAD) {
            return keyGlyph(cKeyPad);
        }
        return originalGlyph(icon);
    }

    // The glyph icon `icon` shows: ours, the game's icon in its place (Plus, Minus, HOME or the Control Pad, which a Nintendo
    // controller's look like), or its own.
    u16 shownGlyph(s32 family, s32 icon, s32 glyph, s32 place, s32 model = NEBULA_ICON_MODEL_STANDARD) {
        if (family < 0 || glyph == NEBULA_ICON_KEEP) {
            return originalGlyph(icon);
        }
        const s32 slot = atlasSlot(glyph, place, model);
        if (slot >= 0 && sFont.mPainted[family][slot]) {
            return atlasGlyph(family, slot);
        }
        return originalGlyph(glyph == NEBULA_ICON_START ? IconPlus : glyph == NEBULA_ICON_BACK ? IconMinus : glyph == NEBULA_ICON_HOME ? IconHome
                                                                         : glyph == NEBULA_ICON_DPAD ? IconPad : icon);
    }

    // The A button over people and signs: IconAButton's two pictures (the button up, 48 pixels square, and pressed, 40;
    // small) give way to ours (records), drawn as the font's A is, while it draws.
    struct Picture {
        u8 mRecord[NEBULA_TEXTURE_HEADER + cPictureMax * cPictureMax * 4];
    };
    const s32 cPictureSizes[2] = {48, 40};
    const s32 cPictureLooks[2] = {LookUp, LookPressed};
    Picture sPictures[2];
    bool sPicturesShown = false;

    // The font's glyph `glyph` scaled into the canvas (an icon the style borrows from the game's).
    void paintFromGlyph(Canvas& rCanvas, u16 glyph) {
        rCanvas.clear();
        u8* pSheet;
        s32 cellX, cellY;
        cellOf(glyph, &pSheet, &cellX, &cellY);
        const s32 width = sFont.mGlyphWidth;
        const s32 height = sFont.mGlyphHeight;
        const f32 scale = static_cast< f32 >(width > height ? width : height) / rCanvas.mWidth;
        for (s32 y = 0; y < rCanvas.mHeight; y++) {
            for (s32 x = 0; x < rCanvas.mWidth; x++) {
                const s32 sx = static_cast< s32 >((x + 0.5f) * scale - (rCanvas.mWidth * scale - width) * 0.5f);
                const s32 sy = static_cast< s32 >((y + 0.5f) * scale - (rCanvas.mHeight * scale - height) * 0.5f);
                if (sx >= 0 && sy >= 0 && sx < width && sy < height) {
                    rCanvas.mPixels[y * rCanvas.mWidth + x] = texelOf(pSheet, sFont.mSheetWidth, cellX + sx, cellY + sy);
                }
            }
        }
    }

    // The icon at A's code the pictures show (a keyboard's key: family cFamilies).
    struct PictureSource {
        s32 mFamily;
        s32 mGlyph;
        s32 mPlace;
        s32 mModel;
    };
    PictureSource sPictureSource = {-1, 0, 0, NEBULA_ICON_MODEL_STANDARD};

    // Picture `k` into `rCanvas` (whole, or the band it names): false when the style has no picture of its own, the
    // game's glyph scaled standing in.
    bool paintPicture(Canvas& rCanvas, s32 k) {
        const PictureSource& rSource = sPictureSource;
        if (rSource.mFamily == cFamilies) {
            const bool named = !keyHasCell(rSource.mPlace);
            paintKey(rCanvas, named ? cKeyNamed : rSource.mPlace, cPictureLooks[k], sFont.mKeyLayout, sFont.mKeyPad,
                     named ? sFont.mKeyNames[0] : nullptr);
            return true;
        }
        return paint(rCanvas, rSource.mFamily + 1, rSource.mGlyph, rSource.mPlace, cPictureLooks[k], rSource.mModel);
    }

    // Each picture painted eight times larger with mip levels, a band of rows per update once no sheet waits, and handed
    // over to stand in for it as a sheet's is. A picture painted again loses its large one until that is painted again.
    struct HdPicture {
        u8* mRecord;
        Rgba* mBand;
        s32 mRow;  // the next row; -1: nothing to paint
        bool mDone;
    };
    HdPicture sHdPictures[2] = {{nullptr, nullptr, -1, false}, {nullptr, nullptr, -1, false}};

    void drawPicture(s32 k) {
        Canvas canvas = {canvasPixels(cPictureSizes[k], cPictureSizes[k]), cPictureSizes[k], cPictureSizes[k], 1.0f};
        if (!paintPicture(canvas, k)) {
            const PictureSource& rSource = sPictureSource;
            paintFromGlyph(canvas, shownGlyph(rSource.mFamily, 0, rSource.mGlyph, rSource.mPlace, rSource.mModel));
        }
        initRecord(sPictures[k].mRecord, cPictureSizes[k], cPictureSizes[k]);
        paintInto(canvas, sPictures[k].mRecord, cPictureSizes[k], 0, 0);
        HdPicture& rHd = sHdPictures[k];
        if (rHd.mDone) {
            NebulaHost::textureHd(RenderTexture::idOf(sPictures[k].mRecord), nullptr);
            rHd.mDone = false;
        }
        rHd.mRow = 0;
    }

    // One band of a picture waiting to be painted large.
    void paintHdPictureBand() {
        for (s32 k = 0; k < 2; k++) {
            HdPicture& rHd = sHdPictures[k];
            if (rHd.mRow < 0) {
                continue;
            }
            const s32 size = cPictureSizes[k] * cHdScale;
            if (rHd.mRecord == nullptr) {
                rHd.mRecord = static_cast< u8* >(
                    malloc(NEBULA_TEXTURE_HEADER + nebula_texture_level_bytes(size, size, cHdLevels, NEBULA_TEXTURE_RGBA8)));
                rHd.mBand = static_cast< Rgba* >(malloc(sizeof(Rgba) * size * cHdBandRows));
            }
            if (rHd.mRow == 0) {
                nebula_texture_init(rHd.mRecord, static_cast< u16 >(size), static_cast< u16 >(size), cHdLevels, NEBULA_TEXTURE_RGBA8, 0);
                memset(rHd.mRecord + NEBULA_TEXTURE_HEADER, 0, nebula_texture_level_bytes(size, size, cHdLevels, NEBULA_TEXTURE_RGBA8));
            }
            Canvas canvas = {rHd.mBand, size, size, static_cast< f32 >(cHdScale), rHd.mRow,
                             size - rHd.mRow < cHdBandRows ? size - rHd.mRow : cHdBandRows};
            if (!paintPicture(canvas, k)) {
                rHd.mRow = -1;
                return;
            }
            Canvas band = {rHd.mBand, size, canvas.mBandRows, canvas.mScale};
            paintInto(band, rHd.mRecord, size, 0, rHd.mRow);
            rHd.mRow += canvas.mBandRows;
            if (rHd.mRow >= size) {
                makeHdLevels(rHd.mRecord, size, size);
                NebulaHost::textureHd(RenderTexture::idOf(sPictures[k].mRecord), rHd.mRecord);
                rHd.mDone = true;
                rHd.mRow = -1;
            }
            return;
        }
    }

    // The Pair Joy-Con picture: each half a texture of Nebula's own (a record of RGBA8 rows): a Joy-Con's front drawn simply, in the
    // reference drawing's style (thin dark outlines, its colour, dark grey parts: its rail, its shoulder over the top
    // curve, its stick, four plain buttons, Minus or Plus, Capture or HOME), a little raised off the page (a soft shadow
    // under it and its buttons, a highlight down its round side). A half's body is 12.07 x 37.1 units in the middle of
    // its picture's cJoyConBox, its parts where the drawing has them. Its looks: not connected (grey), in its colour, and
    // its shoulder held (clicked down). Painted at the screen's pixels per unit on each axis (as its 2D layer draws it),
    // so drawn without scaling and without mipmaps, a band of rows at a time.
    const f32 cJcBodyWidth = NebulaIcons::cJoyConBody[0];
    const f32 cJcBodyHeight = NebulaIcons::cJoyConBody[1];
    const f32 cJcOutline = 0.12f;
    const s32 cJcMaxSide = 65532;  // the largest texture (TexMap's sizes are 16-bit, whole tiles): past it, a picture is a little magnified
    const s32 cJcBand = 16;  // rows painted at once (whole 4 x 4 tiles)
    const s32 cJcPictures = 2 * NebulaIcons::JoyConLooks;
    const s32 cJcOrder[NebulaIcons::JoyConLooks] = {NebulaIcons::JoyConShown, NebulaIcons::JoyConAway, NebulaIcons::JoyConHeld};
    const s64 cJcRetireMs = 250;  // a set no longer shown keeps its memory this long (frames still drawing it)
    const s32 cJcSteady = 3;         // a density asked for this many frames running before a set is painted for it

    // One density's pictures, every half in every look (painted in cJcOrder, both halves of a look in turn), in the
    // game's image.
    struct JoyConSet {
        u8* mBlock;  // the pictures' records, in the root arena
        u8* mTexels[2][NebulaIcons::JoyConLooks];
        Ui::TexMap mMaps[2][NebulaIcons::JoyConLooks];
        s32 mWidth;  // each picture's texels
        s32 mHeight;
        f32 mPixelsX;  // its pixels per unit
        f32 mPixelsY;
        s32 mNext;  // the picture being painted (cJcPictures: all are)
        s32 mRow;   // its rows painted
        s64 mSpent;  // nanoseconds
        s32 mCalls;
        s64 mRetired;  // when the screen stopped showing it (NebulaClock; 0: it never showed)
    };
    JoyConSet sJoyConSets[2];
    s32 sJoyConShown = -1;  // the set whose pictures the screen shows, or -1
    s32 sJoyConWidth = 0;   // the density asked for, as each picture's texels (0: none yet)
    s32 sJoyConHeight = 0;
    f32 sJoyConPixelsX = 0.0f;
    f32 sJoyConPixelsY = 0.0f;
    s32 sJoyConAsked = 0;  // the frames running it has been asked for
    MR::GrowBuffer< Rgba > sJoyConBand;

    // Where a band of a half's picture is painted: pixels per unit on each axis, the pixels per unit an edge is smoothed
    // over, the body's place in the picture (units, centred) and the band's first row in it.
    struct JcFrame {
        f32 mPixelsX;
        f32 mPixelsY;
        f32 mEdge;
        f32 mLeft;
        f32 mTop;
        s32 mRow;
    };

    Rgba mix(const Rgba& rA, const Rgba& rB, f32 t) {
        const Rgba c = {rA.r + (rB.r - rA.r) * t, rA.g + (rB.g - rA.g) * t, rA.b + (rB.b - rA.b) * t, rA.a + (rB.a - rA.a) * t};
        return c;
    }

    Rgba shaded(const Rgba& rColour, f32 light) {
        const Rgba white = {1.0f, 1.0f, 1.0f, rColour.a};
        const Rgba black = {0.0f, 0.0f, 0.0f, rColour.a};
        return light >= 0.0f ? mix(rColour, white, light) : mix(rColour, black, -light);
    }

    // Every pixel of the band whose centre lies in units (u0, v0)-(u1, v1) of the half: coloured by `colour(u, v, d)`
    // where `distance(u, v)` (units, negative inside) puts it in the shape, anti-aliased over a pixel.
    template < typename Distance, typename Colour >
    void fillUnits(Canvas& rCanvas, const JcFrame& rFrame, f32 u0, f32 v0, f32 u1, f32 v1, Distance distance, Colour colour) {
        const f32 rowFirst = static_cast< f32 >(rFrame.mRow);
        const f32 rowEnd = static_cast< f32 >(rFrame.mRow + rCanvas.mHeight);
        const s32 x0 = static_cast< s32 >(NebulaUi::clamp((rFrame.mLeft + u0) * rFrame.mPixelsX - 1.0f, 0.0f, static_cast< f32 >(rCanvas.mWidth)));
        const s32 x1 = static_cast< s32 >(NebulaUi::clamp((rFrame.mLeft + u1) * rFrame.mPixelsX + 2.0f, 0.0f, static_cast< f32 >(rCanvas.mWidth)));
        const s32 y0 = static_cast< s32 >(NebulaUi::clamp((rFrame.mTop + v0) * rFrame.mPixelsY - 1.0f, rowFirst, rowEnd));
        const s32 y1 = static_cast< s32 >(NebulaUi::clamp((rFrame.mTop + v1) * rFrame.mPixelsY + 2.0f, rowFirst, rowEnd));
        for (s32 py = y0; py < y1; py++) {
            const f32 v = (py + 0.5f) / rFrame.mPixelsY - rFrame.mTop;
            for (s32 px = x0; px < x1; px++) {
                const f32 u = (px + 0.5f) / rFrame.mPixelsX - rFrame.mLeft;
                const f32 d = distance(u, v);
                const f32 cover = coverOf(d * rFrame.mEdge);
                if (cover > 0.0f) {
                    rCanvas.blend(px, py - rFrame.mRow, colour(u, v, d), cover);
                }
            }
        }
    }

    f32 circleDistance(f32 u, f32 v, f32 cu, f32 cv, f32 radius) {
        return __builtin_sqrtf((u - cu) * (u - cu) + (v - cv) * (v - cv)) - radius;
    }

    // A box centred on (cx, cy), `hx` x `hy` from its middle, its corners round `radius` (as Shape, for any corners).
    f32 boxDistance(f32 u, f32 v, f32 cx, f32 cy, f32 hx, f32 hy, f32 radius) {
        const Shape box = {cx, cy, hx, hy, radius};
        return box.distance(u, v);
    }

    // A half's shapes in `o`, units from its outer side (the left half's left, the right half's right) toward its rail.
    // Its body: a box whose outer corners are round (the top 6.3, the foot 8, more than half its width, as drawn) and
    // inner ones nearly square. Each corner's arc is centred `radius` in from both its sides, so it meets them where it
    // turns into them; within the square between that centre and the corner, the distance is the arc's.
    f32 bodyDistance(f32 o, f32 v) {
        const f32 w = cJcBodyWidth;
        const f32 h = cJcBodyHeight;
        const f32 dx = -o > o - w ? -o : o - w;
        const f32 dy = -v > v - h ? -v : v - h;
        f32 d = dx > 0.0f && dy > 0.0f ? __builtin_sqrtf(dx * dx + dy * dy) : (dx > dy ? dx : dy);
        const f32 inner = 0.25f;
        const f32 radii[4] = {6.3f, 8.0f, inner, inner};  // outer top, outer foot, inner top, inner foot
        for (s32 i = 0; i < 4; i++) {
            const f32 r = radii[i];
            const f32 cu = i < 2 ? r : w - r;
            const f32 cv = (i & 1) == 0 ? r : h - r;
            const bool zone = (i < 2 ? o < cu : o > cu) && ((i & 1) == 0 ? v < cv : v > cv);
            if (zone) {
                d = __builtin_sqrtf((o - cu) * (o - cu) + (v - cv) * (v - cv)) - r;
            }
        }
        return d;
    }

    // Its rail along the inner side, with its foot.
    f32 railDistance(f32 o, f32 v) {
        const f32 half = NebulaIcons::cJoyConRail * 0.5f;
        const f32 rail = boxDistance(o, v, cJcBodyWidth + half, 17.57f, half, 15.63f, 0.35f);
        const f32 foot = boxDistance(o, v, cJcBodyWidth + 0.88f, 32.9f, 0.5f, 0.7f, 0.25f);
        return rail < foot ? rail : foot;
    }

    // Its shoulder: a band over the top curve, outside the body, its ends cut square.
    f32 shoulderDistance(f32 o, f32 v) {
        const f32 d = bodyDistance(o, v);
        const f32 out = d - NebulaIcons::cJoyConShoulder;
        const f32 band = -d > out ? -d : out;
        const f32 end = o - (cJcBodyWidth - 2.07f);
        const f32 ends = v - 4.7f > end ? v - 4.7f : end;
        return band > ends ? band : ends;
    }

    void paintJoyCon(Canvas& rCanvas, const JcFrame& rFrame, s32 half, s32 look) {
        const bool left = half == 0;
        const bool shown = look != NebulaIcons::JoyConAway;
        const bool held = look == NebulaIcons::JoyConHeld;
        const Rgba body = rgba(shown ? NebulaIcons::cJoyConColours[half] : NEBULA_RGBA(0xD3, 0xD8, 0xE1, 0xFF));
        const Rgba outline = rgba(shown ? 0x1E2024FF : NEBULA_RGBA(0x8D, 0x93, 0x9E, 0xFF));
        const Rgba part = rgba(shown ? 0x43464BFF : 0xAAB0BAFF);  // the rail, the shoulder, the buttons
        const Rgba white = rgba(0xFFFFFFFF);
        // The half's own coordinates: `o` from its outer side.
        const auto outer = [left](f32 u) { return left ? u : cJcBodyWidth - u; };
        const f32 uMin = left ? -0.5f : -2.0f;
        const f32 uMax = left ? cJcBodyWidth + 2.0f : cJcBodyWidth + 0.5f;
        rCanvas.clear();
        // Held, the shoulder sits a little lower and thinner (clicked down).
        const f32 press = held ? 0.22f : 0.0f;
        const auto shoulderShape = [&](f32 u, f32 v) {
            return shoulderDistance(outer(u), v - press) + press * 0.5f;
        };
        const auto whole = [&](f32 u, f32 v) {
            const f32 o = outer(u);
            const f32 a = bodyDistance(o, v);
            const f32 b = railDistance(o, v);
            const f32 c = shoulderDistance(o, v);
            const f32 ab = a < b ? a : b;
            return ab < c ? ab : c;
        };
        // A soft shadow under the whole half, down and to the right.
        fillUnits(rCanvas, rFrame, uMin, -1.0f, uMax + 1.0f, cJcBodyHeight + 1.5f,
                  [&](f32 u, f32 v) { return whole(u - 0.35f, v - 0.45f) - 0.6f; },
                  [&](f32, f32, f32 d) {
                      Rgba c = rgba(0x1E2A44FF);
                      c.a = 0.2f * NebulaUi::clamp(-d / 1.2f, 0.0f, 1.0f);
                      return c;
                  });
        // A shape in a colour with its outline.
        const auto outlined = [&](f32 u0, f32 v0, f32 u1, f32 v1, auto distance, auto colour) {
            fillUnits(rCanvas, rFrame, u0, v0, u1, v1, distance,
                      [&](f32 u, f32 v, f32 d) { return d > -cJcOutline ? outline : colour(u, v, d); });
        };
        // The rail, the shoulder (dark grey; darker pressed), then the body: a highlight down its round side, a little
        // darker toward the rail.
        outlined(uMin, 1.5f, uMax, 34.0f, [&](f32 u, f32 v) { return railDistance(outer(u), v); },
                 [&](f32, f32, f32) { return part; });
        outlined(uMin, -1.0f, uMax, 6.5f, shoulderShape,
                 [&](f32, f32, f32) { return held ? shaded(part, -0.25f) : part; });
        outlined(0.0f, 0.0f, cJcBodyWidth, cJcBodyHeight, [&](f32 u, f32 v) { return bodyDistance(outer(u), v); },
                 [&](f32 u, f32 v, f32 d) {
                     const f32 o = outer(u);
                     const f32 shine = NebulaUi::clamp(1.0f - __builtin_fabsf(o - 1.7f) / 1.5f, 0.0f, 1.0f) *
                                       NebulaUi::clamp(-(d + cJcOutline) / 0.5f, 0.0f, 1.0f);
                     const f32 shade = NebulaUi::clamp((o - (cJcBodyWidth - 2.57f)) / 2.5f, 0.0f, 1.0f) * 0.07f;
                     return shaded(mix(body, white, shine * 0.16f), -shade - 0.03f * v / cJcBodyHeight);
                 });
        // A raised part: its shadow under it, its face lit from above.
        const auto raised = [&](f32 cu, f32 cv, f32 reach, auto distance, const Rgba& rFace) {
            fillUnits(rCanvas, rFrame, cu - reach, cv - reach, cu + reach + 0.3f, cv + reach + 0.3f,
                      [&](f32 u, f32 v) { return distance(u - 0.1f, v - 0.16f) - 0.05f; },
                      [&](f32, f32, f32) {
                          Rgba c = rgba(0x000000FF);
                          c.a = 0.22f;
                          return c;
                      });
            outlined(cu - reach, cv - reach, cu + reach, cv + reach, distance,
                     [&, cv, reach](f32, f32 v, f32) { return shaded(rFace, 0.1f * (cv - v) / reach); });
        };
        // The stick: its ring and its cap (the stick and the buttons a little toward the outer side, as drawn).
        const f32 middle = cJcBodyWidth * 0.5f + (left ? 0.335f : -0.385f);
        const f32 su = middle;
        const f32 sv = left ? 9.58f : 19.66f;
        raised(su, sv, 2.9f, [=](f32 u, f32 v) { return circleDistance(u, v, su, sv, 2.88f); }, part);
        outlined(su - 2.3f, sv - 2.3f, su + 2.3f, sv + 2.3f,
                 [=](f32 u, f32 v) { return circleDistance(u, v, su, sv, 2.27f); },
                 [&](f32 u, f32 v, f32) {
                     const f32 t = NebulaUi::clamp((u - su + v - sv) / 4.5f + 0.5f, 0.0f, 1.0f);
                     return shaded(part, 0.08f - 0.14f * t);
                 });
        // Four plain buttons: the direction buttons on the left, the face buttons on the right.
        const f32 gu = middle;
        const f32 gv = left ? 20.0f : 9.41f;
        const f32 du[4] = {0.0f, 0.0f, -1.0f, 1.0f};
        const f32 dv[4] = {-1.0f, 1.0f, 0.0f, 0.0f};
        for (s32 i = 0; i < 4; i++) {
            const f32 cu = gu + du[i] * 2.77f;
            const f32 cv = gv + dv[i] * 2.77f;
            raised(cu, cv, 1.3f, [=](f32 u, f32 v) { return circleDistance(u, v, cu, cv, 1.3f); }, part);
        }
        if (left) {
            // Minus, and Capture (a rounded square), placed from the rail.
            const f32 w = cJcBodyWidth;
            raised(w - 1.94f, 3.65f, 1.4f, [w](f32 u, f32 v) { return segmentDistance(u, v, w - 2.91f, 3.65f, w - 0.97f, 3.65f) - 0.33f; },
                   part);
            raised(w - 3.65f, 27.1f, 1.2f, [w](f32 u, f32 v) { return boxDistance(u, v, w - 3.65f, 27.1f, 1.05f, 1.05f, 0.33f); },
                   part);
        } else {
            // Plus, and HOME (a round button).
            raised(1.94f, 3.65f, 1.4f,
                   [](f32 u, f32 v) {
                       const f32 a = segmentDistance(u, v, 0.97f, 3.65f, 2.91f, 3.65f);
                       const f32 b = segmentDistance(u, v, 1.94f, 2.68f, 1.94f, 4.62f);
                       return (a < b ? a : b) - 0.33f;
                   },
                   part);
            raised(3.6f, 26.6f, 1.25f, [](f32 u, f32 v) { return circleDistance(u, v, 3.6f, 26.6f, 1.2f); }, part);
        }
    }

    // Set `index` made ready for the density asked for, its pictures unpainted records (each a multiple of 8 bytes, the texture record's widest member).
    void startJoyConSet(s32 index) {
        JoyConSet& rSet = sJoyConSets[index];
        // The pictures of the density before are given up once the new block is taken.
        u8* const pOld = rSet.mBlock;
        memset(&rSet, 0, sizeof(rSet));
        const u32 bytes = (NEBULA_TEXTURE_HEADER + static_cast< u32 >(sJoyConWidth * sJoyConHeight * 4) + 7u) & ~7u;
        rSet.mBlock = static_cast< u8* >(NebulaArena::alloc(bytes * cJcPictures, 16, NebulaArena::root()));
        if (pOld != nullptr) {
            NebulaArena::free(pOld, NebulaArena::root());
        }
        for (s32 i = 0; i < cJcPictures; i++) {
            u8* pRecord = rSet.mBlock + bytes * i;
            initRecord(pRecord, sJoyConWidth, sJoyConHeight);
            rSet.mTexels[i % 2][cJcOrder[i / 2]] = pRecord;
        }
        rSet.mWidth = sJoyConWidth;
        rSet.mHeight = sJoyConHeight;
        rSet.mPixelsX = sJoyConPixelsX;
        rSet.mPixelsY = sJoyConPixelsY;
    }

    // The next band of rows of the set's picture being painted, painted and encoded; the picture ready once its last.
    void paintJoyConBand(JoyConSet& rSet) {
        const s32 half = rSet.mNext % 2;
        const s32 look = cJcOrder[rSet.mNext / 2];
        const s32 rows = rSet.mHeight - rSet.mRow < cJcBand ? rSet.mHeight - rSet.mRow : cJcBand;
        Canvas canvas = {sJoyConBand.room(static_cast< u32 >(rSet.mWidth * rows)), rSet.mWidth, rows, 1.0f};
        const JcFrame frame = {rSet.mPixelsX,
                               rSet.mPixelsY,
                               rSet.mPixelsX < rSet.mPixelsY ? rSet.mPixelsX : rSet.mPixelsY,
                               (rSet.mWidth / rSet.mPixelsX - cJcBodyWidth) * 0.5f,
                               (rSet.mHeight / rSet.mPixelsY - cJcBodyHeight) * 0.5f,
                               rSet.mRow};
        paintJoyCon(canvas, frame, half, look);
        u8* pRecord = rSet.mTexels[half][look];
        paintInto(canvas, pRecord, rSet.mWidth, 0, rSet.mRow);
        rSet.mRow += rows;
        if (rSet.mRow < rSet.mHeight) {
            return;
        }
        Ui::TexMap& rMap = rSet.mMaps[half][look];
        rMap.SetImage(pRecord);
        rMap.SetSize(static_cast< u16 >(rSet.mWidth), static_cast< u16 >(rSet.mHeight));
        rMap.SetTexelFormat(Ui::TexMap::TEXEL_RGBA8);
        rMap.SetWrapMode(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);
        rMap.SetMipMap(false);
        rMap.SetFilter(NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);
        rMap.SetLOD(0.0f, 0.0f);
        rSet.mNext++;
        rSet.mRow = 0;
    }

    // Each of the IconAButton layout's pictures of A, ours in its place while it draws.
    struct Swaps {
        MR::InlineVector< Ui::TexMap*, 8 > mMaps;
        MR::InlineVector< Ui::TexMap, 8 > mSaved;
    };

    void swapPane(Ui::Pane* pPane, Swaps& rSwaps, s32 depth) {
        Ui::Material* pMaterial = pPane->GetMaterial();
        for (u32 i = 0; pMaterial != nullptr && i < pMaterial->GetTextureNum(); i++) {
            Ui::TexMap& rMap = pMaterial->GetTexMapAry()[i];
            for (s32 k = 0; k < 2; k++) {
                if (rMap.mWidth == cPictureSizes[k] && rMap.mHeight == cPictureSizes[k] && rMap.GetTexelFormat() == Ui::TexMap::TEXEL_IA4) {
                    rSwaps.mMaps.push_back(&rMap);
                    rSwaps.mSaved.push_back(rMap);
                    rMap.SetImage(sPictures[k].mRecord);
                    rMap.SetTexelFormat(Ui::TexMap::TEXEL_RGBA8);
                    rMap.SetMipMap(false);
                    rMap.SetLOD(0.0f, 0.0f);
                    break;
                }
            }
        }
        if (depth > 16) {
            return;
        }
        for (Ui::Pane* pChild : pPane->GetChildList()) {
            swapPane(pChild, rSwaps, depth + 1);
        }
    }
};  // namespace

namespace NebulaIcons {
    void init() {
        if (sFontState != FontUnread) {
            return;
        }
        sFontState = install();
        if (sFontState == FontUnread) {
            nebula_log("NebulaIcons: the picture font is not loaded; its icons stay the game's\n");
            sFontState = FontUnusable;
        }
    }

    void update() {
        NebulaButtonIcons now;
        memset(&now, 0, sizeof(now));
        NebulaHost::buttonIcons(&now);
        // The Wii Remote's pictures follow the remote even when the icons cannot be drawn.
        sShown.wii_remote = now.wii_remote;
        if (sFontState == FontReady && !paintHdBand()) {
            paintHdPictureBand();
        }
        if (sFontState != FontReady || memcmp(&now, &sShown, sizeof(now)) == 0) {
            return;
        }
        const bool keyboard = now.style == NEBULA_ICON_STYLE_KEYBOARD;
        const s32 family = keyboard ? cFamilies
                                    : now.style >= NEBULA_ICON_STYLE_XBOX && now.style <= NEBULA_ICON_STYLE_PLAYSTATION ? now.style - 1 : -1;
        if (keyboard) {
            drawKeys(now.layout, now.pad_keys, now.other);
        } else if (family >= 0) {
            drawFamily(family);
        }
        for (s32 i = 0; i < NEBULA_BUTTON_ICONS; i++) {
            *sFont.mSlot[i] =
                keyboard ? keyboardGlyph(i, now.glyphs[i], now.places[i]) : shownGlyph(family, i, now.glyphs[i], now.places[i], now.model);
        }
        // The A bubble's two frames, as the font's A.
        sPicturesShown = family >= 0 && (keyboard ? now.glyphs[0] == NEBULA_ICON_KEY : now.glyphs[0] != NEBULA_ICON_KEEP);
        sPictureSource = {family, now.glyphs[0], now.places[0], now.model};
        for (s32 k = 0; k < 2 && sPicturesShown; k++) {
            drawPicture(k);
        }
        sShown = now;
    }

    void noteDrawn(const void* pSheet) {
        if (sFontState != FontReady || sFont.mSheetBytes == 0 || pSheet < sFont.mSheets) {
            return;
        }
        const s32 sheet = static_cast< s32 >((static_cast< const u8* >(pSheet) - sFont.mSheets) / sFont.mSheetBytes);
        if (sheet >= sFont.mFirstSheet && sheet < sFont.mKeyFirstSheet + sFont.mKeySheets && sheet < sHdSheetNum) {
            sHdWanted[sheet] = true;
        }
    }

    bool wiiRemoteInUse() {
        return sShown.wii_remote != 0;
    }

    u16 keyCode(s32 key) {
        if (sFontState != FontReady || key < 0 || key >= NEBULA_KEYS) {
            return 0;
        }
        const bool keyboard = sShown.style == NEBULA_ICON_STYLE_KEYBOARD;
        drawKeys(keyboard ? sShown.layout : nullptr, keyboard ? sShown.pad_keys : nullptr, keyboard ? sShown.other : nullptr);
        return static_cast< u16 >(cKeyCodeFirst + key);
    }

    void setJoyConDensity(f32 pixelsX, f32 pixelsY) {
        const f32 width = cJoyConBox[0] * pixelsX;
        const f32 height = cJoyConBox[1] * pixelsY;
        if (!(width >= 4.0f && height >= 4.0f)) {
            return;
        }
        f32 fit = cJcMaxSide / width < cJcMaxSide / height ? cJcMaxSide / width : cJcMaxSide / height;
        fit = fit < 1.0f ? fit : 1.0f;
        // Whole 4 x 4 tiles: a picture's few extra texels widen its box a little (joyConPictureSize), at the density.
        const s32 w = (static_cast< s32 >(__builtin_ceilf(width * fit)) + 3) & ~3;
        const s32 h = (static_cast< s32 >(__builtin_ceilf(height * fit)) + 3) & ~3;
        sJoyConAsked = w == sJoyConWidth && h == sJoyConHeight ? sJoyConAsked + 1 : 1;
        sJoyConWidth = w;
        sJoyConHeight = h;
        sJoyConPixelsX = pixelsX * fit;
        sJoyConPixelsY = pixelsY * fit;
    }

    bool paintJoyConAhead(u32 budgetMicros) {
        const auto asked = [](const JoyConSet& rSet) { return rSet.mWidth == sJoyConWidth && rSet.mHeight == sJoyConHeight; };
        if (sJoyConWidth == 0 || (sJoyConShown >= 0 && asked(sJoyConSets[sJoyConShown]))) {
            return false;
        }
        const s32 index = sJoyConShown == 0 ? 1 : 0;
        JoyConSet& rSet = sJoyConSets[index];
        const s64 start = NebulaClock::nowNs();
        if (!asked(rSet)) {
            // A density settles over a few frames (the 2D layer's scale follows the window); a set the screen showed is
            // still being drawn for a few frames after it stops.
            if (sJoyConAsked < cJcSteady || (rSet.mRetired != 0 && (start - rSet.mRetired) / 1000000 < cJcRetireMs)) {
                return true;
            }
            startJoyConSet(index);
        }
        do {
            paintJoyConBand(rSet);
        } while (rSet.mNext < cJcPictures && (NebulaClock::nowNs() - start) / 1000 < budgetMicros);
        rSet.mSpent += NebulaClock::nowNs() - start;
        rSet.mCalls++;
        if (rSet.mNext < cJcPictures) {
            return true;
        }
        nebula_log("NebulaIcons: Joy-Con pictures %d x %d painted in %d us over %d frames\n", static_cast< int >(rSet.mWidth),
                 static_cast< int >(rSet.mHeight), static_cast< int >(rSet.mSpent / 1000),
                 static_cast< int >(rSet.mCalls));
        if (sJoyConShown >= 0) {
            sJoyConSets[sJoyConShown].mRetired = NebulaClock::nowNs();
        }
        sJoyConShown = index;
        return false;
    }

    Ui::TexMap* joyConPicture(s32 half, s32 look) {
        return sJoyConShown >= 0 ? &sJoyConSets[sJoyConShown].mMaps[half][look] : nullptr;
    }

    void joyConPictureSize(f32* pWidth, f32* pHeight) {
        const JoyConSet* pSet = sJoyConShown >= 0 ? &sJoyConSets[sJoyConShown] : nullptr;
        *pWidth = pSet != nullptr ? pSet->mWidth / pSet->mPixelsX : cJoyConBox[0];
        *pHeight = pSet != nullptr ? pSet->mHeight / pSet->mPixelsY : cJoyConBox[1];
    }

    void drawWithIcons(const LayoutActor* pActor) {
        LayoutManager* pManager = pActor->getLayoutManager();
        Swaps swaps;
        if (sPicturesShown && pManager != nullptr && pManager->mLayout != nullptr && pManager->mLayout->mpRootPane != nullptr) {
            swapPane(pManager->mLayout->mpRootPane, swaps, 0);
        }
        pActor->LayoutActor::draw();
        // The game's own pictures back: its animation and the next frame see the layout as it was.
        for (s32 i = 0; i < swaps.mMaps.size(); i++) {
            *swaps.mMaps[i] = swaps.mSaved[i];
        }
    }
};  // namespace NebulaIcons
