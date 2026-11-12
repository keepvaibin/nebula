#include "Game/LiveActor/ActorPadAndCameraCtrl.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Render/Clip.hpp"

namespace {
    const char* sFileName = "PadAndCameraCtrl";
    const f32 sDefaultDistanceInvalid = 3000.0f;
};  // namespace

ActorPadAndCameraCtrl::ActorPadAndCameraCtrl(const ModelManager* pModelManager, const Vec3f* pPosition)
    : mModelManager(pModelManager), mPosition(pPosition), mResourceHolder(pModelManager->getResourceHolder()), mPlayingBckName(), mInfoNum(), mInfo() {
    Table* parser = MR::tryCreateCsvParser(mResourceHolder, "%s.bcsv", ::sFileName);
    if (!parser) {
        return;
    }

    s32 count = 0;
    if (parser) {
        count = MR::getCsvDataElementNum(parser);
    }

    mInfoNum = count;
    ActorPadAndCameraCtrlInfo* infoArray = new ActorPadAndCameraCtrlInfo[mInfoNum];
    mInfo = infoArray;
    for (s32 i = 0; i < mInfoNum; i++) {
        ActorPadAndCameraCtrlInfo* info = &infoArray[i];
        MR::getCsvDataStr(&info->mBckName, parser, "BckName", i);
        MR::getCsvDataF32(&info->mStartFrame, parser, "StartFrame", i);
        MR::getCsvDataF32(&info->mEndFrame, parser, "EndFrame", i);
        MR::getCsvDataStrOrNULL(&info->mPadRumbleName, parser, "PadRumbleName", i);
        MR::getCsvDataStrOrNULL(&info->mCameraShakeName, parser, "CameraShakeName", i);
        MR::getCsvDataF32(&info->mDistanceNear, parser, "DistanceNear", i);
        MR::getCsvDataF32(&info->mDistanceFar, parser, "DistanceFar", i);
        info->mDistanceInvalid = ::sDefaultDistanceInvalid;
        parser->getValue(i, "DistanceInvalid", &info->mDistanceInvalid);
        MR::getCsvDataStrOrNULL(&info->mPadRumbleNameMiddle, parser, "PadRumbleNameMiddle", i);
        MR::getCsvDataStrOrNULL(&info->mPadRumbleNameFar, parser, "PadRumbleNameFar", i);
        MR::getCsvDataStrOrNULL(&info->mCameraShakeNameMiddle, parser, "CameraShakeNameMiddle", i);
        MR::getCsvDataStrOrNULL(&info->mCameraShakeNameFar, parser, "CameraShakeNameFar", i);
        info->mIsBckPlaying = false;
        info->mIsActive = false;
    }
}

ActorPadAndCameraCtrl* ActorPadAndCameraCtrl::tryCreate(const ModelManager* pModelManager, const Vec3f* pPosition) {
    const ResourceHolder* resourceHolder = pModelManager->getResourceHolder();
    if (!MR::isExistFileInArc(resourceHolder, "%s.bcsv", ::sFileName)) {
        return nullptr;
    }

    if (resourceHolder->mMotionResTable->mCount == 0) {
        return nullptr;
    }

    return new ActorPadAndCameraCtrl(pModelManager, pPosition);
}

void ActorPadAndCameraCtrl::update() {
    ActorPadAndCameraCtrlInfo* info;
    if (mPlayingBckName != mModelManager->getPlayingBckName()) {
        mPlayingBckName = mModelManager->getPlayingBckName();
        updateInfoBck();
    }

    FrameCtrl* frameCtrl = mModelManager->getBckCtrl();
    for (s32 i = 0; i < mInfoNum; i++) {
        info = &mInfo[i];
        if (info->mIsBckPlaying) {
            if (frameCtrl->checkPass(info->mStartFrame)) {
                info->mIsActive = true;
            }

            if (info->mIsActive) {
                updatePadAndCamera(info);
                if (info->mEndFrame < 0.0f || frameCtrl->mRate <= 0.0f || frameCtrl->checkPass(info->mEndFrame)) {
                    info->mIsActive = false;
                }
            }
        }
    }
}

void ActorPadAndCameraCtrl::updateInfoBck() {
    ActorPadAndCameraCtrlInfo* info;
    if (!mPlayingBckName) {
        return;
    }

    for (s32 i = 0; i < mInfoNum; i++) {
        info = &mInfo[i];
        if (info->mBckName) {
            if (MR::isEqualStringCase(mPlayingBckName, info->mBckName)) {
                info->mIsBckPlaying = true;
            } else {
                info->mIsBckPlaying = false;
            }
        }
    }
}

namespace {
    void tryUpdateCameraShake(const char* pName) {
        if (!pName) {
            return;
        }

        if (MR::isEqualStringCase(pName, "最強")) {
            MR::shakeCameraVeryStrong();
        } else if (MR::isEqualStringCase(pName, "強")) {
            MR::shakeCameraStrong();
        } else if (MR::isEqualStringCase(pName, "中強")) {
            MR::shakeCameraNormalStrong();
        } else if (MR::isEqualStringCase(pName, "中")) {
            MR::shakeCameraNormal();
        } else if (MR::isEqualStringCase(pName, "中弱")) {
            MR::shakeCameraNormalWeak();
        } else if (MR::isEqualStringCase(pName, "弱")) {
            MR::shakeCameraWeak();
        } else if (MR::isEqualStringCase(pName, "最弱")) {
            MR::shakeCameraVeryWeak();
        }
    }

    bool isDistanceExistAndFar(f32 distance, f32 threshold) {
        if (threshold < 0.0f) {
            return false;
        }

        return distance > threshold;
    }
};  // namespace

void ActorPadAndCameraCtrl::updatePadAndCamera(const ActorPadAndCameraCtrlInfo* pInfo) {
    f32 distance = MR::getPlayerPos()->distance(*mPosition);
    ::isDistanceExistAndFar(distance, pInfo->mDistanceInvalid);
    if (::isDistanceExistAndFar(distance, pInfo->mDistanceFar)) {
        if (pInfo->mPadRumbleNameFar) {
            MR::tryRumblePad(this, pInfo->mPadRumbleNameFar, 0);
        }

        ::tryUpdateCameraShake(pInfo->mCameraShakeNameFar);
    } else if (::isDistanceExistAndFar(distance, pInfo->mDistanceNear)) {
        if (pInfo->mPadRumbleNameMiddle) {
            MR::tryRumblePad(this, pInfo->mPadRumbleNameMiddle, 0);
        }

        ::tryUpdateCameraShake(pInfo->mCameraShakeNameMiddle);
    } else {
        if (pInfo->mPadRumbleName) {
            MR::tryRumblePad(this, pInfo->mPadRumbleName, 0);
        }

        ::tryUpdateCameraShake(pInfo->mCameraShakeName);
    }
}
