#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SoundID;

class SoundEmitter : public LiveActor {
public:
    SoundEmitter(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;

    void exePlaySound();
    void exeStandBy();

    s32 mSoundType;
};
