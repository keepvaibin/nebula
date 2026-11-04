#include "Game/LiveActor/ClippingActorHolder.hpp"
#include "Game/LiveActor/ClippingActorInfo.hpp"
#include "Game/LiveActor/ClippingGroupHolder.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ViewGroupCtrl.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"

namespace {
    static int sActorNumMax = 2560;
};  // namespace

ClippingActorHolder::ClippingActorHolder() : NameObj("\203\116\203\212\203\142\203\163\203\223\203\117\203\101\203\116\203\136\201\133\225\333\216\235" /* クリッピングアクター保持 */), mActorNum(), _10(), _14(), _18(), _1C(), mViewGroupCtrl(), mPlanets(), mPlanetNum() {
    _10 = new ClippingActorInfoList(::sActorNumMax);
    _14 = new ClippingActorInfoList(::sActorNumMax);
    _18 = new ClippingActorInfoList(::sActorNumMax);
    _1C = new ClippingActorInfoList(::sActorNumMax);
    mViewGroupCtrl = new ViewGroupCtrl();
}

void ClippingActorHolder::movement() {
    mViewGroupCtrl->update();

    for (s32 i = 0; i < _10->_4; i++) {
        _10->mClippingActorList[i]->judgeClipping();
    }
}

void ClippingActorHolder::registerActor(LiveActor* pActor) {
    ClippingActorInfo* pInfo = new ClippingActorInfo(pActor);
    _18->add(pInfo);
    mViewGroupCtrl->initMadeDuringPlay(pInfo);

    mActorNum++;
}

void ClippingActorHolder::initSystemInfo(LiveActor* pActor, const JMapInfoIter& rIter) {
    s32 viewGroupID = -1;

    if (MR::getJMapInfoViewGroupID(rIter, &viewGroupID)) {
        mViewGroupCtrl->initActorInfo(find(pActor), viewGroupID);
    }
}

void ClippingActorHolder::initViewGroupTable() {
    mViewGroupCtrl->startInitViewGroupTable();
    mViewGroupCtrl->initViewGroup(_10);
    mViewGroupCtrl->initViewGroup(_18);
    mViewGroupCtrl->initViewGroup(_1C);
    mViewGroupCtrl->initViewGroup(_14);
    mViewGroupCtrl->endInitViewGroupTable();

    ClippingActorInfoList* lists[] = {_10, _14, _18, _1C};
    s32 total = 0;
    for (s32 l = 0; l < 4; l++) {
        total += lists[l]->_4;
    }
    mPlanets = new ClippingActorInfo*[total > 0 ? total : 1];
    for (s32 l = 0; l < 4; l++) {
        for (s32 i = 0; i < lists[l]->_4; i++) {
            if (MR::isMovementPlanet(lists[l]->mClippingActorList[i]->mActor)) {
                mPlanets[mPlanetNum++] = lists[l]->mClippingActorList[i];
            }
        }
    }
}

// The smallest planet whose clipping sphere (it holds the model) has the actor in or just above it.
const ClippingActorInfo* ClippingActorHolder::findGround(const ClippingActorInfo* pInfo) const {
    const ClippingActorInfo* ground = nullptr;
    const TVec3f& pos = pInfo->mActor->mPosition;
    for (s32 i = 0; i < mPlanetNum; i++) {
        const ClippingActorInfo* planet = mPlanets[i];
        if (planet == pInfo) {
            return nullptr;
        }
        const f32 reach = planet->_8 * 1.25f;
        const f32 x = pos.x - planet->_4->x;
        const f32 y = pos.y - planet->_4->y;
        const f32 z = pos.z - planet->_4->z;
        if (x * x + y * y + z * z <= reach * reach && (ground == nullptr || planet->_8 < ground->_8)) {
            ground = planet;
        }
    }
    return ground;
}

void ClippingActorHolder::entryLodCtrl(LodCtrl* pLodCtrl, const JMapInfoIter& rIter) {
    mViewGroupCtrl->entryLodCtrl(pLodCtrl, rIter);
}

void ClippingActorHolder::validateClipping(LiveActor* pActor) {
    pActor->mFlag.mIsInvalidClipping = false;

    ClippingActorInfo* pActorInfo = _14->remove(pActor);

    if (MR::isDead(pActor)) {
        _18->add(pActorInfo);
    } else if (pActorInfo->isGroupClipping()) {
        _1C->add(pActorInfo);
    } else {
        _10->add(pActorInfo);
    }
}

void ClippingActorHolder::invalidateClipping(LiveActor* pActor) {
    pActor->mFlag.mIsInvalidClipping = true;

    ClippingActorInfo* pActorInfo;

    if (MR::isDead(pActor)) {
        pActorInfo = _18->remove(pActor);
    } else if (_10->isInList(pActor)) {
        pActorInfo = _10->remove(pActor);
    } else {
        pActorInfo = _1C->remove(pActor);
    }

    _14->add(pActorInfo);

    if (MR::isClipped(pActor)) {
        pActor->endClipped();
    }
}

void ClippingActorHolder::addToClippingTarget(LiveActor* pActor) {
    if (MR::isInvalidClipping(pActor) || _10->isInList(pActor) || _1C->isInList(pActor)) {
        return;
    }

    ClippingActorInfo* pActorInfo = _18->remove(pActor);

    if (pActorInfo->isGroupClipping()) {
        _1C->add(pActorInfo);
    } else {
        _10->add(pActorInfo);
    }
}

void ClippingActorHolder::removeFromClippingTarget(LiveActor* pActor) {
    if (MR::isInvalidClipping(pActor)) {
        return;
    }

    if (_18->isInList(pActor)) {
        return;
    }

    ClippingActorInfo* pActorInfo;

    if (_1C->isInList(pActor)) {
        pActorInfo = _1C->remove(pActor);
    } else {
        pActorInfo = _10->remove(pActor);
    }

    _18->add(pActorInfo);
}

ClippingActorInfo* ClippingActorHolder::startGroupClipping(LiveActor* pActor, const JMapInfoIter& rIter) {
    ClippingActorInfo* pActorInfo = nullptr;

    if (_10->isInList(pActor)) {
        pActorInfo = _10->remove(pActor);

        _1C->add(pActorInfo);
    } else if (_18->isInList(pActor)) {
        pActorInfo = _18->find(pActor, 0);
    } else if (_14->isInList(pActor)) {
        pActorInfo = _14->remove(pActor);

        _1C->add(pActorInfo);
        pActor->endClipped();
    }

    pActorInfo->setGroupClippingNo(rIter);

    return pActorInfo;
}

void ClippingActorHolder::setTypeToSphere(LiveActor* pActor, f32 range, const TVec3f* pCenter) {
    find(pActor)->setTypeToSphere(range, pCenter);
}

void ClippingActorHolder::setFarClipLevel(LiveActor* pActor, s32 level) {
    find(pActor)->setFarClipLevel(level);
}

ClippingActorInfo* ClippingActorHolder::find(const LiveActor* pActor) const {
    ClippingActorInfo* pActorInfo;

    pActorInfo = _10->findOrNone(pActor);

    if (pActorInfo != nullptr) {
        return pActorInfo;
    }

    pActorInfo = _18->findOrNone(pActor);

    if (pActorInfo != nullptr) {
        return pActorInfo;
    }

    pActorInfo = _1C->findOrNone(pActor);

    if (pActorInfo != nullptr) {
        return pActorInfo;
    }

    return _14->find(pActor, 0);
}
