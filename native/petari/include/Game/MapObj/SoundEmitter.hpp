#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SoundID;

class SoundEmitter : public LiveActor {
public:
    SoundEmitter(const char*);

    virtual void init(const TableIter&);
    virtual void control();

    void exePlaySound();
    void exeStandBy();

    s32 mSoundType;
};
