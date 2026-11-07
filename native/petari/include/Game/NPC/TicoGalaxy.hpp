#pragma once

#include "Game/NPC/TicoFat.hpp"

class TicoStarRing;

class TicoGalaxy : public TicoFat {
public:
    TicoGalaxy(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void shootStarPiece();
    virtual void startEat();
    virtual bool enableAppear();
    virtual void initStarPieceSaveData(const TableIter&);
    virtual void addStarPieceSaveData(s32);
    virtual void disappear(bool);
    virtual void appearInformation() const;

    const char* mGalaxyName;
    TicoStarRing* mStarRing;
};
