#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/MapObj/ChipHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/CameraInfo.hpp"
#include "Game/Screen/CounterLayoutController.hpp"
#include "Game/Screen/InformationMessage.hpp"
#include "Game/Screen/MarioMeter.hpp"
#include "Game/Screen/MarioSubMeter.hpp"
#include "Game/Screen/MissLayout.hpp"
#include "Game/NPC/NebulaAchievementLuma.hpp"
#include "Game/Screen/NebulaAchievementBoard.hpp"
#include "Game/Screen/NebulaAchievementToast.hpp"
#include "Game/Screen/NebulaModLogNotice.hpp"
#include "Game/Screen/NebulaModText.hpp"
#include "Game/Screen/NoteCounter.hpp"
#include "Game/Screen/OneUpBoard.hpp"
#include "Game/Screen/PurpleCoinCounter.hpp"
#include "Game/Screen/YesNoLayout.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/System/LumaCore.hpp"

GameSceneLayoutHolder::GameSceneLayoutHolder()
    : NameObj("ゲームシーンのレイアウト保持"), mCameraInfo(nullptr), mInformationMessage(nullptr), mMarioMeter(nullptr), mMarioSubMeter(nullptr),
      mMissLayout(nullptr), mNoteCounter(nullptr), mYesNoLayout(nullptr), mPurpleCoinCounter(nullptr), mIsStatusInvalidatePending(false),
      mCounterLayoutCtrl(nullptr), mIsActiveDefaultGameLayout(true), mAchievementToast(nullptr), mModText(nullptr), mAchievementBoard(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< GameSceneLayoutHolder >);
    MR::joinToNameObjGroup(this, "IgnorePauseNameObj");
    MR::connectToSceneLayoutMovement(this);

    mMarioMeter = new MarioMeter("マリオメーター");
    mMarioMeter->initWithoutIter();

    mCounterLayoutCtrl = new CounterLayoutController();
    mCounterLayoutCtrl->initWithoutIter();
    mCounterLayoutCtrl->setHPMeter(mMarioMeter);

    mCameraInfo = new CameraInfo("カメラ情報表示");
    mCameraInfo->initWithoutIter();

    mInformationMessage = new InformationMessage();
    mInformationMessage->initWithoutIter();

    mOneUpBoard = new OneUpBoard();
    mOneUpBoard->initWithoutIter();

    mMissLayout = new MissLayout();
    mMissLayout->initWithoutIter();

    mMarioSubMeter = new MarioSubMeter("マリオサブメーター");
    mMarioSubMeter->initWithoutIter();

    mNoteCounter = new NoteCounter("音符カウンタ");
    mNoteCounter->initWithoutIter();
    MR::hideLayout(mNoteCounter);

    mYesNoLayout = new YesNoLayout();
    mYesNoLayout->initWithoutIter();

    mAchievementToast = new NebulaAchievementToast();
    mAchievementToast->initWithoutIter();
    // The mod log's notice: only when the session has mods or something was said (contract mod-log 5).
    NebulaModLogNotice::make();
    if (NebulaMods::hasCode()) {
        mModText = new NebulaModText();
        mModText->initWithoutIter();
    }
    mAchievementBoard = nullptr;
    if (NebulaAchievementLuma::isHere()) {
        mAchievementBoard = new NebulaAchievementBoard();
        mAchievementBoard->initWithoutIter();
    }
}

void GameSceneLayoutHolder::createPurpleCoinCounter() {
    mPurpleCoinCounter = new PurpleCoinCounter("パープルコインカウンタ");
    mPurpleCoinCounter->initWithoutIter();
}

void GameSceneLayoutHolder::movement() {
    if (!mIsStatusInvalidatePending) {
        return;
    }

    invalidateStatusLayout();

    mIsStatusInvalidatePending = false;
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
