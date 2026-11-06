#pragma once

#include "Game/Render/Clip.hpp"
#include <nebula_geometry.hpp>

class Clip;
class Model;
class XanimeCore;
class XanimeGroupInfo;
class XanimeResourceTable;

class XanimeFrameCtrl : public FrameCtrl {
public:
    /// @brief Creates a new `XanimeFrameCtrl`.
    XanimeFrameCtrl();

    s32 _14;
};

class XanimePlayer {
public:
    XanimePlayer(Model*, XanimeResourceTable*);
    XanimePlayer(Model*, XanimeResourceTable*, XanimePlayer*);

    void init();
    void stopAnimation();
    void stopAnimation(const char*);
    void changeTrackAnimation(u8, const char*);
    void changeTrackAnimation(u8, u32);
    void changeAnimationBck(const char*);
    bool changeSimpleBck(const char*);
    void changeAnimation(const char*);
    void changeAnimationByHash(u32);
    void swapFrameCtrl(const XanimeGroupInfo*);
    void changeAnimation(const XanimeGroupInfo*);
    void prepareAnimation(const XanimeGroupInfo*);
    void runNextAnimation();
    void changeAnimationSimple(Clip*);
    void changeSpeed(f32);
    void changeInterpoleFrame(s32);
    bool changeTrackWeight(u32, f32);
    void calcAnm(u32);
    void setModel(Model*);
    void overWriteMtxCalc(u32);
    void clearMtxCalc(u32);
    void clearAnm(u32);
    void updateBeforeMovement();
    void updateAfterMovement();
    void updateInterpoleRatio();
    void getMainAnimationTrans(u32, Vec3f*) const;
    bool isRun(const char*) const;
    bool isTerminate(const char*) const;
    bool isTerminate() const;
    void setDefaultAnimation(const char*);
    void runDefaultAnimation();
    bool isAnimationRunSimple() const;
    const char* getCurrentAnimationName() const;
    const char* getDefaultAnimationName() const;
    const char* getCurrentBckName() const;
    const char* getNameStringPointer(const char*) const;
    bool checkPass(f32) const;
    f32 tellAnimationFrame() const;
    XanimeGroupInfo* getSimpleGroup() const;
    void duplicateSimpleGroup();
    void changeCurrentAnimation(const XanimeGroupInfo*);
    XanimeFrameCtrl* getFrameCtrl() {
        return _20;
    }

    inline XanimeCore* getCore() {
        return mCore;
    }
    inline f32 getRate() {
        return _20->getRate();
    }

    Model* mModel;
    ModelData* mModelData;
    f32 _08;
    f32 _0C;
    f32 mWeights[4];
    XanimeFrameCtrl* _20;
    XanimeFrameCtrl _24[2];
    u8 _54;
    u8 _55;
    const char* mCurrentBckName;
    const XanimeGroupInfo* mDefaultAnimation;
    const XanimeGroupInfo* mCurrentAnimation;
    const XanimeGroupInfo* mPrevAnimation;
    const XanimeGroupInfo* _68;
    XanimeCore* mCore;
    XanimeResourceTable* mResourceTable;
    XanimeGroupInfo* mSimpleGroup;
    s32 _78;
    bool _7C;
    bool _7D;
    bool _7E;
    bool _7F;
    bool _80;
    f32 _84;
    bool _88;
};
