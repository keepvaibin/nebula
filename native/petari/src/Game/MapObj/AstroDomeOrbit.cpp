#include "Game/MapObj/AstroDomeOrbit.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Map/SphereSelector.hpp"
#include "Game/MapObj/MiniatureGalaxyHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

namespace {
    const static Vec cRotateOutermost = {20.0f, 45.0f, 0.0f};
    const static f32 cRadius[] = {4000.0f, 6200.0f, 8100.0f, 10300.0f, 12000.0f};
    const static f32 cRadiusLastDome[] = {4000.0f, 6700.0f, 9100.0f, 11800.0f};
    const static s32 cDevideNum = 64;
    const static f32 cWidth = 100.0f;
    const static f32 cHeight = 50.0f;
    const static f32 cGalaxyRotateSpeed = -0.05f;
    const static f32 cGalaxyRotateCoordOffset = 230.0f;
    const static f32 cBloomWidth = 131.0f;
    const static f32 cBloomHeight = 60.0f;

    const static Color8 cColor(0x13, 0xB1, 0xFF, 0xFF);
    const static Color8 cBloomColor(0x00, 0xB4, 0x64, 0xFF);
};  // namespace

AstroDomeOrbit::AstroDomeOrbit() : LiveActor("\223\126\225\266\203\150\201\133\203\200\202\314\213\117\223\271" /* 天文ドームの軌道 */), mOrbitRadius(5000.0f), mAngle() {
}

void AstroDomeOrbit::init(const JMapInfoIter& rIter) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_AstroDomeOrbit);

    MR::invalidateClipping(this);

    MR::createAdaptorAndConnectToDrawBloomModel("\223\126\225\266\203\150\201\133\203\200\213\117\223\271\203\165\203\213\201\133\203\200\225\140\211\346" /* 天文ドーム軌道ブルーム描画 */, MR::Functor(this, &AstroDomeOrbit::drawBloom));

    makeActorDead();
}

void AstroDomeOrbit::draw() const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    initDraw(::cColor);

    drawCelling(::cWidth, true, ::cHeight);
    drawCelling(::cWidth, false, ::cHeight);

    drawSide(::cWidth, true, ::cHeight);
    drawSide(::cWidth, false, ::cHeight);
}

void AstroDomeOrbit::drawBloom() const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    initDraw(::cBloomColor);

    drawCelling(::cBloomWidth, true, ::cBloomHeight);
    drawCelling(::cBloomWidth, false, ::cBloomHeight);

    drawSide(::cBloomWidth, true, ::cBloomHeight);
    drawSide(::cBloomWidth, false, ::cBloomHeight);
}

void AstroDomeOrbit::setup(s32 radiusIdx) {
    s32 miniNum = MiniatureGalaxyFunction::getMiniatureGalaxyNum();
    const f32* radii = ::cRadiusLastDome;

    if (miniNum == 5) {
        radii = ::cRadius;
    }

    mOrbitRadius = radii[radiusIdx];
    mAngle = ::cGalaxyRotateCoordOffset * radiusIdx;

    if (radiusIdx >= 4) {
        mRotation.set(::cRotateOutermost);
    }
}

void AstroDomeOrbit::moveCoord() {
    mAngle = calcRepeatedRotateCoord(::cGalaxyRotateSpeed + mAngle);
}

void AstroDomeOrbit::calcGalaxyPos(TVec3f* pPos) const {
    f32 angle = calcRepeatedRotateCoord(mAngle) * TWO_PI / 360.0f;
    pPos->set< f32 >(MR::cos(angle), 0.0f, MR::sin(angle));
    pPos->mult(mOrbitRadius);
    pPos->add(*pPos, SphereSelectorFunction::getHandleTrans());

    TPos3f rotateMtx;
    SphereSelectorFunction::calcHandledRotateMtx(mRotation, &rotateMtx);
    rotateMtx.mult(*pPos, *pPos);
}

void AstroDomeOrbit::initDraw(const Color8& rColor) const {
    TPos3f rotateMtx;
    SphereSelectorFunction::calcHandledRotateMtx(mRotation, &rotateMtx);
    rotateMtx.concat(MR::getCameraViewMtx(), rotateMtx);
    J3DNativeShape::sendPosition(rotateMtx, GX_PNMTX0);
    J3DNativeState::setCurrentMtx(GX_PNMTX0);
    J3DNativeState::setNumChans(1);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, 0, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setChanMatColor(GX_COLOR0A0, rColor);
    J3DNativeState::setNumTexGens(0);
    J3DNativeState::setNumTevStages(1u);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_NOOP);
    J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    J3DNativeState::setZMode(1u, GX_LEQUAL, 0);
    J3DNativeState::setZCompLoc(0);
    J3DNativeState::setCullMode(GX_CULL_BACK);
}

void AstroDomeOrbit::drawCelling(f32 width, bool isAddHeight, f32 height) const {
    TVec3f handleTrans;
    handleTrans.set(SphereSelectorFunction::getHandleTrans());

    if (isAddHeight) {
        handleTrans.y += (0.5f * height);
    } else {
        handleTrans.y -= (0.5f * height);
    }

    J3DNativeDraw::begin(GX_TRIANGLESTRIP, 130, 0);

    for (s32 i = 0; i < ::cDevideNum + 1; i++) {
        f32 angle = TWO_PI * i / ::cDevideNum;

        TVec3f rotateVec;
        rotateVec.set< f32 >(MR::cos(angle), 0.0f, MR::sin(angle));

        TVec3f negVec;
        negVec.scale(mOrbitRadius - (0.5f * width), rotateVec);
        negVec.add(handleTrans);

        TVec3f posVec;
        posVec.scale(mOrbitRadius + (0.5f * width), rotateVec);
        posVec.add(handleTrans);

        if (isAddHeight) {
            J3DNativeDraw::position(negVec.x, negVec.y, negVec.z);
            J3DNativeDraw::position(posVec.x, posVec.y, posVec.z);
        } else {
            J3DNativeDraw::position(posVec.x, posVec.y, posVec.z);
            J3DNativeDraw::position(negVec.x, negVec.y, negVec.z);
        }
    }
}

void AstroDomeOrbit::drawSide(f32 width, bool isAddWidth, f32 height) const {
    TVec3f handleTrans;
    handleTrans.set(SphereSelectorFunction::getHandleTrans());
    handleTrans.y += 0.5f * height;

    f32 scale;
    if (isAddWidth) {
        scale = mOrbitRadius + (0.5f * width);
    } else {
        scale = mOrbitRadius - (0.5f * width);
    }

    J3DNativeDraw::begin(GX_TRIANGLESTRIP, (::cDevideNum + 1) * 2, 0);

    for (s32 i = 0; i < ::cDevideNum + 1; i++) {
        f32 angle = TWO_PI * i / ::cDevideNum;

        TVec3f rotateVec;
        rotateVec.set< f32 >(MR::cos(angle), 0.0f, MR::sin(angle));

        TVec3f v16;
        v16.scale(scale, rotateVec);
        v16.add(handleTrans);

        TVec3f v15;
        v15.set(v16);
        v15.y -= height;

        if (isAddWidth) {
            J3DNativeDraw::position(v16.x, v16.y, v16.z);
            J3DNativeDraw::position(v15.x, v15.y, v15.z);
        } else {
            J3DNativeDraw::position(v15.x, v15.y, v15.z);
            J3DNativeDraw::position(v16.x, v16.y, v16.z);
        }
    }
}

f32 AstroDomeOrbit::calcRepeatedRotateCoord(f32 coord) const {
    return MR::repeatDegree(coord);
}

AstroDomeOrbit::~AstroDomeOrbit() {
}
