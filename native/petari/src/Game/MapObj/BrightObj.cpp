#include "Game/System/NebulaFrameClock.hpp"
#include "Game/MapObj/BrightObj.hpp"
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
#include "Game/System/LumaCore.hpp"

namespace {
    const static f32 sSunRadius = 3000.0f;
    const static f32 sSunModelScale = 100.0f;
    // Each sample's depth is a tenth of the radius in front of the sphere's surface. The object's own model is a
    // polygon sphere (corners on the sphere, faces inside it), and at the sun's distance one step of a float depth near
    // 1 spans about 6 units (56 with the near plane at 10): read at the surface itself, a point near a corner of the
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
        screenPos.set< f32 >(0.5f * width + 0.5f * (newPos.x * width), 0.5f * height + 0.5f * (-newPos.y * height), newPos.z);

        pDst->x = screenPos.x;
        pDst->y = screenPos.y;

        return isOnScreen;
    }

    // One sample: the point's place in the frame and the depth of `rFront`, the sphere's near surface in front of it.
    bool calcSample(BrightSample* pSample, const Vec3f& rPoint, const Vec3f& rFront, const Pos3f& rView, const Proj3f& rProj) {
        Vec2f screenPos;
        pSample->mOnScreen = ::calcScreenPosition(&screenPos, rPoint, rView, rProj);
        Vec2f framePos;
        MR::convertScreenPosToFrame(&framePos, screenPos);
        pSample->mFrameX = framePos.x;
        pSample->mFrameY = framePos.y;
        pSample->mDepth = ::calcDepth(rFront, rView, rProj);
        return pSample->mOnScreen;
    }
};  // namespace

BrightObjBase::BrightObjBase() : mDrawPos(0.0f, 0.0f, 0.0f), mDrawRadius(0.0f) {
    MR::addBrightObj(this);
}

// The sphere's samples as the scene's camera sees them: the step's camera when the step asks, the picture's when its draw
// does (the same camera: the picture is drawn with the step's).
u32 BrightObjBase::calcSamples(BrightSample* pSamples) const {
    const Pos3f& rView = MR::getCameraViewMtx();
    const Proj3f& rProj = MR::getCameraProjectionMtx();
    Vec3f drawPos(mDrawPos);
    Vec3f camDir(MR::getCamYdir());
    Vec3f drawDir(MR::getCamPos() - drawPos);

    for (u32 idx = 0; idx < cSampleCount; idx++) {
        pSamples[idx].mFrameX = 0.0f;
        pSamples[idx].mFrameY = 0.0f;
        pSamples[idx].mDepth = 0.0f;
        pSamples[idx].mOnScreen = false;
    }

    if (MR::isNearZero(drawDir)) {
        return 0;
    }

    MR::normalize(&drawDir);

    Vec3f side;
    side.cross(camDir, drawDir);
    camDir.cross(drawDir, side);

    Pos3f mtx;
    mtx.setTR(side, camDir, drawDir, drawPos);

    u32 onScreen = 0;
    if (::calcSample(&pSamples[0], drawPos, drawPos + drawDir * (mDrawRadius * (1.0f + ::sSurfaceMargin)), rView, rProj)) {
        onScreen++;
    }

    for (u32 idx = 0; idx < 8; idx++) {
        f32 angle = idx * MR::pi() / 4.0f;
        Vec3f innerPoint(MR::cos(angle), MR::sin(angle), 0.0f);
        innerPoint.scale(mDrawRadius * 0.4f);

        mtx.mult(innerPoint, innerPoint);

        if (::calcSample(&pSamples[1 + idx], innerPoint, innerPoint + drawDir * (mDrawRadius * (0.9165f + ::sSurfaceMargin)), rView,
                         rProj)) {
            onScreen++;
        }
    }

    for (u32 idx = 0; idx < 8; idx++) {
        f32 angle = (idx + 0.5f) * MR::pi() / 4.0f;
        Vec3f outerPoint(MR::cos(angle), MR::sin(angle), 0.0f);
        outerPoint.scale(mDrawRadius * 0.7f);

        mtx.mult(outerPoint, outerPoint);

        if (::calcSample(&pSamples[9 + idx], outerPoint, outerPoint + drawDir * (mDrawRadius * (0.7141f + ::sSurfaceMargin)), rView,
                         rProj)) {
            onScreen++;
        }
    }

    return onScreen;
}

BrightObj::BrightObj(const char* pName) : LiveActor(pName), BrightObjBase(), mRadius(::sSunModelScale) {
    lumacore::setClass(this, lumacore::kClassId< BrightObj >);
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

void BrightObj::getNowCenter(Vec2f* pDst) const {
    ::calcScreenPosition(pDst, mPosition, MR::getCameraViewMtx(), MR::getCameraProjectionMtx());
}

BrightSun::BrightSun(const char* pName) : LiveActor(pName), BrightObjBase(), mSun() {
    lumacore::setClass(this, lumacore::kClassId< BrightSun >);
}

void BrightSun::control() {
    Vec3f camPos = MR::getCamPos();

    Pos3f mtx;
    mtx.identity33();
    mtx.setTrans(0.0f, 0.0f, 100000.0f);

    Pos3f mtxTR;
    MR::makeMtxTR(mtxTR, camPos.x, camPos.y, camPos.z, mRotation.x, mRotation.y, mRotation.z);

    mtxTR.concat(mtx);
    mtxTR.getTrans(mPosition);

    controlSunModel();

    mDrawPos.set(mPosition);
    mDrawRadius = ::sSunRadius;
}

// A sun not out gives no light.
bool BrightSun::isShining() const {
    return !MR::isDead(this);
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

    if (fwdVec.dot(camDir) < -0.999f) {
        rotation.set< f32 >(0.0f, 180.0f, 0.0f);
    } else {
        rotateMtx.getEulerXYZ(rotation);
        rotation.scale(57.295776f);
    }

    mSun->mRotation.set(rotation);
}

BrightObjBase::~BrightObjBase() {
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
