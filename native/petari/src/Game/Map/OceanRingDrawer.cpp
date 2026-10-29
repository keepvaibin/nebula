#include "Game/Map/OceanRingDrawer.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Map/OceanRing.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SchedulerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/JKernel/JKRHeap.hpp>
#include <revolution/gx/GXVert.h>

namespace {
    const f32 sPointNumInPart = 0.0f;
    const f32 sDistancePartDL = 13000.0f;
    const f32 sFlowSpeedTexRate = 0.0f;
    const f32 sFlowSpeedTexRateMin = 0.0f;
    const f32 sTexRate0 = 0.0f;
    const f32 sTexRate1 = 0.0f;
    const f32 sTexRate2 = 0.0f;
    const f32 sTexSpeed0U = -0.003f;
    const f32 sTexSpeed0V = -0.001f;
    const f32 sTexSpeed1U = -0.002f;
    const f32 sTexSpeed1V = 0.001f;
    const f32 sTexSpeed2V = 0.003f;
    const f32 sIndirectScale = 0.1f;
    const f32 sBloomCameraOffsetZ = 3000.0f;
    const f32 sDistancePartDrawBloom = 4000.0f;
    const f32 sBloomCameraUpMin = 0.0f;
    const f32 sBloomCameraUpMax = 0.0f;
    const f32 sBloomCameraDepthMin = 0.0f;
    const f32 sBloomCameraDepthMax = 0.0f;

    static GXColor color1 = {0x28, 0x28, 0x28, 0x14};
    static GXColor color2 = {0x76, 0xD7, 0xFF, 0xFF};
    static u8 unknownVal = 1;
};  // namespace

OceanRingPartDrawer::OceanRingPartDrawer(const OceanRing* pRing, int a3, int a4, bool a5, f32* pTexCoordU0, f32* pTexCoordU1, f32* pTexCoordU2)
    : mPosition(0.0f, 0.0f, 0.0f) {
    mOceanRing = pRing;
    _10 = a3;
    _14 = a4;
    _18 = a5;
    _1C = *pTexCoordU0;
    _20 = *pTexCoordU1;
    _24 = *pTexCoordU2;
    initDisplayList(pTexCoordU0, pTexCoordU1, pTexCoordU2);

    for (int i = 0; i < a4; i++) {
        int v12 = a3 + i;

        if (i == a4 - 1 && _18) {
            v12 = 0;
        }

        mPosition.add(((mOceanRing->mWaterPoints[(v12 * mOceanRing->mStride - 1) + mOceanRing->mStride / 2])->mOrigPos));
    }

    mPosition.scale(1.0f / a4);
}

void OceanRingPartDrawer::initDisplayList(f32* pTexCoordU0, f32* pTexCoordU1, f32* pTexCoordU2) {
    mMesh.init(NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0 | NEBULA_SHAPE_TEXCOORD0 << 1 | NEBULA_SHAPE_TEXCOORD0 << 2, 2 * mOceanRing->mStride * (_14 - 1));
    drawGD(pTexCoordU0, pTexCoordU1, pTexCoordU2);
    mMesh.send();
}

void OceanRingPartDrawer::draw() const {
    if (mPosition.distance(*MR::getPlayerPos()) < ::sDistancePartDL) {
        drawDynamic();
    } else {
        mMesh.draw();
    }
}

void OceanRingPartDrawer::drawGD(f32* pTexCoordU0, f32* pTexCoordU1, f32* pTexCoordU2) {
    f32 f30 = 0.05f;
    f32 f29;
    f32 f28;
    f32 f27;
    f32 f26;
    f32 f25;
    f32 f24;
    f32 f23;
    f32 f22;
    f32 f21 = 1.0f;

    f28 = f30 * f21;
    f29 = 0.1f * f21;
    f27 = _1C;
    f25 = _20;
    f23 = _24;
    f26 = _1C;
    f24 = _20;
    f22 = _24;
    for (s32 i = 0; i < _14 - 1; i++) {
        s32 index = _10 + i;
        s32 indexPlusOne = index + 1;
        if (i == _14 - 2 && _18) {
            indexPlusOne = 0;
        }

        mOceanRing->getPoint(0, index);
        mOceanRing->getPoint(0, indexPlusOne);

        f26 += f28;
        f24 += f28;
        f22 += f29;
        f32 f20 = 0.0f;
        f32 f19 = 0.0f;
        f32 f18 = 0.0f;
        f32 f17 = 0.0f;
        f32 f16 = 0.0f;
        f32 f15 = 0.0f;
        mMesh.begin(GX_TRIANGLESTRIP, (u16)mOceanRing->mStride << 1);

        for (s32 j = 0; j < mOceanRing->mStride; j++) {
            WaterPoint* pPoint = mOceanRing->getPoint(j, index);
            mMesh.position(pPoint->mPosition.x, pPoint->mPosition.y, pPoint->mPosition.z);
            mMesh.color(0xFF, 0xFF, 0xFF, pPoint->mAlpha);
            mMesh.texCoord(0, f27, f17);
            mMesh.texCoord(1, f25, f16);
            mMesh.texCoord(2, f23, f15);
            mMesh.corner();

            pPoint = mOceanRing->getPoint(j, indexPlusOne);
            mMesh.position(pPoint->mPosition.x, pPoint->mPosition.y, pPoint->mPosition.z);
            mMesh.color(0xFF, 0xFF, 0xFF, pPoint->mAlpha);
            mMesh.texCoord(0, f26, f20);
            mMesh.texCoord(1, f24, f19);
            mMesh.texCoord(2, f22, f18);
            mMesh.corner();

            f17 += f28;
            f16 += f28;
            f15 += f29;
            f20 += f28;
            f19 += f28;
            f18 += f29;
        }

        f27 = f26;
        f25 = f24;
        f23 = f22;
        if (i != _14 - 1) {
            *pTexCoordU0 += 0.05f * f21;
            *pTexCoordU1 += 0.05f * f21;
            *pTexCoordU2 += 0.1f * f21;
        }
    }
}

void OceanRingPartDrawer::drawDynamic() const {
    f32 f30 = 0.05f;
    f32 f29;
    f32 f28;
    f32 f27;
    f32 f26;
    f32 f25;
    f32 f24;
    f32 f23;
    f32 f22;
    f32 f21 = 1.0f;

    f28 = f30 * f21;
    f29 = 0.1f * f21;
    f27 = _1C;
    f25 = _20;
    f23 = _24;
    f26 = _1C;
    f24 = _20;
    f22 = _24;
    for (s32 i = 0; i < _14 - 1; i++) {
        s32 index = _10 + i;
        s32 indexPlusOne = index + 1;
        if (i == _14 - 2 && _18) {
            indexPlusOne = 0;
        }

        mOceanRing->getPoint(0, index);
        mOceanRing->getPoint(0, indexPlusOne);

        f26 += f28;
        f24 += f28;
        f22 += f29;
        f32 f20 = 0.0f;
        f32 f19 = 0.0f;
        f32 f18 = 0.0f;
        f32 f17 = 0.0f;
        f32 f16 = 0.0f;
        f32 f15 = 0.0f;

        J3DNativeDraw::begin(GX_TRIANGLESTRIP, mOceanRing->mStride * 2, NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0 | NEBULA_SHAPE_TEXCOORD0 << 1 | NEBULA_SHAPE_TEXCOORD0 << 2);
        for (s32 j = 0; j < mOceanRing->mStride; j++) {
            WaterPoint* pPoint = mOceanRing->getPoint(j, index);
            J3DNativeDraw::position(pPoint->mPosition.x, pPoint->mPosition.y, pPoint->mPosition.z);
            J3DNativeDraw::color(0xFF, 0xFF, 0xFF, pPoint->mAlpha);
            J3DNativeDraw::texCoord(f27, f17);
            J3DNativeDraw::texCoord(f25, f16);
            J3DNativeDraw::texCoord(f23, f15);

            pPoint = mOceanRing->getPoint(j, indexPlusOne);
            J3DNativeDraw::position(pPoint->mPosition.x, pPoint->mPosition.y, pPoint->mPosition.z);
            J3DNativeDraw::color(0xFF, 0xFF, 0xFF, pPoint->mAlpha);
            J3DNativeDraw::texCoord(f26, f20);
            J3DNativeDraw::texCoord(f24, f19);
            J3DNativeDraw::texCoord(f22, f18);

            f17 += f28;
            f16 += f28;
            f15 += f29;
            f20 += f28;
            f19 += f28;
            f18 += f29;
        }


        f27 = f26;
        f25 = f24;
        f23 = f22;
    }
}

void OceanRingPartDrawer::drawDynamicBloom() const {
    f32 f30 = 0.05f;
    f32 f29;
    f32 f28;
    f32 f27;
    f32 f26;
    f32 f25;
    f32 f24;
    f32 f21 = 1.0f;

    f28 = f30 * f21;
    f29 = 0.1f * f21;
    f27 = _1C;
    f25 = _20;
    f26 = _1C;
    f24 = _20;
    for (s32 i = 0; i < _14 - 1; i++) {
        s32 index = _10 + i;
        s32 indexPlusOne = index + 1;
        if (i == _14 - 2 && _18) {
            indexPlusOne = 0;
        }

        mOceanRing->getPoint(0, index);
        mOceanRing->getPoint(0, indexPlusOne);

        f26 += f28;
        f24 += f28;
        f32 f20 = 0.0f;
        f32 f19 = 0.0f;
        f32 f17 = 0.0f;
        f32 f16 = 0.0f;

        J3DNativeDraw::begin(GX_TRIANGLESTRIP, mOceanRing->mStride * 2, NEBULA_SHAPE_TEXCOORD0 | NEBULA_SHAPE_TEXCOORD0 << 1);
        for (s32 j = 0; j < mOceanRing->mStride; j++) {
            WaterPoint* pPoint = mOceanRing->getPoint(j, index);
            WaterPoint* pPoint2 = mOceanRing->getPoint(j, indexPlusOne);
            J3DNativeDraw::position(pPoint->mPosition.x, pPoint->mPosition.y, pPoint->mPosition.z);
            J3DNativeDraw::texCoord(f27, f17);
            J3DNativeDraw::texCoord(f25, f16);

            J3DNativeDraw::position(pPoint2->mPosition.x, pPoint2->mPosition.y, pPoint2->mPosition.z);
            J3DNativeDraw::texCoord(f26, f20);
            J3DNativeDraw::texCoord(f24, f19);

            f17 += f28;
            f16 += f28;
            f20 += f28;
            f19 += f28;
        }


        f27 = f26;
        f25 = f24;
    }
}

OceanRingDrawer::OceanRingDrawer(const OceanRing* pOceanRing) {
    mRing = pOceanRing;
    mDrawerCount = 0;
    mPartDrawers = nullptr;
    _C = 0.0f;
    _10 = 0.0f;
    _14 = 0.0f;
    _18 = 0.0f;
    _1C = 0.0f;
    _20 = 0.0f;
    mWaterTex = nullptr;
    mWaterIndTex = nullptr;

    mWaterTex = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "Water.bti"), 0);
    mWaterIndTex = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "WaterIndirect.bti"), 0);
    initParts();
    initDisplayList();
}

void OceanRingDrawer::update() {
    _C = MR::repeat(_C + ::sTexSpeed0U, 0.0f, 1.0f);
    _10 = MR::repeat(_10 + ::sTexSpeed0V, 0.0f, 1.0f);
    _14 = MR::repeat(_14 + ::sTexSpeed1U, 0.0f, 1.0f);
    _18 = MR::repeat(_18 + ::sTexSpeed1V, 0.0f, 1.0f);
    _1C = MR::repeat(_1C + 0.0f, 0.0f, 1.0f);
    _20 = MR::repeat(_20 + ::sTexSpeed2V, 0.0f, 1.0f);
}

void OceanRingDrawer::draw() const {
    loadMaterial();

    if (mRing->mIsClipped) {
        mMesh.draw();
    } else {
        for (s32 i = 0; i < mDrawerCount; i++) {
            getDrawer(i)->draw();
        }
    }
}

void OceanRingDrawer::drawBloom() const {
    loadMaterialBloom();
    TVec3f zDir = MR::getCamZdir();
    zDir.scale(::sBloomCameraOffsetZ);
    TVec3f camPos = MR::getCamPos();
    zDir.add(camPos);

    if (!mRing->mIsClipped) {
        for (s32 i = 0; i < mDrawerCount; i++) {
            OceanRingPartDrawer* drwr = getDrawer(i);

            if (drwr->mPosition.distance(zDir) < ::sDistancePartDrawBloom) {
                drwr->drawDynamicBloom();
            }
        }
    }
}

void OceanRingDrawer::initParts() {
    mDrawerCount = (mRing->mSegCount / 10) + 1;
    s32 count2 = mRing->mSegCount / mDrawerCount;
    f32 a = 0.0f, b = 0.0f, c = 0.0f;
    mPartDrawers = new OceanRingPartDrawer*[mDrawerCount];
    for (s32 i = 0; i < mDrawerCount - 1; i++) {
        mPartDrawers[i] = new OceanRingPartDrawer(mRing, count2 * i, count2 + 1, false, &a, &b, &c);
    }

    s32 v;
    bool flag;
    s32 w;

    s32 last = mDrawerCount - 1;
    w = count2 * last;
    v = mRing->mSegCount - w;
    flag = false;

    if (MR::isLoopRail(mRing)) {
        flag = true;
        v += 1;
    }

    mPartDrawers[last] = new OceanRingPartDrawer(mRing, w, v, flag, &a, &b, &c);
}

void OceanRingDrawer::initDisplayList() {
    s32 flag = 0;

    if (MR::isLoopRail(mRing)) {
        flag = 1;
    }

    mMesh.init(NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0 | NEBULA_SHAPE_TEXCOORD0 << 1 | NEBULA_SHAPE_TEXCOORD0 << 2, 2 * (flag + mRing->mSegCount));
    drawGD();
    mMesh.send();
}

void OceanRingDrawer::drawGD() {
    s32 flag = 0;

    if (MR::isLoopRail(mRing)) {
        flag = 1;
    }

    f32 scalex;
    f32 U0 = 0.0f;
    f32 U1 = 0.0f;
    f32 U2 = 0.0f;
    f32 V01 = 0.0f;
    f32 V11 = 0.0f;
    f32 V21 = 0.0f;
    s32 m = mRing->mStride;
    m -= 1;
    f32 V00 = 0.05f * m;
    f32 V10 = 0.05f * m;
    f32 V20 = 0.1f * m;

    mMesh.begin(GX_TRIANGLESTRIP, (flag + mRing->mSegCount) * 2);

    scalex = 1.0f;
    f32 V0 = V00 * scalex;
    f32 V1 = V10 * scalex;
    f32 V2 = V20 * scalex;

    WaterPoint* pPoint;
    WaterPoint* pPoint2;
    for (u32 i = 0; i < flag + mRing->mSegCount; i++) {
        s32 index = i;
        if (index == mRing->mSegCount) {
            index = 0;
        }

        s32 ddd = index * mRing->mStride;
        pPoint = mRing->mWaterPoints[mRing->mStride + ddd - 1];
        pPoint2 = mRing->mWaterPoints[ddd];
        mMesh.position(pPoint->mOrigPos.x, pPoint->mOrigPos.y, pPoint->mOrigPos.z);
        mMesh.color(0xFF, 0xFF, 0xFF, pPoint->mAlpha);
        mMesh.texCoord(0, U0, V0);
        mMesh.texCoord(1, U1, V1);
        mMesh.texCoord(2, U2, V2);
        mMesh.corner();

        mMesh.position(pPoint2->mOrigPos.x, pPoint2->mOrigPos.y, pPoint2->mOrigPos.z);
        mMesh.color(0xFF, 0xFF, 0xFF, pPoint2->mAlpha);
        mMesh.texCoord(0, U0, V01);
        mMesh.texCoord(1, U1, V11);
        mMesh.texCoord(2, U2, V21);
        mMesh.corner();

        U0 += (0.05f * scalex);
        U1 += (0.05f * scalex);
        U2 += (0.1f * scalex);
    }
}

void OceanRingDrawer::loadMaterial() const {
    J3DNativeShape::sendPosition(MR::getCameraViewMtx(), 0);
    J3DNativeState::setCurrentMtx(0);
    J3DNativeState::setNumChans(1);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, 0, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setNumTexGens(4);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x1E, 0, 0x7D);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, 0x21, 0, 0x7D);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX2, 0x24, 0, 0x7D);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD3, GX_TG_MTX3x4, GX_TG_POS, 0x27, 0, 0x7D);

    TMtx34f mtx;
    mtx.identity();
    mtx.mMtx[0][2] = _C;
    mtx.mMtx[1][2] = _10;
    J3DNativeShape::sendTexture(mtx.toMtxPtr(), 0x1E, GX_MTX2x4);
    mtx.mMtx[0][2] = _14;
    mtx.mMtx[1][2] = _18;
    J3DNativeShape::sendTexture(mtx.toMtxPtr(), 0x21, GX_MTX2x4);
    mtx.mMtx[0][2] = _1C;
    mtx.mMtx[1][2] = _20;
    J3DNativeShape::sendTexture(mtx.toMtxPtr(), 0x24, GX_MTX2x4);
    MR::loadTexProjectionMtx(39);

    mWaterTex->load(GX_TEXMAP0);
    JUTTexture tex(MR::getScreenResTIMG(), 0);
    tex.load(GX_TEXMAP1);
    mWaterIndTex->load(GX_TEXMAP2);

    if (::unknownVal != 0 && MR::isCameraInWater()) {
        J3DNativeState::setNumIndStages(0);
        J3DNativeState::setTevDirect(GX_TEVSTAGE0);
        J3DNativeState::setTevDirect(GX_TEVSTAGE1);
        J3DNativeState::setTevDirect(GX_TEVSTAGE2);
        J3DNativeState::setTevDirect(GX_TEVSTAGE3);
    } else {
        J3DNativeState::setNumIndStages(1);
        J3DNativeState::setIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD2, GX_TEXMAP2);
        J3DNativeState::setTevIndWarp(GX_TEVSTAGE3, GX_INDTEXSTAGE0, 1, 0, GX_ITM_0);

        Mtx23 new_mtx;
        new_mtx[0][0] = ::sIndirectScale;
        new_mtx[0][1] = 0.0f;
        new_mtx[0][2] = 0.0f;
        new_mtx[1][0] = 0.0f;
        new_mtx[1][1] = ::sIndirectScale;
        new_mtx[1][2] = 0.0f;

        J3DNativeState::setIndTexMtx(GX_ITM_0, new_mtx, 0);
    }

    J3DNativeState::setNumTevStages(4);
    J3DNativeState::setTevColor(GX_TEVREG0, (GXColor&)::color1);
    J3DNativeState::setTevColor(GX_TEVREG1, (GXColor&)::color2);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 0, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 0, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_CPREV, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_DIVIDE_2, 0, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, 0, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE2, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE2, GX_CC_CPREV, GX_CC_A0, GX_CC_C0, GX_CC_CPREV);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE2, GX_TEV_COMP_R8_EQ, GX_TB_ZERO, GX_CS_SCALE_1, 0, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE2, GX_CA_KONST, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 0, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE3, GX_TEXCOORD3, GX_TEXMAP1, GX_COLOR0A0);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE3, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C1, GX_CC_CPREV);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE3, GX_CA_RASA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 0, GX_TEVPREV);
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    J3DNativeState::setAlphaCompare(GX_GREATER, 0, GX_AOP_OR, GX_GREATER, 0);
    J3DNativeState::setZMode(1, GX_LEQUAL, 0);
    J3DNativeState::setZCompLoc(1);
    J3DNativeState::setCullMode(GX_CULL_NONE);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
}

void OceanRingDrawer::loadMaterialBloom() const {
    J3DNativeShape::sendPosition(MR::getCameraViewMtx(), 0);
    J3DNativeState::setCurrentMtx(0);
    J3DNativeState::setNumChans(0);
    J3DNativeState::setNumTexGens(2);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x1E, 0, 0x7D);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, 0x21, 0, 0x7D);

    TMtx34f mtx;
    mtx.identity();
    mtx.mMtx[0][2] = _C;
    mtx.mMtx[1][2] = _10;
    J3DNativeShape::sendTexture(mtx.toMtxPtr(), 0x1E, GX_MTX2x4);
    mtx.mMtx[0][2] = _14;
    mtx.mMtx[1][2] = _18;
    J3DNativeShape::sendTexture(mtx.toMtxPtr(), 0x21, GX_MTX2x4);
    mWaterTex->load(GX_TEXMAP0);
    J3DNativeState::setNumIndStages(0);
    TVec3f zDir = MR::getCamZdir();
    f32 v6 = ((MR::clamp(zDir.y, -0.2f, 0.1f) - -0.2f) / 0.3f);
    f32 v8 = ((MR::clamp(WaterAreaFunction::getCameraWaterDepth(), 150.0f, 300.0f) - 150.0f) / 150.0f);

    if (v6 >= v8) {
        v8 = v8;
    } else {
        v8 = v6;
    }

    f32 v18 = (150.0f + (105.0f * v8));
    u8 v19 = (s32)(v18);
    J3DNativeState::setNumTevStages(2);
    Color8 v14(80, 80, 80, v19);
    Color8 v13(80, 80, 80, 255);
    J3DNativeState::setTevColor(GX_TEVREG0, (GXColor&)v14);
    J3DNativeState::setTevColor(GX_TEVREG1, (GXColor&)v13);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C0, GX_CC_C1);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1u, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_A0, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1u, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C0, GX_CC_CPREV);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1u, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_TEXA, GX_CA_A0, GX_CA_APREV);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1u, GX_TEVPREV);
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    J3DNativeState::setAlphaCompare(GX_GREATER, 0, GX_AOP_OR, GX_GREATER, 0);
    J3DNativeState::setZMode(1u, GX_LEQUAL, 1u);
    J3DNativeState::setZCompLoc(1u);
    J3DNativeState::setCullMode(GX_CULL_NONE);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
}
