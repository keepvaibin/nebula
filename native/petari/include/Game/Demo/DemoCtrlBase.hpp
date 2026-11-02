#pragma once


class ActorCameraInfo;
class JMapInfoIter;
class LiveActor;

class DemoCtrlBase {
public:
    DemoCtrlBase(LiveActor*, const char*);

    void init(const JMapInfoIter&);
    void end();
    void update();
    bool isDone() const;
    bool isExistEndFrame() const;
    bool tryStart();

    LiveActor* mActor;
    ActorCameraInfo* mCameraInfo;
    const char* mDemoName;
    s32 mCurrentFrame;
    bool _10;
};
