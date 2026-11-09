#pragma once

#include "Game/NPC/TicoFat.hpp"

class TicoStarRing;

class TicoGalaxy : public TicoFat {
public:
    TicoGalaxy(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void shootStarPiece() override;
    virtual void startEat() override;
    virtual bool enableAppear() override;
    virtual void initStarPieceSaveData(const TableIter&) override;
    virtual void addStarPieceSaveData(s32) override;
    virtual void disappear(bool) override;
    virtual void appearInformation() const override;

    const char* mGalaxyName;
    TicoStarRing* mStarRing;
};
