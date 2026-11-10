#include "Game/System/NebulaFrameClock.hpp"
#include "Game/MapObj/BrightObj.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Util/GameLightSignal.hpp"
#include "Game/MapObj/Sun.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Screen/LensFlare.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcScreenPosition_f1, "calcScreenPosition.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcScreenPosition_f2, "calcScreenPosition.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcScreenPosition_f3, "calcScreenPosition.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcScreenPosition_f4, "calcScreenPosition.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkVisibilityOfSphere_f1, "checkVisibilityOfSphere.f1", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkVisibilityOfSphere_f2, "checkVisibilityOfSphere.f2", 0.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkVisibilityOfSphere_f3, "checkVisibilityOfSphere.f3", 0.9165f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkVisibilityOfSphere_f4, "checkVisibilityOfSphere.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkVisibilityOfSphere_f5, "checkVisibilityOfSphere.f5", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkVisibilityOfSphere_f6, "checkVisibilityOfSphere.f6", 0.7f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkVisibilityOfSphere_f7, "checkVisibilityOfSphere.f7", 0.7141f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 100000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_controlSunModel_f1, "controlSunModel.f1", 0.999f);
NEBULA_TUNABLE_NAMED(f32, cNum_controlSunModel_f2, "controlSunModel.f2", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_controlSunModel_f3, "controlSunModel.f3", 57.295776f);

namespace {
    const static f32 sSunRadius = 3000.0f;
    const static f32 sSunModelScale = 100.0f;
    // Each point is asked about a tenth of the radius in front of the sphere's surface. The object's own model is a
    // polygon sphere (corners on the sphere, faces inside it), and at the sun's distance one step of a float depth near
    // 1 spans about 6 units (56 with the near plane at 10): asked at the surface itself, a point near a corner of the
    // model was hidden or seen by rounding, and the flare's points came and went as the camera moved. Anything that
    // really hides the object stands far nearer than this.
    const static f32 sSurfaceMargin = 0.1f;

    // The depth of a point, 0 near to 1 far.
    f32 calcDepth(const Vec3f& rTrans, const Pos3f& rViewMtx, const Proj3f& rProjMtx) {
        Vec3f vec;
        rViewMtx.mult(rTrans, vec);
        return (vec.z * rProjMtx[2][2] + rProjMtx[2][3]) / -vec.z + 1.0f;
    }

    bool calcScreenPosition(Vec2f* pDst, const Vec3f& rTrans, const Pos3f& rViewMtx, const Proj3f& rProjMtx) {
        Vec3f newPos;
        Vec3f screenPos;
        {
            Vec4f clipPos;
            Vec3f vec;
            rViewMtx.mult(rTrans, vec);
            f32 z = vec.z * rProjMtx[2][2] + rProjMtx[2][3];
            f32 w = -vec.z;
            f32 y = vec.y * rProjMtx[1][1] + vec.z * rProjMtx[1][2];
            f32 x = vec.x * rProjMtx[0][0] + vec.z * rProjMtx[0][2];
            clipPos.x = x;
            clipPos.y = y;
            clipPos.z = z;
            clipPos.w = w;
            newPos.scale(1.0f / clipPos.w, *clipPos.toVec3());
        }

        // Inside the screen and in front of the near plane.
        bool isOnScreen = !(1.0f < MR::abs(newPos.x) || 1.0f < MR::abs(newPos.y)) && !(0.0f < newPos.z);

        f32 width = MR::getScreenWidth();
        f32 height = static_cast< s32 >(NebulaFrameClock::cScreenLines);
        screenPos.set< f32 >(cNum_calcScreenPosition_f1 * width + cNum_calcScreenPosition_f2 * (newPos.x * width), cNum_calcScreenPosition_f3 * height + cNum_calcScreenPosition_f4 * (-newPos.y * height), newPos.z);

        pDst->x = screenPos.x;
        pDst->y = screenPos.y;

        return isOnScreen;
    }
};  // namespace

BrightInfo::BrightInfo() {
    reset();
}

void BrightInfo::write(const Vec2f& rBrightnessCenter, const Vec2f& rRealCenter, f32 brightness) {
    mBrightnessCenter.set(rBrightnessCenter);
    mRealCenter.set(rRealCenter);
    mBright = brightness;
}

void BrightInfo::reset() {
    mBrightnessCenter.zero();
    mRealCenter.zero();
    mBright = 0.0f;
}

BrightObjBase::BrightObjBase() : mInfo(), mBrightCenter(0.0f, 0.0f), mNowCenter(0.0f, 0.0f), mBrightness(), mDrawPos(0.0f, 0.0f, 0.0f), mDrawRadius(0.0f) {
    MR::addBrightObj(this);
}

// The picture has been drawn with this frame's camera: the points of the sphere it shows are asked about with that camera.
void BrightObjBase::checkVisibilityOfSphere(NebulaScreenProbe* pProbes) {
    const Pos3f& rView = MR::getCameraViewMtx();
    const Proj3f& rProj = MR::getCameraProjectionMtx();
    Vec3f drawPos(mDrawPos);
    Vec3f camDir(MR::getCamYdir());
    Vec3f drawDir(MR::getCamPos() - drawPos);

    for (u32 idx = 0; idx < cProbeCount; idx++) {
        pProbes[idx].x = -1.0f;
        pProbes[idx].y = -1.0f;
        pProbes[idx].user_x = 0.0f;
        pProbes[idx].user_y = 0.0f;
        pProbes[idx].user_z = 0.0f;
    }

    if (MR::isNearZero(drawDir)) {
        return;
    }

    MR::normalize(&drawDir);

    Vec3f side;
    side.cross(camDir, drawDir);
    camDir.cross(drawDir, side);

    Pos3f mtx;
    mtx.setTR(side, camDir, drawDir, drawPos);

    // The first point is the centre: its screen position comes back as the flare's centre whether or not it shows.
    Vec2f center;
    ::calcScreenPosition(&center, drawPos, rView, rProj);
    checkVisible(&pProbes[0], drawPos, rView, rProj);
    pProbes[0].user_x = center.x;
    pProbes[0].user_y = center.y;
    // Each point's depth is the sphere's near surface in front of it.
    pProbes[0].user_z = ::calcDepth(drawPos + drawDir * (mDrawRadius * (1.0f + ::sSurfaceMargin)), rView, rProj);

    for (u32 idx = 0; idx < 8; idx++) {
        f32 angle = idx * MR::pi() / cNum_checkVisibilityOfSphere_f1;
        Vec3f innerProbe(MR::cos(angle), MR::sin(angle), 0.0f);
        innerProbe.scale(mDrawRadius * cNum_checkVisibilityOfSphere_f2);

        mtx.mult(innerProbe, innerProbe);

        checkVisible(&pProbes[1 + idx], innerProbe, rView, rProj);
        pProbes[1 + idx].user_z = ::calcDepth(innerProbe + drawDir * (mDrawRadius * (cNum_checkVisibilityOfSphere_f3 + ::sSurfaceMargin)), rView, rProj);
    }

    for (u32 idx = 0; idx < 8; idx++) {
        f32 angle = (idx + cNum_checkVisibilityOfSphere_f4) * MR::pi() / cNum_checkVisibilityOfSphere_f5;
        Vec3f outerProbe(MR::cos(angle), MR::sin(angle), 0.0f);
        outerProbe.scale(mDrawRadius * cNum_checkVisibilityOfSphere_f6);

        mtx.mult(outerProbe, outerProbe);

        checkVisible(&pProbes[9 + idx], outerProbe, rView, rProj);
        pProbes[9 + idx].user_z = ::calcDepth(outerProbe + drawDir * (mDrawRadius * (cNum_checkVisibilityOfSphere_f7 + ::sSurfaceMargin)), rView, rProj);
    }
}

void BrightObjBase::checkVisible(NebulaScreenProbe* pProbe, const Vec3f& rTrans, const Pos3f& rViewMtx, const Proj3f& rProjMtx) {
    Vec2f screenPos;

    if (::calcScreenPosition(&screenPos, rTrans, rViewMtx, rProjMtx)) {
        Vec2f frameBufferPos;
        MR::convertScreenPosToFrame(&frameBufferPos, screenPos);
        pProbe->x = frameBufferPos.x;
        pProbe->y = frameBufferPos.y;
        pProbe->user_x = screenPos.x;
        pProbe->user_y = screenPos.y;
    }
}

// The points' answers: how many of the sphere's points nothing drawn so far stands in front of.
void BrightObjBase::setResult(const NebulaScreenProbe* pProbes) {
    u32 bright = 0;
    Vec2f objCenter(0.0f, 0.0f);

    for (u32 idx = 0; idx < cProbeCount; idx++) {
        if (pProbes[idx].x >= 0.0f && pProbes[idx].value >= pProbes[idx].user_z) {
            bright++;
            objCenter += Vec2f(pProbes[idx].user_x, pProbes[idx].user_y);
        }
    }

    const Vec2f center(pProbes[0].user_x, pProbes[0].user_y);
    f32 ratio = static_cast< f32 >(bright) / cProbeCount;

    if (bright != 0) {
        mBrightCenter.set(Vec2f(objCenter * (1.0f / bright)));
        mBrightness = ratio;
        mNowCenter.set(center);
    } else {
        mBrightness = ratio;
    }

    mInfo.write(mBrightCenter, center, mBrightness);
}

BrightObj::BrightObj(const char* pName) : LiveActor(pName), BrightObjBase(), mRadius(::sSunModelScale) {
}

void BrightObj::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    MR::getPlacementArg0NoInit(rIter, &mRadius);

    MR::invalidateClipping(this);

    MR::connectToScene(this, MR::MovementType_Environment, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);

    makeActorAppeared();
}

void BrightObj::control() {
    mDrawPos.set(mPosition);
    mDrawRadius = mRadius;
}

void BrightObj::calcBrightInfo(NebulaScreenProbe* pProbes) {
    BrightObjBase::checkVisibilityOfSphere(pProbes);
}

void BrightObj::getNowCenter(Vec2f* pDst) const {
    ::calcScreenPosition(pDst, mPosition, MR::getCameraViewMtx(), MR::getCameraProjectionMtx());
}

BrightSun::BrightSun(const char* pName) : LiveActor(pName), BrightObjBase(), mSun() {
}

void BrightSun::control() {
    Vec3f camPos = MR::getCamPos();

    Pos3f mtx;
    mtx.identity33();
    mtx.setTrans(0.0f, 0.0f, cNum_control_f1);

    Pos3f mtxTR;
    MR::makeMtxTR(mtxTR, camPos.x, camPos.y, camPos.z, mRotation.x, mRotation.y, mRotation.z);

    mtxTR.concat(mtx);
    mtxTR.getTrans(mPosition);
    GameLightSignal::sun(mPosition);

    controlSunModel();

    mDrawPos.set(mPosition);
    mDrawRadius = ::sSunRadius;
}

void BrightSun::calcBrightInfo(NebulaScreenProbe* pProbes) {
    // A sun not out gives no light: nothing it saw before it went stays on the screen.
    if (MR::isDead(this)) {
        mInfo.reset();
        mBrightness = 0.0f;
        pProbes[0].x = cNoProbes;
        return;
    }

    checkVisibilityOfSphere(pProbes);
}

void BrightSun::getNowCenter(Vec2f* pDst) const {
    ::calcScreenPosition(pDst, mPosition, MR::getCameraViewMtx(), MR::getCameraProjectionMtx());
}

void BrightSun::controlSunModel() {
    mSun->mPosition.set(mPosition);
    mSun->mScale.set(::sSunModelScale);

    Vec3f camDir(MR::getCamPos() - mPosition);
    MR::normalize(&camDir);

    Vec3f fwdVec(0.0f, 0.0f, 1.0f);
    Pos3f rotateMtx;
    rotateMtx.identity();
    rotateMtx.setRotate(fwdVec, camDir);

    Vec3f rotation;

    if (fwdVec.dot(camDir) < -cNum_controlSunModel_f1) {
        rotation.set< f32 >(0.0f, cNum_controlSunModel_f2, 0.0f);
    } else {
        rotateMtx.getEulerXYZ(rotation);
        rotation.scale(cNum_controlSunModel_f3);
    }

    mSun->mRotation.set(rotation);
}

BrightObjBase::~BrightObjBase() {
}

const Vec2f* BrightObjBase::getCenter() const {
    return &mInfo.mRealCenter;
}

const Vec2f* BrightObjBase::getBrightCenter() const {
    return &mInfo.mBrightnessCenter;
}

f32 BrightObjBase::getBright() const {
    return mInfo.mBright;
}

void BrightObjBase::getNowCenter(Vec2f* pDst) const {
    pDst->set(mNowCenter);
}

void BrightSun::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    MR::invalidateClipping(this);

    MR::connectToScene(this, MR::MovementType_Environment, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);

    mSun = new Sun("太陽");
    mSun->initWithoutIter();

    MR::tryRegisterDemoCast(this, rIter);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}
