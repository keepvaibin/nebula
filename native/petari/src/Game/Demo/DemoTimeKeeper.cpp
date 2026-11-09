#include "Game/Demo/DemoTimeKeeper.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"

DemoTimeKeeper::DemoTimeKeeper(const DemoExecutor* pExecutor)
    : mExecutor(pExecutor), mMainPartInfos(), mCurrentPart(), mNumPartInfos(), mElapsedStep(-1), mCurrentStep(-1), mPartIndex(-1), mIsPaused() {
    Table* pMap = nullptr;
    mNumPartInfos = DemoFunction::createSheetParser(mExecutor, "Time", &pMap);
    mMainPartInfos = new DemoTimePartInfo[mNumPartInfos];

    for (s32 i = 0; i < mNumPartInfos; i++) {
        DemoTimePartInfo* pInfo = &mMainPartInfos[i];
        MR::getCsvDataStrOrNULL(&pInfo->mPartName, pMap, "PartName", i);
        MR::getCsvDataS32(&pInfo->mTotalStep, pMap, "TotalStep", i);

        s32 suspendFlag = 0;
        MR::getCsvDataS32(&suspendFlag, pMap, "SuspendFlag", i);
        pInfo->mSuspendFlag = suspendFlag != 0;
    }
}

DemoTimePartInfo::DemoTimePartInfo() : mPartName(), mTotalStep(1), mSuspendFlag() {
}

void DemoTimeKeeper::start() {
    mPartIndex = 0;
    mCurrentPart = &mMainPartInfos[mPartIndex];
    NebulaMods::fire(NEBULA_MOD_EVENT_CUTSCENE_PART, mPartIndex, nullptr, mCurrentPart->mPartName);
}

void DemoTimeKeeper::update() {
    if (mIsPaused) {
        if (mElapsedStep <= 0) {
            mElapsedStep++;
        }

        if (mCurrentStep <= 0) {
            mCurrentStep++;
        }
    } else {
        mCurrentStep++;
        mElapsedStep++;

        if (mCurrentPart->mTotalStep <= mCurrentStep && !mCurrentPart->mSuspendFlag) {
            mPartIndex++;

            if (mNumPartInfos > mPartIndex) {
                mCurrentPart = &mMainPartInfos[mPartIndex];
                mCurrentStep = 0;
                NebulaMods::fire(NEBULA_MOD_EVENT_CUTSCENE_PART, mPartIndex, nullptr, mCurrentPart->mPartName);
            }
        }
    }
}

void DemoTimeKeeper::end() {
    mElapsedStep = -1;
    mCurrentStep = -1;
    mPartIndex = -1;
    mCurrentPart = nullptr;
}

bool DemoTimeKeeper::isDemoEnd() const {
    if (mIsPaused) {
        return false;
    }

    if (mCurrentPart->mSuspendFlag && mCurrentPart->mTotalStep <= mCurrentStep) {
        return true;
    }

    if (mCurrentPart->mTotalStep >= mCurrentStep && mNumPartInfos == mPartIndex) {
        return true;
    }

    return false;
}

void DemoTimeKeeper::setStartPart(const char* pPartName) {
    setCurrentPart(pPartName);
}

bool DemoTimeKeeper::isExistSuspendFlagCurrentPart() const {
    return mCurrentPart->mSuspendFlag;
}

bool DemoTimeKeeper::isPartLast() const {
    if (mIsPaused) {
        return false;
    }

    return mPartIndex == mNumPartInfos - 1;
}

inline s32 DemoTimeKeeper::getPart(const char* pPartName) const {
    for (s32 i = 0; i < mNumPartInfos; i++) {
        if (MR::isEqualString(mMainPartInfos[i].mPartName, pPartName)) {
            return i;
        }
    }

    return -1;
}

void DemoTimeKeeper::setCurrentPart(const char* pPartName) {
    mPartIndex = getPart(pPartName);
    mCurrentPart = &mMainPartInfos[mPartIndex];
    if (mPartIndex >= 0) {
        NebulaMods::fire(NEBULA_MOD_EVENT_CUTSCENE_PART, mPartIndex, nullptr, mCurrentPart->mPartName);
    }
}

bool DemoTimeKeeper::isCurrentDemoPartLastStep() const {
    return mCurrentPart->mTotalStep - 1 <= mCurrentStep;
}
