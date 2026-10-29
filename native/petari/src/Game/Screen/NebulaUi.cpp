#include "Game/Screen/NebulaUi.hpp"
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>

#ifdef TARGET_PC
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Screen/NebulaPaneKit.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/material.h>
#include <nw4r/ut/Font.h>
#include <nw4r/ut/Rect.h>
#include <revolution/os.h>

namespace NebulaUi {
    nw4r::ut::Color color(u32 rgba) {
        return nw4r::ut::Color(rgba);
    }

    void setTextColor(TextBox* pText, u32 top, u32 bottom) {
        pText->SetTextColor(0, color(top));
        pText->SetTextColor(1, color(bottom));
    }

    GXColorS10 tevColor(u32 rgba, bool alpha) {
        GXColorS10 c;
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
        if (nw4r::lyt::Material* pContent = pWindow->GetContentMaterial()) {
            pContent->SetTevColor(0, tevColor(fill, false));
            pContent->SetTevColor(1, tevColor(fill, true));
        }
        for (u32 i = 0; i < pWindow->mFrameNum; i++) {
            if (nw4r::lyt::Material* pFrame = pWindow->GetFrameMaterial(i)) {
                pFrame->SetTevColor(0, tevColor(ring, false));
                pFrame->SetTevColor(1, tevColor(fill, true));
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
        pPane->mScale = nw4r::math::VEC2(k, k);
        pPane->mSize = nw4r::lyt::Size(w / k, h / k);
        pPane->mTranslate = nw4r::math::VEC3(x, y, 0.0f);
    }

    void setText(TextBox* pText, const wchar_t* pString) {
        if (pText != nullptr) {
            LayoutCoreUtil::setTextBoxMessage(pText, pString);
        }
    }

    void setTextSize(TextBox* pText, f32 size) {
        pText->SetFontSize(nw4r::lyt::Size(size * cFontAspect, size));
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
        pText->mSize = nw4r::lyt::Size(limit, 1000.0f);
        const nw4r::ut::Rect rect = pText->GetTextDrawRect();
        *pWidth = rect.right - rect.left;
        *pHeight = rect.bottom - rect.top;
        pText->mSize = nw4r::lyt::Size(*pWidth + 2.0f, *pHeight + 2.0f);
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
    s32 unitLength(const wchar_t* p) {
        if (*p == 0x1A && p[1] != 0) {
            const s32 units = (p[1] & 0xFF) / 2;
            return units > 0 ? units : 1;
        }
        return 1;
    }

    void wrapWords(TextBox* pText, const wchar_t* pSource, f32 limit, f32* pWidth, f32* pHeight) {
        const s32 cMax = 256;
        wchar_t out[cMax];
        wchar_t line[cMax];
        s32 length = 0;
        s32 lineStart = 0;
        s32 i = 0;
        while (pSource[i] != 0 && length < cMax - 2) {
            s32 end = i;
            while (pSource[end] != 0 && pSource[end] != L' ') {
                end += unitLength(&pSource[end]);
            }
            // The line so far with the word added.
            s32 n = 0;
            for (s32 k = lineStart; k < length && n < cMax - 1; k++) {
                line[n++] = out[k];
            }
            if (length > lineStart && n < cMax - 1) {
                line[n++] = L' ';
            }
            for (s32 k = i; k < end && n < cMax - 1; k++) {
                line[n++] = pSource[k];
            }
            line[n] = 0;
            setText(pText, line);
            f32 width, height;
            measure(pText, 4000.0f, &width, &height);
            if (width > limit && length > lineStart) {
                out[length++] = L'\n';
                lineStart = length;
            } else if (length > lineStart) {
                out[length++] = L' ';
            }
            for (s32 k = i; k < end && length < cMax - 1; k++) {
                out[length++] = pSource[k];
            }
            i = end;
            while (pSource[i] == L' ') {
                i++;
            }
        }
        out[length] = 0;
        setText(pText, out);
        measure(pText, limit, pWidth, pHeight);
    }

    // A box's font size for picture-font glyphs `size` high: the writer scales them by the box's own font (the message
    // font, 27 wide for 33 high), so a square size would draw them a fifth too wide.
    nw4r::lyt::Size glyphFontSize(const TextBox* pText, f32 size) {
        const nw4r::ut::Font* pFont = pText->mpFont;
        const f32 aspect = pFont != nullptr && pFont->GetHeight() > 0 ? static_cast< f32 >(pFont->GetWidth()) / pFont->GetHeight() : 1.0f;
        return nw4r::lyt::Size(size * aspect, size);
    }

    // The game's picture font: a glyph sized like text, centred on its place.
    void setGlyph(TextBox* pText, s32 code, f32 size) {
        if (pText == nullptr) {
            return;
        }
        wchar_t glyph[8] = {0};
        MR::addPictureFontTag(glyph, code);
        setText(pText, glyph);
        pText->SetFontSize(glyphFontSize(pText, size));
        pText->mCharSpace = 0.0f;
        setAnchor(pText, 4);
        pText->mSize = nw4r::lyt::Size(size * 2.0f, size * 2.0f);
    }

    // A glyph picture's coverage at (x, y) of its sheet, 0-15: its alpha where it has one, else its intensity. The
    // sheets are the console's tiled formats.
    s32 glyphCoverage(const nw4r::ut::Glyph& rGlyph, s32 x, s32 y) {
        const u8* pData = static_cast< const u8* >(rGlyph.pTexture);
        const s32 tilesAcross = (rGlyph.texWidth + 7) / 8;
        switch (rGlyph.texFormat) {
        case GX_TF_I4: {
            const u8 byte = pData[((y / 8) * tilesAcross + x / 8) * 32 + (y % 8) * 4 + (x % 8) / 2];
            return (x & 1) == 0 ? byte >> 4 : byte & 0xF;
        }
        case GX_TF_IA4:
            return pData[((y / 4) * tilesAcross + x / 8) * 32 + (y % 4) * 8 + x % 8] >> 4;
        case GX_TF_I8:
            return pData[((y / 4) * tilesAcross + x / 8) * 32 + (y % 4) * 8 + x % 8] >> 4;
        case GX_TF_IA8:
            return pData[(((y / 4) * ((rGlyph.texWidth + 3) / 4) + x / 4) * 16 + (y % 4) * 4 + x % 4) * 2] >> 4;
        default:
            return -1;
        }
    }

    // The ink box of `code`'s picture, in its cell's pixels (x from the cell's left, y from its top); false when
    // the picture cannot be read or holds no ink.
    bool glyphInkBox(const nw4r::ut::Font* pFont, u16 code, s32* pLeft, s32* pTop, s32* pRight, s32* pBottom) {
        nw4r::ut::Glyph glyph;
        pFont->GetGlyph(&glyph, code);
        if (glyph.pTexture == nullptr) {
            return false;
        }
        *pLeft = glyph.widths.glyphWidth;
        *pTop = glyph.height;
        *pRight = -1;
        *pBottom = -1;
        for (s32 y = 0; y < glyph.height; y++) {
            for (s32 x = 0; x < glyph.widths.glyphWidth; x++) {
                const s32 ink = glyphCoverage(glyph, glyph.cellX + x, glyph.cellY + y);
                if (ink < 0) {
                    return false;
                }
                if (ink >= 4) {
                    *pLeft = x < *pLeft ? x : *pLeft;
                    *pRight = x > *pRight ? x : *pRight;
                    *pTop = y < *pTop ? y : *pTop;
                    *pBottom = y > *pBottom ? y : *pBottom;
                }
            }
        }
        return *pRight >= 0;
    }
    bool inkOffset(const TextBox* pText, f32* pX, f32* pY, f32* pWidth, f32* pHeight) {
        const nw4r::ut::Font* pFont = pText->mpFont;
        const wchar_t* pString = pText->mTextBuf;
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
        s32 aLeft, aTop, aRight, aBottom;
        if (!glyphInkBox(pFont, L'A', &aLeft, &aTop, &aRight, &aBottom)) {
            return false;
        }
        *pX = ((left + right) * 0.5f - cursor * 0.5f) * scaleX;
        if (pWidth != nullptr && pHeight != nullptr) {
            *pWidth = (right - left) * scaleX;
            *pHeight = (bottom - top) * scaleY;
        }
        // The cell's y runs down, the layout's up.
        *pY = -((top + bottom) * 0.5f - (aTop + aBottom + 1) * 0.5f) * scaleY;
        return true;
    }

    bool stringInkBox(const nw4r::ut::Font* pFont, const wchar_t* pString, f32 space, bool strict, f32* pLeft, f32* pTop,
                      f32* pRight, f32* pBottom, f32* pEnd) {
        f32 cursor = 0.0f;
        f32 left = 1e9f;
        f32 right = -1e9f;
        f32 top = 1e9f;
        f32 bottom = -1e9f;
        for (const wchar_t* p = pString; *p != 0; p++) {
            const nw4r::ut::CharWidths widths = pFont->GetCharWidths(static_cast< u16 >(*p));
            if (p != pString) {
                cursor += space;
            }
            s32 l, t, r, b;
            if (glyphInkBox(pFont, static_cast< u16 >(*p), &l, &t, &r, &b)) {
                left = cursor + widths.left + l < left ? cursor + widths.left + l : left;
                right = cursor + widths.left + r + 1 > right ? cursor + widths.left + r + 1 : right;
                top = t < top ? t : top;
                bottom = b + 1 > bottom ? b + 1 : bottom;
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

    wchar_t* append(wchar_t* pDst, wchar_t* pEnd, const wchar_t* pSrc) {
        while (pSrc != nullptr && *pSrc != 0 && pDst + 1 < pEnd) {
            *pDst++ = *pSrc++;
        }
        *pDst = 0;
        return pDst;
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
        Pane* pPane = nw4r::lyt::Layout::NewObj< Pane >();
        if (pPane != nullptr) {
            pPane->SetName(pName);
            pPane->SetInfluencedAlpha(true);
            pParent->AppendChild(pPane);
        }
        return pPane;
    }

    Picture* makePicture(Pane* pParent, const char* pName, nw4r::lyt::TexMap* pTexture) {
        if (pTexture == nullptr) {
            return nullptr;
        }
        Picture* pPicture = nw4r::lyt::Layout::NewObj< Picture >(*pTexture);
        if (pPicture != nullptr) {
            pPicture->SetName(pName);
            pPicture->SetInfluencedAlpha(true);
            for (u32 i = 0; i < 4; i++) {
                pPicture->SetVtxColor(i, color(0xFFFFFFFF));
            }
            pParent->AppendChild(pPicture);
        }
        return pPicture;
    }

    // One piece of the game's button art, under `pParent`, at its authored height offset.
    Pane* makePiece(const Kits& rKits, Pane* pParent, const char* pPiece, const char* pName, f32 y) {
        Pane* pPane = rKits.mButton->make(pPiece, pName);
        if (pPane != nullptr) {
            pPane->mTranslate = nw4r::math::VEC3(0.0f, y, 0.0f);
            pParent->AppendChild(pPane);
        }
        return pPane;
    }

    // A button: the shadow (BackButton's ShaBButton: 1.07 by 1.1, half seen, 6 below), the rim and face (BButton:
    // 1.1), the shine (BButtonShine: 1.1, shown for the highlight), Back's arrow, and its label.
    bool makeChip(const Kits& rKits, Pane* pParent, Chip* pChip, const char* pName, bool arrow, f32 textSize, u32 textColor) {
        char name[17];
        pChip->mRoot = makeGroup(pParent, pName);
        snprintf(name, sizeof(name), "%sA", pName);
        pChip->mArt = pChip->mRoot != nullptr ? makeGroup(pChip->mRoot, name) : nullptr;
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
            snprintf(name, sizeof(name), "%s%s", pName, cLayerNames[layer]);
            Pane* pLayer = makeGroup(pChip->mArt, name);
            if (pLayer == nullptr) {
                return false;
            }
            pChip->mLayers[layer] = pLayer;
            if (layer == LayerShadow) {
                pLayer->mScale = nw4r::math::VEC2(1.07f, 1.1f);
                pLayer->mAlpha = 128;
            } else {
                pLayer->mScale = nw4r::math::VEC2(cButtonGroup, cButtonGroup);
            }
            const f32 y = layer == LayerShadow ? -6.0f : 0.0f;
            snprintf(name, sizeof(name), "%s%sL", pName, cLayerNames[layer]);
            pChip->mLeft[layer] = makePiece(rKits, pLayer, cPieces[layer][0], name, y);
            snprintf(name, sizeof(name), "%s%sC", pName, cLayerNames[layer]);
            pChip->mMiddle[layer] = makePiece(rKits, pLayer, cPieces[layer][1], name, y);
            snprintf(name, sizeof(name), "%s%sR", pName, cLayerNames[layer]);
            pChip->mRight[layer] = makePiece(rKits, pLayer, cPieces[layer][2], name, y);
            if (pChip->mLeft[layer] == nullptr || pChip->mMiddle[layer] == nullptr || pChip->mRight[layer] == nullptr) {
                return false;
            }
            if (layer == LayerFace && arrow) {
                snprintf(name, sizeof(name), "%sArw", pName);
                pChip->mArrow = makePiece(rKits, pLayer, "PicArrow", name, 0.0f);
            }
        }
        snprintf(name, sizeof(name), "%sT", pName);
        pChip->mText = makeText(rKits, pChip->mRoot, name, 64);
        if (pChip->mText == nullptr) {
            return false;
        }
        setTextStyle(pChip->mText, textSize, textColor, textColor, 4);
        return true;
    }

    // The button's art for `width` x `height` (its rim's outer edge), centred on the chip's place.
    void shapeChip(Chip& rChip, f32 width, f32 height) {
        const f32 scale = height / cButtonRimHeight;
        rChip.mArt->mScale = nw4r::math::VEC2(scale, scale);
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
        for (nw4r::lyt::PaneList::Iterator it = pPane->GetChildList().GetBeginIter(); it != pPane->GetChildList().GetEndIter(); ++it) {
            setInfluencedAlphaRecursive(&*it, depth + 1);
        }
    }

    // The screen in screen units (LayoutAspect's canvas, its width made square).
    void readScreen(f32* pScaleX, f32* pWidth, f32* pHeight) {
        f32 canvasWidth = 608.0f;
        f32 canvasHeight = 456.0f;
        if (MR::isScreen16Per9() && MR::getDisplay().wide) {
            canvasWidth = MR::getDisplay().canvas_width;
            canvasHeight = 456.0f * MR::getDisplay().vertical_fit;
        }
        *pScaleX = MR::isScreen16Per9() ? 0.75f : 1.0f;
        *pWidth = canvasWidth / *pScaleX;
        *pHeight = canvasHeight;
    }

    void setSharp(u32 screen, bool shown) {
        static u32 sShown = 0;
        const u32 next = shown ? (sShown | screen) : (sShown & ~screen);
        if ((next != 0) != (sShown != 0)) {
            __NebulaSharp2D(next != 0 ? 1 : 0);
        }
        sShown = next;
    }

    // GX's scissor is in the frame buffer's units: the layout's canvas spans the default viewport.
    void drawClipped(Pane* pPane, const nw4r::lyt::DrawInfo& rDrawInfo, f32 rise, f32 left, f32 right, f32 top, f32 bottom) {
        f32 scaleX;
        f32 width;
        f32 canvasHeight;
        readScreen(&scaleX, &width, &canvasHeight);
        const f32 canvasWidth = width * scaleX;
        const f32 frameWidth = static_cast< f32 >(MR::getFrameBufferWidth());
        const f32 frameHeight = static_cast< f32 >(MR::getScreenHeight());
        const f32 x0f = (left * scaleX / canvasWidth + 0.5f) * frameWidth;
        const f32 x1f = (right * scaleX / canvasWidth + 0.5f) * frameWidth;
        const f32 y0f = (0.5f - (top + rise) / canvasHeight) * frameHeight;
        const f32 y1f = (0.5f - (bottom + rise) / canvasHeight) * frameHeight;
        const s32 x0 = x0f < 0.0f ? 0 : static_cast< s32 >(x0f);
        const s32 y0 = y0f < 0.0f ? 0 : static_cast< s32 >(y0f);
        const s32 x1 = static_cast< s32 >((x1f > frameWidth ? frameWidth : x1f) + 0.999f);
        const s32 y1 = static_cast< s32 >((y1f > frameHeight ? frameHeight : y1f) + 0.999f);
        if (x1 <= x0 || y1 <= y0) {
            return;
        }
        J3DNativeView::setScissor(x0, y0, x1 - x0, y1 - y0);
        pPane->Draw(rDrawInfo);
        J3DNativeView::setScissor(0, 0, static_cast< u32 >(frameWidth), static_cast< u32 >(frameHeight));
    }
};  // namespace NebulaUi
#endif
