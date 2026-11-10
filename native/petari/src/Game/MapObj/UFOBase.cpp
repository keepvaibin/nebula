#include "Game/MapObj/UFOBase.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/MapObj/MapPartsRailMover.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapPartsUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_geometry.hpp>
#include <cstddef>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSubModel_f1, "initSubModel.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSubModel_f2, "initSubModel.f2", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensorType_f1, "initSensorType.f1", 100.0f);

namespace NrvUFOBase {
    NEW_NERVE(UFOBaseNrvWait, UFOBase, Wait);
    NEW_NERVE(UFOBaseNrvWaitForPlayerOn, UFOBase, WaitForPlayerOn);
    NEW_NERVE(UFOBaseNrvMove, UFOBase, Move);
    NEW_NERVE(UFOBaseNrvBreak, UFOBase, Break);
};  // namespace NrvUFOBase

UFOBase::UFOBase(const char* pName) : LiveActor(pName) {
    mCollisionParts = nullptr;
    mLODCtrl = nullptr;
    mModel = nullptr;
    mRailMover = nullptr;
    mFront.x = 0.0f;
    mFront.y = 0.0f;
    mFront.z = 1.0f;
    mRotateSpeed = 0.0f;
    mMoveConditionType = 0;
    mIsRailDone = false;
    mKind = Kind_Other;
}

void UFOBase::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    const char* name;
    MR::getObjectName(&name, rIter);
    initModelManagerWithAnm(name, nullptr, false);
    if (MR::isEqualString("UFONormalB", name)) {
        mKind = Kind_NormalB;
    } else if (MR::isEqualString("UFOBattleStageC", name)) {
        mKind = Kind_BattleStageC;
    } else if (MR::isEqualString("UFOBattleStageD", name)) {
        mKind = Kind_BattleStageD;
    }
    MR::connectToSceneCollisionMapObj(this);
    initHitSensor(1);
    initSensorType();
    MR::initCollisionParts(this, name, getSensor(nullptr), nullptr);
    mCollisionParts = MR::tryCreateCollisionMoveLimit(this, getBaseMtx(), getSensor(nullptr));
    initEffectKeeper(0, nullptr);
    initSound(4, false);

    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
        mRailMover = new MapPartsRailMover(this);
        mRailMover->init(rIter);
    }

    MR::setClippingTypeSphereContainsModelBoundingBox(this, cNum_init_f1);
    MR::setGroupClipping(this, rIter, 32);
    MR::useStageSwitchReadB(this, rIter);
    MR::useStageSwitchWriteDead(this, rIter);
    MR::useStageSwitchSleep(this, rIter);
    MR::addBaseMatrixFollowTarget(this, rIter, nullptr, nullptr);
    initSubModel(rIter, name);
    MR::tryRegisterDemoCast(this, rIter);
    MR::calcFrontVec(&mFront, this);
    MR::getMapPartsArgRotateSpeed(&mRotateSpeed, rIter);
    mRotateSpeed *= cNum_init_f2;
    MR::getMapPartsArgMoveConditionType(&mMoveConditionType, rIter);
    MR::tryStartAllAnim(this, name);

    if (MR::isMoveStartTypeUnconditional(mMoveConditionType)) {
        initNerve(GET_NERVE(UFOBase, UFOBaseNrvWait));
    } else {
        initNerve(GET_NERVE(UFOBase, UFOBaseNrvWaitForPlayerOn));
    }
    makeActorAppeared();
}

void UFOBase::exeWait() {
    if (!MR::isValidSwitchB(this) || MR::isOnSwitchB(this)) {
        setNerve(GET_NERVE(UFOBase, UFOBaseNrvMove));
    }
}

void UFOBase::exeWaitForPlayerOn() {
    if (MR::isOnPlayer(getSensor(nullptr)) && (!MR::isValidSwitchB(this) || MR::isOnSwitchB(this))) {
        setNerve(GET_NERVE(UFOBase, UFOBaseNrvMove));
    }
}

void UFOBase::exeMove() {
    if (MR::isFirstStep(this)) {
        if (mRailMover) {
            mRailMover->start();
            if (mKind == Kind_BattleStageC) {
                MR::startSound(this, "SE_OJ_UFO_BTL_C_START");
            }
        }
    }
    MR::rotateVecDegree(&mFront, mGravity, mRotateSpeed);
}

void UFOBase::exeBreak() {
    kill();
};

void UFOBase::initSensorType() {
    MR::addBodyMessageSensorMapObj(this);
}

void UFOBase::makeActorDead() {
    if (mModel) {
        mModel->makeActorDead();
    }

    LiveActor::makeActorDead();
}

void UFOBase::kill() {
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    LiveActor::kill();
}

void UFOBase::control() {
    if (mCollisionParts) {
        mCollisionParts->setMtx();
    }
    mLODCtrl->update();

    if (mKind == Kind_NormalB) {
        MR::startLevelSound(this, "SE_OJ_LV_UFO_NORM_B_WORK");
    } else if (mKind == Kind_BattleStageD) {
        MR::startLevelSound(this, "SE_OJ_LV_UFO_BTL_D_ROTATE");
    }

    if (mRailMover) {
        mRailMover->movement();
        mPosition.x = mRailMover->mStartPos.x;
        mPosition.y = mRailMover->mStartPos.y;
        mPosition.z = mRailMover->mStartPos.z;
        if (mRailMover->isWorking()) {
            if (mKind == Kind_BattleStageC) {
                MR::startLevelSound(this, "SE_OJ_LV_UFO_BTL_C_MOVE");
            } else {
                MR::startLevelSound(this, "SE_OJ_LV_UFO_MOVE");
            }
        }
        if (mRailMover->isDone() && !mIsRailDone) {
            MR::tryRumblePadMiddle(this, PLAYER_1);
            MR::shakeCameraNormal();
            if (mKind == Kind_BattleStageC) {
                MR::startSound(this, "SE_OJ_UFO_BTL_C_STOP");
            } else {
                MR::startSound(this, "SE_OJ_UFO_MOVE_END");
            }
            mIsRailDone = true;
        }
    }
}

void UFOBase::calcAndSetBaseMtx() {
    Vec3f vec;
    Pos3f mtx;
    mtx.identity();
    MR::calcUpVec(&vec, this);
    MR::makeMtxUpFrontPos(&mtx, vec, mFront, mPosition);
    MR::setBaseTRMtx(this, mtx);
}

void UFOBase::initSubModel(const TableIter& rIter, const char* name) {
    mLODCtrl = MR::createLodCtrlMapObj(this, rIter, cNum_initSubModel_f1);
    if (MR::isExistSubModel(name, "Bloom")) {
        MR::FormattedName buf("%sBloom", name);
        const char* name2 = mName;
        mModel = MR::createModelObjBloomModel(name2, buf.c_str(), getBaseMtx());
        f32 num = cNum_initSubModel_f2;
        MR::calcModelBoundingRadius(&num, this);
        MR::setClippingFarMax(mModel);
        MR::setClippingTypeSphere(mModel, num);
        MR::startBpk(mModel, buf.c_str());
    }
}

UFOSolid::UFOSolid(const char* pName) : UFOBase(pName) {
}

UFOBreakable::UFOBreakable(const char* pName) : UFOBase(pName) {
}

void UFOBreakable::initSensorType() {
    MR::addHitSensor(this, "body", ATYPE_KILLER_TARGET_MAPOBJ, 8, cNum_initSensorType_f1, Vec3f(0.0f, 0.0f, 0.0f));
}

bool UFOBreakable::receiveMsgEnemyAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgExplosionAttack(msg) && !isNerve(GET_NERVE(UFOBase, UFOBaseNrvBreak))) {
        setNerve(GET_NERVE(UFOBase, UFOBaseNrvBreak));
        return true;
    }
    return false;
}
