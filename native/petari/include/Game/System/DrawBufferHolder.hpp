#pragma once

#include "Game/System/DrawBufferGroup.hpp"
#include "Game/Util/Array.hpp"

class LiveActor;

typedef MR::Vector< MR::AssignableArray< DrawBufferGroup* > > ExecutorList;

struct DrawBufferInitialTable {
    u32 mDrawBufferType;
    u32 mLightType;
    u32 mDrawCameraType;
};

// Each draw buffer group is listed for entry in its camera type's list only while it has active actors.
class DrawBufferHolder {
public:
    static const s32 cDrawCameraTypeNum = 3;

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

    MR::AssignableArray< DrawBufferGroup > mBufferGroups;
    ExecutorList mExecuteLists[cDrawCameraTypeNum];
};
