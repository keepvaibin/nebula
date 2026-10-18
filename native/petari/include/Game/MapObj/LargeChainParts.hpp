#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LargeChainParts : public LiveActor {
public:
    LargeChainParts(const char* pName = "\202\305\202\251\202\242\215\275\203\160\201\133\203\143" /* でかい鎖パーツ */);

    virtual void kill();

    void breakChainParts();
    void initChainParts(TVec3f*, TVec3f*, TVec3f*, bool);
};
