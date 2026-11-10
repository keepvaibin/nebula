#include "Game/Enemy/HammerHeadPackun.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointRumbler.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_geometry.hpp>



#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 169.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 8.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 0.06f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f1, "kill.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f2, "kill.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeNumb_f1, "exeNumb.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f2, "initSensor.f2", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f3, "initSensor.f3", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f4, "initSensor.f4", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f5, "initSensor.f5", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f6, "initSensor.f6", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f7, "initSensor.f7", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f8, "initSensor.f8", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f9, "initSensor.f9", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLeaf_f1, "initLeaf.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLeaf_f2, "initLeaf.f2", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLeaf_f3, "initLeaf.f3", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLeaf_f4, "initLeaf.f4", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLeaf_f5, "initLeaf.f5", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isTargetInRange_f1, "isTargetInRange.f1", 800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isInFieldOfView_f1, "isInFieldOfView.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isInFieldOfView_f2, "isInFieldOfView.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isInFieldOfView_f3, "isInFieldOfView.f3", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_turnTo_f1, "turnTo.f1", 180.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeAttack_1, "exeAttack.1", 156);
NEBULA_TUNABLE_NAMED(s32, cStep_exeDamage_1, "exeDamage.1", 76);
NEBULA_TUNABLE_NAMED(s32, cStep_exeRise_1, "exeRise.1", 56);
NEBULA_TUNABLE_NAMED(s32, cStep_exeCrow_1, "exeCrow.1", 45);
NEBULA_TUNABLE_NAMED(s32, cStep_isAttackSensorValid_1, "isAttackSensorValid.1", 40);
NEBULA_TUNABLE_NAMED(s32, cStep_isChance_1, "isChance.1", 50);

HammerHeadPackun::HammerHeadPackun(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< HammerHeadPackun >);
    mLeafModel = nullptr;
    mImmunityTime = 0;
    mUp.x = 0.0f;
    mUp.y = 1.0f;
    mUp.z = 0.0f;
    mFront.x = 0.0f;
    mFront.y = 0.0f;
    mFront.z = 1.0f;
    mAttackDir.x = 0.0f;
    mAttackDir.y = 0.0f;
    mAttackDir.z = 1.0f;
    mJointRumbler = nullptr;
    mNumbTime = 0;
}

namespace {
    NEW_NERVE(HammerHeadPackunNrvWait, HammerHeadPackun, Wait);
    NEW_NERVE(HammerHeadPackunNrvSearch, HammerHeadPackun, Search);
    NEW_NERVE(HammerHeadPackunNrvAttack, HammerHeadPackun, Attack);
    NEW_NERVE(HammerHeadPackunNrvRest, HammerHeadPackun, Rest);
    NEW_NERVE(HammerHeadPackunNrvRecover, HammerHeadPackun, Recover);
    NEW_NERVE(HammerHeadPackunNrvDamage, HammerHeadPackun, Damage);
    NEW_NERVE(HammerHeadPackunNrvRise, HammerHeadPackun, Rise);
    NEW_NERVE(HammerHeadPackunNrvHit, HammerHeadPackun, Hit);
    NEW_NERVE(HammerHeadPackunNrvCrow, HammerHeadPackun, Crow);
    NEW_NERVE(HammerHeadPackunNrvSmashKill, HammerHeadPackun, SmashKill);
    NEW_NERVE(HammerHeadPackunNrvBlowKill, HammerHeadPackun, BlowKill);
    NEW_NERVE_ONEND(HammerHeadPackunNrvNumb, HammerHeadPackun, Numb, Numb);
};  // namespace

void HammerHeadPackun::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initPosture();
    initModelManagerWithAnm("PackunFlower", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initSensor();
    initEffectKeeper(1, nullptr);
    MR::addEffect(this, "PointerTouchManual");
    MR::setEffectHostMtx(this, "PointerTouchManual", MR::getJointMtx(this, "MouthUp"));
    initSound(4, false);
    initNerve(GET_NERVE_ANON(HammerHeadPackunNrvWait));
    MR::useStageSwitchWriteDead(this, rIter);
    initShadow();
    initLeaf(rIter);
    MR::initStarPointerTargetAtJoint(this, "Head", cNum_init_f1, Vec3f(cNum_init_f2, 0.0f, 0.0f));
    MR::declareStarPiece(this, 9);
    mJointRumbler = new JointRumbler(this, "AllRoot", cNum_init_f3, cNum_init_f4, 60, true, 0);
    makeActorAppeared();
}

void HammerHeadPackun::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    setNerve(GET_NERVE_ANON(HammerHeadPackunNrvWait));
}

void HammerHeadPackun::kill() {
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    MR::startSound(this, "SE_EM_HHPACKUN_DEAD");
    Vec3f pos;
    MR::copyJointPos(this, "Head", &pos);
    MR::appearStarPiece(this, pos, 9, cNum_kill_f1, cNum_kill_f2, false);
    MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    LiveActor::kill();
}

void HammerHeadPackun::control() {
    mJointRumbler->update();
}

void HammerHeadPackun::exeWait() {
    if (MR::isFirstStep(this)) {
        const char* wait = "Wait";
        MR::startBck(this, wait);
        MR::startBck(mLeafModel, wait);
    }

    if (!tryShiftNumb() && isTargetInRange()) {
        setNerve(GET_NERVE_ANON(HammerHeadPackunNrvSearch));
    }
}

void HammerHeadPackun::exeSearch() {
    Vec3f dir;
    if (calcPlayerDir(&dir) && !tryShiftNumb()) {
        turnTo(dir, 1.0f);
        if (isInFieldOfView(dir)) {
            setNerve(GET_NERVE_ANON(HammerHeadPackunNrvAttack));
        }
    }
}

void HammerHeadPackun::exeAttack() {
    if (MR::isFirstStep(this)) {
        const char* attack = "Attack";
        MR::startBck(this, attack);
        MR::startBck(mLeafModel, attack);
    }

    Vec3f dir;
    if (!tryShiftNumb()) {
        if (MR::isStep(this, cStep_exeAttack_1)) {
            MR::shakeCameraNormalStrong();
            MR::tryRumblePadStrong(this, PLAYER_1);
        }

        if (MR::isLessEqualStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
            if (calcPlayerDir(&dir)) {
                turnTo(dir, 1.0f);
            }
        } else {
            turnToAttackVec(60);
        }
        MR::setNerveAtBckStopped(this, GET_NERVE_ANON(HammerHeadPackunNrvRest));
    }
}

void HammerHeadPackun::exeRest() {
    if (MR::isFirstStep(this)) {
        const char* chance = "Chance";
        MR::startBck(this, chance);
        MR::startBck(mLeafModel, chance);
        mImmunityTime = 0;
    }

    if (!tryShiftNumb()) {
        MR::setNerveAtStep(this, GET_NERVE_ANON(HammerHeadPackunNrvRecover), NebulaFrameClock::stepsOfMilliseconds(1500));
    }
}

void HammerHeadPackun::exeRecover() {
    if (MR::isFirstStep(this)) {
        const char* recovery = "Recovery";
        MR::startBck(this, recovery);
        MR::startBck(mLeafModel, recovery);
    }

    if (!tryShiftNumb()) {
        MR::startLevelSound(this, "SE_EM_LV_HHPACKUN_RECOVERY");
        MR::setNerveAtBckStopped(this, GET_NERVE_ANON(HammerHeadPackunNrvWait));
    }
}

void HammerHeadPackun::exeDamage() {
    if (MR::isFirstStep(this)) {
        const char* swoonStart = "SwoonStart";
        MR::startBck(this, swoonStart);
        MR::startBck(mLeafModel, swoonStart);
        MR::startBlowHitSound(this);
        mImmunityTime++;
    }

    if (MR::isStep(this, cStep_exeDamage_1)) {
        MR::shakeCameraNormal();
        MR::tryRumblePadMiddle(this, PLAYER_1);
    }

    if (MR::isBckOneTimeAndStopped(this)) {
        const char* swoon = "Swoon";
        MR::startBck(this, swoon);
        MR::startBck(mLeafModel, swoon);
    }

    if (MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        MR::startLevelSound(this, "SE_EM_LV_SWOON_S");
    }
    MR::setNerveAtStep(this, GET_NERVE_ANON(HammerHeadPackunNrvRise), NebulaFrameClock::stepsOfSeconds(3));
}

void HammerHeadPackun::exeRise() {
    if (MR::isFirstStep(this)) {
        const char* swoonEnd = "SwoonEnd";
        MR::startBck(this, swoonEnd);
        MR::startBck(mLeafModel, swoonEnd);
    }

    if (!tryShiftNumb()) {
        if (MR::isStep(this, cStep_exeRise_1)) {
            MR::shakeCameraNormalWeak();
            MR::tryRumblePadWeak(this, PLAYER_1);
        }

        if (MR::isBckStopped(this)) {
            if (isTargetInRange()) {
                const char* wait = "Wait";
                MR::startBck(this, wait);
                MR::startBck(mLeafModel, wait);
                setNerve(GET_NERVE_ANON(HammerHeadPackunNrvSearch));
            } else {
                setNerve(GET_NERVE_ANON(HammerHeadPackunNrvWait));
            }
        }
    }
}

inline void HammerHeadPackun::exeHit() {
    if (!tryShiftNumb()) {
        MR::setNerveAtBckStopped(this, GET_NERVE_ANON(HammerHeadPackunNrvCrow));
    }
}

void HammerHeadPackun::exeCrow() {
    if (MR::isFirstStep(this)) {
        const char* crow = "Crow";
        MR::startBck(this, crow);
        MR::startBck(mLeafModel, crow);
    }

    if (!tryShiftNumb()) {
        if (MR::isStep(this, cStep_exeCrow_1)) {
            MR::shakeCameraNormalWeak();
            MR::tryRumblePadWeak(this, PLAYER_1);
        }
        MR::setNerveAtBckStopped(this, GET_NERVE_ANON(HammerHeadPackunNrvWait));
    }
}

void HammerHeadPackun::exeSmashKill() {
    if (MR::isFirstStep(this)) {
        const char* down = "Down";
        MR::startBck(this, down);
        MR::startBck(mLeafModel, down);
        MR::startSound(this, "SE_EM_HHPACKUN_STOMPED");
        MR::startSound(this, "SE_EM_STOMPED_S");
    }

    if (MR::isBckStopped(this)) {
        kill();
    }
}

void HammerHeadPackun::exeBlowKill() {
    if (MR::isFirstStep(this)) {
        const char* blowDown = "BlowDown";
        MR::startBck(this, blowDown);
        MR::startBck(mLeafModel, blowDown);
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::startSound(this, "SE_EM_HHPACKUN_HIT_PUNCH");
        MR::startSound(this, "SE_EV_HHPACKUN_DEAD");
    }

    if (MR::isBckOneTimeAndStopped(this)) {
        kill();
    }
}

void HammerHeadPackun::exeNumb() {
    if (MR::isFirstStep(this)) {
        MR::startPointerHitSound();
        mEffectKeeper->forceDeleteEmitterAll();
        MR::stopBck(this);
        MR::stopBck(mLeafModel);
        MR::emitEffectWithScale(this, "PointerTouchManual", cNum_exeNumb_f1, -1);
        mNumbTime = 0;
    }

    MR::startPointerFreezeLevelSound(this);
    if (!(getNerveStep() % NebulaFrameClock::stepsOfMilliseconds(500))) {
        mJointRumbler->start();
    }

    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        mNumbTime = 0;
    }

    mNumbTime++;
    if (mNumbTime >= 20) {
        MR::deleteEffect(this, "PointerTouchManual");
        setNerve(GET_NERVE_ANON(HammerHeadPackunNrvWait));
    }
}

void HammerHeadPackun::endNumb() {
    MR::deleteEffect(this, "PointerTouchManual");
}

void HammerHeadPackun::calcAndSetBaseMtx() {
    Vec3f vec;
    Pos3f mtx;
    vec.cross(mUp, mFront);
    mtx[0][0] = vec.x;
    mtx[1][0] = vec.y;
    mtx[2][0] = vec.z;
    mtx[0][1] = mUp.x;
    mtx[1][1] = mUp.y;
    mtx[2][1] = mUp.z;
    mtx[0][2] = mFront.x;
    mtx[1][2] = mFront.y;
    mtx[2][2] = mFront.z;
    mtx[0][3] = mPosition.x;
    mtx[1][3] = mPosition.y;
    mtx[2][3] = mPosition.z;
    MR::setBaseTRMtx(this, mtx);
}

inline bool HammerHeadPackun::checkBlowAttack() const {
    return isNerve(GET_NERVE_ANON(HammerHeadPackunNrvSmashKill)) || isNerve(GET_NERVE_ANON(HammerHeadPackunNrvBlowKill));
}

void HammerHeadPackun::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!checkBlowAttack() && MR::isSensor(pSender, "blow") && MR::getSensorHost(pReceiver) &&
        MR::isEqualString(MR::getSensorHost(pReceiver)->mName, "パンチングキノコ")) {
        f32 recvRadius = pReceiver->mRadius;
        f32 sendRadius = pSender->mRadius;
        f32 senderWeight = recvRadius / (sendRadius + recvRadius);
        Vec3f hitPos = pSender->mPosition * senderWeight + (pReceiver->mPosition) * (1.0f - senderWeight);
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::emitEffectHit(this, hitPos, nullptr);
        MR::stopScene(8);
        setNerve(GET_NERVE_ANON(HammerHeadPackunNrvBlowKill));
        MR::sendMsgPush(pReceiver, pSender);
        return;
    }

    if (MR::isSensorPlayerOrRide(pReceiver)) {
        bool isSensorBody = getSensor("body") != pSender;
        if (isSensorBody) {
            if (!MR::sendMsgEnemyAttack(pReceiver, pSender)) {
                MR::sendMsgPush(pReceiver, pSender);
            }
        } else if (isAttackSensorValid() && MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
            MR::shakeCameraNormal();
            MR::tryRumblePadMiddle(this, PLAYER_1);
            if (isNerve(GET_NERVE_ANON(HammerHeadPackunNrvAttack)) && !MR::isNewNerve(this)) {
                setNerve(GET_NERVE_ANON(HammerHeadPackunNrvHit));
            } else {
                mJointRumbler->start();
            }
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    }
}

bool HammerHeadPackun::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    MR::isSensor(pReceiver, "body");
    MR::isSensor(pReceiver, "blow");
    MR::isSensor(pReceiver, "push1");
    MR::isSensor(pReceiver, "push2");
    MR::isSensor(pReceiver, "push3");
    MR::isSensor(pReceiver, "push4");

    if (!checkBlowAttack() && (MR::isMsgJetTurtleAttack(msg) || MR::isMsgFireBallAttack(msg))) {
        f32 sendRadius = pReceiver->mRadius;
        f32 recvRadius = pSender->mRadius;
        f32 receiverWeight = recvRadius / (recvRadius + sendRadius);
        Vec3f hitPos = pReceiver->mPosition * receiverWeight + (pSender->mPosition) * (1.0f - receiverWeight);
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::emitEffectHit(this, hitPos, nullptr);
        MR::stopScene(8);
        setNerve(GET_NERVE_ANON(HammerHeadPackunNrvBlowKill));
        return true;
    }

    if (MR::isMsgStarPieceReflect(msg)) {
        mJointRumbler->start();
        MR::startSound(this, "SE_EM_STARPIECE_REFRECT_PLIABLE");
        return true;
    }

    if (pReceiver != getSensor("body")) {
        return false;
    }

    if (isChance()) {
        return receiveMsgPlayerAttackChance(msg, pSender, pReceiver);
    }

    if (checkBlowAttack()) {
        return MR::isMsgPlayerTrample(msg);
    } else {
        return false;
    }
}

bool HammerHeadPackun::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgToEnemyAttackBlow(msg) && !checkBlowAttack()) {
        f32 sendRadius = pReceiver->mRadius;
        f32 recvRadius = pSender->mRadius;
        f32 receiverWeight = recvRadius / (recvRadius + sendRadius);
        Vec3f hitPos = pReceiver->mPosition * receiverWeight + (pSender->mPosition) * (1.0f - receiverWeight);
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::emitEffectHit(this, hitPos, nullptr);
        MR::stopScene(8);
        setNerve(GET_NERVE_ANON(HammerHeadPackunNrvBlowKill));
        return true;
    }
    return false;
}

void HammerHeadPackun::initPosture() {
    Pos3f mtx;
    MR::makeMtxTR(mtx, this);
    mtx.getYDir(mUp);
    mtx.getZDir(mFront);
}

void HammerHeadPackun::initSensor() {
    initHitSensor(6);
    MR::addHitSensorAtJointMapObj(this, "body", "Head", 32, cNum_initSensor_f1, Vec3f(cNum_initSensor_f2, 0.0f, 0.0f));
    MR::addHitSensorAtJointMapObj(this, "blow", "Spin1", 32, cNum_initSensor_f3, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJointMapObj(this, "push1", "Spin2", 8, cNum_initSensor_f4, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJointMapObj(this, "push2", "Spin2", 8, cNum_initSensor_f5, Vec3f(cNum_initSensor_f6, 0.0f, 0.0f));
    MR::addHitSensorAtJointMapObj(this, "push3", "Spin3", 8, cNum_initSensor_f7, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJointMapObj(this, "push4", "Spin3", 8, cNum_initSensor_f8, Vec3f(cNum_initSensor_f9, 0.0f, 0.0f));
}

void HammerHeadPackun::initShadow() {
    MR::initShadowFromCSV(this, "Shadow");
    mGravity.set(-mUp);
}

inline s32 tableEntries(const Table* table) {
    return table->getNumEntries();
}

inline bool Placement_inRange(const TableIter& rIter) {
    return rIter.mInfo && rIter.mIndex >= 0 && rIter.mIndex < tableEntries(rIter.mInfo);
}

void HammerHeadPackun::initLeaf(const TableIter& rIter) {
    mLeafModel = new PartsModel(this, "パックンの葉", "PackunLeaf", nullptr, MR::DrawBufferType_Enemy, false);
    MR::initLightCtrl(mLeafModel);
    s32 arg = -1;
    if (Placement_inRange(rIter)) {
        MR::getPlacementArg0NoInit(rIter, &arg);
    }

    if (arg > 0) {
        mLeafModel->initFixedPosition(Vec3f(cNum_initLeaf_f1, 0.0f, 0.0f), Vec3f(0.0f, -cNum_initLeaf_f2, -cNum_initLeaf_f3), "Spin1");
    } else {
        mLeafModel->initFixedPosition(Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, -cNum_initLeaf_f4, -cNum_initLeaf_f5), "Spin1");
    }

    mLeafModel->initWithoutIter();
}

bool HammerHeadPackun::isTargetInRange() const {
    Vec3f vec;
    MR::getPlayerUpVec(&vec);
    MtxPtr baseMtx1 = getBaseMtx();
    MtxPtr baseMtx2 = getBaseMtx();
    MtxPtr baseMtx3 = getBaseMtx();
    Vec3f up(baseMtx3[0][1], baseMtx2[1][1], baseMtx1[2][1]);

    if (vec.dot(up) <= 0.0f) {
        return false;
    } else if (MR::calcDistanceToPlayer(mPosition) < cNum_isTargetInRange_f1) {
        return true;
    }

    return false;
}

bool HammerHeadPackun::isInFieldOfView(const Vec3f& rPos) const {
    f32 deg = cNum_isInFieldOfView_f1;
    if (cNum_isInFieldOfView_f2 < 0.0f) {
        deg = -cNum_isInFieldOfView_f3;
    }

    return rPos.dot(mFront) >= MR::cosDegree(deg);
}

bool HammerHeadPackun::isAttackSensorValid() const {
    bool isValid = false;
    if ((!isNerve(GET_NERVE_ANON(HammerHeadPackunNrvAttack)) || !MR::isNewNerve(this)) && !isNerve(GET_NERVE_ANON(HammerHeadPackunNrvRest)) &&
        (!isNerve(GET_NERVE_ANON(HammerHeadPackunNrvRecover)) || !MR::isLessEqualStep(this, cStep_isAttackSensorValid_1)) &&
        !isNerve(GET_NERVE_ANON(HammerHeadPackunNrvDamage)) && (!isNerve(GET_NERVE_ANON(HammerHeadPackunNrvRise)) || !MR::isLessStep(this, NebulaFrameClock::stepsOfSeconds(1))) &&
        !isNerve(GET_NERVE_ANON(HammerHeadPackunNrvNumb)) && !isNerve(GET_NERVE_ANON(HammerHeadPackunNrvSmashKill)) &&
        !isNerve(GET_NERVE_ANON(HammerHeadPackunNrvBlowKill))) {
        isValid = true;
    }
    return isValid;
}

bool HammerHeadPackun::isChance() const {
    bool isChance = false;
    if ((isNerve(GET_NERVE_ANON(HammerHeadPackunNrvRest)) && !MR::isNewNerve(this)) || isNerve(GET_NERVE_ANON(HammerHeadPackunNrvDamage)) ||
        (isNerve(GET_NERVE_ANON(HammerHeadPackunNrvRecover)) && MR::isLessEqualStep(this, cStep_isChance_1)) ||
        (isNerve(GET_NERVE_ANON(HammerHeadPackunNrvRise)) && MR::isLessEqualStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) || isNerve(GET_NERVE_ANON(HammerHeadPackunNrvNumb))) {
        isChance = true;
    }
    return isChance;
}

bool HammerHeadPackun::calcPlayerDir(Vec3f* pPos) const {
    pPos->set(*MR::getPlayerPos() - mPosition);
    pPos->orthogonalize2(mUp);
    if (MR::isNearZero(*pPos)) {
        return false;
    }

    MR::normalize(pPos);
    return true;
}

void HammerHeadPackun::turnTo(const Vec3f& rFrom, f32 rate) {
    f32 absAngle = mFront.angle(rFrom);
    f32 ratio = 1.0f;
    f32 maxAngle = rate * MR::pi() / cNum_turnTo_f1;
    if (absAngle > maxAngle) {
        ratio = maxAngle / absAngle;
    }
    Quat4f quat;
    quat.setRotate(mFront, rFrom, ratio);
    quat.transform(mFront);
    verticalizeFrontVec();
}

void HammerHeadPackun::turnToAttackVec(s32 rate) {
    Quat4f mtx;
    if (getNerveStep() <= rate) {
        f32 step = (f32)getNerveStep() / rate;
        if (step > 1.0f) {
            step = 1.0f;
        }

        mtx.setRotate(mFront, mAttackDir, (step * step));
        mtx.transform(mFront);
        verticalizeFrontVec();
    }
}

void HammerHeadPackun::verticalizeFrontVec() {
    mFront.orthogonalize2(mUp);
    MR::normalize(&mFront);
}

bool HammerHeadPackun::tryShiftNumb() {
    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(GET_NERVE_ANON(HammerHeadPackunNrvNumb));
        return true;
    }
    return false;
}

bool HammerHeadPackun::receiveMsgPlayerAttackChance(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerHipDrop(msg)) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
        setNerve(GET_NERVE_ANON(HammerHeadPackunNrvSmashKill));
        MR::sendMsgAwayJump(pSender, pReceiver);
        return true;
    } else if (MR::isMsgPlayerTrample(msg)) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
        setNerve(GET_NERVE_ANON(HammerHeadPackunNrvSmashKill));
        return true;
    } else if (MR::isMsgPlayerSpinAttack(msg)) {
        if (mImmunityTime >= 3u) {
            setNerve(GET_NERVE_ANON(HammerHeadPackunNrvRise));
        } else {
            setNerve(GET_NERVE_ANON(HammerHeadPackunNrvDamage));
        }
        return true;
    }
    return false;
}
