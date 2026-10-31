#include "JSystem/J2DGraph/J2DPicture.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "JSystem/J2DGraph/J2DScreen.hpp"
#include "JSystem/JUtility/JUTPalette.hpp"
#include <nebula_game_api.h>
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // The picture's TEV programs (nebula_game_api.h NebulaMaterialProgram), one per shape of its stages: the
    // textures' count (1-4), which of them take their alpha from the texture, the black and white remap, the
    // vertex colours. Each is registered with the host when first drawn; its key is its entry here.
    struct PictureProgram {
        NebulaMaterialProgram mProgram;
        bool mRegistered;
    };
    PictureProgram sPrograms[4 * 16 * 2 * 2];

    // A stage that adds its inputs (a + (b - a) c + d, no bias, scale 1, clamped) into the previous colour.
    void addStage(NebulaMaterialTevStage& rStage, u8 coord, u8 map, u8 channel, const u8 (&color)[4],
                  const u8 (&alpha)[4]) {
        rStage.tex_coord = coord;
        rStage.tex_map = map;
        rStage.channel = channel;
        for (u32 k = 0; k < 4; k++) {
            rStage.color_in[k] = color[k];
            rStage.alpha_in[k] = alpha[k];
        }
        rStage.color_op = rStage.alpha_op = GX_TEV_ADD;
        rStage.color_bias = rStage.alpha_bias = GX_TB_ZERO;
        rStage.color_scale = rStage.alpha_scale = GX_CS_SCALE_1;
        rStage.color_clamp = rStage.alpha_clamp = GX_TRUE;
        rStage.color_reg = rStage.alpha_reg = GX_TEVPREV;
    }
}  // namespace

J2DPicture::J2DPicture(JUTTexture* texture) {
    for (u32 i = 0; i < 4; i++) {
        mTexture[i] = NULL;
    }
    field_0x109 = 0;
    mTextureNum = 0;
    if (texture != NULL) {
        append(texture, 1.0f);
    }
    mPalette = NULL;
    initinfo();
}

void J2DPicture::initiate(const ResTIMG* img, const ResTLUT* lut) {
    private_initiate(img, lut);
    if (mTexture[0] == NULL) {
        return;
    }

    place(JGeometry::TBox2< f32 >(0.0f, 0.0f, mTexture[0]->getWidth(), mTexture[0]->getHeight()));
}

void J2DPicture::private_initiate(const ResTIMG* timg, const ResTLUT* tlut) {
    if (mTextureNum != 0) {
        return;
    }

    if (timg != NULL) {
        if (mTexture[0] == NULL) {
            mTexture[0] = new JUTTexture(timg, 0);
            if (mTexture[0] != NULL) {
                mTextureNum++;
                // SMG1 change -- mask of 2 to 0xE
                field_0x109 = (field_0x109 & 0xE) | 1;
            }
        } else {
            mTexture[0]->storeTIMG(timg, (u8)0);
            mTextureNum++;
            // SMG1 change -- mask of 2 to 0xE
            field_0x109 = (field_0x109 & 0xE) | 1;
        }
    }

    mPalette = NULL;
    if (tlut && mPalette == NULL) {
        mPalette = new JUTPalette(GX_TLUT0, const_cast< ResTLUT* >(tlut));
        if (mTexture[0] != NULL) {
            mTexture[0]->attachPalette(mPalette);
        }
    }
}

void J2DPicture::initinfo() {
    mKind = 'PIC1';
    setTexCoord(field_0x10a, NULL, BIND15, MIRROR0, false);
    setBlendRatio(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);

    mBlack = 0;
    mWhite = -1;
    setCornerColor(-1, -1, -1, -1);
}

J2DPicture::~J2DPicture() {
    for (int i = 0; i < 4; i++) {
        if ((int)(field_0x109 & 1 << i) != 0) {
            delete mTexture[i];
        }
    }

    delete mPalette;
}

bool J2DPicture::prepareTexture(u8 param_0) {
    for (u8 i = 0; i < param_0; i++) {
        if (i >= 4) {
            return 0;
        }

        if (mTexture[i] == NULL) {
            JUTTexture* tmp = new JUTTexture();
            mTexture[i] = tmp;

            if (mTexture[i] == NULL) {
                return 0;
            }
            field_0x109 |= (1 << i);
        }
    }
    return 1;
}

bool J2DPicture::insert(ResTIMG const* img, JUTPalette* palette, u8 param_2, f32 param_3) {
    if (img == NULL || mTextureNum >= 4 || param_2 >= 4 || param_2 > mTextureNum) {
        return false;
    }

    u8 var_r26 = 0;
    if (img->mPaletteName != 0 && palette == NULL) {
        var_r26 = getUsableTlut(mTextureNum);
    }

    JUTTexture* var_r31;
    if (mTexture[mTextureNum] == NULL) {
        var_r31 = new JUTTexture(img, var_r26);

        if (palette != NULL) {
            var_r31->storeTIMG(img, palette);
        }

        for (u8 i = 3; i > param_2; i--) {
            mTexture[i] = mTexture[i - 1];
            field_0x11c[i] = field_0x11c[i - 1];
            field_0x124[i] = field_0x124[i - 1];
        }

        field_0x109 = (field_0x109 & ((1 << param_2) - 1)) | ((field_0x109 & ~((1 << param_2) - 1)) * 2);
        field_0x109 |= (1 << param_2);
    } else {
        var_r31 = mTexture[mTextureNum];
        if (palette == NULL) {
            mTexture[mTextureNum]->storeTIMG(img, var_r26);
        } else {
            mTexture[mTextureNum]->storeTIMG(img, palette);
        }

        u8 sp8[4];
        for (u8 i = 0; i < 4; i++) {
            sp8[i] = (field_0x109 & (1 << i)) != 0;
        }

        for (u8 i = mTextureNum; i > param_2; i--) {
            mTexture[i] = mTexture[i - 1];
            sp8[i] = sp8[i - 1];
            field_0x11c[i] = field_0x11c[i - 1];
            field_0x124[i] = field_0x124[i - 1];
        }

        field_0x109 = 0;

        for (u8 i = 0; i < 4; i++) {
            if (sp8[i] != 0) {
                field_0x109 |= (1 << i);
            }
        }

        field_0x109 |= (1 << param_2);
    }

    mTexture[param_2] = var_r31;
    field_0x11c[param_2] = param_3;
    field_0x124[param_2] = param_3;

    if (mTextureNum == 0 && mTexture[0] != NULL) {
        place(JGeometry::TBox2< f32 >(0.0f, 0.0f, mTexture[0]->getWidth(), mTexture[0]->getHeight()));
        setTexCoord(field_0x10a, NULL, BIND15, MIRROR0, false);
    }

    mTextureNum++;
    setBlendKonstColor();
    setBlendKonstAlpha();
    return true;
}

bool J2DPicture::insert(char const* resName, JUTPalette* palette, u8 param_2, f32 param_3) {
    void* resource = J2DScreen::getNameResource(resName);
    return insert((ResTIMG*)resource, palette, param_2, param_3);
}

bool J2DPicture::insert(JUTTexture* texture, u8 param_1, f32 param_2) {
    if (texture == NULL || mTextureNum >= 4 || param_1 >= 4 || param_1 > mTextureNum) {
        return false;
    }

    if (mTexture[3] != NULL && field_0x109 & 8) {
        delete mTexture[3];
        field_0x109 &= 7;
    }

    for (u8 i = 3; i > param_1; i--) {
        mTexture[i] = mTexture[i - 1];
        field_0x11c[i] = field_0x11c[i - 1];
        field_0x124[i] = field_0x124[i - 1];
    }

    field_0x109 = (field_0x109 & ((1 << param_1) - 1)) | ((field_0x109 & ~((1 << param_1) - 1)) * 2);
    mTexture[param_1] = texture;
    field_0x109 &= ~(1 << param_1);

    field_0x11c[param_1] = param_2;
    field_0x124[param_1] = param_2;

    if (mTextureNum == 0 && &mTexture[0] != NULL) {
        place(JGeometry::TBox2< f32 >(0.0f, 0.0f, mTexture[0]->getWidth(), mTexture[0]->getHeight()));
        setTexCoord(field_0x10a, NULL, BIND15, MIRROR0, false);
    }

    mTextureNum++;
    setBlendKonstColor();
    setBlendKonstAlpha();
    return true;
}

bool J2DPicture::remove(u8 param_0) {
    if (mTextureNum <= param_0 || mTextureNum == 1) {
        return false;
    }

    if (field_0x109 & (1 << param_0)) {
        delete mTexture[param_0];
    }

    for (u8 i = param_0; i < mTextureNum - 1; i++) {
        mTexture[i] = mTexture[i + 1];
        field_0x11c[i] = field_0x11c[i + 1];
        field_0x124[i] = field_0x124[i + 1];
    }

    mTexture[mTextureNum - 1] = NULL;
    field_0x109 = (field_0x109 & ((1 << param_0) - 1)) | ((field_0x109 & ~((1 << (param_0 + 1)) - 1)) >> 1);

    mTextureNum--;
    setBlendKonstColor();
    setBlendKonstAlpha();
    return true;
}

bool J2DPicture::remove(JUTTexture* param_0) {
    u8 i = 0;
    for (; i < mTextureNum; i++) {
        if (mTexture[i] == param_0) {
            break;
        }
    }
    return remove(i);
}

const ResTIMG* J2DPicture::changeTexture(ResTIMG const* img, u8 texIndex) {
    if (texIndex > mTextureNum || 4 <= texIndex || img == NULL) {
        return NULL;
    }

    const ResTIMG* result;
    if (texIndex < mTextureNum) {
        JUTTexture* tex = getTexture(texIndex);
        result = tex->getTexInfo();

        u8 var_r30 = 0;
        if (img->mPaletteName != 0) {
            var_r30 = getUsableTlut(texIndex);
        }
        getTexture(texIndex)->storeTIMG(img, var_r30);
        return result;
    }

    append(img, 1.0f);
    return NULL;
}

const ResTIMG* J2DPicture::changeTexture(char const* resName, u8 param_1) {
    void* resource = J2DScreen::getNameResource(resName);
    return changeTexture((ResTIMG*)resource, param_1);
}

const ResTIMG* J2DPicture::changeTexture(ResTIMG const* img, u8 texIndex, JUTPalette* palette) {
    if (texIndex > mTextureNum || 4 <= texIndex || img == NULL) {
        return NULL;
    }

    const ResTIMG* result;
    if (texIndex < mTextureNum) {
        JUTTexture* tex = getTexture(texIndex);
        result = tex->getTexInfo();

        GXTlut var_r30 = GX_TLUT0;
        if (img->mPaletteName != 0) {
            var_r30 = getTlutID(img, getUsableTlut(texIndex));
        }
        getTexture(texIndex)->storeTIMG(img, palette, var_r30);
        return result;
    }

    append(img, palette, 1.0f);
    return NULL;
}

const ResTIMG* J2DPicture::changeTexture(char const* param_0, u8 param_1, JUTPalette* param_2) {
    void* resource = J2DScreen::getNameResource(param_0);
    return changeTexture((ResTIMG*)resource, param_1, param_2);
}

void J2DPicture::drawSelf(f32 param_0, f32 param_1) {
    Mtx tmp;
    NebulaMtxIdentity(tmp);

    drawSelf(param_0, param_1, &tmp);
}

void J2DPicture::drawSelf(f32 param_0, f32 param_1, Mtx* param_2) {
    if (mTexture[0] != NULL && mTextureNum != 0) {
        drawFullSet(mGlobalBounds.i.x + param_0, mGlobalBounds.i.y + param_1, getWidth(), getHeight(), param_2);
    }
}

void J2DPicture::drawFullSet(f32 param_0, f32 param_1, f32 param_2, f32 param_3, Mtx* param_4) {
    if (mTexture[0] != NULL && mTextureNum != 0) {
        drawTexCoord(mBounds.i.x, mBounds.i.y, param_2, param_3, field_0x10a[0].x, field_0x10a[0].y, field_0x10a[1].x, field_0x10a[1].y,
                     field_0x10a[2].x, field_0x10a[2].y, field_0x10a[3].x, field_0x10a[3].y, param_4);
    }
}

void J2DPicture::draw(f32 x, f32 y, f32 width, f32 height, bool mirrorX, bool mirrorY, bool rotate90) {
    if (isVisible() && mTextureNum != 0 && mTexture[0] != NULL) {
        for (u8 i = 0; i < mTextureNum; i++) {
            load(i);
        }

        J3DNativeState::setNumTexGens(mTextureNum);
        mColorAlpha = mAlpha;
        JUtility::TColor color[4];
        getNewColor(color);

        setTevMode();
        makeMatrix(x, y, 0.0f, 0.0f);
        J3DNativeShape::setPosition(0, mPositionMtx);
        J3DNativeState::setCurrentMtx(GX_PNMTX0);

        JGeometry::TVec2< s16 > coords[4];
        setTexCoord(coords, mTexture[0], BIND15, (J2DMirror)((mirrorX << 1) | mirrorY), rotate90);

        J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0);
        J3DNativeDraw::position(0.0f, 0.0f, 0.0f);
        J3DNativeDraw::color(color[0]);
        J3DNativeDraw::texCoord(static_cast< s16 >(coords[0].x) / 256.0f, static_cast< s16 >(coords[0].y) / 256.0f);

        J3DNativeDraw::position(width, 0.0f, 0.0f);
        J3DNativeDraw::color(color[1]);
        J3DNativeDraw::texCoord(static_cast< s16 >(coords[1].x) / 256.0f, static_cast< s16 >(coords[1].y) / 256.0f);

        J3DNativeDraw::position(width, height, 0.0f);
        J3DNativeDraw::color(color[3]);
        J3DNativeDraw::texCoord(static_cast< s16 >(coords[3].x) / 256.0f, static_cast< s16 >(coords[3].y) / 256.0f);

        J3DNativeDraw::position(0.0f, height, 0.0f);
        J3DNativeDraw::color(color[2]);
        J3DNativeDraw::texCoord(static_cast< s16 >(coords[2].x) / 256.0f, static_cast< s16 >(coords[2].y) / 256.0f);

        J3DNativeState::setNumTexGens(0);
        J3DNativeState::setNumTevStages(1);
        J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);

        Mtx m;
        NebulaMtxIdentity(m);
        J3DNativeShape::setPosition(0, m);
        J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    }
}

void J2DPicture::drawOut(JGeometry::TBox2< f32 > const& posBox, JGeometry::TBox2< f32 > const& texRect) {
    if (isVisible() && mTextureNum != 0 && mTexture[0] != NULL) {
        for (u8 i = 0; i < mTextureNum; i++) {
            load(i);
        }

        J3DNativeState::setNumTexGens(mTextureNum);

        f32 s0 = (posBox.i.x - texRect.i.x) / texRect.getWidth();
        f32 s1 = (posBox.f.x - texRect.f.x) / texRect.getWidth() + 1.0f;

        f32 t0 = (posBox.i.y - texRect.i.y) / texRect.getHeight();
        f32 t1 = (posBox.f.y - texRect.f.y) / texRect.getHeight() + 1.0f;

        mColorAlpha = mAlpha;
        JUtility::TColor color[4];
        getNewColor(color);
        setTevMode();

        J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0);
        J3DNativeDraw::position((s16)(s32)(posBox.i.x), (s16)(s32)(posBox.i.y), 0.0f);
        J3DNativeDraw::color(color[0]);
        J3DNativeDraw::texCoord(s0, t0);

        J3DNativeDraw::position((s16)(s32)(posBox.f.x), (s16)(s32)(posBox.i.y), 0.0f);
        J3DNativeDraw::color(color[1]);
        J3DNativeDraw::texCoord(s1, t0);

        J3DNativeDraw::position((s16)(s32)(posBox.f.x), (s16)(s32)(posBox.f.y), 0.0f);
        J3DNativeDraw::color(color[3]);
        J3DNativeDraw::texCoord(s1, t1);

        J3DNativeDraw::position((s16)(s32)(posBox.i.x), (s16)(s32)(posBox.f.y), 0.0f);
        J3DNativeDraw::color(color[2]);
        J3DNativeDraw::texCoord(s0, t1);

        J3DNativeState::setNumTexGens(0);
        J3DNativeState::setNumTevStages(1);
        J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    }
}

void J2DPicture::drawTexCoord(f32 param_0, f32 param_1, f32 param_2, f32 param_3, s16 param_4, s16 param_5, s16 param_6, s16 param_7, s16 param_8,
                              s16 param_9, s16 param_10, s16 param_11, Mtx* param_12) {
    if (mTextureNum != 0) {
        for (u8 i = 0; i < mTextureNum; i++) {
            load(i);
        }

        f32 tmp1 = param_0 + param_2;
        f32 tmp2 = param_1 + param_3;

        J3DNativeState::setNumTexGens(mTextureNum);

        JUtility::TColor black[4];

        Mtx outputMtx;

        getNewColor(black);
        setTevMode();
        NebulaMtxConcat((MtxPtr)param_12, mGlobalMtx, outputMtx);

        J3DNativeShape::setPosition(0, outputMtx);
        J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0);

        J3DNativeDraw::position(param_0, param_1, 0.0f);
        J3DNativeDraw::color(black[0]);
        J3DNativeDraw::texCoord(static_cast< s16 >(param_4) / 256.0f, static_cast< s16 >(param_5) / 256.0f);

        J3DNativeDraw::position(tmp1, param_1, 0.0f);
        J3DNativeDraw::color(black[1]);
        J3DNativeDraw::texCoord(static_cast< s16 >(param_6) / 256.0f, static_cast< s16 >(param_7) / 256.0f);

        J3DNativeDraw::position(tmp1, tmp2, 0.0f);
        J3DNativeDraw::color(black[3]);
        J3DNativeDraw::texCoord(static_cast< s16 >(param_10) / 256.0f, static_cast< s16 >(param_11) / 256.0f);

        J3DNativeDraw::position(param_0, tmp2, 0.0f);
        J3DNativeDraw::color(black[2]);
        J3DNativeDraw::texCoord(static_cast< s16 >(param_8) / 256.0f, static_cast< s16 >(param_9) / 256.0f);


    }
}

// The picture's stages as a native material: texture 0's colour (its alpha, or register 2's), each further texture
// blended in by its konst colour and alpha (konst 0 and 2, by the selections the stage's place names), then the black
// and white remap and the vertex colours when they change anything. The registers and konst colours stay state.
void J2DPicture::setTevMode() {
    const bool remap = mBlack != 0 || mWhite != 0xFFFFFFFF;
    const bool vertexColours = mColorAlpha != 0xFF || mCornerColor[0] != 0xFFFFFFFF || mCornerColor[1] != 0xFFFFFFFF ||
                               mCornerColor[2] != 0xFFFFFFFF || mCornerColor[3] != 0xFFFFFFFF;
    u32 textureAlpha = 0;
    for (u8 i = 0; i < mTextureNum; i++) {
        textureAlpha |= mTexture[i] != NULL && mTexture[i]->getTransparency() ? 1u << i : 0u;
    }
    const u32 shape = (((static_cast< u32 >(mTextureNum - 1) & 3u) * 16u + textureAlpha) * 2u + (remap ? 1u : 0u)) * 2u +
                      (vertexColours ? 1u : 0u);
    PictureProgram& rEntry = sPrograms[shape];
    if (!rEntry.mRegistered) {
        NebulaMaterialProgram& rProgram = rEntry.mProgram;
        memset(&rProgram, 0, sizeof(rProgram));
        // Stages that read no konst colour keep the selections J3D's drawing state starts with.
        for (u32 s = 0; s < 16; s++) {
            rProgram.tev[s].konst_color = GX_TEV_KCSEL_1_4;
            rProgram.tev[s].konst_alpha = GX_TEV_KASEL_1;
        }
        u8 i = 0;
        for (; i < mTextureNum; i++) {
            const u8 alpha = (textureAlpha & (1u << i)) != 0 ? GX_CA_TEXA : GX_CA_A2;
            if (i == 0) {
                const u8 color[4] = {GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO};
                const u8 alphaIn[4] = {alpha, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO};
                addStage(rProgram.tev[i], i, i, GX_COLOR_NULL, color, alphaIn);
            } else {
                const u8 color[4] = {GX_CC_CPREV, GX_CC_TEXC, GX_CC_KONST, GX_CC_ZERO};
                const u8 alphaIn[4] = {GX_CA_APREV, alpha, GX_CA_KONST, GX_CA_ZERO};
                addStage(rProgram.tev[i], i, i, GX_COLOR_NULL, color, alphaIn);
                rProgram.tev[i].konst_color = static_cast< u8 >(0x20 - i * 4);
                rProgram.tev[i].konst_alpha = static_cast< u8 >(0x22 - i * 4);
            }
        }
        if (remap) {
            const u8 color[4] = {GX_CC_C0, GX_CC_C1, GX_CC_CPREV, GX_CC_ZERO};
            const u8 alphaIn[4] = {GX_CA_A0, GX_CA_A1, GX_CA_APREV, GX_CA_ZERO};
            addStage(rProgram.tev[i++], GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL, color, alphaIn);
        }
        if (vertexColours) {
            const u8 color[4] = {GX_CC_ZERO, GX_CC_CPREV, GX_CC_RASC, GX_CC_ZERO};
            const u8 alphaIn[4] = {GX_CA_ZERO, GX_CA_APREV, GX_CA_RASA, GX_CA_ZERO};
            addStage(rProgram.tev[i++], GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0, color, alphaIn);
        }
        // The swap tables as J3D's drawing state starts them.
        static const u8 kSwap[4][4] = {{GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA},
                                       {GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA},
                                       {GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA},
                                       {GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA}};
        memcpy(rProgram.swap_table, kSwap, sizeof(kSwap));
        rProgram.tev_stages = i;
        rProgram.tex_gens = mTextureNum;
        rProgram.channels = 1;
        rProgram.cull = GX_CULL_NONE;
        rProgram.loads = NEBULA_MATERIAL_LOADS_TEV | NEBULA_MATERIAL_LOADS_KONST_SELECT;
        for (u8 t = 0; t < mTextureNum; t++) {
            rProgram.tex_gen[t].type = GX_TG_MTX2x4;
            rProgram.tex_gen[t].source = GX_TG_TEX0;
            rProgram.tex_gen[t].matrix = GX_IDENTITY;
            rProgram.tex_gen[t].post_matrix = GX_PTIDENTITY;
        }
        // J2DGrafContext::setup2D's alpha test and depth, the picture's blend.
        rProgram.alpha_compare.comp0 = rProgram.alpha_compare.comp1 = GX_GREATER;
        rProgram.alpha_compare.op = GX_AOP_OR;
        rProgram.blend.type = GX_BM_BLEND;
        rProgram.blend.source = GX_BL_SRCALPHA;
        rProgram.blend.destination = GX_BL_INVSRCALPHA;
        rProgram.blend.logic_op = GX_LO_SET;
        rProgram.z_mode.enable = GX_FALSE;
        rProgram.z_mode.func = GX_LEQUAL;
        rProgram.z_mode.update = GX_FALSE;
        const NebulaHostServices* pHost = nebula_host();
        pHost->material_program(pHost->user, reinterpret_cast< uintptr_t >(&rEntry), &rProgram);
        rEntry.mRegistered = true;
    }

    // Values: each texture's coordinate scale (its size, as GX derives it for a normalized coordinate).
    NebulaMaterialValues values;
    memset(&values, 0, sizeof(values));
    for (u8 t = 0; t < mTextureNum; t++) {
        if (mTexture[t] != NULL) {
            values.tex_coord_scale[t].scale_s = mTexture[t]->getWidth();
            values.tex_coord_scale[t].scale_t = mTexture[t]->getHeight();
        }
    }
    J3DNativeShape::flushState();
    const NebulaHostServices* pHost = nebula_host();
    pHost->material(pHost->user, reinterpret_cast< uintptr_t >(&rEntry), &values);
    // The coordinates' texture matrices: none (the identity's place), as GXSetTexCoordGen2's matrix sets them.
    static const u8 kIdentity[8] = {GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY,
                                    GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY};
    J3DNativeShape::setCurrent(GX_PNMTX0, kIdentity);

    J3DNativeState::setTevColor(GX_TEVREG2, JUtility::TColor(0xFFFFFFFF));
    J3DNativeState::setTevKColor(GX_KCOLOR0, mBlendKonstColor);
    J3DNativeState::setTevKColor(GX_KCOLOR2, mBlendKonstAlpha);
    if (remap) {
        J3DNativeState::setTevColor(GX_TEVREG0, mBlack);
        J3DNativeState::setTevColor(GX_TEVREG1, mWhite);
    }
    J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    for (int i = GX_TEVSTAGE0; i < GX_MAX_TEVSTAGE; i++) {
        J3DNativeState::setTevDirect((GXTevStageID)i);
    }
}

void J2DPicture::swap(f32& lhs, f32& rhs) {
    f32 tmp = lhs;
    lhs = rhs;
    rhs = tmp;
}

/* NEW IN SMG1 */
void J2DPicture::setBlendColorRatio(f32 a1, f32 a2, f32 a3, f32 a4, f32 a5, f32 a6, f32 a7, f32 a8) {
    field_0x11c[0] = a1;
    field_0x11c[1] = a2;
    field_0x11c[2] = a3;
    field_0x11c[3] = a4;
    setBlendKonstColor();
}

/* NEW IN SMG1 */
void J2DPicture::setBlendAlphaRatio(f32 a1, f32 a2, f32 a3, f32 a4, f32 a5, f32 a6, f32 a7, f32 a8) {
    field_0x124[0] = a1;
    field_0x124[1] = a2;
    field_0x124[2] = a3;
    field_0x124[3] = a4;
    setBlendKonstAlpha();
}

void J2DPicture::setBlendKonstColor() {
    int uvar3 = 0;
    for (u8 i = 1; i < mTextureNum; i++) {
        f32 tmp = 0.0f;
        for (u8 j = 0; j < i; j++) {
            tmp += field_0x11c[j];
        }

        if (tmp + field_0x11c[i] != 0.0f) {
            f32 value = 255.0f * (1.0f - tmp / (tmp + field_0x11c[i]));
            uvar3 |= static_cast< u8 >((s32)(value)) << (i - 1) * 8;
        }
    }
    mBlendKonstColor = uvar3;
}

void J2DPicture::setBlendKonstAlpha() {
    int uvar3 = 0;
    for (u8 i = 1; i < mTextureNum; i++) {
        f32 tmp = 0.0f;
        for (u8 j = 0; j < i; j++) {
            tmp += field_0x124[j];
        }

        if (tmp + field_0x124[i] != 0.0f) {
            f32 value = 255.0f * (1.0f - tmp / (tmp + field_0x124[i]));
            uvar3 |= static_cast< u8 >((s32)(value)) << (i - 1) * 8;
        }
    }
    mBlendKonstAlpha = uvar3;
}

void J2DPicture::getNewColor(JUtility::TColor* param_0) {
    param_0[0] = mCornerColor[0];
    param_0[1] = mCornerColor[1];
    param_0[2] = mCornerColor[2];
    param_0[3] = mCornerColor[3];

    if (mColorAlpha != 0xFF) {
        param_0[0].a = (param_0[0].a * mColorAlpha) / 0xFF;
        param_0[1].a = (param_0[1].a * mColorAlpha) / 0xFF;
        param_0[2].a = (param_0[2].a * mColorAlpha) / 0xFF;
        param_0[3].a = (param_0[3].a * mColorAlpha) / 0xFF;
    }
}

void J2DPicture::setTexCoord(JUTTexture const* param_0, J2DBinding param_1, J2DMirror param_2, bool param_3) {
    setTexCoord(&field_0x10a[0], param_0, param_1, param_2, param_3);
}

void J2DPicture::setTexCoord(JGeometry::TVec2< s16 >* param_0, JUTTexture const* param_1, J2DBinding binding, J2DMirror mirror, bool rotate90) {
    bool bindLeft;
    bool bindRight;
    bool bindTop;
    bool bindBottom;

    if (!rotate90) {
        if (mirror & J2DMirror_X)
            bindLeft = binding & J2DBind_Right;
        else
            bindLeft = binding & J2DBind_Left;

        if (mirror & J2DMirror_X)
            bindRight = binding & J2DBind_Left;
        else
            bindRight = binding & J2DBind_Right;

        if (mirror & J2DMirror_Y)
            bindTop = binding & J2DBind_Bottom;
        else
            bindTop = binding & J2DBind_Top;

        if (mirror & J2DMirror_Y)
            bindBottom = binding & J2DBind_Top;
        else
            bindBottom = binding & J2DBind_Bottom;
    } else {
        if (mirror & J2DMirror_X)
            bindLeft = binding & J2DBind_Bottom;
        else
            bindLeft = binding & J2DBind_Top;

        if (mirror & J2DMirror_X)
            bindRight = binding & J2DBind_Top;
        else
            bindRight = binding & J2DBind_Bottom;

        if (mirror & J2DMirror_Y)
            bindTop = binding & J2DBind_Left;
        else
            bindTop = binding & J2DBind_Right;

        if (mirror & J2DMirror_Y)
            bindBottom = binding & J2DBind_Right;
        else
            bindBottom = binding & J2DBind_Left;
    }

    f32 rectWidth;
    f32 rectHeight;

    rectWidth = !rotate90 ? getWidth() : getHeight();
    rectHeight = !rotate90 ? getHeight() : getWidth();

    f32 texWidth;
    f32 texHeight;
    if (param_1 == NULL) {
        texWidth = rectWidth;
        texHeight = rectHeight;
    } else {
        texWidth = param_1->getWidth();
        texHeight = param_1->getHeight();
    }

    f32 s0, t0, s1, t1;
    if (bindLeft) {
        s0 = 0.0f;
        s1 = bindRight ? 1.0f : (rectWidth / texWidth);
    } else if (bindRight) {
        s0 = 1.0f - (rectWidth / texWidth);
        s1 = 1.0f;
    } else {
        s0 = 0.5f - (rectWidth / texWidth) / 2.0f;
        s1 = 0.5f + (rectWidth / texWidth) / 2.0f;
    }

    if (bindTop) {
        t0 = 0.0f;
        t1 = bindBottom ? 1.0f : (rectHeight / texHeight);
    } else if (bindBottom) {
        t0 = 1.0f - (rectHeight / texHeight);
        t1 = 1.0f;
    } else {
        t0 = 0.5f - (rectHeight / texHeight) / 2.0f;
        t1 = 0.5f + (rectHeight / texHeight) / 2.0f;
    }

    if (mirror & J2DMirror_X) {
        swap(s0, s1);
    }
    if (mirror & J2DMirror_Y) {
        swap(t0, t1);
    }

    s16 temp_r27 = J2DCast_F32_to_S16(s0, 8);
    s16 temp_r28 = J2DCast_F32_to_S16(s1, 8);
    s16 temp_r30 = J2DCast_F32_to_S16(t0, 8);
    s16 temp_r31 = J2DCast_F32_to_S16(t1, 8);

    if (!rotate90) {
        param_0[0].set(temp_r27, temp_r30);
        param_0[1].set(temp_r28, temp_r30);
        param_0[2].set(temp_r27, temp_r31);
        param_0[3].set(temp_r28, temp_r31);
    } else {
        param_0[0].set(temp_r27, temp_r31);
        param_0[1].set(temp_r27, temp_r30);
        param_0[2].set(temp_r28, temp_r31);
        param_0[3].set(temp_r28, temp_r30);
    }
}

bool J2DPicture::isUsed(ResTIMG const* param_0) {
    for (u8 i = 0; i < mTextureNum; i++) {
        if (mTexture[i] != NULL && mTexture[i]->getTexInfo() == param_0) {
            return true;
        }
    }

    return J2DPane::isUsed(param_0);
}

u8 J2DPicture::getUsableTlut(u8 param_0) {
    u8 var_r8 = 0;

    for (u8 i = 0; i < mTextureNum; i++) {
        if (i != param_0 && mTexture[i] != NULL) {
            const ResTIMG* img = mTexture[i]->getTexInfo();

            if (img != NULL && img->mPaletteName != 0) {
                int name = mTexture[i]->getTlutName();
                int var_r0 = name >= GX_BIGTLUT0 ? GX_BIGTLUT0 : GX_TLUT0;

                u8 temp_r0 = name - var_r0;
                if (temp_r0 < 4) {
                    var_r8 |= (1 << temp_r0);
                }
            }
        }
    }

    u8 var_r26 = 0;
    for (u8 i = 0; i < 4; i++) {
        if (!(var_r8 & (1 << i))) {
            var_r26 = i;
            break;
        }
    }

    return var_r26;
}

GXTlut J2DPicture::getTlutID(ResTIMG const* img, u8 param_1) {
    if (img->mPaletteNum > 256) {
        return (GXTlut)((param_1 % 4) + 0x10);
    }

    return (GXTlut)param_1;
}

void J2DPicture::setCornerColor(JUtility::TColor c0, JUtility::TColor c1, JUtility::TColor c2, JUtility::TColor c3) {
    mCornerColor[0] = c0;
    mCornerColor[1] = c1;
    mCornerColor[2] = c2;
    mCornerColor[3] = c3;
}
