#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LargeChainParts : public LiveActor {
public:
    LargeChainParts(const char* pName = "でかい鎖パーツ");

    virtual void kill() override;

    void breakChainParts();
    void initChainParts(Vec3f*, Vec3f*, Vec3f*, bool);
};
