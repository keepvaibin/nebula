#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphBase/J3DTevs.hpp"
#include <revolution/os/OSFastCast.h>

J3DSys j3dSys;

Mtx J3DSys::mCurrentMtx;
Vec J3DSys::mCurrentS;
Vec J3DSys::mParentS;

// The texture every map holds after a reset: 4x4 transparent black, a texture record (nebula_cooked_texture.h).
static struct {
    NebulaTexture header;
    u8 texels[4 * 4 * 2];
} sNullTexture ATTRIBUTE_ALIGN(32) = {NEBULA_TEXTURE_STATIC(4, 4, NEBULA_TEXTURE_LA8, 4 * 4 * 2), {0}};

J3DTexCoordScaleInfo J3DSys::sTexCoordScaleTable[8];

static Mtx j3dIdentityMtx = {
    1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
};

static Mtx23 IndMtx = {
    0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f,
};

u32 j3dDefaultViewNo;

static NebulaColor8 ColorBlack = {0x00, 0x00, 0x00, 0x00};

static NebulaColor8 ColorWhite = {0xFF, 0xFF, 0xFF, 0xFF};

J3DSys::J3DSys() {
    makeTexCoordTable();
    makeTevSwapTable();
    makeAlphaCmpTable();
    makeZModeTable();

    mFlags = 0;
    NebulaMtxIdentity(mViewMtx);
    mDrawMode = 1;
    mMaterialMode = 0;
    mModel = nullptr;
    mShape = nullptr;

    for (int i = 0; i < 2; i++)
        mDrawBuffer[i] = nullptr;

    mTexture = nullptr;
    mMatPacket = nullptr;
    mShapePacket = nullptr;
    mModelDrawMtx = nullptr;
    mModelNrmMtx = nullptr;
    mVtxPos = nullptr;
    mVtxNrm = nullptr;
    mVtxCol = nullptr;

    for (int i = 0; i < 8; i++) {
        sTexCoordScaleTable[i].field_0x00 = 1;
        sTexCoordScaleTable[i].field_0x02 = 1;
        sTexCoordScaleTable[i].field_0x04 = 0;
        sTexCoordScaleTable[i].field_0x06 = 0;
    }
}

// The model's draw and normal matrices `indx` into matrix slot `addr` (the next shape draw carries them).
void J3DSys::loadPosMtxIndx(int addr, u16 indx) const {
    J3DNativeShape::setPosition(addr, mModelDrawMtx[indx], indx + 1);
}

void J3DSys::loadNrmMtxIndx(int addr, u16 indx) const {
    J3DNativeShape::setNormal33(addr, mModelNrmMtx[indx], indx + 1);
}

void J3DSys::drawInit() {
    J3DNativeState::setCurrentMtx(GX_PNMTX0);
    J3DNativeState::setCullMode(GX_CULL_BACK);
    J3DNativeState::setCoPlanar(GX_FALSE);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setDither(GX_TRUE);
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    J3DNativeState::setZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);

    reinitTexture();

    for (u32 i = 0; i < 10; i++)
        J3DNativeShape::setTexture(i, j3dIdentityMtx, 3);

    Mtx23 indTexMtx = {
        {0.5f, 0.0f, 0.0f},
        {0.0f, 0.5f, 0.0f},
    };

    u8 i;
    for (i = 0; i < 3; i++)
        J3DNativeState::setIndTexMtx((GXIndTexMtxID)(GX_ITM_0 + i), indTexMtx, 1);

    J3DNativeState::setChanMatColor(GX_COLOR0A0, j3dDefaultColInfo);
    J3DNativeState::setChanMatColor(GX_COLOR1A1, j3dDefaultColInfo);

    for (i = 0; i < GX_MAX_TEVREG; i++)
        J3DNativeState::setTevKColor((GXTevKColorID)i, j3dDefaultTevKColor);

    for (i = 0; i < GX_MAX_TEVREG; i++)
        J3DNativeState::setTevColorS10((GXTevRegID)i, j3dDefaultTevColor);


}

void J3DSys::reinitGX() {
    J3DNativeState::setCullMode(GX_CULL_BACK);
    J3DNativeState::setCoPlanar(GX_FALSE);
    reinitLighting();
    reinitTransform();
    reinitTexture();
    reinitTevStages();
    reinitIndStages();
    reinitPixelProc();
}

void J3DSys::reinitLighting() {
    J3DNativeState::setChanAmbColor(GX_COLOR0A0, ColorBlack);
    J3DNativeState::setChanAmbColor(GX_COLOR1A1, ColorBlack);
    J3DNativeState::setChanMatColor(GX_COLOR0A0, ColorWhite);
    J3DNativeState::setChanMatColor(GX_COLOR1A1, ColorWhite);
}

void J3DSys::reinitTransform() {
    J3DNativeState::setCurrentMtx(GX_PNMTX0);
}

void J3DSys::reinitTexture() {
    J3DNativeTexture::registerImage(&sNullTexture, GX_TF_IA8, 4, 4, 1);
}

void J3DSys::reinitTevStages() {
    J3DNativeState::setTevColor(GX_TEVREG0, ColorWhite);
    J3DNativeState::setTevColor(GX_TEVREG1, ColorWhite);
    J3DNativeState::setTevColor(GX_TEVREG2, ColorWhite);
    J3DNativeState::setTevKColor(GX_KCOLOR0, ColorWhite);
    J3DNativeState::setTevKColor(GX_KCOLOR1, ColorWhite);
    J3DNativeState::setTevKColor(GX_KCOLOR2, ColorWhite);
    J3DNativeState::setTevKColor(GX_KCOLOR3, ColorWhite);

    J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
}

void J3DSys::reinitIndStages() {
    J3DNativeState::setIndTexMtx(GX_ITM_0, IndMtx, 1);
    J3DNativeState::setIndTexMtx(GX_ITM_1, IndMtx, 1);
    J3DNativeState::setIndTexMtx(GX_ITM_2, IndMtx, 1);
}

void J3DSys::reinitPixelProc() {
    J3DNativeState::setBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    J3DNativeState::setColorUpdate(GX_TRUE);
    // SMG1 change -- GX_FALSE to GX_TRUE
    J3DNativeState::setAlphaUpdate(GX_TRUE);
    J3DNativeState::setDither(GX_TRUE);
    J3DNativeState::setFog(GX_FOG_NONE, 0.0f, 1.0f, 0.1f, 1.0f, ColorBlack);
    J3DNativeState::setFogRangeAdj(GX_FALSE, 0, nullptr);
    J3DNativeState::setZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
}
