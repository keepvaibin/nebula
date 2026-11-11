#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include <nebula_cooked_texture.h>
#include "Game/Render/FrameList.hpp"

#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Screen/NebulaPaneKit.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/TextString.hpp"
#include <cstdio>
#include <Game/Ui/Layout.hpp>
#include <Game/Ui/Material.hpp>
#include <Game/Ui/Font.hpp>
#include <Game/Ui/Rect.hpp>
#include "Game/System/NebulaHost.hpp"

namespace NebulaUi {
    Color8 color(u32 rgba) {
        return Color8(rgba);
    }

    void setTextColor(TextBox* pText, u32 top, u32 bottom) {
        pText->SetTextColor(0, color(top));
        pText->SetTextColor(1, color(bottom));
    }

    NebulaColor16 tevColor(u32 rgba, bool alpha) {
        NebulaColor16 c;
        c.r = static_cast< s16 >((rgba >> 24) & 0xFF);
        c.g = static_cast< s16 >((rgba >> 16) & 0xFF);
        c.b = static_cast< s16 >((rgba >> 8) & 0xFF);
        c.a = alpha ? static_cast< s16 >(rgba & 0xFF) : 0;
        return c;
    }

    // A window's fill and ring: its content and frame mix colour 0 (the ring) to colour 1 (the fill) by their texture.
    void paint(Window* pWindow, u32 fill, u32 ring) {
        if (pWindow == nullptr) {
            return;
        }
        if (Ui::Material* pContent = pWindow->GetContentMaterial()) {
            pContent->SetStageColor(0, tevColor(fill, false));
            pContent->SetStageColor(1, tevColor(fill, true));
        }
        for (u32 i = 0; i < pWindow->mFrameNum; i++) {
            if (Ui::Material* pFrame = pWindow->GetFrameMaterial(i)) {
                pFrame->SetStageColor(0, tevColor(ring, false));
                pFrame->SetStageColor(1, tevColor(fill, true));
            }
        }
    }

    // A rounded box `w` x `h` centred on (x, y) with corners of `radius`.
    void shape(Pane* pPane, f32 x, f32 y, f32 w, f32 h, f32 radius) {
        if (pPane == nullptr) {
            return;
        }
        f32 k = radius / cCorner;
        const f32 most = (w < h ? w : h) / (2.0f * cCorner);
        k = k > most ? most : k;
        k = k < 0.05f ? 0.05f : k;
        pPane->mScale = Vec2f(k, k);
        pPane->mSize = Ui::Size(w / k, h / k);
        pPane->mTranslate = Vec3f(x, y, 0.0f);
    }

    void setText(TextBox* pText, const char16_t* pString) {
        if (pText != nullptr) {
            LayoutCoreUtil::setTextBoxMessage(pText, pString);
        }
    }

    void setTextSize(TextBox* pText, f32 size) {
        pText->SetFontSize(Ui::Size(size * cFontAspect, size));
        pText->mCharSpace = size * cCharSpace;
        pText->mLineSpace = size * cLineSpace;
    }

    // `anchor`: the pane's base point and the text's place in it, 0-8 row by row from the top left (4 the centre).
    void setAnchor(TextBox* pText, s32 anchor) {
        pText->mBasePosition = static_cast< u8 >(anchor);
        pText->SetTextPositionH(static_cast< u8 >(anchor % 3));
        pText->SetTextPositionV(static_cast< u8 >(anchor / 3));
    }

    void setTextStyle(TextBox* pText, f32 size, u32 top, u32 bottom, s32 anchor) {
        if (pText == nullptr) {
            return;
        }
        setTextSize(pText, size);
        setTextColor(pText, top, bottom);
        setAnchor(pText, anchor);
    }

    // The text's drawn size when it may run `limit` wide (the writer breaks lines there); the box takes that size.
    void measure(TextBox* pText, f32 limit, f32* pWidth, f32* pHeight) {
        *pWidth = 0.0f;
        *pHeight = 0.0f;
        if (pText == nullptr) {
            return;
        }
        pText->mSize = Ui::Size(limit, 1000.0f);
        const Ui::Rect rect = pText->GetTextDrawRect();
        *pWidth = rect.right - rect.left;
        *pHeight = rect.bottom - rect.top;
        pText->mSize = Ui::Size(*pWidth + 2.0f, *pHeight + 2.0f);
    }

    // A box centred on `y` whose capitals look centred there.
    f32 textCentre(f32 y, f32 size) {
        return y - cCapLift * size;
    }

    // A glyph box centred on `y` whose picture looks centred there.
    f32 glyphCentre(f32 y, f32 size) {
        return y + cGlyphDrop * size;
    }

    // The lines a measured box holds at `size`.
    s32 countLines(f32 height, f32 size) {
        const s32 lines = static_cast< s32 >((height - size) / (size * (1.0f + cLineSpace)) + 1.5f);
        return lines < 1 ? 1 : lines;
    }

    // `pSource` broken into lines at its spaces so that none runs wider than `limit`: the text writer alone breaks
    // anywhere, mid-word. A word wider than a line, and text written without spaces (Japanese, Chinese), keep the
    // writer's breaks.
    // A unit of text: one character, or a whole tag (0x1A, then its size in bytes in the next unit's low byte; its
    // parameters may be zero).
    s32 unitLength(const char16_t* p) {
        if (*p == 0x1A && p[1] != 0) {
            const s32 units = (p[1] & 0xFF) / 2;
            return units > 0 ? units : 1;
        }
        return 1;
    }

    void wrapWords(TextBox* pText, const char16_t* pSource, f32 limit, f32* pWidth, f32* pHeight) {
        // The result holds the source's units, one break in place of each run of spaces; a line holds a word more.
        const s32 sourceLength = static_cast< s32 >(MR::textLength(pSource));
        const u32 room = static_cast< u32 >(sourceLength) + 3;
        MR::Text16 outText;
        MR::Text16 lineText;
        char16_t* const out = outText.room(room);
        char16_t* const line = lineText.room(room);
        s32 length = 0;
        s32 lineStart = 0;
        s32 i = 0;
        while (pSource[i] != 0) {
            s32 end = i;
            while (pSource[end] != 0 && pSource[end] != u' ') {
                end += unitLength(&pSource[end]);
            }
            // A tag longer than the text left (a mod's text) ends at the text's end.
            if (end > sourceLength) {
                end = sourceLength;
            }
            // The line so far with the word added.
            s32 n = 0;
            for (s32 k = lineStart; k < length; k++) {
                line[n++] = out[k];
            }
            if (length > lineStart) {
                line[n++] = u' ';
            }
            for (s32 k = i; k < end; k++) {
                line[n++] = pSource[k];
            }
            line[n] = 0;
            setText(pText, line);
            f32 width, height;
            measure(pText, 4000.0f, &width, &height);
            if (width > limit && length > lineStart) {
                out[length++] = u'\n';
                lineStart = length;
            } else if (length > lineStart) {
                out[length++] = u' ';
            }
            for (s32 k = i; k < end; k++) {
                out[length++] = pSource[k];
            }
            i = end;
            while (pSource[i] == u' ') {
                i++;
            }
        }
        out[length] = 0;
        setText(pText, out);
        measure(pText, limit, pWidth, pHeight);
    }

    // A box's font size for picture-font glyphs `size` high: the writer scales them by the box's own font (the message
    // font, 27 wide for 33 high), so a square size would draw them a fifth too wide.
    Ui::Size glyphFontSize(const TextBox* pText, f32 size) {
        const Ui::Font* pFont = pText->mpFont;
        const f32 aspect = pFont != nullptr && pFont->GetHeight() > 0 ? static_cast< f32 >(pFont->GetWidth()) / pFont->GetHeight() : 1.0f;
        return Ui::Size(size * aspect, size);
    }

    // The game's picture font: a glyph sized like text, centred on its place.
    void setGlyph(TextBox* pText, s32 code, f32 size) {
        if (pText == nullptr) {
            return;
        }
        char16_t glyph[8] = {0};  // one picture font tag (3 units) and the terminator
        MR::addPictureFontTag(glyph, code);
        setText(pText, glyph);
        pText->SetFontSize(glyphFontSize(pText, size));
        pText->mCharSpace = 0.0f;
        setAnchor(pText, 4);
        pText->mSize = Ui::Size(size * 2.0f, size * 2.0f);
    }

    // A sheet texel's ink, 0-255, at (x, y) in the record's own texels: its alpha, an L8 sheet's intensity (what the
    // font's shader takes as coverage, font_text.hlsl); -1 for a layout it cannot read.
    static s32 sheetInk(const NebulaTexture* pRecord, u32 x, u32 y) {
        const u8* pTexels = reinterpret_cast< const u8* >(pRecord) + NEBULA_TEXTURE_HEADER;
        const u32 at = y * pRecord->width + x;
        switch (pRecord->layout) {
        case NEBULA_TEXTURE_L8:
            return pTexels[at];
        case NEBULA_TEXTURE_LA8:
            return pTexels[at * 2 + 1];
        case NEBULA_TEXTURE_RGBA8:
            return pTexels[at * 4 + 3];
        default:
            return -1;
        }
    }

    // The ink box of `code`'s picture as the font's shader draws it, in its cell's pixels (x from the cell's left, y from
    // its top, right and bottom the far edges); false when the picture cannot be read or holds no ink. A distance sheet
    // (Font::GetFieldRange) is larger than the glyph's sheet size, its texels finer; its edge is where the field crosses
    // one half between two texels' centres, as the GPU samples it. A picture's ink is a texel at a quarter or more.
    bool glyphInkBox(const Ui::Font* pFont, u16 code, f32* pLeft, f32* pTop, f32* pRight, f32* pBottom) {
        Ui::Glyph glyph;
        pFont->GetGlyph(&glyph, code);
        const u8* pData = static_cast< const u8* >(glyph.pTexture);
        const NebulaTexture* pRecord = pData != nullptr ? nebula_texture_record(pData) : nullptr;
        if (pRecord == nullptr || glyph.texWidth == 0 || glyph.texHeight == 0 || glyph.widths.glyphWidth <= 0 || glyph.height == 0) {
            return false;
        }
        const f32 scaleX = static_cast< f32 >(pRecord->width) / static_cast< f32 >(glyph.texWidth);
        const f32 scaleY = static_cast< f32 >(pRecord->height) / static_cast< f32 >(glyph.texHeight);
        const s32 cellRight = glyph.cellX + glyph.widths.glyphWidth;
        const s32 cellBottom = glyph.cellY + glyph.height;
        const s32 x0 = static_cast< s32 >(glyph.cellX * scaleX + 0.5f);
        const s32 y0 = static_cast< s32 >(glyph.cellY * scaleY + 0.5f);
        s32 x1 = static_cast< s32 >(cellRight * scaleX + 0.5f);
        s32 y1 = static_cast< s32 >(cellBottom * scaleY + 0.5f);
        x1 = x1 > static_cast< s32 >(pRecord->width) ? static_cast< s32 >(pRecord->width) : x1;
        y1 = y1 > static_cast< s32 >(pRecord->height) ? static_cast< s32 >(pRecord->height) : y1;
        if (x1 <= x0 || y1 <= y0) {
            return false;
        }
        const bool field = pFont->GetFieldRange() != 0;
        const s32 edge = field ? 128 : 64;
        // Outside the cell nothing is drawn.
        const auto inkAt = [&](s32 x, s32 y) -> s32 {
            return x < x0 || x >= x1 || y < y0 || y >= y1 ? 0 : sheetInk(pRecord, static_cast< u32 >(x), static_cast< u32 >(y));
        };
        // From an outside texel's centre (ink `a`) toward an inside one's (ink `b`), a texel apart: where the edge is.
        const auto cross = [&](s32 a, s32 b) -> f32 {
            return field && b > a ? static_cast< f32 >(edge - a) / static_cast< f32 >(b - a) : 0.5f;
        };
        f32 left = 1e9f;
        f32 right = -1e9f;
        f32 top = 1e9f;
        f32 bottom = -1e9f;
        for (s32 y = y0; y < y1; y++) {
            for (s32 x = x0; x < x1; x++) {
                const s32 ink = inkAt(x, y);
                if (ink < 0) {
                    return false;
                }
                if (ink < edge) {
                    continue;
                }
                const f32 cx = static_cast< f32 >(x) + 0.5f;
                const f32 cy = static_cast< f32 >(y) + 0.5f;
                s32 next = inkAt(x - 1, y);
                if (next < edge) {
                    const f32 at = cx - 1.0f + cross(next, ink);
                    left = at < left ? at : left;
                }
                next = inkAt(x + 1, y);
                if (next < edge) {
                    const f32 at = cx + 1.0f - cross(next, ink);
                    right = at > right ? at : right;
                }
                next = inkAt(x, y - 1);
                if (next < edge) {
                    const f32 at = cy - 1.0f + cross(next, ink);
                    top = at < top ? at : top;
                }
                next = inkAt(x, y + 1);
                if (next < edge) {
                    const f32 at = cy + 1.0f - cross(next, ink);
                    bottom = at > bottom ? at : bottom;
                }
            }
        }
        if (right <= left || bottom <= top) {
            return false;
        }
        // Within the drawn cell, in the glyph's pixels.
        left = left < static_cast< f32 >(x0) ? static_cast< f32 >(x0) : left;
        top = top < static_cast< f32 >(y0) ? static_cast< f32 >(y0) : top;
        right = right > static_cast< f32 >(x1) ? static_cast< f32 >(x1) : right;
        bottom = bottom > static_cast< f32 >(y1) ? static_cast< f32 >(y1) : bottom;
        *pLeft = left / scaleX - static_cast< f32 >(glyph.cellX);
        *pRight = right / scaleX - static_cast< f32 >(glyph.cellX);
        *pTop = top / scaleY - static_cast< f32 >(glyph.cellY);
        *pBottom = bottom / scaleY - static_cast< f32 >(glyph.cellY);
        return true;
    }

    bool inkOffset(const TextBox* pText, f32* pX, f32* pY, f32* pWidth, f32* pHeight) {
        const Ui::Font* pFont = pText->mpFont;
        const char16_t* pString = pText->mTextBuf;
        if (pFont == nullptr || pString == nullptr || pString[0] == 0 || pFont->GetWidth() <= 0 || pFont->GetHeight() <= 0) {
            return false;
        }
        // The text's glyphs side by side as the writer sets them (its space between them), their ink boxes joined.
        const f32 scaleX = pText->mFontSize.width / pFont->GetWidth();
        const f32 scaleY = pText->mFontSize.height / pFont->GetHeight();
        f32 left, top, right, bottom, cursor;
        if (!stringInkBox(pFont, pString, pText->mCharSpace / scaleX, true, &left, &top, &right, &bottom, &cursor)) {
            return false;
        }
        f32 aLeft, aTop, aRight, aBottom;
        if (!glyphInkBox(pFont, u'A', &aLeft, &aTop, &aRight, &aBottom)) {
            return false;
        }
        *pX = ((left + right) * 0.5f - cursor * 0.5f) * scaleX;
        if (pWidth != nullptr && pHeight != nullptr) {
            *pWidth = (right - left) * scaleX;
            *pHeight = (bottom - top) * scaleY;
        }
        // The cell's y runs down, the layout's up.
        *pY = -((top + bottom) * 0.5f - (aTop + aBottom) * 0.5f) * scaleY;
        return true;
    }

    bool stringInkBox(const Ui::Font* pFont, const char16_t* pString, f32 space, bool strict, f32* pLeft, f32* pTop,
                      f32* pRight, f32* pBottom, f32* pEnd) {
        f32 cursor = 0.0f;
        f32 left = 1e9f;
        f32 right = -1e9f;
        f32 top = 1e9f;
        f32 bottom = -1e9f;
        for (const char16_t* p = pString; *p != 0; p++) {
            const Ui::CharWidths widths = pFont->GetCharWidths(static_cast< u16 >(*p));
            if (p != pString) {
                cursor += space;
            }
            f32 l, t, r, b;
            if (glyphInkBox(pFont, static_cast< u16 >(*p), &l, &t, &r, &b)) {
                left = cursor + widths.left + l < left ? cursor + widths.left + l : left;
                right = cursor + widths.left + r > right ? cursor + widths.left + r : right;
                top = t < top ? t : top;
                bottom = b > bottom ? b : bottom;
            } else if (strict) {
                return false;
            }
            cursor += widths.charWidth;
        }
        *pLeft = left;
        *pTop = top;
        *pRight = right;
        *pBottom = bottom;
        *pEnd = cursor;
        return right >= left;
    }

    f32 clamp(f32 v, f32 lo, f32 hi) {
        return v < lo ? lo : v > hi ? hi : v;
    }

    f32 easeOut(f32 t) {
        const f32 u = 1.0f - clamp(t, 0.0f, 1.0f);
        return 1.0f - u * u * u;
    }

    u8 toAlpha(f32 a) {
        return static_cast< u8 >(clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
    }

    char16_t* append(char16_t* pDst, char16_t* pEnd, const char16_t* pSrc) {
        while (pSrc != nullptr && *pSrc != 0 && pDst + 1 < pEnd) {
            *pDst++ = *pSrc++;
        }
        *pDst = 0;
        return pDst;
    }

    // The room is asked again at twice the size while the text fills it (NebulaText cuts at the room it is given).
    s32 getText(MR::Text16& rText, const char* pId) {
        u32 units = rText.capacity();
        for (;;) {
            const s32 length = NebulaText::get(rText.room(units), static_cast< s32 >(units), pId);
            if (length < static_cast< s32 >(units) - 1) {
                return length;
            }
            units *= 2;
        }
    }

    s32 getText(MR::GrowBuffer< char16_t >& rText, const char* pId) {
        u32 units = rText.mRoom > 256 ? rText.mRoom : 256;
        for (;;) {
            const s32 length = NebulaText::get(rText.room(units), static_cast< s32 >(units), pId);
            if (length < static_cast< s32 >(units) - 1) {
                return length;
            }
            units *= 2;
        }
    }

    Window* makeWindow(const Kits& rKits, Pane* pParent, const char* pName) {
        Window* pWindow = rKits.mWindow->makeWindow("WinInfo", pName);
        if (pWindow == nullptr) {
            pWindow = rKits.mText->makeWindow("InfoWinC", pName);
        }
        if (pWindow != nullptr) {
            pWindow->mBasePosition = 4;
            // The fill inside the frame only: the authored overlap shows as a darker band on a see-through window.
            pWindow->mContentInflation.l = 0.0f;
            pWindow->mContentInflation.r = 0.0f;
            pWindow->mContentInflation.t = 0.0f;
            pWindow->mContentInflation.b = 0.0f;
            pParent->AppendChild(pWindow);
        }
        return pWindow;
    }

    TextBox* makeText(const Kits& rKits, Pane* pParent, const char* pName, u32 length) {
        TextBox* pText = rKits.mText->makeText("InfConfirmC", pName, length);
        if (pText != nullptr) {
            pParent->AppendChild(pText);
        }
        return pText;
    }

    Pane* makeGroup(Pane* pParent, const char* pName) {
        Pane* pPane = pParent->GetLayout()->NewPane< Pane >();
        pPane->SetName(pName);
        pPane->SetInfluencedAlpha(true);
        pParent->AppendChild(pPane);
        return pPane;
    }

    Picture* makePicture(Pane* pParent, const char* pName, Ui::TexMap* pTexture) {
        if (pTexture == nullptr) {
            return nullptr;
        }
        Picture* pPicture = pParent->GetLayout()->NewPane< Picture >(*pTexture);
        pPicture->SetName(pName);
        pPicture->SetInfluencedAlpha(true);
        for (u32 i = 0; i < 4; i++) {
            pPicture->SetVtxColor(i, color(0xFFFFFFFF));
        }
        pParent->AppendChild(pPicture);
        return pPicture;
    }

    // One piece of the game's button art, under `pParent`, at its authored height offset.
    Pane* makePiece(const Kits& rKits, Pane* pParent, const char* pPiece, const char* pName, f32 y) {
        Pane* pPane = rKits.mButton->make(pPiece, pName);
        if (pPane != nullptr) {
            pPane->mTranslate = Vec3f(0.0f, y, 0.0f);
            pParent->AppendChild(pPane);
        }
        return pPane;
    }

    // A button: the shadow (BackButton's ShaBButton: 1.07 by 1.1, half seen, 6 below), the rim and face (BButton:
    // 1.1), the shine (BButtonShine: 1.1, shown for the highlight), Back's arrow, and its label.
    bool makeChip(const Kits& rKits, Pane* pParent, Chip* pChip, const char* pName, bool arrow, f32 textSize, u32 textColor) {
        MR::FormattedName name;
        pChip->mRoot = makeGroup(pParent, pName);
        name.format("%sA", pName);
        pChip->mArt = pChip->mRoot != nullptr ? makeGroup(pChip->mRoot, name.c_str()) : nullptr;
        if (pChip->mArt == nullptr) {
            return false;
        }
        static const char* const cLayerNames[LayerNum] = {"S", "R", "F", "H"};
        static const char* const cPieces[LayerNum][3] = {
            {"ShaLButton", "ShaCButton", "ShaRButton"},
            {"PicLFrame", "PicCFrame", "PicRFrame"},
            {"PicLButton", "PicCButton", "PicRButton"},
            {"PicLShine", "PicCShine", "PicRShine"},
        };
        for (s32 layer = 0; layer < LayerNum; layer++) {
            name.format("%s%s", pName, cLayerNames[layer]);
            Pane* pLayer = makeGroup(pChip->mArt, name.c_str());
            if (pLayer == nullptr) {
                return false;
            }
            pChip->mLayers[layer] = pLayer;
            if (layer == LayerShadow) {
                pLayer->mScale = Vec2f(1.07f, 1.1f);
                pLayer->mAlpha = 128;
            } else {
                pLayer->mScale = Vec2f(cButtonGroup, cButtonGroup);
            }
            const f32 y = layer == LayerShadow ? -6.0f : 0.0f;
            name.format("%s%sL", pName, cLayerNames[layer]);
            pChip->mLeft[layer] = makePiece(rKits, pLayer, cPieces[layer][0], name.c_str(), y);
            name.format("%s%sC", pName, cLayerNames[layer]);
            pChip->mMiddle[layer] = makePiece(rKits, pLayer, cPieces[layer][1], name.c_str(), y);
            name.format("%s%sR", pName, cLayerNames[layer]);
            pChip->mRight[layer] = makePiece(rKits, pLayer, cPieces[layer][2], name.c_str(), y);
            if (pChip->mLeft[layer] == nullptr || pChip->mMiddle[layer] == nullptr || pChip->mRight[layer] == nullptr) {
                return false;
            }
            if (layer == LayerFace && arrow) {
                name.format("%sArw", pName);
                pChip->mArrow = makePiece(rKits, pLayer, "PicArrow", name.c_str(), 0.0f);
            }
        }
        name.format("%sT", pName);
        pChip->mText = makeText(rKits, pChip->mRoot, name.c_str(), 64);
        if (pChip->mText == nullptr) {
            return false;
        }
        setTextStyle(pChip->mText, textSize, textColor, textColor, 4);
        return true;
    }

    // The button's art for `width` x `height` (its rim's outer edge), centred on the chip's place.
    void shapeChip(Chip& rChip, f32 width, f32 height) {
        const f32 scale = height / cButtonRimHeight;
        rChip.mArt->mScale = Vec2f(scale, scale);
        f32 middle = width / (scale * cButtonGroup) - 2.0f * cButtonEnd;
        middle = middle < 0.0f ? 0.0f : middle;
        for (s32 layer = 0; layer < LayerNum; layer++) {
            rChip.mMiddle[layer]->mSize.width = middle;
            rChip.mLeft[layer]->mTranslate.x = -middle * 0.5f;
            rChip.mRight[layer]->mTranslate.x = middle * 0.5f;
        }
    }

    // The game's button art is 1.1 times larger inside its scale; a point in board units in its face layer's units.
    f32 toFaceUnits(const Chip& rChip, f32 x) {
        return x / (rChip.mArt->mScale.x * cButtonGroup);
    }

    // Back's arrow (BackButton's 48 across), as drawn at the button's height.
    f32 backArrowSize(f32 height) {
        return 48.0f * cButtonGroup * height / cButtonRimHeight;
    }

    void setInfluencedAlphaRecursive(Pane* pPane, s32 depth) {
        if (depth > 16) {
            return;
        }
        pPane->SetInfluencedAlpha(true);
        for (Ui::Pane* pChild : pPane->GetChildList()) {
            setInfluencedAlphaRecursive(pChild, depth + 1);
        }
    }

    // The screen in screen units (LayoutAspect's canvas, its width made square).
    void readScreen(f32* pScaleX, f32* pWidth, f32* pHeight) {
        f32 canvasWidth = NebulaFrameClock::cLayoutWidth;
        f32 canvasHeight = NebulaFrameClock::cScreenHeight;
        if (MR::isScreen16Per9() && MR::getDisplay().wide) {
            canvasWidth = MR::getDisplay().layout_width;
            canvasHeight = NebulaFrameClock::cScreenHeight * MR::getDisplay().vertical_fit;
        }
        *pScaleX = MR::isScreen16Per9() ? 0.75f : 1.0f;
        *pWidth = canvasWidth / *pScaleX;
        *pHeight = canvasHeight;
    }

    static u32 sShown = 0;

    bool isSharpScreenShown() {
        return sShown != 0;
    }

    void setSharp(u32 screen, bool shown) {
        const u32 next = shown ? (sShown | screen) : (sShown & ~screen);
        if ((next != 0) != (sShown != 0)) {
            NebulaHost::sharp2D(next != 0 ? 1 : 0);
        }
        sShown = next;
    }

    // the scissor is in the frame's units: the layout's canvas spans the default viewport.
    void drawClipped(Pane* pPane, const Ui::DrawInfo& rDrawInfo, f32 left, f32 right, f32 top, f32 bottom) {
        const Mtx34f& rPlace = pPane->mpParent->mGlbMtx;
        f32 scaleX;
        f32 width;
        f32 canvasHeight;
        readScreen(&scaleX, &width, &canvasHeight);
        const f32 canvasWidth = width * scaleX;
        const f32 x0f = (rPlace.mMtx[0][0] * left + rPlace.mMtx[0][3]) / canvasWidth + 0.5f;
        const f32 x1f = (rPlace.mMtx[0][0] * right + rPlace.mMtx[0][3]) / canvasWidth + 0.5f;
        const f32 y0f = 0.5f - (rPlace.mMtx[1][1] * top + rPlace.mMtx[1][3]) / canvasHeight;
        const f32 y1f = 0.5f - (rPlace.mMtx[1][1] * bottom + rPlace.mMtx[1][3]) / canvasHeight;
        const f32 x0 = x0f < 0.0f ? 0.0f : x0f;
        const f32 y0 = y0f < 0.0f ? 0.0f : y0f;
        const f32 x1 = x1f > 1.0f ? 1.0f : x1f;
        const f32 y1 = y1f > 1.0f ? 1.0f : y1f;
        if (x1 <= x0 || y1 <= y0) {
            return;
        }
        Frame::setScissor(x0, y0, x1 - x0, y1 - y0);
        pPane->Draw(rDrawInfo);
        Frame::setScissor(0.0f, 0.0f, 1.0f, 1.0f);
    }
};  // namespace NebulaUi
