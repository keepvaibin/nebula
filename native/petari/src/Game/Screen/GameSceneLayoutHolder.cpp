#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/MapObj/ChipHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/CameraInfo.hpp"
#include "Game/Screen/CounterLayoutController.hpp"
#include "Game/Screen/InformationMessage.hpp"
#include "Game/Screen/MarioMeter.hpp"
#include "Game/Screen/MarioSubMeter.hpp"
#include "Game/Screen/MissLayout.hpp"
#include "Game/Screen/NoteCounter.hpp"
#include "Game/Screen/OneUpBoard.hpp"
#include "Game/Screen/PurpleCoinCounter.hpp"
#include "Game/Screen/YesNoLayout.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

GameSceneLayoutHolder::GameSceneLayoutHolder()
    : NameObj("\203\121\201\133\203\200\203\126\201\133\203\223\202\314\203\214\203\103\203\101\203\105\203\147\225\333\216\235" /* ゲームシーンのレイアウト保持 */), mCameraInfo(nullptr), mInformationMessage(nullptr), mMarioMeter(nullptr), mMarioSubMeter(nullptr),
      mMissLayout(nullptr), mNoteCounter(nullptr), mYesNoLayout(nullptr), mPurpleCoinCounter(nullptr), _30(false), _31(false),
      mCounterLayoutCtrl(nullptr), mIsActiveDefaultGameLayout(true) {
    MR::joinToNameObjGroup(this, "IgnorePauseNameObj");
    MR::connectToSceneLayoutMovement(this);

    mMarioMeter = new MarioMeter("\203\175\203\212\203\111\203\201\201\133\203\136\201\133" /* マリオメーター */);
    mMarioMeter->initWithoutIter();

    mCounterLayoutCtrl = new CounterLayoutController();
    mCounterLayoutCtrl->initWithoutIter();
    mCounterLayoutCtrl->setHPMeter(mMarioMeter);

    mCameraInfo = new CameraInfo("\203\112\203\201\203\211\217\356\225\361\225\134\216\246" /* カメラ情報表示 */);
    mCameraInfo->initWithoutIter();

    mInformationMessage = new InformationMessage();
    mInformationMessage->initWithoutIter();

    mOneUpBoard = new OneUpBoard();
    mOneUpBoard->initWithoutIter();

    mMissLayout = new MissLayout();
    mMissLayout->initWithoutIter();

    mMarioSubMeter = new MarioSubMeter("\203\175\203\212\203\111\203\124\203\165\203\201\201\133\203\136\201\133" /* マリオサブメーター */);
    mMarioSubMeter->initWithoutIter();

    mNoteCounter = new NoteCounter("\211\271\225\204\203\112\203\105\203\223\203\136" /* 音符カウンタ */);
    mNoteCounter->initWithoutIter();
    MR::hideLayout(mNoteCounter);

    mYesNoLayout = new YesNoLayout();
    mYesNoLayout->initWithoutIter();
}

void GameSceneLayoutHolder::createPurpleCoinCounter() {
    mPurpleCoinCounter = new PurpleCoinCounter("\203\160\201\133\203\166\203\213\203\122\203\103\203\223\203\112\203\105\203\223\203\136" /* パープルコインカウンタ */);
    mPurpleCoinCounter->initWithoutIter();
}

void GameSceneLayoutHolder::movement() {
    if (!_30) {
        return;
    }

    invalidateStatusLayout();

    _30 = false;
}

void GameSceneLayoutHolder::initLifeCount(u32 lifeCount) {
    mMarioMeter->initLifeCount(lifeCount);
}

void GameSceneLayoutHolder::setLifeCount(u32 lifeCount) {
    mMarioMeter->setLifeCount(lifeCount);
}

void GameSceneLayoutHolder::setOxygenRatio(f32 oxygenRatio) {
    mMarioSubMeter->setWaterLifeRatio(oxygenRatio);
}

void GameSceneLayoutHolder::setBeePowerRatio(f32 beePowerRatio) {
    mMarioSubMeter->setBeeLifeRatio(beePowerRatio);
}

void GameSceneLayoutHolder::changeLifeMeterModeGround() {
    mMarioSubMeter->frameOutSubMeter();
}

void GameSceneLayoutHolder::changeLifeMeterModeWater() {
    mMarioSubMeter->activeSubMeterWater();
}

void GameSceneLayoutHolder::changeLifeMeterModeBee() {
    mMarioSubMeter->activeSubMeterBee();
}

void GameSceneLayoutHolder::requestOneUp(s32 oneUpNum) {
    mOneUpBoard->popup(oneUpNum);
}

void GameSceneLayoutHolder::requestPowerUpHPMeter() {
    mMarioMeter->powerUp();
}

void GameSceneLayoutHolder::requestForceAppearHPMeter() {
    mMarioMeter->requestForceAppearHPMeter();
}

void GameSceneLayoutHolder::startMiss() {
    if (mMissLayout->isActive()) {
        return;
    }

    mMissLayout->start();
}

bool GameSceneLayoutHolder::isMissAnimEnd() const {
    return !mMissLayout->isActive();
}

void GameSceneLayoutHolder::activateDefaultGameLayout() {
    mIsActiveDefaultGameLayout = true;

    mCameraInfo->activate();
    mMarioMeter->activate();
    mMarioSubMeter->activate();
    mCounterLayoutCtrl->appear();

    if (mPurpleCoinCounter != nullptr) {
        mPurpleCoinCounter->appear();
    }

    MR::activateChipLayout();
}

void GameSceneLayoutHolder::deactivateDefaultGameLayout() {
    mIsActiveDefaultGameLayout = false;

    mCameraInfo->deactivate();
    mMarioMeter->deactivate();
    mMarioSubMeter->deactivate();

    if (!MR::isDead(mCounterLayoutCtrl)) {
        mCounterLayoutCtrl->kill();
    }

    if (mPurpleCoinCounter != nullptr && !MR::isDead(mPurpleCoinCounter)) {
        mPurpleCoinCounter->kill();
    }

    MR::deactivateChipLayout();
}

PurpleCoinCounter* GameSceneLayoutHolder::getPurpleCoinCounter() const {
    return mPurpleCoinCounter;
}

void GameSceneLayoutHolder::invalidateStatusLayout() {
    mCameraInfo->kill();
    mMarioMeter->deactivate();
    mMarioSubMeter->deactivate();
    mCounterLayoutCtrl->kill();

    if (mPurpleCoinCounter != nullptr) {
        mPurpleCoinCounter->kill();
    }

    MR::deactivateChipLayout();
}

namespace MR {
    GameSceneLayoutHolder* getGameSceneLayoutHolder() {
        return MR::getSceneObj< GameSceneLayoutHolder >(SceneObj_GameSceneLayoutHolder);
    }
};  // namespace MR
