#include "Game/Boss/KoopaSequencer.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Boss/KoopaDemoJumpToPlanet.hpp"
#include "Game/Boss/KoopaDemoPowerUp.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/Map/KoopaBattleMapPlanet.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    const s32 sKoopaFallVoiceStep = 20;
};  // namespace

KoopaSequencer::KoopaSequencer(const char* pName, s32 vs)
    : NerveExecutor(pName), mKoopa(), mPowerUp(), mBattleStairs(), mJumpToPlanet(), mLv(1), mVs(vs), mIsAngry() {
}

void KoopaSequencer::init(Koopa* pKoopa, const TableIter& /*rIter*/) {
    mKoopa = pKoopa;

    mPowerUp = new KoopaDemoPowerUp(mKoopa);
    mPowerUp->init();

    mJumpToPlanet = new KoopaDemoJumpToPlanet(mKoopa);
    mJumpToPlanet->init();
}

void KoopaSequencer::update() {
    updateNerve();
}

void KoopaSequencer::calcAndSetBaseMtx() {
    MR::calcAndSetBaseMtxFromGravityAndZAxis(mKoopa, mKoopa->mFront);
}

void KoopaSequencer::attackSensor(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
}

bool KoopaSequencer::receiveMsgPlayerAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

bool KoopaSequencer::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

bool KoopaSequencer::receiveOtherMsg(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

void KoopaSequencer::startBattleLevel2() {
    mLv = 2;
    mIsAngry = false;
}

void KoopaSequencer::startBattleLevel3() {
    mLv = 3;
    mIsAngry = false;
}

void KoopaSequencer::updateFallVoice() {
    if (MR::isStep(this, ::sKoopaFallVoiceStep)) {
        MR::startSound(mKoopa, "SE_BV_KOOPA_BATTLE_END_FALL");
    }
}

// Bowser falls through his planet, then the Power Star appears at the demo centre.
void KoopaSequencer::updateDemoDown(const char* pPlayerPosName) {
    if (MR::isFirstStep(this)) {
        MR::startAtmosphereSE("SE_AT_KOOPA_FALLDOWN_LV1");
        MR::stopStageBGM(180);

        KoopaFunction::endFaceCtrl(mKoopa, -1);

        MR::startBrk(KoopaFunction::getKoopaPlanet(mKoopa), "Death");
    }

    updateFallVoice();

    if (MR::isDemoLastStep()) {
        MR::overlayWithPreviousScreen(3);

        if (pPlayerPosName != nullptr) {
            MR::setPlayerPosAndWait(pPlayerPosName);
        }

        // Without a demo centre point the star appears at Bowser.
        Vec3f namePos(mKoopa->mPosition);
        MR::findNamePos("デモ中心", &namePos, nullptr);

        MR::startAfterBossBGM();

        MR::requestAppearPowerStar(mKoopa, namePos);
        KoopaFunction::startKoopaPlateDamageAnimPowerStarAppear(mKoopa);

        mKoopa->kill();
    }
}
