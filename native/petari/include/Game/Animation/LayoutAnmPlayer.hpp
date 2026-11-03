#pragma once

#include <JSystem/J3DGraphAnimator/J3DAnimation.hpp>

namespace nw4r {
    namespace lyt {
        class AnimTransform;
    };  // namespace lyt
};  // namespace nw4r

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
    nw4r::lyt::AnimTransform* mAnimTransform;
    const LayoutManager* mManager;
    J3DFrameCtrl mFrameCtrl;
};
