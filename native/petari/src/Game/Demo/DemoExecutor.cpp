#include "Game/Demo/DemoExecutor.hpp"
#include "Game/Demo/DemoActionKeeper.hpp"
#include "Game/Demo/DemoCameraKeeper.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/Demo/DemoPlayerKeeper.hpp"
#include "Game/Demo/DemoSoundKeeper.hpp"
#include "Game/Demo/DemoSubPartKeeper.hpp"
#include "Game/Demo/DemoTalkAnimCtrl.hpp"
#include "Game/Demo/DemoTimeKeeper.hpp"
#include "Game/Demo/DemoWipeKeeper.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

DemoExecutor::DemoExecutor(const char* pName)
    : DemoCastGroup(pName), mSheetName(), mTimeKeeper(), mSubPartKeeper(), mPlayerKeeper(), mCameraKeeper(), mActionKeeper(), mWipeKeeper(),
      mSoundKeeper(), mSheetKeeper(), mStageSwitchCtrl(), mDemoStarter(), mDemoName(), mMovementType(MR::MovementControlType_None), mUnclippedCasts(),mTalkAnimCtrl(), mTalkMessageCtrl() {
    lumacore::setClass(this, lumacore::kClassId< DemoExecutor >);
}

void DemoExecutor::init(const TableIter& rIter) {
    DemoCastGroup::init(rIter);

    mSheetName = MR::getDemoSheetName(rIter);
    mTimeKeeper = new DemoTimeKeeper(this);
    mSubPartKeeper = new DemoSubPartKeeper(this);
    mPlayerKeeper = new DemoPlayerKeeper(this);
    mCameraKeeper = new DemoCameraKeeper(this, rIter);
    mActionKeeper = new DemoActionKeeper(this);
    mWipeKeeper = new DemoWipeKeeper(this);
    mSheetKeeper.push_back(mWipeKeeper);
    mSoundKeeper = new DemoSoundKeeper(this);
    mSheetKeeper.push_back(mSoundKeeper);
    mStageSwitchCtrl = MR::createStageSwitchCtrl(this, rIter);

    if (mStageSwitchCtrl->isValidSwitchAppear()) {
        MR::listenNameObjStageSwitchOnAppear(this, mStageSwitchCtrl, MR::Functor(this, &DemoExecutor::startProperDemoSystem));
    }

    DemoFunction::registerDemoExecutor(this);
}

void DemoExecutor::registerDemoActor(LiveActor* pActor, const TableIter& rIter) {
    DemoCastGroup::registerDemoActor(pActor, rIter);
    mCameraKeeper->initCast(pActor, rIter);
    mActionKeeper->initCast(pActor, rIter);

    for (DemoSheetKeeperBase** it = mSheetKeeper.begin(); it != mSheetKeeper.end(); it++) {
        (*it)->initCast(pActor, rIter);
    }
}

void DemoExecutor::movement() {
    mTimeKeeper->update();

    if (mTimeKeeper->isDemoEnd()) {
        end();
    } else {
        if (!mTimeKeeper->mIsPaused) {
            mSubPartKeeper->update();
            mPlayerKeeper->update();
            mCameraKeeper->update();
            mActionKeeper->update();
            for (auto& item : mSheetKeeper) {
                item->update();
            }
        }

        for (auto& item : mTalkAnimCtrl) {
            item->updateDemo();
        }
    }
}

void DemoExecutor::start(NameObj* pStarter, const char* pDemoName, MR::MovementControlType movementType) {
    mDemoStarter = pStarter;
    mDemoName = pDemoName;
    mMovementType = movementType;

    mTimeKeeper->start();
    mCameraKeeper->start();
    for (auto& item : mSheetKeeper) {
        item->start();
    }

    for (auto& item : mTalkAnimCtrl) {
        item->startDemo();
    }

    mUnclippedCasts.clear();

    for (s32 i = 0; i < mGroup->getObjNum(); i++) {
        LiveActor* actor = mGroup->getActor(i);

        MR::sendMsgStartDemo(actor);
        DemoFunction::requestDemoCastMovementOn(actor);

        if (MR::isInvalidClipping(actor)) {
            continue;
        }

        MR::invalidateClipping(actor);
        mUnclippedCasts.push_back(actor);
    }
}

void DemoExecutor::startPart(NameObj* pStarter, const char* pDemoName, const char* pPartName, MR::MovementControlType movementType) {
    for (auto& item : mTalkAnimCtrl) {
        item->setupStartDemoPart(pPartName);
    }
    start(pStarter, pDemoName, movementType);
    mTimeKeeper->setStartPart(pPartName);
}

void DemoExecutor::startProperDemoSystem() {
    if (mPlayerKeeper->mNumPlayerInfos > 0) {
        MR::requestStartTimeKeepDemoMarioPuppetable(this, mName, nullptr);
    } else {
        MR::requestStartTimeKeepDemo(this, mName, nullptr);
    }
}

void DemoExecutor::startDemoSystemPart(const char* pPartName, MR::MovementControlType movementType) {
    for (auto& item : mTalkAnimCtrl) {
        item->setupStartDemoPart(pPartName);
    }

    if (movementType == MR::MovementControlType_Demo) {
        MR::startTimeKeepDemo(this, mName, nullptr);
    } else if (movementType == MR::MovementControlType_DemoMarioPuppetable) {
        MR::startTimeKeepDemoMarioPuppetable(this, mName, nullptr);
    }

    mTimeKeeper->setStartPart(pPartName);
}

bool DemoExecutor::tryStartProperDemoSystem() {
    if (mPlayerKeeper->mNumPlayerInfos > 0) {
        return MR::requestStartTimeKeepDemoMarioPuppetable(this, mName, nullptr);
    } else {
        return MR::requestStartTimeKeepDemo(this, mName, nullptr);
    }
}

bool DemoExecutor::tryStartDemoSystemPart(const char* pPartName, MR::MovementControlType movementType) {
    for (auto& item : mTalkAnimCtrl) {
        item->setupStartDemoPart(pPartName);
    }

    bool isStarted = false;

    if (movementType == MR::MovementControlType_Demo) {
        isStarted = MR::tryStartTimeKeepDemo(this, mName, nullptr);
    } else if (movementType == MR::MovementControlType_DemoMarioPuppetable) {
        isStarted = MR::tryStartTimeKeepDemoMarioPuppetable(this, mName, nullptr);
    }

    if (!isStarted) {
        return false;
    }

    mTimeKeeper->setStartPart(pPartName);
    return true;
}

bool DemoExecutor::tryStartProperDemoSystemPart(const char* pPartName) {
    if (mPlayerKeeper->mNumPlayerInfos > 0) {
        return MR::tryStartTimeKeepDemoMarioPuppetable(this, mName, pPartName);
    }

    return MR::tryStartTimeKeepDemo(this, mName, pPartName);
}

void DemoExecutor::pause() {
    mTimeKeeper->mIsPaused = true;
}

void DemoExecutor::resume() {
    mTimeKeeper->mIsPaused = false;
}

void DemoExecutor::addTalkAnimCtrl(DemoTalkAnimCtrl* pCtrl) {
    mTalkAnimCtrl.push_back(pCtrl);
}

void DemoExecutor::addTalkMessageCtrl(LiveActor* pActor, TalkMessageCtrl* pCtrl) {
    DemoTalkMessageCtrl talkMessageCtrl = {pActor, pCtrl};
    mTalkMessageCtrl.push_back(talkMessageCtrl);
}

TalkMessageCtrl* DemoExecutor::findTalkMessageCtrl(const LiveActor* pActor) const {
    for (const DemoTalkMessageCtrl* it = mTalkMessageCtrl.begin(); it != mTalkMessageCtrl.end(); it++) {
        if (it->mActor != pActor) {
            continue;
        }

        return it->mTalkMessageCtrl;
    }

    return nullptr;
}

void DemoExecutor::setTalkMessageCtrl(const LiveActor* pActor, TalkMessageCtrl* pCtrl) {
    for (DemoTalkMessageCtrl* it = mTalkMessageCtrl.begin(); it != mTalkMessageCtrl.end(); it++) {
        if (it->mActor != pActor) {
            continue;
        }

        it->mTalkMessageCtrl = pCtrl;
        break;
    }
}

void DemoExecutor::end() {
    mTimeKeeper->end();
    mSubPartKeeper->end();
    mCameraKeeper->end();
    for (auto& item : mSheetKeeper) {
        item->end();
    }

    if (mMovementType == MR::MovementControlType_Demo || mMovementType == MR::MovementControlType_DemoMarioPuppetable) {
        MR::endDemo(mDemoStarter, mDemoName);
    }

    if (mStageSwitchCtrl->isValidSwitchDead()) {
        mStageSwitchCtrl->onSwitchDead();
    }

    mDemoStarter = nullptr;
    mDemoName = nullptr;
    mMovementType = MR::MovementControlType_None;

    for (auto& item : mUnclippedCasts) {
        MR::validateClipping(item);
    }

    mUnclippedCasts.clear();
}

