#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"

class Unizo;

class UnizoLauncher : public LiveActor {
public:
    UnizoLauncher(const char*);

    virtual void init(const TableIter&) override;

    void exeWait();
    void exeLaunch();

    Unizo** mUnizos;
    s32 mUnizoNum;
};
