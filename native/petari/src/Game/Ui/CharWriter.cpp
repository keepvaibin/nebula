#include "Game/Ui/CharWriter.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/Screen/NebulaIcons.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Ui/UiCommon.hpp"
#include "Game/Ui/Font.hpp"
#include "Game/Util/Array.hpp"

namespace {
    // The writer's native material (runtime/src/render/materials/font_text.hlsl) as Setup set it, with the glyph sheet
    // LoadTexture binds. Blended by alpha, no depth, no culling.
    SiteMaterial sMaterial;

    // The run of glyphs not yet sent: one sheet, one material, one matrix, one instance per glyph
    // (runtime/src/render/materials/glyph_instances.hlsl). A glyph's quad blends over the ones before it in the run as it
    // would over a draw before it, so a run draws what a draw a glyph drew.
    MR::GrowBuffer< NebulaGlyphInstance > sRun;
    u32 sRunGlyphs;
    f32 sRunMtx[3][4];

    void SendRun() {
        if (sRunGlyphs == 0) {
            return;
        }
        u32 first;
        void* pRows = FrameRing::instanceRows(sRunGlyphs * (sizeof(NebulaGlyphInstance) / 16), &first);
        memcpy(pRows, sRun.mItems, sRunGlyphs * sizeof(NebulaGlyphInstance));
        TypedDraw site = TypedDraw::eyeSpace(sMaterial, sRunMtx);
        site.drawGlyphs(first, sRunGlyphs);
        sRunGlyphs = 0;
    }

    // The next glyph's instance; the run is sent first when the matrix moved. The glyph's plane z is the
    // matrix's: its translation moved along the third column (z 0 leaves it as it is).
    NebulaGlyphInstance* RunGlyph(const f32 (*pMtx)[4], f32 z) {
        f32 mtx[3][4];
        for (u32 r = 0; r < 3; r++) {
            mtx[r][0] = pMtx[r][0];
            mtx[r][1] = pMtx[r][1];
            mtx[r][2] = pMtx[r][2];
            mtx[r][3] = pMtx[r][3] + pMtx[r][2] * z;
        }
        if (sRunGlyphs != 0) {
            bool same = true;
            for (u32 i = 0; same && i < 12; i++) {
                same = __builtin_bit_cast(u32, (&mtx[0][0])[i]) == __builtin_bit_cast(u32, (&sRunMtx[0][0])[i]);
            }
            if (!same) {
                SendRun();
            }
        }
        if (sRunGlyphs == 0) {
            memcpy(sRunMtx, mtx, sizeof(sRunMtx));
        }
        NebulaGlyphInstance* const pRun = sRun.room(sRunGlyphs + 1);
        return &pRun[sRunGlyphs++];
    }

    u32 ColorWord(Color8 color) {
        return static_cast< u32 >(color.r) << 24 | static_cast< u32 >(color.g) << 16 | static_cast< u32 >(color.b) << 8 | color.a;
    }

    // `range`: the font's distance range in sheet texels, 0 for sheets that are images.
    void SetupMaterial(Color8 min, Color8 max, bool intensity, u8 range) {
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

    // The material as Setup chooses it for a font whose sheets are the glyph's (a mod font's fallback glyph comes from
    // another font): when it differs from the run's, the run so far is sent first. The game's fonts never differ.
    void UseSheet(const Ui::Glyph& glyph, bool mapped) {
        bool intensity = false;
        u8 range = glyph.fieldRange;
        if (!mapped) {
            switch (glyph.texFormat) {
            case Ui::SHEET_I4:
            case Ui::SHEET_I8:
                intensity = true;
                break;
            case Ui::SHEET_RGB565:
            case Ui::SHEET_RGB5A3:
            case Ui::SHEET_RGBA8:
                range = 0;
                break;
            default:
                break;
            }
        }
        const f32 wantIntensity = intensity ? 1.0f : 0.0f;
        const f32 wantRange = static_cast< f32 >(range);
        if (sMaterial.constants[8] != wantIntensity || sMaterial.constants[9] != wantRange) {
            SendRun();
            sMaterial.constants[8] = wantIntensity;
            sMaterial.constants[9] = wantRange;
        }
    }
};  // namespace

namespace Ui {
    CharWriter::LoadingTexture CharWriter::mLoadingTexture;

    CharWriter::CharWriter() : mAlpha(255), mIsWidthFixed(false), mFixedWidth(0), mFont(NULL) {
        mLoadingTexture.Reset();
        ResetColorMapping();
        SetGradationMode(GRADMODE_NONE);
        SetTextColor(0xFFFFFFFFu);
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
        const f32 posTop = y + glyph.lift * mScale.y;
        const f32 posRight = posLeft + glyph.widths.glyphWidth * glyph.scale * mScale.x;
        const f32 posBottom = posTop + glyph.height * glyph.scale * mScale.y;
        const f32 posZ = z;
        // The cell's corners on its sheet, in the sheet's own size (source pixels: a distance sheet's texels are
        // finer, its coordinates the same).
        const f32 sheetWidth = static_cast< f32 >(glyph.texWidth);
        const f32 sheetHeight = static_cast< f32 >(glyph.texHeight);

        UseSheet(glyph, (mColorMapping.min != 0x00000000u) || (mColorMapping.max != 0xFFFFFFFFu));
        LoadTexture(glyph, 0);

        NebulaGlyphInstance* pGlyph = RunGlyph(detail::GetDrawMtx(), posZ);
        pGlyph->left = posLeft;
        pGlyph->top = posTop;
        pGlyph->right = posRight;
        pGlyph->bottom = posBottom;
        pGlyph->s0 = glyph.cellX / sheetWidth;
        pGlyph->t0 = glyph.cellY / sheetHeight;
        pGlyph->s1 = (glyph.cellX + glyph.widths.glyphWidth) / sheetWidth;
        pGlyph->t1 = (glyph.cellY + glyph.height) / sheetHeight;
        pGlyph->colors[0] = ColorWord(mVertexColor.lu);
        pGlyph->colors[1] = ColorWord(mVertexColor.ru);
        pGlyph->colors[2] = ColorWord(mVertexColor.rd);
        pGlyph->colors[3] = ColorWord(mVertexColor.ld);
    }

    void CharWriter::UpdateVertexColor() {
        mVertexColor.lu = mTextColor.start;
        mVertexColor.ru = (mTextColor.gradationMode != GRADMODE_H) ? mTextColor.start : mTextColor.end;
        mVertexColor.ld = (mTextColor.gradationMode != GRADMODE_V) ? mTextColor.start : mTextColor.end;
        mVertexColor.rd = (mTextColor.gradationMode == GRADMODE_NONE) ? mTextColor.start : mTextColor.end;

        mVertexColor.lu.a = static_cast< u8 >(mVertexColor.lu.a * mAlpha / 255);
        mVertexColor.ru.a = static_cast< u8 >(mVertexColor.ru.a * mAlpha / 255);
        mVertexColor.ld.a = static_cast< u8 >(mVertexColor.ld.a * mAlpha / 255);
        mVertexColor.rd.a = static_cast< u8 >(mVertexColor.rd.a * mAlpha / 255);
    }

    void CharWriter::SetupWithColorMapping(Color8 min, Color8 max) {
        SetupMaterial(min, max, false, FieldRange());
        SetupVertexFormat();
    }
}  // namespace Ui
