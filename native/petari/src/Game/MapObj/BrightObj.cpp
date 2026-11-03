#include "Game/System/NebulaFrameClock.hpp"
#include "Game/MapObj/BrightObj.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/Util/GameLightSignal.hpp"
#include "Game/MapObj/Sun.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Screen/LensFlare.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"

namespace {
    const static f32 sSunRadius = 3000.0f;
    const static f32 sSunModelScale = 100.0f;

    bool calcScreenPosition(TVec2f* pDst, const TVec3f& rTrans, const TPos3f& rViewMtx, const TProj3f& rProjMtx) {
        TVec3f newPos;
        TVec3f screenPos;
        {
            TVec4f clipPos;
            TVec3f vec;
            rViewMtx.mult(rTrans, vec);
            f32 z = vec.z * rProjMtx[2][2] + rProjMtx[2][3];
            f32 w = -vec.z;
            f32 y = vec.y * rProjMtx[1][1] + vec.z * rProjMtx[1][2];
            f32 x = vec.x * rProjMtx[0][0] + vec.z * rProjMtx[0][2];
            clipPos.x = x;
            clipPos.y = y;
            clipPos.z = z;
            clipPos.w = w;
            newPos.scale(1.0f / clipPos.w, *clipPos.toTVec3());
        }

        bool ret;

        if (1.0f < MR::abs(newPos.x) || 1.0f < MR::abs(newPos.y)) {
            ret = false;
        } else {
            ret = 0.0f < newPos.z == false;
        }

        f32 width = MR::getScreenWidth();
        f32 height = static_cast< s32 >(NebulaFrameClock::cScreenUnitsDown);
        screenPos.set< f32 >(0.5f * width + 0.5f * (newPos.x * width), 0.5f * height + 0.5f * (-newPos.y * height), newPos.z);

        pDst->x = screenPos.x;
        pDst->y = screenPos.y;

        return ret;
    }
};  // namespace

BrightInfo::BrightInfo() {
    reset();
}

void BrightInfo::write(const TVec2f& rBrightnessCenter, const TVec2f& rRealCenter, f32 brightness) {
    mBrightnessCenter[mIndex].set(rBrightnessCenter);
    mRealCenter[mIndex].set(rRealCenter);
    mBright[mIndex] = brightness;

    u32 val = mIndex;

    val++;

    if (val > 2) {
        val = 0;
    }

    if (val != mCurrIndex) {
        mIndex = val;
    }
}

void BrightInfo::endRead() {
    if (mCurrIndex == mIndex) {
        return;
    }

    mCurrIndex++;

    if (mCurrIndex > 2) {
        mCurrIndex = 0;
    }
}

void BrightInfo::reset() {
    mIndex = 0;
    mCurrIndex = 0;

    for (u32 i = 0; i < 3; i++) {
        mBrightnessCenter[i].zero();
        mRealCenter[i].zero();
        mBright[i] = 0.0f;
    }
}

BrightObjBase::BrightObjBase() : mInfo(), mBrightCenter(0.0f, 0.0f), mNowCenter(0.0f, 0.0f), mBrightness(), mIsNotVisible(true), mDrawPos(0.0f, 0.0f, 0.0f), mDrawRadius(0.0f) {
    MR::addBrightObj(this);
}

// The picture has been drawn with this frame's camera: the points of the sphere it shows are asked about with that camera.
void BrightObjBase::checkVisibilityOfSphere(NebulaScreenProbe* pProbes) {
    const TPos3f& rView = MR::getCameraViewMtx();
    const TProj3f& rProj = MR::getCameraProjectionMtx();
    TVec3f drawPos(mDrawPos);
    TVec3f camDir(MR::getCamYdir());
    TVec3f drawDir(MR::getCamPos() - drawPos);

    for (u32 idx = 0; idx < cProbeCount; idx++) {
        pProbes[idx].x = -1.0f;
        pProbes[idx].y = -1.0f;
        pProbes[idx].user_x = 0.0f;
        pProbes[idx].user_y = 0.0f;
    }

    if (MR::isNearZero(drawDir)) {
        return;
    }

    MR::normalize(&drawDir);

    TVec3f vec110;
    vec110.cross(camDir, drawDir);
    camDir.cross(drawDir, vec110);

    TPos3f mtx;
    mtx.setTR(vec110, camDir, drawDir, drawPos);

    // The first point is the centre: its screen position comes back as the flare's centre whether or not it shows.
    TVec2f center;
    ::calcScreenPosition(&center, drawPos, rView, rProj);
    checkVisible(&pProbes[0], drawPos, rView, rProj);
    pProbes[0].user_x = center.x;
    pProbes[0].user_y = center.y;

    for (u32 idx = 0; idx < 8; idx++) {
        f32 angle = idx * MR::pi() / 4.0f;
        TVec3f vec11C(MR::cos(angle), MR::sin(angle), 0.0f);
        vec11C.scale(mDrawRadius * 0.4f);

        mtx.mult(vec11C, vec11C);

        checkVisible(&pProbes[1 + idx], vec11C, rView, rProj);
    }

    for (u32 idx = 0; idx < 8; idx++) {
        f32 angle = (idx + 0.5f) * MR::pi() / 4.0f;
        TVec3f vec128(MR::cos(angle), MR::sin(angle), 0.0f);
        vec128.scale(mDrawRadius * 0.7f);

        mtx.mult(vec128, vec128);

        checkVisible(&pProbes[9 + idx], vec128, rView, rProj);
    }
}

void BrightObjBase::checkVisible(NebulaScreenProbe* pProbe, const TVec3f& rTrans, const TPos3f& rViewMtx, const TProj3f& rProjMtx) {
    TVec2f screenPos;

    if (::calcScreenPosition(&screenPos, rTrans, rViewMtx, rProjMtx)) {
        TVec2f frameBufferPos;
        MR::convertScreenPosToCanvasPos(&frameBufferPos, screenPos);
        pProbe->x = frameBufferPos.x;
        pProbe->y = frameBufferPos.y;
        pProbe->user_x = screenPos.x;
        pProbe->user_y = screenPos.y;
    }
}

// The points' answers: how much of the sphere passed its depth test (its alpha is set where it did).
void BrightObjBase::setResult(const NebulaScreenProbe* pProbes) {
    u32 bright = 0;
    TVec2f objCenter(0.0f, 0.0f);

    for (u32 idx = 0; idx < cProbeCount; idx++) {
        if (pProbes[idx].x >= 0.0f && pProbes[idx].value >= (0xF0 / 255.0f)) {
            bright++;
            objCenter += TVec2f(pProbes[idx].user_x, pProbes[idx].user_y);
        }
    }

    const TVec2f center(pProbes[0].user_x, pProbes[0].user_y);
    f32 ratio = static_cast< f32 >(bright) / cProbeCount;

    if (bright != 0) {
        mBrightCenter.set(TVec2f(objCenter * (1.0f / bright)));
        mBrightness = ratio;
        mNowCenter.set(center);
        mIsNotVisible = false;
    } else {
        mBrightness = ratio;
        mIsNotVisible = true;
    }

    mInfo.write(mBrightCenter, center, mBrightness);
}

void BrightObjBase::drawSphere(const TVec3f& rPos, float radius) const {
    TPos3f viewMtx(MR::getCameraViewMtx());

    TPos3f mtx;
    mtx.identity33();
    mtx.setTrans(rPos);
    mtx.concat(viewMtx, mtx);

    // The sun's visibility in the alpha buffer: cleared by a larger sphere over everything, then set where the sun
    // itself passes the depth test (alpha only).
    NebulaNativeMaterial material;
    TDDraw::setupNative(&material, 0, 0, 1);
    TDDraw::loadViewMtx(mtx);

    J3DPass::constantAlpha(false, 0);
    material.color_write = 0;
    material.alpha_write = 1;
    NativeShader::setDepth(material, true, NEBULA_COMPARE_ALWAYS, false);
    NativeShader::send(material);

    TDDraw::drawSphere(TVec3f(0, 0, 0), radius * 1.1f, 0xFF000000, 0x10);

    NativeShader::setDepth(material, true, NEBULA_COMPARE_LEQUAL, false);
    NativeShader::send(material);

    TDDraw::drawSphere(TVec3f(0, 0, 0), radius, 0x00FF00FF, 0x10);

    J3DPass::writeColor(true);
    J3DPass::writeAlpha(false);
}

BrightObj::BrightObj(const char* pName) : LiveActor(pName), BrightObjBase(), mRadius(::sSunModelScale) {
}

void BrightObj::init(const JMapInfoIter& rIter) {
    MR::initDefaultPos(this, rIter);

    MR::getJMapInfoArg0NoInit(rIter, &mRadius);

    MR::invalidateClipping(this);

    MR::connectToScene(this, MR::MovementType_Environment, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_BrightSun);

    makeActorAppeared();
}

void BrightObj::control() {
    mDrawPos.set(mPosition);
    mDrawRadius = mRadius;
}

void BrightObj::draw() const {
    if (MR::isDead(this) || MR::isHiddenModel(this) || MR::isClipped(this)) {
        return;
    }

    drawSphere(mPosition, mRadius);
}

void BrightObj::calcBrightInfo(NebulaScreenProbe* pProbes) {
    BrightObjBase::checkVisibilityOfSphere(pProbes);
}

void BrightObj::getNowCenter(TVec2f* pDst) const {
    ::calcScreenPosition(pDst, mPosition, MR::getCameraViewMtx(), MR::getCameraProjectionMtx());
}

BrightSun::BrightSun(const char* pName) : LiveActor(pName), BrightObjBase(), mSun() {
}

void BrightSun::control() {
    TVec3f camPos = MR::getCamPos();

    TPos3f mtx;
    mtx.identity33();
    mtx.setTrans(0.0f, 0.0f, 100000.0f);

    TPos3f mtxTR;
    MR::makeMtxTR(mtxTR, camPos.x, camPos.y, camPos.z, mRotation.x, mRotation.y, mRotation.z);

    mtxTR.concat(mtx);
    mtxTR.getTrans(mPosition);
    GameLightSignal::sun(mPosition);

    controlSunModel();

    mDrawPos.set(mPosition);
    mDrawRadius = ::sSunRadius;
}

void BrightSun::draw() const {
    if (MR::isDead(this) || MR::isHiddenModel(this) || MR::isClipped(this)) {
        return;
    }

    drawSphere(mPosition, ::sSunRadius);
}

void BrightSun::calcBrightInfo(NebulaScreenProbe* pProbes) {
    if (MR::isDead(this)) {
        pProbes[0].x = cNoProbes;
        return;
    }

    checkVisibilityOfSphere(pProbes);
}

void BrightSun::getNowCenter(TVec2f* pDst) const {
    ::calcScreenPosition(pDst, mPosition, MR::getCameraViewMtx(), MR::getCameraProjectionMtx());
}

void BrightSun::controlSunModel() {
    mSun->mPosition.set(mPosition);
    mSun->mScale.set(::sSunModelScale);

    TVec3f camDir(MR::getCamPos() - mPosition);
    MR::normalize(&camDir);

    TVec3f fwdVec(0.0f, 0.0f, 1.0f);
    TPos3f rotateMtx;
    rotateMtx.identity();
    rotateMtx.setRotate(fwdVec, camDir);

    TVec3f rotation;

    if (fwdVec.dot(camDir) < -0.999f) {
        rotation.set< f32 >(0.0f, 180.0f, 0.0f);
    } else {
        rotateMtx.getEulerXYZ(rotation);
        rotation.scale(57.295776f);
    }

    mSun->mRotation.set(rotation);
}

BrightObj::~BrightObj() {
}

BrightSun::~BrightSun() {
}

BrightObjBase::~BrightObjBase() {
}

void BrightObjBase::endRead() {
    mInfo.endRead();
}

const TVec2f* BrightObjBase::getCenter() const {
    return &mInfo.mRealCenter[mInfo.mCurrIndex];
}

const TVec2f* BrightObjBase::getBrightCenter() const {
    return &mInfo.mBrightnessCenter[mInfo.mCurrIndex];
}

f32 BrightObjBase::getBright() const {
    return mInfo.mBright[mInfo.mCurrIndex];
}

void BrightObjBase::getNowCenter(TVec2f* pDst) const {
    pDst->set(mNowCenter);
}

void BrightSun::init(const JMapInfoIter& rIter) {
    MR::initDefaultPos(this, rIter);

    MR::invalidateClipping(this);

    MR::connectToScene(this, MR::MovementType_Environment, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_BrightSun);

    mSun = new Sun("\221\276\227\172" /* 太陽 */);
    mSun->initWithoutIter();

    MR::tryRegisterDemoCast(this, rIter);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}
