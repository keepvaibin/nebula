#pragma once

#include "Game/NPC/Tico.hpp"

class TicoStarRing : public Tico {
public:
    TicoStarRing(const char* pName);

    virtual void init(const TableIter&);
    virtual void kill();

    void initialize(LiveActor*, LiveActor*, const char*);

    void exeWait();

    const char* mGalaxyName;
    LiveActor* mAppearActor;
    LiveActor* mTicoGalaxy;
};
