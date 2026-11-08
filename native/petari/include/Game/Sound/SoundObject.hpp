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

    virtual void process();
    virtual void dispose();
    virtual bool stopOK(SoundHandle&);
    virtual SoundHandle* startSound(SoundID);
    virtual SoundHandle* startSoundIndex(SoundID, u8);
    virtual SoundHandle* startLevelSound(SoundID);
    virtual SoundHandle* startLevelSoundIndex(SoundID, u8);

    void stopSound(SoundID, u32);
    SoundHandle* getLowPrioSound(SoundID);
    SoundHandle* getFreeHandleNotReserved();

    const Vec3f* getPos() const {
        return mPos;
    }

    bool mIsAllocated;
    Vec3f* mPos;
};
