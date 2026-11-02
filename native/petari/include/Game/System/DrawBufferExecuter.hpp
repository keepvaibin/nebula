#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class J3DModel;
class LiveActor;
class DrawBuffer;

class DrawBufferExecuter {
public:
    DrawBufferExecuter(const char*, J3DModel*, s32);

    void allocateActorListBuffer();
    void add(LiveActor*);
    void remove(LiveActor*);
    void findLightInfo(LiveActor*);
    void onExecuteLight(s32);
    void offExecuteLight();
    void calcViewAndEntry();
    void drawOpa() const;
    void drawXlu() const;

    LiveActor* getActor(int index) const {
        return mActors[index];
    }

    LiveActor** mActors;
    s32 mMaxNumActors;
    s32 mNumActors;
    const char* mName;
    DrawBuffer* mDrawBuffer;
    s32 mLightType;
    s32 mDrawBufferCount;
};
