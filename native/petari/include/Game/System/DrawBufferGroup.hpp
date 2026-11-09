#pragma once

#include "Game/System/DrawBufferExecuter.hpp"
#include "Game/Util/Array.hpp"

class LiveActor;

class DrawBufferGroup {
public:
    DrawBufferGroup();

    s32 registerDrawBuffer(LiveActor*);
    void allocateActorListBuffer();
    void active(LiveActor*, s32);
    void deactive(LiveActor*, s32);
    void findLightInfo(LiveActor*, s32);
    void entry();
    void drawOpa() const;
    void drawXlu() const;
    void setDrawCameraType(s32);
    void setLightType(s32);
    s32 findExecuterIndex(const char*) const;

    bool isExecutorEmpty(int index) const {
        return mExecutors[index]->mNumActors == 0;
    }

    MR::Vector< MR::AssignableArray< DrawBufferExecuter* > > mExecutors;
    MR::Vector< MR::AssignableArray< DrawBufferExecuter* > > mActiveExecutors;
    s32 mDrawCameraType;
    s32 mLightType;
    s32 mLightLoadType;
};
