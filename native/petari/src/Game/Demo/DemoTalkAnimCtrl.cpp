#include "Game/Demo/DemoTalkAnimCtrl.hpp"
#include "Game/Demo/DemoExecutor.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/Demo/DemoSubPartKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/System/NerveExecutor.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/System/ResourceInfo.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/StringUtil.hpp"

DemoTalkAnimCtrl::DemoTalkAnimCtrl(LiveActor* pActor, const char* pExecutorName, const char* pStartPartName)
    : NerveExecutor(pExecutorName), mActor(pActor), mExecutorName(pExecutorName), mStartPartName(pStartPartName), mPartName(nullptr), mAnimName(""), mCameraName(""), mCameraInfo(nullptr), mAnimFrame(0),
      mIsActive(true), mIsScene(false), mIsTalkPart(false), mInterpole(0), mCameraStartStep(0), mCameraEndStep(0), mHavePlayerBck(false), mHaveCamera(false), mHaveBck(false), mHaveBtk(false), mHaveBpk(false),
      mHaveBtp(false), mHaveBrk(false), mHaveBva(false) {
}

void DemoTalkAnimCtrl::initForScene(const char* pAnimName, const char* pCameraName, const TableIter& rIter) {
    MR::FormattedName buf("%s.canm", pCameraName);
    initForActor(pAnimName);
    mIsScene = true;
    mIsActive = mStartPartName == nullptr;
    mHaveCamera = MR::getResourceHolder(mActor)->mFileInfoTable->isExistRes(buf.c_str());
    mHavePlayerBck = MR::isExistBck(MR::getPlayerDemoActor(), pAnimName);
    if (mHaveCamera) {
        mCameraName = pCameraName;
        mCameraInfo = MR::createActorCameraInfo(rIter);
        MR::initAnimCamera(mActor, mCameraInfo, mCameraName);
    }
}

void DemoTalkAnimCtrl::updateCamera() {
    MR::FormattedName buf("%s%s", mActor->mName, mCameraName);
    if (DemoFunction::isPauseTimeKeepDemo(mExecutorName)) {
        MR::pauseOnAnimCamera(mCameraInfo, buf.c_str());
    } else {
        MR::pauseOffAnimCamera(mCameraInfo, buf.c_str());
    }
}

namespace {
    s32 calcDemoPartStep(const char* pExecutorName, const char* pMainPartName) {
        DemoExecutor* exec = DemoFunction::findDemoExecutor(pExecutorName);

        // A demo no sheet has has no parts.
        if (exec == nullptr) {
            return 0;
        }

        DemoSubPartInfo* subpart;
        DemoSubPartKeeper* subpartkeeper = exec->mSubPartKeeper;
        for (int i = 0; i < subpartkeeper->mNumSubPartInfos; i++) {
            subpart = &subpartkeeper->mSubPartInfos[i];
            if (MR::isEqualString(pMainPartName, subpart->mMainPartName) && MR::isEqualSubString(subpart->mSubPartName, "会話アニメループ")) {
                return subpart->mMainPartStep;
            }
        }
        return 0;
    }
};  // namespace

void DemoTalkAnimCtrl::createBckCtrlData(BckCtrlData* pBck, s32 totalSteps) const {
    s16 startFrame = MR::max(mAnimFrame - 1, 0);
    pBck->mLoopMode = 2;
    pBck->mStartFrame = startFrame;
    pBck->mRepeatFrame = startFrame + 1;
    pBck->mInterpole = mInterpole;
    pBck->mEndFrame = startFrame + totalSteps + 1;

    if (mIsTalkPart) {
        s32 step = ::calcDemoPartStep(mExecutorName, mPartName);
        if (step > 0) {
            pBck->mRepeatFrame = startFrame + step;
        }
    }
}

void DemoTalkAnimCtrl::initForActor(const char* pAnimName) {
    mIsScene = false;
    mIsActive = true;
    mHaveCamera = false;
    mHavePlayerBck = false;
    mAnimName = pAnimName;
    mPartName = nullptr;
    mAnimFrame = 0;
    mIsTalkPart = false;
    mCameraInfo = nullptr;
    mCameraName = "";
    mHaveBck = MR::isExistBck(mActor, pAnimName);
    mHaveBtk = MR::isExistBtk(mActor, mAnimName);
    mHaveBpk = MR::isExistBpk(mActor, mAnimName);
    mHaveBtp = MR::isExistBtp(mActor, mAnimName);
    mHaveBrk = MR::isExistBrk(mActor, mAnimName);
    mHaveBva = MR::isExistBva(mActor, mAnimName);
}

void DemoTalkAnimCtrl::startDemo() {
    mPartName = nullptr;
    mAnimFrame = 0;
    mIsTalkPart = false;
    if (mHaveCamera && mIsActive) {
        startCamera();
    }
}

bool DemoTalkAnimCtrl::updateDemo() {
    if (!mIsActive) {
        return false;
    }

    const char* name = MR::getCurrentDemoPartNameMain(mExecutorName);
    if (mPartName == nullptr) {
        startAnim();
        mPartName = "";
    }

    bool ok = MR::isDemoPartFirstStep(name);
    if (MR::isEqualStringCase(mPartName, "")) {
        ok = MR::isDemoPartStep(name, 1);
    }

    if (ok) {
        s32 totalsteps = MR::getDemoPartTotalStep(name);
        BckCtrlData bck;
        bck.mLoopMode = 0xFF;
        mIsTalkPart = MR::isDemoPartTalk(name);
        mPartName = name;
        createBckCtrlData(&bck, totalsteps);
        updateAnim(bck);
        if (mHavePlayerBck) {
            MR::startBckPlayer(mAnimName, bck);
        }
        mAnimFrame += totalsteps;
    }

    if (mIsScene && mIsTalkPart) {
        updatePause();
    }
    if (mHaveCamera) {
        updateCamera();
    }

    if (DemoFunction::isDemoSuspendOrLastPartLastStep()) {
        if (mHaveCamera) {
            endCamera();
        }
        return false;
    }

    return true;
}

void DemoTalkAnimCtrl::startAnim() {
    if (mHaveBck) {
        MR::startBckNoInterpole(mActor, mAnimName);
    }
    if (mHaveBtk) {
        MR::startBtk(mActor, mAnimName);
    }
    if (mHaveBpk) {
        MR::startBpk(mActor, mAnimName);
    }
    if (mHaveBtp) {
        MR::startBtp(mActor, mAnimName);
    }
    if (mHaveBrk) {
        MR::startBrk(mActor, mAnimName);
    }
    if (mHaveBva) {
        MR::startBva(mActor, mAnimName);
    }
}

void DemoTalkAnimCtrl::startCamera() {
    MR::startAnimCameraTargetSelf(mActor, mCameraInfo, mCameraName, mCameraStartStep, 1.0f);
}

void DemoTalkAnimCtrl::updateAnim(const BckCtrlData& rBck) {
    FrameCtrl* ctrls[5];

    ctrls[0] = mHaveBtk ? MR::getBtkCtrl(mActor) : nullptr;
    ctrls[1] = mHaveBpk ? MR::getBpkCtrl(mActor) : nullptr;
    ctrls[2] = mHaveBtp ? MR::getBtpCtrl(mActor) : nullptr;
    ctrls[3] = mHaveBrk ? MR::getBrkCtrl(mActor) : nullptr;
    ctrls[4] = mHaveBva ? MR::getBvaCtrl(mActor) : nullptr;

    for (int i = 0; i < 5; i++) {
        FrameCtrl* ctrl = ctrls[i];
        if (ctrl != nullptr) {
            ctrl->mAttribute = rBck.mLoopMode;
            ctrl->mStart = rBck.mStartFrame;
            ctrl->mFrame = (f32)rBck.mStartFrame;
            ctrl->mLoop = rBck.mRepeatFrame;
            ctrl->mFrame = (f32)rBck.mStartFrame;
            ctrl->mEnd = rBck.mEndFrame;
            ctrl->mRate = 1.0f;
        }
    }

    if (mHaveBck) {
        MR::startBck(mActor, mAnimName);
        MR::reflectBckCtrlData(mActor, rBck);
    }
}

void DemoTalkAnimCtrl::updatePause() {
    const char* name = MR::getCurrentDemoPartNameMain(mExecutorName);
    if (DemoFunction::isPauseTimeKeepDemo(mExecutorName)) {
        if (!MR::isSystemTalking()) {
            DemoFunction::resumeTimeKeepDemo(mExecutorName);
        }
    } else {
        if (MR::isDemoPartStep(name, MR::getDemoPartTotalStep(name) - 2)) {
            DemoFunction::pauseTimeKeepDemo(mExecutorName);
        }
    }
}

void DemoTalkAnimCtrl::endCamera() {
    MR::endAnimCamera(mActor, mCameraInfo, mCameraName, mCameraEndStep, true);
}

void DemoTalkAnimCtrl::setupStartDemoPart(const char* pPartName) {
    if (mStartPartName != nullptr) {
        mIsActive = MR::isEqualStringCase(pPartName, mStartPartName);
    }
}
