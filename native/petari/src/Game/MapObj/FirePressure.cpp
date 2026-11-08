#include "Game/MapObj/FirePressure.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/FirePressureBullet.hpp"
#include "Game/MapObj/FirePressureBulletHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>

FirePressure::~FirePressure() {
}

FirePressure::FirePressure(const char* pName) : PressureBase(pName) {
    mDisableCollsion = false;
}

void FirePressure::init(const TableIter& rIter) {
    PressureBase::init(rIter);
    MR::getPlacementArg7NoInit(rIter, &mDisableCollsion);
}

void FirePressure::initBullet(const TableIter& rIter) {
    MR::createSceneObj(SceneObj_FirePressureBulletHolder);
}

bool FirePressure::shotBullet(f32 a1) {
    FirePressureBulletHolder* hldr = MR::getSceneObj< FirePressureBulletHolder >(SceneObj_FirePressureBulletHolder);
    FirePressureBullet* bullet = hldr->callEmptyBullet();

    // do we have an empty bullet slot available?
    if (bullet == nullptr) {
        return false;
    } else {
        Pos3f gunPoint;
        calcGunPointFromCannon(&gunPoint);
        bullet->shotFireBullet(this, gunPoint, a1, isShotTypeFollow(), mDisableCollsion);
        return true;
    }
}

void FirePressure::calcGunPointFromCannon(Pos3f* pOutPoint) {
    Pos3f jointMtx(MR::getJointMtx(this, "Cannon1"));
    Vec3f front;
    jointMtx.getXDir(front);
    Vec3f muzzlePos;
    jointMtx.getTrans(muzzlePos);
    muzzlePos.scaleAdd(200.0f, front, muzzlePos);
    Vec3f up;
    jointMtx.getYDir(up);
    MR::makeMtxFrontUpPos(pOutPoint, front, up, muzzlePos);
}
