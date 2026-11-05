#pragma once

#include <JSystem/J3DGraphAnimator/J3DAnimation.hpp>

class J3DAnmBase;
class J3DModel;
class J3DModelData;
class ResTable;
class ResourceHolder;

class AnmPlayerBase {
public:
    AnmPlayerBase(const ResTable*);

    virtual void changeAnimation(J3DAnmBase*);
    virtual void stopAnimation();

    void update();
    void reflectFrame();
    void start(const char*);
    void stop();
    bool isPlaying(const char*) const;
    bool isStop() const {
        return mAnmRes == nullptr || static_cast< s32 >(mFrameCtrl.checkState(1)) == 1 || mFrameCtrl.getRate() == 0.0f;
    }

    J3DAnmBase* mAnmRes;
    const ResTable* mResTable;
    J3DFrameCtrl mFrameCtrl;
};

class MaterialAnmPlayerBase : public AnmPlayerBase {
public:
    MaterialAnmPlayerBase(const ResTable*, J3DModelData*);

    // The animation's values at its frame into the model's own materials.
    virtual void apply(J3DAnmBase*, J3DModel*) = 0;

    void applyFrame(J3DModel*);

    J3DModelData* mModelData;
};
