#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphBase/J3DTevs.hpp"

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
    {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f},
};


u32 j3dDefaultViewNo;

static NebulaColor8 ColorBlack = {0x00, 0x00, 0x00, 0x00};


J3DSys::J3DSys() {

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
    J3DPass::positionMatrix(0);
    J3DPass::cull(NEBULA_CULL_BACK);
    J3DPass::writeColor(true);
    J3DPass::blend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
    J3DPass::depth(true, NEBULA_COMPARE_LEQUAL, true);

    reinitTexture();

    for (u32 i = 0; i < 10; i++)
        J3DNativeShape::setTexture(i, j3dIdentityMtx, 3);



}

void J3DSys::resetDrawState() {
    J3DPass::cull(NEBULA_CULL_BACK);
    reinitLighting();
    reinitTransform();
    reinitTexture();
    reinitPixelProc();
}

void J3DSys::reinitLighting() {
    J3DNativeMaterial::setLightAmbient(0, ColorBlack);
    J3DNativeMaterial::setLightAmbient(1, ColorBlack);
}

void J3DSys::reinitTransform() {
    J3DPass::positionMatrix(0);
}

void J3DSys::reinitTexture() {
    J3DNativeTexture::registerRecord(&sNullTexture);
}

void J3DSys::reinitPixelProc() {
    J3DPass::noBlend();
    J3DPass::writeColor(true);
    // SMG1 change -- false to true
    J3DPass::writeAlpha(true);
    J3DPass::depth(true, NEBULA_COMPARE_LEQUAL, true);
}
