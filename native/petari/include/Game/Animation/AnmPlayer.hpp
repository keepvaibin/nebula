#pragma once

#include "Game/Render/Clip.hpp"

class Clip;
class Model;
class ModelData;
class ResTable;
class ResourceHolder;

class AnmPlayerBase {
public:
    AnmPlayerBase(const ResTable*);

    virtual void changeAnimation(Clip*);
    virtual void stopAnimation();

    void update();
    void reflectFrame();
    void start(const char*);
    void stop();
    bool isPlaying(const char*) const;
    bool isStop() const {
        return mAnmRes == nullptr || static_cast< s32 >(mFrameCtrl.checkState(1)) == 1 || mFrameCtrl.getRate() == 0.0f;
    }

    Clip* mAnmRes;
    const ResTable* mResTable;
    FrameCtrl mFrameCtrl;
    // The running clip's name in mResTable, found once by start().
    const char* mAnmName;
};

class MaterialAnmPlayerBase : public AnmPlayerBase {
public:
    MaterialAnmPlayerBase(const ResTable*, ModelData*);

    // The animation's values at its frame into the model's own materials.
    virtual void apply(Clip*, Model*) = 0;

    void applyFrame(Model*);

    ModelData* mModelData;
};
