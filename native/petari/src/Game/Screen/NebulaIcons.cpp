#include "Game/Screen/NebulaIcons.hpp"

#ifdef TARGET_PC
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemFontHolder.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <cmath>
#include <cstring>
#include <nebula_game_api.h>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/material.h>
#include <nw4r/ut/ResFont.h>
#include <revolution/os.h>

namespace {
    // The picture font's code of each icon, in NEBULA_BUTTON_ICONS' order (A, B, C, Z, 1, 2, Plus, Minus, HOME, Control
    // Pad), and the largest cell it has (34 x 35).
    const u16 cCodes[NEBULA_BUTTON_ICONS] = {0x30, 0x31, 0x32, 0x49, 0x35, 0x36, 0x48, 0x47, 0x46, 0x43};
    enum { IconPlus = 6, IconMinus = 7, IconHome = 8, IconPad = 9 };
    const s32 cCellMax = 48;

    struct Rgba {
        f32 r, g, b, a;
    };

    Rgba rgba(u32 color) {
        const Rgba c = {(color >> 24 & 0xFF) / 255.0f, (color >> 16 & 0xFF) / 255.0f, (color >> 8 & 0xFF) / 255.0f,
                        (color & 0xFF) / 255.0f};
        return c;
    }

    // An icon's cell in its sheet (RGB5A3, 4x4 tiles of big-endian texels) and its texels as the font has them.
    struct Cell {
        u8* mSheet;
        s32 mSheetWidth;
        s32 mSheetHeight;
        s32 mX;
        s32 mY;
        s32 mWidth;
        s32 mHeight;
        u16 mOriginal[cCellMax * cCellMax];
    };
    Cell sCells[NEBULA_BUTTON_ICONS];
    enum { CellsUnread, CellsFound, CellsUnusable };
    s32 sCellState = CellsUnread;
    NebulaButtonIcons sShown;  // zero: the Wii's own, as the font has them
    Rgba sCanvas[cCellMax * cCellMax];

    u8* texelAt(const Cell& rCell, s32 x, s32 y) {
        const s32 sheetX = rCell.mX + x;
        const s32 sheetY = rCell.mY + y;
        return rCell.mSheet + (((sheetY / 4) * (rCell.mSheetWidth / 4) + sheetX / 4) * 16 + (sheetY % 4) * 4 + sheetX % 4) * 2;
    }

    // The picture font's icon cells, once it is loaded; a font whose cells are not RGB5A3 (a mod's) is left as it is.
    s32 findCells() {
        const nw4r::ut::Font* pFont = MR::getPictureFontNW4R();
        if (pFont == nullptr) {
            return CellsUnread;
        }
        for (s32 i = 0; i < NEBULA_BUTTON_ICONS; i++) {
            nw4r::ut::Glyph glyph;
            pFont->GetGlyph(&glyph, cCodes[i]);
            if (!pFont->HasGlyph(cCodes[i]) || glyph.pTexture == nullptr || glyph.texFormat != GX_TF_RGB5A3) {
                OSReport("NebulaIcons: the picture font lacks an icon or is not RGB5A3; the icons stay the Wii's\n");
                return CellsUnusable;
            }
            Cell& rCell = sCells[i];
            rCell.mSheet = static_cast< u8* >(glyph.pTexture);
            rCell.mSheetWidth = glyph.texWidth;
            rCell.mSheetHeight = glyph.texHeight;
            rCell.mX = glyph.cellX;
            rCell.mY = glyph.cellY;
            rCell.mWidth = glyph.widths.glyphWidth < cCellMax ? glyph.widths.glyphWidth : cCellMax;
            rCell.mHeight = glyph.height < cCellMax ? glyph.height : cCellMax;
            for (s32 y = 0; y < rCell.mHeight; y++) {
                for (s32 x = 0; x < rCell.mWidth; x++) {
                    const u8* pTexel = texelAt(rCell, x, y);
                    rCell.mOriginal[y * cCellMax + x] = static_cast< u16 >(pTexel[0] << 8 | pTexel[1]);
                }
            }
        }
        return CellsFound;
    }

    // Straight alpha over what is drawn, `cover` of a pixel.
    void blend(s32 x, s32 y, const Rgba& rColor, f32 cover) {
        cover *= rColor.a;
        if (cover <= 0.0f) {
            return;
        }
        Rgba& rDst = sCanvas[y * cCellMax + x];
        const f32 a = cover + rDst.a * (1.0f - cover);
        const f32 keep = rDst.a * (1.0f - cover);
        rDst.r = (rColor.r * cover + rDst.r * keep) / a;
        rDst.g = (rColor.g * cover + rDst.g * keep) / a;
        rDst.b = (rColor.b * cover + rDst.b * keep) / a;
        rDst.a = a;
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

    f32 coverOf(f32 distance) {
        return NebulaUi::clamp(0.5f - distance, 0.0f, 1.0f);
    }

    // `rShape` ringed `ring` wide in `rRing`, its face from `rTop` at its top to `rBottom` at its foot.
    void drawShape(const Shape& rShape, s32 width, s32 height, const Rgba& rRing, f32 ring, const Rgba& rTop, const Rgba& rBottom) {
        for (s32 y = 0; y < height; y++) {
            const f32 t = NebulaUi::clamp((y + 0.5f - (rShape.mY - rShape.mHalfHeight)) / (rShape.mHalfHeight * 2.0f), 0.0f, 1.0f);
            const Rgba face = {rTop.r + (rBottom.r - rTop.r) * t, rTop.g + (rBottom.g - rTop.g) * t, rTop.b + (rBottom.b - rTop.b) * t,
                               rTop.a + (rBottom.a - rTop.a) * t};
            for (s32 x = 0; x < width; x++) {
                const f32 d = rShape.distance(x + 0.5f, y + 0.5f);
                blend(x, y, rRing, coverOf(d));
                blend(x, y, face, coverOf(d + ring));
            }
        }
    }

    // A filled box (bars of a plus, lines of a menu), as drawShape without a ring.
    void drawBar(f32 x, f32 y, f32 halfWidth, f32 halfHeight, s32 width, s32 height, const Rgba& rColor) {
        const f32 radius = halfWidth < halfHeight ? halfWidth : halfHeight;
        const Shape bar = {x, y, halfWidth, halfHeight, radius};
        drawShape(bar, width, height, rColor, 0.0f, rColor, rColor);
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
    void drawPlayStationSymbol(s32 symbol, f32 x, f32 y, f32 diameter, s32 width, s32 height, const Rgba& rInk) {
        const f32 stroke = diameter * 0.075f;
        const f32 arm = diameter * 0.165f;
        const Shape square = {x, y, diameter * 0.15f, diameter * 0.15f, diameter * 0.02f};
        const f32 side = diameter * 0.4f;
        const f32 rise = side * 0.866f;
        const f32 apex = y - rise * 0.6f;
        const f32 base = apex + rise;
        for (s32 py = 0; py < height; py++) {
            for (s32 px = 0; px < width; px++) {
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
                blend(px, py, rInk, coverOf(d - stroke * 0.5f));
            }
        }
    }

    // A glyph's coverage 0-1 at (x, y) of its cell, nothing outside it.
    f32 sample(const nw4r::ut::Glyph& rGlyph, s32 x, s32 y) {
        if (x < 0 || y < 0 || x >= rGlyph.widths.glyphWidth || y >= rGlyph.height) {
            return 0.0f;
        }
        const s32 value = NebulaUi::glyphCoverage(rGlyph, rGlyph.cellX + x, rGlyph.cellY + y);
        return value < 0 ? 0.0f : value / 15.0f;
    }

    f32 sampleSmooth(const nw4r::ut::Glyph& rGlyph, f32 x, f32 y) {
        const s32 x0 = static_cast< s32 >(__builtin_floorf(x));
        const s32 y0 = static_cast< s32 >(__builtin_floorf(y));
        const f32 fx = x - x0;
        const f32 fy = y - y0;
        return (sample(rGlyph, x0, y0) * (1.0f - fx) + sample(rGlyph, x0 + 1, y0) * fx) * (1.0f - fy) +
               (sample(rGlyph, x0, y0 + 1) * (1.0f - fx) + sample(rGlyph, x0 + 1, y0 + 1) * fx) * fy;
    }

    // `pText` in the game's message font, its ink centred on (x, y), its capitals `capital` pixels tall.
    void drawText(const wchar_t* pText, f32 x, f32 y, f32 capital, s32 width, s32 height, const Rgba& rInk) {
        const nw4r::ut::Font* pFont = SingletonHolder< GameSystem >::get()->mFontHolder->mMessageFont;
        s32 aLeft, aTop, aRight, aBottom;
        if (pFont == nullptr || !NebulaUi::glyphInkBox(pFont, L'A', &aLeft, &aTop, &aRight, &aBottom)) {
            return;
        }
        const f32 scale = capital / (aBottom - aTop + 1);
        f32 left, top, right, bottom, cursor;
        if (!NebulaUi::stringInkBox(pFont, pText, 0.0f, false, &left, &top, &right, &bottom, &cursor)) {
            return;
        }
        const f32 originX = x - (left + right) * 0.5f * scale;
        const f32 originY = y - (top + bottom) * 0.5f * scale;
        // Each pixel the mean of 3 x 3 smooth samples: the font's letters shrink a third or more.
        cursor = 0.0f;
        for (const wchar_t* p = pText; *p != 0; p++) {
            nw4r::ut::Glyph glyph;
            pFont->GetGlyph(&glyph, static_cast< u16 >(*p));
            const nw4r::ut::CharWidths widths = pFont->GetCharWidths(static_cast< u16 >(*p));
            const f32 glyphX = originX + (cursor + widths.left) * scale;
            cursor += widths.charWidth;
            if (glyph.pTexture == nullptr) {
                continue;
            }
            // Only the pixels the glyph's cell covers.
            const s32 x0 = glyphX < 0.0f ? 0 : static_cast< s32 >(glyphX);
            const s32 y0 = originY < 0.0f ? 0 : static_cast< s32 >(originY);
            const s32 x1 = static_cast< s32 >(glyphX + glyph.widths.glyphWidth * scale) + 1;
            const s32 y1 = static_cast< s32 >(originY + glyph.height * scale) + 1;
            for (s32 py = y0; py < y1 && py < height; py++) {
                for (s32 px = x0; px < x1 && px < width; px++) {
                    f32 cover = 0.0f;
                    for (s32 sy = 0; sy < 3; sy++) {
                        for (s32 sx = 0; sx < 3; sx++) {
                            const f32 fx = (px + (sx + 0.5f) / 3.0f - glyphX) / scale - 0.5f;
                            const f32 fy = (py + (sy + 0.5f) / 3.0f - originY) / scale - 0.5f;
                            cover += sampleSmooth(glyph, fx, fy);
                        }
                    }
                    blend(px, py, rInk, cover / 9.0f);
                }
            }
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

    // The icon of `glyph` at `place` in `style`, `width` x `height`, on the canvas; false when the Wii's own shows.
    bool paint(s32 style, s32 glyph, s32 place, s32 width, s32 height, s32 look) {
        static const wchar_t* const cNintendoFaces[4] = {L"B", L"A", L"Y", L"X"};
        static const wchar_t* const cXboxLetters[4] = {L"A", L"B", L"X", L"Y"};
        static const wchar_t* const cShoulders[3][2] = {{L"LB", L"RB"}, {L"L", L"R"}, {L"L1", L"R1"}};
        static const wchar_t* const cTriggers[3][2] = {{L"LT", L"RT"}, {L"ZL", L"ZR"}, {L"L2", L"R2"}};
        static const wchar_t* const cSticks[3][2] = {{L"LS", L"RS"}, {L"L", L"R"}, {L"L3", L"R3"}};
        const s32 family = style == NEBULA_ICON_STYLE_XBOX ? 0 : style == NEBULA_ICON_STYLE_NINTENDO ? 1 : 2;
        const bool xbox = family == 0;
        const f32 side = static_cast< f32 >(width < height ? width : height);
        const f32 diameter = look == LookUp ? side * (39.0f / 48.0f) : look == LookPressed ? side * (36.0f / 40.0f) : side - 2.0f;
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
        for (s32 i = 0; i < cCellMax * cCellMax; i++) {
            sCanvas[i].r = sCanvas[i].g = sCanvas[i].b = sCanvas[i].a = 0.0f;
        }
        if (look == LookUp) {
            // Its shadow, soft-edged, down and right.
            const Shape shadow = {cx + diameter * 0.07f, cy + diameter * 0.07f, diameter * 0.5f, diameter * 0.5f, diameter * 0.5f};
            const Rgba dark = {0.0f, 0.0f, 0.0f, 0.4f};
            for (s32 y = 0; y < height; y++) {
                for (s32 x = 0; x < width; x++) {
                    blend(x, y, dark, NebulaUi::clamp((1.0f - shadow.distance(x + 0.5f, y + 0.5f)) * 0.5f, 0.0f, 1.0f));
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
                drawShape(circle, width, height, ringColor, ring, pressed ? darkened(fill) : top, pressed ? darkened(top) : fill);
                drawText(cXboxLetters[p], cx, cy, diameter * 0.46f, width, height, rgba(p == 3 ? 0x3A3A3AFF : cWhite));
            } else {
                drawShape(circle, width, height, ringColor, ring, faceTop, faceBottom);
                if (family == 1) {
                    drawText(cNintendoFaces[p], cx, cy, diameter * 0.5f, width, height, ink);
                } else {
                    drawPlayStationSymbol(p, cx, cy, diameter, width, height, ink);
                }
            }
            return true;
        }
        case NEBULA_ICON_SHOULDER:
            drawShape(pill, width, height, ringColor, ring, faceTop, faceBottom);
            drawText(cShoulders[family][place & 1], cx, cy, diameter * 0.3f, width, height, ink);
            return true;
        case NEBULA_ICON_TRIGGER:
            drawShape(box, width, height, ringColor, ring, faceTop, faceBottom);
            drawText(cTriggers[family][place & 1], cx, cy, diameter * 0.3f, width, height, ink);
            return true;
        case NEBULA_ICON_STICK: {
            // The stick's cap and its rim.
            drawShape(circle, width, height, ringColor, ring, faceTop, faceBottom);
            const Shape rim = {cx, cy, diameter * 0.36f, diameter * 0.36f, diameter * 0.36f};
            drawShape(rim, width, height, rgba(xbox ? 0x6A6E78FF : 0xB8C0D0FF), ring * 0.7f, faceTop, faceBottom);
            drawText(cSticks[family][place & 1], cx, cy, diameter * (family == 1 ? 0.34f : 0.26f), width, height, ink);
            return true;
        }
        case NEBULA_ICON_START:
            if (family == 1) {
                return false;  // Plus: the Wii's own
            }
            drawShape(family == 2 ? pill : circle, width, height, ringColor, ring, faceTop, faceBottom);
            // Three lines (Menu, Options).
            for (s32 line = -1; line <= 1; line++) {
                drawBar(cx, cy + line * diameter * 0.15f, diameter * 0.2f, diameter * 0.035f, width, height, ink);
            }
            return true;
        case NEBULA_ICON_BACK:
            if (family == 1) {
                return false;  // Minus: the Wii's own
            }
            drawShape(family == 2 ? pill : circle, width, height, ringColor, ring, faceTop, faceBottom);
            if (xbox) {
                // View: two windows, one over the other.
                const Shape back = {cx - diameter * 0.05f, cy - diameter * 0.05f, diameter * 0.15f, diameter * 0.12f, diameter * 0.03f};
                const Shape front = {cx + diameter * 0.06f, cy + diameter * 0.06f, diameter * 0.15f, diameter * 0.12f, diameter * 0.03f};
                drawShape(back, width, height, ink, diameter * 0.05f, faceBottom, faceBottom);
                drawShape(front, width, height, ink, diameter * 0.05f, faceTop, faceBottom);
            } else {
                // Share and Create: three short rays.
                for (s32 ray = -1; ray <= 1; ray++) {
                    drawBar(cx + ray * diameter * 0.14f, cy, diameter * 0.035f, diameter * 0.12f, width, height, ink);
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
        return static_cast< u16 >(static_cast< u32 >(a * 7.0f + 0.5f) << 12 | static_cast< u32 >(r * 15.0f + 0.5f) << 8 |
                                  static_cast< u32 >(g * 15.0f + 0.5f) << 4 | static_cast< u32 >(b * 15.0f + 0.5f));
    }

    // The canvas's `width` x `height` into a picture `pictureWidth` wide (RGB5A3, 4x4 tiles of big-endian texels) at
    // (x, y).
    void encodeInto(u8* pPicture, s32 pictureWidth, s32 x, s32 y, s32 width, s32 height) {
        for (s32 py = 0; py < height; py++) {
            for (s32 px = 0; px < width; px++) {
                const s32 tx = x + px;
                const s32 ty = y + py;
                u8* pTexel = pPicture + (((ty / 4) * (pictureWidth / 4) + tx / 4) * 16 + (ty % 4) * 4 + tx % 4) * 2;
                const u16 texel = encode(sCanvas[py * cCellMax + px]);
                pTexel[0] = static_cast< u8 >(texel >> 8);
                pTexel[1] = static_cast< u8 >(texel);
            }
        }
    }

    // The A button over people and signs: IconAButton's two pictures (the button up, 48 pixels square, and pressed, 40;
    // IA4) give way to ours, drawn as the font's A is, while the icons are not the Wii's.
    struct Picture {
        alignas(32) u8 mTexels[cCellMax * cCellMax * 2];
    };
    const s32 cPictureSizes[2] = {48, 40};
    const s32 cPictureLooks[2] = {LookUp, LookPressed};
    Picture sPictures[2];
    bool sPicturesShown = false;
    // Counts the icons' changes (each IconAButton remembers the one it shows).
    u32 sGeneration = 1;

    // Each map given one of our pictures and the console's picture it had, put back when the icons are the Wii's again.
    // Maps of layouts gone (a scene's end frees them unseen) stay until the least recently used gives way; a map whose
    // entry gave way takes back the last original seen at its size.
    struct Swap {
        nw4r::lyt::TexMap* mMap;
        void* mOriginal;
        u32 mUsed;
    };
    const s32 cSwapsMax = 256;
    Swap sSwaps[cSwapsMax];
    s32 sSwapCount = 0;
    u32 sSwapClock = 0;
    void* sLastOriginal[2] = {nullptr, nullptr};

    // Icon `index`'s font cell scaled into a `size`-pixel picture (an icon the style borrows from the Wii's).
    void paintFromCell(const Cell& rSource, s32 size) {
        for (s32 i = 0; i < cCellMax * cCellMax; i++) {
            sCanvas[i].r = sCanvas[i].g = sCanvas[i].b = sCanvas[i].a = 0.0f;
        }
        const f32 scale = static_cast< f32 >(rSource.mWidth > rSource.mHeight ? rSource.mWidth : rSource.mHeight) / size;
        for (s32 y = 0; y < size; y++) {
            for (s32 x = 0; x < size; x++) {
                const s32 sx = static_cast< s32 >((x + 0.5f) * scale - (size * scale - rSource.mWidth) * 0.5f);
                const s32 sy = static_cast< s32 >((y + 0.5f) * scale - (size * scale - rSource.mHeight) * 0.5f);
                if (sx >= 0 && sy >= 0 && sx < rSource.mWidth && sy < rSource.mHeight) {
                    sCanvas[y * cCellMax + x] = decodeTexel(rSource.mOriginal[sy * cCellMax + sx]);
                }
            }
        }
    }

    void applyToPane(nw4r::lyt::Pane* pPane, s32 depth) {
        nw4r::lyt::Material* pMaterial = pPane->GetMaterial();
        for (u8 i = 0; pMaterial != nullptr && i < pMaterial->GetTextureNum(); i++) {
            nw4r::lyt::TexMap& rMap = pMaterial->GetTexMapAry()[i];
            for (s32 k = 0; k < 2; k++) {
                Picture& rPicture = sPictures[k];
                const bool ours = rMap.mImage == rPicture.mTexels;
                if (rMap.mWidth != cPictureSizes[k] || rMap.mHeight != cPictureSizes[k] || (!ours && rMap.GetTexelFormat() != GX_TF_IA4)) {
                    continue;
                }
                s32 swap = 0;
                while (swap < sSwapCount && sSwaps[swap].mMap != &rMap) {
                    swap++;
                }
                if (sPicturesShown && !ours) {
                    if (swap == sSwapCount && sSwapCount == cSwapsMax) {
                        swap = 0;
                        for (s32 j = 1; j < sSwapCount; j++) {
                            swap = sSwaps[j].mUsed < sSwaps[swap].mUsed ? j : swap;
                        }
                    } else if (swap == sSwapCount) {
                        sSwapCount++;
                    }
                    sSwaps[swap].mMap = &rMap;
                    sSwaps[swap].mOriginal = rMap.mImage;
                    sSwaps[swap].mUsed = ++sSwapClock;
                    sLastOriginal[k] = rMap.mImage;
                    rMap.SetImage(rPicture.mTexels);
                    rMap.SetTexelFormat(GX_TF_RGB5A3);
                } else if (!sPicturesShown && ours && (swap < sSwapCount || sLastOriginal[k] != nullptr)) {
                    rMap.SetImage(swap < sSwapCount ? sSwaps[swap].mOriginal : sLastOriginal[k]);
                    rMap.SetTexelFormat(GX_TF_IA4);
                    if (swap < sSwapCount) {
                        sSwaps[swap] = sSwaps[--sSwapCount];
                    }
                } else if (ours && swap < sSwapCount) {
                    sSwaps[swap].mUsed = ++sSwapClock;
                }
            }
        }
        if (depth > 16) {
            return;
        }
        for (nw4r::lyt::PaneList::Iterator it = pPane->GetChildList().GetBeginIter(); it != pPane->GetChildList().GetEndIter(); ++it) {
            applyToPane(&*it, depth + 1);
        }
    }

    // Icon `index` as `style` has it: drawn, or the Wii's own (its own icon, or Plus's, Minus's or HOME's in their
    // place, which a Nintendo controller's look like).
    void redraw(s32 index, s32 style, s32 glyph, s32 place) {
        const Cell& rCell = sCells[index];
        s32 source = index;
        bool painted = false;
        if (style != NEBULA_ICON_STYLE_WII && glyph != NEBULA_ICON_KEEP) {
            painted = paint(style, glyph, place, rCell.mWidth, rCell.mHeight, LookText);
            if (!painted) {
                source = glyph == NEBULA_ICON_START ? IconPlus : glyph == NEBULA_ICON_BACK ? IconMinus : glyph == NEBULA_ICON_HOME ? IconHome
                                                    : glyph == NEBULA_ICON_DPAD ? IconPad : index;
            }
        }
        const Cell& rSource = sCells[source];
        if (painted) {
            encodeInto(rCell.mSheet, rCell.mSheetWidth, rCell.mX, rCell.mY, rCell.mWidth, rCell.mHeight);
        } else {
            // Centred where the cells differ in size.
            const s32 dx = (rSource.mWidth - rCell.mWidth) / 2;
            const s32 dy = (rSource.mHeight - rCell.mHeight) / 2;
            for (s32 y = 0; y < rCell.mHeight; y++) {
                for (s32 x = 0; x < rCell.mWidth; x++) {
                    const s32 sx = x + dx;
                    const s32 sy = y + dy;
                    const u16 texel =
                        sx >= 0 && sy >= 0 && sx < rSource.mWidth && sy < rSource.mHeight ? rSource.mOriginal[sy * cCellMax + sx] : 0;
                    u8* pTexel = texelAt(rCell, x, y);
                    pTexel[0] = static_cast< u8 >(texel >> 8);
                    pTexel[1] = static_cast< u8 >(texel);
                }
            }
        }
    }
};  // namespace

namespace NebulaIcons {
    void update() {
        if (sCellState == CellsUnread) {
            sCellState = findCells();
        }
        NebulaButtonIcons now;
        memset(&now, 0, sizeof(now));
        __NebulaButtonIcons(&now);
        // The Wii Remote's pictures follow the remote even when the icons cannot be drawn.
        sShown.wii_remote = now.wii_remote;
        if (sCellState != CellsFound || memcmp(&now, &sShown, sizeof(now)) == 0) {
            return;
        }
        u8* pStored[NEBULA_BUTTON_ICONS];
        s32 storedCount = 0;
        for (s32 i = 0; i < NEBULA_BUTTON_ICONS; i++) {
            if (now.style != sShown.style || now.glyphs[i] != sShown.glyphs[i] || now.places[i] != sShown.places[i]) {
                redraw(i, now.style, now.glyphs[i], now.places[i]);
                bool stored = false;
                for (s32 k = 0; k < storedCount; k++) {
                    stored = stored || pStored[k] == sCells[i].mSheet;
                }
                if (!stored) {
                    pStored[storedCount++] = sCells[i].mSheet;
                    DCStoreRange(sCells[i].mSheet, static_cast< u32 >(sCells[i].mSheetWidth * sCells[i].mSheetHeight * 2));
                }
            }
        }
        sShown = now;
        sGeneration++;
        // The A bubble's two frames, as the font's A (an icon borrowed from the Wii's, as the font shows it).
        sPicturesShown = now.style != NEBULA_ICON_STYLE_WII && now.glyphs[0] != NEBULA_ICON_KEEP;
        for (s32 k = 0; k < 2 && sPicturesShown; k++) {
            if (!paint(now.style, now.glyphs[0], now.places[0], cPictureSizes[k], cPictureSizes[k], cPictureLooks[k])) {
                const s32 glyph = now.glyphs[0];
                paintFromCell(sCells[glyph == NEBULA_ICON_START ? IconPlus : glyph == NEBULA_ICON_BACK ? IconMinus
                                     : glyph == NEBULA_ICON_HOME ? IconHome : IconPad],
                              cPictureSizes[k]);
            }
            encodeInto(sPictures[k].mTexels, cPictureSizes[k], 0, 0, cPictureSizes[k], cPictureSizes[k]);
            DCStoreRange(sPictures[k].mTexels, sizeof(sPictures[k].mTexels));
        }
    }

    bool wiiRemoteInUse() {
        return sShown.wii_remote != 0;
    }

    void applyToLayout(LayoutActor* pActor, u32* pShown) {
        if (*pShown == sGeneration) {
            return;
        }
        *pShown = sGeneration;
        // Nothing to do while the Wii's own show and none of ours is in place.
        if (sCellState != CellsFound || (!sPicturesShown && sSwapCount == 0 && sLastOriginal[0] == nullptr &&
                                         sLastOriginal[1] == nullptr)) {
            return;
        }
        LayoutManager* pManager = pActor != nullptr ? pActor->getLayoutManager() : nullptr;
        if (pManager != nullptr && pManager->mLayout != nullptr && pManager->mLayout->mpRootPane != nullptr) {
            applyToPane(pManager->mLayout->mpRootPane, 0);
        }
    }
};  // namespace NebulaIcons
#endif
