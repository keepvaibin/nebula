#include "nw4r/ut/CharWriter.h"
#include "Game/Render/RenderTexture.hpp"
#include "Game/Screen/NebulaIcons.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "nw4r/lyt/common.h"
#include "nw4r/ut/Font.h"

namespace {
    // The writer's native material (runtime/src/render/materials/font_text.hlsl) as Setup set it, with the glyph sheet
    // LoadTexture binds. Blended by alpha, no depth, no culling.
    SiteMaterial sMaterial;

    // The run of glyphs not yet sent: one sheet, one material, one matrix. A glyph's quad blends over the ones before
    // it in the run as it would over a draw before it, so a run draws what a draw a glyph drew.
    const u32 cRunGlyphs = 256;
    VertexPCT sRun[cRunGlyphs * 4];
    u32 sRunGlyphs;
    f32 sRunMtx[3][4];

    void SendRun() {
        if (sRunGlyphs == 0) {
            return;
        }
        TypedDraw site = TypedDraw::eyeSpace(sMaterial, sRunMtx);
        VertexPCT* pOut = site.vertices< VertexPCT >(NEBULA_PRIMITIVE_QUADS, sRunGlyphs * 4);
        memcpy(pOut, sRun, sRunGlyphs * 4 * sizeof(VertexPCT));
        sRunGlyphs = 0;
    }

    // Where the next glyph's corners go; the run is sent first when it is full or the matrix moved.
    VertexPCT* RunQuad(const f32 (*pMtx)[4]) {
        if (sRunGlyphs != 0) {
            bool same = sRunGlyphs < cRunGlyphs;
            for (u32 i = 0; same && i < 12; i++) {
                same = __builtin_bit_cast(u32, (&pMtx[0][0])[i]) == __builtin_bit_cast(u32, (&sRunMtx[0][0])[i]);
            }
            if (!same) {
                SendRun();
            }
        }
        if (sRunGlyphs == 0) {
            memcpy(sRunMtx, pMtx, sizeof(sRunMtx));
        }
        return &sRun[sRunGlyphs++ * 4];
    }

    // `range`: the font's distance range in sheet texels, 0 for sheets that are images.
    void SetupMaterial(nw4r::ut::Color min, nw4r::ut::Color max, bool intensity, u8 range) {
        SendRun();
        sMaterial.init(NEBULA_SHADER_FONT_TEXT, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        sMaterial.mState.depth_test = 0;
        sMaterial.mState.depth_func = NEBULA_COMPARE_NEVER;
        sMaterial.setColor(0, min.r, min.g, min.b, min.a);
        sMaterial.setColor(4, max.r, max.g, max.b, max.a);
        sMaterial.constants[8] = intensity ? 1.0f : 0.0f;
        sMaterial.constants[9] = static_cast< f32 >(range);
    }
};  // namespace

namespace nw4r {
    namespace ut {
        CharWriter::LoadingTexture CharWriter::mLoadingTexture;

        CharWriter::CharWriter() : mAlpha(Color::ALPHA_MAX), mIsWidthFixed(false), mFixedWidth(0), mFont(NULL) {
            mLoadingTexture.Reset();
            ResetColorMapping();
            SetGradationMode(GRADMODE_NONE);
            SetTextColor(Color::WHITE);
            SetScale(1, 1);
            SetCursor(0, 0, 0);
            EnableLinearFilter(true, true);
        }

        void CharWriter::LoadTexture(const Glyph& glyph, u8 slot) {
            LoadingTexture loadInfo;

            loadInfo.slot = slot;
            loadInfo.texture = glyph.pTexture;
            loadInfo.filter = mFilter;

            if (loadInfo != mLoadingTexture) {
                SiteTexture tobj;

                SendRun();
                NebulaIcons::noteDrawn(glyph.pTexture);
                SiteTextures::init(&tobj, const_cast< void* >(glyph.pTexture), glyph.texWidth, glyph.texHeight, NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);
                SiteTextures::setFilter(&tobj, mFilter.atSmall, mFilter.atLarge, 0, 0, 0, false, 0);
                sMaterial.setTexture(slot - 0, tobj);

                mLoadingTexture = loadInfo;
            }
        }

        CharWriter::~CharWriter() {
        }

        void CharWriter::SetupVertexFormat() {
        }

        void CharWriter::EndRun() {
            SendRun();
        }

        void CharWriter::Setup() {
            ResetTextureCache();

            if ((mColorMapping.min != 0x00000000u) || (mColorMapping.max != 0xFFFFFFFFu)) {
                SetupWithColorMapping(mColorMapping.min, mColorMapping.max);
            } else {
                if (mFont) {
                    u8 format = mFont->GetTextureFormat();

                    switch (format) {
                    case SHEET_I4:
                    case SHEET_I8:
                        SetupForIntensity();
                        break;

                    case SHEET_IA4:
                    case SHEET_IA8:
                        SetupDefault();
                        break;

                    case SHEET_RGB565:
                    case SHEET_RGB5A3:
                    case SHEET_RGBA8:
                        SetupForColor();
                        break;

                    default:
                        SetupDefault();
                        break;
                    }
                } else {
                    SetupDefault();
                }
            }
        }

        u8 CharWriter::FieldRange() const {
            return mFont != nullptr ? mFont->GetFieldRange() : 0;
        }

        void CharWriter::SetupDefault() {
            SetupMaterial(0x00000000u, 0xFFFFFFFFu, false, FieldRange());
            SetupVertexFormat();
        }

        void CharWriter::SetupForIntensity() {
            SetupMaterial(0x00000000u, 0xFFFFFFFFu, true, FieldRange());
            SetupVertexFormat();
        }

        void CharWriter::SetupForColor() {
            SetupMaterial(0x00000000u, 0xFFFFFFFFu, false, 0);
            SetupVertexFormat();
        }

        void CharWriter::SetFontSize(f32 width, f32 height) {
            SetScale(width / mFont->GetWidth(), height / mFont->GetHeight());
        }

        f32 CharWriter::GetFontWidth() const {
            return mFont->GetWidth() * mScale.x;
        }

        f32 CharWriter::GetFontHeight() const {
            return mFont->GetHeight() * mScale.y;
        }

        f32 CharWriter::GetFontAscent() const {
            return mFont->GetAscent() * mScale.y;
        }

        void CharWriter::EnableLinearFilter(bool atSmall, bool atLarge) {
            mFilter.atSmall = atSmall ? NEBULA_FILTER_LINEAR : NEBULA_FILTER_NEAREST;
            mFilter.atLarge = atLarge ? NEBULA_FILTER_LINEAR : NEBULA_FILTER_NEAREST;
        }

        f32 CharWriter::Print(CharCode code) {
            Glyph glyph;
            f32 width;
            f32 left;

            mFont->GetGlyph(&glyph, code);

            CharWidths& widths = glyph.widths;

            if (mIsWidthFixed) {
                f32 margin = (mFixedWidth - widths.charWidth * mScale.x) / 2;

                width = mFixedWidth;
                left = margin + widths.left * mScale.x;
            } else {
                width = widths.charWidth * mScale.x;
                left = widths.left * mScale.x;
            }

            PrintGlyph(mCursorPos.x + left, mCursorPos.y, mCursorPos.z, glyph);

            mCursorPos.x += width;

            return width;
        }

        void CharWriter::PrintGlyph(f32 x, f32 y, f32 z, const Glyph& glyph) {
            const f32 posLeft = x;
            const f32 posTop = y;
            const f32 posRight = posLeft + glyph.widths.glyphWidth * mScale.x;
            const f32 posBottom = posTop + glyph.height * mScale.y;
            const f32 posZ = z;
            const u16 texLeft = static_cast< u16 >(glyph.cellX * TEXTURE_COODINATE_ONE / glyph.texWidth);
            const u16 texTop = static_cast< u16 >(glyph.cellY * TEXTURE_COODINATE_ONE / glyph.texHeight);
            const u16 texRight = static_cast< u16 >((glyph.cellX + glyph.widths.glyphWidth) * TEXTURE_COODINATE_ONE / glyph.texWidth);
            const u16 texBottom = static_cast< u16 >((glyph.cellY + glyph.height) * TEXTURE_COODINATE_ONE / glyph.texHeight);

            LoadTexture(glyph, 0);

            VertexPCT* vtx = RunQuad(lyt::detail::GetDrawMtx());
            {
                *vtx++ = {posLeft, posTop, posZ, Rgba8(mVertexColor.lu), texLeft / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS), texTop / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS)};

                *vtx++ = {posRight, posTop, posZ, Rgba8(mVertexColor.ru), texRight / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS), texTop / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS)};

                *vtx++ = {posRight, posBottom, posZ, Rgba8(mVertexColor.rd), texRight / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS), texBottom / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS)};

                *vtx++ = {posLeft, posBottom, posZ, Rgba8(mVertexColor.ld), texLeft / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS), texBottom / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS)};
            }
        }

        void CharWriter::UpdateVertexColor() {
            mVertexColor.lu = mTextColor.start;
            mVertexColor.ru = (mTextColor.gradationMode != GRADMODE_H) ? mTextColor.start : mTextColor.end;
            mVertexColor.ld = (mTextColor.gradationMode != GRADMODE_V) ? mTextColor.start : mTextColor.end;
            mVertexColor.rd = (mTextColor.gradationMode == GRADMODE_NONE) ? mTextColor.start : mTextColor.end;

            mVertexColor.lu.a = static_cast< u8 >(mVertexColor.lu.a * mAlpha / Color::ALPHA_MAX);
            mVertexColor.ru.a = static_cast< u8 >(mVertexColor.ru.a * mAlpha / Color::ALPHA_MAX);
            mVertexColor.ld.a = static_cast< u8 >(mVertexColor.ld.a * mAlpha / Color::ALPHA_MAX);
            mVertexColor.rd.a = static_cast< u8 >(mVertexColor.rd.a * mAlpha / Color::ALPHA_MAX);
        }

        void CharWriter::SetupWithColorMapping(Color min, Color max) {
            SetupMaterial(min, max, false, FieldRange());
            SetupVertexFormat();
        }
    };  // namespace ut
};  // namespace nw4r
