#pragma once

#include "Game/Sound/SoundHandles.hpp"
#include <nebula_geometry.hpp>

class SoundHandle;
class SoundID;
class NebulaArena;

class SoundObject : public SoundHandles {
public:
    SoundObject();

    SoundObject(Vec3f*, u8, NebulaArena*);
    ~SoundObject();

    void process();
    void dispose();
    SoundHandle* startSound(SoundID);
    SoundHandle* startLevelSound(SoundID);

    void stopSound(SoundID, u32);
    SoundHandle* getLowPrioSound(SoundID);
    SoundHandle* getFreeHandleNotReserved();

    const Vec3f* getPos() const {
        return mPos;
    }

    bool mIsAllocated;
    Vec3f* mPos;
};
