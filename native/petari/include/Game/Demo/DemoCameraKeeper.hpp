#pragma once


class ActorCameraInfo;
class DemoExecutor;
class TableIter;
class LiveActor;

class DemoCameraInfo {
public:
    DemoCameraInfo();

    const char* mPartName;
    const char* mCameraTargetName;
    s32 mCameraTargetCastID;
    const char* mAnimCameraName;
    s32 mAnimCameraStartFrame;
    s32 mAnimCameraEndFrame;
    u8 mIsContinuous;
    const char* mCameraName;
    ActorCameraInfo* mCameraInfo;
    LiveActor* mTargetActor;
};

class DemoCameraKeeper {
public:
    DemoCameraKeeper(DemoExecutor*, const TableIter&);

    void initCast(LiveActor*, const TableIter&);
    void start();
    void update();
    void end();
    void initActorCamera(DemoCameraInfo*, const TableIter&);
    void executeType(const DemoCameraInfo*);
    void executeFirst(const DemoCameraInfo*);
    void executeLast(const DemoCameraInfo*);
    void endCurrentCamera();

    DemoExecutor* mExecutor;
    s32 mNumInfos;
    DemoCameraInfo* mInfos;
    s32 mCurrentIndex;
    const DemoCameraInfo* mCurrentInfo;
};
