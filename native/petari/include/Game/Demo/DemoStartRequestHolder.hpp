#pragma once

#include "Game/Util/Array.hpp"

class DemoExecutor;
class LayoutActor;
class LiveActor;
class NameObj;
class Nerve;
class NerveExecutor;

class DemoStartInfo {
public:
    enum DemoType { DemoType_0, DemoType_TimeKeep };
    enum CinemaFrameType { CinemaFrameType_0 };
    enum StarPointerType { StarPointerType_0, StarPointerType_1, StarPointerType_2 };
    enum DeleteEffectType { DeleteEffectType_0, DeleteEffectType_1};

    DemoStartInfo();
    DemoStartInfo& operator=(const DemoStartInfo&);

    LiveActor* _0;
    LayoutActor* _4;
    NerveExecutor* _8;
    NameObj* _C;
    NameObj* _10;
    DemoExecutor* mDemoExecutor;
    const char* mDemoName;
    const char* _1C;
    const Nerve* _20;
    u32 _24;
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
