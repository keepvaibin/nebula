#pragma once


class ActorCameraInfo;
class DemoExecutor;
class JMapInfoIter;
class LiveActor;

class DemoCameraInfo {
public:
    /// @brief Creates a new `DemoCameraInfo`.
    DemoCameraInfo();

    const char* mPartName;
    const char* mCameraTargetName;
    s32 mCameraTargetCastID;
    const char* mAnimCameraName;
    s32 mAnimCameraStartFrame;
    s32 mAnimCameraEndFrame;
    u8 mIsContinuous;
    const char* _1C;
    ActorCameraInfo* _20;
    LiveActor* _24;
};

class DemoCameraKeeper {
public:
    DemoCameraKeeper(DemoExecutor*, const JMapInfoIter&);

    void initCast(LiveActor*, const JMapInfoIter&);
    void start();
    void update();
    void end();
    void initActorCamera(DemoCameraInfo*, const JMapInfoIter&);
    void executeType(const DemoCameraInfo*);
    void executeFirst(const DemoCameraInfo*);
    void executeLast(const DemoCameraInfo*);
    void endCurrentCamera();

    DemoExecutor* mExecutor;
    s32 _4;
    DemoCameraInfo* _8;
    s32 _C;
    const DemoCameraInfo* _10;
};
