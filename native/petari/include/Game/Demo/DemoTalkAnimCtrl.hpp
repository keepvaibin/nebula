#pragma once

#include "Game/System/NerveExecutor.hpp"

class ActorCameraInfo;
class BckCtrlData;
class TableIter;
class LiveActor;

class DemoTalkAnimCtrl : public NerveExecutor {
public:
    DemoTalkAnimCtrl(LiveActor*, const char*, const char*);

    void initForScene(const char*, const char*, const TableIter&);
    void updateCamera();
    void createBckCtrlData(BckCtrlData*, s32) const;
    void initForActor(const char*);
    void startDemo();
    bool updateDemo();
    void startAnim();
    void startCamera();
    void updateAnim(const BckCtrlData&);
    void updatePause();
    void endCamera();
    void setupStartDemoPart(const char*);

    LiveActor* mActor;
    const char* mExecutorName;
    const char* mStartPartName;
    const char* mPartName;
    const char* mAnimName;
    const char* mCameraName;
    ActorCameraInfo* mCameraInfo;
    s32 mAnimFrame;
    bool mIsActive;
    bool mIsScene;
    bool mIsTalkPart;
    s32 mInterpole;
    s32 mCameraStartStep;
    s32 mCameraEndStep;
    bool mHavePlayerBck;
    bool mHaveCamera;
    bool mHaveBck;
    bool mHaveBtk;
    bool mHaveBpk;
    bool mHaveBtp;
    bool mHaveBrk;
    bool mHaveBva;
};
