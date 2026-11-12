#include "Game/Boss/KoopaShockWave.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvKoopaShockWave {
    NEW_NERVE(KoopaShockWaveNrvWaveAttack, KoopaShockWave, WaveAttack);
};  // namespace NrvKoopaShockWave

KoopaShockWave::KoopaShockWave(Koopa* pKoopa)
    : LiveActor("球状衝撃波"), mKoopa(pKoopa), mAngle(), mUp(0.0f, 1.0f, 0.0f), mFront(0.0f, 0.0f, 1.0f), mPartsModel(), mShadow() {
    lumacore::setClass(this, lumacore::kClassId< KoopaShockWave >);
    mKoopaMtx.identity();
    mBaseMtx.identity();
    mMtx.identity();
}

void KoopaShockWave::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("KoopaShockWave", nullptr, false);

    MR::startAction(this, "Spread");
    MR::setBckFrameAndStop(this, 0.0f);

    MR::connectToSceneEnemy(this);

    initHitSensor(1);
    MR::addHitSensorCallbackEnemyAttack(this, "Attack", 16, 15.0f);

    initSound(8, false);

    initNerve(GET_NERVE(KoopaShockWave, KoopaShockWaveNrvWaveAttack));

    MR::invalidateClipping(this);

    PartsModel* pPartsModel = MR::createBloomModel(this, nullptr);
    mPartsModel = pPartsModel;
    MR::startAction(pPartsModel, "Spread");
    MR::setBckFrameAndStop(pPartsModel, 0.0f);

    mPartsModel->kill();
    MR::invalidateClipping(mPartsModel);

    ModelObj* pModelObj = MR::createModelObjNoSilhouettedMapObj("衝撃波の影", "KoopaShockWaveShadow", mMtx);
    mShadow = pModelObj;
    MR::startAction(pModelObj, "Spread");
    MR::setBckFrameAndStop(pModelObj, 0.0f);

    MR::invalidateClipping(mShadow);
    mShadow->kill();

    makeActorDead();
}

void KoopaShockWave::appear() {
    LiveActor::appear();
    MR::invalidateHitSensors(this);

    mAngle = 0.0f;

    setNerve(GET_NERVE(KoopaShockWave, KoopaShockWaveNrvWaveAttack));
}

void KoopaShockWave::makeActorDead() {
    LiveActor::makeActorDead();
    mPartsModel->makeActorDead();
    mShadow->makeActorDead();
}

namespace {
    // The wave's frame, at scale * height above the planet's center along up.
    void makeShockWaveMtx(Pos3f* pDest, const Vec3f& rUp, const Vec3f& rFront, const Vec3f& rCenter, f32 scale, f32 height) {
        MR::makeMtxUpFrontPos(pDest, rUp, rFront, rUp * scale * height + rCenter);
    }
};  // namespace

void KoopaShockWave::exeWaveAttack() {
    if (MR::isFirstStep(this)) {
        mPartsModel->makeActorAppeared();
        mShadow->makeActorAppeared();

        MR::startBrk(this, "Erase");
        MR::startBrk(mShadow, "Erase");
        MR::startBrk(mPartsModel, "Erase");

        MR::setBrkRate(this, 0.0f);
        MR::setBrkRate(mShadow, 0.0f);
        MR::setBrkRate(mPartsModel, 0.0f);

        MR::startAction(this, "Spread");
        MR::startAction(mShadow, "Spread");
        MR::startAction(mPartsModel, "Spread");

        mUp.set(mKoopa->mPosition - KoopaFunction::getPlanetCenterPos(mKoopa));
        mFront.set(mKoopa->mFront);
        MR::normalizeOrZero(&mUp);

        Vec3f surfacePos = mUp;
        surfacePos.scale(KoopaFunction::getPlanetRadius(mKoopa));

        surfacePos.add(KoopaFunction::getPlanetCenterPos(mKoopa));

        MR::makeMtxUpFrontPos(&mKoopaMtx, -mKoopa->mGravity, mKoopa->mFront, surfacePos);

        MR::startSound(mKoopa, "SE_BM_KOOPA_SWAVE_SHOOT");

        MR::validateHitSensors(this);
    }

    mAngle = MR::calcNerveValue(this, NebulaFrameClock::stepsOfSeconds(6), 0.0f, 180.0f);

    f32 scale = MR::cosDegree(mAngle);
    ::makeShockWaveMtx(&mBaseMtx, mUp, mFront, KoopaFunction::getPlanetCenterPos(mKoopa), scale, 1300.0f);
    ::makeShockWaveMtx(&mMtx, mUp, mFront, KoopaFunction::getPlanetCenterPos(mKoopa), scale, 1260.0f);

    f32 frame = MR::sinDegree(mAngle) * 10.0f;
    MR::setBckFrameAndStop(this, frame);
    MR::setBckFrameAndStop(mPartsModel, frame);
    MR::setBckFrameAndStop(mShadow, frame);

    f32 volume;
    if (mAngle <= 90.0f) {
        volume = mAngle / 90.0f;
    } else {
        volume = 1.0f - (mAngle - 90.0f) / 90.0f;
    }
    volume *= 100.0f;
    MR::startLevelSound(mKoopa, "SE_BM_LV_KOOPA_SWAVE_MOVE", volume);

    if (MR::isStep(this, 350)) {
        MR::startBrk(this, "Erase");
        MR::startBrk(mShadow, "Erase");
        MR::startBrk(mPartsModel, "Erase");
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(6))) {
        kill();
    }
}

void KoopaShockWave::updateHitSensor(HitSensor* pSensor) {
    Vec3f playerPos = *MR::getPlayerPos();
    MR::calcLocalVec(&playerPos, mKoopaMtx);

    f32 degree = MR::toDegree(MR::atan2(-playerPos.z, playerPos.x)) + 180.0f;

    Pos3f matrix = mKoopaMtx;
    matrix.setTrans(KoopaFunction::getPlanetCenterPos(mKoopa));

    Vec3f rotateVec;
    rotateVec.set(0.0f, KoopaFunction::getPlanetRadius(mKoopa), 0.0f);
    MR::rotateVecDegree(&rotateVec, Vec3f(0.0f, 0.0f, 1.0f), mAngle);
    MR::rotateVecDegree(&rotateVec, Vec3f(0.0f, 1.0f, 0.0f), degree);

    matrix.mult(rotateVec, rotateVec);
    MR::setSensorPos(pSensor, rotateVec);
}

void KoopaShockWave::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mBaseMtx);
}

void KoopaShockWave::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver) && MR::isOnGroundPlayer() && MR::sendMsgEnemyAttack(pReceiver, pSender)) {
        mKoopa->receiveMessage(ACTMES_KOOPA_PLAYER_DAMAGE, pSender, MR::getMessageSensor());
    }
}
