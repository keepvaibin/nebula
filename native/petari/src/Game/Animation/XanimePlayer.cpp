#include "Game/Animation/XanimePlayer.hpp"
#include "Game/Animation/XanimeCore.hpp"
#include "Game/Animation/XanimeResource.hpp"
#include "Game/Render/Clip.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/Model.hpp"

namespace {
    const char* const cDummyName = "NULL";
}  // namespace

XanimePlayer::XanimePlayer(Model* pModel, XanimeResourceTable* pResourceTable) {
    init();

    mModel = pModel;
    mModelData = pModel->mModelData;
    u8 scaleMode = pModel->mModelData->getScaleMode();
    mResourceTable = pResourceTable;

    mCore = new XanimeCore(mResourceTable->mMaxGroupInfoTableSize, mModelData->getJointCount(), scaleMode);
    _7E = true;

    _24[0].init(0);
    _24[1].init(0);
    _20 = _24;

    mCore->initT(mModelData);
}

XanimePlayer::XanimePlayer(Model* pModel, XanimeResourceTable* pResourceTable, XanimePlayer* pPlayer) {
    init();

    mModel = pModel;
    mModelData = pModel->mModelData;
    mResourceTable = pResourceTable;

    mCore = new XanimeCore(mResourceTable->mMaxGroupInfoTableSize, pPlayer->mCore);
    _7E = true;

    _24[0].init(0);
    _24[1].init(0);
    _20 = _24;

    mCore->initT(mModelData);
}

void XanimePlayer::init() {
    mModel = nullptr;
    mModelData = nullptr;
    mCurrentBckName = nullptr;
    mPendingAnimation = nullptr;
    mPrevAnimation = nullptr;
    mCurrentAnimation = nullptr;
    mDefaultAnimation = nullptr;
    mCore = nullptr;
    mResourceTable = nullptr;
    _20 = nullptr;
    mNextFrameCtrlIndex = 0;
    _54 = 0;

    _0C = 0.0f;
    mInterpoleRatio = 0.0f;

    mIsPrepared = false;
    _7E = false;
    mHasDefaultAnimation = false;
    mIsStarted = false;
    mHasPrevFrame = false;

    for (u32 i = 0; i < ARRAY_SIZE(mWeights); i++) {
        mWeights[i] = 0.0f;
    }

    mSimpleGroup = nullptr;
    mPrevFrame = 0.0f;
}

void XanimePlayer::setDefaultAnimation(const char* pAnimationName) {
    const XanimeGroupInfo* pDefaultAnimation = mResourceTable->getGroupInfo(pAnimationName);

    if (mCurrentAnimation == mDefaultAnimation) {
        changeAnimation(pDefaultAnimation);
    }

    mDefaultAnimation = pDefaultAnimation;
    mHasDefaultAnimation = true;
}

void XanimePlayer::changeCurrentAnimation(const XanimeGroupInfo* pAnimation) {
    if (mCurrentAnimation == pAnimation) {
        return;
    }

    mPrevAnimation = mCurrentAnimation;
    mCurrentAnimation = pAnimation;

    updateAfterMovement();
}

void XanimePlayer::stopAnimation() {
    runDefaultAnimation();
}

void XanimePlayer::stopAnimation(const char* pName) {
    if (mResourceTable->getGroupInfo(pName) == mCurrentAnimation) {
        stopAnimation();
    }
}

void XanimePlayer::changeTrackAnimation(u8 track, const char* pName) {
    u32 index = mResourceTable->getSingleIndex(pName);
    if (index != MR::NameIndex::cNoValue) {
        changeTrackAnimation(track, index);
    }
}

void XanimePlayer::changeTrackAnimation(u8 track, u32 index) {
    if (track > mCurrentAnimation->mBckTableVariant) {
        return;
    }

    Clip* pAnm = mResourceTable->findResMotion(mResourceTable->mBckTables[index].mFileName);
    if (pAnm != nullptr) {
        mCore->setBck(track, pAnm);
    }
}

void XanimePlayer::changeAnimationBck(const char* pName) {
    changeSimpleBck(pName);
}

bool XanimePlayer::changeSimpleBck(const char* pName) {
    Clip* pAnm = mResourceTable->findResMotion(pName);
    if (pAnm == nullptr) {
        mCurrentBckName = nullptr;
        return false;
    }

    changeAnimationSimple(pAnm);
    mCurrentBckName = mResourceTable->findStringMotion(pName);

    return true;
}

void XanimePlayer::changeAnimation(const char* pName) {
    const XanimeGroupInfo* pInfo = mResourceTable->getGroupInfo(pName);
    if (pInfo == nullptr) {
        changeSimpleBck(pName);
        return;
    }

    changeAnimation(pInfo);
}

void XanimePlayer::changeAnimationByHash(u32 hash) {
    const XanimeGroupInfo* pInfo = mResourceTable->getGroupInfoFromHash(hash);
    if (pInfo != nullptr) {
        changeAnimation(pInfo);
    }
}

// Sets up the other frame control for pInfo; the running one keeps playing until runNextAnimation.
void XanimePlayer::swapFrameCtrl(const XanimeGroupInfo* pInfo) {
    if (pInfo == nullptr || pInfo->mBcks[0] == nullptr) {
        return;
    }

    mNextFrameCtrlIndex = 1 - _54;

    _20 = &_24[mNextFrameCtrlIndex];
    _20->init(pInfo->mBcks[0]->getFrameMax());
    _20->setRate(pInfo->mRate);
    _20->setStart(static_cast< s32 >(pInfo->mStart));
    _20->setFrame(pInfo->mStart);
    _20->setLoop(static_cast< s32 >(pInfo->mLoop));
    _20->setEnd(static_cast< s32 >(pInfo->mEnd));
    _20->setAttribute(pInfo->mAttribute);
    _20->mInterpoleFrame = pInfo->mInterpoleFrame;

    mPrevFrame = pInfo->mStart;
    mHasPrevFrame = false;
}

void XanimePlayer::changeAnimation(const XanimeGroupInfo* pInfo) {
    if (pInfo == nullptr || mCurrentAnimation == pInfo) {
        return;
    }

    swapFrameCtrl(pInfo);
    mPendingAnimation = pInfo;
    changeCurrentAnimation(pInfo);
    mCurrentBckName = pInfo->mBckName;
}

void XanimePlayer::prepareAnimation(const XanimeGroupInfo* pInfo) {
    if (pInfo == nullptr) {
        return;
    }

    mCore->doFreeze();
    changeCurrentAnimation(pInfo);
    mIsPrepared = true;
    runNextAnimation();
}

void XanimePlayer::setModel(Model* pModel) {
    mModel = pModel;
    mModelData = pModel->mModelData;
    mCore->reconfigJointTransform(mModelData);
}

void XanimePlayer::runNextAnimation() {
    if (!mIsPrepared) {
        return;
    }

    mIsPrepared = false;

    const XanimeGroupInfo* pCurrent = mCurrentAnimation;
    u32 i;
    for (i = 0; i < pCurrent->mBckTableVariant; i++) {
        mCore->setBck(static_cast< u8 >(i), pCurrent->mBcks[i]);
        mWeights[i] = pCurrent->mWeights[i];
    }

    for (; i < mResourceTable->mMaxGroupInfoTableSize; i++) {
        mCore->setBck(static_cast< u8 >(i), nullptr);
        mWeights[i] = 0.0f;
    }

    const bool wasStarted = mIsStarted;

    _54 = mNextFrameCtrlIndex;

    mInterpoleRatio = 0.0f;
    _0C = 1.0f;
    mIsStarted = true;

    // The first animation has no pose to blend from.
    if (!wasStarted) {
        _24[mNextFrameCtrlIndex].mInterpoleFrame = 0;
        mInterpoleRatio = 1.0f;
        updateBeforeMovement();
        calcAnm(0);
    }
}

void XanimePlayer::changeAnimationSimple(Clip* pAnm) {
    mPendingAnimation = nullptr;
    _54 = mNextFrameCtrlIndex;

    mCore->doFreeze();
    changeCurrentAnimation(getSimpleGroup());
    mCore->setBck(0, pAnm);

    mWeights[0] = 1.0f;
    for (u32 i = 1; i < mResourceTable->mMaxGroupInfoTableSize; i++) {
        mCore->setBck(static_cast< u8 >(i), nullptr);
        mWeights[i] = 0.0f;
    }

    getSimpleGroup()->mBcks[0] = pAnm;
    _20->init(pAnm->getFrameMax());
    _20->setAttribute(pAnm->getAttribute());
    _20->setFrame(0.0f);

    mInterpoleRatio = 0.0f;
    _0C = 1.0f;
    _20->mInterpoleFrame = 1;
    mIsStarted = true;
    mIsPrepared = false;
    mPendingAnimation = nullptr;
    mPrevFrame = 0.0f;
    mHasPrevFrame = false;
}

void XanimePlayer::changeSpeed(f32 speed) {
    _20->mRate = speed;
}

void XanimePlayer::changeInterpoleFrame(s32 frame) {
    _20->mInterpoleFrame = frame;
    if (frame == 0) {
        mInterpoleRatio = 1.0f;
        mCore->mInterpoleRatio = 1.0f;
        return;
    }

    mInterpoleRatio = 0.0f;
    mCore->mInterpoleRatio = 0.0f;
    updateInterpoleRatio();
}

bool XanimePlayer::changeTrackWeight(u32 track, f32 weight) {
    if (mCurrentAnimation == nullptr || track >= mCurrentAnimation->mBckTableVariant) {
        return false;
    }

    mWeights[track] = weight;
    return true;
}

void XanimePlayer::calcAnm(u32 joint) {
    f32 currentFrame;
    if (_20->checkState(1) != 0) {
        currentFrame = _20->getEnd();
    } else if (mHasPrevFrame) {
        currentFrame = mPrevFrame;
    } else {
        currentFrame = _20->getFrame();
    }

    if (mCurrentAnimation != nullptr && mCurrentAnimation->mBcks[0] != nullptr) {
        s32 maxFrame = mCurrentAnimation->mBcks[0]->getFrameMax();
        if (maxFrame == 0) {
            maxFrame = 1;
        }

        mCore->mFrameRatio = currentFrame / maxFrame;
    }

    mCore->updateFrame();
    mModelData->getJointAt(joint)->mMtxCalc = mCore;
    mHasPrevFrame = false;
}

void XanimePlayer::overWriteMtxCalc(u32 joint) {
    mModelData->getJointAt(joint)->mMtxCalc = mCore;
}

void XanimePlayer::clearMtxCalc(u32 joint) {
    mModelData->getJointAt(joint)->mMtxCalc = nullptr;
}

void XanimePlayer::clearAnm(u32 joint) {
    if (!mIsStarted) {
        return;
    }

    mModelData->getJointAt(joint)->mMtxCalc = nullptr;
}

void XanimePlayer::updateBeforeMovement() {
    if (!mIsStarted) {
        return;
    }

    // A one-shot or hold-at-end animation that ended falls back to the default one.
    if ((_20->getState() & 1) == 1 && _7E && (_20->getAttribute() == 0 || _20->getAttribute() == 3)) {
        runDefaultAnimation();
    }
}

void XanimePlayer::updateAfterMovement() {
    prepareAnimation(mPendingAnimation);
    mPendingAnimation = nullptr;

    if (!mIsStarted) {
        return;
    }

    if (_20->checkState(1) == 0 && !mHasPrevFrame) {
        f32 rate = _20->getRate();
        f32 frame = _20->getFrame();

        _20->update();
        mPrevFrame = frame;
        mHasPrevFrame = true;

        if (_20->checkState(1) != 0) {
            _20->setRate(rate);
        }
    }

    updateInterpoleRatio();

    for (u32 i = 0; i < mCurrentAnimation->mBckTableVariant; i++) {
        mCore->setWeight(static_cast< u8 >(i), mWeights[i]);
    }

    mCore->mInterpoleRatio = mInterpoleRatio;
    mCore->mPoseBlendRatio = _0C;
}

// Moves mInterpoleRatio 1/frames-left of the way to 1 while the animation plays.
void XanimePlayer::updateInterpoleRatio() {
    if (_20->getRate() != 0.0f || _20->getAttribute() == 1) {
        if (_20->mInterpoleFrame != 0) {
            mInterpoleRatio += (1.0f - mInterpoleRatio) / _20->mInterpoleFrame;
            _20->mInterpoleFrame -= 1;
        } else {
            mInterpoleRatio = 1.0f;
        }

        return;
    }

    mInterpoleRatio = 1.0f;
    _20->mInterpoleFrame = 0;
}

// The translation of `joint` in the heaviest track (ties within 0.01 keep the earlier track).
void XanimePlayer::getMainAnimationTrans(u32 joint, Vec3f* pOut) const {
    XanimeTrack* pTracks = mCore->mTrackList;
    const u8 trackCount = mCore->mTrackCount;
    u8 index = 0;
    f32 threshold = pTracks[0].getWeight() + 0.01f;
    for (u8 i = 1; i < trackCount; i++) {
        const XanimeTrack& rTrack = pTracks[i];
        if (rTrack.mClip != nullptr && rTrack.getWeight() > threshold) {
            threshold = rTrack.getWeight() + 0.01f;
            index = i;
        }
    }

    if (pTracks[index].mClip == nullptr) {
        pOut->zero();
        return;
    }

    TransformInfo info;
    pTracks[index].mClip->getTransform(joint, &info);
    pOut->set(info.mTranslate);
}

bool XanimePlayer::isRun(const char* pName) const {
    if (mCurrentAnimation == nullptr) {
        return false;
    }

    const XanimeGroupInfo* pSimpleGroup = getSimpleGroup();
    if (mCurrentAnimation == pSimpleGroup) {
        return mResourceTable->findResMotion(pName) == pSimpleGroup->mBcks[0];
    }

    return mCurrentAnimation == mResourceTable->getGroupInfo(pName);
}

bool XanimePlayer::isTerminate(const char* pName) const {
    return mCurrentAnimation != mResourceTable->getGroupInfo(pName) || isTerminate();
}

bool XanimePlayer::isTerminate() const {
    return _24[_54].checkState(1) != 0;
}

void XanimePlayer::runDefaultAnimation() {
    mPendingAnimation = nullptr;

    if (!mHasDefaultAnimation || mResourceTable->mAmountOfGroupInfos == 0) {
        return;
    }

    swapFrameCtrl(mDefaultAnimation);
    mPendingAnimation = mDefaultAnimation;
    changeCurrentAnimation(mDefaultAnimation);
    mCurrentBckName = mDefaultAnimation->mBckName;
}

bool XanimePlayer::isAnimationRunSimple() const {
    return mCurrentAnimation != nullptr && mCurrentAnimation == getSimpleGroup();
}

const char* XanimePlayer::getCurrentAnimationName() const {
    return mCurrentAnimation == nullptr ? ::cDummyName : mCurrentAnimation->mParent.mAnimationName;
}

const char* XanimePlayer::getDefaultAnimationName() const {
    return mDefaultAnimation == nullptr ? ::cDummyName : mDefaultAnimation->mParent.mAnimationName;
}

const char* XanimePlayer::getCurrentBckName() const {
    return mCurrentBckName;
}

const char* XanimePlayer::getNameStringPointer(const char* pName) const {
    const XanimeGroupInfo* pInfo = mResourceTable->getGroupInfo(pName);
    return pInfo != nullptr ? pInfo->mParent.mAnimationName : nullptr;
}

bool XanimePlayer::checkPass(f32 frame) const {
    if (!mHasPrevFrame) {
        return _20->checkPass(frame);
    }

    f32 oldFrame = _20->getFrame();
    _20->setFrame(mPrevFrame);
    bool isPass = _20->checkPass(frame);
    _20->setFrame(oldFrame);

    return isPass;
}

XanimeGroupInfo* XanimePlayer::getSimpleGroup() const {
    if (mSimpleGroup != nullptr) {
        return mSimpleGroup;
    }

    return &mResourceTable->mNonGroupInfo;
}

void XanimePlayer::duplicateSimpleGroup() {
    mSimpleGroup = new XanimeGroupInfo();
    mSimpleGroup->init();
    mSimpleGroup->mParent.mAnimationName = "dup-non-group";
    mSimpleGroup->mRate = 1.0f;
    mSimpleGroup->mInterpoleFrame = 1;
    mSimpleGroup->mBckTableVariant = 1;
}

XanimeFrameCtrl::XanimeFrameCtrl() : mInterpoleFrame(0) {
}
