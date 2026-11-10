#include "Game/Enemy/KoopaJrShip.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/HomingKiller.hpp"
#include "Game/Enemy/Kameck.hpp"
#include "Game/Enemy/KoopaJrShipCannonMainShell.hpp"
#include "Game/Enemy/KoopaJrShipCannonShell.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/KoopaJr.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <algorithm>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initKiller_f1, "initKiller.f1", 5500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initKiller_f2, "initKiller.f2", 6500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getPropellerRotSpeed_f1, "getPropellerRotSpeed.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getPropellerRotSpeed_f2, "getPropellerRotSpeed.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getPropellerRotSpeed_f3, "getPropellerRotSpeed.f3", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateCoordSpeed_f1, "updateCoordSpeed.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateCoordSpeed_f2, "updateCoordSpeed.f2", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateCoordSpeed_f3, "updateCoordSpeed.f3", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateCoordSpeed_f4, "updateCoordSpeed.f4", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_shootShell_f1, "shootShell.f1", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_shootMainShells_f1, "shootMainShells.f1", 23.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_shootMainShells_f2, "shootMainShells.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_shootMainShells_f3, "shootMainShells.f3", 23.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_shootMainShells_f4, "shootMainShells.f4", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_shootMainShells_f5, "shootMainShells.f5", 23.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateKoopaJrPos_f1, "updateKoopaJrPos.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f1, "exeMove.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f2, "exeMove.f2", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMoveFrontAttack_f1, "exeMoveFrontAttack.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMoveFrontAttack_f2, "exeMoveFrontAttack.f2", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTurnFront_f1, "exeTurnFront.f1", 0.60f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_init_setBckRate_1, "init.setBckRate.1", 0.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeAppear_1, "exeAppear.1", 1);
NEBULA_TUNABLE_NAMED(s32, cStep_exeShootMain_1, "exeShootMain.1", 15);
NEBULA_TUNABLE_NAMED(s32, cStep_exeBreak_1, "exeBreak.1", 287);
NEBULA_TUNABLE_NAMED(s32, cStep_exeBreak_2, "exeBreak.2", 320);

namespace {
    static const char* cJointNamePropellerBack0 = "Screw00";
    static const char* cJointNamePropellerBack1 = "Screw01";
    static const char* cJointNamePropellerTop = "Propeller00";
    static const char* cJointNameCannonMain = "FirePoint3";
    static const char* cJointNameKoopaJrPos = "KoopaJrShip";
    static const char* cJointNamePodPos = "KoopaJrShipPod";

    NEBULA_TUNABLE(s32, sShellNum, 18);
    NEBULA_TUNABLE(s32, sMainShellNum, 9);
    const s32 sSpeedEaseTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, sKillerBackOffset, 100.0f);  // killers start this far behind their cannon

    NEBULA_TUNABLE_RECORD(Vec, sKoopaJrPos, {135.0f, 188.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, sKoopaJrPosFront, {0.0f, 188.0f, 135.0f});

    static const char* const cJointNameCannon[] = {"FirePoint0", "FirePoint1", "FirePoint2", "FirePoint4", "FirePoint5", "FirePoint6"};
    static const char* const cEffectNameShoot[] = {"ShootJ0", "ShootJ1", "ShootJ2", "ShootJ4", "ShootJ5", "ShootJ6"};

    inline void setRotateY(Rot3f& rMtx, f32 angle) {
        f32 s = Angle::libSin(angle);
        f32 c = Angle::libCos(angle);

        rMtx.mMtx[0][0] = c;
        rMtx.mMtx[1][1] = 1.0f;
        rMtx.mMtx[0][2] = s;
        rMtx.mMtx[2][0] = -s;
        rMtx.mMtx[2][2] = c;
        rMtx.mMtx[2][1] = 0.0f;
        rMtx.mMtx[1][2] = 0.0f;
        rMtx.mMtx[1][0] = 0.0f;
        rMtx.mMtx[0][1] = 0.0f;
    }
};  // namespace

namespace NrvKoopaJrShip {
    NEW_NERVE(HostTypeAppear, KoopaJrShip, Appear);
    NEW_NERVE(HostTypeMove, KoopaJrShip, Move);
    NEW_NERVE(HostTypeMoveFrontAttack, KoopaJrShip, MoveFrontAttack);
    NEW_NERVE(HostTypeShoot1, KoopaJrShip, Shoot);
    NEW_NERVE(HostTypeShoot2, KoopaJrShip, Shoot);
    NEW_NERVE(HostTypeShootMain, KoopaJrShip, ShootMain);
    NEW_NERVE(HostTypeStopAtEnd, KoopaJrShip, StopAtEnd);
    NEW_NERVE(HostTypePowerUp, KoopaJrShip, PowerUp);
    NEW_NERVE(HostTypeDamage, KoopaJrShip, Damage);
    NEW_NERVE(HostTypeBreakStart, KoopaJrShip, BreakStart);
    NEW_NERVE(HostTypeBreak, KoopaJrShip, Break);
    NEW_NERVE(HostTypeBreakEnd, KoopaJrShip, BreakEnd);
    NEW_NERVE(HostTypeTurnFront, KoopaJrShip, TurnFront);
};  // namespace NrvKoopaJrShip

KoopaJrShip::KoopaJrShip(const char* pName)
    : LiveActor(pName), mShellHolder(nullptr), mMainShellHolder(nullptr), mJr(nullptr), mShipBreakModel(nullptr), mPodModel(nullptr), mHitPoint(5),
      mHitPos(gZeroVec), mHitDir(0.0f, 0.0f, 1.0f), mPropAngle(0), mPropRotateSpeed(20.0f), mCoordSpeed(0.0f), mSpeedEaseStep(0), mJrLocalPos(::sKoopaJrPos), mInitPos(gZeroVec) {
    lumacore::setClass(this, lumacore::kClassId< KoopaJrShip >);
    mScrew00Mtx.identity();
    mScrew01Mtx.identity();
    mPropellerMtx.identity();
    mShipMtx.identity();
    mPodMtx.identity();
}

bool KoopaJrShip::isStateBreak(void) const {
    return isNerve(GET_NERVE(KoopaJrShip, HostTypeBreakStart)) || isNerve(GET_NERVE(KoopaJrShip, HostTypeBreak)) ||
           isNerve(GET_NERVE(KoopaJrShip, HostTypeBreakEnd));
}

void KoopaJrShip::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("KoopaJrShip", nullptr, false);
    MR::startBck(this, "KoopaJrShip");
    MR::setBckRate(this, cNum_init_setBckRate_1);
    MR::initJointTransform(this);
    MR::setJointTransformLocalMtx(this, ::cJointNamePropellerTop, mPropellerMtx);
    MR::setJointTransformLocalMtx(this, ::cJointNamePropellerBack0, mScrew00Mtx);
    MR::setJointTransformLocalMtx(this, ::cJointNamePropellerBack1, mScrew01Mtx);
    MR::connectToSceneCollisionEnemyStrongLight(this);
    initEffectKeeper(1, nullptr);
    initSound(8, false);
    initHitSensor(1);
    MR::initCollisionParts(this, "KoopaJrShip", MR::addBodyMessageSensorEnemy(this), nullptr);
    MR::invalidateClipping(this);
    initRailRider(rIter);
    MR::declarePowerStar(this);
    MR::addToAttributeGroupSearchTurtle(this);
    initShells();
    initKoopaJr(rIter);
    initKameck(rIter);
    initKiller();
    mShipBreakModel = MR::createModelObjMapObjStrongLight("クッパJrシップ壊れモデル", "KoopaJrShipBreak", mShipMtx);
    mShipBreakModel->makeActorDead();
    mPodModel = MR::createModelObjMapObjStrongLight("クッパJrシップポッドモデル", "KoopaJrShipPod", mPodMtx);
    mPodModel->makeActorDead();

    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::registerDemoActionNerve(this, GET_NERVE(KoopaJrShip, HostTypeAppear), "出現");
        MR::registerDemoActionFunctor(this, MR::Functor(this, &KoopaJrShip::setStateTurnFront), "旋廻");
        MR::tryRegisterDemoCast(mShipBreakModel, rIter);
        MR::tryRegisterDemoCast(mPodModel, rIter);
        initNerve(GET_NERVE(KoopaJrShip, HostTypeAppear));
        makeActorDead();
    } else {
        initNerve(GET_NERVE(KoopaJrShip, HostTypeMove));
        makeActorAppeared();
    }
}

void KoopaJrShip::initAfterPlacement() {
    MR::moveCoordAndTransToNearestRailPos(this);
    mInitPos.set(mPosition);
}

void KoopaJrShip::kill() {
    MR::requestAppearPowerStar(this, mPosition);
    LiveActor::kill();
}

void KoopaJrShip::control() {
    mPropAngle = MR::repeatDegree(mPropAngle + mPropRotateSpeed);

    setRotateY(mPropellerMtx, MR::toRadian(mPropAngle));
    mScrew00Mtx.setEulerZ(MR::toRadian(mPropAngle));
    mScrew01Mtx.setEulerZ(MR::toRadian(mPropAngle));
    MR::setRailCoordSpeed(this, mCoordSpeed);

    if (mHitPoint > 0) {
        if (mHitPoint <= 3) {
            MR::startLevelSound(this, "SE_BM_LV_KOOPAJR_SHIP_BURN1");
        }
        if (mHitPoint <= 1) {
            MR::startLevelSound(this, "SE_BM_LV_KOOPAJR_SHIP_BURN2");
        }
        MR::startLevelSound(this, "SE_BM_LV_KOOPAJR_SHIP_MOVE");
    }

    updateKoopaJrPos();
    updateKillerPos();
}

// Only a thrown shell hurts the ship.
bool KoopaJrShip::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    return MR::isMsgJetTurtleAttack(msg) && receiveMsgJetTurtleAttack(pSender, pReceiver);
}

void KoopaJrShip::initShells() {
    mShellHolder = new CannonShellHolder(sShellNum);
    CannonShellUtil::registerKoopaJrShipCannonShell(mShellHolder, sShellNum);
    mMainShellHolder = new CannonShellHolder(sMainShellNum);
    CannonShellUtil::registerKoopaJrShipCannonMainShell(mMainShellHolder, sMainShellNum);
}

void KoopaJrShip::initKoopaJr(const TableIter& rIter) {
    mJr = new KoopaJr("クッパJr");
    Pos3f mtx;
    mtx.set(getBaseMtx());
    mtx.setTrans(mPosition);
    mtx.mult(mJrLocalPos, mJr->mPosition);
    mJr->init(rIter);
    mJr->kill();
}

void KoopaJrShip::initKameck(const TableIter& rIter) {
    for (s32 i = 0; i < 2; i++) {
        Kameck* kameck = static_cast< Kameck* >(MR::createFireBallBeamKameck("カメック[クッパＪｒシップ]"));
        MR::initChildObj(kameck, rIter, i);
        kameck->makeActorDead();
        mKamecks.push_back(kameck);
    }
}

void KoopaJrShip::initKiller() {
    for (HomingKiller** pActor = mKillers.begin(); pActor != mKillers.end(); pActor++) {
        *pActor = new HomingKiller("ホーミングキラー");
        (*pActor)->initWithoutIter();
        (*pActor)->setChaseStartEndDistance(cNum_initKiller_f1, cNum_initKiller_f2);
    }
}

bool KoopaJrShip::receiveMsgJetTurtleAttack(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorEnemy(pReceiver) || isNerve(GET_NERVE(KoopaJrShip, HostTypeDamage)) || isNerve(GET_NERVE(KoopaJrShip, HostTypePowerUp)) ||
        isStateBreak()) {
        return false;
    }

    mHitPos.set(pSender->mPosition);
    mHitDir.set(pSender->mHost->mVelocity);
    MR::normalizeOrZero(&mHitDir);

    if (damageAndIsBroken(mHitPoint)) {
        MR::startSound(this, "SE_BM_KOOPAJR_SHIP_DAMAGE_L");
        MR::startSound(mJr, "SE_BV_KOOPAJR_DAMAGE_L");
        MR::startSystemSE("SE_SY_VS_BOSS_LAST_HIT");
        setNerve(GET_NERVE(KoopaJrShip, HostTypeBreakStart));
    } else {
        MR::startSound(this, "SE_BM_KOOPAJR_SHIP_DAMAGE");
        MR::startSound(mJr, "SE_BV_KOOPAJR_DAMAGE_S");
        MR::startSystemSE("SE_SY_VS_BOSS_DAMAGE_1");
        setNerve(GET_NERVE(KoopaJrShip, HostTypeDamage));
    }

    return true;
}

s32 KoopaJrShip::getNumShootShells() const {
    if (isNerve(GET_NERVE(KoopaJrShip, HostTypeShoot1))) {
        return 1;
    }

    return isNerve(GET_NERVE(KoopaJrShip, HostTypeShoot2)) != 0 ? 5 : 0;
}

f32 KoopaJrShip::getPropellerRotSpeed() const {
    if (isNerve(GET_NERVE(KoopaJrShip, HostTypeBreakStart))) {
        return (cNum_getPropellerRotSpeed_f1 * (1.0f - MR::calcNerveRate(this, NebulaFrameClock::stepsOfSeconds(1))));
    }

    if (isNerve(GET_NERVE(KoopaJrShip, HostTypeBreak))) {
        return 0.0f;
    }

    if (isNerve(GET_NERVE(KoopaJrShip, HostTypePowerUp)) || (2 >= mHitPoint && !isNerve(GET_NERVE(KoopaJrShip, HostTypeDamage)))) {
        return cNum_getPropellerRotSpeed_f2;
    }

    return cNum_getPropellerRotSpeed_f3;
}

// Slows into each rail point (top speed 4, 2 once at 2 hit points) and eases back up over sSpeedEaseTime after a stop.
void KoopaJrShip::updateCoordSpeed() {
    f32 rate = 0.0f;
    MR::calcRailRateToNextPoint(&rate, this);
    const f32 topSpeed = mHitPoint <= 2 ? cNum_updateCoordSpeed_f1 : cNum_updateCoordSpeed_f2;

    if (cNum_updateCoordSpeed_f3 < rate) {
        mCoordSpeed = MR::getEaseInValue(rate, topSpeed, 1.0f, 1.0f);
        return;
    }

    if (mSpeedEaseStep >= 0) {
        mCoordSpeed = MR::getEaseInValue(mSpeedEaseStep, topSpeed, 0.0f, cNum_updateCoordSpeed_f4);
        mSpeedEaseStep = mSpeedEaseStep - 1;
        MR::startLevelSound(this, "SE_BM_LV_KOOPAJR_SHIP_ACCEL");
    }

    mPropRotateSpeed = getPropellerRotSpeed();
}

// Cannon idx's position and its unit X direction.
void KoopaJrShip::calcLauncherInfo(Vec3f* pPos, Vec3f* pDir, s32 idx) const {
    Pos3f mtx;
    mtx.set(MR::getJointMtx(this, ::cJointNameCannon[idx]));
    mtx.getTrans(*pPos);
    mtx.getXDir(*pDir);
    MR::normalize(pDir);
}

void KoopaJrShip::calcLauncherInfoKiller(Vec3f* pPos, Vec3f* pDir, s32 idx) const {
    calcLauncherInfo(pPos, pDir, idx);
    pPos->add(*pDir * -sKillerBackOffset);
}

void KoopaJrShip::shootShell(s32 idx) {
    CannonShellBase* shell = mShellHolder->getValidShell();

    if (shell != nullptr) {
        Vec3f pos, dir;
        calcLauncherInfo(&pos, &dir, idx);
        shell->launch(pos, dir * cNum_shootShell_f1);
    }

    MR::emitEffect(this, ::cEffectNameShoot[idx]);
}

bool KoopaJrShip::tryShootAllKillers() {
    if (isExistActiveKiller()) {
        return false;
    }

    if (isExistActiveKameck()) {
        return false;
    }

    for (HomingKiller** pActor = mKillers.begin(); pActor != mKillers.end(); pActor++) {
        Vec3f pos, dir;
        s32 idx = mKillers.indexOf(pActor);
        calcLauncherInfoKiller(&pos, &dir, idx);
        (*pActor)->appear(pos, dir);
        MR::emitEffect(this, ::cEffectNameShoot[idx]);
    }

    return true;
}

void KoopaJrShip::shootKiller(s32 idx) {
    HomingKiller* killer = mKillers[idx];
    if (MR::isDead(killer)) {
        Vec3f pos, dir;
        calcLauncherInfoKiller(&pos, &dir, idx);
        killer->appear(pos, dir);
        MR::emitEffect(this, ::cEffectNameShoot[idx]);
    }
}

void KoopaJrShip::shootKillersAfterDamage() {
    if (mHitPoint <= 2) {
        tryShootAllKillers();
    } else {
        shootKiller(0);
        shootKiller(1);
        shootKiller(2);
    }
}

bool KoopaJrShip::isExistActiveKiller() const {
    return std::find_if(mKillers.begin(), mKillers.end(), [&](const auto& item) { return !MR::isDead(item); }) != mKillers.end();
}

bool KoopaJrShip::isExistActiveKameck() const {
    return std::find_if(mKamecks.begin(), mKamecks.end(), [&](const auto& item) { return !MR::isDead(item); }) != mKamecks.end();
}

void KoopaJrShip::shootMainShells() {
    Pos3f m;
    m.set(MR::getJointMtx(this, ::cJointNameCannonMain));

    Vec3f pos;
    m.getTrans(pos);

    // Straight ahead
    {
        Vec3f forward;
        m.getZDir(forward);
        MR::normalize(&forward);
        CannonShellBase* p = mMainShellHolder->getValidShell();
        p->launch(pos, forward * cNum_shootMainShells_f1);
    }

    MR::rotateMtxLocalYDegree(m, cNum_shootMainShells_f2);

    // Side
    {
        Vec3f forward;
        m.getZDir(forward);
        MR::normalize(&forward);
        CannonShellBase* p = mMainShellHolder->getValidShell();
        p->launch(pos, forward * cNum_shootMainShells_f3);
    }

    MR::rotateMtxLocalYDegree(m, -cNum_shootMainShells_f4);

    // Other side
    {
        Vec3f forward;
        m.getZDir(forward);
        MR::normalize(&forward);
        CannonShellBase* p = mMainShellHolder->getValidShell();
        p->launch(pos, forward * cNum_shootMainShells_f5);
    }
}

void KoopaJrShip::emitDamageSmokeEffect() {
    if (mHitPoint == 3) {
        MR::emitEffect(this, "DamageSmoke1");
    } else if (mHitPoint == 1) {
        MR::emitEffect(this, "DamageSmoke2");
    }
}

void KoopaJrShip::emitDamageHitEffect() {
    Pos3f hitMtx;
    hitMtx.identity();
    Vec3f up;
    MR::calcUpVec(&up, this);
    MR::makeMtxSideUpPos(&hitMtx, -mHitDir, up, mHitPos);
    MR::emitEffectHit(this, hitMtx, "DamageFire");
    MR::startSound(this, "SE_BM_KOOPAJR_SHIP_IGNIT");
}

// Koopa Jr. rides the escape pod once the ship breaks; before that he stands on the ship turned toward the player.
void KoopaJrShip::updateKoopaJrPos() {
    Pos3f jointMtx;
    if (isNerve(GET_NERVE(KoopaJrShip, HostTypeBreak))) {
        jointMtx.set(MR::getJointMtx(mPodModel, ::cJointNamePodPos));
    } else {
        jointMtx.set(MR::getJointMtx(this, ::cJointNameKoopaJrPos));
        MR::faceToPoint(jointMtx, *MR::getPlayerPos(), cNum_updateKoopaJrPos_f1);
    }

    jointMtx.mult(mJrLocalPos, mJr->mPosition);
}

void KoopaJrShip::updateKillerPos() {
    for (HomingKiller** pActor = mKillers.begin(); pActor != mKillers.end(); pActor++) {
        if (MR::isDead(*pActor)) {
            continue;
        }

        Vec3f pos;
        Vec3f dir;
        calcLauncherInfoKiller(&pos, &dir, mKillers.indexOf(pActor));
        (*pActor)->setBasePosAndFront(pos, dir);
    }
}

void KoopaJrShip::killAllSubModels() {
    mJr->kill();
    mPodModel->makeActorDead();
    mShipBreakModel->makeActorDead();
}

void KoopaJrShip::setStateTurnFront() {
    mPosition.set(mInitPos);
    MR::moveCoordAndTransToNearestRailPos(this);
    mJr->setStateShipBattlePowerUp();
    mJrLocalPos.x = ::sKoopaJrPosFront.x;
    mJrLocalPos.y = ::sKoopaJrPosFront.y;
    mJrLocalPos.z = ::sKoopaJrPosFront.z;
    setNerve(GET_NERVE(KoopaJrShip, HostTypeTurnFront));
}

void KoopaJrShip::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_BM_KOOPAJR_SHIP_ENTER");
        MR::startBck(this, "Arrival");
        MR::hideModel(this);
    }

    if (MR::isStep(this, cStep_exeAppear_1)) {
        MR::showModel(this);
        mJr->appear();
        mJr->setStateShipBattleAppear();
    }

    if (MR::isDemoPartLastStep("クッパJr会話")) {
        mJr->endShipBattleTalk();
        mSpeedEaseStep = sSpeedEaseTime;
        MR::startStageBGM("MBGM_BOSS_06_A", false);
        setNerve(GET_NERVE(KoopaJrShip, HostTypeMove));
    }
}

void KoopaJrShip::exeMove() {
    if (MR::isFirstStep(this)) {
        f32 speed = mHitPoint <= 2 ? cNum_exeMove_f1 : cNum_exeMove_f2;
        MR::setRailCoordSpeed(this, speed);
    }

    updateCoordSpeed();
    MR::moveCoordAndFollowTrans(this);

    // Every 120 steps with no killer out: one shell at full health, five at 2 to 4 hit points.
    if (MR::isGreaterStep(this, 0) && getNerveStep() % NebulaFrameClock::stepsOfSeconds(2) == 0 && !isExistActiveKiller()) {
        if (mHitPoint == 5) {
            setNerve(GET_NERVE(KoopaJrShip, HostTypeShoot1));
        } else if (mHitPoint >= 2 && mHitPoint <= 4) {
            setNerve(GET_NERVE(KoopaJrShip, HostTypeShoot2));
        }
    }

    if (MR::isRailReachedEdge(this)) {
        setNerve(GET_NERVE(KoopaJrShip, HostTypeStopAtEnd));
    }
}

void KoopaJrShip::exeMoveFrontAttack() {
    if (MR::isFirstStep(this)) {
        f32 speed = mHitPoint <= 2 ? cNum_exeMoveFrontAttack_f1 : cNum_exeMoveFrontAttack_f2;
        MR::setRailCoordSpeed(this, speed);
        MR::emitEffect(this, "HeadShootSign");
    }

    MR::startLevelSound(this, "SE_BM_LV_KOOPAJR_SHIP_MAIN_PREP");
    updateCoordSpeed();
    MR::moveCoordAndFollowTrans(this);

    if (MR::isRailReachedGoal(this)) {
        if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
            MR::reverseRailDirection(this);
            setNerve(GET_NERVE(KoopaJrShip, HostTypeShootMain));
        }
    } else if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(KoopaJrShip, HostTypeShootMain));
    }
}

void KoopaJrShip::exeShoot() {
    MR::moveCoordAndFollowTrans(this);
    updateCoordSpeed();

    for (s32 i = 0; i < getNumShootShells(); i++) {
        if (MR::isStep(this, 45 * i)) {
            shootShell(1);

            if (i == getNumShootShells() - 1) {
                setNerve(GET_NERVE(KoopaJrShip, HostTypeMove));
                return;
            }
        }
    }

    if (MR::isRailReachedEdge(this)) {
        setNerve(GET_NERVE(KoopaJrShip, HostTypeStopAtEnd));
    }
}

void KoopaJrShip::exeShootMain() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "HeadShoot");
        MR::startSound(this, "SE_BM_KOOPAJR_SHIP_SHOOT_MAIN");
        MR::shakeCameraNormal();

        if (mHitPoint <= 1) {
            tryShootAllKillers();
        }
    }

    if (MR::isStep(this, cStep_exeShootMain_1)) {
        shootMainShells();
    }

    MR::moveCoordAndFollowTrans(this);
    updateCoordSpeed();

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(2500))) {
        setNerve(GET_NERVE(KoopaJrShip, HostTypeMoveFrontAttack));
    }
}

void KoopaJrShip::exeStopAtEnd() {
    s32 step = (mHitPoint <= 2) ? 90 : 150;

    if (MR::isStep(this, step)) {
        MR::reverseRailDirection(this);
        mSpeedEaseStep = sSpeedEaseTime;

        if (mHitPoint <= 2) {
            setNerve(GET_NERVE(KoopaJrShip, HostTypeMoveFrontAttack));
        } else {
            setNerve(GET_NERVE(KoopaJrShip, HostTypeMove));
        }
    }
}

void KoopaJrShip::exePowerUp() {
    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        mSpeedEaseStep = sSpeedEaseTime;
        setNerve(GET_NERVE(KoopaJrShip, HostTypeMove));
    }
}

void KoopaJrShip::exeDamage() {
    if (MR::isFirstStep(this)) {
        mCoordSpeed = 0.0f;
        MR::startBck(this, "Damage");
        mJr->setStateShipBattleShipDamage();
        emitDamageSmokeEffect();
        emitDamageHitEffect();
        MR::shakeCameraNormal();
        MR::tryRumbleDefaultHit(this, PLAYER_1);
        MR::stopScene(12);
    }

    // At 2 hit points the turn demo takes over (setStateTurnFront); a Kameck joins at 3 and at 1.
    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        if (mHitPoint == 2) {
            MR::requestStartDemoRegistered(this, nullptr, nullptr, "旋廻");
            return;
        }

        shootKillersAfterDamage();
        if (mHitPoint == 3) {
            mKamecks[0]->appear();
        }

        if (mHitPoint == 1) {
            mKamecks[1]->appear();
        }

        mSpeedEaseStep = sSpeedEaseTime;

        if (mHitPoint <= 2) {
            setNerve(GET_NERVE(KoopaJrShip, HostTypeMoveFrontAttack));
        } else {
            setNerve(GET_NERVE(KoopaJrShip, HostTypeMove));
        }
    }
}

void KoopaJrShip::exeBreakStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Damage");
        mJr->setStateShipBattleShipDamage();
        emitDamageHitEffect();
        MR::shakeCameraNormal();
        MR::tryRumbleDefaultHit(this, PLAYER_1);
        MR::stopScene(12);
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        mShellHolder->killActiveShells();
        mMainShellHolder->killActiveShells();

        Kameck* const* pActor;

        for (pActor = mKamecks.begin(); pActor != mKamecks.end(); pActor++) {
            if (!MR::isDead(*pActor)) {
                (*pActor)->makeActorDeadForce();
            }
        }

        for (HomingKiller** pActor = mKillers.begin(); pActor != mKillers.end(); pActor++) {
            if (!MR::isDead(*pActor)) {
                (*pActor)->kill();
            }
        }

        MR::requestStartDemoRegistered(this, nullptr, nullptr, "破壊");
        mShipMtx.set(getBaseMtx());
        mShipBreakModel->appear();
        MR::startBck(mShipBreakModel, "Break");
        mPodMtx.set(getBaseMtx());
        mPodModel->appear();
        MR::startBck(mPodModel, "Escape");
        setNerve(GET_NERVE(KoopaJrShip, HostTypeBreak));
    }
}

void KoopaJrShip::exeBreak() {
    if (MR::isFirstStep(this)) {
        MR::deleteEffect(this, "EyeLight");
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraNormal();
        MR::startSound(this, "SE_BM_KOOPAJR_SHIP_BREAK_S");
        mJr->setStateShipBattleEscape();
        MR::hideModel(this);
        MR::deleteEffect(this, "DamageSmoke1");
        MR::deleteEffect(this, "DamageSmoke2");
    }

    if (!MR::isHiddenModel(mPodModel) && MR::isBckStopped(mPodModel)) {
        MR::hideModel(mPodModel);
        MR::hideModel(mJr);
    }

    if (MR::isStep(this, cStep_exeBreak_1)) {
        MR::startSound(this, "SE_BM_KOOPAJR_SHIP_BREAK_S");
    }

    if (MR::isLessStep(this, NebulaFrameClock::stepsOfSeconds(5))) {
        MR::startLevelSound(this, "SE_BM_LV_KOOPAJR_SHIP_BURN1");
        MR::startLevelSound(this, "SE_BM_LV_KOOPAJR_SHIP_BURN2");
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(5))) {
        MR::shakeCameraNormal();
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::startSound(this, "SE_BM_KOOPAJR_SHIP_BREAK_L");
    }

    if (!MR::isHiddenModel(mShipBreakModel) && (MR::isBckStopped(mShipBreakModel) || MR::isStep(this, cStep_exeBreak_2))) {
        mShipBreakModel->makeActorDead();
    }

    if (MR::isDemoLastStep()) {
        killAllSubModels();
        setNerve(GET_NERVE(KoopaJrShip, HostTypeBreakEnd));
    }
}

void KoopaJrShip::exeTurnFront() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(30);
        mShellHolder->killActiveShells();
        mMainShellHolder->killActiveShells();

        if (!MR::isDead(mKamecks[0])) {
            mKamecksAtTurn.push_back(mKamecks[0]);
        }

        mKamecks[0]->makeActorDeadForce();
        for (auto& item : mKillers) {
            item->makeActorDead();
        }
    }

    MR::startLevelSound(this, "SE_BM_LV_KOOPAJR_SHIP_3RD_DEMO");

    if (MR::isLessEqualStep(this, NebulaFrameClock::stepsOfMilliseconds(2500))) {
        mRotation.y += cNum_exeTurnFront_f1;
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(2500))) {
        MR::emitEffect(this, "EyeLight");
        MR::startSound(this, "SE_BM_KOOPAJR_SHIP_EYE_BLINK");
    }

    if (MR::isDemoPartLastStep("旋廻")) {
        mSpeedEaseStep = sSpeedEaseTime;
        MR::startStageBGM("MBGM_BOSS_06_B", false);

        if (mKamecksAtTurn.size() != 0) {
            mKamecks[0]->appear();
        }

        setNerve(GET_NERVE(KoopaJrShip, HostTypeMoveFrontAttack));
    }
}

void KoopaJrShip::exeBreakEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAfterBossBGM();
        kill();
    }
}
