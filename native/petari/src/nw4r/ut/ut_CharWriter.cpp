#include "nw4r/ut/CharWriter.h"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "nw4r/ut/Font.h"
#include <revolution/gx/GXVert.h>

namespace {
    void SetupGXCommon() {
        static const nw4r::ut::Color fog = 0;
        J3DNativeState::setFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, fog);
        J3DNativeState::setTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
        J3DNativeState::setZTexture(GX_ZT_DISABLE, GX_TF_Z8, 0);
        J3DNativeState::setNumChans(1);
        J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_DISABLE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setNumTexGens(1);
        J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, 0x7D);
        J3DNativeState::setNumIndStages(0);
        J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_SET);
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

        void CharWriter::LoadTexture(const Glyph& glyph, GXTexMapID slot) {
            LoadingTexture loadInfo;

            loadInfo.slot = slot;
            loadInfo.texture = glyph.pTexture;
            loadInfo.filter = mFilter;

            if (loadInfo != mLoadingTexture) {
                GXTexObj tobj;

                J3DNativeTexture::init(&tobj, const_cast< void* >(glyph.pTexture), glyph.texWidth, glyph.texHeight, glyph.texFormat, GX_CLAMP, GX_CLAMP,
                             GX_FALSE);
                J3DNativeTexture::initLOD(&tobj, mFilter.atSmall, mFilter.atLarge, 0, 0, 0, GX_DISABLE, GX_DISABLE, GX_ANISO_1);
                J3DNativeTexture::load(&tobj, slot);

                mLoadingTexture = loadInfo;
            }
        }

        CharWriter::~CharWriter() {
        }

        void CharWriter::SetupVertexFormat() {
        }

        void CharWriter::SetupGX() {
            ResetTextureCache();

            if ((mColorMapping.min != 0x00000000UL) || (mColorMapping.max != 0xFFFFFFFFUL)) {
                SetupGXWithColorMapping(mColorMapping.min, mColorMapping.max);
            } else {
                if (mFont) {
                    GXTexFmt format = mFont->GetTextureFormat();

                    switch (format) {
                    case GX_TF_I4:
                    case GX_TF_I8:
                        SetupGXForI();
                        break;

                    case GX_TF_IA4:
                    case GX_TF_IA8:
                        SetupGXDefault();
                        break;

                    case GX_TF_RGB565:
                    case GX_TF_RGB5A3:
                    case GX_TF_RGBA8:
                        SetupGXForRGBA();
                        break;

                    default:
                        SetupGXDefault();
                        break;
                    }
                } else {
                    SetupGXDefault();
                }
            }
        }

        void CharWriter::SetupGXDefault() {
            SetupGXCommon();
            J3DNativeState::setNumTevStages(1);
            J3DNativeState::setTevDirect(GX_TEVSTAGE0);
            J3DNativeState::setTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
            J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
            J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_MODULATE);
            SetupVertexFormat();
        }

        void CharWriter::SetupGXForI() {
            SetupGXCommon();
            J3DNativeState::setNumTevStages(1);
            J3DNativeState::setTevDirect(GX_TEVSTAGE0);
            J3DNativeState::setTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
            J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
            J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
            J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
            J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            SetupVertexFormat();
        }

        void CharWriter::SetupGXForRGBA() {
            SetupGXDefault();
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
            mFilter.atSmall = atSmall ? GX_LINEAR : GX_NEAR;
            mFilter.atLarge = atLarge ? GX_LINEAR : GX_NEAR;
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

            LoadTexture(glyph, GX_TEXMAP0);

            J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0);
            {
                J3DNativeDraw::position(posLeft, posTop, posZ);
                J3DNativeDraw::color(mVertexColor.lu);
                J3DNativeDraw::texCoord(texLeft / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS), texTop / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS));

                J3DNativeDraw::position(posRight, posTop, posZ);
                J3DNativeDraw::color(mVertexColor.ru);
                J3DNativeDraw::texCoord(texRight / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS), texTop / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS));

                J3DNativeDraw::position(posRight, posBottom, posZ);
                J3DNativeDraw::color(mVertexColor.rd);
                J3DNativeDraw::texCoord(texRight / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS), texBottom / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS));

                J3DNativeDraw::position(posLeft, posBottom, posZ);
                J3DNativeDraw::color(mVertexColor.ld);
                J3DNativeDraw::texCoord(texLeft / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS), texBottom / static_cast< f32 >(1 << TEXTURE_COODINATE_FRACTION_BITS));
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

        void CharWriter::SetupGXWithColorMapping(Color min, Color max) {
            SetupGXCommon();
            J3DNativeState::setNumTevStages(2);
            J3DNativeState::setTevDirect(GX_TEVSTAGE0);
            J3DNativeState::setTevDirect(GX_TEVSTAGE1);
            J3DNativeState::setTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
            J3DNativeState::setTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP0);
            J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
            J3DNativeState::setTevColor(GX_TEVREG0, min);
            J3DNativeState::setTevColor(GX_TEVREG1, max);
            J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_C1, GX_CC_TEXC, GX_CC_ZERO);
            J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_A0, GX_CA_A1, GX_CA_TEXA, GX_CA_ZERO);
            J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            J3DNativeState::setTevOrder(GX_TEVSTAGE1, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
            J3DNativeState::setTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_RASC, GX_CC_ZERO);
            J3DNativeState::setTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_APREV, GX_CA_RASA, GX_CA_ZERO);
            J3DNativeState::setTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            J3DNativeState::setTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

            SetupVertexFormat();
        }
    };  // namespace ut
};  // namespace nw4r
