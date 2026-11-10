#include "Game/MapObj/MarblePlanet.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeScaleUpCore_f1, "exeScaleUpCore.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeScaleUpCore_f2, "exeScaleUpCore.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeScaleUpCore_f3, "exeScaleUpCore.f3", 1.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCoreAndElectron_f1, "initCoreAndElectron.f1", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCoreAndElectron_f2, "initCoreAndElectron.f2", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCoreAndElectron_f3, "initCoreAndElectron.f3", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f1, "exeMove.f1", 13.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f2, "exeMove.f2", 13.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttack_f1, "exeAttack.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 440.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_crashElectron_f1, "crashElectron.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_crashElectron_f2, "crashElectron.f2", 1.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_crashElectron_f3, "crashElectron.f3", 1.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_crashElectron_f4, "crashElectron.f4", 1.2f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_attenuateVelocity_1, "exeMove.attenuateVelocity.1", 0.99f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeBreakCore_1, "exeBreakCore.1", 1);

namespace NrvMarblePlanet {
    NEW_NERVE(MarblePlanetNrvWait, MarblePlanet, Wait);
    NEW_NERVE(MarblePlanetNrvScaleUpCore, MarblePlanet, ScaleUpCore);
    NEW_NERVE(MarblePlanetNrvBreakCore, MarblePlanet, BreakCore);
};  // namespace NrvMarblePlanet

namespace NrvMarblePlanetElectron {
    NEW_NERVE(MarblePlanetElectronNrvMove, MarblePlanetElectron, Move);
    NEW_NERVE(MarblePlanetElectronNrvAttack, MarblePlanetElectron, Attack);
};  // namespace NrvMarblePlanetElectron

MarblePlanet::MarblePlanet(const char* pName) : LiveActor(pName) {
    mCorePlanetModel = 0;
    mPlanetElectrons = 0;
    mWatermelonCollision = 0;
    mNumElectrons = 3;
    mRemainingElectrons = 3;
}

void MarblePlanet::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("MarblePlanet", 0, false);
    MR::connectToScenePlanet(this);
    initHitSensor(1);
    MR::addHitSensorEnemy(this, "core", 8, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initCollisionParts(this, "MarblePlanet", getSensor(0), 0);
    mWatermelonCollision = MR::createCollisionPartsFromLiveActor(this, "WaterMelon", getSensor(0), MR::CollisionScaleType_Default);
    initEffectKeeper(0, 0);
    initSound(4, false);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, cNum_init_f2);
    MR::setClippingFar(this, -1.0f);
    MR::needStageSwitchWriteDead(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mNumElectrons);
    if (mNumElectrons < 0) {
        mNumElectrons = 0;
    }
    mRemainingElectrons = mNumElectrons;
    initCoreAndElectron();
    MR::declarePowerStar(this);
    initNerve(GET_NERVE(MarblePlanet, MarblePlanetNrvWait));
    makeActorAppeared();
}

void MarblePlanet::exeWait() {
}

void MarblePlanet::exeScaleUpCore() {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::shakeCameraNormal();
        mRemainingElectrons = mRemainingElectrons - 1;
        switch (mRemainingElectrons) {
        case 0:
            MR::emitEffect(this, "Break");
            MR::startSound(this, "SE_OJ_MARBLE_HIT_CORE_3");
            MR::startSystemSE("SE_SY_MARBLE_HIT_CORE_3");
            break;
        case 1:
            MR::emitEffect(mCorePlanetModel, "Smoke6f");
            MR::startSound(this, "SE_OJ_MARBLE_HIT_CORE_2");
            MR::startSystemSE("SE_SY_MARBLE_HIT_CORE_2");
            break;
        case 2:
        default:
            MR::emitEffect(mCorePlanetModel, "Smoke3f");
            MR::startSound(this, "SE_OJ_MARBLE_HIT_CORE_1");
            MR::startSystemSE("SE_SY_MARBLE_HIT_CORE_1");
            break;
        }

        if (mRemainingElectrons <= 0) {
            setNerve(GET_NERVE(MarblePlanet, MarblePlanetNrvBreakCore));
            return;
        } else {
            s32 electronCount = mNumElectrons;
            f32 frame = ((electronCount - mRemainingElectrons) * MR::getBckFrameMax(mCorePlanetModel)) / electronCount;
            MR::setBckFrameAndStop(mCorePlanetModel, frame);
            MR::setBtkFrameAndStop(mCorePlanetModel, frame);
        }
    }

    f32 nerveRate = MR::calcNerveRate(this, NebulaFrameClock::stepsOfMilliseconds(500));
    f32 scale = MR::getScaleWithReactionValueZeroToOne(nerveRate, cNum_exeScaleUpCore_f1, -cNum_exeScaleUpCore_f2);
    mCorePlanetModel->mScale.setAll< f32 >(MR::getLinerValue(scale, cNum_exeScaleUpCore_f3, 1.0f, 1.0f));

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        setNerve(GET_NERVE(MarblePlanet, MarblePlanetNrvWait));
    }
}

void MarblePlanet::exeBreakCore() {
    if (MR::isFirstStep(this)) {
        MR::setBckFrameAndStop(mCorePlanetModel, MR::getBckFrameMax(mCorePlanetModel));
        MR::setBtkFrameAndStop(mCorePlanetModel, MR::getBtkFrameMax(mCorePlanetModel));
    }

    if (MR::isStep(this, cStep_exeBreakCore_1)) {
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraStrong();
        MR::startAfterBossBGM();
        MR::requestAppearPowerStar(this, mPosition);
        MR::hideModel(this);
        MR::invalidateCollisionParts(this);
        MR::validateCollisionParts(mWatermelonCollision);
        MR::onSwitchDead(this);
    }
}

void MarblePlanet::startClipped() {
    mCorePlanetModel->startClipped();

    for (int i = 0; i < mNumElectrons; i++) {
        mPlanetElectrons[i]->startClipped();
    }

    LiveActor::startClipped();
}

void MarblePlanet::endClipped() {
    mCorePlanetModel->endClipped();

    for (int i = 0; i < mNumElectrons; i++) {
        mPlanetElectrons[i]->endClipped();
    }

    LiveActor::endClipped();
}

void MarblePlanet::kill() {
    MR::onSwitchDead(this);
    mCorePlanetModel->kill();
    LiveActor::kill();
}

bool MarblePlanet::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(MarblePlanet, MarblePlanetNrvScaleUpCore))) {
        return false;
    }

    if (isNerve(GET_NERVE(MarblePlanet, MarblePlanetNrvBreakCore))) {
        return false;
    }

    setNerve(GET_NERVE(MarblePlanet, MarblePlanetNrvScaleUpCore));
    return true;
}

void MarblePlanet::initCoreAndElectron() {
    mCorePlanetModel = MR::createModelObjMapObj("ビー玉惑星コア", "MarblePlanetCore", getBaseMtx());
    MR::invalidateClipping(mCorePlanetModel);
    MR::startBck(mCorePlanetModel, "MarblePlanetCore");
    MR::startBtk(mCorePlanetModel, "MarblePlanetCore");
    MR::setBckFrameAndStop(mCorePlanetModel, 0.0f);
    MR::setBtkFrameAndStop(mCorePlanetModel, 0.0f);
    mPlanetElectrons = new MarblePlanetElectron*[mNumElectrons];

    Vec3f front;
    MR::calcFrontVec(&front, this);

    Vec3f* pos;
    int i = 0;

    if (mNumElectrons > 0) {
        pos = &mPosition;
        for (; i < mNumElectrons; i++) {
            if (i != 0) {
                Vec3f up;
                MR::calcUpVec(&up, this);
                MR::rotateVecDegree(&front, up, (cNum_initCoreAndElectron_f1 / mNumElectrons));
            }

            Vec3f position;
            position.scaleAdd(cNum_initCoreAndElectron_f2, front, *pos);
            Vec3f rotation;
            rotation.setAll< f32 >((cNum_initCoreAndElectron_f3 * i) / mNumElectrons);
            mPlanetElectrons[i] = new MarblePlanetElectron(this, position, rotation, "ビー玉惑星電子");
            mPlanetElectrons[i]->initWithoutIter();
        }
    }
}

MarblePlanetElectron::MarblePlanetElectron(LiveActor* pPlanet, const Vec3f& rPosition, const Vec3f& rRotation, const char* pName)
    : LiveActor(pName) {
    mParentPlanet = static_cast< MarblePlanet* >(pPlanet);
    mElectronShadow = 0;
    mFront.x = 0.0f;
    mFront.y = 0.0f;
    mFront.z = 1.0f;
    mPosition.set(rPosition);
    mRotation.set(rRotation);
}

void MarblePlanetElectron::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("MarblePlanetElectron", 0, false);
    MR::connectToScenePlanet(this);
    initHitSensor(1);
    MR::addHitSensorEnemy(this, "body", 8, cNum_init_f3, Vec3f(0.0f, 0.0f, 0.0f));
    initBinder(cNum_init_f4, 0.0f, 0);
    initEffectKeeper(1, 0);
    MR::addEffectHitNormal(this, 0);
    initSound(4, false);
    MR::invalidateClipping(this);
    mElectronShadow = new MarblePlanetElectronShadow(this, mParentPlanet->mPosition, "電子影");
    mElectronShadow->initWithoutIter();
    MR::calcGravity(this);
    mGravity.negate();

    Pos3f mtx;
    MR::makeMtxUpNoSupportPos(&mtx, mGravity, mPosition);
    MR::setBaseTRMtx(this, mtx);
    MR::calcFrontVec(&mFront, this);
    MR::startBck(this, "MarblePlanetElectron");
    initNerve(GET_NERVE(MarblePlanetElectron, MarblePlanetElectronNrvMove));
    makeActorAppeared();
}

void MarblePlanetElectron::exeMove() {
    MR::turnDirectionToGround(this, &mFront);
    MR::attenuateVelocity(this, cNum_exeMove_attenuateVelocity_1);
    f32 mag = mVelocity.length();
    f32 scale = (mag >= cNum_exeMove_f1 ? mag : cNum_exeMove_f2);
    mVelocity.scale(scale, mFront);
    MR::startLevelSound(this, "SE_OJ_LV_MARBLE_ROTATE");
}

void MarblePlanetElectron::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_MARBLE_FLIP");
    }

    Vec3f velocity;
    velocity.sub(mParentPlanet->mPosition, mPosition);
    MR::normalize(&velocity);
    mVelocity.scale(cNum_exeAttack_f1, velocity);
}

void MarblePlanetElectron::control() {
    MR::calcGravity(this);
    mGravity.negate();

    if (isNerve(GET_NERVE(MarblePlanetElectron, MarblePlanetElectronNrvMove))) {
        MR::restrictVelocity(this, cNum_control_f1);
    }
}

void MarblePlanetElectron::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorEnemy(pReceiver)) {
        if (MR::sendMsgEnemyAttack(pReceiver, pSender)) {
            mElectronShadow->kill();
            kill();
        } else {
            bool isNear = !MR::isNear(this, pReceiver->mHost->mPosition, cNum_attackSensor_f1);

            if (!isNear) {
                if (MR::sendMsgPush(pReceiver, pSender)) {
                    MR::tryRumblePadVeryWeak(this, PLAYER_1);

                    if (!MR::isEffectValid(this, "HitMarkNormal")) {
                        MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, 0);
                    }

                    MR::killVelocityToTarget(this, pReceiver->mHost->mPosition);
                }
            }
        }
    }
}

bool MarblePlanetElectron::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(MarblePlanetElectron, MarblePlanetElectronNrvAttack))) {
        return false;
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        setNerve(GET_NERVE(MarblePlanetElectron, MarblePlanetElectronNrvAttack));
        return true;
    }

    return false;
}

bool MarblePlanetElectron::receiveMsgPush(HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (!MR::isSensorEnemy(pSender)) {
        return false;
    }

    crashElectron(pSender);

    return true;
}

void MarblePlanetElectron::crashElectron(HitSensor* pSensor) {
    Vec3f toOther;
    toOther.sub(pSensor->mHost->mPosition, mPosition);
    MR::normalize(&toOther);
    mVelocity.scaleAdd(-cNum_crashElectron_f1, toOther, mVelocity);
    MR::normalize(mVelocity, &mFront);
    mVelocity.x *= cNum_crashElectron_f2;
    mVelocity.y *= cNum_crashElectron_f3;
    mVelocity.z *= cNum_crashElectron_f4;
    MR::startSound(this, "SE_OJ_MARBLE_HIT_EACH");
}

MarblePlanetElectronShadow::MarblePlanetElectronShadow(LiveActor* pElectronPtr, const Vec3f& rVec, const char* pName) : LiveActor(pName) {
    mParentElectron = static_cast< MarblePlanetElectron* >(pElectronPtr);
    mGroundPos = &rVec;
}

void MarblePlanetElectronShadow::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("MarblePlanetElectronShadow", 0, false);
    MR::connectToScenePlanet(this);
    MR::invalidateClipping(this);
    makeActorAppeared();
}

void MarblePlanetElectronShadow::calcAndSetBaseMtx() {
    mPosition.set(*mGroundPos);
    Vec3f up;
    up.sub(mParentElectron->mPosition, *mGroundPos);
    MR::normalize(&up);
    Pos3f baseMtx;
    MR::makeMtxUpNoSupportPos(&baseMtx, up, *mGroundPos);
    MR::setBaseTRMtx(this, baseMtx);
}
