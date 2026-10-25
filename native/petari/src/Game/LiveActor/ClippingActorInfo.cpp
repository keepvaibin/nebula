#include "Game/LiveActor/ClippingActorInfo.hpp"
#include "Game/LiveActor/ClippingJudge.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ViewGroupCtrl.hpp"
#ifdef TARGET_PC
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include <revolution/os.h>
#endif
#include "Game/Util.hpp"

ClippingActorInfo::ClippingActorInfo(LiveActor* pActor) {
    mActor = pActor;
    _4 = 0;
    _8 = 0.0f;
    mInfo = 0;
    mFarClipLevel = 6;
    _12 = -1;
    _14 = 0;
#ifdef TARGET_PC
    mKeptForLight = false;
    mLightReach = -1.0f;
#endif
    setTypeToSphere(300.0f, 0);
}

void ClippingActorInfo::judgeClipping() {
    if (isJudgedToClip()) {
        if (!MR::isClipped(mActor)) {
            mActor->startClipped();
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
#ifdef TARGET_PC
    // Ray-traced light draws the whole stage to the camera's far plane: an actor's own draw distance (5,000 to 60,000
    // units by its clip level) was the console's budget, and with it objects stood in the sky without the planet they
    // sit on, and the traced light lost whatever lay beyond.
    if (judge->isLightSourcesWanted()) {
        farClipLevel = 0;
    }
    if (!judge->isJudgedToClipFrustum(*_4, val, farClipLevel)) {
        mKeptForLight = false;
        return false;
    }
    // Ray-traced light: an actor out of view whose shadow, or whose light, reaches it stays unclipped. Drawn while
    // clipped it showed frozen where the game stopped it, or in its bind pose if it never moved (a Toad standing in a
    // T-pose in the light of the plaza).
    mKeptForLight = isWantedForLight(farClipLevel);
    return !mKeptForLight;
#else
    return judge->isJudgedToClipFrustum(*_4, val, farClipLevel);
#endif
}

#ifdef TARGET_PC
bool ClippingActorInfo::isWantedForLight(s32 farClipLevel) const {
    const ClippingJudge* judge = MR::getClippingJudge();
    if (!judge->isLightSourcesWanted() || MR::isDead(mActor) || MR::isNoEntryDrawBuffer(mActor)) {
        return false;
    }
    if (mLightReach < 0.0f) {
        // Asked once, while ray-traced light is on: the reach of the model's light (0: none).
        mLightReach = mActor->mModelManager != nullptr ? __NebulaLightReach(MR::getModelResName(mActor), _8) : 0.0f;
    }
    return judge->isShadowCasterInView(*_4, _8, farClipLevel) ||
           (mLightReach > 0.0f && judge->isLightInView(*_4, _8 + mLightReach));
}
#endif

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
