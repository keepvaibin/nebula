#include "Game/Screen/LayoutPaneCtrl.hpp"
#include "Game/Animation/LayoutAnmPlayer.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include <Game/Ui/Pane.hpp>

LayoutPaneCtrl::LayoutPaneCtrl(LayoutManager* pHost, const char* pPaneName, u32 animLayerNum)
    : mHost(pHost), mPane(), mStatePane(), mAnmPlayerArray(animLayerNum), mFollowType(), mFollowPos() {
    LayoutManager* pManager = mHost;
    mPane = pPaneName == nullptr ? pManager->mLayout->mpRootPane : pManager->mLayout->mpRootPane->FindPaneByName(pPaneName, true);

    // A pane the layout lacks (a mod's layout) is the root, as LayoutManager::findPaneOrRoot answers.
    if (mPane == nullptr) {
        mPane = pManager->mLayout->mpRootPane;
    }

    for (s32 i = 0; i < mAnmPlayerArray.size(); i++) {
        mAnmPlayerArray[i] = new LayoutAnmPlayer(pHost);
    }
}

void LayoutPaneCtrl::movement() {
    for (s32 i = 0; i < mAnmPlayerArray.size(); i++) {
        mAnmPlayerArray[i]->movement();
    }
}

void LayoutPaneCtrl::calcAnim() {
    for (s32 i = 0; i < mAnmPlayerArray.size(); i++) {
        mAnmPlayerArray[i]->reflectFrame();
    }
}

void LayoutPaneCtrl::start(const char* pAnimName, u32 layer) {
    LayoutAnmPlayer* pAnmPlayer = mAnmPlayerArray[layer];

    if (pAnmPlayer->mAnimTransform != nullptr) {
        if (mHost->mParentAnimValid) {
            mPane->UnbindAnimation(pAnmPlayer->mAnimTransform, true);
        } else {
            mHost->unbindPaneCtrlAnim(this, pAnmPlayer->mAnimTransform);
        }
    }

    pAnmPlayer->start(pAnimName);

    // The layout has no such animation: nothing to bind.
    if (pAnmPlayer->mAnimTransform == nullptr) {
        return;
    }

    if (mHost->mParentAnimValid) {
        mPane->UnbindAnimation(pAnmPlayer->mAnimTransform, true);
        mPane->BindAnimation(pAnmPlayer->mAnimTransform, true);
    } else {
        mHost->bindPaneCtrlAnim(this, pAnmPlayer->mAnimTransform);
    }
}

void LayoutPaneCtrl::stop(u32 layer) {
    mAnmPlayerArray[layer]->stop();
}

bool LayoutPaneCtrl::isAnimStopped(u32 layer) const {
    return mAnmPlayerArray[layer]->isStop();
}

void LayoutPaneCtrl::setFollowPos(const Vec2f* pFollowPos) {
    mFollowPos = pFollowPos;
    if (mPane != nullptr) {
        mPane->mpFollow = pFollowPos != nullptr ? this : nullptr;
    }
}

void LayoutPaneCtrl::Follow(const Ui::Pane& rPane, Mtx34f& rWorld) const {
    Mtx34f& global = rWorld;

    switch (mFollowType) {
    case 0: {
        Vec2f position;
        MR::convertScreenPosToLayoutPos(&position, *mFollowPos);
        global.mMtx[0][3] = position.x;
        global.mMtx[1][3] = position.y;
        break;
    }
    case 1: {
        Vec2f screenOrigin;
        MR::convertLayoutPosToScreenPos(&screenOrigin, Vec2f(0.0f, 0.0f));
        Vec2f offset(screenOrigin + *mFollowPos);
        MR::convertScreenPosToLayoutPos(&offset, offset);
        global.mMtx[0][3] += offset.x;
        // A distance, in the layout's own units on any canvas: only places stretch with a taller one.
        global.mMtx[1][3] -= mFollowPos->y;
        break;
    }
    case 2: {
        Mtx34f local = rPane.mMtx;
        Mtx34f inverse;
        NebulaMtxInverse(local, inverse);
        global.concat(inverse);

        Mtx34f replacement;
        replacement.set(local);
        replacement.mMtx[0][3] = mFollowPos->x;
        replacement.mMtx[1][3] = mFollowPos->y;
        MtxPtr pGlobal = global;
        NebulaMtxConcat(pGlobal, replacement, pGlobal);
        break;
    }
    case 3: {
        Vec3f offset(mFollowPos->x, mFollowPos->y, 0.0f);
        NebulaMtxMultVecSR(rPane.mMtx, &offset, &offset);
        global.mMtx[0][3] += offset.x;
        global.mMtx[1][3] += offset.y;
        break;
    }
    }
}

FrameCtrl* LayoutPaneCtrl::getFrameCtrl(u32 layer) const {
    return &mAnmPlayerArray[layer]->mFrameCtrl;
}
