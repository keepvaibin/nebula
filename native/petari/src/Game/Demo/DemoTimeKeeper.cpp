#include "Game/Demo/DemoTimeKeeper.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"

DemoTimeKeeper::DemoTimeKeeper(const DemoExecutor* pExecutor)
    : mExecutor(pExecutor), mMainPartInfos(), mSubPartInfos(), mNumPartInfos(), mElapsedStep(-1), mCurrentStep(-1), mPartIndex(-1), mIsPaused() {
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
    mSubPartInfos = &mMainPartInfos[mPartIndex];
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

        if (mSubPartInfos->mTotalStep <= mCurrentStep && !mSubPartInfos->mSuspendFlag) {
            mPartIndex++;

            if (mNumPartInfos > mPartIndex) {
                mSubPartInfos = &mMainPartInfos[mPartIndex];
                mCurrentStep = 0;
            }
        }
    }
}

void DemoTimeKeeper::end() {
    mElapsedStep = -1;
    mCurrentStep = -1;
    mPartIndex = -1;
    mSubPartInfos = nullptr;
}

bool DemoTimeKeeper::isDemoEnd() const {
    if (mIsPaused) {
        return false;
    }

    if (mSubPartInfos->mSuspendFlag && mSubPartInfos->mTotalStep <= mCurrentStep) {
        return true;
    }

    if (mSubPartInfos->mTotalStep >= mCurrentStep && mNumPartInfos == mPartIndex) {
        return true;
    }

    return false;
}

void DemoTimeKeeper::setStartPart(const char* pPartName) {
    setCurrentPart(pPartName);
}

bool DemoTimeKeeper::isExistSuspendFlagCurrentPart() const {
    return mSubPartInfos->mSuspendFlag;
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
    mSubPartInfos = &mMainPartInfos[mPartIndex];
}

bool DemoTimeKeeper::isCurrentDemoPartLastStep() const {
    return mSubPartInfos->mTotalStep - 1 <= mCurrentStep;
}
