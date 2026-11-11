#include "Game/Boss/TombSpider.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/TombSpiderActionBattle.hpp"
#include "Game/Boss/TombSpiderActionCocoon.hpp"
#include "Game/Boss/TombSpiderDemo.hpp"
#include "Game/Boss/TombSpiderEnvironment.hpp"
#include "Game/Boss/TombSpiderFunction.hpp"
#include "Game/Boss/TombSpiderParts.hpp"
#include "Game/Boss/TombSpiderSensorCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/PlanetMap.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeWaitPlayer_f1, "exeWaitPlayer.f1", 18000.0f);

namespace {
    const s32 sSecondBgmStartStep = NebulaFrameClock::stepsOfSeconds(3);

    // The second round waits less, attacks longer and turns faster, and its glands hurt.
    const TombSpiderBattleParam sBattleParam1st = {60, 90, 180, 300, 0.01f, 0.35f, 900, false};
    const TombSpiderBattleParam sBattleParam2nd = {30, 60, 240, 360, 0.005f, 0.4f, 600, true};
};  // namespace

namespace NrvTombSpider {
    NEW_NERVE(TombSpiderNrvWaitPlayer, TombSpider, WaitPlayer);
    NEW_NERVE(TombSpiderNrvDemoGateOpen, TombSpider, DemoGateOpen);
    NEW_NERVE(TombSpiderNrvActionCocoon, TombSpider, ActionCocoon);
    NEW_NERVE(TombSpiderNrvDemoCocoonBreak, TombSpider, DemoCocoonBreak);
    NEW_NERVE(TombSpiderNrvDemoBattle1stStart, TombSpider, DemoBattle1stStart);
    NEW_NERVE(TombSpiderNrvDemoBattle1stStartJumpToPlayer, TombSpider, DemoBattle1stStartJumpToPlayer);
    NEW_NERVE(TombSpiderNrvAction1st, TombSpider, Action1st);
    NEW_NERVE(TombSpiderNrvDemoBattle1stEnd, TombSpider, DemoBattle1stEnd);
    NEW_NERVE(TombSpiderNrvDemoBattle2ndStart, TombSpider, DemoBattle2ndStart);
    NEW_NERVE(TombSpiderNrvAction2nd, TombSpider, Action2nd);
    NEW_NERVE(TombSpiderNrvDemoDeath, TombSpider, DemoDeath);
    NEW_NERVE(TombSpiderNrvWaitDemo, TombSpider, WaitDemo);
};  // namespace NrvTombSpider

TombSpider::TombSpider(const char* pName)
    : LiveActor(pName), mSensorCtrl(nullptr), mParts(nullptr), mEnvironment(nullptr), mActionCocoon(nullptr), mAction1st(nullptr),
      mAction2nd(nullptr), mDemo(nullptr), mNoctilucaActive(false), mCameraInfo(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< TombSpider >);
}

void TombSpider::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("TombSpider", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initEffectKeeper(8, nullptr);

    mParts = new TombSpiderParts(this);
    mParts->init();

    initHitSensor(32);
    mSensorCtrl = new TombSpiderSensorCtrl(this);
    mSensorCtrl->initSensor();

    mEnvironment = new TombSpiderEnvironment(this);

    initSound(8, false);

    MR::needStageSwitchWriteA(this, rIter);
    MR::needStageSwitchWriteB(this, rIter);
    MR::needStageSwitchWriteDead(this, rIter);

    initNerve(GET_NERVE(TombSpider, TombSpiderNrvWaitPlayer));

    mActionCocoon = new TombSpiderActionCocoon(this);
    mActionCocoon->init();
    mAction1st = new TombSpiderActionBattle(this, "戦闘１回戦[トゥームスパイダー]", ::sBattleParam1st);
    mAction1st->init();
    mAction2nd = new TombSpiderActionBattle(this, "戦闘２回戦[トゥームスパイダー]", ::sBattleParam2nd);
    mAction2nd->init();

    MR::invalidateClipping(this);
    MR::initActorCamera(this, rIter, &mCameraInfo);

    mDemo = new TombSpiderDemo(this);

    MR::declarePowerStar(this);
    MR::declareStarPiece(this, 25);
    makeActorAppeared();
}

void TombSpider::exeWaitPlayer() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        MR::hideModel(this);
        MR::invalidateHitSensors(this);
    }

    if (MR::getPlayerPos()->distance(mPosition) < cNum_exeWaitPlayer_f1) {
        if (!mNoctilucaActive) {
            MR::emitEffect(this, "Noctiluca");
            mNoctilucaActive = true;
        }
    } else {
        if (mNoctilucaActive) {
            MR::deleteEffect(this, "Noctiluca");
            mNoctilucaActive = false;
        }
    }

    if (mDemo->isStartDemoGateOpen()) {
        setNerve(GET_NERVE(TombSpider, TombSpiderNrvWaitDemo));
        MR::requestStartDemoMarioPuppetable(this, "ゲートオープンデモ[トゥームスパイダー]", GET_NERVE(TombSpider, TombSpiderNrvDemoGateOpen),
                                            nullptr);
    }
}

void TombSpider::exeDemoGateOpen() {
    if (mDemo->updateGateOpen()) {
        MR::deleteEffect(this, "Noctiluca");
        MR::emitEffect(TombSpiderFunction::getPlanet(this), "Noctiluca");
        setNerve(GET_NERVE(TombSpider, TombSpiderNrvActionCocoon));
    }
}

void TombSpider::exeActionCocoon() {
    if (MR::updateActorState(this, mActionCocoon)) {
        setNerve(GET_NERVE(TombSpider, TombSpiderNrvWaitDemo));
        MR::requestStartDemoMarioPuppetable(this, "出現", GET_NERVE(TombSpider, TombSpiderNrvDemoCocoonBreak), nullptr);
    }
}

void TombSpider::exeDemoCocoonBreak() {
    if (mDemo->updateCocoonBreak()) {
        setNerve(GET_NERVE(TombSpider, TombSpiderNrvDemoBattle1stStart));
    }
}

void TombSpider::exeDemoBattle1stStart() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(0);
        MR::startBossBGM(MR::BossBgmID_TombSpiderA);
    }

    if (mDemo->updateBattle1stStart()) {
        setNerve(GET_NERVE(TombSpider, TombSpiderNrvDemoBattle1stStartJumpToPlayer));
    }
}

void TombSpider::exeDemoBattle1stStartJumpToPlayer() {
    if (mDemo->updateBattle1stStartJumpToPlayer()) {
        setNerve(GET_NERVE(TombSpider, TombSpiderNrvAction1st));
    }
}

void TombSpider::exeAction1st() {
    if (MR::updateActorState(this, mAction1st)) {
        setNerve(GET_NERVE(TombSpider, TombSpiderNrvWaitDemo));
        MR::requestStartDemoMarioPuppetable(this, "１回戦終了", GET_NERVE(TombSpider, TombSpiderNrvDemoBattle1stEnd), nullptr);
    }
}

void TombSpider::exeDemoBattle1stEnd() {
    if (mDemo->updateBattle1stEnd()) {
        setNerve(GET_NERVE(TombSpider, TombSpiderNrvDemoBattle2ndStart));
    }
}

void TombSpider::exeDemoBattle2ndStart() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(30);
    }

    if (MR::isStep(this, ::sSecondBgmStartStep)) {
        MR::startBossBGM(MR::BossBgmID_TombSpiderB);
    }

    if (mDemo->updateBattle2ndStart()) {
        setNerve(GET_NERVE(TombSpider, TombSpiderNrvAction2nd));
    }
}

void TombSpider::exeAction2nd() {
    if (MR::updateActorState(this, mAction2nd)) {
        setNerve(GET_NERVE(TombSpider, TombSpiderNrvWaitDemo));
        MR::requestStartDemoMarioPuppetable(this, "死亡", GET_NERVE(TombSpider, TombSpiderNrvDemoDeath), nullptr);
    }
}

void TombSpider::exeDemoDeath() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(60);
        MR::deleteEffect(this, "Noctiluca");
        MR::emitEffect(TombSpiderFunction::getPlanet(this), "NoctilucaFast");
    }

    mDemo->updateDeath();
}

void TombSpider::exeWaitDemo() {
}

void TombSpider::control() {
    if (!isNerve(GET_NERVE(TombSpider, TombSpiderNrvWaitPlayer))) {
        TombSpiderFunction::updateAcid(this);
    }
}

void TombSpider::updateHitSensor(HitSensor* pSensor) {
    mSensorCtrl->update(pSensor);
}

void TombSpider::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(TombSpider, TombSpiderNrvActionCocoon))) {
        mActionCocoon->attackSensor(pSender, pReceiver);
        return;
    }

    if (isNerve(GET_NERVE(TombSpider, TombSpiderNrvAction1st))) {
        mAction1st->attackSensor(pSender, pReceiver);
        return;
    }

    if (isNerve(GET_NERVE(TombSpider, TombSpiderNrvAction2nd))) {
        mAction2nd->attackSensor(pSender, pReceiver);
        return;
    }
}

bool TombSpider::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    return false;
}

bool TombSpider::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(TombSpider, TombSpiderNrvActionCocoon))) {
        return mActionCocoon->receiveOtherMsg(msg, pSender, pReceiver);
    }

    if (isNerve(GET_NERVE(TombSpider, TombSpiderNrvAction1st))) {
        return mAction1st->receiveOtherMsg(msg, pSender, pReceiver);
    }

    if (isNerve(GET_NERVE(TombSpider, TombSpiderNrvAction2nd))) {
        return mAction2nd->receiveOtherMsg(msg, pSender, pReceiver);
    }

    return false;
}
