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
    XanimeFrameCtrl();

    // Frames left to blend in from the previous animation's pose.
    s32 mInterpoleFrame;
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
        return mFrameCtrl;
    }

    inline XanimeCore* getCore() {
        return mCore;
    }
    inline f32 getRate() {
        return mFrameCtrl->getRate();
    }
    Model* mModel;
    ModelData* mModelData;
    // XanimeCore's blend from the frozen pose (0) to the new animation (1).
    f32 mInterpoleRatio;
    // XanimeCore::mPoseBlendRatio: blend from last frame's pose (0) to this frame's (1); 1 once an animation ran (walk lowers it).
    f32 mPoseBlendRatio;
    f32 mWeights[4];
    // The frame control in use: mFrameCtrls[mNextFrameCtrlIndex] once swapFrameCtrl ran.
    XanimeFrameCtrl* mFrameCtrl;
    XanimeFrameCtrl mFrameCtrls[2];
    // Index into mFrameCtrls of the running animation's control.
    u8 mFrameCtrlIndex;
    u8 mNextFrameCtrlIndex;
    const char* mCurrentBckName;
    const XanimeGroupInfo* mDefaultAnimation;
    const XanimeGroupInfo* mCurrentAnimation;
    // Started by the next updateAfterMovement.
    const XanimeGroupInfo* mPendingAnimation;
    XanimeCore* mCore;
    XanimeResourceTable* mResourceTable;
    XanimeGroupInfo* mSimpleGroup;
    bool mIsStarted;
    bool mHasDefaultAnimation;
    // A one-shot or hold animation that ended returns to the default one (the rush states clear it).
    bool mReturnToDefault;
    bool mIsPrepared;
    // The frame before this movement's update, read until calcAnm.
    f32 mPrevFrame;
    bool mHasPrevFrame;
};
