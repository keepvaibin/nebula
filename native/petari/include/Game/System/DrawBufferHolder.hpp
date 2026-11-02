#pragma once

#include "Game/System/DrawBufferGroup.hpp"
#include "Game/Util/Array.hpp"

class LiveActor;

typedef MR::Vector< MR::AssignableArray< DrawBufferGroup* > > ExecutorList;

struct DrawBufferInitialTable {
    u32 mDrawBufferType;
    s32 mCapacity;
    u32 mLightType;
    u32 mDrawCameraType;
};

class DrawBufferHolder {
public:
    DrawBufferHolder();

    void initTable(const DrawBufferInitialTable*, s32);

    void allocateActorListBuffer();
    void active(LiveActor*, s32, s32);
    s32 registerDrawBuffer(LiveActor*, s32);
    void deactive(LiveActor*, s32, s32);
    void findLightInfo(LiveActor*, s32, s32);
    void entry(s32);

    void drawOpa(s32) const;
    void drawXlu(s32) const;

    ExecutorList& getExecuteList(s32 drawBufferType);
    bool isBufferGroupEmpty(s32 drawBufferType) const;

    // debug symbol map functions
    s32 getActiveActorCount(const char*);
    void onEntryAll();
    void offEntryAll();
    void onDrawOpaAll();
    void offDrawOpaAll();
    void onDrawXluAll();
    void offDrawXluAll();

    // TODO: see .cpp

    MR::AssignableArray< DrawBufferGroup > mBufferGroups;
    ExecutorList mExecuteLists[3];
    bool mTableInitialized;
};
