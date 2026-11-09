#pragma once

#include "Game/NPC/Tico.hpp"

class TicoStarRing : public Tico {
public:
    TicoStarRing(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void kill() override;

    void initialize(LiveActor* pTicoGalaxy, LiveActor* pAppearActor, const char* pGalaxyName);

    void exeWait();

    const char* mGalaxyName;
    LiveActor* mAppearActor;
    LiveActor* mTicoGalaxy;
};
