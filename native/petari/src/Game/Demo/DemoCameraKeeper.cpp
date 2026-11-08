#include "Game/Demo/DemoCameraKeeper.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/Demo/DemoCameraFunction.hpp"
#include "Game/Demo/DemoExecutor.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    const char sSheetName[] = "Camera";
};  // namespace

DemoCameraInfo::DemoCameraInfo()
    : mPartName(), mCameraTargetName(), mCameraTargetCastID(-1), mAnimCameraName(), mAnimCameraStartFrame(-1), mAnimCameraEndFrame(-1),
      mIsContinuous(), mCameraName(), mCameraInfo(), mTargetActor() {
}

void DemoCameraKeeper::initCast(LiveActor* pActor, const TableIter& rIter) {
    for (s32 i = 0; i < mNumInfos; i++) {
        DemoCameraInfo* curInfo = &mInfos[i];
        if (!DemoFunction::isTargetDemoCast(pActor, rIter, curInfo->mCameraTargetName, curInfo->mCameraTargetCastID)) {
            continue;
        }

        curInfo->mTargetActor = pActor;
        initActorCamera(curInfo, rIter);
    }
}

void DemoCameraKeeper::start() {
    mCurrentIndex = 0;
}

void DemoCameraKeeper::update() {
    if (mCurrentIndex < 0 || mNumInfos <= mCurrentIndex) {
        return;
    }

    for (s32 i = 0; i < mNumInfos; i++) {
        DemoCameraInfo* curInfo = &mInfos[i];
        executeType(curInfo);
    }
}

void DemoCameraKeeper::end() {
    endCurrentCamera();
    mCurrentIndex = -1;
}

void DemoCameraKeeper::initActorCamera(DemoCameraInfo* pInfo, const TableIter& rIter) {
    ActorCameraInfo* cameraInfo = new ActorCameraInfo(-1, MR::getPlacedZoneId(rIter));
    pInfo->mCameraInfo = cameraInfo;
    MR::declareEventCamera(cameraInfo, pInfo->mCameraName);
    if (pInfo->mAnimCameraName == nullptr) {
        return;
    }

    MR::FormattedName animCameraName;
    DemoCameraFunction::makeAnimCameraName(animCameraName, pInfo);

    MR::initAnimCamera(pInfo->mTargetActor, pInfo->mCameraInfo, animCameraName.c_str());
}

void DemoCameraKeeper::executeType(const DemoCameraInfo* pInfo) {
    // A row naming a cast nobody plays: it never got a camera.
    if (pInfo->mCameraInfo == nullptr) {
        return;
    }

    if (MR::isDemoPartFirstStep(pInfo->mPartName)) {
        endCurrentCamera();
        mCurrentInfo = pInfo;
        executeFirst(mCurrentInfo);
        return;
    }

    if (!MR::isDemoPartLastStep(pInfo->mPartName)) {
        return;
    }

    executeLast(mCurrentInfo);
    mCurrentIndex++;
}

void DemoCameraKeeper::executeFirst(const DemoCameraInfo* pInfo) {
    MR::FormattedName animCameraName;

    if (pInfo->mTargetActor != nullptr) {
        if (pInfo->mAnimCameraName != nullptr) {
            MR::removeExtensionString(animCameraName, pInfo->mAnimCameraName);
            MR::startAnimCameraTargetSelf(pInfo->mTargetActor, pInfo->mCameraInfo, animCameraName.c_str(), pInfo->mAnimCameraStartFrame, 1.0f);
        } else {
            MR::startEventCamera(pInfo->mCameraInfo, pInfo->mCameraName, CameraTargetArg(pInfo->mTargetActor));
        }
    } else if (DemoCameraFunction::isCameraTargetMario(pInfo)) {
        MR::startEventCameraTargetPlayer(pInfo->mCameraInfo, pInfo->mCameraName);
    } else {
        MR::startEventCameraNoTarget(pInfo->mCameraInfo, pInfo->mCameraName);
    }
}

void DemoCameraKeeper::executeLast(const DemoCameraInfo* pInfo) {
    MR::FormattedName animCameraName;
    if (pInfo->mAnimCameraName == nullptr) {
        return;
    }

    MR::removeExtensionString(animCameraName, pInfo->mAnimCameraName);
    MR::endAnimCamera(pInfo->mTargetActor, pInfo->mCameraInfo, animCameraName.c_str(), pInfo->mAnimCameraEndFrame, true);
}

void DemoCameraKeeper::endCurrentCamera() {
    if (mCurrentInfo != nullptr) {
        MR::endEventCamera(mCurrentInfo->mCameraInfo, mCurrentInfo->mCameraName, mCurrentInfo->mIsContinuous != 0);
    }

    mCurrentInfo = nullptr;
}

DemoCameraKeeper::DemoCameraKeeper(DemoExecutor* pExecutor, const TableIter& rIter) : mExecutor(pExecutor), mNumInfos(), mInfos(), mCurrentIndex(-1), mCurrentInfo() {
    Table* map = nullptr;
    mNumInfos = DemoFunction::createSheetParser(mExecutor, ::sSheetName, &map);

    mInfos = new DemoCameraInfo[mNumInfos];

    s32 executorNameLen = strlen(mExecutor->mName);

    for (s32 i = 0; i < mNumInfos; i++) {
        DemoCameraInfo* curInfo = &mInfos[i];
        map->getValue< const char* >(i, "PartName", &curInfo->mPartName);
        map->getValue< const char* >(i, "CameraTargetName", &curInfo->mCameraTargetName);
        map->getValue< s32 >(i, "CameraTargetCastID", &curInfo->mCameraTargetCastID);
        map->getValue< const char* >(i, "AnimCameraName", &curInfo->mAnimCameraName);
        map->getValue< s32 >(i, "AnimCameraStartFrame", &curInfo->mAnimCameraStartFrame);
        map->getValue< s32 >(i, "AnimCameraEndFrame", &curInfo->mAnimCameraEndFrame);
        s32 continuous = -1;
        map->getValue< s32 >(i, "IsContinuous", &continuous);

        curInfo->mIsContinuous = continuous == 1;
        DemoCameraFunction::setStringNullIfEmpty(&curInfo->mCameraTargetName);
        DemoCameraFunction::setStringNullIfEmpty(&curInfo->mAnimCameraName);

        s32 animNameLen = executorNameLen + strlen(curInfo->mPartName) + 3;

        char* cameraName = new char[animNameLen];

        nebula_snprintf(cameraName, animNameLen, "%s[%s]", mExecutor->mName, curInfo->mPartName);

        curInfo->mCameraName = cameraName;
        if (curInfo->mCameraTargetName == nullptr || DemoCameraFunction::isCameraTargetMario(curInfo)) {
            initActorCamera(curInfo, rIter);
        }
    }
}
