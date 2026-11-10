#include "Game/MapObj/BlueStarCupsulePlanet.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapPartsRailMover.hpp"
#include "Game/MapObj/MapPartsRotator.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/BaseMatrixFollowTargetHolder.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 600.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_isReleaseForce_isNearPlayer_1, "isReleaseForce.isNearPlayer.1", 0.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeHitPointer_1, "exeHitPointer.1", 3);


namespace {
    NEBULA_TUNABLE(f32, sPointableRange, 3400.0f);

    static const BlueStarCupsulePlanetParam sParams[] = {{"BlueStarCupsulePlanet", 500.0f}, {"UFOBlueStarCupsule", 200.0f}};

    const BlueStarCupsulePlanetParam* getParam(const char* pName) {
        for (u32 i = 0; i < ARRAY_SIZE(sParams); i++) {
            if (MR::isEqualString(pName, sParams[i].mBlueStarName)) {
                return &sParams[i];
            }
        }

        return nullptr;
    }
};  // namespace

namespace NrvBlueStarCupsulePlanet {
    NEW_NERVE(BlueStarCupsulePlanetNrvWait, BlueStarCupsulePlanet, Wait);
    NEW_NERVE(BlueStarCupsulePlanetNrvPointable, BlueStarCupsulePlanet, Pointable);
    NEW_NERVE(BlueStarCupsulePlanetNrvHitPointer, BlueStarCupsulePlanet, HitPointer);
    NEW_NERVE(BlueStarCupsulePlanetNrvActive, BlueStarCupsulePlanet, Active);
};  // namespace NrvBlueStarCupsulePlanet

BlueStarCupsulePlanet::BlueStarCupsulePlanet(const char* pName) : LiveActor(pName), mRailMover(), mRotator(), mName(), mIsStepUpdatable() {
    MR::createGCapture();
}

void BlueStarCupsulePlanet::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::getObjectName(&mName, rIter);
    initModelManagerWithAnm(mName, nullptr, false);
    MR::connectToScene(this, MR::MovementType_Planet, MR::CalcAnimType_Planet, MR::DrawBufferType_Planet, MR::DrawType_None);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    initNerve(GET_NERVE(BlueStarCupsulePlanet, BlueStarCupsulePlanetNrvWait));
    initHitSensor(1);

    Vec3f offset, vec;
    Pos3f mtx;
    MR::addHitSensorMapObj(this, "body", 16, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initCollisionParts(this, mName, getSensor("body"), nullptr);
    MR::copyJointPos(this, "Root", &vec);
    mtx.set(getBaseMtx());
    mtx.invert(mtx);
    mtx.mult(vec, offset);
    // A model name outside the table takes the first entry's radius.
    const BlueStarCupsulePlanetParam* pParam = ::getParam(mName);
    MR::initStarPointerTarget(this, pParam != nullptr ? pParam->mRadius : ::sParams[0].mRadius, offset);
    MR::setClippingTypeSphere(this, cNum_init_f1);

    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
        mRailMover = new MapPartsRailMover(this);
        mRailMover->init(rIter);
        mRailMover->start();
    }

    mRotator = new MapPartsRotator(this);
    mRotator->init(rIter);
    mRotator->start();
    MR::addBaseMatrixFollowTarget(this, rIter, nullptr, nullptr);

    if (MR::useStageSwitchReadB(this, rIter)) {
        MR::listenStageSwitchOnOffB(this, MR::Functor(this, &BlueStarCupsulePlanet::activateMapPartsFunction),
                                    MR::Functor(this, &BlueStarCupsulePlanet::deactivateMapPartsFunction));
    } else if (mRailMover || mRotator) {
        activateMapPartsFunction();
    }

    makeActorAppeared();
    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    }

    if (MR::tryRegisterDemoCast(this, rIter) && MR::isRegisteredDemoActionAppear(this)) {
        makeActorDead();
    }

    MR::startAllAnim(this, "Wait");
}

void BlueStarCupsulePlanet::control() {
    updatePose();
    mIsStepUpdatable = false;
}

void BlueStarCupsulePlanet::startClipped() {
    LiveActor::startClipped();
    MR::deleteEffectAll(this);
}

void BlueStarCupsulePlanet::endClipped() {
    LiveActor::endClipped();
    emitNerveEffect();
}

void BlueStarCupsulePlanet::updatePose() {
    if (mIsStepUpdatable) {
        return;
    }

    if (mRailMover != nullptr) {
        mRailMover->movement();
        if (mRailMover->isWorking()) {
            if (MR::isEqualString(mName, "UFOBlueStarCupsule")) {
                MR::startLevelSound(this, "SE_OJ_LV_UFO_BLUE_STAR_MOVE");
            }
            mPosition.set(mRailMover->mStartPos);
        }
    }

    if (mRotator != nullptr) {
        mRotator->movement();
    }

    calcAnim();
    mIsStepUpdatable = true;
}

void BlueStarCupsulePlanet::decidedTarget() {
    if (!isNerve(GET_NERVE(BlueStarCupsulePlanet, BlueStarCupsulePlanetNrvActive))) {
        setNerve(GET_NERVE(BlueStarCupsulePlanet, BlueStarCupsulePlanetNrvActive));
    }
}

void BlueStarCupsulePlanet::releasedTarget() {
    if (!isNerve(GET_NERVE(BlueStarCupsulePlanet, BlueStarCupsulePlanetNrvWait))) {
        setNerve(GET_NERVE(BlueStarCupsulePlanet, BlueStarCupsulePlanetNrvWait));
        MR::startAllAnim(this, "Wait");
    }
}

void BlueStarCupsulePlanet::getTargetPosition(Vec3f* pPos) {
    MR::copyJointPos(this, "Root", pPos);
}

bool BlueStarCupsulePlanet::isReleaseForce() const {
    return MR::isNearPlayer(this, cNum_isReleaseForce_isNearPlayer_1);
}

f32 BlueStarCupsulePlanet::getPointableRange() const {
    return ::sPointableRange;
}

void BlueStarCupsulePlanet::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        emitNerveEffect();
    }
    updatePose();

    if (isPointable()) {
        setNerve(GET_NERVE(BlueStarCupsulePlanet, BlueStarCupsulePlanetNrvPointable));
    }
}

void BlueStarCupsulePlanet::exePointable() {
    if (MR::isFirstStep(this)) {
        emitNerveEffect();
    }
    MR::noticeInTouchableRange();
    updatePose();

    if (!isPointable()) {
        setNerve(GET_NERVE(BlueStarCupsulePlanet, BlueStarCupsulePlanetNrvWait));
    } else if (MR::isStarPointerPointing(this, PLAYER_1, true, "弱")) {
        if (MR::requestGCaptureTarget(this)) {
            MR::invalidateClipping(this);
            setNerve(GET_NERVE(BlueStarCupsulePlanet, BlueStarCupsulePlanetNrvHitPointer));
        }
    }
}

void BlueStarCupsulePlanet::exeHitPointer() {
    if (!MR::isRequestedGCaptureTarget(this)) {
        setNerve(GET_NERVE(BlueStarCupsulePlanet, BlueStarCupsulePlanetNrvWait));
        return;
    }

    if (MR::isFirstStep(this)) {
        emitNerveEffect();
    }

    if (MR::isStep(this, cStep_exeHitPointer_1)) {
        MR::startCSSound("CS_STAR_POWER", nullptr, PLAYER_1);
    }

    updatePose();
    MR::noticeInTouchableRange();
    MR::startLevelSound(this, "SE_OJ_LV_MAGIC_PNT_G_POINT");

    bool pointable = isPointable();
    if (MR::isStarPointerPointing(this, PLAYER_1, true, "弱") && pointable) {
        MR::requestGCaptureTarget(this);
    }

    if (!pointable) {
        MR::unrequestGCaptureTarget(this);
    }
}

void BlueStarCupsulePlanet::exeActive() {
    if (MR::isFirstStep(this)) {
        emitNerveEffect();
        MR::startAllAnim(this, "React");
        MR::startSound(this, "SE_OJ_MAGIC_PNT_G_ON");
    }

    updatePose();
    MR::noticeInTouchableRange();
    MR::startLevelSound(this, "SE_OJ_LV_MAGIC_PNT_G_PULL", MR::calcDistanceToPlayer(this));
    MR::startLevelSound(this, "SE_OJ_LV_MAGIC_PNT_G_POINT");
}

void BlueStarCupsulePlanet::calcAndSetBaseMtx() {
    if (mRotator != nullptr && mRotator->isWorking()) {
        Pos3f mtx;
        mtx.identity();
        mtx.set(mRotator->getRotateMtx());
        mtx.setTrans(mPosition);
        MR::setBaseTRMtx(this, mtx);
    } else {
        LiveActor::calcAndSetBaseMtx();
    }
}

bool BlueStarCupsulePlanet::isPointable() const {
    Vec3f pos, up;
    MR::copyJointPos(this, "Root", &pos);
    f32 distance = pos.distance(*MR::getPlayerPos());

    if (MR::isEqualString(mName, "UFOBlueStarCupsule")) {
        MR::calcUpVec(&up, this);
        if (up.dot(*MR::getPlayerPos() - mPosition) <= 0.0f) {
            return false;
        }
    }

    return 0.0f < distance && distance < ::sPointableRange;
}

void BlueStarCupsulePlanet::emitNerveEffect() {
    if (isNerve(GET_NERVE(BlueStarCupsulePlanet, BlueStarCupsulePlanetNrvWait))) {
        MR::deleteEffect(this, "TargetLight");
        MR::deleteEffect(this, "Touch");
        MR::deleteEffect(this, "Active");
    } else if (isNerve(GET_NERVE(BlueStarCupsulePlanet, BlueStarCupsulePlanetNrvPointable))) {
        MR::emitEffect(this, "TargetLight");
        MR::emitEffect(this, "TouchAble");
        MR::deleteEffect(this, "Touch");
        MR::deleteEffect(this, "Active");
    } else if (isNerve(GET_NERVE(BlueStarCupsulePlanet, BlueStarCupsulePlanetNrvHitPointer))) {
        MR::emitEffect(this, "TargetLight");
        MR::emitEffect(this, "Touch");
        MR::deleteEffect(this, "Active");
    } else if (isNerve(GET_NERVE(BlueStarCupsulePlanet, BlueStarCupsulePlanetNrvActive))) {
        MR::emitEffect(this, "TargetLight");
        MR::deleteEffect(this, "Touch");
        MR::emitEffect(this, "Active");
    }
}

void BlueStarCupsulePlanet::activateMapPartsFunction() {
    if (mRailMover != nullptr) {
        mRailMover->mIsActive = true;
    }

    if (mRotator != nullptr) {
        mRotator->mIsActive = true;
    }
}

void BlueStarCupsulePlanet::deactivateMapPartsFunction() {
    if (mRailMover != nullptr) {
        mRailMover->mIsActive = false;
    }

    if (mRotator != nullptr) {
        mRotator->mIsActive = false;
    }
}
