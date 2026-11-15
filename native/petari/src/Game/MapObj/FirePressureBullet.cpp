#include "Game/MapObj/FirePressureBullet.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvFirePressureBullet {
    NEW_NERVE(FirePressureBulletNrvFly, FirePressureBullet, Fly);
};  // namespace NrvFirePressureBullet

FirePressureBullet::FirePressureBullet(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< FirePressureBullet >);
    mFront.x = 0.0f;
    mFront.y = 0.0f;
    mFront.z = 0.0f;
    mSpeed = 0.0f;
    mIsHoming = false;
    mCanCrash = false;
}

void FirePressureBullet::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("FireBullet", nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addHitSensorEnemyAttack(this, "body", 8, 100.0f, Vec3f(0.0f, 0.0f, 0.0f));
    initBinder(100.0f, 0.0f, 0);
    MR::onCalcGravity(this);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    MR::initShadowVolumeSphere(this, 75.0f);
    initNerve(GET_NERVE(FirePressureBullet, FirePressureBulletNrvFly));
    makeActorDead();
}

void FirePressureBullet::kill() {
    MR::startSound(this, "SE_OJ_F_PRESSURE_FIRE_BURST");
    LiveActor::kill();
}

void FirePressureBullet::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, mGravity, mFront);
    MR::setBaseTRMtx(this, mtx);
}

void FirePressureBullet::shotFireBullet(LiveActor* /*pFirePressure*/, const Pos3f& rMtx, const f32& rSpeed, bool isHoming, bool canCrash) {
    mSpeed = rSpeed;
    mIsHoming = isHoming;
    mCanCrash = canCrash;

    rMtx.getZDir(mFront);
    mVelocity.scale(mSpeed, mFront);
    rMtx.getTrans(mPosition);
    mRotation.zero();
    MR::validateHitSensors(this);
    MR::invalidateClipping(this);
    setNerve(GET_NERVE(FirePressureBullet, FirePressureBulletNrvFly));
    makeActorAppeared();
}

void FirePressureBullet::exeFly() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Shot");
        MR::startBtk(this, "FireBullet");
    }

    if (MR::isBckOneTimeAndStopped(this)) {
        MR::startBck(this, "Move");
    }

    MR::startLevelSound(this, "SE_OJ_LV_F_PRESSURE_BULLET");

    if (mIsHoming && !(getNerveStep() % 5)) {
        MR::turnDirectionToTargetDegree(this, &mFront, *MR::getPlayerPos(), 3.0f);
        mVelocity.scale(mSpeed, mFront);
    }

    if (isCrash()) {
        kill();
    } else if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(5))) {
        kill();
    }
}

void FirePressureBullet::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorEnemy(pReceiver) || MR::isSensorMapObj(pReceiver)) {
        kill();
    } else if (MR::isSensorPlayer(pReceiver) && MR::sendMsgEnemyAttackFire(pReceiver, pSender)) {
        kill();
    }
}

bool FirePressureBullet::isCrash() const {
    if (mCanCrash) {
        return false;
    }

    return MR::isBinded(this);
}
