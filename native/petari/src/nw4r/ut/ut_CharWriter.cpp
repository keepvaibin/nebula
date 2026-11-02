#include "nw4r/ut/CharWriter.h"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "nw4r/ut/Font.h"

namespace {
    // The writer's native material (runtime/src/render/materials/font_text.hlsl) as SetupGX set it; each glyph sheet
    // LoadTexture binds sends it. Blended by alpha; depth, culling and writes as the drawing state has them.
    NebulaNativeMaterial sMaterial;

    void SetupMaterial(nw4r::ut::Color min, nw4r::ut::Color max, bool intensity) {
        static const nw4r::ut::Color fog = 0;
        J3DNativeState::setFog(GPU_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, fog);
        NativeShader::init(sMaterial, NEBULA_SHADER_FONT_TEXT, NativeShader::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        sMaterial.depth_test = NEBULA_NATIVE_KEPT;
        sMaterial.cull = NEBULA_NATIVE_KEPT;
        NativeShader::setColor(sMaterial, 0, min.r, min.g, min.b, min.a);
        NativeShader::setColor(sMaterial, 4, max.r, max.g, max.b, max.a);
        sMaterial.constants[8] = intensity ? 1.0f : 0.0f;
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

        void CharWriter::LoadTexture(const Glyph& glyph, GpuTexMapID slot) {
            LoadingTexture loadInfo;

            loadInfo.slot = slot;
            loadInfo.texture = glyph.pTexture;
            loadInfo.filter = mFilter;

            if (loadInfo != mLoadingTexture) {
                GpuTexObj tobj;

                J3DNativeTexture::init(&tobj, const_cast< void* >(glyph.pTexture), glyph.texWidth, glyph.texHeight, glyph.texFormat, GPU_CLAMP, GPU_CLAMP,
                             GPU_FALSE);
                J3DNativeTexture::initLOD(&tobj, mFilter.atSmall, mFilter.atLarge, 0, 0, 0, GPU_DISABLE, GPU_DISABLE, GPU_ANISO_1);
                NativeShader::setTexObj(sMaterial, slot - GPU_TEXMAP0, &tobj);
                NativeShader::send(sMaterial);

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
                    GpuTexFmt format = mFont->GetTextureFormat();

                    switch (format) {
                    case GPU_TF_I4:
                    case GPU_TF_I8:
                        SetupGXForI();
                        break;

                    case GPU_TF_IA4:
                    case GPU_TF_IA8:
                        SetupGXDefault();
                        break;

                    case GPU_TF_RGB565:
                    case GPU_TF_RGB5A3:
                    case GPU_TF_RGBA8:
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
            SetupMaterial(0x00000000UL, 0xFFFFFFFFUL, false);
            SetupVertexFormat();
        }

        void CharWriter::SetupGXForI() {
            SetupMaterial(0x00000000UL, 0xFFFFFFFFUL, true);
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
            mFilter.atSmall = atSmall ? GPU_LINEAR : GPU_NEAR;
            mFilter.atLarge = atLarge ? GPU_LINEAR : GPU_NEAR;
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

            LoadTexture(glyph, GPU_TEXMAP0);

            J3DNativeDraw::begin(GPU_QUADS, 4, NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0);
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
            SetupMaterial(min, max, false);
            SetupVertexFormat();
        }
    };  // namespace ut
};  // namespace nw4r
