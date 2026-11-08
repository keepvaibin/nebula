#pragma once

#include "Game/Scene/MovementControlType.hpp"
#include "Game/Util/Array.hpp"

class DemoExecutor;
class LayoutActor;
class LiveActor;
class NameObj;
class Nerve;
class NerveExecutor;

class DemoStartInfo {
public:
    enum DemoType { DemoType_Programmable, DemoType_TimeKeep };
    enum CinemaFrameType { CinemaFrameType_Frame, CinemaFrameType_None };
    // The pointer mode during the demo (StarPointerMode_Demo, _DemoWithStarPointer, _DemoWithHandPointerFinger).
    enum StarPointerType { StarPointerType_Demo, StarPointerType_DemoWithStarPointer, StarPointerType_DemoWithHandPointerFinger };
    // Whether the one-time emitters still playing are deleted when the demo starts.
    enum DeleteEffectType { DeleteEffectType_Keep, DeleteEffectType_OneTimeEmitter };

    DemoStartInfo();
    DemoStartInfo& operator=(const DemoStartInfo&) = default;

    LiveActor* mActor;
    LayoutActor* mLayout;
    NerveExecutor* mNerveExecutor;
    NameObj* mNameObj;
    NameObj* mNerveExecutorHost;
    DemoExecutor* mDemoExecutor;
    const char* mDemoName;
    const char* mPartName;
    const Nerve* mDemoNerve;
    MR::MovementControlType mMovementType;
    DemoType mDemoType;
    CinemaFrameType mFrameType;
    StarPointerType mPointerType;
    DeleteEffectType mDeleteEffectType;
};

class DemoStartRequestHolder {
public:
    DemoStartRequestHolder();

    void pushRequest(LiveActor*, const char*);
    void pushRequest(LayoutActor*, const char*);
    void pushRequest(NerveExecutor*, const char*);
    void pushRequest(NameObj*, const char*);
    void popRequest();
    bool isExistRequest() const;
    const DemoStartInfo* getCurrentInfo() const;
    void registerStartDemoInfo(const DemoStartInfo&);
    DemoStartInfo* find(const LiveActor*, const char*) const;
    DemoStartInfo* find(const LayoutActor*, const char*) const;
    DemoStartInfo* find(const NerveExecutor*, const char*) const;
    DemoStartInfo* find(const NameObj*, const char*) const;
    DemoStartInfo* findEmpty();

    MR::Vector< MR::AssignableArray< DemoStartInfo* > > mStartInfos;
    // A queue: requests before mRequestHead are popped; both reset when it empties.
    MR::Vector< MR::AssignableArray< const DemoStartInfo* > > mRequests;
    s32 mRequestHead;
    NameObj* mProxyObj;
};
