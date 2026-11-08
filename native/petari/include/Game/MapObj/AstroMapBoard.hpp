#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class AstroMapBoard : public MapObjActor {
public:
    AstroMapBoard(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void connectToScene(const MapObjActorInitInfo&);

    void exeWait();
};
