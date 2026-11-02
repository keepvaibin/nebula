#include "Game/LiveActor/ClippingActorInfo.hpp"
#include "Game/LiveActor/ClippingJudge.hpp"
#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ViewGroupCtrl.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/Util.hpp"
#include "Game/System/NebulaHost.hpp"

ClippingActorInfo::ClippingActorInfo(LiveActor* pActor) {
    mActor = pActor;
    _4 = 0;
    _8 = 0.0f;
    mInfo = 0;
    mFarClipLevel = 6;
    _12 = -1;
    _14 = 0;
    setTypeToSphere(300.0f, 0);
}

void ClippingActorInfo::judgeClipping() {
    if (isJudgedToClip()) {
        if (!MR::isClipped(mActor)) {
            startClipped();
        } else {
            updateDrawnClipped();
        }
    } else {
        if (MR::isClipped(mActor)) {
            mActor->endClipped();
        }
    }
}

bool ClippingActorInfo::isJudgedToClip() const {
    s32 farClipLevel = mFarClipLevel;
    if (_14->_0 == 1) {
        farClipLevel = 0;
    }

    f32 val = _8;
    ClippingJudge* judge = MR::getClippingJudge();
    return judge->isJudgedToClipFrustum(*_4, val, farClipLevel);
}

// The level's distances say when an actor runs; the camera says whether it is seen. An actor out of its distance and
// in view stays stopped and keeps its model, so nothing runs that the level did not expect and scenery does not pop in.
bool ClippingActorInfo::isDrawnWhileClipped() const {
    return !_14->_4 && !MR::isNoEntryDrawBuffer(mActor) && MR::isDrawnFarWhenClipped(mActor) &&
           !MR::getClippingJudge()->isJudgedToClipFrustum(*_4, _8, 0);
}

void ClippingActorInfo::startClipped() {
    if (isDrawnWhileClipped()) {
        mActor->mFlag.mIsDrawnClipped = true;
        mActor->prepareDrawClipped();
    }

    mActor->startClipped();
}

void ClippingActorInfo::updateDrawnClipped() {
    const bool drawn = isDrawnWhileClipped();
    if (drawn == mActor->mFlag.mIsDrawnClipped) {
        return;
    }

    mActor->mFlag.mIsDrawnClipped = drawn;
    if (drawn) {
        mActor->prepareDrawClipped();
        MR::connectToDrawTemporarily(mActor);
        if (mActor->mEffectKeeper != nullptr) {
            mActor->mEffectKeeper->playEmitterOffClipped();
        }
    } else {
        MR::disconnectToDrawTemporarily(mActor);
        if (mActor->mEffectKeeper != nullptr) {
            mActor->mEffectKeeper->stopEmitterOnClipped();
        }
    }
}

bool ClippingActorInfo::isGroupClipping() const {
    return mInfo;
}

void ClippingActorInfo::setTypeToSphere(f32 radius, const TVec3f* pCenter) {
    _8 = radius;
    if (!pCenter) {
        _4 = &mActor->mPosition;
    } else {
        _4 = pCenter;
    }
}

void ClippingActorInfo::setGroupClippingNo(const JMapInfoIter& rIter) {
    mInfo = new JMapIdInfo(MR::createJMapIdInfoFromClippingGroupId(rIter));
}

ClippingActorInfoList::ClippingActorInfoList(int a1) {
    _0 = a1;
    _4 = 0;
    mClippingActorList = 0;

    mClippingActorList = new ClippingActorInfo*[a1];

    for (s32 i = 0; i < _0; i++) {
        mClippingActorList[i] = 0;
    }
}

void ClippingActorInfoList::add(ClippingActorInfo* pInfo) {
    mClippingActorList[_4] = pInfo;
    _4++;
}

ClippingActorInfo* ClippingActorInfoList::remove(LiveActor* pActor) {
    s32 index = 0;
    ClippingActorInfo* info = find(pActor, &index);
    s32 lastIndex = _4 - 1;
    mClippingActorList[index] = mClippingActorList[lastIndex];
    _4--;
    return info;
}

ClippingActorInfo* ClippingActorInfoList::find(const LiveActor* pActor, s32* pIndex) const {
    for (s32 i = 0; i < _4; i++) {
        ClippingActorInfo* inf = mClippingActorList[i];

        if (inf->mActor == pActor) {
            if (pIndex) {
                *pIndex = i;
            }

            return mClippingActorList[i];
        }
    }

    return mClippingActorList[0];
}

ClippingActorInfo* ClippingActorInfoList::findOrNone(const LiveActor* pActor) const {
    for (s32 i = _4 - 1; i >= 0; i--) {
        ClippingActorInfo* info = mClippingActorList[i];
        if (info->mActor == pActor) {
            return info;
        }
    }

    return nullptr;
}

bool ClippingActorInfoList::isInList(const LiveActor* pActor) const {
    for (s32 i = 0; i < _4; i++) {
        ClippingActorInfo* inf = mClippingActorList[i];
        if (inf->mActor == pActor) {
            return true;
        }
    }

    return false;
}
