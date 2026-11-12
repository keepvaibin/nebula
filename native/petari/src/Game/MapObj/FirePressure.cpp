#include "Game/MapObj/FirePressure.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/FirePressureBullet.hpp"
#include "Game/MapObj/FirePressureBulletHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

FirePressure::FirePressure(const char* pName) : PressureBase(pName), mIsDisableCollision(false) {
    lumacore::setClass(this, lumacore::kClassId< FirePressure >);
}

void FirePressure::init(const TableIter& rIter) {
    PressureBase::init(rIter);
    MR::getPlacementArg7NoInit(rIter, &mIsDisableCollision);
}

void FirePressure::initBullet(const TableIter& /*rIter*/) {
    MR::createSceneObj(SceneObj_FirePressureBulletHolder);
}

bool FirePressure::shotBullet(f32 speed) {
    FirePressureBullet* pBullet = MR::getSceneObj< FirePressureBulletHolder >(SceneObj_FirePressureBulletHolder)->callEmptyBullet();

    Pos3f gunPoint;
    calcGunPointFromCannon(&gunPoint);
    pBullet->shotFireBullet(this, gunPoint, speed, isShotTypeFollow(), mIsDisableCollision);
    return true;
}

void FirePressure::calcGunPointFromCannon(Pos3f* pMtx) {
    Pos3f jointMtx(MR::getJointMtx(this, "Cannon1"));
    Vec3f front;
    jointMtx.getXDir(front);
    Vec3f muzzlePos;
    jointMtx.getTrans(muzzlePos);
    muzzlePos.scaleAdd(200.0f, front, muzzlePos);
    Vec3f up;
    jointMtx.getYDir(up);
    MR::makeMtxFrontUpPos(pMtx, front, up, muzzlePos);
}
