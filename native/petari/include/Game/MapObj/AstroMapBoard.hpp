#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class AstroMapBoard : public MapObjActor {
public:
    AstroMapBoard(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void connectToScene(const MapObjActorInitInfo&) override;

    void exeWait();
};
