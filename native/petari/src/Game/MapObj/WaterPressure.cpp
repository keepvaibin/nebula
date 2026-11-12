#include "Game/MapObj/WaterPressure.hpp"
#include "Game/MapObj/WaterPressureBullet.hpp"
#include "Game/MapObj/WaterPressureBulletHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sGunPointOffset = 200.0f;
};  // namespace

WaterPressure::WaterPressure(const char* pName) : PressureBase(pName), mCameraInfo(), mIsInvalidSpinKill() {
    lumacore::setClass(this, lumacore::kClassId< WaterPressure >);
}

void WaterPressure::init(const TableIter& rIter) {
    PressureBase::init(rIter);
    MR::initActorCamera(this, rIter, &mCameraInfo);
    MR::getPlacementArg7NoInit(rIter, &mIsInvalidSpinKill);
}

void WaterPressure::initBullet(const TableIter& /*rIter*/) {
    MR::createSceneObj(SceneObj_WaterPressureBulletHolder);
}

bool WaterPressure::shotBullet(f32 speed) {
    WaterPressureBullet* pBullet = MR::getSceneObj< WaterPressureBulletHolder >(SceneObj_WaterPressureBulletHolder)->callEmptyBullet();
    Pos3f point;
    calcGunPointFromCannon(&point);
    pBullet->shotWaterBullet(this, point, speed, !isShotTypeOnGravity(), false, mIsInvalidSpinKill, &mCameraInfo);
    return true;
}

// The cannon fires along its joint's X axis.
void WaterPressure::calcGunPointFromCannon(Pos3f* pMtx) {
    Pos3f jointMtx = MR::getJointMtx(this, "Cannon1");
    Vec3f front, pos;
    jointMtx.getXDir(front);
    jointMtx.getTrans(pos);
    pos.scaleAdd(::sGunPointOffset, front, pos);
    Vec3f up;
    jointMtx.getYDir(up);
    MR::makeMtxFrontUpPos(pMtx, front, up, pos);
}
