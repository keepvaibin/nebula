#include "Game/Animation/LayoutAnmPlayer.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include <Game/Ui/Animation.hpp>

LayoutAnmPlayer::LayoutAnmPlayer(const LayoutManager* pManager) : mAnimName(), mAnimTransform(), mManager(pManager), mFrameCtrl() {
}

void LayoutAnmPlayer::movement() {
    if (mAnimTransform != nullptr) {
        mFrameCtrl.update();
    }
}

void LayoutAnmPlayer::reflectFrame() {
    if (mAnimTransform != nullptr) {
        mAnimTransform->mFrame = mFrameCtrl.getFrame();
    }
}

void LayoutAnmPlayer::start(const char* pAnimName) {
    Ui::AnimTransform* pAnimTransform = mManager->getAnimTransform(pAnimName);

    // An animation the layout lacks (a mod's layout) plays nothing and reads as stopped.
    if (pAnimTransform == nullptr) {
        mAnimTransform = nullptr;
        mAnimName = pAnimName;
        return;
    }

    if (pAnimTransform != mAnimTransform) {
        mAnimTransform = pAnimTransform;
    }

    u32 frameSize = mAnimTransform->GetFrameSize();

    mFrameCtrl.init(static_cast< s32 >(frameSize));

    if (mAnimTransform->IsLoopData()) {
        mFrameCtrl.setAttribute(2);
    } else {
        mFrameCtrl.setAttribute(0);
    }

    mFrameCtrl.setFrame(0.0f);
    mFrameCtrl.setRate(1.0f);
    mAnimName = pAnimName;
}

void LayoutAnmPlayer::stop() {
    mFrameCtrl.setRate(0.0f);
}
