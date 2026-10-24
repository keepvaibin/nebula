#include "Game/Screen/NebulaIcons.hpp"

#ifdef TARGET_PC
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemFontHolder.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <JSystem/JKernel/JKRExpHeap.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <nebula_game_api.h>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/material.h>
#include <nw4r/ut/ResFont.h>
#include <revolution/os.h>

// Nebula's icons are textures of its own, never the game's: the picture font gains sheets after its own (each
// controller family's icons, one to a cell) and a code map before its own that sends each icon's code to the glyph the
// controller shows; the A bubble's two pictures are ours only while it draws. The game's textures keep their bytes (a
// texture pack's replacements of them still apply) and ours are the same every run, so a pack can name them too
// (GALAXY_EXPORT_ART writes one, eight times their size).
namespace {
    // The picture font's code of each icon, in NEBULA_BUTTON_ICONS' order (A, B, C, Z, 1, 2, Plus, Minus, HOME, Control
    // Pad); the code map we put first covers them all.
    const u16 cCodes[NEBULA_BUTTON_ICONS] = {0x30, 0x31, 0x32, 0x49, 0x35, 0x36, 0x48, 0x47, 0x46, 0x43};
    enum { IconPlus = 6, IconMinus = 7, IconHome = 8, IconPad = 9 };
    const u16 cMapFirst = 0x30;
    const u16 cMapLast = 0x49;
    // The canvas's largest picture at the game's size (the A bubble, 48 pixels), and a pack's size of ours.
    const s32 cCellMax = 48;
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

    // A picture being drawn, straight alpha; `mScale` its pixels to one of the game's.
    struct Canvas {
        Rgba* mPixels;
        s32 mWidth;
        s32 mHeight;
        f32 mScale;

        void clear() { memset(mPixels, 0, sizeof(Rgba) * mWidth * mHeight); }

        // Straight alpha over what is drawn, `cover` of a pixel.
        void blend(s32 x, s32 y, const Rgba& rColor, f32 cover) {
            cover *= rColor.a;
            if (cover <= 0.0f) {
                return;
            }
            Rgba& rDst = mPixels[y * mWidth + x];
            const f32 a = cover + rDst.a * (1.0f - cover);
            const f32 keep = rDst.a * (1.0f - cover);
            rDst.r = (rColor.r * cover + rDst.r * keep) / a;
            rDst.g = (rColor.g * cover + rDst.g * keep) / a;
            rDst.b = (rColor.b * cover + rDst.b * keep) / a;
            rDst.a = a;
        }
    };
    Rgba sCanvasPixels[cCellMax * cCellMax];

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

    f32 coverOf(f32 distance) {
        return NebulaUi::clamp(0.5f - distance, 0.0f, 1.0f);
    }

    // `rShape` ringed `ring` wide in `rRing`, its face from `rTop` at its top to `rBottom` at its foot.
    void drawShape(Canvas& rCanvas, const Shape& rShape, const Rgba& rRing, f32 ring, const Rgba& rTop, const Rgba& rBottom) {
        for (s32 y = 0; y < rCanvas.mHeight; y++) {
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
        for (s32 py = 0; py < rCanvas.mHeight; py++) {
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
    // each side) with round ends, in a box one capital tall from the capitals' top (0) to their foot (1). A segment is
    // (x0, y0, x1, y1); an arc (centre, radius, its start and end directions) turns clockwise on screen, half a turn at
    // most.
    struct Stroke {
        u8 mArc;
        f32 mA, mB, mC, mD, mE, mF, mG;
    };
    struct Letter {
        char mCode;
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
    };
    const Letter cLetters[] = {
        {'A', 0.84f, 0, 3},   {'B', 0.6675f, 3, 7}, {'L', 0.64f, 10, 2}, {'R', 0.74f, 12, 5},
        {'S', 0.62f, 17, 4},  {'T', 0.72f, 21, 2},  {'X', 0.8f, 23, 2},  {'Y', 0.82f, 25, 3},
        {'Z', 0.72f, 28, 3},  {'1', 0.48f, 31, 2},  {'2', 0.645f, 33, 4}, {'3', 0.605f, 37, 4},
    };

    const Letter* findLetter(char code) {
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
        const Letter* letters[4];
        s32 count = 0;
        f32 width = 0.0f;
        for (const char* p = pText; *p != 0 && count < 4; p++) {
            letters[count] = findLetter(*p);
            if (letters[count] != nullptr) {
                width += (count > 0 ? cLetterGap : 0.0f) + letters[count]->mAdvance;
                count++;
            }
        }
        f32 left = x - width * capital * 0.5f;
        const f32 top = y - capital * 0.5f;
        for (s32 i = 0; i < count; i++) {
            const Letter& rLetter = *letters[i];
            // Only the pixels its box (and a stroke's half around it) covers.
            const f32 margin = capital * cStrokeHalf + 1.0f;
            const s32 x0 = static_cast< s32 >(NebulaUi::clamp(left - margin, 0.0f, static_cast< f32 >(rCanvas.mWidth)));
            const s32 x1 = static_cast< s32 >(NebulaUi::clamp(left + rLetter.mAdvance * capital + margin + 1.0f, 0.0f, static_cast< f32 >(rCanvas.mWidth)));
            const s32 y0 = static_cast< s32 >(NebulaUi::clamp(top - margin, 0.0f, static_cast< f32 >(rCanvas.mHeight)));
            const s32 y1 = static_cast< s32 >(NebulaUi::clamp(top + capital + margin + 1.0f, 0.0f, static_cast< f32 >(rCanvas.mHeight)));
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
    const u32 cXboxFaces[4] = {0x5FA83CFF, 0xD8443AFF, 0x3C78D8FF, 0xE6B422FF};
    const u32 cRing = 0x262A33FF;
    const u32 cDarkTop = 0x52555EFF;
    const u32 cDarkBottom = 0x2E3036FF;
    const u32 cWhiteTop = 0xFFFFFFFF;
    const u32 cWhiteBottom = 0xD3D9E6FF;
    const u32 cInk = 0x2B3045FF;
    const u32 cWhite = 0xFFFFFFFF;

    Rgba darkened(const Rgba& rColor) {
        const Rgba c = {rColor.r * 0.9f, rColor.g * 0.9f, rColor.b * 0.9f, rColor.a};
        return c;
    }

    Rgba decodeTexel(u16 texel) {
        if ((texel & 0x8000) != 0) {
            const Rgba c = {(texel >> 10 & 31) / 31.0f, (texel >> 5 & 31) / 31.0f, (texel & 31) / 31.0f, 1.0f};
            return c;
        }
        const Rgba c = {(texel >> 8 & 15) / 15.0f, (texel >> 4 & 15) / 15.0f, (texel & 15) / 15.0f, (texel >> 12 & 7) / 7.0f};
        return c;
    }

    // How an icon is drawn: in text (its font cell), or as the A bubble's two frames, as the console's own two pictures:
    // the button up (a 39-pixel button at (4, 4) of 48, lit from above, its shadow down and right) and pressed (36 of
    // 40, centred, its shading reversed).
    enum { LookText, LookUp, LookPressed };

    // The icon of `glyph` at `place` in `style` on the canvas (cleared first); false when the Wii's own shows.
    bool paint(Canvas& rCanvas, s32 style, s32 glyph, s32 place, s32 look) {
        static const char* const cNintendoFaces[4] = {"B", "A", "Y", "X"};
        static const char* const cXboxLetters[4] = {"A", "B", "X", "Y"};
        static const char* const cShoulders[3][2] = {{"LB", "RB"}, {"L", "R"}, {"L1", "R1"}};
        static const char* const cTriggers[3][2] = {{"LT", "RT"}, {"ZL", "ZR"}, {"L2", "R2"}};
        static const char* const cSticks[3][2] = {{"LS", "RS"}, {"L", "R"}, {"L3", "R3"}};
        const s32 family = style == NEBULA_ICON_STYLE_XBOX ? 0 : style == NEBULA_ICON_STYLE_NINTENDO ? 1 : 2;
        const bool xbox = family == 0;
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
            for (s32 y = 0; y < height; y++) {
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
            if (xbox) {
                // Y's letter dark on its yellow, as the console's own.
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
            drawText(rCanvas, cShoulders[family][place & 1], cx, cy, diameter * 0.3f, ink);
            return true;
        case NEBULA_ICON_TRIGGER:
            drawShape(rCanvas, box, ringColor, ring, faceTop, faceBottom);
            drawText(rCanvas, cTriggers[family][place & 1], cx, cy, diameter * 0.3f, ink);
            return true;
        case NEBULA_ICON_STICK: {
            // The stick's cap and its rim.
            drawShape(rCanvas, circle, ringColor, ring, faceTop, faceBottom);
            const Shape rim = {cx, cy, diameter * 0.36f, diameter * 0.36f, diameter * 0.36f};
            drawShape(rCanvas, rim, rgba(xbox ? 0x6A6E78FF : 0xB8C0D0FF), ring * 0.7f, faceTop, faceBottom);
            drawText(rCanvas, cSticks[family][place & 1], cx, cy, diameter * (family == 1 ? 0.32f : 0.24f), ink);
            return true;
        }
        case NEBULA_ICON_START:
            if (family == 1) {
                return false;  // Plus: the Wii's own
            }
            drawShape(rCanvas, family == 2 ? pill : circle, ringColor, ring, faceTop, faceBottom);
            // Three lines (Menu, Options).
            for (s32 line = -1; line <= 1; line++) {
                drawBar(rCanvas, cx, cy + line * diameter * 0.15f, diameter * 0.2f, diameter * 0.035f, ink);
            }
            return true;
        case NEBULA_ICON_BACK:
            if (family == 1) {
                return false;  // Minus: the Wii's own
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
        default:
            return false;
        }
    }

    u16 encode(const Rgba& rColor) {
        const f32 a = NebulaUi::clamp(rColor.a, 0.0f, 1.0f);
        const f32 r = NebulaUi::clamp(rColor.r, 0.0f, 1.0f);
        const f32 g = NebulaUi::clamp(rColor.g, 0.0f, 1.0f);
        const f32 b = NebulaUi::clamp(rColor.b, 0.0f, 1.0f);
        if (a >= 0.94f) {
            return static_cast< u16 >(0x8000 | static_cast< u32 >(r * 31.0f + 0.5f) << 10 | static_cast< u32 >(g * 31.0f + 0.5f) << 5 |
                                      static_cast< u32 >(b * 31.0f + 0.5f));
        }
        if (a < 1.0f / 14.0f) {
            return 0;  // clear: the same bytes wherever nothing is drawn
        }
        return static_cast< u16 >(static_cast< u32 >(a * 7.0f + 0.5f) << 12 | static_cast< u32 >(r * 15.0f + 0.5f) << 8 |
                                  static_cast< u32 >(g * 15.0f + 0.5f) << 4 | static_cast< u32 >(b * 15.0f + 0.5f));
    }

    u8* texelAt(u8* pPicture, s32 pictureWidth, s32 x, s32 y) {
        return pPicture + (((y / 4) * (pictureWidth / 4) + x / 4) * 16 + (y % 4) * 4 + x % 4) * 2;
    }

    // The canvas into a picture `pictureWidth` wide (RGB5A3, 4x4 tiles of big-endian texels) at (x, y).
    void encodeInto(const Canvas& rCanvas, u8* pPicture, s32 pictureWidth, s32 x, s32 y) {
        for (s32 py = 0; py < rCanvas.mHeight; py++) {
            for (s32 px = 0; px < rCanvas.mWidth; px++) {
                u8* pTexel = texelAt(pPicture, pictureWidth, x + px, y + py);
                const u16 texel = encode(rCanvas.mPixels[py * rCanvas.mWidth + px]);
                pTexel[0] = static_cast< u8 >(texel >> 8);
                pTexel[1] = static_cast< u8 >(texel);
            }
        }
    }

    // The canvas as RGBA8 into an image `imageWidth` wide at (x, y).
    void canvasToRgba8(const Canvas& rCanvas, u8* pImage, s32 imageWidth, s32 x, s32 y) {
        for (s32 py = 0; py < rCanvas.mHeight; py++) {
            for (s32 px = 0; px < rCanvas.mWidth; px++) {
                const Rgba& c = rCanvas.mPixels[py * rCanvas.mWidth + px];
                u8* pOut = pImage + ((y + py) * imageWidth + x + px) * 4;
                pOut[0] = static_cast< u8 >(NebulaUi::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
                pOut[1] = static_cast< u8 >(NebulaUi::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
                pOut[2] = static_cast< u8 >(NebulaUi::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
                pOut[3] = static_cast< u8 >(NebulaUi::clamp(c.a, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
    }

    // A picture of ours (RGB5A3) as RGBA8.
    void pictureToRgba8(const u8* pPicture, s32 width, s32 height, u8* pImage) {
        for (s32 y = 0; y < height; y++) {
            for (s32 x = 0; x < width; x++) {
                const u8* pTexel = texelAt(const_cast< u8* >(pPicture), width, x, y);
                const Rgba c = decodeTexel(static_cast< u16 >(pTexel[0] << 8 | pTexel[1]));
                u8* pOut = pImage + (y * width + x) * 4;
                pOut[0] = static_cast< u8 >(c.r * 255.0f + 0.5f);
                pOut[1] = static_cast< u8 >(c.g * 255.0f + 0.5f);
                pOut[2] = static_cast< u8 >(c.b * 255.0f + 0.5f);
                pOut[3] = static_cast< u8 >(c.a * 255.0f + 0.5f);
            }
        }
    }

    // Each family's icons, one to a cell: face buttons by place, shoulders, triggers, sticks, Start, Back.
    const s32 cFamilies = 3;  // Xbox, Nintendo, PlayStation: NEBULA_ICON_STYLE_* - 1
    const char* const cFamilyNames[cFamilies] = {"Xbox", "Nintendo", "PlayStation"};
    struct AtlasIcon {
        u8 mGlyph;
        u8 mPlace;
        const char* mName;
    };
    const AtlasIcon cAtlas[] = {
        {NEBULA_ICON_FACE, 0, "face-south"},          {NEBULA_ICON_FACE, 1, "face-east"},       {NEBULA_ICON_FACE, 2, "face-west"},
        {NEBULA_ICON_FACE, 3, "face-north"},          {NEBULA_ICON_SHOULDER, 0, "shoulder-left"}, {NEBULA_ICON_SHOULDER, 1, "shoulder-right"},
        {NEBULA_ICON_TRIGGER, 0, "trigger-left"},     {NEBULA_ICON_TRIGGER, 1, "trigger-right"}, {NEBULA_ICON_STICK, 0, "stick-left"},
        {NEBULA_ICON_STICK, 1, "stick-right"},        {NEBULA_ICON_START, 0, "start"},           {NEBULA_ICON_BACK, 0, "back"},
    };
    const s32 cAtlasIcons = sizeof(cAtlas) / sizeof(cAtlas[0]);

    s32 atlasSlot(s32 glyph, s32 place) {
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
        default:
            return -1;
        }
    }

    // The picture font as we extend it.
    struct Font {
        nw4r::ut::ResFont* mFont;
        nw4r::ut::FontCodeMap* mMap;  // ours, before the font's own
        u8* mSheets;                  // the font's sheets (their bytes as they were), then ours
        s32 mSheetBytes;
        s32 mSheetWidth;
        s32 mSheetHeight;
        s32 mCellWidth;   // a cell's pitch less its 1-pixel gap
        s32 mCellHeight;
        s32 mRow;         // cells across a sheet
        s32 mCells;       // cells in a sheet
        s32 mFamilySheets;
        s32 mFirstSheet;  // the first family's first sheet
        s32 mGlyphWidth;  // every icon of ours, as A's cell has it
        s32 mGlyphHeight;
        u16 mOriginal[cMapLast - cMapFirst + 1];  // each code's own glyph
        bool mPainted[cFamilies][cAtlasIcons];
        bool mFamilyDrawn[cFamilies];
    };
    Font sFont;
    enum { FontUnread, FontReady, FontUnusable };
    s32 sFontState = FontUnread;
    NebulaButtonIcons sShown;  // zero: the Wii's own

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

    bool fitsSelfRel(const void* pFrom, const void* pTo) {
        const s64 d = static_cast< const char* >(pTo) - static_cast< const char* >(pFrom);
        return d > -0x7FFFFFF0LL && d < 0x7FFFFFF0LL;
    }

    // The picture font gains our sheets, glyph widths and code map, once it is loaded. A font whose icons are not RGB5A3
    // cells of a usual size (a mod's) keeps its own icons.
    s32 install() {
        GameSystem* pSystem = SingletonHolder< GameSystem >::get();
        nw4r::ut::ResFont* pFont = pSystem != nullptr && pSystem->mFontHolder != nullptr ? pSystem->mFontHolder->mPictureFont : nullptr;
        if (pFont == nullptr || pFont->mFontInfo == nullptr) {
            return FontUnread;
        }
        nw4r::ut::FontInformation* pInfo = pFont->mFontInfo;
        nw4r::ut::FontTextureGlyph* pGlyph = pInfo->pGlyph;
        bool usable = pGlyph != nullptr && pGlyph->sheetFormat == GX_TF_RGB5A3 && pGlyph->sheetRow > 0 && pGlyph->sheetLine > 0 &&
                      pGlyph->cellWidth <= cCellMax && pGlyph->cellHeight <= cCellMax && pGlyph->sheetImage.get() != nullptr &&
                      (pGlyph->cellWidth + 1) * pGlyph->sheetRow <= pGlyph->sheetWidth &&
                      (pGlyph->cellHeight + 1) * pGlyph->sheetLine <= pGlyph->sheetHeight &&
                      static_cast< u32 >(pGlyph->sheetSize) == static_cast< u32 >(pGlyph->sheetWidth) * pGlyph->sheetHeight * 2u;
        for (s32 i = 0; i < NEBULA_BUTTON_ICONS && usable; i++) {
            usable = pFont->HasGlyph(cCodes[i]);
        }
        if (!usable) {
            OSReport("NebulaIcons: the picture font's icons are not RGB5A3 cells; they stay the Wii's\n");
            return FontUnusable;
        }
        Font& f = sFont;
        f.mFont = pFont;
        f.mSheetBytes = static_cast< s32 >(pGlyph->sheetSize);
        f.mSheetWidth = pGlyph->sheetWidth;
        f.mSheetHeight = pGlyph->sheetHeight;
        f.mCellWidth = pGlyph->cellWidth;
        f.mCellHeight = pGlyph->cellHeight;
        f.mRow = pGlyph->sheetRow;
        f.mCells = pGlyph->sheetRow * pGlyph->sheetLine;
        f.mFamilySheets = (cAtlasIcons + f.mCells - 1) / f.mCells;
        f.mFirstSheet = pGlyph->sheetNum;
        const s32 sheets = f.mFirstSheet + cFamilies * f.mFamilySheets;
        const s32 added = cFamilies * f.mFamilySheets * f.mCells;
        if (sheets * f.mCells > 0xFFFF) {
            return FontUnusable;
        }
        const nw4r::ut::CharWidths widths = pFont->GetCharWidths(cCodes[0]);
        f.mGlyphWidth = widths.glyphWidth < f.mCellWidth ? widths.glyphWidth : f.mCellWidth;
        f.mGlyphHeight = f.mCellHeight;
        for (u16 code = cMapFirst; code <= cMapLast; code++) {
            f.mOriginal[code - cMapFirst] = pFont->FindGlyphIndex(code);
        }
        // One block that lasts the game (the stationed heap's room for it, HeapMemoryWatcher), as near the font as its
        // offsets need: the sheets, our widths, our code map.
        const u32 sheetsBytes = static_cast< u32 >(sheets * f.mSheetBytes);
        const u32 widthBytes = (sizeof(nw4r::ut::FontWidth) + added * sizeof(nw4r::ut::CharWidths) + 3u) & ~3u;
        const u32 mapBytes = sizeof(nw4r::ut::FontCodeMap) + (cMapLast - cMapFirst + 1) * sizeof(u16);
        const u32 bytes = sheetsBytes + widthBytes + mapBytes;
        JKRHeap* pHeap = MR::getStationedHeapGDDR3();
        u8* pBlock = bytes <= NebulaIcons::cStationedBytes && pHeap != nullptr && pHeap->getFreeSize() >= static_cast< s32 >(bytes + 64u)
                         ? static_cast< u8* >(JKRHeap::alloc(bytes, 32, pHeap))
                         : nullptr;
        if (pBlock == nullptr || !fitsSelfRel(pInfo, pBlock) || !fitsSelfRel(pInfo, pBlock + bytes) ||
            !fitsSelfRel(pBlock, pInfo->pWidth.get()) || !fitsSelfRel(pBlock, pInfo->pMap.get())) {
            OSReport("NebulaIcons: no room for the icons (%u bytes, the stationed heap has %d); they stay the Wii's\n", bytes,
                     pHeap != nullptr ? pHeap->getFreeSize() : -1);
            return FontUnusable;
        }
        memcpy(pBlock, pGlyph->sheetImage.get(), static_cast< size_t >(f.mFirstSheet) * f.mSheetBytes);
        memset(pBlock + f.mFirstSheet * f.mSheetBytes, 0, sheetsBytes - f.mFirstSheet * f.mSheetBytes);
        f.mSheets = pBlock;

        nw4r::ut::FontWidth* pWidth = reinterpret_cast< nw4r::ut::FontWidth* >(pBlock + sheetsBytes);
        pWidth->indexBegin = static_cast< u16 >(f.mFirstSheet * f.mCells);
        pWidth->indexEnd = static_cast< u16 >(f.mFirstSheet * f.mCells + added - 1);
        pWidth->pNext = pInfo->pWidth.get();
        for (s32 i = 0; i < added; i++) {
            pWidth->widthTable[i] = widths;
        }
        nw4r::ut::FontCodeMap* pMap = reinterpret_cast< nw4r::ut::FontCodeMap* >(pBlock + sheetsBytes + widthBytes);
        pMap->ccodeBegin = cMapFirst;
        pMap->ccodeEnd = cMapLast;
        pMap->mappingMethod = 1;
        pMap->reserved = 0;
        pMap->pNext = pInfo->pMap.get();
        for (u16 code = cMapFirst; code <= cMapLast; code++) {
            pMap->mapInfo[code - cMapFirst] = f.mOriginal[code - cMapFirst];
        }
        f.mMap = pMap;
        DCStoreRange(pBlock, sheetsBytes);
        pGlyph->sheetImage = pBlock;
        pGlyph->sheetNum = static_cast< u16 >(sheets);
        pInfo->pWidth = pWidth;
        pInfo->pMap = pMap;
        return FontReady;
    }

    // A family's icons into its cells, the first time it shows.
    void drawFamily(s32 family) {
        if (sFont.mFamilyDrawn[family]) {
            return;
        }
        Canvas canvas = {sCanvasPixels, sFont.mGlyphWidth, sFont.mGlyphHeight, 1.0f};
        for (s32 k = 0; k < cAtlasIcons; k++) {
            sFont.mPainted[family][k] = paint(canvas, family + 1, cAtlas[k].mGlyph, cAtlas[k].mPlace, LookText);
            if (sFont.mPainted[family][k]) {
                u8* pSheet;
                s32 x, y;
                cellOf(atlasGlyph(family, k), &pSheet, &x, &y);
                encodeInto(canvas, pSheet, sFont.mSheetWidth, x, y);
            }
        }
        DCStoreRange(sFont.mSheets + (sFont.mFirstSheet + family * sFont.mFamilySheets) * sFont.mSheetBytes,
                     static_cast< u32 >(sFont.mFamilySheets * sFont.mSheetBytes));
        sFont.mFamilyDrawn[family] = true;
    }

    // The glyph icon `icon` shows: ours, a Wii icon in its place (Plus, Minus, HOME or the Control Pad, which a Nintendo
    // controller's look like), or its own.
    u16 shownGlyph(s32 family, s32 icon, s32 glyph, s32 place) {
        if (family < 0 || glyph == NEBULA_ICON_KEEP) {
            return originalGlyph(icon);
        }
        const s32 slot = atlasSlot(glyph, place);
        if (slot >= 0 && sFont.mPainted[family][slot]) {
            return atlasGlyph(family, slot);
        }
        return originalGlyph(glyph == NEBULA_ICON_START ? IconPlus : glyph == NEBULA_ICON_BACK ? IconMinus : glyph == NEBULA_ICON_HOME ? IconHome
                                                                         : glyph == NEBULA_ICON_DPAD ? IconPad : icon);
    }

    // The A button over people and signs: IconAButton's two pictures (the button up, 48 pixels square, and pressed, 40;
    // IA4) give way to ours, drawn as the font's A is, while it draws.
    struct Picture {
        alignas(32) u8 mTexels[cCellMax * cCellMax * 2];
    };
    const s32 cPictureSizes[2] = {48, 40};
    const s32 cPictureLooks[2] = {LookUp, LookPressed};
    Picture sPictures[2];
    bool sPicturesShown = false;

    // The font's glyph `glyph` scaled into the canvas (an icon the style borrows from the Wii's).
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
                    const u8* pTexel = texelAt(pSheet, sFont.mSheetWidth, cellX + sx, cellY + sy);
                    rCanvas.mPixels[y * rCanvas.mWidth + x] = decodeTexel(static_cast< u16 >(pTexel[0] << 8 | pTexel[1]));
                }
            }
        }
    }

    // The A bubble's picture `k` of the icon at A's code.
    void drawPicture(s32 k, s32 family, s32 glyph, s32 place) {
        Canvas canvas = {sCanvasPixels, cPictureSizes[k], cPictureSizes[k], 1.0f};
        if (!paint(canvas, family + 1, glyph, place, cPictureLooks[k])) {
            paintFromGlyph(canvas, shownGlyph(family, 0, glyph, place));
        }
        encodeInto(canvas, sPictures[k].mTexels, cPictureSizes[k], 0, 0);
        DCStoreRange(sPictures[k].mTexels, sizeof(sPictures[k].mTexels));
    }

    // GALAXY_EXPORT_ART: each family's sheets and A bubbles at the game's size and eight times it, and each icon alone.
    void exportArt() {
        if (sFont.mSheetWidth * cHdScale > cHdSheetMax || sFont.mSheetHeight * cHdScale > cHdSheetMax) {
            OSReport("NebulaIcons: the picture font's sheets are too large to export at eight times\n");
            return;
        }
        const s32 hdIcon = cCellMax * cHdScale;
        const s32 scale = cHdScale;
        const s32 hdSheetWidth = sFont.mSheetWidth * scale;
        const s32 hdSheetHeight = sFont.mSheetHeight * scale;
        // The game's size of a sheet, an icon or an A bubble picture (the largest of them).
        const s32 sheetPixels = sFont.mSheetWidth * sFont.mSheetHeight;
        const s32 imagePixels = sheetPixels > cCellMax * cCellMax ? sheetPixels : cCellMax * cCellMax;
        Rgba* pHdPixels = static_cast< Rgba* >(malloc(sizeof(Rgba) * hdIcon * hdIcon));
        u8* pHdImage = static_cast< u8* >(malloc(static_cast< size_t >(hdIcon) * hdIcon * 4));
        u8* pImage = static_cast< u8* >(malloc(static_cast< size_t >(imagePixels) * 4));
        u8* pHdSheet = static_cast< u8* >(malloc(static_cast< size_t >(hdSheetWidth) * hdSheetHeight * 4));
        if (pHdPixels == nullptr || pHdImage == nullptr || pImage == nullptr || pHdSheet == nullptr) {
            OSReport("NebulaIcons: no memory to export the icons\n");
            free(pHdPixels);
            free(pHdImage);
            free(pImage);
            free(pHdSheet);
            return;
        }
        char name[64];
        for (s32 family = 0; family < cFamilies; family++) {
            drawFamily(family);
            for (s32 sheet = 0; sheet < sFont.mFamilySheets; sheet++) {
                memset(pHdSheet, 0, static_cast< size_t >(hdSheetWidth) * hdSheetHeight * 4);
                for (s32 k = sheet * sFont.mCells; k < (sheet + 1) * sFont.mCells && k < cAtlasIcons; k++) {
                    if (!sFont.mPainted[family][k]) {
                        continue;
                    }
                    u8* pSheet;
                    s32 x, y;
                    cellOf(atlasGlyph(family, k), &pSheet, &x, &y);
                    Canvas hd = {pHdPixels, sFont.mGlyphWidth * scale, sFont.mGlyphHeight * scale, static_cast< f32 >(scale)};
                    paint(hd, family + 1, cAtlas[k].mGlyph, cAtlas[k].mPlace, LookText);
                    canvasToRgba8(hd, pHdSheet, hdSheetWidth, x * scale, y * scale);
                    // The icon alone, as text shows it.
                    Canvas native = {sCanvasPixels, sFont.mGlyphWidth, sFont.mGlyphHeight, 1.0f};
                    paint(native, family + 1, cAtlas[k].mGlyph, cAtlas[k].mPlace, LookText);
                    canvasToRgba8(native, pImage, native.mWidth, 0, 0);
                    canvasToRgba8(hd, pHdImage, hd.mWidth, 0, 0);
                    snprintf(name, sizeof(name), "%s/%s", cFamilyNames[family], cAtlas[k].mName);
                    NebulaArtExport art = {name, nullptr, static_cast< u32 >(native.mWidth), static_cast< u32 >(native.mHeight), GX_TF_RGB5A3,
                                           pImage, pHdImage, static_cast< u32 >(hd.mWidth), static_cast< u32 >(hd.mHeight)};
                    __NebulaArtExport(&art);
                }
                const u8* pTexels = sFont.mSheets + (sFont.mFirstSheet + family * sFont.mFamilySheets + sheet) * sFont.mSheetBytes;
                pictureToRgba8(pTexels, sFont.mSheetWidth, sFont.mSheetHeight, pImage);
                snprintf(name, sizeof(name), "%s/sheet-%d", cFamilyNames[family], static_cast< int >(sheet + 1));
                NebulaArtExport art = {name, pTexels, static_cast< u32 >(sFont.mSheetWidth), static_cast< u32 >(sFont.mSheetHeight), GX_TF_RGB5A3,
                                       pImage, pHdSheet, static_cast< u32 >(hdSheetWidth), static_cast< u32 >(hdSheetHeight)};
                __NebulaArtExport(&art);
            }
            // The A bubble as each icon (A's code can show any of them).
            for (s32 k = 0; k < cAtlasIcons; k++) {
                for (s32 look = 0; look < 2 && sFont.mPainted[family][k]; look++) {
                    const s32 size = cPictureSizes[look];
                    Canvas native = {sCanvasPixels, size, size, 1.0f};
                    paint(native, family + 1, cAtlas[k].mGlyph, cAtlas[k].mPlace, cPictureLooks[look]);
                    Picture picture;
                    encodeInto(native, picture.mTexels, size, 0, 0);
                    pictureToRgba8(picture.mTexels, size, size, pImage);
                    Canvas hd = {pHdPixels, size * cHdScale, size * cHdScale, static_cast< f32 >(cHdScale)};
                    paint(hd, family + 1, cAtlas[k].mGlyph, cAtlas[k].mPlace, cPictureLooks[look]);
                    canvasToRgba8(hd, pHdImage, hd.mWidth, 0, 0);
                    snprintf(name, sizeof(name), "%s/bubble-%s-%s", cFamilyNames[family], cAtlas[k].mName, look == 0 ? "up" : "pressed");
                    NebulaArtExport art = {name, picture.mTexels, static_cast< u32 >(size), static_cast< u32 >(size), GX_TF_RGB5A3,
                                           pImage, pHdImage, static_cast< u32 >(hd.mWidth), static_cast< u32 >(hd.mHeight)};
                    __NebulaArtExport(&art);
                }
            }
        }
        free(pHdPixels);
        free(pHdImage);
        free(pImage);
        free(pHdSheet);
        OSReport("NebulaIcons: exported the icons\n");
    }

    // Each of the IconAButton layout's pictures of A, ours in its place while it draws.
    const s32 cSwapsMax = 8;
    struct Swaps {
        nw4r::lyt::TexMap* mMaps[cSwapsMax];
        nw4r::lyt::TexMap mSaved[cSwapsMax];
        s32 mCount;
    };

    void swapPane(nw4r::lyt::Pane* pPane, Swaps& rSwaps, s32 depth) {
        nw4r::lyt::Material* pMaterial = pPane->GetMaterial();
        for (u8 i = 0; pMaterial != nullptr && i < pMaterial->GetTextureNum() && rSwaps.mCount < cSwapsMax; i++) {
            nw4r::lyt::TexMap& rMap = pMaterial->GetTexMapAry()[i];
            for (s32 k = 0; k < 2; k++) {
                if (rMap.mWidth == cPictureSizes[k] && rMap.mHeight == cPictureSizes[k] && rMap.GetTexelFormat() == GX_TF_IA4) {
                    rSwaps.mMaps[rSwaps.mCount] = &rMap;
                    rSwaps.mSaved[rSwaps.mCount] = rMap;
                    rSwaps.mCount++;
                    rMap.SetImage(sPictures[k].mTexels);
                    rMap.SetTexelFormat(GX_TF_RGB5A3);
                    rMap.SetMipMap(false);
                    rMap.SetLOD(0.0f, 0.0f);
                    break;
                }
            }
        }
        if (depth > 16) {
            return;
        }
        for (nw4r::lyt::PaneList::Iterator it = pPane->GetChildList().GetBeginIter(); it != pPane->GetChildList().GetEndIter(); ++it) {
            swapPane(&*it, rSwaps, depth + 1);
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
            OSReport("NebulaIcons: the picture font is not loaded; its icons stay the Wii's\n");
            sFontState = FontUnusable;
        } else if (sFontState == FontReady && __NebulaArtExport(nullptr) != 0) {
            exportArt();
        }
    }

    void update() {
        NebulaButtonIcons now;
        memset(&now, 0, sizeof(now));
        __NebulaButtonIcons(&now);
        // The Wii Remote's pictures follow the remote even when the icons cannot be drawn.
        sShown.wii_remote = now.wii_remote;
        if (sFontState != FontReady || memcmp(&now, &sShown, sizeof(now)) == 0) {
            return;
        }
        const s32 family = now.style >= NEBULA_ICON_STYLE_XBOX && now.style <= NEBULA_ICON_STYLE_PLAYSTATION ? now.style - 1 : -1;
        if (family >= 0) {
            drawFamily(family);
        }
        for (s32 i = 0; i < NEBULA_BUTTON_ICONS; i++) {
            sFont.mMap->mapInfo[cCodes[i] - cMapFirst] = shownGlyph(family, i, now.glyphs[i], now.places[i]);
        }
        // The A bubble's two frames, as the font's A.
        sPicturesShown = family >= 0 && now.glyphs[0] != NEBULA_ICON_KEEP;
        for (s32 k = 0; k < 2 && sPicturesShown; k++) {
            drawPicture(k, family, now.glyphs[0], now.places[0]);
        }
        sShown = now;
    }

    bool wiiRemoteInUse() {
        return sShown.wii_remote != 0;
    }

    void drawWithIcons(const LayoutActor* pActor) {
        LayoutManager* pManager = pActor->getLayoutManager();
        Swaps swaps;
        swaps.mCount = 0;
        if (sPicturesShown && pManager != nullptr && pManager->mLayout != nullptr && pManager->mLayout->mpRootPane != nullptr) {
            swapPane(pManager->mLayout->mpRootPane, swaps, 0);
        }
        pActor->LayoutActor::draw();
        // The console's own again: its animation and the next frame see the layout as it was.
        for (s32 i = 0; i < swaps.mCount; i++) {
            *swaps.mMaps[i] = swaps.mSaved[i];
        }
    }
};  // namespace NebulaIcons
#endif
