#pragma once

#include "Game/Render/Clip.hpp"

namespace Ui {
    class AnimTransform;
}  // namespace Ui

class LayoutManager;

class LayoutAnmPlayer {
public:
    LayoutAnmPlayer(const LayoutManager*);

    virtual void movement();
    virtual void reflectFrame();
    virtual void start(const char*);
    virtual void stop();

    bool isStop() const {
        return mAnimTransform == nullptr || static_cast< s32 >(mFrameCtrl.checkState(1)) == 1 || mFrameCtrl.getRate() == 0.0f;
    }

    const char* mAnimName;
    Ui::AnimTransform* mAnimTransform;
    const LayoutManager* mManager;
    FrameCtrl mFrameCtrl;
};
